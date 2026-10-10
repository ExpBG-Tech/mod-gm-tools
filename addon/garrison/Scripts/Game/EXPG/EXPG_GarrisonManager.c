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
 // Loaded from a save: the saved node index, a hint only until the post is remapped
 // onto the analysed plan by position (EXPG_GarrisonManager.Remap).
 int NodeHint = -1;

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
 // 0.5 m keeps a plan reservation, otherwise an off-plan post (NodeIndex -1) that
 // patrol claims still keep their spacing from (HoldOffPlan). He keeps watching
 // the same way (PostLook) and is never moved. A patrol claim of the building
 // within POST_SPACING of his new post moves on (EXPG_GarrisonManager.YieldClaims).
 void Anchor(vector at)
 {
  if (!Plan || !CacheMember || !CacheMember.Entity) { return; }
  Plan.ReleaseReservation(CacheMember.Entity);
  NodeIndex = -1;
  PostPosition = at;
  int node = Plan.NearestNode(at, 0.5, true);
  if (node >= 0 && Plan.ReserveNode(CacheMember.Entity, node)) { NodeIndex = node; }
  else { Plan.HoldOffPlan(CacheMember.Entity, at); }
  EXPG_GarrisonManager.YieldClaims(Plan, PostPoint());
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
 // Persistence (EXPG_Snapshot.c). The token is made at adoption and kept across
 // saves. GeneratedBy: "" for EXPBG Add Garrison, "rg:<hi>-<lo>" for a garrison a
 // Random Garrison module made (EXPG_GarrisonSpawner.Spawn); saved in the ledger.
 string Token;
 string GeneratedBy;
 // Settings live in the record: a Full-cached garrison has no squad in the world.
 // The squad's replicated values are the Game Master's edit surface (SyncSettings).
 int CacheMode = 2;
 float WakeDistance = 300;
 float SleepDistance = 400;
 // Loaded from a save: posts are remapped once the building's plan is analysed,
 // then it wakes (saved awake or Simulation, which caches again at once when no
 // player is near) or stays Full cached until a player comes within wake distance.
 bool RemapPending;
 bool WakeOnLoad;
 bool SleepAfterLoad;
 ref EXPG_BuildingRef Site;
 // Full refused because the squad could not be captured: Simulation instead, and
 // Full is tried again ten minutes later (EXPG_GarrisonManager.TrySleep).
 string FullRefused;
 float FullRefusedAt;
 bool FallbackSimulation;

 // The squad's replicated settings are normalized and copied into the record.
 void SyncSettings()
 {
  if (!Group) { return; }
  vector settings = EXPG_GarrisonSettings.Normalize(Vector(Group.EXPG_WakeDistance, Group.EXPG_SleepDistance, Group.EXPG_CacheMode));
  Group.EXPG_WakeDistance = settings[0];
  Group.EXPG_SleepDistance = settings[1];
  Group.EXPG_CacheMode = settings[2];
  WakeDistance = settings[0];
  SleepDistance = settings[1];
  CacheMode = settings[2];
 }

 // A Full wake recreated the squad (durable Full): it belongs to this garrison in
 // the spawning call, before it replicates, with the record's settings and status.
 void AdoptRestoredGroup(SCR_AIGroup group)
 {
  if (!group || Group == group) { return; }
  Group = group;
  group.EXPG_Active = true;
  group.EXPG_WakeDistance = WakeDistance;
  group.EXPG_SleepDistance = SleepDistance;
  group.EXPG_CacheMode = CacheMode;
  group.EXPG_Status = Status;
  group.GetOnWaypointAdded().Insert(OnWaypoint);
  group.EXPG_Changed();
  EXPG_GarrisonPersistence.KeepNewborn(group);
  if (EBG_CacheDebug.Verbose()) PrintFormat("[EXPG Garrison] group=%1 squad recreated for its Full restore", group);
 }

 // Durable Full sleep: the squad is about to be captured and deleted.
 void DetachGroup()
 {
  UnbindSquadHook();
  if (Group) { Group.GetOnWaypointAdded().Remove(OnWaypoint); }
 }

 void Report(string message)
 {
  if (Status == message) { return; }
  Status = message;
  if (Group) { Group.EXPG_Status = message; Group.EXPG_Changed(); }
  if (EBG_CacheDebug.Verbose()) PrintFormat("[EXPG Garrison] group=%1 %2", Group, message);
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
   // An off-plan post holds no node: patrol claims still keep their spacing from it.
   if (member.NodeIndex < 0) { Plan.HoldOffPlan(actor, member.PostPosition); }
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
 // A module's request (Random Garrison): it keeps the plan in use but never takes
 // a Game Master's analysis turn (NextAnalysis); it is analysed when none waits.
 bool Background;

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
 protected float m_Born;
 // Persistence (EXPG_Persistence.c): a load in progress, its queued ledger, whether
 // the native ledger was checked, the last logged mode and the exclusion schedule.
 protected bool m_Importing;
 protected ref array<ref EXPG_GarrisonSnapshot> m_PendingImport;
 protected bool m_NativeChecked;
 protected int m_LastMode = -1;
 protected float m_NextExclusion;
 protected float m_NextNotice;
 // The full exclusion sync runs every 5 s; every save and load forces its own first.
 protected float m_NextExclusionSync;
 // Tick: the guards whose Unit Caching support is checked only once caching is due.
 protected ref array<SCR_ChimeraCharacter> m_SupportActors = {};
 protected ref array<int> m_SupportIds = {};
 // Near: the player origins of one call.
 protected ref array<vector> m_NearOrigins = {};

 static EXPG_GarrisonManager Get()
 {
  if (!Replication.IsServer() || !GetGame() || !GetGame().InPlayMode()) { return null; }
  if (!s_Instance || s_Instance.m_World != GetGame().GetWorld())
  {
   if (s_Instance) { GetGame().GetCallqueue().Remove(s_Instance.Pump); }
   s_Instance = new EXPG_GarrisonManager();
   s_Instance.m_World = GetGame().GetWorld();
   s_Instance.m_Born = s_Instance.Now();
   GetGame().GetCallqueue().CallLater(s_Instance.Pump, 100, true);
   // A native load imports its garrisons once persistence is active; mid-session
   // that is now, so a first Add Garrison is never told garrisons are loading.
   s_Instance.ServiceNativeImport();
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
  // A garrison's own squad first: no guard needs to be visited for it.
  foreach (EXPG_GarrisonRecord owner : s_Instance.m_Records)
  {
   if (!owner.Finished && owner.Group == group) { return true; }
  }
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
   if (record.Group == group) { return s_Instance.CacheState(record); }
   bool held = false;
   foreach (EXPG_GarrisonMember member : record.Members)
   {
    if (member.CacheMember.Entity && member.CacheMember.Entity.GetCharacterGroup() == group) { held = true; break; }
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

 // CDF legacy mode only (no EXPBG CDF Compat bridge): Prepare for Save releases garrisons.
 static void RequestReleaseAll()
 {
  ReleaseAll("Unit Caching Prepare for Save (CDF without the EXPBG CDF Compat bridge)");
 }

 // Every garrison wakes and becomes an ordinary squad. Returns how many.
 static int ReleaseAll(string reason)
 {
  if (!HasActive()) { return 0; }
  int released;
  foreach (EXPG_GarrisonRecord record : s_Instance.m_Records)
  {
   if (record.Finished || record.ReleaseRequested) { continue; }
   record.RequestRelease(reason);
   released++;
  }
  return released;
 }

 // A guard's post moved (EXPG_GarrisonMember.Anchor): every patrol claim of the
 // building within POST_SPACING of the new post moves on to another free stop
 // (EXPG_PatrolControl.Yield, which keeps alarms and cache settles as they are).
 // Event-driven, the building's awake garrisons only; no world query.
 static void YieldClaims(EXPG_BuildingPlan plan, vector post)
 {
  if (!plan || !HasActive()) { return; }
  foreach (EXPG_GarrisonRecord record : s_Instance.m_Records)
  {
   if (record.Finished || record.Plan != plan || record.Simulation) { continue; }
   foreach (EXPG_GarrisonMember walker : record.Members)
   {
    EXPG_PatrolControl patrol = walker.Patrol;
    if (walker.CacheMember.Dead || !patrol) { continue; }
    int claim = patrol.ClaimedNode();
    if (claim < 0 || claim >= plan.Nodes.Count() || !EXPG_BuildingPlan.Crowded(post, plan.Nodes[claim].Position, EXPG_BuildingPlan.POST_SPACING)) { continue; }
    patrol.Yield();
    // His post follows his new claim at once (floor checks, parking).
    if (patrol.ClaimedNode() >= 0) { walker.NodeIndex = patrol.ClaimedNode(); }
   }
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
  if (!waiter.Background) { plan.WaitedAt = Now(); }
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

 // A save or load is running. Prepare for Save holds garrisons only in CDF legacy
 // mode (it releases them there); otherwise garrisons save themselves.
 static bool SaveInProgress()
 {
  if (EBG_CacheSnapshot.Loading) { return true; }
  if (s_Instance && s_Instance.m_World == GetGame().GetWorld())
  {
   if (s_Instance.IsImporting()) { return true; }
   if (EBG_OptimizerControl.Preparing && s_Instance.PersistenceMode() == EXPG_GarrisonPersistence.MODE_CDF_LEGACY) { return true; }
  }
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
  if (IsImporting()) { reason = "Garrisons are loading from a save; add the garrison again in a moment"; return false; }
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

 // Only the successful native editor spawn callback may call this: the fresh
 // roster is checked before anyone moves. Nobody is ever deleted to fit a building.
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
  record.Token = NewToken();
  record.SyncSettings();
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

 // A player within distance of the building or of a living guard. Each player's
 // origin and each guard's point are read once per call; any hit answers.
 protected bool Near(EXPG_GarrisonRecord record, float distance)
 {
  float limitSq = distance * distance;
  m_NearOrigins.Clear();
  foreach (IEntity player : m_Players)
  {
   if (!player) { continue; }
   vector origin = player.GetOrigin();
   if (vector.DistanceSq(origin, record.Plan.Origin) <= limitSq) { return true; }
   m_NearOrigins.Insert(origin);
  }
  if (m_NearOrigins.IsEmpty()) { return false; }
  foreach (EXPG_GarrisonMember member : record.Members)
  {
   if (member.CacheMember.Dead) { continue; }
   vector point = member.CacheMember.Position;
   if (member.CacheMember.Entity) { point = member.CacheMember.Entity.GetOrigin(); }
   foreach (vector seen : m_NearOrigins)
   {
    if (vector.DistanceSq(seen, point) <= limitSq) { return true; }
   }
  }
  return false;
 }

 protected bool Initialize(EXPG_GarrisonRecord record, bool spawnedOnly = false)
 {
  SCR_AIGroup group = record.Group;
  // spawnedOnly: the squad never reported a completed spawn (production 2026-10-10: a
  // second squad added to a large building waited 45 s and was left standing outside);
  // the members that do exist take their posts instead.
  if (!spawnedOnly && (!group.IsExpandComplete() || !group.EBG_HasCompletedInitialSpawn())) { return false; }
  array<AIAgent> agents = {};
  group.GetAgents(agents);
  if (spawnedOnly && agents.IsEmpty()) { return false; }
  // A partial roster is taken as it stands: the fresh-roster count no longer applies.
  if (spawnedOnly) { record.FreshRequested = 0; }
  if (record.FreshRequested > 0 && !group.EXPG_FreshRosterMatches(record.FreshRequested))
  { record.RequestRelease("Fresh squad membership changed; retained as normal AI"); return false; }
  // Every squad deploys in full and nobody is ever deleted to fit a building. The
  // first garrison takes the building's planned posts and patrol starts; soldiers
  // beyond them, and every later squad, follow the reinforcement order: free fixed
  // posts, interior patrollers (free-room rule), more watch positions, close rings
  // around the building, then the soldier's own spawn point (ChooseReinforcement).
  bool reinforce = OtherGarrisons(record) > 0;
  int fitting = PlacementCount(agents.Count(), agents.Count());
  if (fitting == 0 || (record.FreshRequested > 0 && agents.Count() != record.FreshRequested))
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
   int planned = Math.Min(fitting, record.Plan.Slots.Count());
   if (planned < 1 || !record.Plan.ValidateSlots(planned, originals))
   { record.RequestRelease("Structure positions changed or are obstructed; retained as a normal squad"); return false; }
   for (int slotIndex = 0; slotIndex < planned; slotIndex++)
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
   // Soldiers beyond the planned posts: same order as an added squad.
   if (placements.Count() < fitting) { ChooseReinforcement(record, originals, placements); }
  }
  if (placements.Count() < fitting)
  { record.RequestRelease("Garrison positions unavailable; retained as a normal squad"); return false; }
  // The fresh roster is checked once more before anyone is moved; nobody is deleted.
  bool fresh = record.FreshRequested > 0;
  record.FreshRequested = 0;
  group.EBG_UseCapturedRoster();
  if (fresh && !group.EXPG_FreshRosterMatches(originals.Count()))
  { record.RequestRelease("Fresh squad changed while taking posts; retained as normal AI"); return false; }
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

 //------------------------------------------------------------------------------------------------
 // Random Garrison (addon/random-garrison): read-only views and the per-garrison
 // discard. The module never steps a plan itself and keeps no list of its squads.
 //------------------------------------------------------------------------------------------------
 int PlanCount()
 {
  return m_Plans.Count();
 }

 // Unfinished garrisons a module made (GeneratedBy), in record order.
 int CollectGenerated(string generatedBy, notnull array<ref EXPG_GarrisonRecord> outRecords)
 {
  outRecords.Clear();
  if (generatedBy.IsEmpty())
  {
   return 0;
  }
  foreach (EXPG_GarrisonRecord record : m_Records)
  {
   if (!record.Finished && record.GeneratedBy == generatedBy) { outRecords.Insert(record); }
  }
  return outRecords.Count();
 }

 // Living guards of the unfinished garrisons in a building (Full-cached ones
 // included); a squad still taking its posts counts with its requested size.
 int AssignedSoldiers(IEntity building)
 {
  if (!building)
  {
   return 0;
  }
  int count;
  foreach (EXPG_GarrisonRecord record : m_Records)
  {
   if (record.Finished || record.Plan.Structure != building) { continue; }
   if (!record.Ready) { count += record.FreshRequested; continue; }
   foreach (EXPG_GarrisonMember member : record.Members)
   {
    if (!member.CacheMember.Dead) { count++; }
   }
  }
  return count;
 }

 // Faction keys of the unfinished garrisons in a building, without duplicates; a
 // Full-cached or loaded squad (no group in the world) gives its snapshot's key.
 int GarrisonFactions(IEntity building, notnull array<string> outFactions)
 {
  outFactions.Clear();
  if (!building)
  {
   return 0;
  }
  foreach (EXPG_GarrisonRecord record : m_Records)
  {
   if (record.Finished || !record.Plan || record.Plan.Structure != building) { continue; }
   string factionKey = string.Empty;
   if (record.Group) { factionKey = record.Group.GetFactionName(); }
   else if (record.Full && record.Full.LedgerSquad()) { factionKey = record.Full.LedgerSquad().FactionName; }
   if (!factionKey.IsEmpty() && !outFactions.Contains(factionKey)) { outFactions.Insert(factionKey); }
  }
  return outFactions.Count();
 }

 // One garrison is forgotten at once, as a load forgets them (DiscardAll): nobody
 // is woken or respawned. The caller deletes its squad and soldiers. False when the
 // garrison has already finished.
 bool Discard(EXPG_GarrisonRecord record, string why)
 {
  if (!record || record.Finished)
  {
   return false;
  }
  if (record.Full)
  {
   record.Full.Abandon();
   record.Full = null;
   record.FullRecord = null;
  }
  if (record.Simulation)
  {
   bool present = false;
   foreach (EBG_SimulationAgent saved : record.Simulation.Members)
   {
    if (saved.Character) { present = true; }
    else if (saved.Devices) { saved.Devices.Discard(); }
   }
   string ignored;
   if (present) { EBG_SimulationCache.Restore(record.Simulation, ignored); }
   record.Simulation = null;
  }
  record.RequestRelease(why);
  record.FinishRelease();
  PrintFormat("[EXPG Garrison] group=%1 discarded (%2); nobody was woken or respawned", record.Group, why);
  return true;
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
  // A first garrison's overflow keeps clear of the posts its squad already took.
  foreach (EXPG_Placement chosen : placements) { occupied.Insert(chosen.Position); }
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
  record.FallbackSimulation = false;
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
  // The recreated squad was deleted (by a Game Master, with its soldiers): nothing
  // is left to restore and nobody is respawned.
  if (full.GroupLost())
  {
   full.Abandon();
   record.Full = null;
   record.FullRecord = null;
   record.Plan.Unpark(record);
   record.RequestRelease("Squad deleted");
   return true;
  }
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
  record.WakeOnLoad = false;
  // Saved Simulation-cached: it caches again on its next tick when no player is
  // near (the 15 s and 30 s sleep timers are skipped once).
  if (record.SleepAfterLoad)
  {
   record.SleepAfterLoad = false;
   record.Created = Now() - 16;
   record.ClearSince = Now() - 31;
   record.RetryAt = 0;
  }
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
  // Durable Full: the squad is captured and deleted with its soldiers.
  record.DetachGroup();
  if (record.Full.BeginManagedSleep(group))
  {
   record.Group = null;
   record.FullRefused = "";
   record.Report("Full cached (prefab-default kits on restore)");
   return;
  }
  string failure = record.Full.GetError();
  if (record.Full.HasDeletionAttempted())
  { record.Report("Full recovery retained: " + failure); return; }
  // No native deletion: abandon only this unused transaction and rebind the
  // untouched actors. Once deletion starts the transaction must survive retries.
  record.Full = null;
  record.FullRecord = null;
  record.Plan.Unpark(record);
  if (group) { group.GetOnWaypointAdded().Insert(record.OnWaypoint); }
  // Untouched originals: bind again; a guard not bound yet is retried every Tick.
  record.BindControls();
  record.Report("Full cache held: " + failure);
  // The squad itself could not be captured (orders, AI settings, editor protection):
  // the next sleep caches it in Simulation instead (TrySleep), Full again later.
  record.FullRefused = failure;
  record.FullRefusedAt = Now();
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
  if (CdfWithoutCompanion())
  { record.Report("Cache held: CDF requires the EXPBG GM Tools CDF companion for save protection"); return; }
  bool legacy = PersistenceMode() == EXPG_GarrisonPersistence.MODE_CDF_LEGACY;
  if ((legacy && EBG_OptimizerControl.Preparing) || EBG_CacheSnapshot.Loading || IsImporting()) { record.Report("Cache held: a Unit Caching save or load is in progress"); return; }
  // Patrollers first finish their walk at a stop (silently: not a refusal).
  if (!PatrolsSettled(record)) { return; }
  // Full refused for the squad itself: Simulation instead, Full again after ten minutes.
  if (!record.FullRefused.IsEmpty() && Now() - record.FullRefusedAt > 600) { record.FullRefused = ""; }
  bool fallback = !record.FullRefused.IsEmpty() && CacheModeInUse(record.Group) == 2;
  if (!fallback && CacheModeInUse(record.Group) == 2)
  {
   TryFullSleep(record);
   // A held sleep lets the patrol go on until the next attempt.
   if (!record.Full) { ReleaseSettle(record); }
   return;
  }
  // Simulation keeps the original actors bound; a refused Full sleep released and
  // rebound their controls, which may take a tick.
  if (fallback && !record.BindControls()) { ReleaseSettle(record); return; }
  string reason;
  record.Simulation = EBG_SimulationCache.Suspend(record.Group, reason);
  record.FallbackSimulation = fallback && record.Simulation != null;
  // Full chosen, CDF loaded without its bridge: one status line per sleep names the fallback.
  string cachedStatus = "Simulation cached";
  if (record.FallbackSimulation) { cachedStatus = "Simulation cached (Full refused: " + record.FullRefused + ")"; }
  else if (legacy && record.CacheMode == 2) { cachedStatus = "Simulation cached (CDF loaded)"; }
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

 // CDF Game Master Save (6A1876F37D65AB09) is loaded. Read once per mission. With
 // the EXPBG CDF Compat garrison bridge (CdfBridgeVersion) the CDF document carries
 // the garrison ledger; without it (legacy) a garrison set to Full caches in
 // Simulation, CDF saves are refused while a garrison is active and a CDF load
 // deletes the soldiers like any squad (0.1.8 rules).
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

 // CDF Game Master Save loaded without the EXPBG CDF Compat companion (07BC942D90324CD9),
 // read from the loaded addon list once per mission like CdfLoaded. Not the CdfLoaded
 // seam: the sleep refusal has always read the addon list itself.
 protected int m_CdfWithoutCompanion = -1;
 protected bool CdfWithoutCompanion()
 {
  if (m_CdfWithoutCompanion < 0)
  {
   array<string> addons = {};
   GameProject.GetLoadedAddons(addons);
   m_CdfWithoutCompanion = 0;
   if (addons.Contains("6A1876F37D65AB09") && !addons.Contains("07BC942D90324CD9")) { m_CdfWithoutCompanion = 1; }
  }
  return m_CdfWithoutCompanion == 1;
 }

 // EXPBG CDF Compat 0.1.6 and later override this with EXPG_GarrisonPersistence.
 // BRIDGE_API: its bridge saves and loads the garrison ledger in the CDF document.
 // An instance method on purpose: a modded static would bind to this base version.
 protected int CdfBridgeVersion()
 {
  return 0;
 }

 // EXPG_GarrisonPersistence mode (native, CDF bridged, CDF legacy); a change is logged.
 int PersistenceMode()
 {
  int mode = EXPG_GarrisonPersistence.MODE_NATIVE;
  if (CdfLoaded())
  {
   mode = EXPG_GarrisonPersistence.MODE_CDF_LEGACY;
   if (CdfBridgeVersion() == EXPG_GarrisonPersistence.BRIDGE_API) { mode = EXPG_GarrisonPersistence.MODE_CDF_BRIDGED; }
  }
  if (mode != m_LastMode)
  {
   m_LastMode = mode;
   PrintFormat("[EXPG SAVE] garrison persistence mode: %1 (CDF bridge API %2, expected %3)", EXPG_GarrisonPersistence.ModeName(mode), CdfBridgeVersion(), EXPG_GarrisonPersistence.BRIDGE_API);
  }
  return mode;
 }

 // The cache mode a garrison runs: 0 Off, 1 Simulation, 2 Full. The GM's choice
 // (EXPG_CacheMode) is kept; only its use changes while CDF is loaded without the
 // EXPBG CDF Compat garrison bridge (legacy).
 int CacheModeInUse(SCR_AIGroup group)
 {
  int mode;
  if (group) { mode = group.EXPG_CacheMode; }
  if (mode == 2 && CdfLoaded() && PersistenceMode() == EXPG_GarrisonPersistence.MODE_CDF_LEGACY) { mode = 1; }
  return mode;
 }

 // The same for a record: a Full-cached garrison has no squad in the world.
 int RecordModeInUse(EXPG_GarrisonRecord record)
 {
  int mode = record.CacheMode;
  if (mode == 2 && CdfLoaded() && PersistenceMode() == EXPG_GarrisonPersistence.MODE_CDF_LEGACY) { mode = 1; }
  return mode;
 }

 // Short cache state of one garrison for the Unit Caching zone status.
 string CacheState(EXPG_GarrisonRecord record)
 {
  string text = "awake on their posts";
  if (record.RemapPending) { text = "loaded from a save, analysing the building"; }
  else if (record.Full && record.Full.GetState() == EBG_FullGroupPhase.CACHED) { text = "Full cached"; }
  else if (record.Full) { text = "restoring Full survivors"; }
  else if (record.Simulation && record.Simulation.Suspended) { text = "Simulation cached"; }
  else if (record.Simulation) { text = "restoring from Simulation"; }
  else if (!record.Ready) { text = "taking their posts"; }
  else if (record.CacheMode == 0) { text = "awake, caching Off"; }
  else if (record.Status.Contains("Cache held") || record.Status.Contains("cache held")) { text = "awake, caching held (see the garrison status)"; }
  if (record.Simulation && record.CacheMode == 2 && PersistenceMode() == EXPG_GarrisonPersistence.MODE_CDF_LEGACY) { text += " (CDF loaded)"; }
  else if (record.Simulation && record.FallbackSimulation) { text += " (Full refused)"; }
  return text;
 }

 //------------------------------------------------------------------------------------------------
 // Persistence: save exclusion, ledger export and import (EXPG_Persistence.c)
 //------------------------------------------------------------------------------------------------
 protected string NewToken()
 {
  string token;
  for (int attempt = 0; attempt < 8; attempt++)
  {
   token = string.Format("G%1-%2", Math.RandomInt(100000000, 999999999), Math.RandomInt(100000000, 999999999));
   bool used = false;
   foreach (EXPG_GarrisonRecord record : m_Records)
   {
    if (record.Token == token) { used = true; break; }
   }
   if (!used) { break; }
  }
  return token;
 }

 // A living guard still in his squad, never possessed: the garrison saves him.
 protected bool OwnsActor(EXPG_GarrisonRecord record, EXPG_GarrisonMember member)
 {
  EBG_CacheMember cache = member.CacheMember;
  SCR_ChimeraCharacter actor = cache.Entity;
  if (cache.Dead || cache.WasPlayer || !actor || !record.Group || actor.GetCharacterGroup() != record.Group) { return false; }
  if (actor.EBG_WasPlayerControlled() || actor.EBG_HasLeftSquad() || IsDeadActor(actor)) { return false; }
  CharacterControllerComponent controller = actor.GetCharacterController();
  return controller && !controller.IsPlayerControlled();
 }

 // Saved by the ledger: a Full transaction, or a squad and living guards that a load
 // can delete and recreate. A squad the editor protects from deletion (a scenario
 // squad) is left to the other saves as an ordinary squad (its posts are not kept).
 protected bool Portable(EXPG_GarrisonRecord record)
 {
  if (record.Full) { return true; }
  if (!record.Group || !EBG_PrefabFullCache.CanDeleteFullEntity(record.Group)) { return false; }
  foreach (EXPG_GarrisonMember member : record.Members)
  {
   if (OwnsActor(record, member) && !EBG_PrefabFullCache.CanDeleteFullEntity(member.CacheMember.Entity)) { return false; }
  }
  return true;
 }

 // Every owned entity of a Ready, portable garrison leaves the other saves; whatever
 // is no longer owned is handed back (EXPG_SaveExclusion). Bounded by the records.
 // One member pass per record decides portability (as Portable) and collects the
 // owned guards; they are kept after the squad and its waypoints, in member order.
 void SyncExclusion()
 {
  bool flag = PersistenceMode() == EXPG_GarrisonPersistence.MODE_CDF_BRIDGED;
  EXPG_SaveExclusion.BeginSync();
  array<IEntity> guards = {};
  array<AIWaypoint> orders = {};
  foreach (EXPG_GarrisonRecord record : m_Records)
  {
   if (record.Finished || !record.Ready) { continue; }
   SCR_AIGroup group = record.Group;
   if (!record.Full && (!group || !EBG_PrefabFullCache.CanDeleteFullEntity(group))) { continue; }
   guards.Clear();
   bool portable = true;
   foreach (EXPG_GarrisonMember member : record.Members)
   {
    if (!OwnsActor(record, member)) { continue; }
    if (!record.Full && !EBG_PrefabFullCache.CanDeleteFullEntity(member.CacheMember.Entity)) { portable = false; break; }
    guards.Insert(member.CacheMember.Entity);
   }
   if (!portable) { continue; }
   if (group)
   {
    EXPG_SaveExclusion.Keep(group, flag);
    orders.Clear();
    group.GetWaypoints(orders);
    foreach (AIWaypoint order : orders) { EXPG_SaveExclusion.Keep(order, flag); }
   }
   foreach (IEntity guard : guards) { EXPG_SaveExclusion.Keep(guard, flag); }
  }
  EXPG_SaveExclusion.EndSync();
 }

 void KeepOwned(IEntity entity)
 {
  EXPG_SaveExclusion.Keep(entity, PersistenceMode() == EXPG_GarrisonPersistence.MODE_CDF_BRIDGED);
 }

 // False while a load is replacing the garrisons; for CDF in legacy mode, also while
 // any garrison is active (Prepare for Save releases them first, as in 0.1.8).
 bool LedgerAllowed(bool forCdf, out string reason)
 {
  reason = "";
  if (forCdf && PersistenceMode() == EXPG_GarrisonPersistence.MODE_CDF_LEGACY && HasActive())
  {
   reason = "Use Unit Caching Prepare for Save to restore and release all garrisons, then wait for Ready. Without the EXPBG CDF Compat garrison bridge, garrison assignments are mission-only under CDF.";
   return false;
  }
  if (m_Importing && !m_PendingImport)
  {
   reason = "A load is replacing the garrisons; save again once it has finished";
   return false;
  }
  return true;
 }

 // Queued imports verbatim, then every Ready portable garrison in record order.
 bool ExportLedger(notnull array<ref EXPG_GarrisonSnapshot> ledger, out string reason)
 {
  reason = "";
  if (m_PendingImport)
  {
   foreach (EXPG_GarrisonSnapshot pending : m_PendingImport) { ledger.Insert(pending); }
  }
  int order = ledger.Count();
  int protectedSquads;
  foreach (EXPG_GarrisonRecord record : m_Records)
  {
   if (record.Finished || !record.Ready) { continue; }
   if (!Portable(record)) { protectedSquads++; continue; }
   string why;
   EXPG_GarrisonSnapshot saved = ExportRecord(record, why);
   if (!saved)
   {
    if (why.IsEmpty()) { continue; }
    reason = string.Format("garrison %1 (%2) cannot be saved: %3", record.Token, record.Group, why);
    return false;
   }
   saved.OrderIndex = order;
   if (saved.Squad.Token <= 0) { saved.Squad.Token = order + 1; }
   order++;
   ledger.Insert(saved);
  }
  if (protectedSquads > 0) { PrintFormat("[EXPG SAVE] %1 garrisons are protected from deletion by the editor and save as ordinary squads (their posts are not kept)", protectedSquads, level: LogLevel.WARNING); }
  return EXPG_Ledger.Validate(ledger, reason);
 }

 // One garrison, read only. Null with an empty reason: nothing alive to save.
 protected EXPG_GarrisonSnapshot ExportRecord(EXPG_GarrisonRecord record, out string reason)
 {
  reason = "";
  record.SyncSettings();
  if (record.Token.IsEmpty()) { record.Token = NewToken(); }
  EXPG_GarrisonSnapshot saved = new EXPG_GarrisonSnapshot();
  saved.Token = record.Token;
  saved.GeneratedBy = record.GeneratedBy;
  saved.CacheMode = record.CacheMode;
  saved.WakeDistance = record.WakeDistance;
  saved.SleepDistance = record.SleepDistance;
  saved.ReleaseRequested = record.ReleaseRequested;
  saved.ReleaseReason = record.ReleaseReason;
  if (record.Plan.Structure) { saved.Site.Capture(record.Plan.Structure); }
  else if (record.Site) { saved.Site.CopyFrom(record.Site); }
  if (!saved.Site.Valid()) { reason = "its building is unknown"; return null; }
  EXPG_FullCache full = record.Full;
  if (full)
  {
   // Cached, or waking: rows not created yet are written from the transaction.
   saved.CacheState = 2;
   saved.Squad = full.LedgerSquad();
  }
  if (!saved.Squad)
  {
   if (record.Simulation) { saved.CacheState = 1; }
   saved.Squad = new EBG_CacheGroupSnapshot();
   if (!record.Group || !saved.Squad.CaptureForLedger(record.Group, 0, true))
   {
    reason = "its squad could not be recorded: " + saved.Squad.CaptureProblem;
    return null;
   }
  }
  IEntity leader;
  if (record.Group) { leader = record.Group.GetLeaderEntity(); }
  foreach (EXPG_GarrisonMember member : record.Members)
  {
   EBG_CacheMember cache = member.CacheMember;
   EXPG_MemberSnapshot row = new EXPG_MemberSnapshot();
   row.Id = cache.Id;
   row.Fixed = member.Fixed;
   row.PostKind = member.PostKind;
   row.NodeHint = member.NodeIndex;
   if (record.RemapPending) { row.NodeHint = member.NodeHint; }
   vector post = member.PostPoint();
   row.LocalPost = saved.Site.ToLocal(post);
   row.LocalLook = saved.Site.DirToLocal(member.PostLook);
   vector transform[4];
   EBG_SurvivorCarry carry = null;
   row.Dead = cache.Dead;
   if (!row.Dead && full)
   {
    EBG_PrefabSurvivor survivor = full.FindRow(cache);
    if (!survivor || survivor.Abandoned)
    {
     // A possessed guard belongs to the other saves; any other missing row is a casualty.
     if (cache.WasPlayer) { continue; }
     row.Dead = true;
    }
    else if (!survivor.Created)
    {
     row.Prefab = survivor.Prefab;
     for (int axis = 0; axis < 4; axis++) { transform[axis] = survivor.Transform[axis]; }
     row.Author = survivor.Author;
     carry = survivor.Carry;
    }
    else if (survivor.Entity && !IsDeadActor(survivor.Entity) && !cache.WasPlayer)
    {
     survivor.Entity.GetWorldTransform(transform);
     row.Prefab = SCR_ResourceNameUtils.GetPrefabName(survivor.Entity);
     row.Author.Capture(survivor.Entity);
     carry = new EBG_SurvivorCarry();
     carry.Capture(survivor.Entity);
    }
    else if (cache.WasPlayer) { continue; }
    else { row.Dead = true; }
   }
   else if (!row.Dead)
   {
    SCR_ChimeraCharacter actor = cache.Entity;
    // A possessed guard or a leaver belongs to the other saves now.
    if (cache.WasPlayer) { continue; }
    if (!actor || IsDeadActor(actor)) { row.Dead = true; }
    else if (!OwnsActor(record, member)) { continue; }
    else
    {
     actor.GetWorldTransform(transform);
     row.Prefab = SCR_ResourceNameUtils.GetPrefabName(actor);
     row.Author.Capture(actor);
     carry = new EBG_SurvivorCarry();
     carry.Capture(actor);
    }
   }
   if (!row.Dead)
   {
    // He restores where he stood when that is within reach of his post (or stop),
    // otherwise on the post itself: a loaded guard is always bindable.
    vector at = transform[3];
    vector facing = transform[2];
    if (vector.DistanceXZ(at, post) > 1.2 || Math.AbsFloat(at[1] - post[1]) > 0.9)
    {
     at = post;
     facing = member.PostLook;
    }
    facing[1] = 0;
    if (facing.Length() < 0.1) { facing = member.PostLook; }
    row.LocalLast = saved.Site.ToLocal(at);
    row.LocalFacing = saved.Site.DirToLocal(facing);
    row.WorldLast = at;
    row.WorldFacing = facing;
    if (carry) { row.CaptureCarry(carry); }
    if (leader && cache.Entity == leader) { saved.LeaderId = row.Id; }
   }
   saved.Members.Insert(row);
  }
  if (saved.AliveCount() == 0) { return null; }
  if (saved.LeaderId == 0)
  {
   foreach (EXPG_MemberSnapshot first : saved.Members)
   {
    if (!first.Dead) { saved.LeaderId = first.Id; break; }
   }
  }
  return saved;
 }

 // A load replaces the scene, or a native load is not active yet.
 bool IsImporting()
 {
  return m_Importing || NativeLoadPending();
 }

 // A native save is being loaded and its persistence is not active yet (at most two
 // minutes): its garrisons come first.
 protected bool NativeLoadPending()
 {
  if (m_NativeChecked) { return false; }
  PersistenceSystem system = PersistenceSystem.GetInstance();
  if (!system || !system.WasDataLoaded()) { return false; }
  return system.GetState() < EPersistenceSystemState.ACTIVE && Now() - m_Born < 120;
 }

 bool NativeImportChecked()
 {
  return m_NativeChecked;
 }

 // Native carrier: once persistence is active, the ledger read by the serializer is
 // imported (a native load always starts a fresh world: nothing to discard).
 protected void ServiceNativeImport()
 {
  if (m_Importing || NativeLoadPending()) { return; }
  m_NativeChecked = true;
  string problem;
  array<ref EXPG_GarrisonSnapshot> ledger = EXPG_GarrisonPersistence.TakeNativeLedger(problem);
  if (!problem.IsEmpty()) { EXPG_GarrisonNotice.Post("The garrisons of this save could not be loaded: " + problem + ". The rest of the save loaded; the garrisons were not restored."); }
  if (!ledger || ledger.IsEmpty()) { return; }
  string reason;
  if (!StartImport(reason) || !Queue(ledger, reason))
  {
   StopImport();
   EXPG_GarrisonNotice.Post("The garrisons of this save were not loaded: " + reason);
   return;
  }
  Materialize();
 }

 bool StartImport(out string reason)
 {
  reason = "a garrison load is already in progress";
  if (m_Importing) { return false; }
  m_Importing = true;
  m_PendingImport = null;
  // A carrier's clear decides by OwnsForSave: the owned set must be current.
  SyncExclusion();
  reason = "";
  return true;
 }

 void StopImport()
 {
  m_Importing = false;
  m_PendingImport = null;
 }

 // A load replaced the scene: every garrison of the old scene is forgotten without
 // waking or respawning anyone. Soldiers the load did not remove wake as ordinary
 // AI; plans and Add Garrison requests are dropped; the exclusion is handed back.
 void DiscardAll(string why)
 {
  int discarded;
  foreach (EXPG_GarrisonRecord record : m_Records)
  {
   if (record.Finished) { continue; }
   discarded++;
   if (record.Full)
   {
    record.Full.Abandon();
    record.Full = null;
    record.FullRecord = null;
   }
   if (record.Simulation)
   {
    bool present = false;
    foreach (EBG_SimulationAgent saved : record.Simulation.Members)
    {
     if (saved.Character) { present = true; }
     else if (saved.Devices) { saved.Devices.Discard(); }
    }
    string ignored;
    if (present) { EBG_SimulationCache.Restore(record.Simulation, ignored); }
    record.Simulation = null;
   }
   record.RequestRelease(why);
   record.FinishRelease();
  }
  m_Records.Clear();
  foreach (EXPG_PlanWaiter waiter : m_Waiters)
  {
   if (waiter.Finished) { continue; }
   waiter.Finished = true;
   waiter.OnFailed("a save was loaded; use EXPBG Add Garrison again");
  }
  m_Waiters.Clear();
  m_Plans.Clear();
  m_PendingImport = null;
  EXPG_SaveExclusion.ReleaseAll();
  if (discarded > 0) { PrintFormat("[EXPG LOAD] %1 garrisons of the previous scene discarded (%2); nobody was woken or respawned", discarded, why); }
 }

 bool Queue(array<ref EXPG_GarrisonSnapshot> ledger, out string reason)
 {
  reason = "no garrison load was started";
  if (!m_Importing || !ledger) { return false; }
  if (!EXPG_Ledger.Validate(ledger, reason)) { return false; }
  m_PendingImport = {};
  foreach (EXPG_GarrisonSnapshot saved : ledger)
  {
   int at = m_PendingImport.Count();
   while (at > 0 && m_PendingImport[at - 1].OrderIndex > saved.OrderIndex) { at--; }
   m_PendingImport.InsertAt(saved, at);
  }
  reason = "";
  return true;
 }

 // The carrier's world is final: create the loaded garrisons in saved order. Nothing
 // spawns here; each waits for its building's plan (ServiceLoaded).
 void Materialize()
 {
  if (!m_Importing) { return; }
  array<ref EXPG_GarrisonSnapshot> pending = m_PendingImport;
  m_PendingImport = null;
  m_Importing = false;
  if (!pending || pending.IsEmpty()) { return; }
  int created, lost, skipped, awake, simulation, full;
  foreach (EXPG_GarrisonSnapshot saved : pending)
  {
   EXPG_GarrisonRecord record = ImportRecord(saved);
   if (!record) { skipped++; continue; }
   created++;
   if (!record.Plan.Structure) { lost++; }
   if (saved.CacheState == 0) { awake++; }
   else if (saved.CacheState == 1) { simulation++; }
   else { full++; }
  }
  PrintFormat("[EXPG LOAD] garrisons=%1 lost=%2 skipped=%3 awake=%4 simulation=%5 full=%6", created, lost, skipped, awake, simulation, full);
  string message = string.Format("Loaded %1 garrisons from the save (%2 awake, %3 Simulation cached, %4 Full cached).", created, awake, simulation, full);
  if (lost > 0) { message += string.Format(" %1 lost their building and are released as ordinary squads.", lost); }
  if (skipped > 0) { message += string.Format(" %1 had no surviving guards or were already present (see [EXPG LOAD] in the server log).", skipped); }
  EXPG_GarrisonNotice.Post(message);
 }

 // One loaded garrison, created Full cached with no soldier in the world. Its squad
 // and survivors spawn at wake (casualties never), AI pinned until bound.
 protected EXPG_GarrisonRecord ImportRecord(EXPG_GarrisonSnapshot saved)
 {
  foreach (EXPG_GarrisonRecord existing : m_Records)
  {
   if (!existing.Finished && existing.Token == saved.Token)
   {
    PrintFormat("[EXPG LOAD] garrison %1 already exists; its saved copy is not loaded twice", saved.Token, level: LogLevel.WARNING);
    return null;
   }
  }
  if (saved.AliveCount() == 0)
  {
   PrintFormat("[EXPG LOAD] garrison %1 had no surviving guards; nothing to restore", saved.Token);
   return null;
  }
  string missing;
  IEntity building = saved.Site.Find(missing);
  EXPG_BuildingPlan plan;
  if (building)
  {
   plan = Prepare(building);
   if (!plan) { missing = m_PrepareFailure; }
  }
  bool lost = !plan;
  if (lost)
  {
   // No building to hold: a plan without structure keeps the origin for distances.
   plan = new EXPG_BuildingPlan();
   plan.Origin = saved.Site.Transform[3];
   plan.Done = true;
   plan.Error = missing;
   PrintFormat("[EXPG LOAD] garrison %1: %2 (%3); its survivors restore as an ordinary squad", saved.Token, missing, saved.Site.Describe(), level: LogLevel.WARNING);
  }
  EXPG_GarrisonRecord record = new EXPG_GarrisonRecord();
  record.Token = saved.Token;
  record.GeneratedBy = saved.GeneratedBy;
  vector settings = EXPG_GarrisonSettings.Normalize(Vector(saved.WakeDistance, saved.SleepDistance, saved.CacheMode));
  record.WakeDistance = settings[0];
  record.SleepDistance = settings[1];
  record.CacheMode = settings[2];
  record.Plan = plan;
  record.Site = saved.Site;
  record.Ready = true;
  record.Created = Now();
  record.RemapPending = true;
  record.WakeOnLoad = saved.CacheState != 2;
  record.SleepAfterLoad = saved.CacheState == 1;
  EBG_CacheGroup captured = new EBG_CacheGroup();
  captured.Id = m_NextFullId--;
  captured.Anchor = plan.Origin;
  array<ref EBG_PrefabSurvivor> rows = {};
  foreach (EXPG_MemberSnapshot row : saved.Members)
  {
   EXPG_GarrisonMember member = new EXPG_GarrisonMember();
   member.Plan = plan;
   member.CacheMember = new EBG_CacheMember();
   member.CacheMember.Id = row.Id;
   member.CacheMember.Dead = row.Dead;
   member.Fixed = row.Fixed;
   member.PostKind = row.PostKind;
   member.NodeIndex = -1;
   member.NodeHint = row.NodeHint;
   member.PostPosition = saved.Site.ToWorld(row.LocalPost);
   member.PostLook = saved.Site.DirToWorld(row.LocalLook);
   member.GivenPost = member.PostPosition;
   member.Arrived = true;
   vector spawn[4];
   SpawnTransform(saved.Site, row, lost, spawn);
   member.CacheMember.Position = spawn[3];
   record.Members.Insert(member);
   captured.Members.Insert(member.CacheMember);
   if (row.Dead)
   {
    captured.Dead++;
    continue;
   }
   EBG_PrefabSurvivor survivor = new EBG_PrefabSurvivor();
   survivor.Member = member.CacheMember;
   survivor.Prefab = row.Prefab;
   for (int axis = 0; axis < 4; axis++) { survivor.Transform[axis] = spawn[axis]; }
   survivor.Author = row.Author;
   survivor.Emplacement = new EBG_StaticEmplacement();
   row.FillCarry(survivor.Carry);
   // The squad leader is restored first, so he leads the recreated squad again.
   if (row.Id == saved.LeaderId) { rows.InsertAt(survivor, 0); }
   else { rows.Insert(survivor); }
   captured.FullMembers.Insert(member.CacheMember);
   captured.Alive++;
  }
  record.FullRecord = captured;
  record.Full = new EXPG_FullCache();
  record.Full.SetRecord(captured);
  record.Full.SetOwner(record);
  if (!record.Full.ImportCached(saved.Squad, rows))
  {
   PrintFormat("[EXPG LOAD] garrison %1: its squad snapshot could not be restored; not loaded", saved.Token, level: LogLevel.ERROR);
   return null;
  }
  if (lost) { record.RequestRelease("Loaded from save: " + missing + "; released as an ordinary squad"); }
  else if (saved.ReleaseRequested) { record.RequestRelease("Loaded from save: " + saved.ReleaseReason); }
  if (!record.ReleaseRequested) { record.Report("Loaded from save: analysing the building"); }
  m_Records.Insert(record);
  return record;
 }

 // Upright, at the saved last position (building-local), or at the world position
 // when the building is gone (snapped to the surface below).
 protected void SpawnTransform(EXPG_BuildingRef site, EXPG_MemberSnapshot row, bool lost, out vector transform[4])
 {
  vector point = site.ToWorld(row.LocalLast);
  vector facing = site.DirToWorld(row.LocalFacing);
  if (lost)
  {
   point = GroundPoint(row.WorldLast);
   facing = row.WorldFacing;
  }
  facing[1] = 0;
  float yaw = 0;
  if (facing.Length() > 0.1) { yaw = facing.ToYaw(); }
  Math3D.AnglesToMatrix(Vector(yaw, 0, 0), transform);
  transform[3] = point;
 }

 protected vector GroundPoint(vector point)
 {
  TraceParam trace = new TraceParam();
  trace.Start = point + "0 1.5 0";
  trace.End = point - "0 30 0";
  trace.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
  trace.LayerMask = EPhysicsLayerDefs.CharacterAI;
  float hit = m_World.TraceMove(trace, null);
  if (hit < 1) { return vector.Lerp(trace.Start, trace.End, hit) + "0 0.05 0"; }
  vector ground = point;
  ground[1] = m_World.GetSurfaceY(point[0], point[2]);
  return ground;
 }

 // A loaded garrison waits for its building's plan (analysed first while it must
 // wake or a player is near), then takes its posts back (Remap).
 protected bool ServiceLoaded(EXPG_GarrisonRecord record)
 {
  EXPG_BuildingPlan plan = record.Plan;
  if (plan.Structure && !plan.Done)
  {
   plan.LastUsed = Now();
   if (record.WakeOnLoad || Near(record, record.WakeDistance)) { plan.WaitedAt = Now(); }
   return false;
  }
  Remap(record);
  return true;
 }

 // Posts and patrol stops are saved building-local. A saved node is kept when it
 // still stands within 0.1 m of the post, else the nearest standing node within
 // 0.25 m is taken, else the post stays off the plan at the saved spot. A patrol
 // stop that cannot be found again becomes a fixed post where the patroller wakes.
 // An unusable plan (building changed or gone) releases the squad when it wakes.
 protected void Remap(EXPG_GarrisonRecord record)
 {
  record.RemapPending = false;
  EXPG_BuildingPlan plan = record.Plan;
  bool usable = plan.Structure && plan.Done && plan.Error.IsEmpty() && plan.Valid();
  int kept, moved, offPlan, fixedPatrols;
  foreach (EXPG_GarrisonMember member : record.Members)
  {
   if (member.CacheMember.Dead) { continue; }
   vector post = member.PostPosition;
   int node = -1;
   if (usable)
   {
    int hint = member.NodeHint;
    if (hint >= 0 && hint < plan.Nodes.Count() && plan.Nodes[hint].Reachable && vector.DistanceSq(plan.Nodes[hint].Position, post) <= 0.01) { node = hint; kept++; }
    else
    {
     node = plan.NearestNode(post, 0.25, true);
     if (node >= 0) { moved++; }
    }
   }
   if (!member.Fixed)
   {
    if (node >= 0 && plan.Nodes[node].Reachable)
    {
     member.NodeIndex = node;
     member.PatrolState = new EXPG_PatrolState();
     member.PatrolState.NodeIndex = node;
     member.PatrolState.Target = -1;
     member.PatrolState.From = member.CacheMember.Position;
     member.PatrolState.To = plan.Nodes[node].Position;
    }
    else
    {
     member.Fixed = true;
     member.NodeIndex = -1;
     member.PostPosition = member.CacheMember.Position;
     fixedPatrols++;
    }
   }
   else
   {
    member.NodeIndex = node;
    if (node < 0) { offPlan++; }
   }
   member.GivenPost = member.PostPoint();
  }
  PrintFormat("[EXPG LOAD] garrison %1 posts: kept %2, remapped %3, off the plan %4, patrols held as fixed posts %5, plan usable %6", record.Token, kept, moved, offPlan, fixedPatrols, usable);
  if (!usable && !record.ReleaseRequested)
  {
   string why = plan.Error;
   if (why.IsEmpty()) { why = "the building changed"; }
   record.RequestRelease("Loaded from save: " + why + "; released as an ordinary squad");
  }
  record.ParkPosts();
  if (!record.ReleaseRequested && record.Full && !record.WakeOnLoad) { record.Report("Full cached (loaded from save)"); }
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
  // A loaded garrison waits for its building's analysis, then takes its posts back.
  if (record.RemapPending && !ServiceLoaded(record)) { return; }
  // Durable Full: a Full-cached garrison has no squad in the world until it wakes.
  if (!record.Group && !record.Full) { record.RequestRelease("Squad deleted"); }
  if (!record.Plan.Valid()) { record.RequestRelease("Building moved or was replaced"); }
  record.SyncSettings();
  // One floor support check per group service catches damaged or removed floors
  // without rescanning the whole building. Never a release and never a teleport:
  // a guard whose floor gives way falls and holds where he lands (PostControl);
  // until then caching is held. Debris on a post is no floor loss while the guard
  // still stands on something. A Full-cached garrison has no guard in the world:
  // the round robin resumes where it stopped once it wakes (WakeFull checks every
  // restored guard's own position first).
  if (!record.Members.IsEmpty() && record.Plan.Structure && !(record.Full && record.Full.GetState() == EBG_FullGroupPhase.CACHED))
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
   bool wakeFull = record.ReleaseRequested || record.WakeOnLoad || RecordModeInUse(record) != 2;
   if (Near(record, record.WakeDistance)) { wakeFull = true; }
   if (record.Full.GetState() != EBG_FullGroupPhase.CACHED) { wakeFull = true; }
   if (!wakeFull || !Wake(record)) { return; }
  }
  if (record.Simulation)
  {
   // A Full garrison Simulation-cached (CDF legacy, or Full refused) runs mode 1 too.
   int simulationMode = RecordModeInUse(record);
   if (record.FallbackSimulation && simulationMode == 2) { simulationMode = 1; }
   bool wake = record.ReleaseRequested || !record.Group || simulationMode != 1;
   if (Near(record, record.WakeDistance)) { wake = true; }
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
   if (Now() - record.Created > 45)
   {
    if (!Initialize(record, true) && !record.ReleaseRequested) { record.RequestRelease("Squad initialization timed out; retained as a normal squad"); }
   }
   else { Initialize(record); }
   return;
  }
  ForgetLeavers(record);
  int alive;
  bool unsafe;
  // Why caching is held this Tick (the first reason found; NoteHold shows it).
  string hold;
  // Guards whose Unit Caching support is checked only once caching is due (below the
  // awake return), in member order; holdAt: how many came before the first other
  // hold reason (an earlier guard's support problem still names the hold).
  int holdAt = -1;
  m_SupportActors.Clear();
  m_SupportIds.Clear();
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
    if (hold.IsEmpty()) { hold = string.Format("guard %1 has no character controller", cached.Id); holdAt = m_SupportActors.Count(); }
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
    if (hold.IsEmpty()) { hold = string.Format("guard %1 is possessed by a player", cached.Id); holdAt = m_SupportActors.Count(); }
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
    if (hold.IsEmpty()) { hold = string.Format("guard %1 is being sent back to his post", cached.Id); holdAt = m_SupportActors.Count(); }
    if (member.OnPost(actor.GetOrigin()))
    {
     member.Returning = false;
     // On his post within its 1 m tolerance: the post control watches the spot he
     // stands on, so the same small offset (over the 0.5 m drift) is not reported
     // again every tick (a loop that held caching and flooded the log).
     member.Post.Return(actor.GetOrigin());
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
    if (hold.IsEmpty()) { hold = string.Format("the floor under guard %1's post gave way", cached.Id); holdAt = m_SupportActors.Count(); }
   }
   // Unit Caching support (EBG_SimulationCache.Unsupported) is costly and only
   // matters once caching is due: checked after the awake return below.
   m_SupportActors.Insert(actor);
   m_SupportIds.Insert(cached.Id);
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
   if (hold.IsEmpty()) { hold = "a soldier who is not one of its guards joined the squad (posts kept)"; holdAt = m_SupportActors.Count(); }
   break;
  }
  if (alive == 0) { record.RequestRelease("No surviving guards"); }
  if (record.ReleaseRequested) { record.FinishRelease(); return; }
  if (!record.BindControls())
  {
   unsafe = true;
   if (hold.IsEmpty()) { hold = UnboundGuard(record); holdAt = m_SupportActors.Count(); }
  }
  else if (record.Status == "Native controls are initializing") { record.Report("Garrison active"); }
  ServiceAlert(record);
  record.SyncSettings();
  // Awake by design (caching Off, a player near, just placed or woken): no hold.
  if (record.CacheMode == 0 || Near(record, record.SleepDistance) || Now() - record.Created < 15)
  { record.ClearSince = -1; ReleaseSettle(record); record.ClearHold(); return; }
  // Caching is due: the first guard in member order without Unit Caching support
  // holds it, unless another reason was found before him (that reason stays); the
  // alarm names its own reason.
  if (!record.AlertActive)
  {
   int limit = m_SupportActors.Count();
   if (holdAt >= 0) { limit = holdAt; }
   if (limit > 0)
   {
    bool preserve = CacheModeInUse(record.Group) == 1;
    for (int supportIndex = 0; supportIndex < limit; supportIndex++)
    {
     string problem = EBG_SimulationCache.Unsupported(m_SupportActors[supportIndex], preserve);
     if (problem.IsEmpty()) { continue; }
     unsafe = true;
     hold = string.Format("guard %1: %2", m_SupportIds[supportIndex], problem);
     break;
    }
   }
  }
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
  // Once a second: a native ledger is imported once persistence is active; load
  // notices reach late Game Masters. Owned entities stay out of the other saves: a
  // full exclusion sync every 5 s (every save and load forces its own sync first),
  // and in between the before-save hook is still installed as early as before.
  if (Now() >= m_NextExclusion)
  {
   m_NextExclusion = Now() + 1;
   if (!m_Importing)
   {
    if (Now() >= m_NextExclusionSync) { SyncExclusion(); m_NextExclusionSync = Now() + 5; }
    else { EXPG_SaveExclusion.IsSaveHooked(); }
   }
   ServiceNativeImport();
   EXPG_GarrisonNotice.Deliver();
  }
  if (!m_Plans.IsEmpty())
  {
   // About 4 ms of analysis per 100 ms pump: large buildings finish in seconds
   // instead of minutes. The budget is checked after every operation, so one
   // expensive batch cannot stall a frame (the operations run in the same order).
   EXPG_BuildingPlan analysing = NextAnalysis();
   int analysisStart = System.GetTickCount();
   while (analysing && !analysing.Done && System.GetTickCount() - analysisStart < 4) { analysing.Step(1); }
   // One plan per pump is checked for eviction (a waiting request keeps its plan used).
   m_PlanCursor = m_PlanCursor % m_Plans.Count();
   EXPG_BuildingPlan plan = m_Plans[m_PlanCursor++];
   bool used;
   foreach (EXPG_GarrisonRecord record : m_Records) { if (record.Plan == plan && !record.Finished) { used = true; break; } }
   if (!used && Now() - plan.LastUsed > 120) { m_Plans.RemoveItem(plan); }
  }
  ServiceWaiters();
  // The players Near reads, once a second and only while there is a garrison to
  // tick (a garrison created in this pump is ticked with a fresh list).
  if (!m_Records.IsEmpty() && Now() >= m_NextPlayers) { Players(); m_NextPlayers = Now() + 1; }
  // A carrier is replacing the garrisons: no record is ticked until it has finished.
  int serviceCount = Math.Min(4, m_Records.Count());
  if (m_Importing) { serviceCount = 0; }
  for (int i = 0; i < serviceCount && !m_Records.IsEmpty(); i++)
  {
   m_RecordCursor = m_RecordCursor % m_Records.Count();
   EXPG_GarrisonRecord record = m_Records[m_RecordCursor++];
   Tick(record);
   if (record.Finished) { m_Records.RemoveItem(record); }
  }
 }
}
