// Alarm listener bound to native invokers: a guard's threat state reaching
// ALERTED, or his squad detecting a new enemy. It only raises a flag (and keeps
// the target's position); EXPG_GarrisonManager.ServiceAlert reads it. It holds no
// pointer back to the garrison, so a late event never reaches a released record.
class EXPG_AlertListener : Managed
{
 bool Raised;
 bool Known;
 vector Where;

 void OnThreat(EAIThreatState previousState, EAIThreatState newState)
 {
  if (newState >= EAIThreatState.ALERTED) { Raised = true; }
 }

 void OnEnemyDetected(SCR_AIGroup group, SCR_AITargetInfo target, AIAgent reporter)
 {
  Raised = true;
  if (!target) { return; }
  Known = true;
  Where = target.m_vWorldPos;
 }
}

class EXPG_GarrisonMember
{
 ref EBG_CacheMember CacheMember;
 EXPG_BuildingPlan Plan;
 int NodeIndex;
 bool Fixed;
 // The post. NodeIndex -1 marks a post outside the sampled plan (around the
 // building or the spawn point), used once a garrisoned building is full. A
 // patroller's NodeIndex follows his current claim (copied every Tick).
 vector PostPosition;
 vector PostLook;
 int PostKind; // EXPG_Placement kind
 ref EXPG_PostControl Post;
 // Patrol controller is bound only after its native movement contract is checked.
 ref EXPG_PatrolControl Patrol;
 ref EXPG_PatrolState PatrolState;
 // Alarm: the listener on this guard's native threat state and the utility
 // component it was bound to (a component, so the weak pointer clears itself).
 ref EXPG_AlertListener ThreatHook;
 SCR_AIUtilityComponent ThreatUtility;
 // Seconds since a patroller's control could not start (-1 while bound): after 10 s
 // he becomes a fixed guard where he stands, never free AI (BindControls).
 float UnboundSince = -1;
 // The floor under his post gave way (logged once; caching held meanwhile).
 bool FloorLost;
 // Full restore accepted where its safety check kept failing (logged once).
 bool Forced;
 // The post he was given at placement: a displaced guard's resting spot becomes
 // his post only within MAX_DRIFT of it (Plausible).
 vector GivenPost;
 // Placement: the editor's character transform lands a frame or more after it is
 // sent. Until he stands on his post (or patrol stop) he is never bound and never
 // anchored; the move is sent again every second, MOVE_TRIES times, then every 5 s.
 bool Arrived = true;
 int MoveTries;
 float MoveSent = -1000;
 // A bound guard who came to rest on an implausible spot is being sent back.
 bool Returning;
 float ReturnSent = -1000;
 static const int MOVE_TRIES = 5;
 static const float MAX_DRIFT = 6;

 void ReleaseControl()
 {
  if (Plan && CacheMember) { Plan.ReleaseReservation(CacheMember.Entity); }
  if (Post) { Post.Release(); Post = null; }
  if (Patrol) { Patrol.Release(); Patrol = null; }
  Returning = false;
  if (CacheMember && CacheMember.Entity) { CacheMember.Entity.SetSpeedLimit(this, 1); }
  UnbindThreat();
 }

 // While he is moved onto his post and not bound yet, this member's own native
 // speed cap keeps him from walking off (formation, regroup) once the move lands.
 void HoldStill(SCR_ChimeraCharacter actor, bool hold)
 {
  if (!actor) { return; }
  if (hold) { actor.SetSpeedLimit(this, 0, true); }
  else { actor.SetSpeedLimit(this, 1); }
 }

 // A Game Master transform (the editor's owner teleport), facing look. The move
 // lands later: callers confirm OnPost before binding.
 static void Teleport(SCR_ChimeraCharacter actor, vector at, vector look)
 {
  if (!actor) { return; }
  vector transform[4];
  Math3D.AnglesToMatrix(Vector(look.ToYaw(), 0, 0), transform);
  transform[3] = at;
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.GetEditableEntity(actor);
  if (editable) { editable.SetTransform(transform); }
  else { actor.SetWorldTransform(transform); }
 }

 // Conscious, on foot and still (below 0.2 m/s): he may be moved now.
 static bool AtRest(SCR_ChimeraCharacter actor)
 {
  if (!actor || actor.IsInVehicle()) { return false; }
  CharacterControllerComponent controller = actor.GetCharacterController();
  if (!controller || controller.IsDead() || controller.IsUnconscious()) { return false; }
  return vector.DistanceSq(controller.GetVelocity(), vector.Zero) < 0.04;
 }

 // Standing on his post or patrol stop: within 1 m across and 1 m up or down.
 bool OnPost(vector at)
 {
  vector post = PostPoint();
  if (vector.DistanceXZ(at, post) > 1.0) { return false; }
  return Math.AbsFloat(at[1] - post[1]) <= 1.0;
 }

 // Where a displaced guard came to rest may become his post only when it is
 // plausible: within MAX_DRIFT of the post he was given (a patroller: of his
 // claimed stop) and, for a post in the building, on its indoor floor (inside the
 // walls, never on the roof or out in the yard) with a floor under him; for a post
 // outside, on walkable ground at about the post's height.
 bool Plausible(vector at, IEntity actor)
 {
  if (!Plan) { return false; }
  vector postRef = GivenPost;
  if (!Fixed) { postRef = PostPoint(); }
  if (vector.Distance(at, postRef) > MAX_DRIFT) { return false; }
  if (PostKind == EXPG_Placement.AROUND || PostKind == EXPG_Placement.SPAWN)
  {
   if (Math.AbsFloat(at[1] - postRef[1]) > 2.0) { return false; }
   return Plan.GroundSupported(at, 0.3, actor);
  }
  if (!Plan.Contained(at)) { return false; }
  return Plan.Supported(at, 0.3, actor);
 }

 // Not on his post (just placed, or sent back): the move is sent again after a
 // second, MOVE_TRIES times, then every 5 s (logged once).
 void ResendMove(SCR_ChimeraCharacter actor, float now, SCR_AIGroup group)
 {
  float wait = 1;
  if (MoveTries >= MOVE_TRIES) { wait = 5; }
  if (now - MoveSent < wait) { return; }
  if (MoveTries == MOVE_TRIES)
  { PrintFormat("[EXPG Garrison] group=%1 guard %2 (%3) is not on his post at %4 after %5 moves; retrying every 5 s, never bound elsewhere", group, CacheMember.Id, actor, PostPoint(), MOVE_TRIES + 1); }
  MoveTries++;
  MoveSent = now;
  HoldStill(actor, true);
  Teleport(actor, PostPoint(), PostLook);
 }

 // Back to his post: he is bound again once he stands on it (BindControls).
 void SendBack(SCR_ChimeraCharacter actor, float now)
 {
  Arrived = false;
  MoveTries = 0;
  MoveSent = now;
  HoldStill(actor, true);
  Teleport(actor, PostPoint(), PostLook);
 }

 // Idempotent. Posts are bound too: any guard's alarm alerts the building.
 void BindThreat()
 {
  if (ThreatHook || !CacheMember) { return; }
  SCR_AIUtilityComponent utility = FindUtility(CacheMember.Entity);
  if (!utility || !utility.m_ThreatSystem) { return; }
  ThreatHook = new EXPG_AlertListener();
  ThreatUtility = utility;
  utility.m_ThreatSystem.GetOnThreatStateChanged().Insert(ThreatHook.OnThreat);
 }

 // The threat system is not Managed: look it up again from the actor, never keep it.
 void UnbindThreat()
 {
  if (ThreatHook && CacheMember)
  {
   SCR_AIUtilityComponent utility = FindUtility(CacheMember.Entity);
   if (utility && utility.m_ThreatSystem) { utility.m_ThreatSystem.GetOnThreatStateChanged().Remove(ThreatHook.OnThreat); }
  }
  ThreatHook = null;
  ThreatUtility = null;
 }

 static SCR_AIUtilityComponent FindUtility(IEntity entity)
 {
  SCR_ChimeraCharacter actor = SCR_ChimeraCharacter.Cast(entity);
  if (!actor) { return null; }
  AIControlComponent control = actor.GetAIControlComponent();
  if (!control || !control.GetAIAgent()) { return null; }
  return SCR_AIUtilityComponent.Cast(control.GetAIAgent().FindComponent(SCR_AIUtilityComponent));
 }

 vector PostPoint()
 {
  if (NodeIndex >= 0 && Plan && NodeIndex < Plan.Nodes.Count()) { return Plan.Nodes[NodeIndex].Position; }
  return PostPosition;
 }

 // His post becomes the spot he stands on: the nearest free standing node within
 // 0.5 m keeps a plan reservation, otherwise an off-plan post (NodeIndex -1). He
 // keeps watching the same way (PostLook) and is never moved.
 void Anchor(vector at)
 {
  if (!Plan || !CacheMember || !CacheMember.Entity) { return; }
  Plan.ReleaseReservation(CacheMember.Entity);
  NodeIndex = -1;
  PostPosition = at;
  int node = Plan.NearestNode(at, 0.5, true);
  if (node >= 0 && Plan.ReserveNode(CacheMember.Entity, node)) { NodeIndex = node; }
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
 // Alarm (EXPG_GarrisonManager.ServiceAlert): patrollers hold windows or watch
 // points; after calm one returns to patrol every 3-8 s.
 bool AlertActive;
 float NextRelease;
 ref EXPG_AlertListener SquadHook;
 // Cache sleep waits until every patroller dwells at a stop (PatrolsSettled).
 float SettleSince = -1;
 // Why the whole garrison is released (the first request wins).
 string ReleaseReason;
 // Caching due but held since (seconds, -1 while not held); the reason is shown
 // after 30 s, a changed reason at most every 10 s (NoteHold).
 float HoldSince = -1;
 float HoldShownAt;
 bool HoldShown;
 // A Full restore whose safety check keeps failing waits at most 10 s (WakeFull).
 float WakeHeldSince = -1;

 void Report(string message)
 {
  if (Status == message) { return; }
  Status = message;
  if (Group) { Group.EXPG_Status = message; Group.EXPG_Changed(); }
  PrintFormat("[EXPG Garrison] group=%1 %2", Group, message);
 }

 // The only way to release a whole garrison: the reason is logged now and on the
 // release line, and the Game Master's status keeps it ("Released: <reason>").
 // One guard's problem never comes here; it is handled for that guard alone.
 void RequestRelease(string reason)
 {
  if (!ReleaseRequested)
  {
   ReleaseReason = reason;
   Report(reason);
  }
  ReleaseRequested = true;
 }

 // Caching is due but held: once it has been held for 30 s (when it would have
 // slept), name why in the status and the server log. A changing reason (a
 // firing guard's trigger state flickers) is shown at most every 10 s.
 void NoteHold(string reason, float now)
 {
  if (HoldSince < 0) { HoldSince = now; }
  if (reason.IsEmpty() || now - HoldSince < 30) { return; }
  string message = "Cache held: " + reason;
  if (Status == message || (HoldShown && now - HoldShownAt < 10)) { return; }
  HoldShown = true;
  HoldShownAt = now;
  Report(message);
 }

 void ClearHold()
 {
  HoldSince = -1;
  if (!HoldShown) { return; }
  HoldShown = false;
  if (Status.StartsWith("Cache held: ")) { Report("Garrison active"); }
 }

 void OnWaypoint(AIWaypoint waypoint)
 {
  if (!waypoint) { return; }
  ResourceName prefab = SCR_ResourceNameUtils.GetPrefabName(waypoint);
  if (prefab.Contains("AIWaypoint_ForcedMove.et")) { RequestRelease("Force Move waypoint given"); }
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

 // The squad's enemy detection, bound once; removed in FinishRelease.
 void BindSquadHook()
 {
  if (SquadHook || !Group) { return; }
  SCR_AIGroupUtilityComponent utility = SCR_AIGroupUtilityComponent.Cast(Group.FindComponent(SCR_AIGroupUtilityComponent));
  if (!utility || !utility.m_Perception) { return; }
  SquadHook = new EXPG_AlertListener();
  utility.m_Perception.GetOnEnemyDetectedFiltered().Insert(SquadHook.OnEnemyDetected);
 }

 void UnbindSquadHook()
 {
  if (SquadHook && Group)
  {
   SCR_AIGroupUtilityComponent utility = SCR_AIGroupUtilityComponent.Cast(Group.FindComponent(SCR_AIGroupUtilityComponent));
   if (utility && utility.m_Perception) { utility.m_Perception.GetOnEnemyDetectedFiltered().Remove(SquadHook.OnEnemyDetected); }
  }
  SquadHook = null;
 }

 // Never stops at the first problem: every guard who can be bound is bound, and one
 // who cannot (in another squad, a native control not ready, not on his post yet)
 // only makes the result false, which holds caching; nobody is released here. A
 // guard is bound only once he stands on the post he was moved to (Arrived); until
 // then the move is sent again, and nobody is anchored on the way. A possessed
 // guard is left to the player and bound again where he was left. A guard further
 // than 1.5 m from his post, or whose post another guard now holds, takes the spot
 // he stands on (Anchor) when it is plausible; otherwise, once conscious and
 // still, he is sent back to his post. A patroller whose control cannot start for
 // 10 s becomes a fixed guard where he stands (if plausible), never free AI.
 bool BindControls()
 {
  BindSquadHook();
  bool complete = true;
  float now = GetGame().GetWorld().GetWorldTime() * 0.001;
  foreach (EXPG_GarrisonMember member : Members)
  {
   EBG_CacheMember cache = member.CacheMember;
   if (cache.Dead) { continue; }
   SCR_ChimeraCharacter actor = cache.Entity;
   if (!actor) { complete = false; continue; }
   CharacterControllerComponent controller = actor.GetCharacterController();
   if (!controller) { complete = false; continue; }
   if (controller.IsPlayerControlled()) { continue; }
   if (actor.GetCharacterGroup() != Group) { complete = false; continue; }
   member.BindThreat();
   if (member.Post || member.Patrol) { continue; }
   vector origin = actor.GetOrigin();
   if (!member.Arrived)
   {
    if (!member.OnPost(origin))
    {
     complete = false;
     member.ResendMove(actor, now, Group);
     continue;
    }
    member.Arrived = true;
   }
   if (!member.Fixed)
   {
    member.Patrol = new EXPG_PatrolControl();
    if (member.Patrol.Start(actor, Plan, member.NodeIndex, member.PatrolState))
    {
     member.UnboundSince = -1;
     member.HoldStill(actor, false);
     continue;
    }
    member.Patrol = null;
    if (member.UnboundSince < 0) { member.UnboundSince = now; }
    if (now - member.UnboundSince < 10)
    {
     complete = false;
     continue;
    }
    if (!cache.WasPlayer && !member.Plausible(origin, actor))
    {
     // Off the indoor floor or far from his stop: back to his stop first.
     member.UnboundSince = -1;
     complete = false;
     if (EXPG_GarrisonMember.AtRest(actor)) { member.SendBack(actor, now); }
     continue;
    }
    member.Fixed = true;
    member.PatrolState = null;
    member.Anchor(origin);
    member.GivenPost = origin;
    PrintFormat("[EXPG Garrison] group=%1 guard %2 (%3) could not patrol for 10 s; holds where he stands", Group, cache.Id, actor);
   }
   if (vector.DistanceSq(origin, member.PostPoint()) > EXPG_PostControl.BIND_SQ)
   {
    // Left his post while unbound (a vehicle, another squad, a restore). Only a
    // plausible spot becomes his post (a possessed guard holds where the player
    // left him); otherwise he is sent back once he is conscious and still.
    if (!cache.WasPlayer && !member.Plausible(origin, actor))
    {
     complete = false;
     if (EXPG_GarrisonMember.AtRest(actor))
     {
      PrintFormat("[EXPG Garrison] group=%1 guard %2 (%3) is %4 m from his post at %5, not a plausible post; sent back to it", Group, cache.Id, actor, vector.Distance(origin, member.PostPoint()), member.PostPoint());
      member.SendBack(actor, now);
     }
     continue;
    }
    member.Anchor(origin);
   }
   if (member.NodeIndex >= 0 && !Plan.ReserveNode(actor, member.NodeIndex)) { member.Anchor(origin); }
   member.Post = new EXPG_PostControl();
   if (!member.Post.Bind(actor, member.PostPoint(), member.PostLook))
   {
    member.Post = null;
    complete = false;
    continue;
   }
   member.HoldStill(actor, false);
  }
  return complete;
 }

 void FinishRelease()
 {
  ReleaseControls();
  UnbindSquadHook();
  Plan.Unpark(this);
  string reason = ReleaseReason;
  if (reason.IsEmpty()) { reason = Status; }
  PrintFormat("[EXPG Garrison] group=%1 released (%2)", Group, reason);
  if (Group)
  {
   Group.EXPG_EndFreshRoster();
   Group.GetOnWaypointAdded().Remove(OnWaypoint);
   Group.EXPG_Active = false;
   Group.EXPG_Status = "Released: " + reason;
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

 // Zone status text only: a squad a garrison holds is cached by Garrison, not left uncached.
 override bool DescribeExternalCache(SCR_AIGroup group, out string moduleName, out string cacheState)
 {
  string garrisonState = EXPG_GarrisonManager.DescribeCache(group);
  if (garrisonState.IsEmpty()) { return super.DescribeExternalCache(group, moduleName, cacheState); }
  moduleName = "EXPBG Garrison";
  cacheState = garrisonState;
  return true;
 }
}

// One chosen post for a soldier of a squad being garrisoned.
class EXPG_Placement
{
 static const int PLANNED = 0; // a selected building post
 static const int BUILDING = 1; // another verified position in the building
 static const int AROUND = 2; // a standing place around the building
 static const int SPAWN = 3; // where the engine spawned the soldier
 static const int ROAM = 4; // an interior patroller starting on a roam stop (not fixed)
 int NodeIndex = -1;
 bool Fixed = true;
 vector Position;
 vector Look;
 int Kind;
}

// One EXPBG Add Garrison request waiting for its building's analysis before the
// squad picker opens. Every request for a building joins that building's single
// plan (a second request or a second Game Master never starts another analysis).
// The manager calls these on the server; the editor's subclass sends owner RPCs.
class EXPG_PlanWaiter
{
 IEntity Structure;
 ref EXPG_BuildingPlan Plan;
 bool Finished;
 int Percent = -1;
 bool Queued;
 float Sent = -1000;

 // Rate limited: a new percentage at most four times a second, the same one again
 // every two seconds (the Game Master's client gives up after 15 silent seconds).
 void Report(float progress, bool queued, float now)
 {
  int percent = Math.Floor(Math.Clamp(progress, 0, 1) * 100);
  if (queued == Queued && percent == Percent && now - Sent < 2) return;
  if (queued == Queued && now - Sent < 0.25) return;
  Percent = percent;
  Queued = queued;
  Sent = now;
  OnProgress(percent, queued);
 }

 // Whole percent of the analysis; queued: the plan is ready, but another Game
 // Master is choosing a garrison for this building.
 void OnProgress(int percent, bool queued) {}

 // The plan is ready (once per request). False keeps waiting (queued).
 bool OnReady()
 {
  return true;
 }

 // The analysis failed or cannot go on; the reason is for the Game Master.
 void OnFailed(string reason) {}
}

class EXPG_GarrisonManager
{
 protected static ref EXPG_GarrisonManager s_Instance;
 protected BaseWorld m_World;
 protected ref array<ref EXPG_BuildingPlan> m_Plans = {};
 protected ref array<ref EXPG_GarrisonRecord> m_Records = {};
 protected ref array<IEntity> m_Players = {};
 protected ref array<ref EXPG_PlanWaiter> m_Waiters = {};
 protected string m_PrepareFailure;
 protected int m_AnalysisCursor;
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

 // Unit Caching's DescribeExternalCache seam (zone status text only): the cache
 // state of the garrison holding this squad. Empty when no garrison holds it.
 static string DescribeCache(SCR_AIGroup group)
 {
  if (!group || !HasActive()) { return string.Empty; }
  foreach (EXPG_GarrisonRecord record : s_Instance.m_Records)
  {
   if (record.Finished) { continue; }
   bool held = record.Group == group;
   foreach (EXPG_GarrisonMember member : record.Members)
   {
    if (member.CacheMember.Entity && member.CacheMember.Entity.GetCharacterGroup() == group) { held = true; }
   }
   if (held) { return s_Instance.CacheState(record); }
  }
  return string.Empty;
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
   if (touched) { record.RequestRelease("Unit Caching regroup takes over the squad"); reserved = true; }
  }
  return reserved;
 }

 static void RequestReleaseAll()
 {
  if (!HasActive()) { return; }
  foreach (EXPG_GarrisonRecord record : s_Instance.m_Records)
  {
   if (!record.Finished) { record.RequestRelease("Unit Caching Prepare for Save (garrisons are mission-only)"); }
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

 // Plans are cached per building, never per building type: a plan is reused
 // while its building has not moved, kept while a garrison uses it and for two
 // minutes after the last request. A plan of one house cannot stand in for
 // another house of the same prefab, not even in the house's local space: the
 // entrance tests walk out to the terrain and read its height (Entrance,
 // DoorEntrance, WalkOut), open window views trace 4 m beyond the walls and see
 // neighbouring houses, walls and trees, terrain on a slope or inside a barn and
 // map objects that overlap the bounds change floors and body clearance, open or
 // closed doors and broken windows change the enclosure test, a composition
 // parent changes which entities count as the building, and the destruction
 // state changes the geometry. Re-checking only those parts would repeat nearly
 // every trace, so a per-type cache would be wrong somewhere or barely faster.
 // A Game Master waiting for a new building sees the progress instead (Wait).
 // Returns the building's plan (running, finished, or kept by a garrison although
 // the building moved: callers check Valid), or null with m_PrepareFailure.
 EXPG_BuildingPlan Prepare(IEntity building)
 {
  m_PrepareFailure = "that is not a supported building";
  if (!building || !SCR_DestructibleBuildingEntity.Cast(building)) { return null; }
  EXPG_BuildingPlan existing = FindPlan(building);
  if (existing)
  {
   if (existing.Valid()) { existing.LastUsed = Now(); return existing; }
   foreach (EXPG_GarrisonRecord record : m_Records)
   {
    if (record.Plan == existing && !record.Finished) { return existing; }
   }
   m_Plans.RemoveItem(existing);
  }
  m_PrepareFailure = "64 buildings are already being analysed or garrisoned; try again in two minutes";
  if (m_Plans.Count() >= 64) { return null; }
  EXPG_BuildingPlan plan = new EXPG_BuildingPlan();
  plan.Begin(building);
  plan.LastUsed = Now();
  m_Plans.Insert(plan);
  return plan;
 }

 // EXPBG Add Garrison: wait for the building's analysis, joining the running one
 // or starting it. A ready plan calls OnReady before this returns; otherwise the
 // pump reports progress and calls OnReady or OnFailed once. False (with the
 // reason) when no plan can be made.
 bool Wait(EXPG_PlanWaiter waiter, out string reason)
 {
  reason = "the request is incomplete";
  if (!waiter || !waiter.Structure || waiter.Finished) { return false; }
  EXPG_BuildingPlan plan = Prepare(waiter.Structure);
  reason = m_PrepareFailure;
  if (!plan) { return false; }
  reason = "";
  waiter.Plan = plan;
  m_Waiters.Insert(waiter);
  ServiceWaiter(waiter);
  if (waiter.Finished) { m_Waiters.RemoveItem(waiter); }
  return true;
 }

 // The Game Master cancelled or left; the analysis itself goes on.
 void StopWaiting(EXPG_PlanWaiter waiter)
 {
  if (!waiter) { return; }
  waiter.Finished = true;
  m_Waiters.RemoveItem(waiter);
 }

 int WaiterCount(IEntity building)
 {
  int count;
  foreach (EXPG_PlanWaiter waiter : m_Waiters)
  {
   if (!waiter.Finished && waiter.Structure == building) { count++; }
  }
  return count;
 }

 protected void ServiceWaiter(EXPG_PlanWaiter waiter)
 {
  IEntity building = waiter.Structure;
  EXPG_BuildingPlan plan = waiter.Plan;
  string failure;
  if (!building || building.IsDeleted()) { failure = "the building no longer exists"; }
  else if (!plan || FindPlan(building) != plan) { failure = "the building's analysis was discarded; use EXPBG Add Garrison again"; }
  else if (plan.Done && !plan.Valid()) { failure = "the building moved while it was analysed; use EXPBG Add Garrison again"; }
  else if (plan.Done && !plan.Error.IsEmpty()) { failure = plan.Error; }
  if (!failure.IsEmpty())
  {
   waiter.Finished = true;
   waiter.OnFailed(failure);
   return;
  }
  plan.LastUsed = Now();
  plan.WaitedAt = Now();
  if (!plan.Done) { waiter.Report(plan.Progress(), false, Now()); return; }
  // A request that showed progress sees it reach 100 %; a cached plan opens silently.
  if (waiter.Percent >= 0 && waiter.Percent < 100 && !waiter.Queued)
  {
   waiter.Percent = 100;
   waiter.Sent = Now();
   waiter.OnProgress(100, false);
  }
  if (waiter.OnReady()) { waiter.Finished = true; return; }
  waiter.Report(1, true, Now());
 }

 // Oldest request first: when a plan becomes ready, the Game Master who asked
 // first gets the squad picker and later requests for the building queue behind
 // him. A waiter's event can stop its own request (removed here or by
 // StopWaiting); the index then stays on the request that moved into its place.
 protected void ServiceWaiters()
 {
  int i = 0;
  while (i < m_Waiters.Count())
  {
   EXPG_PlanWaiter waiter = m_Waiters[i];
   if (!waiter.Finished) { ServiceWaiter(waiter); }
   if (waiter.Finished) { m_Waiters.RemoveItem(waiter); continue; }
   i++;
  }
 }

 // The plan analysed in this pump: one a Game Master waits for first (round robin
 // between several), otherwise the first unfinished plan. Finished plans never
 // take the turn, so garrisoned buildings no longer slow a new analysis down.
 protected EXPG_BuildingPlan NextAnalysis()
 {
  EXPG_BuildingPlan fallback;
  int count = m_Plans.Count();
  for (int offset = 0; offset < count; offset++)
  {
   int index = (m_AnalysisCursor + offset) % count;
   EXPG_BuildingPlan plan = m_Plans[index];
   if (plan.Done) { continue; }
   if (Now() - plan.WaitedAt < 1)
   {
    m_AnalysisCursor = index + 1;
    return plan;
   }
   if (!fallback) { fallback = plan; }
  }
  return fallback;
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
  if (record) { record.RequestRelease("Released by the Game Master"); }
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
  { record.RequestRelease("Fresh squad membership changed; retained as normal AI"); return false; }
  // A squad added to a building that already has a garrison deploys in full
  // (free posts first, then extra positions). Only a building's sole garrison
  // is limited to, and a fresh one trimmed to, the verified safe posts.
  bool reinforce = OtherGarrisons(record) > 0;
  int capacity = record.Plan.Slots.Count();
  if (reinforce) { capacity = agents.Count(); }
  int fitting = PlacementCount(agents.Count(), capacity);
  if (fitting == 0 || (agents.Count() > fitting && record.FreshRequested == 0) || (record.FreshRequested > 0 && agents.Count() != record.FreshRequested))
  { record.RequestRelease("Squad cannot fit safely; retained as a normal squad"); return false; }
  array<IEntity> originals = {};
  foreach (AIAgent agent : agents)
  {
   SCR_ChimeraCharacter actor = SCR_ChimeraCharacter.Cast(agent.GetControlledEntity());
   if (!EligiblePlacement(actor, group))
   { record.RequestRelease("Squad contains unsupported occupants; retained as a normal squad"); return false; }
   if (EBG_CacheManager.Instance && EBG_CacheManager.Instance.FindMember(actor))
   { record.RequestRelease("Squad member already has Unit Caching ownership; garrison assignment refused"); return false; }
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
   { record.RequestRelease("Structure positions changed or are obstructed; retained as a normal squad"); return false; }
   for (int slotIndex = 0; slotIndex < fitting; slotIndex++)
   {
    EXPG_BuildingNode slotNode = record.Plan.Nodes[record.Plan.Slots[slotIndex]];
    bool slotFixed = record.Plan.FixedSlots[slotIndex];
    int slotKind = EXPG_Placement.PLANNED;
    vector slotLook = slotNode.Look;
    if (!slotFixed)
    {
     slotKind = EXPG_Placement.ROAM;
     slotLook = slotNode.WatchLook;
    }
    AddPlacement(placements, null, record.Plan.Slots[slotIndex], slotNode.Position, slotLook, slotFixed, slotKind);
   }
  }
  if (placements.Count() < fitting)
  { record.RequestRelease("Garrison positions unavailable; retained as a normal squad"); return false; }
  // Complete deletion preflight before the first mutation. A refused/late
  // ownership change never retries trimming or refills a casualty.
  for (int surplus = fitting; surplus < originals.Count(); surplus++)
  {
   SCR_EditableEntityComponent extra = SCR_EditableEntityComponent.GetEditableEntity(originals[surplus]);
   if (!extra || extra.HasEntityFlag(EEditableEntityFlag.NON_DELETABLE))
   { record.RequestRelease("Fresh squad surplus cannot be deleted safely; retained as normal AI"); return false; }
  }
  bool fresh = record.FreshRequested > 0;
  record.FreshRequested = 0;
  group.EBG_UseCapturedRoster();
  for (int surplus = originals.Count() - 1; surplus >= fitting; surplus--)
  {
   if (!group.EXPG_FreshRosterMatches(originals.Count()))
   { record.RequestRelease("Fresh squad provenance changed; retained survivors as normal AI"); return false; }
   foreach (IEntity candidate : originals)
   {
    if (!EligiblePlacement(SCR_ChimeraCharacter.Cast(candidate), group))
    { record.RequestRelease("Fresh squad ownership changed during trimming; retained survivors as normal AI"); return false; }
   }
   SCR_EditableEntityComponent extra = SCR_EditableEntityComponent.GetEditableEntity(originals[surplus]);
   if (!extra || extra.HasEntityFlag(EEditableEntityFlag.NON_DELETABLE) || !group.EXPG_ExpectFreshRemoval(originals[surplus]) || !extra.Delete(false, false) || originals[surplus])
   { record.RequestRelease("Native surplus deletion was not acknowledged; retained survivors as normal AI"); return false; }
   originals.RemoveOrdered(surplus);
  }
  // Deletion callbacks may modify the roster. Check the remaining identities
  // before positioning anyone, then retire this one-shot deletion authority.
  if (fresh && !group.EXPG_FreshRosterMatches(originals.Count()))
  { record.RequestRelease("Fresh squad changed after trimming; retained survivors as normal AI"); return false; }
  group.EXPG_EndFreshRoster();
  int index;
  array<int> kinds = {0, 0, 0, 0, 0};
  foreach (IEntity original : originals)
  {
   SCR_ChimeraCharacter actor = SCR_ChimeraCharacter.Cast(original);
   if (!EligiblePlacement(actor, group) || index >= placements.Count()) { record.RequestRelease("Squad membership changed while taking posts; retained survivors as normal AI"); return false; }
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
   member.GivenPost = place.Position;
   // The move lands a frame or more later: BindControls binds him only once he
   // stands on this post, and sends the move again until he does.
   member.Arrived = false;
   member.MoveSent = Now();
   member.HoldStill(actor, true);
   EXPG_GarrisonMember.Teleport(actor, place.Position, place.Look);
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

 // Same spacing as node reservations, so a chosen post never blocks a wake, and
 // the planner's post spacing, so added soldiers never bunch up on one spot.
 protected static bool Spaced(vector point, array<vector> occupied)
 {
  foreach (vector taken : occupied)
  {
   if (EXPG_BuildingPlan.RoutesConflict(point, point, taken, taken)) { return false; }
   if (EXPG_BuildingPlan.Crowded(point, taken, EXPG_BuildingPlan.POST_SPACING)) { return false; }
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
  EXPG_BuildingNode node = plan.Nodes[nodeIndex];
  vector point = node.Position;
  // Indoor floor only: porches, entrance steps and stair flights never hold a post,
  // nor does a door's swing or doorway.
  if (!node.Reachable || !node.Interior || node.Stair || node.DoorBlock) { return 0; }
  // Full wake re-checks Inside with a 0.25 m margin at the restored position.
  if (!plan.Inside(point, 0.35) || !Spaced(point, occupied) || plan.ReservationConflict(point)) { return 0; }
  if (!plan.Supported(point, 0.2, null, originals) || !plan.ClearBody(point, point + "0 0.01 0", null, originals)) { return 1; }
  AddPlacement(placements, occupied, nodeIndex, point, plan.Nodes[nodeIndex].Look, true, kind);
  return 2;
 }

 // A standing place on open ground beside the building, facing away from it, never
 // on the step in front of a door (door zone).
 protected int TryAroundPost(EXPG_BuildingPlan plan, vector point, array<IEntity> originals, array<vector> occupied, array<ref EXPG_Placement> placements)
 {
  point[1] = m_World.GetSurfaceY(point[0], point[2]);
  if (!Spaced(point, occupied) || plan.InAnyDoorZone(point)) { return 0; }
  TraceParam foot = new TraceParam();
  foot.Start = point + "0 1.2 0";
  foot.End = point - "0 0.5 0";
  foot.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
  foot.LayerMask = EPhysicsLayerDefs.CharacterAI;
  foot.ExcludeArray = originals;
  float hit = m_World.TraceMove(foot, null);
  if (hit >= 0.999 || foot.TraceNorm[1] <= 0.65) { return 1; }
  vector stand = vector.Lerp(foot.Start, foot.End, hit) + "0 0.05 0";
  if (!Spaced(stand, occupied) || plan.InAnyDoorZone(stand) || !plan.GroundSupported(stand, 0.2, null, originals) || !plan.ClearBody(stand, stand + "0 0.01 0", null, originals)) { return 1; }
  vector outward = stand - plan.Origin;
  outward[1] = 0;
  if (outward.Length() < 0.1) { outward = vector.FromYaw(plan.Angles[1]); }
  outward.Normalize();
  AddPlacement(placements, occupied, -1, stand, outward, true, EXPG_Placement.AROUND);
  return 2;
 }

 // Interior patrollers on free roam stops, in plan order (spread over the
 // storeys). Free-room rule: a patroller joins a walk group only while at least
 // KeepFree of its stops stay free after he takes his, so patrollers can always
 // move on. Bounded: at most 96 traced stops.
 protected void AddPatrollers(EXPG_BuildingPlan plan, array<IEntity> originals, array<vector> occupied, array<ref EXPG_Placement> placements, int count)
 {
  if (placements.Count() >= count || plan.RoamStops.IsEmpty()) { return; }
  array<int> freeStops = {};
  for (int g = 0; g < plan.WalkGroupCount(); g++) { freeStops.Insert(0); }
  foreach (int stop : plan.RoamStops)
  {
   int area = plan.Nodes[stop].WalkGroup;
   if (area < 0 || area >= freeStops.Count()) { continue; }
   if (plan.StopFree(stop) && Spaced(plan.Nodes[stop].Position, occupied)) { freeStops[area] = freeStops[area] + 1; }
  }
  int traced = 0;
  foreach (int roam : plan.RoamStops)
  {
   if (placements.Count() >= count || traced >= 96) { break; }
   EXPG_BuildingNode node = plan.Nodes[roam];
   int walkGroup = node.WalkGroup;
   if (walkGroup < 0 || walkGroup >= freeStops.Count() || freeStops[walkGroup] - 1 < plan.KeepFree(walkGroup)) { continue; }
   if (!plan.StopFree(roam) || !Spaced(node.Position, occupied) || !plan.Inside(node.Position, 0.35)) { continue; }
   traced++;
   if (!plan.Supported(node.Position, 0.2, null, originals) || !plan.ClearBody(node.Position, node.Position + "0 0.01 0", null, originals)) { continue; }
   freeStops[walkGroup] = freeStops[walkGroup] - 1;
   AddPlacement(placements, occupied, roam, node.Position, node.WatchLook, false, EXPG_Placement.ROAM);
  }
 }

 // An added squad is never refused for lack of room and never trimmed. Fixed
 // posts no other garrison holds come first (in plan order: windows, then outer
 // doors, with the storeys taking turns), then interior patrol starts under the
 // free-room rule (AddPatrollers), then other verified watch positions (windows,
 // doors, entrances, stairs; best view first, storeys taking turns) that keep
 // clear of every patrol stop, so patrollers always have free stops, then
 // standing places on rings around the building facing outward (at most 24.75 m
 // beyond its walls), and last the soldier's own spawn point. Every post keeps the
 // planner's post spacing and stays out of door zones. Patrollers claim stops
 // clear of every other claim and of a cached garrison's parked posts. Traced and
 // examined candidates are bounded per stage.
 protected void ChooseReinforcement(EXPG_GarrisonRecord record, array<IEntity> originals, array<ref EXPG_Placement> placements)
 {
  EXPG_BuildingPlan plan = record.Plan;
  int count = originals.Count();
  array<vector> occupied = {};
  OtherGarrisons(record, occupied);
  foreach (int slotOrder, int slot : plan.Slots)
  {
   if (placements.Count() >= count) { break; }
   // Patrol starts of the plan are not fixed posts; patrollers come next.
   if (!plan.FixedSlots[slotOrder]) { continue; }
   TryBuildingPost(plan, slot, EXPG_Placement.PLANNED, originals, occupied, placements);
  }
  AddPatrollers(plan, originals, occupied, placements, count);
  if (placements.Count() < count)
  {
   // Extra posts never take a patrol stop's room: spaced from every stop too.
   array<vector> keepClear = {};
   keepClear.Copy(occupied);
   foreach (int roamStop : plan.RoamStops) { keepClear.Insert(plan.Nodes[roamStop].Position); }
   int firstExtra = placements.Count();
   array<int> scores = {};
   scores.Insert(EXPG_BuildingPlan.SCORE_SENTINEL);
   scores.Insert(EXPG_BuildingPlan.SCORE_WINDOW);
   scores.Insert(EXPG_BuildingPlan.SCORE_DOOR);
   scores.Insert(EXPG_BuildingPlan.SCORE_ENTRANCE);
   scores.Insert(EXPG_BuildingPlan.SCORE_STAIRS);
   array<ref array<int>> ranked = {};
   for (int rank = 0; rank < scores.Count(); rank++) { ranked.Insert(new array<int>()); }
   foreach (int nodeIndex, EXPG_BuildingNode candidate : plan.Nodes)
   {
    // Outside the walls (porch, steps, under the eaves) or on a stair flight is
    // never a building post; only the explicit ring stage below stands outside.
    if (!candidate.Reachable || !candidate.Interior || candidate.Stair || candidate.DoorBlock || plan.Slots.Contains(nodeIndex)) { continue; }
    int bucket = scores.Find(candidate.Score);
    if (bucket < 0) { continue; }
    ranked[bucket].Insert(nodeIndex);
   }
   int attempts = 0;
   int examined = 0;
   foreach (array<int> nodesOfRank : ranked)
   {
    plan.InterleaveStoreys(nodesOfRank);
    foreach (int extra : nodesOfRank)
    {
     if (placements.Count() >= count || attempts >= 256 || examined >= 4096) { break; }
     examined++;
     if (TryBuildingPost(plan, extra, EXPG_Placement.BUILDING, originals, keepClear, placements) > 0) { attempts++; }
    }
   }
   for (int placed = firstExtra; placed < placements.Count(); placed++) { occupied.Insert(placements[placed].Position); }
  }
  // Around posts stay close: ring gaps 1 m to 24.75 m beyond the walls.
  int probes = 0;
  int points = 0;
  for (int ring = 0; ring < 20 && placements.Count() < count && probes < 256 && points < 4096; ring++)
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
  int placed = kinds[0] + kinds[1] + kinds[2] + kinds[3] + kinds[4];
  string building = string.Format("%1 at %2", record.Plan.Structure, record.Plan.Origin);
  string total = string.Format("%1 squads, %2 guards", squads, guards);
  string placedAt = string.Format("building posts %1, more building positions %2, patrolling inside %3, around the building %4, at spawn point %5", kinds[0], kinds[1], kinds[4], kinds[2], kinds[3]);
  PrintFormat("[EXPG Garrison] group=%1 added to building %2: placed %3 of %4 soldiers (%5); building garrison now %6", record.Group, building, placed, requested, placedAt, total);
 }

 protected bool Wake(EXPG_GarrisonRecord record)
 {
  if (record.Full) { return WakeFull(record); }
  // This record is outside Optimizer's zone registry; observe casualties and
  // possession before its adapter restores the retained original actors. A
  // possessed guard is that guard's matter only: the others wake on their posts.
  foreach (EXPG_GarrisonMember member : record.Members)
  {
   SCR_ChimeraCharacter actor = member.CacheMember.Entity;
   if (!actor) { continue; }
   CharacterControllerComponent controller = actor.GetCharacterController();
   if (IsDeadActor(actor)) { member.CacheMember.Dead = true; member.ReleaseControl(); }
   if (controller && (controller.IsPlayerControlled() || actor.EBG_WasPlayerControlled()))
   { member.CacheMember.WasPlayer = true; member.ReleaseControl(); }
  }
  if (!record.Simulation) { return true; }
  // A deleted original cannot be restored and must never be recreated. Retire
  // its snapshot explicitly so missing actors cannot wedge Force Move forever;
  // Tick marks him dead, and the other guards keep their posts.
  for (int i = record.Simulation.Members.Count() - 1; i >= 0; i--)
  {
   EBG_SimulationAgent saved = record.Simulation.Members[i];
   if (saved.Character) { continue; }
   if (saved.Devices) { saved.Devices.Discard(); }
   record.Simulation.Members.Remove(i);
  }
  string reason;
  if (!EBG_SimulationCache.Restore(record.Simulation, reason)) { record.Report(reason); return false; }
  record.Simulation = null;
  record.Created = Now();
  // Patrollers kept their controls while cached: each stands at his stop for a
  // fresh dwell, then walks again.
  foreach (EXPG_GarrisonMember resumed : record.Members)
  {
   if (resumed.Patrol) { resumed.Patrol.RestartDwell(); }
  }
  ReleaseSettle(record);
  record.AlertActive = false;
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
   // A survivor another squad took meanwhile is forgotten alone.
   ForgetLeavers(record);
   if (record.WakeHeldSince < 0) { record.WakeHeldSince = Now(); }
   bool patient = Now() - record.WakeHeldSince < 10;
   foreach (EXPG_GarrisonMember member : record.Members)
   {
    if (member.CacheMember.Dead || member.CacheMember.WasPlayer || member.Forced) { continue; }
    SCR_ChimeraCharacter actor = member.CacheMember.Entity;
    // Not recreated (deleted meanwhile, or abandoned by the operator): gone for
    // good and never respawned; the others wake on their posts.
    if (!actor)
    {
     member.CacheMember.Dead = true;
     member.ReleaseControl();
     PrintFormat("[EXPG Garrison] group=%1 guard %2 was not restored; never respawned, the other guards keep their posts", record.Group, member.CacheMember.Id);
     continue;
    }
    if (actor.GetCharacterGroup() != record.Group)
    { record.Report(string.Format("Full recovery held: guard %1 is not back in the squad yet", member.CacheMember.Id)); return false; }
    bool standing = false;
    if (member.NodeIndex >= 0) { standing = record.Plan.Inside(actor.GetOrigin(), 0.25) && record.Plan.Supported(actor.GetOrigin(), 0.2, actor); }
    else { standing = record.Plan.GroundSupported(actor.GetOrigin(), 0.2, actor); }
    if (standing && record.Plan.ClearBody(actor.GetOrigin(), actor.GetOrigin() + "0 0.01 0", actor)) { continue; }
    // A corpse or debris on his spot: after 10 s he is better awake and bound
    // where he stands (never moved) than frozen for good.
    if (patient)
    { record.Report(string.Format("Full recovery held: guard %1's restored position is unsafe (floor or body clearance)", member.CacheMember.Id)); return false; }
    member.Forced = true;
    PrintFormat("[EXPG Garrison] group=%1 guard %2 restored where the safety check failed for 10 s; holds where he stands", record.Group, member.CacheMember.Id);
   }
   if (!record.BindControls()) { record.Report("Full recovery held: guard controls not ready"); return false; }
  }
  else { record.ReleaseControls(); }
  // Save/Force Move first materializes every survivor, then releases normal AI
  // without reassigning it to the building. Controls already exist otherwise.
  if (!full.ReleaseRestored()) { record.Report("Full recovery held: " + full.GetError()); return false; }
  if (!record.Group) { record.RequestRelease("Squad deleted"); }
  record.Full = null;
  record.FullRecord = null;
  record.WakeHeldSince = -1;
  foreach (EXPG_GarrisonMember member : record.Members)
  {
   member.PatrolState = null;
   member.Forced = false;
  }
  record.Created = Now();
  record.ClearSince = -1;
  ReleaseSettle(record);
  record.AlertActive = false;
  record.Report("Garrison restored");
  return true;
 }

 protected void TryFullSleep(EXPG_GarrisonRecord record)
 {
  SCR_AIGroup group = record.Group;
  string ownership = SquadProblem(group);
  if (!ownership.IsEmpty()) { record.Report("Full cache held: " + ownership); return; }
  EBG_CacheGroup captured = new EBG_CacheGroup();
  captured.Id = m_NextFullId--;
  captured.Group = group;
  captured.Anchor = record.Plan.Origin;
  foreach (EXPG_GarrisonMember member : record.Members)
  {
   EBG_CacheMember cache = member.CacheMember;
   captured.Members.Insert(cache);
   if (cache.Dead) { captured.Dead++; continue; }
   string survivor = SurvivorProblem(cache, group);
   if (!survivor.IsEmpty()) { record.Report(string.Format("Full cache held: guard %1 %2", cache.Id, survivor)); return; }
   string problem = EBG_SimulationCache.Unsupported(cache.Entity, false);
   if (!problem.IsEmpty()) { record.Report(string.Format("Full cache held: guard %1: %2", cache.Id, problem)); return; }
   if (!member.Fixed)
   {
    if (!member.Patrol) { record.Report(string.Format("Full cache held: guard %1's patrol is not bound yet", cache.Id)); return; }
    member.PatrolState = member.Patrol.CaptureState();
    if (!member.PatrolState) { record.Report(string.Format("Full cache held: guard %1's patrol ownership changed", cache.Id)); return; }
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
  // Untouched originals: bind again; a guard not bound yet is retried every Tick.
  record.BindControls();
  record.Report("Full cache held: " + failure);
 }

 // Why the squad itself cannot be Full cached; empty when it can.
 protected string SquadProblem(SCR_AIGroup group)
 {
  if (!group) { return "squad deleted"; }
  if (group.EBG_Exclude) { return "the squad is excluded from Unit Caching"; }
  if (group.IsSlave() || group.GetMaster()) { return "the squad is attached to another group"; }
  if (group.IsCreatedByCommander()) { return "the squad belongs to the commander"; }
  if (!group.EBG_HasCompletedInitialSpawn()) { return "the squad has not finished spawning"; }
  return string.Empty;
 }

 // Why one living guard cannot be Full cached; empty when he can.
 protected string SurvivorProblem(EBG_CacheMember cache, SCR_AIGroup group)
 {
  SCR_ChimeraCharacter actor = cache.Entity;
  if (!actor) { return "is missing"; }
  if (actor.GetCharacterGroup() != group) { return "is in another squad"; }
  if (cache.WasPlayer || actor.EBG_WasPlayerControlled()) { return "was possessed by a player (a once-possessed soldier is never cached)"; }
  if (actor.IsInVehicle()) { return "is in a vehicle"; }
  if (actor.EBG_HasLeftSquad()) { return "left the squad"; }
  CharacterControllerComponent controller = actor.GetCharacterController();
  if (!controller || controller.IsDead()) { return "is dying"; }
  if (controller.IsUnconscious()) { return "is unconscious"; }
  if (controller.IsPlayerControlled()) { return "is possessed by a player"; }
  return string.Empty;
 }

 protected void TrySleep(EXPG_GarrisonRecord record)
 {
  // CDF alone bypasses Optimizer's save-admission hook. Keep originals active
  // rather than let that path serialize suppressed presentation/AI state.
  array<string> addons = {};
  GameProject.GetLoadedAddons(addons);
  if (addons.Contains("6A1876F37D65AB09") && !addons.Contains("07BC942D90324CD9"))
  { record.Report("Cache held: CDF requires the EXPBG GM Tools CDF companion for save protection"); return; }
  if (EBG_OptimizerControl.Preparing || EBG_CacheSnapshot.Loading) { record.Report("Cache held: a Unit Caching save or load is in progress"); return; }
  // Patrollers first finish their walk at a stop (silently: not a refusal).
  if (!PatrolsSettled(record)) { return; }
  if (CacheModeInUse(record.Group) == 2)
  {
   TryFullSleep(record);
   // A held sleep lets the patrol go on until the next attempt.
   if (!record.Full) { ReleaseSettle(record); }
   return;
  }
  string reason;
  record.Simulation = EBG_SimulationCache.Suspend(record.Group, reason);
  // Full chosen, CDF loaded: one status line per sleep names the fallback.
  string cachedStatus = "Simulation cached";
  if (record.Group.EXPG_CacheMode == 2) { cachedStatus = "Simulation cached (CDF loaded)"; }
  if (record.Simulation) { record.Report(cachedStatus); }
  else
  {
   record.Report("Cache held: " + reason);
   ReleaseSettle(record);
  }
 }

 // Cache sleep waits until every living patroller dwells at his claimed stop:
 // no new walks start; after 20 s a walker stops at a stop near him.
 protected bool PatrolsSettled(EXPG_GarrisonRecord record)
 {
  if (record.SettleSince < 0) { record.SettleSince = Now(); }
  bool settled = true;
  foreach (EXPG_GarrisonMember member : record.Members)
  {
   if (member.CacheMember.Dead || !member.Patrol) { continue; }
   member.Patrol.SetSettle(true);
   if (member.Patrol.Settled()) { continue; }
   settled = false;
   if (Now() - record.SettleSince >= 20) { member.Patrol.ForceSettle(); }
  }
  return settled;
 }

 protected void ReleaseSettle(EXPG_GarrisonRecord record)
 {
  if (record.SettleSince < 0) { return; }
  record.SettleSince = -1;
  foreach (EXPG_GarrisonMember member : record.Members)
  {
   if (member.Patrol) { member.Patrol.SetSettle(false); }
  }
 }

 // CDF Game Master Save (6A1876F37D65AB09) cannot keep Garrison Full survivors.
 // Saves are refused while any garrison is active, and a clear-before-load would
 // delete the retained empty group of a Full-cached garrison (the record could
 // never wake and would block every later save) or respawn its survivors into the
 // loaded scene. So a garrison set to Full caches in Simulation while CDF is
 // loaded: the original soldiers stay on their posts with AI paused, a CDF load
 // deletes them like any squad and nobody is recreated. Read once per mission.
 protected int m_CdfLoaded = -1;
 protected bool CdfLoaded()
 {
  if (m_CdfLoaded < 0)
  {
   array<string> addons = {};
   GameProject.GetLoadedAddons(addons);
   m_CdfLoaded = 0;
   if (addons.Contains("6A1876F37D65AB09")) { m_CdfLoaded = 1; }
  }
  return m_CdfLoaded == 1;
 }

 // The cache mode a garrison runs: 0 Off, 1 Simulation, 2 Full. The GM's choice
 // (EXPG_CacheMode) is kept; only its use changes while CDF is loaded.
 int CacheModeInUse(SCR_AIGroup group)
 {
  int mode;
  if (group) { mode = group.EXPG_CacheMode; }
  if (mode == 2 && CdfLoaded()) { mode = 1; }
  return mode;
 }

 // Short cache state of one garrison for the Unit Caching zone status.
 string CacheState(EXPG_GarrisonRecord record)
 {
  string text = "awake on their posts";
  if (record.Full && record.Full.GetState() == EBG_FullGroupPhase.CACHED) { text = "Full cached"; }
  else if (record.Full) { text = "restoring Full survivors"; }
  else if (record.Simulation && record.Simulation.Suspended) { text = "Simulation cached"; }
  else if (record.Simulation) { text = "restoring from Simulation"; }
  else if (!record.Ready) { text = "taking their posts"; }
  else if (!record.Group || record.Group.EXPG_CacheMode == 0) { text = "awake, caching Off"; }
  else if (record.Status.Contains("Cache held") || record.Status.Contains("cache held")) { text = "awake, caching held (see the garrison status)"; }
  if (record.Simulation && record.Group && record.Group.EXPG_CacheMode == 2 && CdfLoaded()) { text += " (CDF loaded)"; }
  return text;
 }

 // Seconds without any guard of the building at ALERTED or above before the
 // patrollers return to patrol.
 static const float CALM_SECONDS = 60;

 // Alarm, building-wide: the native listeners' flags (threat state ALERTED,
 // squad enemy detection) and a poll of at most 32 threat states refresh the
 // building's last alarm. While it is recent, at most two patrollers per Tick
 // claim the nearest free window (facing the threat when known), else a free
 // door or stairs watch point within 12 steps, else hold the stop they have;
 // holders without a window retry every 5 s. Calm: one holder every 3-8 s goes
 // back to patrol. No timers, no world scans.
 protected void ServiceAlert(EXPG_GarrisonRecord record)
 {
  EXPG_BuildingPlan plan = record.Plan;
  float now = Now();
  bool alerted = false;
  EXPG_AlertListener squad = record.SquadHook;
  if (squad && squad.Raised)
  {
   alerted = true;
   squad.Raised = false;
   if (squad.Known)
   {
    plan.ThreatPos = squad.Where;
    plan.ThreatAt = now;
    squad.Known = false;
   }
  }
  int polled = 0;
  foreach (EXPG_GarrisonMember member : record.Members)
  {
   if (member.CacheMember.Dead || polled >= 32) { continue; }
   polled++;
   if (member.ThreatHook && member.ThreatHook.Raised)
   {
    alerted = true;
    member.ThreatHook.Raised = false;
   }
   SCR_AIUtilityComponent utility = member.ThreatUtility;
   if (utility && utility.m_ThreatSystem && utility.m_ThreatSystem.GetState() >= EAIThreatState.ALERTED) { alerted = true; }
  }
  if (alerted) { plan.LastAlert = now; }
  if (now - plan.LastAlert < CALM_SECONDS)
  {
   record.AlertActive = true;
   vector threat;
   bool known = ThreatSide(record, threat);
   int handled = 0;
   foreach (EXPG_GarrisonMember patroller : record.Members)
   {
    EXPG_PatrolControl patrol = patroller.Patrol;
    if (handled >= 2) { break; }
    if (patroller.CacheMember.Dead || !patrol) { continue; }
    if (!patrol.InAlert())
    {
     handled++;
     PlaceAlert(plan, patrol, threat, known, false);
     continue;
    }
    if (patrol.HoldsWindow() || !patrol.RetryDue()) { continue; }
    handled++;
    patrol.DelayRetry(5);
    PlaceAlert(plan, patrol, threat, known, true);
   }
   return;
  }
  if (!record.AlertActive || now < record.NextRelease) { return; }
  foreach (EXPG_GarrisonMember holder : record.Members)
  {
   if (holder.CacheMember.Dead || !holder.Patrol || !holder.Patrol.InAlert()) { continue; }
   holder.Patrol.Calm();
   record.NextRelease = now + Math.RandomFloat(3, 8);
   return;
  }
  record.AlertActive = false;
 }

 // Threat side: the squad's known targets (at most 8, owned by its perception),
 // else the last detected enemy position of the building (30 s), else unknown.
 protected bool ThreatSide(EXPG_GarrisonRecord record, out vector threat)
 {
  threat = record.Plan.ThreatPos;
  bool known = Now() - record.Plan.ThreatAt < 30;
  if (!record.Group) { return known; }
  SCR_AIGroupUtilityComponent utility = SCR_AIGroupUtilityComponent.Cast(record.Group.FindComponent(SCR_AIGroupUtilityComponent));
  if (!utility || !utility.m_Perception) { return known; }
  vector sum = vector.Zero;
  int count = 0;
  foreach (SCR_AITargetInfo target : utility.m_Perception.m_aTargets)
  {
   if (!target) { continue; }
   sum = sum + target.m_vWorldPos;
   count++;
   if (count >= 8) { break; }
  }
  if (count == 0) { return known; }
  threat = sum * (1.0 / count);
  return true;
 }

 // One patroller, one breadth-first pass: the best of three free windows (steps
 // x 0.75, plus 3 x (1 - facing) towards a known threat; a window looking more
 // than 107 degrees away from it is skipped), else the nearest free door or
 // stairs watch point within 12 steps, else hold. Each claim is unique and 1.5 m
 // from every other.
 protected void PlaceAlert(EXPG_BuildingPlan plan, EXPG_PatrolControl patrol, vector threat, bool known, bool windowOnly)
 {
  SCR_ChimeraCharacter actor = patrol.GetActor();
  if (!actor) { return; }
  int from = plan.NearestNode(actor.GetOrigin(), 1.5, false);
  if (from < 0) { from = patrol.ClaimedNode(); }
  if (from < 0) { return; }
  plan.DistancesFrom(from, 40);
  array<int> best = {};
  array<float> scores = {};
  foreach (int window : plan.WindowNodes)
  {
   int hops = plan.Hops(window);
   if (hops < 0 || !plan.StopFree(window, actor)) { continue; }
   float score = hops * 0.75;
   if (known)
   {
    vector toward = threat - plan.Nodes[window].Position;
    toward[1] = 0;
    if (toward.Length() > 0.1)
    {
     toward.Normalize();
     float facing = vector.Dot(plan.Nodes[window].Look, toward);
     if (facing < -0.3) { continue; }
     score += 3 * (1 - facing);
    }
   }
   EXPG_BuildingPlan.KeepBest(best, scores, window, score, 3);
  }
  foreach (int choice : best)
  {
   if (patrol.Alert(choice, plan.Nodes[choice].Look, true, plan.Hops(choice))) { return; }
  }
  if (windowOnly) { return; }
  best.Clear();
  scores.Clear();
  foreach (int watch : plan.WatchNodes)
  {
   int watchHops = plan.Hops(watch);
   if (watchHops < 0 || watchHops > 12 || !plan.StopFree(watch, actor)) { continue; }
   EXPG_BuildingPlan.KeepBest(best, scores, watch, watchHops, 3);
  }
  foreach (int point : best)
  {
   if (patrol.Alert(point, plan.Nodes[point].Look, false, plan.Hops(point))) { return; }
  }
  patrol.HoldHere();
 }

 // A living guard who is no longer in this squad (an AI Surrender prisoner, a
 // Game Master split or regroup, ungrouped) is forgotten alone: never cached,
 // respawned or deleted by this garrison; the other guards keep their posts and
 // caching. A possessed guard is not a leaver (the player's group is not his).
 protected void ForgetLeavers(EXPG_GarrisonRecord record)
 {
  for (int leftIndex = record.Members.Count() - 1; leftIndex >= 0; leftIndex--)
  {
   EXPG_GarrisonMember leaver = record.Members[leftIndex];
   SCR_ChimeraCharacter leaverActor = leaver.CacheMember.Entity;
   if (leaver.CacheMember.Dead || !leaverActor || leaverActor.GetCharacterGroup() == record.Group || IsDeadActor(leaverActor)) { continue; }
   CharacterControllerComponent leaverController = leaverActor.GetCharacterController();
   if (leaverController && leaverController.IsPlayerControlled()) { continue; }
   string departure = "moved to another squad";
   if (leaverActor.EBG_HasLeftSquad()) { departure = "left the squad for good"; }
   leaver.ReleaseControl();
   record.Members.RemoveOrdered(leftIndex);
   PrintFormat("[EXPG Garrison] group=%1 guard %2 (%3) %4; forgotten, the other guards keep their posts", record.Group, leaver.CacheMember.Id, leaverActor, departure);
  }
 }

 // The first living guard who is not under garrison control (for the hold reason).
 protected string UnboundGuard(EXPG_GarrisonRecord record)
 {
  foreach (EXPG_GarrisonMember member : record.Members)
  {
   if (member.CacheMember.Dead || member.Post || member.Patrol) { continue; }
   if (!member.Arrived) { return string.Format("guard %1 is still being moved to his post", member.CacheMember.Id); }
   return string.Format("guard %1 is not under garrison control yet (vehicle, another squad or a native control not ready)", member.CacheMember.Id);
  }
  return "a guard is not under garrison control yet";
 }

 protected void Tick(EXPG_GarrisonRecord record)
 {
  if (!record.Group) { record.RequestRelease("Squad deleted"); }
  if (!record.Plan.Valid()) { record.RequestRelease("Building moved or was replaced"); }
  // One floor support check per group service catches damaged or removed floors
  // without rescanning the whole building. Never a release and never a teleport:
  // a guard whose floor gives way falls and holds where he lands (PostControl);
  // until then caching is held. Debris on a post is no floor loss while the guard
  // still stands on something.
  if (!record.Members.IsEmpty())
  {
   EXPG_GarrisonMember check = record.Members[record.SafetyCursor++ % record.Members.Count()];
   SCR_ChimeraCharacter standing = check.CacheMember.Entity;
   bool supported = true;
   if (!check.CacheMember.Dead && check.NodeIndex >= 0) { supported = record.Plan.Supported(record.Plan.Nodes[check.NodeIndex].Position, 0.2, standing); }
   else if (!check.CacheMember.Dead) { supported = record.Plan.GroundSupported(check.PostPosition, 0.2, standing); }
   if (!supported && standing && !record.Full && !record.Simulation) { supported = record.Plan.GroundSupported(standing.GetOrigin(), 0.2, standing); }
   if (!supported && !check.FloorLost)
   { PrintFormat("[EXPG Garrison] group=%1 guard %2: the floor under his post gave way; nobody released", record.Group, check.CacheMember.Id); }
   check.FloorLost = !supported;
  }
  if (record.Full)
  {
   bool wakeFull = record.ReleaseRequested || !record.Group || CacheModeInUse(record.Group) != 2;
   if (record.Group && Near(record, record.Group.EXPG_WakeDistance)) { wakeFull = true; }
   if (record.Full.GetState() != EBG_FullGroupPhase.CACHED) { wakeFull = true; }
   if (!wakeFull || !Wake(record)) { return; }
  }
  if (record.Simulation)
  {
   // A Full garrison Simulation-cached because CDF is loaded runs mode 1 too.
   bool wake = record.ReleaseRequested || !record.Group || CacheModeInUse(record.Group) != 1;
   if (record.Group && Near(record, record.Group.EXPG_WakeDistance)) { wake = true; }
   if (!record.Simulation.Suspended) { wake = true; }
   // Death, external deletion, a squad change and possession wake the sleeping
   // originals, even when no player has crossed the distance boundary. Awake,
   // each is handled for that guard alone; nobody else is released.
   foreach (EXPG_GarrisonMember sleeping : record.Members)
   {
   SCR_ChimeraCharacter actor = sleeping.CacheMember.Entity;
   if (sleeping.CacheMember.Dead) { continue; }
   if (IsDeadActor(actor)) { wake = true; break; }
   if (!actor || actor.GetCharacterGroup() != record.Group || actor.EBG_WasPlayerControlled()) { wake = true; break; }
    CharacterControllerComponent controller = actor.GetCharacterController();
    if (!controller || controller.IsDead() || controller.IsPlayerControlled()) { wake = true; break; }
   }
   if (!wake || !Wake(record)) { return; }
  }
  if (record.ReleaseRequested) { record.FinishRelease(); return; }
  if (!record.Ready)
  {
   if (Now() - record.Created > 45) { record.RequestRelease("Squad initialization timed out; retained as a normal squad"); }
   else { Initialize(record); }
   return;
  }
  ForgetLeavers(record);
  int alive;
  bool unsafe;
  // Why caching is held this Tick (the first reason found; NoteHold shows it).
  string hold;
  foreach (EXPG_GarrisonMember member : record.Members)
  {
   EBG_CacheMember cached = member.CacheMember;
   if (cached.Dead) { continue; }
   SCR_ChimeraCharacter actor = cached.Entity;
   // Deleted (a Game Master, a cleanup): gone for good and never respawned; the
   // other guards keep their posts.
   if (!actor)
   {
    cached.Dead = true;
    member.ReleaseControl();
    unsafe = true;
    PrintFormat("[EXPG Garrison] group=%1 guard %2 was deleted; never respawned, the other guards keep their posts", record.Group, cached.Id);
    continue;
   }
   CharacterControllerComponent controller = actor.GetCharacterController();
   if (IsDeadActor(actor)) { cached.Dead = true; member.ReleaseControl(); continue; }
   alive++;
   cached.Position = actor.GetOrigin();
   if (!controller)
   {
    unsafe = true;
    if (hold.IsEmpty()) { hold = string.Format("guard %1 has no character controller", cached.Id); }
    continue;
   }
   // Possessed: free while a player has him; afterwards he holds where he was left
   // (BindControls). Unit Caching never caches a once-possessed soldier.
   if (controller.IsPlayerControlled())
   {
    if (member.Post || member.Patrol)
    { PrintFormat("[EXPG Garrison] group=%1 guard %2 (%3) possessed by a player; the other guards keep their posts", record.Group, cached.Id, actor); }
    cached.WasPlayer = true;
    member.ReleaseControl();
    unsafe = true;
    if (hold.IsEmpty()) { hold = string.Format("guard %1 is possessed by a player", cached.Id); }
    continue;
   }
   if (actor.EBG_WasPlayerControlled()) { cached.WasPlayer = true; }
   // A lost control (a vehicle, another agent) is that guard's matter only: he is
   // bound again by BindControls when he can be.
   if (member.Post && !member.Post.Tick()) { member.Post = null; unsafe = true; }
   // Displaced and come to rest (conscious, still): a plausible spot becomes his
   // post; any other spot (more than 6 m from his post, off the floor, on the roof
   // or outside the building) never does, and he is sent back to his post.
   vector anchor;
   if (member.Post && member.Post.TakeMoved(anchor))
   {
    vector before = member.PostPoint();
    float shift = vector.Distance(before, anchor);
    if (shift <= 0.5)
    {
     member.Returning = false;
     member.Post.Return(before);
    }
    else if (cached.WasPlayer || member.Plausible(anchor, actor))
    {
     member.Returning = false;
     member.Anchor(anchor);
     PrintFormat("[EXPG Garrison] group=%1 guard %2 (%3) moved %4 m; holds where he stands", record.Group, cached.Id, actor, shift);
    }
    else if (!member.Returning)
    {
     member.Returning = true;
     member.ReturnSent = -1000;
     PrintFormat("[EXPG Garrison] group=%1 guard %2 (%3) came to rest %4 m from his post at %5, not a plausible post; sent back to it", record.Group, cached.Id, actor, shift, before);
    }
   }
   if (member.Post && member.Returning)
   {
    unsafe = true;
    if (hold.IsEmpty()) { hold = string.Format("guard %1 is being sent back to his post", cached.Id); }
    if (member.OnPost(actor.GetOrigin()))
    {
     member.Returning = false;
     member.Post.Return(member.PostPoint());
    }
    else if (Now() - member.ReturnSent >= 3 && EXPG_GarrisonMember.AtRest(actor))
    {
     member.ReturnSent = Now();
     member.Post.Return(member.PostPoint());
     EXPG_GarrisonMember.Teleport(actor, member.PostPoint(), member.PostLook);
    }
   }
   if (member.Patrol)
   {
    if (!member.Patrol.Tick()) { member.Patrol = null; unsafe = true; }
    // The post a patroller holds is his current claim (floor checks, parking).
    else if (member.Patrol.ClaimedNode() >= 0) { member.NodeIndex = member.Patrol.ClaimedNode(); }
   }
   if (member.FloorLost)
   {
    unsafe = true;
    if (hold.IsEmpty()) { hold = string.Format("the floor under guard %1's post gave way", cached.Id); }
   }
   string problem = EBG_SimulationCache.Unsupported(actor, CacheModeInUse(record.Group) == 1);
   if (!problem.IsEmpty())
   {
    unsafe = true;
    if (hold.IsEmpty()) { hold = string.Format("guard %1: %2", cached.Id, problem); }
   }
  }
  // A casualty may remain in the native agent list during its removal callback.
  // Compare identity instead of counts, so one death does not release the guards.
  // A soldier who is not a guard (added by a Game Master) holds caching only.
  array<AIAgent> currentAgents = {};
  record.Group.GetAgents(currentAgents);
  foreach (AIAgent current : currentAgents)
  {
   bool known;
   foreach (EXPG_GarrisonMember original : record.Members)
   {
    if (original.CacheMember.Entity == current.GetControlledEntity()) { known = true; break; }
   }
   if (known) { continue; }
   unsafe = true;
   if (hold.IsEmpty()) { hold = "a soldier who is not one of its guards joined the squad (posts kept)"; }
   break;
  }
  if (alive == 0) { record.RequestRelease("No surviving guards"); }
  if (record.ReleaseRequested) { record.FinishRelease(); return; }
  if (!record.BindControls())
  {
   unsafe = true;
   if (hold.IsEmpty()) { hold = UnboundGuard(record); }
  }
  else if (record.Status == "Native controls are initializing") { record.Report("Garrison active"); }
  ServiceAlert(record);
  vector settings = EXPG_GarrisonSettings.Normalize(Vector(record.Group.EXPG_WakeDistance, record.Group.EXPG_SleepDistance, record.Group.EXPG_CacheMode));
  record.Group.EXPG_WakeDistance = settings[0];
  record.Group.EXPG_SleepDistance = settings[1];
  record.Group.EXPG_CacheMode = settings[2];
  // Awake by design (caching Off, a player near, just placed or woken): no hold.
  if (record.Group.EXPG_CacheMode == 0 || Near(record, record.Group.EXPG_SleepDistance) || Now() - record.Created < 15)
  { record.ClearSince = -1; ReleaseSettle(record); record.ClearHold(); return; }
  // An alarm, like combat, keeps the garrison awake until it calms down; any
  // other hold names its reason ("Cache held: ...").
  if (record.AlertActive || unsafe)
  {
   if (record.AlertActive) { hold = "combat alarm in the building within the last minute"; }
   record.ClearSince = -1;
   ReleaseSettle(record);
   record.NoteHold(hold, Now());
   return;
  }
  record.ClearHold();
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
   // About 4 ms of analysis per 100 ms pump: large buildings finish in seconds
   // instead of minutes, and one expensive step batch cannot stall a frame.
   EXPG_BuildingPlan analysing = NextAnalysis();
   int analysisStart = System.GetTickCount();
   while (analysing && !analysing.Done && System.GetTickCount() - analysisStart < 4) { analysing.Step(8); }
   // One plan per pump is checked for eviction (a waiting request keeps its plan used).
   m_PlanCursor = m_PlanCursor % m_Plans.Count();
   EXPG_BuildingPlan plan = m_Plans[m_PlanCursor++];
   bool used;
   foreach (EXPG_GarrisonRecord record : m_Records) { if (record.Plan == plan && !record.Finished) { used = true; break; } }
   if (!used && Now() - plan.LastUsed > 120) { m_Plans.RemoveItem(plan); }
  }
  ServiceWaiters();
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
