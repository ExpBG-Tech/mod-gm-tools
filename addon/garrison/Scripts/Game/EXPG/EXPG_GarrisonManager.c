class EXPG_GarrisonMember
{
 ref EBG_CacheMember CacheMember;
 EXPG_BuildingPlan Plan;
 int NodeIndex;
 bool Fixed;
 // The post. NodeIndex -1 marks a post outside the sampled plan (around the
 // building or the spawn point), used once a garrisoned building is full.
 vector PostPosition;
 vector PostLook;
 int PostKind; // EXPG_Placement kind
 ref EXPG_PostControl Post;
 // Patrol controller is bound only after its native movement contract is checked.
 ref EXPG_PatrolControl Patrol;
 ref EXPG_PatrolState PatrolState;
 void ReleaseControl()
 {
  if (Plan && CacheMember) { Plan.ReleaseReservation(CacheMember.Entity); }
  if (Post) { Post.Release(); Post = null; }
  if (Patrol) { Patrol.Release(); Patrol = null; }
 }

 vector PostPoint()
 {
  if (NodeIndex >= 0 && Plan && NodeIndex < Plan.Nodes.Count()) { return Plan.Nodes[NodeIndex].Position; }
  return PostPosition;
 }
}

class EXPG_GarrisonRecord
{
 SCR_AIGroup Group;
 ref EXPG_BuildingPlan Plan;
 ref array<ref EXPG_GarrisonMember> Members = {};
 ref EBG_SimulationState Simulation;
 ref EBG_CacheGroup FullRecord;
 ref EXPG_FullCache Full;
 int FreshRequested; // Nonzero only for the explicit just-spawned editor transaction.
 bool Ready;
 bool ReleaseRequested;
 bool Finished;
 int CreatorId;
 int SafetyCursor;
 float Created;
 float ClearSince = -1;
 float RetryAt;
 string Status;

 void Report(string message)
 {
  if (Status == message) { return; }
  Status = message;
  if (Group) { Group.EXPG_Status = message; Group.EXPG_Changed(); }
  PrintFormat("[EXPG Garrison] group=%1 %2", Group, message);
 }

 void OnWaypoint(AIWaypoint waypoint)
 {
  if (!waypoint) { return; }
  ResourceName prefab = SCR_ResourceNameUtils.GetPrefabName(waypoint);
  if (prefab.Contains("AIWaypoint_ForcedMove.et")) { ReleaseRequested = true; Report("Force Move: releasing garrison"); }
 }

 void ReleaseControls()
 {
  foreach (EXPG_GarrisonMember member : Members) { member.ReleaseControl(); }
 }

 // Full caching deletes the guards and their reservations. Keep other garrisons'
 // patrols in the same building off the spots these survivors wake on.
 void ParkPosts()
 {
  Plan.Unpark(this);
  foreach (EXPG_GarrisonMember member : Members)
  {
   EBG_CacheMember cache = member.CacheMember;
   if (cache.Dead || cache.WasPlayer) { continue; }
   Plan.Park(this, member.PostPoint());
   if (vector.DistanceSq(cache.Position, member.PostPoint()) > 0.0625) { Plan.Park(this, cache.Position); }
   EXPG_PatrolState saved = member.PatrolState;
   if (saved && saved.Target >= 0 && saved.Target < Plan.Nodes.Count()) { Plan.Park(this, Plan.Nodes[saved.Target].Position); }
  }
 }

 bool BindControls()
 {
  foreach (EXPG_GarrisonMember member : Members)
  {
   EBG_CacheMember cache = member.CacheMember;
   if (cache.Dead || cache.WasPlayer) { continue; }
   if (!cache.Entity || cache.Entity.GetCharacterGroup() != Group) { return false; }
   if (member.Fixed)
   {
    if (member.Post) { continue; }
    vector postAt = member.PostPosition;
    vector postLook = member.PostLook;
    if (member.NodeIndex >= 0)
    {
     if (!Plan.ReserveNode(cache.Entity, member.NodeIndex)) { return false; }
     postAt = Plan.Nodes[member.NodeIndex].Position;
     postLook = Plan.Nodes[member.NodeIndex].Look;
    }
    member.Post = new EXPG_PostControl();
    if (!member.Post.Bind(cache.Entity, postAt, postLook)) { member.Post = null; return false; }
   }
   else
   {
    if (member.Patrol) { continue; }
    member.Patrol = new EXPG_PatrolControl();
    if (!member.Patrol.Start(cache.Entity, Plan, member.NodeIndex, member.PatrolState)) { member.Patrol = null; return false; }
   }
  }
  return true;
 }

 void FinishRelease()
 {
  ReleaseControls();
  Plan.Unpark(this);
  if (Group)
  {
   Group.EXPG_EndFreshRoster();
   Group.GetOnWaypointAdded().Remove(OnWaypoint);
   Group.EXPG_Active = false;
   Group.EXPG_Status = "Released";
   Group.EXPG_Changed();
  }
  Finished = true;
 }
}


// Use the published reservation seam; never toggle the user's EBG_Exclude flag
// or weaken Optimizer's actor eligibility checks to obtain caching.
modded class EBG_CacheManager
{
 override bool IsReserved(SCR_AIGroup group)
 {
  if (EXPG_GarrisonManager.Reserves(group)) { return true; }
  return super.IsReserved(group);
 }

 override static bool IsCacheHeld(SCR_AIGroup group)
 {
  if (EXPG_GarrisonManager.HoldsCache(group)) { return true; }
  return super.IsCacheHeld(group);
 }
}

// One chosen post for a soldier of a squad being garrisoned.
class EXPG_Placement
{
 static const int PLANNED = 0; // a selected building post
 static const int BUILDING = 1; // another verified position in the building
 static const int AROUND = 2; // a standing place around the building
 static const int SPAWN = 3; // where the engine spawned the soldier
 int NodeIndex = -1;
 bool Fixed = true;
 vector Position;
 vector Look;
 int Kind;
}

class EXPG_GarrisonManager
{
 protected static ref EXPG_GarrisonManager s_Instance;
 protected BaseWorld m_World;
 protected ref array<ref EXPG_BuildingPlan> m_Plans = {};
 protected ref array<ref EXPG_GarrisonRecord> m_Records = {};
 protected ref array<IEntity> m_Players = {};
 protected int m_PlanCursor;
 protected int m_RecordCursor;
 protected float m_NextPlayers;
 protected int m_NextFullId = -1;

 static EXPG_GarrisonManager Get()
 {
  if (!Replication.IsServer() || !GetGame() || !GetGame().InPlayMode()) { return null; }
  if (!s_Instance || s_Instance.m_World != GetGame().GetWorld())
  {
   if (s_Instance) { GetGame().GetCallqueue().Remove(s_Instance.Pump); }
   s_Instance = new EXPG_GarrisonManager();
   s_Instance.m_World = GetGame().GetWorld();
   GetGame().GetCallqueue().CallLater(s_Instance.Pump, 100, true);
  }
  return s_Instance;
 }

 static bool HasActive()
 {
  if (!s_Instance || !GetGame() || s_Instance.m_World != GetGame().GetWorld()) { return false; }
  foreach (EXPG_GarrisonRecord record : s_Instance.m_Records)
  {
   if (!record.Finished) { return true; }
  }
  return false;
 }

 // Native membership can change before the shared scheduler observes it. Retain
 // ownership by actor identity until every original cache snapshot is restored.
 static bool Reserves(SCR_AIGroup group)
 {
  if (!group || !HasActive()) { return false; }
  foreach (EXPG_GarrisonRecord record : s_Instance.m_Records)
  {
   if (record.Finished) { continue; }
   if (record.Group == group) { return true; }
   foreach (EXPG_GarrisonMember member : record.Members)
   {
    if (member.CacheMember.Entity && member.CacheMember.Entity.GetCharacterGroup() == group) { return true; }
   }
  }
  return false;
 }

 // Unit Caching's IsCacheHeld seam: a garrison holding a Simulation snapshot or a
 // Full transaction keeps its squad until the guards are plainly awake again.
 static bool HoldsCache(SCR_AIGroup group)
 {
  if (!group || !HasActive()) { return false; }
  foreach (EXPG_GarrisonRecord record : s_Instance.m_Records)
  {
   if (record.Finished || record.Group != group) { continue; }
   if (record.Simulation || record.Full) { return true; }
  }
  return false;
 }

 static bool RequestTransferRelease(SCR_AIGroup group)
 {
  if (!group || !HasActive()) { return false; }
  bool reserved;
  foreach (EXPG_GarrisonRecord record : s_Instance.m_Records)
  {
   if (record.Finished) { continue; }
   bool touched = record.Group == group;
   foreach (EXPG_GarrisonMember member : record.Members)
   {
    if (member.CacheMember.Entity && member.CacheMember.Entity.GetCharacterGroup() == group) { touched = true; break; }
   }
   if (touched) { record.ReleaseRequested = true; reserved = true; }
  }
  return reserved;
 }

 static void RequestReleaseAll()
 {
  if (!HasActive()) { return; }
  foreach (EXPG_GarrisonRecord record : s_Instance.m_Records)
  {
   if (!record.Finished) { record.ReleaseRequested = true; }
  }
 }

 float Now() { return m_World.GetWorldTime() * 0.001; }

 EXPG_BuildingPlan FindPlan(IEntity building)
 {
  foreach (EXPG_BuildingPlan plan : m_Plans)
  {
   if (plan.Structure == building) { return plan; }
  }
  return null;
 }

 EXPG_GarrisonRecord Find(SCR_AIGroup group)
 {
  foreach (EXPG_GarrisonRecord record : m_Records)
  {
   if (record.Group == group && !record.Finished) { return record; }
  }
  return null;
 }

 void Prepare(IEntity building)
 {
  if (!building || !SCR_DestructibleBuildingEntity.Cast(building)) { return; }
  EXPG_BuildingPlan existing = FindPlan(building);
  if (existing)
  {
   if (existing.Valid()) { existing.LastUsed = Now(); return; }
   foreach (EXPG_GarrisonRecord record : m_Records)
   {
    if (record.Plan == existing && !record.Finished) { return; }
   }
   m_Plans.RemoveItem(existing);
  }
  if (m_Plans.Count() >= 64) { return; }
  EXPG_BuildingPlan plan = new EXPG_BuildingPlan();
  plan.Begin(building);
  plan.LastUsed = Now();
  m_Plans.Insert(plan);
 }

 static bool SaveInProgress()
 {
  if (EBG_OptimizerControl.Preparing || EBG_CacheSnapshot.Loading) { return true; }
  SaveGameManager saving = GetGame().GetSaveGameManager();
  return saving && saving.IsBusy();
 }

 static int PlacementCount(int requested, int slots)
 {
  if (requested < 1 || requested > 32 || slots < 1) { return 0; }
  return Math.Min(requested, slots);
 }

 // Native damage destruction can remove the agent before controller.IsDead
 // updates. Vanilla kill tasks and smart-action teardown also use DESTROYED
 // as death; numeric health alone is deliberately not a casualty signal.
 static bool IsDeadActor(SCR_ChimeraCharacter actor)
 {
  if (!actor) { return false; }
  CharacterControllerComponent controller = actor.GetCharacterController();
  if (controller && controller.IsDead()) { return true; }
  DamageManagerComponent damage = actor.GetDamageManager();
  return damage && damage.IsDestroyed();
 }

 bool CanFit(IEntity building, int count, out string reason)
 {
  if (SaveInProgress()) { reason = "Finish saving and resume Unit Caching before adding a garrison"; return false; }
  reason = "Choose an infantry squad with 1 to 32 members";
  if (count < 1 || count > 32) { return false; }
  Prepare(building);
  EXPG_BuildingPlan plan = FindPlan(building);
  reason = "Structure analysis is still running; choose the squad again shortly";
  if (!plan || !plan.Done) { return false; }
  plan.LastUsed = Now();
  if (!plan.Valid()) { reason = "Structure changed; close and reopen EXPBG Add Garrison"; return false; }
  if (!plan.Error.IsEmpty()) { reason = plan.Error; return false; }
  // Adding to a garrisoned building always works: the whole squad joins, on
  // free posts first, then on extra positions (ChooseReinforcement).
  if (HasGarrison(building)) { reason = ""; return true; }
  int fitting = PlacementCount(count, plan.Slots.Count());
  reason = "Structure has no verified safe infantry positions";
  if (fitting == 0) { return false; }
  if (!plan.ValidateSlots(fitting)) { reason = "Structure positions changed or are obstructed; remove the obstruction or choose another building"; return false; }
  reason = "";
  return true;
 }

 bool Adopt(SCR_AIGroup group, IEntity building, int playerId)
 {
  return AdoptInternal(group, building, playerId, 0);
 }

 // Only the successful native editor spawn callback may call this. Existing
 // Adopt callers never authorize deleting soldiers to fit a smaller building.
 bool AdoptFresh(SCR_AIGroup group, IEntity building, int playerId, int requestedCount)
 {
  if (requestedCount < 1 || requestedCount > 32 || !group || !group.EXPG_FreshRequestMatches(requestedCount)) { return false; }
  return AdoptInternal(group, building, playerId, requestedCount);
 }

 protected bool AdoptInternal(SCR_AIGroup group, IEntity building, int playerId, int freshRequested)
 {
  if (!Replication.IsServer() || !group || !building || Find(group) || SaveInProgress()) { return false; }
  EXPG_BuildingPlan plan = FindPlan(building);
  if (!plan || !plan.Done || !plan.Valid() || !plan.Error.IsEmpty()) { return false; }
  EBG_CacheManager optimizer = EBG_CacheManager.Instance;
  if (optimizer && (optimizer.FindGroup(group) || optimizer.IsReserved(group))) { return false; }
  PersistenceSystem persistence = PersistenceSystem.GetInstance();
  if (persistence && EBG_MissionPersistence.Reserves(persistence.GetId(group))) { return false; }
  EXPG_GarrisonRecord record = new EXPG_GarrisonRecord();
  record.Group = group;
  record.Plan = plan;
  record.CreatorId = playerId;
  record.FreshRequested = freshRequested;
  record.Created = Now();
  group.EXPG_Active = true;
  group.GetOnWaypointAdded().Insert(record.OnWaypoint);
  m_Records.Insert(record);
  record.Report("Waiting for squad members");
  return true;
 }

 void Release(SCR_AIGroup group)
 {
  EXPG_GarrisonRecord record = Find(group);
  if (record) { record.ReleaseRequested = true; }
 }

 protected void Players()
 {
  m_Players.Clear();
  array<int> ids = {};
  GetGame().GetPlayerManager().GetPlayers(ids);
  foreach (int id : ids)
  {
   IEntity entity = GetGame().GetPlayerManager().GetPlayerControlledEntity(id);
   if (entity) { m_Players.Insert(entity); }
  }
 }

 protected bool Near(EXPG_GarrisonRecord record, float distance)
 {
  foreach (IEntity player : m_Players)
  {
   if (!player) { continue; }
   if (vector.DistanceSq(player.GetOrigin(), record.Plan.Origin) <= distance * distance) { return true; }
   foreach (EXPG_GarrisonMember member : record.Members)
   {
    vector point = member.CacheMember.Position;
    if (member.CacheMember.Entity) { point = member.CacheMember.Entity.GetOrigin(); }
    if (!member.CacheMember.Dead && vector.DistanceSq(player.GetOrigin(), point) <= distance * distance) { return true; }
   }
  }
  return false;
 }

 protected bool Initialize(EXPG_GarrisonRecord record)
 {
  SCR_AIGroup group = record.Group;
  if (!group.IsExpandComplete() || !group.EBG_HasCompletedInitialSpawn()) { return false; }
  array<AIAgent> agents = {};
  group.GetAgents(agents);
  if (record.FreshRequested > 0 && !group.EXPG_FreshRosterMatches(record.FreshRequested))
  { record.Report("Fresh squad membership changed; retained as normal AI"); record.ReleaseRequested = true; return false; }
  // A squad added to a building that already has a garrison deploys in full
  // (free posts first, then extra positions). Only a building's sole garrison
  // is limited to, and a fresh one trimmed to, the verified safe posts.
  bool reinforce = OtherGarrisons(record) > 0;
  int capacity = record.Plan.Slots.Count();
  if (reinforce) { capacity = agents.Count(); }
  int fitting = PlacementCount(agents.Count(), capacity);
  if (fitting == 0 || (agents.Count() > fitting && record.FreshRequested == 0) || (record.FreshRequested > 0 && agents.Count() != record.FreshRequested))
  { record.Report("Squad cannot fit safely; retained as a normal squad"); record.ReleaseRequested = true; return false; }
  array<IEntity> originals = {};
  foreach (AIAgent agent : agents)
  {
   SCR_ChimeraCharacter actor = SCR_ChimeraCharacter.Cast(agent.GetControlledEntity());
   if (!EligiblePlacement(actor, group))
   { record.Report("Squad contains unsupported occupants; retained as a normal squad"); record.ReleaseRequested = true; return false; }
   if (EBG_CacheManager.Instance && EBG_CacheManager.Instance.FindMember(actor))
   { record.Report("Squad member already has Unit Caching ownership; garrison assignment refused"); record.ReleaseRequested = true; return false; }
   originals.Insert(actor);
  }
  // Preserve the native leader when reducing a freshly spawned roster.
  IEntity leader = group.GetLeaderEntity();
  int leaderIndex = originals.Find(leader);
  if (leaderIndex > 0)
  {
   IEntity first = originals[0];
   originals[0] = leader;
   originals[leaderIndex] = first;
  }
  array<ref EXPG_Placement> placements = {};
  if (reinforce) { ChooseReinforcement(record, originals, placements); }
  else
  {
   // Analysis ran while the picker was open. Validate all selected places again
   // before moving anyone; ignore only this squad's original spawn positions.
   if (!record.Plan.ValidateSlots(fitting, originals))
   { record.Report("Structure positions changed or are obstructed; retained as a normal squad"); record.ReleaseRequested = true; return false; }
   for (int slotIndex = 0; slotIndex < fitting; slotIndex++)
   {
    EXPG_BuildingNode slotNode = record.Plan.Nodes[record.Plan.Slots[slotIndex]];
    AddPlacement(placements, null, record.Plan.Slots[slotIndex], slotNode.Position, slotNode.Look, record.Plan.FixedSlots[slotIndex], EXPG_Placement.PLANNED);
   }
  }
  if (placements.Count() < fitting)
  { record.Report("Garrison positions unavailable; retained as a normal squad"); record.ReleaseRequested = true; return false; }
  // Complete deletion preflight before the first mutation. A refused/late
  // ownership change never retries trimming or refills a casualty.
  for (int surplus = fitting; surplus < originals.Count(); surplus++)
  {
   SCR_EditableEntityComponent extra = SCR_EditableEntityComponent.GetEditableEntity(originals[surplus]);
   if (!extra || extra.HasEntityFlag(EEditableEntityFlag.NON_DELETABLE))
   { record.Report("Fresh squad surplus cannot be deleted safely; retained as normal AI"); record.ReleaseRequested = true; return false; }
  }
  bool fresh = record.FreshRequested > 0;
  record.FreshRequested = 0;
  group.EBG_UseCapturedRoster();
  for (int surplus = originals.Count() - 1; surplus >= fitting; surplus--)
  {
   if (!group.EXPG_FreshRosterMatches(originals.Count()))
   { record.Report("Fresh squad provenance changed; retained survivors as normal AI"); record.ReleaseRequested = true; return false; }
   foreach (IEntity candidate : originals)
   {
    if (!EligiblePlacement(SCR_ChimeraCharacter.Cast(candidate), group))
    { record.Report("Fresh squad ownership changed during trimming; retained survivors as normal AI"); record.ReleaseRequested = true; return false; }
   }
   SCR_EditableEntityComponent extra = SCR_EditableEntityComponent.GetEditableEntity(originals[surplus]);
   if (!extra || extra.HasEntityFlag(EEditableEntityFlag.NON_DELETABLE) || !group.EXPG_ExpectFreshRemoval(originals[surplus]) || !extra.Delete(false, false) || originals[surplus])
   { record.Report("Native surplus deletion was not acknowledged; retained survivors as normal AI"); record.ReleaseRequested = true; return false; }
   originals.RemoveOrdered(surplus);
  }
  // Deletion callbacks may modify the roster. Check the remaining identities
  // before positioning anyone, then retire this one-shot deletion authority.
  if (fresh && !group.EXPG_FreshRosterMatches(originals.Count()))
  { record.Report("Fresh squad changed after trimming; retained survivors as normal AI"); record.ReleaseRequested = true; return false; }
  group.EXPG_EndFreshRoster();
  int index;
  array<int> kinds = {0, 0, 0, 0};
  foreach (IEntity original : originals)
  {
   SCR_ChimeraCharacter actor = SCR_ChimeraCharacter.Cast(original);
   if (!EligiblePlacement(actor, group) || index >= placements.Count()) { record.ReleaseRequested = true; return false; }
   EXPG_Placement place = placements[index];
   EXPG_GarrisonMember member = new EXPG_GarrisonMember();
   member.Plan = record.Plan;
   member.CacheMember = new EBG_CacheMember();
   member.CacheMember.Id = index + 1;
   member.CacheMember.Entity = actor;
   member.NodeIndex = place.NodeIndex;
   member.Fixed = place.Fixed;
   member.PostPosition = place.Position;
   member.PostLook = place.Look;
   member.PostKind = place.Kind;
   vector transform[4];
   Math3D.AnglesToMatrix(Vector(place.Look.ToYaw(), 0, 0), transform);
   transform[3] = place.Position;
   SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.GetEditableEntity(actor);
   if (editable) { editable.SetTransform(transform); }
   else { actor.SetWorldTransform(transform); }
   member.CacheMember.Position = place.Position;
   record.Members.Insert(member);
   kinds[place.Kind] = kinds[place.Kind] + 1;
   index++;
  }
  group.EBG_UseCapturedRoster();
  record.Ready = true;
  record.Created = Now();
  ReportAdd(record, agents.Count(), kinds);
  if (!record.BindControls()) { record.Report("Native controls are initializing"); return true; }
  record.Report("Garrison active");
  return true;
 }

 protected bool EligiblePlacement(SCR_ChimeraCharacter actor, SCR_AIGroup group)
 {
  if (!actor || actor.GetCharacterGroup() != group || actor.IsInVehicle() || actor.EBG_WasPlayerControlled() || actor.EBG_HasLeftSquad()) { return false; }
  CharacterControllerComponent controller = actor.GetCharacterController();
  return controller && !controller.IsDead() && !controller.IsUnconscious() && !controller.IsPlayerControlled();
 }

 bool HasGarrison(IEntity building)
 {
  if (!building) { return false; }
  foreach (EXPG_GarrisonRecord record : m_Records)
  {
   if (!record.Finished && record.Plan.Structure == building) { return true; }
  }
  return false;
 }

 // Other unfinished garrisons of this record's building, awake or cached. With
 // occupied given, collect their posts, last positions and resumed patrol
 // targets: a Full-cached guard has no body, but it wakes on that spot.
 protected int OtherGarrisons(EXPG_GarrisonRecord record, array<vector> occupied = null)
 {
  IEntity building = record.Plan.Structure;
  if (!building) { return 0; }
  int others;
  foreach (EXPG_GarrisonRecord other : m_Records)
  {
   if (other == record || other.Finished || other.Plan.Structure != building) { continue; }
   others++;
   if (!occupied) { continue; }
   foreach (EXPG_GarrisonMember member : other.Members)
   {
    EBG_CacheMember cache = member.CacheMember;
    if (cache.Dead || cache.WasPlayer) { continue; }
    vector post = member.PostPoint();
    occupied.Insert(post);
    if (vector.DistanceSq(cache.Position, post) > 0.0625) { occupied.Insert(cache.Position); }
    if (cache.Entity && vector.DistanceSq(cache.Entity.GetOrigin(), post) > 0.0625) { occupied.Insert(cache.Entity.GetOrigin()); }
    EXPG_PatrolState saved = member.PatrolState;
    if (saved && saved.Target >= 0 && saved.Target < other.Plan.Nodes.Count()) { occupied.Insert(other.Plan.Nodes[saved.Target].Position); }
   }
  }
  return others;
 }

 // Same spacing as node reservations, so a chosen post never blocks a wake.
 protected static bool Spaced(vector point, array<vector> occupied)
 {
  foreach (vector taken : occupied)
  {
   if (EXPG_BuildingPlan.RoutesConflict(point, point, taken, taken)) { return false; }
  }
  return true;
 }

 protected static void AddPlacement(array<ref EXPG_Placement> placements, array<vector> occupied, int nodeIndex, vector point, vector look, bool fixedPost, int kind)
 {
  EXPG_Placement place = new EXPG_Placement();
  place.NodeIndex = nodeIndex;
  place.Fixed = fixedPost;
  place.Position = point;
  place.Look = look;
  place.Kind = kind;
  placements.Insert(place);
  if (occupied) { occupied.Insert(point); }
 }

 // 0 rejected without tracing, 1 traced but unsafe, 2 placed.
 protected int TryBuildingPost(EXPG_BuildingPlan plan, int nodeIndex, int kind, array<IEntity> originals, array<vector> occupied, array<ref EXPG_Placement> placements)
 {
  vector point = plan.Nodes[nodeIndex].Position;
  // Full wake re-checks Inside with a 0.25 m margin at the restored position.
  if (!plan.Inside(point, 0.35) || !Spaced(point, occupied) || plan.ReservationConflict(point)) { return 0; }
  if (!plan.Supported(point, 0.2, null, originals) || !plan.ClearBody(point, point + "0 0.01 0", null, originals)) { return 1; }
  AddPlacement(placements, occupied, nodeIndex, point, plan.Nodes[nodeIndex].Look, true, kind);
  return 2;
 }

 // A standing place on open ground beside the building, facing away from it.
 protected int TryAroundPost(EXPG_BuildingPlan plan, vector point, array<IEntity> originals, array<vector> occupied, array<ref EXPG_Placement> placements)
 {
  point[1] = m_World.GetSurfaceY(point[0], point[2]);
  if (!Spaced(point, occupied)) { return 0; }
  TraceParam foot = new TraceParam();
  foot.Start = point + "0 1.2 0";
  foot.End = point - "0 0.5 0";
  foot.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
  foot.LayerMask = EPhysicsLayerDefs.CharacterAI;
  foot.ExcludeArray = originals;
  float hit = m_World.TraceMove(foot, null);
  if (hit >= 0.999 || foot.TraceNorm[1] <= 0.65) { return 1; }
  vector stand = vector.Lerp(foot.Start, foot.End, hit) + "0 0.05 0";
  if (!Spaced(stand, occupied) || !plan.GroundSupported(stand, 0.2, null, originals) || !plan.ClearBody(stand, stand + "0 0.01 0", null, originals)) { return 1; }
  vector outward = stand - plan.Origin;
  outward[1] = 0;
  if (outward.Length() < 0.1) { outward = vector.FromYaw(plan.Angles[1]); }
  outward.Normalize();
  AddPlacement(placements, occupied, -1, stand, outward, true, EXPG_Placement.AROUND);
  return 2;
 }

 // An added squad is never refused for lack of room and never trimmed. Posts no
 // other garrison holds come first, then every other verified position in the
 // building (interior, best view first), then standing places on rings around
 // the building facing outward, and last the soldier's own spawn point. Added
 // posts are all fixed, so no added patrol crosses posts of a cached garrison.
 // Traced and examined candidates are bounded per stage.
 protected void ChooseReinforcement(EXPG_GarrisonRecord record, array<IEntity> originals, array<ref EXPG_Placement> placements)
 {
  EXPG_BuildingPlan plan = record.Plan;
  int count = originals.Count();
  array<vector> occupied = {};
  OtherGarrisons(record, occupied);
  foreach (int slot : plan.Slots)
  {
   if (placements.Count() >= count) { break; }
   TryBuildingPost(plan, slot, EXPG_Placement.PLANNED, originals, occupied, placements);
  }
  if (placements.Count() < count)
  {
   array<int> scores = {100, 90, 80, 70, 60, 0};
   array<ref array<int>> ranked = {};
   for (int rank = 0; rank < scores.Count() * 2; rank++) { ranked.Insert(new array<int>()); }
   foreach (int nodeIndex, EXPG_BuildingNode candidate : plan.Nodes)
   {
    if (!candidate.Reachable || plan.Slots.Contains(nodeIndex)) { continue; }
    int bucket = scores.Find(candidate.Score);
    if (bucket < 0) { bucket = scores.Count() - 1; }
    if (!candidate.Interior) { bucket += scores.Count(); }
    ranked[bucket].Insert(nodeIndex);
   }
   int attempts = 0;
   int examined = 0;
   foreach (array<int> nodesOfRank : ranked)
   {
    foreach (int extra : nodesOfRank)
    {
     if (placements.Count() >= count || attempts >= 256 || examined >= 4096) { break; }
     examined++;
     if (TryBuildingPost(plan, extra, EXPG_Placement.BUILDING, originals, occupied, placements) > 0) { attempts++; }
    }
   }
  }
  int probes = 0;
  int points = 0;
  for (int ring = 0; ring < 24 && placements.Count() < count && probes < 256 && points < 4096; ring++)
  {
   float gap = 1.0 + 1.25 * ring;
   float left = plan.Mins[0] - gap;
   float back = plan.Mins[2] - gap;
   float width = plan.Maxs[0] - plan.Mins[0] + gap * 2;
   float depth = plan.Maxs[2] - plan.Mins[2] + gap * 2;
   float around = (width + depth) * 2;
   int steps = Math.Floor(around / 1.25);
   for (int step = 0; step < steps && placements.Count() < count && probes < 256 && points < 4096; step++)
   {
    points++;
    float along = around * step / steps;
    vector local = Vector(left, 0, back + depth - (along - width * 2 - depth));
    if (along < width) { local = Vector(left + along, 0, back); }
    else if (along < width + depth) { local = Vector(left + width, 0, back + along - width); }
    else if (along < width * 2 + depth) { local = Vector(left + width - (along - width - depth), 0, back + depth); }
    if (TryAroundPost(plan, plan.Structure.CoordToParent(local), originals, occupied, placements) > 0) { probes++; }
   }
  }
  for (int rest = placements.Count(); rest < count; rest++)
  {
   vector spawned[4];
   originals[rest].GetWorldTransform(spawned);
   vector facing = spawned[2];
   facing[1] = 0;
   if (facing.Length() < 0.1) { facing = vector.FromYaw(plan.Angles[1]); }
   facing.Normalize();
   AddPlacement(placements, occupied, -1, spawned[3], facing, true, EXPG_Placement.SPAWN);
  }
 }

 // One server line per Add Garrison: how many soldiers were placed, and where.
 protected void ReportAdd(EXPG_GarrisonRecord record, int requested, array<int> kinds)
 {
  int squads;
  int guards;
  foreach (EXPG_GarrisonRecord other : m_Records)
  {
   if (other.Finished || other.Plan.Structure != record.Plan.Structure) { continue; }
   squads++;
   foreach (EXPG_GarrisonMember member : other.Members)
   {
    if (!member.CacheMember.Dead && !member.CacheMember.WasPlayer) { guards++; }
   }
  }
  int placed = kinds[0] + kinds[1] + kinds[2] + kinds[3];
  string building = string.Format("%1 at %2", record.Plan.Structure, record.Plan.Origin);
  string total = string.Format("%1 squads, %2 guards", squads, guards);
  PrintFormat("[EXPG Garrison] group=%1 added to building %2: placed %3 of %4 soldiers (building posts %5, more building positions %6, around the building %7, at spawn point %8); building garrison now %9", record.Group, building, placed, requested, kinds[0], kinds[1], kinds[2], kinds[3], total);
 }

 protected bool Wake(EXPG_GarrisonRecord record)
 {
  if (record.Full) { return WakeFull(record); }
  // This record is outside Optimizer's zone registry; observe casualties and
  // possession before its adapter restores the retained original actors.
  foreach (EXPG_GarrisonMember member : record.Members)
  {
   SCR_ChimeraCharacter actor = member.CacheMember.Entity;
   if (!actor) { continue; }
   CharacterControllerComponent controller = actor.GetCharacterController();
   if (IsDeadActor(actor)) { member.CacheMember.Dead = true; member.ReleaseControl(); }
   if (controller && (controller.IsPlayerControlled() || actor.EBG_WasPlayerControlled()))
   { member.CacheMember.WasPlayer = true; record.ReleaseRequested = true; member.ReleaseControl(); }
  }
  if (!record.Simulation) { return true; }
  // A deleted original cannot be restored and must never be recreated. Retire
  // its snapshot explicitly so missing actors cannot wedge Force Move forever.
  for (int i = record.Simulation.Members.Count() - 1; i >= 0; i--)
  {
   EBG_SimulationAgent saved = record.Simulation.Members[i];
   if (saved.Character) { continue; }
   if (saved.Devices) { saved.Devices.Discard(); }
   record.Simulation.Members.Remove(i);
   record.ReleaseRequested = true;
  }
  string reason;
  if (!EBG_SimulationCache.Restore(record.Simulation, reason)) { record.Report(reason); return false; }
  record.Simulation = null;
  record.Created = Now();
  record.Report("Garrison restored");
  return true;
 }

 protected bool WakeFull(EXPG_GarrisonRecord record)
 {
  EXPG_FullCache full = record.Full;
  full.ObserveAndHold();
  EBG_FullGroupPhase state = full.GetState();
  if (state == EBG_FullGroupPhase.CACHED || state == EBG_FullGroupPhase.FAILED)
  {
   if (Now() < record.RetryAt) { return false; }
   record.RetryAt = Now() + 5;
   if (!full.BeginWake()) { record.Report("Full recovery held: " + full.GetError()); return false; }
  }
  full.Poll();
  if (full.GetState() != EBG_FullGroupPhase.READY)
  {
   if (full.GetState() == EBG_FullGroupPhase.FAILED) { record.Report("Full recovery held: " + full.GetError()); }
   else { record.Report("Restoring Full survivors"); }
   return false;
  }
  // Validate actual restored positions before binding; never snap an actor to
  // its old initial slot. A failed rebind keeps group and per-actor LOD holds.
  record.Plan.Unpark(record);
  if (!record.ReleaseRequested)
  {
   foreach (EXPG_GarrisonMember member : record.Members)
   {
    if (member.CacheMember.Dead || member.CacheMember.WasPlayer) { continue; }
    SCR_ChimeraCharacter actor = member.CacheMember.Entity;
    bool standing = false;
    if (actor && member.NodeIndex >= 0) { standing = record.Plan.Inside(actor.GetOrigin(), 0.25) && record.Plan.Supported(actor.GetOrigin(), 0.2, actor); }
    else if (actor) { standing = record.Plan.GroundSupported(actor.GetOrigin(), 0.2, actor); }
    if (!actor || actor.GetCharacterGroup() != record.Group || !standing || !record.Plan.ClearBody(actor.GetOrigin(), actor.GetOrigin() + "0 0.01 0", actor))
    { record.Report("Full recovery held: restored position or membership is unsafe"); return false; }
   }
   if (!record.BindControls()) { record.Report("Full recovery held: guard controls not ready"); return false; }
  }
  else { record.ReleaseControls(); }
  // Save/Force Move first materializes every survivor, then releases normal AI
  // without reassigning it to the building. Controls already exist otherwise.
  if (!full.ReleaseRestored()) { record.Report("Full recovery held: " + full.GetError()); return false; }
  if (!record.Group) { record.ReleaseRequested = true; }
  record.Full = null;
  record.FullRecord = null;
  foreach (EXPG_GarrisonMember member : record.Members) { member.PatrolState = null; }
  record.Created = Now();
  record.ClearSince = -1;
  record.Report("Garrison restored");
  return true;
 }

 protected void TryFullSleep(EXPG_GarrisonRecord record)
 {
  SCR_AIGroup group = record.Group;
  if (!group || group.EBG_Exclude || group.IsSlave() || group.GetMaster() || group.IsCreatedByCommander() || !group.EBG_HasCompletedInitialSpawn())
  { record.Report("Full cache held: group has external ownership"); return; }
  EBG_CacheGroup captured = new EBG_CacheGroup();
  captured.Id = m_NextFullId--;
  captured.Group = group;
  captured.Anchor = record.Plan.Origin;
  foreach (EXPG_GarrisonMember member : record.Members)
  {
   EBG_CacheMember cache = member.CacheMember;
   captured.Members.Insert(cache);
   if (cache.Dead) { captured.Dead++; continue; }
   if (cache.WasPlayer || !EligiblePlacement(cache.Entity, group)) { record.Report("Full cache held: survivor ownership changed"); return; }
   string problem = EBG_SimulationCache.Unsupported(cache.Entity, false);
   if (!problem.IsEmpty()) { record.Report("Full cache held: " + problem); return; }
   if (!member.Fixed)
   {
    if (!member.Patrol) { record.Report("Full cache held: patrol state unavailable"); return; }
    member.PatrolState = member.Patrol.CaptureState();
    if (!member.PatrolState) { record.Report("Full cache held: patrol ownership changed"); return; }
    member.NodeIndex = member.PatrolState.NodeIndex;
   }
   captured.FullMembers.Insert(cache);
   captured.Alive++;
  }
  record.FullRecord = captured;
  record.Full = new EXPG_FullCache();
  record.Full.SetRecord(captured);
  record.Full.SetOwner(record);
  record.ReleaseControls();
  record.ParkPosts();
  if (record.Full.BeginManagedSleep(group)) { record.Report("Full cached (prefab-default kits on restore)"); return; }
  string failure = record.Full.GetError();
  if (record.Full.HasDeletionAttempted())
  { record.Report("Full recovery retained: " + failure); return; }
  // No native deletion: abandon only this unused transaction and rebind the
  // untouched actors. Once deletion starts the transaction must survive retries.
  record.Full = null;
  record.FullRecord = null;
  record.Plan.Unpark(record);
  if (!record.BindControls()) { record.ReleaseRequested = true; }
  record.Report("Full cache held: " + failure);
 }

 protected void TrySleep(EXPG_GarrisonRecord record)
 {
  // CDF alone bypasses Optimizer's save-admission hook. Keep originals active
  // rather than let that path serialize suppressed presentation/AI state.
  array<string> addons = {};
  GameProject.GetLoadedAddons(addons);
  if (addons.Contains("6A1876F37D65AB09") && !addons.Contains("07BC942D90324CD9"))
  { record.Report("Cache held: CDF requires the EXPBG GM Tools CDF companion for save protection"); return; }
  if (EBG_OptimizerControl.Preparing || EBG_CacheSnapshot.Loading) { return; }
  if (record.Group.EXPG_CacheMode == 2) { TryFullSleep(record); return; }
  string reason;
  record.Simulation = EBG_SimulationCache.Suspend(record.Group, reason);
  if (record.Simulation) { record.Report("Simulation cached"); }
  else { record.Report("Cache held: " + reason); }
 }

 protected void Tick(EXPG_GarrisonRecord record)
 {
  if (!record.Group) { record.ReleaseRequested = true; }
  if (!record.Plan.Valid()) { record.ReleaseRequested = true; }
  // One floor support check per group service catches damaged/removed posts
  // without rescanning the whole building or teleporting survivors into rubble.
  if (!record.Members.IsEmpty())
  {
   EXPG_GarrisonMember check = record.Members[record.SafetyCursor++ % record.Members.Count()];
   bool supported = true;
   if (!check.CacheMember.Dead && check.NodeIndex >= 0) { supported = record.Plan.Supported(record.Plan.Nodes[check.NodeIndex].Position, 0.2, check.CacheMember.Entity); }
   else if (!check.CacheMember.Dead) { supported = record.Plan.GroundSupported(check.PostPosition, 0.2, check.CacheMember.Entity); }
   if (!supported)
   { record.Report("Structure position destroyed; releasing survivors"); record.ReleaseRequested = true; }
  }
  if (record.Full)
  {
   bool wakeFull = record.ReleaseRequested || !record.Group || record.Group.EXPG_CacheMode != 2;
   if (record.Group && Near(record, record.Group.EXPG_WakeDistance)) { wakeFull = true; }
   if (record.Full.GetState() != EBG_FullGroupPhase.CACHED) { wakeFull = true; }
   if (!wakeFull || !Wake(record)) { return; }
  }
  if (record.Simulation)
  {
   bool wake = record.ReleaseRequested || !record.Group || record.Group.EXPG_CacheMode != 1;
   if (record.Group && Near(record, record.Group.EXPG_WakeDistance)) { wake = true; }
   if (!record.Simulation.Suspended) { wake = true; }
   // Death, external deletion and possession must release sleeping originals,
   // even when no player has crossed the distance boundary.
   foreach (EXPG_GarrisonMember sleeping : record.Members)
   {
   SCR_ChimeraCharacter actor = sleeping.CacheMember.Entity;
   if (sleeping.CacheMember.Dead) { continue; }
   if (IsDeadActor(actor)) { wake = true; break; }
   if (!actor || actor.GetCharacterGroup() != record.Group || actor.EBG_WasPlayerControlled()) { wake = true; record.ReleaseRequested = true; break; }
    CharacterControllerComponent controller = actor.GetCharacterController();
    if (!controller || controller.IsDead() || controller.IsPlayerControlled()) { wake = true; break; }
   }
   if (!wake || !Wake(record)) { return; }
  }
  if (record.ReleaseRequested) { record.FinishRelease(); return; }
  if (!record.Ready)
  {
   if (Now() - record.Created > 45) { record.Report("Squad initialization timed out; retained as a normal squad"); record.ReleaseRequested = true; }
   else { Initialize(record); }
   return;
  }
  // A guard who left his squad for good (an AI Surrender prisoner) is forgotten:
  // never cached, respawned or deleted; the other guards keep posts and caching.
  for (int leftIndex = record.Members.Count() - 1; leftIndex >= 0; leftIndex--)
  {
   EXPG_GarrisonMember leaver = record.Members[leftIndex];
   SCR_ChimeraCharacter leaverActor = leaver.CacheMember.Entity;
   if (leaver.CacheMember.Dead || !leaverActor || !leaverActor.EBG_HasLeftSquad() || leaverActor.GetCharacterGroup() == record.Group || IsDeadActor(leaverActor)) { continue; }
   leaver.ReleaseControl();
   record.Members.RemoveOrdered(leftIndex);
   PrintFormat("[EXPG Garrison] group=%1 guard %2 left the squad for good; forgotten, garrison kept", record.Group, leaverActor);
  }
  int alive;
  bool unsafe;
  foreach (EXPG_GarrisonMember member : record.Members)
  {
   EBG_CacheMember cached = member.CacheMember;
   if (cached.Dead) { continue; }
   SCR_ChimeraCharacter actor = cached.Entity;
   if (!actor) { record.ReleaseRequested = true; unsafe = true; continue; }
   CharacterControllerComponent controller = actor.GetCharacterController();
   if (IsDeadActor(actor)) { cached.Dead = true; member.ReleaseControl(); continue; }
   alive++;
   cached.Position = actor.GetOrigin();
   if (!controller || controller.IsPlayerControlled() || actor.EBG_WasPlayerControlled() || actor.GetCharacterGroup() != record.Group)
   { record.ReleaseRequested = true; unsafe = true; member.ReleaseControl(); continue; }
   if (member.Post && !member.Post.Tick()) { record.ReleaseRequested = true; unsafe = true; }
   if (member.Patrol && !member.Patrol.Tick()) { record.ReleaseRequested = true; unsafe = true; }
   if (!EBG_SimulationCache.Unsupported(actor, record.Group.EXPG_CacheMode == 1).IsEmpty()) { unsafe = true; }
  }
  // A casualty may remain in the native agent list during its removal callback.
  // Compare identity instead of counts, so one death does not release the guards.
  array<AIAgent> currentAgents = {};
  record.Group.GetAgents(currentAgents);
  foreach (AIAgent current : currentAgents)
  {
   bool known;
   foreach (EXPG_GarrisonMember original : record.Members)
   {
    if (original.CacheMember.Entity == current.GetControlledEntity()) { known = true; break; }
   }
   if (!known) { record.ReleaseRequested = true; break; }
  }
  if (alive == 0) { record.ReleaseRequested = true; }
  if (record.ReleaseRequested) { record.FinishRelease(); return; }
  if (!record.BindControls()) { record.Report("Guard controls unavailable; releasing survivors"); record.FinishRelease(); return; }
  vector settings = EXPG_GarrisonSettings.Normalize(Vector(record.Group.EXPG_WakeDistance, record.Group.EXPG_SleepDistance, record.Group.EXPG_CacheMode));
  record.Group.EXPG_WakeDistance = settings[0];
  record.Group.EXPG_SleepDistance = settings[1];
  record.Group.EXPG_CacheMode = settings[2];
  if (unsafe || record.Group.EXPG_CacheMode == 0 || Near(record, record.Group.EXPG_SleepDistance) || Now() - record.Created < 15)
  { record.ClearSince = -1; return; }
  if (record.ClearSince < 0) { record.ClearSince = Now(); }
  if (Now() - record.ClearSince < 30 || Now() < record.RetryAt) { return; }
  record.RetryAt = Now() + 5;
  TrySleep(record);
 }

 protected void Pump()
 {
  if (!GetGame() || GetGame().GetWorld() != m_World || !GetGame().InPlayMode())
  { if (GetGame()) { GetGame().GetCallqueue().Remove(Pump); } return; }
  if (Now() >= m_NextPlayers) { Players(); m_NextPlayers = Now() + 1; }
  if (!m_Plans.IsEmpty())
  {
   m_PlanCursor = m_PlanCursor % m_Plans.Count();
   EXPG_BuildingPlan plan = m_Plans[m_PlanCursor++];
   // About 4 ms of analysis per 100 ms pump: large buildings finish in seconds
   // instead of minutes, and one expensive step batch cannot stall a frame.
   int analysisStart = System.GetTickCount();
   while (!plan.Done && System.GetTickCount() - analysisStart < 4) { plan.Step(8); }
   bool used;
   foreach (EXPG_GarrisonRecord record : m_Records) { if (record.Plan == plan && !record.Finished) { used = true; break; } }
   if (!used && Now() - plan.LastUsed > 120) { m_Plans.RemoveItem(plan); }
  }
  int serviceCount = Math.Min(4, m_Records.Count());
  for (int i = 0; i < serviceCount && !m_Records.IsEmpty(); i++)
  {
   m_RecordCursor = m_RecordCursor % m_Records.Count();
   EXPG_GarrisonRecord record = m_Records[m_RecordCursor++];
   Tick(record);
   if (record.Finished) { m_Records.RemoveItem(record); }
  }
 }
}
