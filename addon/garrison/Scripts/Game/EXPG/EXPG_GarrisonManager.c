class EXPG_GarrisonMember
{
 ref EBG_CacheMember CacheMember;
 EXPG_BuildingPlan Plan;
 int NodeIndex;
 bool Fixed;
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

 bool BindControls()
 {
  foreach (EXPG_GarrisonMember member : Members)
  {
   EBG_CacheMember cache = member.CacheMember;
   if (cache.Dead || cache.WasPlayer) { continue; }
   if (!cache.Entity || cache.Entity.GetCharacterGroup() != Group) { return false; }
   EXPG_BuildingNode node = Plan.Nodes[member.NodeIndex];
   if (member.Fixed)
   {
    if (member.Post) { continue; }
    if (!Plan.ReserveNode(cache.Entity, member.NodeIndex)) { return false; }
    member.Post = new EXPG_PostControl();
    if (!member.Post.Bind(cache.Entity, node.Position, node.Look)) { member.Post = null; return false; }
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
  foreach (EXPG_GarrisonRecord record : m_Records)
  {
   if (!record.Finished && record.Plan.Structure == building) { reason = "This building already has a garrison"; return false; }
  }
  Prepare(building);
  EXPG_BuildingPlan plan = FindPlan(building);
  reason = "Structure analysis is still running; choose the squad again shortly";
  if (!plan || !plan.Done) { return false; }
  plan.LastUsed = Now();
  if (!plan.Valid()) { reason = "Structure changed; close and reopen EXPBG Add Garrison"; return false; }
  if (!plan.Error.IsEmpty()) { reason = plan.Error; return false; }
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
  foreach (EXPG_GarrisonRecord other : m_Records)
  {
   if (!other.Finished && other.Plan.Structure == building) { return false; }
  }
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
  int fitting = PlacementCount(agents.Count(), record.Plan.Slots.Count());
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
  // Analysis ran while the picker was open. Validate all selected places again
  // before moving anyone; ignore only this squad's original spawn positions.
  if (!record.Plan.ValidateSlots(fitting, originals))
  { record.Report("Structure positions changed or are obstructed; retained as a normal squad"); record.ReleaseRequested = true; return false; }
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
  foreach (IEntity original : originals)
  {
   SCR_ChimeraCharacter actor = SCR_ChimeraCharacter.Cast(original);
   if (!EligiblePlacement(actor, group)) { record.ReleaseRequested = true; return false; }
   EXPG_GarrisonMember member = new EXPG_GarrisonMember();
   member.Plan = record.Plan;
   member.CacheMember = new EBG_CacheMember();
   member.CacheMember.Id = index + 1;
   member.CacheMember.Entity = actor;
   member.NodeIndex = record.Plan.Slots[index];
   member.Fixed = record.Plan.FixedSlots[index];
   EXPG_BuildingNode node = record.Plan.Nodes[member.NodeIndex];
   vector transform[4];
   Math3D.AnglesToMatrix(Vector(node.Look.ToYaw(), 0, 0), transform);
   transform[3] = node.Position;
   SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.GetEditableEntity(actor);
   if (editable) { editable.SetTransform(transform); }
   else { actor.SetWorldTransform(transform); }
   member.CacheMember.Position = node.Position;
   record.Members.Insert(member);
   index++;
  }
  group.EBG_UseCapturedRoster();
  record.Ready = true;
  record.Created = Now();
  if (!record.BindControls()) { record.Report("Native controls are initializing"); return true; }
  record.Report("Garrison active");
  return true;
 }

 protected bool EligiblePlacement(SCR_ChimeraCharacter actor, SCR_AIGroup group)
 {
  if (!actor || actor.GetCharacterGroup() != group || actor.IsInVehicle() || actor.EBG_WasPlayerControlled()) { return false; }
  CharacterControllerComponent controller = actor.GetCharacterController();
  return controller && !controller.IsDead() && !controller.IsUnconscious() && !controller.IsPlayerControlled();
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
  if (!record.ReleaseRequested)
  {
   foreach (EXPG_GarrisonMember member : record.Members)
   {
    if (member.CacheMember.Dead || member.CacheMember.WasPlayer) { continue; }
    SCR_ChimeraCharacter actor = member.CacheMember.Entity;
    if (!actor || actor.GetCharacterGroup() != record.Group || !record.Plan.Inside(actor.GetOrigin(), 0.25) || !record.Plan.Supported(actor.GetOrigin(), 0.2, actor) || !record.Plan.ClearBody(actor.GetOrigin(), actor.GetOrigin() + "0 0.01 0", actor))
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
  if (record.Full.BeginManagedSleep(group)) { record.Report("Full cached (prefab-default kits on restore)"); return; }
  string failure = record.Full.GetError();
  if (record.Full.HasDeletionAttempted())
  { record.Report("Full recovery retained: " + failure); return; }
  // No native deletion: abandon only this unused transaction and rebind the
  // untouched actors. Once deletion starts the transaction must survive retries.
  record.Full = null;
  record.FullRecord = null;
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
   if (!check.CacheMember.Dead && !record.Plan.Supported(record.Plan.Nodes[check.NodeIndex].Position, 0.2, check.CacheMember.Entity))
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
