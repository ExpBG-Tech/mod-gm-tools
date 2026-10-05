// TEST ONLY. Real world/prefabs and production scheduler; no fabricated plan,
// forced cache state, replaced actors, synthetic player, or shortened timers.
class EXPG_GameplayActor
{
 EBG_CacheMember Member;
 SCR_ChimeraCharacter Actor;
 EntityID Id;
 vector Post;
 vector SleepingPosition;
 bool Fixed;
 bool Dead;
 int NodeAtSleep;
 vector FullPose[4];
 int Presentation;
}

// Read-only observations around the production fresh ledger and roster freeze.
// No test hook adds/removes agents, changes slots, or authorizes a deletion.
modded class SCR_AIGroup
{
 ref array<EntityID> EXPG_TestNativeAdmissions = {};
 ref array<EntityID> EXPG_TestBeforeTrim = {};
 ref array<EntityID> EXPG_TestAcknowledgedRemovals = {};
 int EXPG_TestInitialAdmissions;
 EntityID EXPG_TestOriginalLeader;
 bool EXPG_TestTrimCaptured;

 override bool EXPG_BeginFreshRoster(int expected)
 {
  bool admitted = super.EXPG_BeginFreshRoster(expected);
  // The production boundary accepts native actors returned synchronously by
  // SpawnEntityPrefab too. Observe those actual entities instead of requiring
  // every supported engine path to defer all twelve additions to the queue.
  if (admitted && expected == 12)
  {
   foreach (IEntity actor : m_EXPG_FreshActors)
   {
    EXPG_TestNativeAdmissions.Insert(actor.GetID());
    EXPG_TestInitialAdmissions++;
    PrintFormat("[EXPG TRIM INITIAL] actor=%1 count=%2", actor.GetID(), EXPG_TestInitialAdmissions);
   }
  }
  return admitted;
 }

 override void OnAgentAdded(AIAgent child)
 {
  bool nativeMember = m_EXPG_FreshExpected == 12 && m_EXPG_InMemberSpawn;
  IEntity actor;
  if (child) actor = child.GetControlledEntity();
  super.OnAgentAdded(child);
  if (nativeMember && actor && m_EXPG_FreshActors && !m_EXPG_FreshInvalid && m_EXPG_FreshActors.Contains(actor))
  {
   EXPG_TestNativeAdmissions.Insert(actor.GetID());
   PrintFormat("[EXPG TRIM ADMISSION] actor=%1 count=%2", actor.GetID(), EXPG_TestNativeAdmissions.Count());
  }
 }

 override void EBG_UseCapturedRoster()
 {
  // Manager invokes this after full preflight, immediately before trimming.
  if (!EXPG_TestTrimCaptured && m_EXPG_FreshExpected == 12 && EXPG_FreshRosterMatches(12))
  {
   EXPG_TestTrimCaptured = true;
   array<AIAgent> agents = {}; GetAgents(agents);
   foreach (AIAgent agent : agents) EXPG_TestBeforeTrim.Insert(agent.GetControlledEntity().GetID());
   IEntity leader = GetLeaderEntity();
   if (leader) EXPG_TestOriginalLeader = leader.GetID();
   PrintFormat("[EXPG TRIM BEFORE] nativeAdmissions=%1 roster=%2 leader=%3", EXPG_TestNativeAdmissions.Count(), EXPG_TestBeforeTrim.Count(), EXPG_TestOriginalLeader);
  }
  super.EBG_UseCapturedRoster();
 }

 override void OnAgentRemoved(AIAgent child)
 {
  IEntity actor;
  if (child) actor = child.GetControlledEntity();
  bool expected = EXPG_TestTrimCaptured && actor && actor == m_EXPG_ExpectedRemoval;
  EntityID id;
  if (actor) id = actor.GetID();
  super.OnAgentRemoved(child);
  if (expected && m_EXPG_FreshActors && !m_EXPG_FreshInvalid && !m_EXPG_ExpectedRemoval && !m_EXPG_FreshActors.Contains(actor))
  {
   EXPG_TestAcknowledgedRemovals.Insert(id);
   PrintFormat("[EXPG TRIM REMOVAL] actor=%1 acknowledged=%2", id, EXPG_TestAcknowledgedRemovals.Count());
  }
 }

 bool EXPG_TestTrimResult()
 {
  array<AIAgent> agents = {}; GetAgents(agents);
  array<EntityID> retained = {};
  bool originals = EXPG_TestTrimCaptured && EXPG_TestBeforeTrim.Count() == 12 && EXPG_TestNativeAdmissions.Count() == 12;
  foreach (EntityID id : EXPG_TestBeforeTrim)
   if (!EXPG_TestNativeAdmissions.Contains(id)) originals = false;
  foreach (AIAgent agent : agents)
  {
   IEntity actor;
   if (agent) actor = agent.GetControlledEntity();
   if (!actor) { originals = false; continue; }
   EntityID id = actor.GetID();
   if (!EXPG_TestBeforeTrim.Contains(id) || retained.Contains(id) || EXPG_TestAcknowledgedRemovals.Contains(id)) originals = false;
   retained.Insert(id);
  }
  int deleted = 0;
  foreach (EntityID id : EXPG_TestBeforeTrim)
  {
   if (retained.Contains(id)) continue;
   if (EXPG_TestAcknowledgedRemovals.Contains(id) && !GetWorld().FindEntityByID(id)) deleted++;
   else originals = false;
  }
  IEntity leader = GetLeaderEntity();
  bool sameLeader = leader && leader.GetID() == EXPG_TestOriginalLeader && retained.Contains(EXPG_TestOriginalLeader);
  PrintFormat("[EXPG TRIM SPAWN] synchronous=%1 queued=%2", EXPG_TestInitialAdmissions, EXPG_TestNativeAdmissions.Count() - EXPG_TestInitialAdmissions);
  PrintFormat("[EXPG TRIM RESULT] admitted=%1 before=%2 retained=%3 acknowledged=%4 deleted=%5 leaderPreserved=%6 originals=%7", EXPG_TestNativeAdmissions.Count(), EXPG_TestBeforeTrim.Count(), retained.Count(), EXPG_TestAcknowledgedRemovals.Count(), deleted, sameLeader, originals);
  return originals && retained.Count() == 9 && EXPG_TestAcknowledgedRemovals.Count() == 3 && deleted == 3 && sameLeader;
 }
}

modded class SCR_CharacterControllerComponent
{
 int EXPG_TestPostCallbacks;
 int EXPG_TestPostPlayerFlag;
 int EXPG_TestPostZeroInputs;
 float EXPG_TestLastSpeed;
 vector EXPG_TestLastDirection;
 override void OnDeath(IEntity instigatorEntity, notnull Instigator instigator)
 {
  SCR_ChimeraCharacter actor = SCR_ChimeraCharacter.Cast(GetOwner());
  PrintFormat("[EXPG DEATH EVENT] time=%1 controllerDead=%2 damageDestroyed=%3 life=%4 actor=%5", GetGame().GetWorld().GetWorldTime(), IsDead(), actor && actor.GetDamageManager().IsDestroyed(), GetLifeState(), actor);
  super.OnDeath(instigatorEntity, instigator);
 }
 protected override void OnPrepareControls(IEntity owner, ActionManager am, float dt, bool player)
 {
  super.OnPrepareControls(owner, am, dt, player);
  if (!EXPG_GetPostControl()) return;
  EXPG_TestPostCallbacks++;
  if (player) EXPG_TestPostPlayerFlag++;
  CharacterInputContext input = GetInputContext();
  if (!input) return;
  input.GetMovement(EXPG_TestLastSpeed, EXPG_TestLastDirection);
  if (EXPG_TestLastSpeed == 0 && vector.DistanceSq(EXPG_TestLastDirection, vector.Zero) < 0.0001) EXPG_TestPostZeroInputs++;
 }
}

// Diagnostic only; production owns every movement control and speed limit.
modded class EXPG_PostControl
{
 override bool Tick()
 {
  bool wasOwned = IsOwnedActor();
  bool setting = m_SpeedSetting != null;
  bool binding = m_Utility && m_Utility.EXPG_GetPostControl() == this;
  vector position;
  if (m_Actor) position = m_Actor.GetOrigin();
  float drift = vector.DistanceSq(position, m_Position);
  bool result = super.Tick();
  if (!result) PrintFormat("[EXPG POST REFUSAL] owned=%1 setting=%2 binding=%3 position=%4 driftSq=%5", wasOwned, setting, binding, position, drift);
  return result;
 }
}

modded class EXPG_GarrisonRecord
{
 int EXPG_TestRestoreReports;
 void EXPG_TestBodyLayer(int memberId, SCR_ChimeraCharacter actor, int layer)
 {
  vector point = actor.GetOrigin();
  vector endPoint = point + Vector(0, 0.01, 0);
  TraceBox body = new TraceBox();
  body.Start = point + Vector(0, 0.05, 0); body.End = endPoint + Vector(0, 0.05, 0);
  body.Mins = Vector(-0.23, 0, -0.23); body.Maxs = Vector(0.23, 1.75, 0.23);
  body.Flags = TraceFlags.WORLD | TraceFlags.ENTS; body.Exclude = actor;
  body.LayerMask = layer;
  float occupied = GetGame().GetWorld().TracePosition(body, null);
  string prefab;
  if (body.TraceEnt) prefab = SCR_ResourceNameUtils.GetPrefabName(body.TraceEnt);
  PrintFormat("[EXPG RESTORE LAYER BODY] member=%1 layer=%2 overlap=%3 collider=%4 index=%5 prefab=%6", memberId, layer, occupied, body.ColliderName, body.ColliderIndex, prefab);
  float swept = GetGame().GetWorld().TraceMove(body, null);
  prefab = "";
  if (body.TraceEnt) prefab = SCR_ResourceNameUtils.GetPrefabName(body.TraceEnt);
  PrintFormat("[EXPG RESTORE LAYER SWEEP] member=%1 layer=%2 fraction=%3 collider=%4 index=%5 prefab=%6", memberId, layer, swept, body.ColliderName, body.ColliderIndex, prefab);
 }
 override void Report(string message)
 {
  bool changed = Status != message;
  super.Report(message);
  if (!changed || message != "Full recovery held: restored position or membership is unsafe" || EXPG_TestRestoreReports >= 2) return;
  EXPG_TestRestoreReports++;
  foreach (EXPG_GarrisonMember member : Members)
  {
   if (member.CacheMember.Dead || member.CacheMember.WasPlayer) continue;
   SCR_ChimeraCharacter actor = member.CacheMember.Entity;
   if (!actor) { PrintFormat("[EXPG RESTORE CHECK] member=%1 actorMissing=1", member.CacheMember.Id); continue; }
   vector point = actor.GetOrigin();
   vector endPoint = point + Vector(0, 0.01, 0);
   bool inside = Plan.Inside(point, 0.25);
   bool support = Plan.Supported(point, 0.2, actor);
   bool clearance = Plan.ClearBody(point, endPoint, actor);
   PrintFormat("[EXPG RESTORE CHECK] member=%1 position=%2 sameGroup=%3 inside=%4 support=%5 bodyClear=%6", member.CacheMember.Id, point, actor.GetCharacterGroup() == Group, inside, support, clearance);
   TraceParam floor = new TraceParam();
   floor.Start = point + Vector(0, 0.2, 0); floor.End = point - Vector(0, 0.4, 0);
   floor.Flags = TraceFlags.WORLD | TraceFlags.ENTS; floor.Exclude = actor;
   float floorHit = GetGame().GetWorld().TraceMove(floor, null);
   string floorPrefab;
   if (floor.TraceEnt) floorPrefab = SCR_ResourceNameUtils.GetPrefabName(floor.TraceEnt);
   PrintFormat("[EXPG RESTORE FLOOR] member=%1 fraction=%2 normal=%3 hit=%4 prefab=%5", member.CacheMember.Id, floorHit, floor.TraceNorm, floor.TraceEnt, floorPrefab);
   TraceBox body = new TraceBox();
   body.Start = point + Vector(0, 0.05, 0); body.End = endPoint + Vector(0, 0.05, 0);
   body.Mins = Vector(-0.23, 0, -0.23); body.Maxs = Vector(0.23, 1.75, 0.23);
   body.Flags = TraceFlags.WORLD | TraceFlags.ENTS; body.Exclude = actor;
   float occupied = GetGame().GetWorld().TracePosition(body, null);
   string bodyPrefab;
   if (body.TraceEnt) bodyPrefab = SCR_ResourceNameUtils.GetPrefabName(body.TraceEnt);
   PrintFormat("[EXPG RESTORE BODY] member=%1 overlap=%2 hit=%3 prefab=%4", member.CacheMember.Id, occupied, body.TraceEnt, bodyPrefab);
   float swept = GetGame().GetWorld().TraceMove(body, null);
   bodyPrefab = "";
   if (body.TraceEnt) bodyPrefab = SCR_ResourceNameUtils.GetPrefabName(body.TraceEnt);
   PrintFormat("[EXPG RESTORE SWEEP] member=%1 fraction=%2 hit=%3 prefab=%4", member.CacheMember.Id, swept, body.TraceEnt, bodyPrefab);
   EXPG_TestBodyLayer(member.CacheMember.Id, actor, EPhysicsLayerDefs.CharacterAI);
   EXPG_TestBodyLayer(member.CacheMember.Id, actor, EPhysicsLayerDefs.Character);
  }
 }
 override void FinishRelease()
 {
  PrintFormat("[EXPG RELEASE] requested=%1 ready=%2 status=%3 planValid=%4 group=%5 preparing=%6", ReleaseRequested, Ready, Status, Plan.Valid(), Group, EBG_OptimizerControl.Preparing);
  foreach (EXPG_GarrisonMember member : Members)
  {
   SCR_ChimeraCharacter actor = member.CacheMember.Entity;
   if (!actor) { PrintFormat("[EXPG RELEASE ACTOR] member=%1 missing=1 dead=%2", member.CacheMember.Id, member.CacheMember.Dead); continue; }
   AIControlComponent control = actor.GetAIControlComponent();
   AIGroup parentGroup;
   if (control && control.GetAIAgent()) parentGroup = control.GetAIAgent().GetParentGroup();
   PrintFormat("[EXPG RELEASE ACTOR] member=%1 position=%2 characterGroup=%3 agentGroup=%4 dead=%5 wasPlayer=%6 post=%7", member.CacheMember.Id, actor.GetOrigin(), actor.GetCharacterGroup(), parentGroup, member.CacheMember.Dead, member.CacheMember.WasPlayer, member.Post != null);
   PrintFormat("[EXPG RELEASE LIFE] member=%1 controllerDead=%2 damageDestroyed=%3 life=%4 health=%5", member.CacheMember.Id, actor.GetCharacterController().IsDead(), actor.GetDamageManager().IsDestroyed(), actor.GetCharacterController().GetLifeState(), actor.GetDamageManager().GetHealthScaled());
   vector postPoint = Plan.Nodes[member.NodeIndex].Position;
   TraceParam floorTrace = new TraceParam();
   floorTrace.Start = postPoint + "0 0.2 0"; floorTrace.End = postPoint - "0 0.4 0";
   floorTrace.Flags = TraceFlags.WORLD | TraceFlags.ENTS; floorTrace.Exclude = actor;
   float fraction = GetGame().GetWorld().TraceMove(floorTrace, null);
   string prefab;
   if (floorTrace.TraceEnt) prefab = SCR_ResourceNameUtils.GetPrefabName(floorTrace.TraceEnt);
   PrintFormat("[EXPG RELEASE FLOOR] member=%1 point=%2 fraction=%3 normal=%4 hit=%5 structural=%6 prefab=%7", member.CacheMember.Id, postPoint, fraction, floorTrace.TraceNorm, floorTrace.TraceEnt, Plan.StructuralFloor(floorTrace.TraceEnt), prefab);
  }
  super.FinishRelease();
 }
}

modded class EXPG_BuildingPlan
{
 void EXPG_TestBoundary()
 {
  int reported = 0;
  for (int a = 0; a < Nodes.Count() && reported < 80; a++)
  {
   if (!Nodes[a].Reachable) continue;
   for (int b = 0; b < Nodes.Count() && reported < 80; b++)
   {
    if (Nodes[b].Reachable || Math.AbsFloat(Nodes[a].Position[1] - Nodes[b].Position[1]) > 0.48 || vector.DistanceSq(Nodes[a].Position, Nodes[b].Position) > 6.25) continue;
    vector from = Nodes[a].Position, to = Nodes[b].Position;
    TraceBox body = new TraceBox();
    body.Start = from + "0 0.45 0"; body.End = to + "0 0.45 0";
    body.Mins = "-0.23 0 -0.23"; body.Maxs = "0.23 1.35 0.23";
    body.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
    body.ExcludeArray = m_TraversableDoors;
    float fraction = GetGame().GetWorld().TraceMove(body, null);
    string prefab;
    if (body.TraceEnt) prefab = SCR_ResourceNameUtils.GetPrefabName(body.TraceEnt);
    PrintFormat("[EXPG BOUNDARY] a=%1 b=%2 from=%3 to=%4 fraction=%5 blocker=%6 walk=%7 doors=%8", a, b, from, to, fraction, prefab, WalkEdge(from, to), m_TraversableDoors.Count());
    reported++;
   }
  }
  foreach (IEntity door : m_TraversableDoors)
  {
   NavmeshCustomLinkComponent link = NavmeshCustomLinkComponent.Cast(door.FindComponent(NavmeshCustomLinkComponent));
   PrintFormat("[EXPG PORTAL] origin=%1 angles=%2 soldiers=%3", door.GetOrigin(), door.GetAngles(), link.HasLinkOfNavmeshType("Soldiers"));
  }
 }
}

// Observe the production adapter in the same callback as each native spawn.
// No state forcing, transform correction, shortened timer or substitute actor.
modded class EXPG_FullCache
{
 bool EXPG_TestParity = true;
 bool EXPG_TestHolds = true;
 bool EXPG_TestBindings = true;
 ref array<EBG_CacheMember> EXPG_TestSeen = {};
 bool EXPG_TestPose(EBG_CacheMember member, out vector pose[4])
 {
  foreach (EBG_PrefabSurvivor row : m_Survivors)
   if (row.Member == member) { Math3D.MatrixCopy(row.Transform, pose); return true; }
  return false;
 }
 override void Poll()
 {
  super.Poll();
  foreach (EBG_PrefabSurvivor row : m_Survivors)
  {
   if (!row.Created || !row.Entity || row.Member.Dead || row.Member.WasPlayer) continue;
   bool first = !EXPG_TestSeen.Contains(row.Member);
   if (first) EXPG_TestSeen.Insert(row.Member);
   vector actual[4]; row.Entity.GetWorldTransform(actual);
   bool matches = true;
   for (int axis = 0; axis < 4; axis++)
    if (vector.DistanceSq(actual[axis], row.Transform[axis]) > 0.000001) matches = false;
   EXPG_TestParity = EXPG_TestParity && matches;
   EXPG_FullHold hold = FindHold(row.Member);
   EXPG_TestHolds = EXPG_TestHolds && hold && hold.Policy && hold.Policy.Agent && hold.Policy.Agent.GetPermanentLOD() == AIAgent.GetMaxLOD();
   if (first && hold && hold.Policy && hold.Policy.Agent)
    PrintFormat("[EXPG FULL HELD POLICY] member=%1 savedPermanent=%2 savedLOD=%3 savedActive=%4 currentPermanent=%5 currentLOD=%6 currentActive=%7", row.Member.Id, hold.Policy.PermanentLOD, hold.Policy.LOD, hold.Policy.Active, hold.Policy.Agent.GetPermanentLOD(), hold.Policy.Agent.GetLOD(), hold.Policy.Agent.IsAIActivated());
   if (first || !matches || !EXPG_TestHolds) PrintFormat("[EXPG FULL SPAWN] member=%1 pass=%2 captured=%3 actual=%4 basisTolerance=0.001 positionTolerance=0.001 held=%5", row.Member.Id, matches, row.Transform[3], actual[3], EXPG_TestHolds);
  }
 }
 override bool ReleaseRestored()
 {
  foreach (EBG_PrefabSurvivor row : m_Survivors)
  {
   if (!row.Entity || row.Member.Dead || row.Member.WasPlayer) continue;
   vector actualPose[4]; row.Entity.GetWorldTransform(actualPose);
   for (int axis = 0; axis < 4; axis++)
    if (vector.DistanceSq(actualPose[axis], row.Transform[axis]) > 0.000001) EXPG_TestParity = false;
   EXPG_FullHold hold = FindHold(row.Member);
   EXPG_TestHolds = EXPG_TestHolds && hold && hold.Policy && hold.Policy.Agent && hold.Policy.Agent.GetPermanentLOD() == AIAgent.GetMaxLOD();
  }
  if (m_Owner && !m_Owner.ReleaseRequested)
  {
   foreach (EXPG_GarrisonMember member : m_Owner.Members)
   {
    if (member.CacheMember.Dead || member.CacheMember.WasPlayer) continue;
    if (member.Fixed) { EXPG_TestBindings = EXPG_TestBindings && member.Post != null; }
    else
    {
     EXPG_PatrolState actual;
     if (member.Patrol) actual = member.Patrol.CaptureState();
     EXPG_PatrolState saved = member.PatrolState;
     EXPG_TestBindings = EXPG_TestBindings && actual && saved && actual.NodeIndex == saved.NodeIndex && actual.Target == saved.Target;
    }
   }
  }
  if (m_Owner && m_Owner.Group && m_GroupPolicy) PrintFormat("[EXPG FULL GROUP POLICY] permanent=%1 LOD=%2 active=%3 savedPermanent=%4 savedLOD=%5 savedActive=%6", m_Owner.Group.GetPermanentLOD(), m_Owner.Group.GetLOD(), m_Owner.Group.IsAIActivated(), m_GroupPolicy.PermanentLOD, m_GroupPolicy.LOD, m_GroupPolicy.Active);
  bool released = super.ReleaseRestored();
  if (released && m_Owner && m_Owner.Group)
  {
   foreach (EXPG_GarrisonMember current : m_Owner.Members)
   {
    SCR_ChimeraCharacter actor = current.CacheMember.Entity;
    if (current.CacheMember.Dead || !actor) continue;
    AIControlComponent control = actor.GetAIControlComponent();
    AIAgent agent;
    if (control) agent = control.GetAIAgent();
    if (agent) PrintFormat("[EXPG FULL RELEASE POLICY] member=%1 active=%2 permanentLOD=%3 LOD=%4 groupPermanent=%5 groupLOD=%6", current.CacheMember.Id, agent.IsAIActivated(), agent.GetPermanentLOD(), agent.GetLOD(), m_Owner.Group.GetPermanentLOD(), m_Owner.Group.GetLOD());
   }
  }
  return released;
 }
}

class EXPG_GarrisonGameplayClass : GenericEntityClass {}
class EXPG_GarrisonGameplay : GenericEntity
{
 static const bool EXPG_TEST_FRESH_TRIM = false;
 int RequestedCount = 4;
 int ExpectedCount = 4;
 IEntity Structure;
 SCR_AIGroup Group;
 AIWaypoint ForceMove;
 EXPG_GarrisonManager Manager;
 ref EXPG_GarrisonRecord Record;
 ref EXPG_FullCache FullTransaction;
 ref array<ref EXPG_GameplayActor> Originals = {};
 ObserversSystem Observers;
 int ObserverKey, Phase, Checks, Failures, FixedCount, FullCycles;
 float Started, Next, PhaseStarted;
 float AwakeSince = -1;
 bool Finished;
 vector Point = "4773.46 0 7094.57";
 SCR_ChimeraCharacter MotionActor;
 CharacterControllerComponent MotionController;
 SCR_AIUtilityComponent MotionUtility;
 ECharacterStance OriginalStance;
 vector InitialForward, LookDirection;
 bool ObservedBodyTurn, ObservedHeadAim;

 void EXPG_GarrisonGameplay(IEntitySource src, IEntity parent) { SetEventMask(EntityEvent.INIT | EntityEvent.FRAME); }
 float Now() { return GetGame().GetWorld().GetWorldTime() * 0.001; }
 override void EOnInit(IEntity owner)
 {
  if (!Replication.IsServer()) { ClearEventMask(EntityEvent.FRAME); return; }
  Started = Now(); Next = Started + 15;
  if (EXPG_TEST_FRESH_TRIM) RequestedCount = 12;
  ObserverKey = "EXPG_GarrisonGameplay.Observer".Hash();
  Print("[EXPG GAMEPLAY BEGIN] nativeObserver=1 wakeTrigger=cacheOff connectedPlayer=0");
 }
 bool Check(bool value, string label)
 {
  Checks++; if (!value) Failures++;
  PrintFormat("[EXPG GAMEPLAY CHECK] pass=%1 %2", value, label);
  return value;
 }
 void RemoveObserver()
 {
  if (Observers && GetGame() && GetGame().GetWorld() && GetGame().GetWorld().FindSystem(ObserversSystem) == Observers)
   Observers.RemoveObserverSP(ObserverKey);
 }
 void ~EXPG_GarrisonGameplay() { RemoveObserver(); }
 void Finish(string reason)
 {
  if (Finished) return;
  Finished = true; RemoveObserver(); ClearEventMask(EntityEvent.FRAME);
  PrintFormat("[EXPG GAMEPLAY RESULT] phase=%1 checks=%2 failures=%3 actors=%4 fixedPosts=%5 reason=%6", Phase, Checks, Failures, Originals.Count(), FixedCount, reason);
  GetGame().RequestClose();
 }
 void Advance(int phase)
 {
  Phase = phase; PhaseStarted = Now();
  PrintFormat("[EXPG GAMEPLAY PHASE] phase=%1 elapsed=%2", Phase, Now() - Started);
 }
 EntitySpawnParams Params(vector point, float elevation = 0)
 {
  EntitySpawnParams spawn = new EntitySpawnParams(); spawn.TransformMode = ETransformMode.WORLD;
  Math3D.AnglesToMatrix(vector.Zero, spawn.Transform);
  point[1] = GetGame().GetWorld().GetSurfaceY(point[0], point[2]) + elevation;
  spawn.Transform[3] = point; return spawn;
 }
 bool SameActors()
 {
  if (!Group || Originals.Count() != ExpectedCount || Group.GetAgentsCount() != LivingCount()) return false;
  array<AIAgent> agents = {}; Group.GetAgents(agents);
  foreach (EXPG_GameplayActor saved : Originals)
  {
   if (saved.Dead)
   {
    if (!saved.Member.Dead || (saved.Member.Entity && !saved.Member.Entity.GetCharacterController().IsDead())) return false;
    continue;
   }
   if (!saved.Actor || saved.Actor.GetID() != saved.Id || saved.Actor.GetCharacterGroup() != Group) return false;
   if (!saved.Actor.GetCharacterController() || saved.Actor.GetCharacterController().IsDead()) return false;
   bool found;
   foreach (AIAgent agent : agents) { if (agent && agent.GetControlledEntity() == saved.Actor) found = true; }
   if (!found) return false;
  }
  return true;
 }
 int LivingCount()
 {
  int count = 0;
  foreach (EXPG_GameplayActor saved : Originals) if (!saved.Dead) count++;
  return count;
 }
 bool NativeAwake(bool report = false)
 {
  bool ready = true;
  foreach (EXPG_GameplayActor saved : Originals)
  {
   if (saved.Dead) continue;
   AIControlComponent control;
   AIAgent agent;
   if (saved.Actor) control = saved.Actor.GetAIControlComponent();
   if (control) agent = control.GetAIAgent();
   if (!agent) { ready = false; if (report) PrintFormat("[EXPG WAKE READY] member=%1 agentMissing=1", saved.Member.Id); continue; }
   if (!agent.IsAIActivated() || agent.GetLOD() != 0) ready = false;
   if (report) PrintFormat("[EXPG WAKE READY] member=%1 active=%2 lod=%3 permanent=%4", saved.Member.Id, agent.IsAIActivated(), agent.GetLOD(), agent.GetPermanentLOD());
  }
  return ready;
 }
 bool PostsHeld()
 {
  foreach (EXPG_GameplayActor saved : Originals)
   if (!saved.Dead && saved.Fixed && (!saved.Actor || vector.DistanceSq(saved.Actor.GetOrigin(), saved.Post) > 0.25)) return false;
  return FixedCount > 0;
 }
 bool StartMotionProbe()
 {
  foreach (EXPG_GameplayActor saved : Originals)
   if (!saved.Dead && saved.Fixed) { MotionActor = saved.Actor; break; }
  if (!MotionActor) return false;
  MotionController = MotionActor.GetCharacterController();
  AIControlComponent control = MotionActor.GetAIControlComponent();
  if (!MotionController || !control || !control.GetAIAgent()) return false;
  MotionUtility = SCR_AIUtilityComponent.Cast(control.GetAIAgent().FindComponent(SCR_AIUtilityComponent));
  if (!MotionUtility || !MotionUtility.m_LookAction || !MotionUtility.EXPG_GetPostControl() || !MotionController.GetHeadAimingComponent()) return false;
  OriginalStance = MotionController.GetStance();
  SCR_AIStanceHandling.SetStance(MotionController, ECharacterStance.CROUCH);
  Advance(15); return true;
 }
 void ReportCasualty()
 {
  EXPG_GameplayActor casualty = Originals[Originals.Count() - 1];
  PrintFormat("[EXPG CASUALTY STATE] exists=%1 memberDead=%2 nativeCount=%3 expected=%4", casualty.Actor != null, casualty.Member.Dead, Group.GetAgentsCount(), ExpectedCount - 1);
  if (!casualty.Actor) return;
  CharacterControllerComponent controller = casualty.Actor.GetCharacterController();
  SCR_DamageManagerComponent damage = casualty.Actor.GetDamageManager();
  PrintFormat("[EXPG CASUALTY LIFE] controllerDead=%1 damageDestroyed=%2 life=%3 characterGroup=%4 health=%5", controller.IsDead(), damage.IsDestroyed(), controller.GetLifeState(), casualty.Actor.GetCharacterGroup(), damage.GetHealthScaled());
  array<AIAgent> agents = {}; Group.GetAgents(agents);
  foreach (AIAgent agent : agents)
   PrintFormat("[EXPG CASUALTY AGENT] agent=%1 entity=%2 active=%3 permanentLOD=%4 LOD=%5", agent, agent.GetControlledEntity(), agent.IsAIActivated(), agent.GetPermanentLOD(), agent.GetLOD());
 }
 bool Presentation(bool sleeping)
 {
  foreach (EXPG_GameplayActor saved : Originals)
  {
   if (saved.Dead) continue;
   if (!saved.Actor || saved.Actor.EBG_IsSimulationCached() != sleeping) return false;
   int flags = saved.Actor.GetFlags() & (EntityFlags.VISIBLE | EntityFlags.TRACEABLE);
   if (sleeping && flags != 0) return false;
   if (!sleeping && flags != saved.Presentation) return false;
  }
  return true;
 }
 void ReportActors()
 {
  foreach (EXPG_GameplayActor saved : Originals)
  {
   if (!saved.Actor) continue;
   PrintFormat("[EXPG GAMEPLAY ACTOR] phase=%1 original=%2 current=%3 group=%4 position=%5 post=%6 fixed=%7 cached=%8", Phase, saved.Id, saved.Actor.GetID(), saved.Actor.GetCharacterGroup(), saved.Actor.GetOrigin(), saved.Post, saved.Fixed, saved.Actor.EBG_IsSimulationCached());
   SCR_CharacterControllerComponent controller = SCR_CharacterControllerComponent.Cast(saved.Actor.GetCharacterController());
   AICharacterMovementComponent movement = AICharacterMovementComponent.Cast(saved.Actor.FindComponent(AICharacterMovementComponent));
   AIControlComponent control = saved.Actor.GetAIControlComponent();
   SCR_AIUtilityComponent utility;
   if (control && control.GetAIAgent()) utility = SCR_AIUtilityComponent.Cast(control.GetAIAgent().FindComponent(SCR_AIUtilityComponent));
   SCR_AIBehaviorBase behavior;
   if (utility) behavior = utility.GetCurrentBehavior();
   if (controller && movement)
   {
    PrintFormat("[EXPG INPUT TRACE] member=%1 bound=%2 callbacks=%3 playerFlag=%4 zeroInputs=%5 inputSpeed=%6 inputDir=%7", saved.Member.Id, controller.EXPG_GetPostControl() != null, controller.EXPG_TestPostCallbacks, controller.EXPG_TestPostPlayerFlag, controller.EXPG_TestPostZeroInputs, controller.EXPG_TestLastSpeed, controller.EXPG_TestLastDirection);
    PrintFormat("[EXPG MOTION TRACE] member=%1 velocity=%2 wanted=%3 override=%4 behavior=%5", saved.Member.Id, controller.GetVelocity(), movement.GetMovementTypeWanted(), movement.GetMovementTypeOverride(), behavior);
   }
  }
 }
 void ReportPlan(EXPG_BuildingPlan plan)
 {
  int entrances = 0, reachable = 0, detailed = 0;
  foreach (EXPG_BuildingNode node : plan.Nodes)
  {
   if (node.Entrance) entrances++;
   if (node.Reachable) reachable++;
  }
  PrintFormat("[EXPG PLAN] nodes=%1 slots=%2 bounds=%3..%4 origin=%5 entrances=%6 reachable=%7", plan.Nodes.Count(), plan.Slots.Count(), plan.Mins, plan.Maxs, plan.Origin, entrances, reachable);
  plan.EXPG_TestBoundary();
  array<IEntity> parts = {};
  SCR_EntityHelper.GetHierarchyEntityList(plan.Structure, parts);
  foreach (IEntity part : parts)
  {
   DoorComponent door = DoorComponent.Cast(part.FindComponent(DoorComponent));
   if (!door) continue;
   vector mins, maxs; part.GetBounds(mins, maxs);
   PrintFormat("[EXPG DOOR] position=%1 center=%2 state=%3 navLink=%4 prefab=%5", part.GetOrigin(), part.CoordToParent((mins + maxs) * 0.5), door.GetNormalizedDoorState(), part.FindComponent(NavmeshCustomLinkComponent) != null, SCR_ResourceNameUtils.GetPrefabName(part));
  }
  for (int n = 0; n < plan.Nodes.Count(); n++)
  {
   EXPG_BuildingNode sample = plan.Nodes[n];
   if (n < 128 || sample.Entrance || sample.Reachable || plan.Slots.Contains(n))
    PrintFormat("[EXPG PLAN NODE] index=%1 position=%2 entrance=%3 reachable=%4 links=%5 score=%6 selected=%7", n, sample.Position, sample.Entrance, sample.Reachable, sample.Links.Count(), sample.Score, plan.Slots.Contains(n));
   // Diagnose ground-level entry candidates without changing production state.
   if (detailed >= 128 || Math.AbsFloat(sample.Position[1] - plan.Origin[1]) > 1) continue;
   for (int side = 0; side < 4; side++)
   {
    vector outside = sample.Position + vector.FromYaw(side * 90 + plan.Angles[0]) * 2.25;
    if (plan.Inside(outside)) continue;
    detailed++;
    float ground = GetGame().GetWorld().GetSurfaceY(outside[0], outside[2]);
    outside[1] = ground + 0.05;
    TraceBox body = new TraceBox();
    body.Start = outside + "0 0.45 0"; body.End = sample.Position + "0 0.45 0";
    body.Mins = "-0.23 0 -0.23"; body.Maxs = "0.23 1.35 0.23";
    body.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
    float hit = GetGame().GetWorld().TraceMove(body, null);
    string blocker;
    if (body.TraceEnt) blocker = SCR_ResourceNameUtils.GetPrefabName(body.TraceEnt);
    PrintFormat("[EXPG ENTRY TRACE] node=%1 side=%2 floorDelta=%3 bodyFraction=%4 entity=%5 prefab=%6 outside=%7", n, side, sample.Position[1] - ground, hit, body.TraceEnt, blocker, outside);
   }
  }
 }
 override void EOnFrame(IEntity owner, float timeSlice)
 {
  if (Finished || Now() < Next) return; Next = Now() + 0.5;
  if (Now() - Started > 420) { Check(false, "420 second gameplay deadline; see last phase/status"); ReportActors(); Finish("timeout"); return; }
  if (Phase >= 3 && Phase < 11 && !SameActors()) { Check(false, "expected living actors must remain in the original single group"); Finish("roster changed"); return; }
  if (Phase >= 3 && (Phase < 8 || Phase >= 11) && (!Group.EXPG_Active || !Manager.Find(Group))) { Check(false, "garrison released before Force Move"); Finish("premature release"); return; }
  if (Phase == 0)
  {
   array<int> players = {}; GetGame().GetPlayerManager().GetPlayers(players);
   Manager = EXPG_GarrisonManager.Get();
   Observers = ObserversSystem.Cast(GetGame().GetWorld().FindSystem(ObserversSystem));
   if (!Check(Manager && Observers && players.IsEmpty() && EBG_CacheZone.Zones.IsEmpty(), "isolated server with no connected players or Optimizer zones")) { Finish("setup"); return; }
   Observers.InsertObserverSP(ObserverKey, Point[0], Point[2], null);
   ResourceName house = "{EDBC0E94793BA9F1}Prefabs/Structures/Houses/Village/House_Village_E_1I01/House_Village_E_1I01.et";
   PrintFormat("[EXPG HOUSE CASE] freshTrim=%1 prefab=%2", EXPG_TEST_FRESH_TRIM, house);
   Structure = GetGame().SpawnEntityPrefab(Resource.Load(house), GetGame().GetWorld(), Params(Point));
   if (!Check(SCR_DestructibleBuildingEntity.Cast(Structure) != null, "native enterable house spawned")) { Finish("building"); return; }
   Manager.Prepare(Structure); Advance(1); return;
  }
  if (Phase == 1)
  {
   EXPG_BuildingPlan plan = Manager.FindPlan(Structure);
   if (!plan || !plan.Done) return;
   ReportPlan(plan);
   string reason;
   if (EXPG_TEST_FRESH_TRIM) ExpectedCount = EXPG_GarrisonManager.PlacementCount(RequestedCount, plan.Slots.Count());
   bool fits = Manager.CanFit(Structure, RequestedCount, reason) && plan.Slots.Count() >= ExpectedCount && ExpectedCount >= 2;
   if (EXPG_TEST_FRESH_TRIM && (plan.Slots.Count() != 9 || ExpectedCount != 9)) fits = false;
   PrintFormat("[EXPG CAPACITY CASE] freshTrim=%1 requested=%2 expected=%3 capacity=%4", EXPG_TEST_FRESH_TRIM, RequestedCount, ExpectedCount, plan.Slots.Count());
   if (!Check(fits, "production building plan fits case capacity: " + reason)) { Finish("unsafe building plan"); return; }
   ResourceName squad = "{84E5BBAB25EA23E5}Prefabs/Groups/BLUFOR/Group_US_FireTeam.et";
   if (EXPG_TEST_FRESH_TRIM) squad = "{383447BBD154428E}Prefabs/Tests/EXPG_TrimTwelve.et";
   Group = SCR_AIGroup.Cast(GetGame().SpawnEntityPrefab(Resource.Load(squad), GetGame().GetWorld(), Params(Point + "12 0 0", 0.3)));
   if (Group && EXPG_TEST_FRESH_TRIM && !Check(Group.EXPG_BeginFreshRoster(RequestedCount), "fresh roster provenance starts in the spawn callback")) { Finish("fresh roster admission"); return; }
   if (Group) Group.EXPG_CacheMode = 1; // This first cycle explicitly covers Simulation even when Full is default.
   bool adopted;
   if (Group && EXPG_TEST_FRESH_TRIM) adopted = Manager.AdoptFresh(Group, Structure, 0, RequestedCount);
   else if (Group) adopted = Manager.Adopt(Group, Structure, 0);
   if (!Check(adopted, "native prefab squad adopted once")) { Finish("adopt"); return; }
   Check(!Manager.Adopt(Group, Structure, 0), "duplicate adoption refused");
   Advance(2); return;
  }
  if (Phase == 2)
  {
   Record = Manager.Find(Group);
   if (!Record || !Group.EXPG_Active) { Check(false, "garrison released during initial member completion"); Finish("initialization release"); return; }
   if (!Record || !Record.Ready) return;
   if (!Check(Record.Members.Count() == ExpectedCount && Group.GetAgentsCount() == ExpectedCount, "expected safe roster in original group without splitting")) { Finish("initial roster"); return; }
   if (EXPG_TEST_FRESH_TRIM && !Check(Group.EXPG_TestTrimResult(), "twelve native admissions reduced to nine original actors, original leader retained, three native deletions acknowledged")) { Finish("fresh trim evidence"); return; }
   foreach (EXPG_GarrisonMember member : Record.Members)
   {
    EXPG_GameplayActor saved = new EXPG_GameplayActor(); saved.Actor = member.CacheMember.Entity;
    saved.Member = member.CacheMember;
    if (!Check(saved.Actor != null, "original actor exists")) { Finish("missing actor"); return; }
    saved.Id = saved.Actor.GetID(); saved.Fixed = member.Fixed; saved.Post = Record.Plan.Nodes[member.NodeIndex].Position;
    saved.Presentation = saved.Actor.GetFlags() & (EntityFlags.VISIBLE | EntityFlags.TRACEABLE);
    if (!Check((saved.Presentation & EntityFlags.VISIBLE) != 0 && !saved.Actor.EBG_IsSimulationCached(), "original actor initially visible and uncached")) { Finish("initial presentation"); return; }
    if (saved.Fixed) FixedCount++;
    Originals.Insert(saved);
   }
   if (!Check(FixedCount > 0, "real planner assigned at least one fixed guard post")) { Finish("no fixed post"); return; }
   Advance(3); return;
  }
  if (Phase == 3)
  {
   if (Now() - PhaseStarted < 5) return;
   if (!Check(SameActors() && PostsHeld() && Presentation(false), "awake original roster and fixed posts within 0.5 metres")) { ReportActors(); Finish("awake posts"); return; }
   ReportActors(); Advance(4); return;
  }
  if (Phase == 4)
  {
   if (!PostsHeld()) { Check(false, "fixed posts must remain held throughout the awake sleep-delay interval"); ReportActors(); Finish("awake post drift"); return; }
   if (!Record.Simulation || !Record.Simulation.Suspended) return;
   if (!Check(SameActors() && Presentation(true) && PostsHeld(), "production scheduler suspended original actors and hid native presentation")) { Finish("sleep observation"); return; }
   foreach (EXPG_GameplayActor saved : Originals) saved.SleepingPosition = saved.Actor.GetOrigin();
   ReportActors(); Advance(5); return;
  }
  if (Phase == 5)
  {
   if (Now() - PhaseStarted < 5) return;
   bool stationary = Record.Simulation && Record.Simulation.Suspended;
   foreach (EXPG_GameplayActor saved : Originals)
    if (vector.DistanceSq(saved.Actor.GetOrigin(), saved.SleepingPosition) > 0.01) stationary = false;
   if (!Check(stationary && Presentation(true), "cached originals remain stationary for five seconds")) { Finish("sleep hold"); return; }
   Group.EXPG_SetSetting(0, 0); Advance(6); return;
  }
  if (Phase == 6)
  {
   if (Record.Simulation) return;
   if (!Check(SameActors() && Presentation(false) && PostsHeld(), "cache Off restored same actors, presentation and fixed posts")) { ReportActors(); Finish("wake observation"); return; }
   Advance(7); return;
  }
  if (Phase == 7)
  {
   if (!PostsHeld()) { Check(false, "fixed posts drifted after restoration"); ReportActors(); Finish("post drift"); return; }
   if (Now() - PhaseStarted < 10) return;
   Check(SameActors() && Presentation(false), "restored roster and fixed posts held ten seconds"); ReportActors();
   Group.EXPG_SetSetting(0, 2); Advance(11); return;
  }
  if (Phase == 11)
  {
   if (!Record.Full) return;
   if (Record.Full.GetState() != EBG_FullGroupPhase.CACHED) return;
   FullTransaction = Record.Full;
   if (!Check(Group.GetAgentsCount() == 0 && FullTransaction.GetMemberCount() == LivingCount(), "Full removes only current survivors and retains the original group")) { Finish("full sleep roster"); return; }
   foreach (int i, EXPG_GameplayActor saved : Originals)
   {
    if (saved.Dead) continue;
    if (!Check(saved.Member == Record.Members[i].CacheMember && !saved.Member.Entity && FullTransaction.EXPG_TestPose(saved.Member, saved.FullPose), "stable logical survivor owns captured WORLD pose while actor is absent")) { Finish("full snapshot"); return; }
    saved.NodeAtSleep = Record.Members[i].NodeIndex;
   }
   Advance(12); return;
  }
  if (Phase == 12)
  {
   if (Now() - PhaseStarted < 5) return;
   if (!Check(Record.Full == FullTransaction && Group.GetAgentsCount() == 0, "Full sleeping roster stays absent for five seconds")) { Finish("full sleep hold"); return; }
   Group.EXPG_SetSetting(0, 0); Advance(13); return;
  }
  if (Phase == 13)
  {
   if (Record.Full) return;
   if (!Check(FullTransaction.EXPG_TestParity && FullTransaction.EXPG_TestHolds && FullTransaction.EXPG_TestBindings && FullTransaction.EXPG_TestSeen.Count() == LivingCount(), "every recreated survivor held its captured WORLD transform at each Poll and had its assignment before release")) { Finish("full spawn parity"); return; }
   foreach (int i, EXPG_GameplayActor saved : Originals)
   {
    if (saved.Dead) continue;
    EXPG_GarrisonMember member = Record.Members[i];
    SCR_ChimeraCharacter restored = member.CacheMember.Entity;
    bool assigned = member.CacheMember == saved.Member && member.Fixed == saved.Fixed && member.NodeIndex == saved.NodeAtSleep;
    if (!Check(assigned && restored && restored.GetID() != saved.Id && restored.GetCharacterGroup() == Group, "new native actor retains original logical member, role, post and group")) { Finish("full assignment"); return; }
    if (saved.Fixed && !Check(vector.DistanceSq(restored.GetOrigin(), saved.FullPose[3]) <= 0.0025, "fixed guard remains within five centimetres of captured position after wake")) { Finish("full post position"); return; }
    // Keep the original awake presentation as the baseline; recreated actors
    // finish presentation after the release frame (observed with nine members).
    saved.Actor = restored; saved.Id = restored.GetID();
   }
   if (!Check(SameActors() && PostsHeld(), "restored survivors stay in one original group at assigned posts")) { Finish("full roster"); return; }
   FullCycles++;
   PrintFormat("[EXPG FULL CYCLE] cycle=%1 living=%2 transformParity=%3 assignments=%4", FullCycles, LivingCount(), FullTransaction.EXPG_TestParity, FullTransaction.EXPG_TestBindings);
   // Both cycles wait for deferred native activation; commands issued in the
   // release frame reach still-inactive agents (observed 20261005-005943).
   AwakeSince = -1;
   Advance(18); return;
  }
  if (Phase == 18)
  {
   // Normal awake-casualty case: native activation is deferred after release.
   // The immediate-release-frame Kill failure remains separate evidence; never
   // force activation or weaken the following controller-death assertion.
   bool ready = SameActors() && PostsHeld() && NativeAwake();
   if (!ready) AwakeSince = -1;
   else if (AwakeSince < 0) AwakeSince = Now();
   if (Now() - PhaseStarted > 10)
   {
    NativeAwake(true); ReportActors();
    Check(false, "native wake activation and retained posts must settle within ten seconds");
    Finish("wake activation"); return;
   }
   if (AwakeSince < 0 || Now() - AwakeSince < 0.5) return;
   NativeAwake(true);
   if (FullCycles > 1)
   {
    Check(true, "all recreated agents active at LOD0 with retained posts for half a second before motion probe");
    if (!Check(StartMotionProbe(), "native stance and look probe starts on a held survivor")) Finish("motion probe setup");
    return;
   }
   Check(true, "all recreated agents active at LOD0 with retained posts for half a second before damage");
   EXPG_GameplayActor casualty = Originals[Originals.Count() - 1];
   // TRUE damage through HandleDamage with a real native instigator.
   casualty.Actor.GetDamageManager().Kill(Instigator.CreateInstigator(casualty.Actor));
   ReportCasualty();
   Advance(14); return;
  }
  if (Phase == 14)
  {
   EXPG_GameplayActor casualty = Originals[Originals.Count() - 1];
   if (!casualty.Actor || !casualty.Actor.GetCharacterController().IsDead() || !casualty.Member.Dead || Group.GetAgentsCount() != ExpectedCount - 1)
   {
    if (Now() - PhaseStarted > 10) { ReportCasualty(); Check(false, "native casualty must settle without releasing the garrison"); Finish("casualty observation"); }
    return;
   }
   casualty.Dead = true;
   if (!Check(SameActors() && LivingCount() == ExpectedCount - 1, "exactly one casualty remains excluded from future capture")) { Finish("casualty roster"); return; }
   Group.EXPG_SetSetting(0, 2); Advance(11); return;
  }
  if (Phase >= 15 && Phase <= 17)
  {
   if (!SameActors() || !PostsHeld() || !MotionUtility.EXPG_GetPostControl()) { Check(false, "native stance/look probe preserves squad and fixed posts"); Finish("motion probe ownership"); return; }
   if (Phase == 15 || Phase == 16)
   {
    ECharacterStance expectedStance = ECharacterStance.CROUCH;
    if (Phase == 16) expectedStance = ECharacterStance.STAND;
    if (MotionController.GetStance() != expectedStance)
    {
     if (Now() - PhaseStarted > 6) { Check(false, "native requested stance observed within six seconds"); Finish("stance timeout"); }
     return;
    }
    Check(true, "native requested stance observed while fixed post remains active");
    PrintFormat("[EXPG STANCE] actual=%1 postActive=1", MotionController.GetStance());
    if (Phase == 15) { SCR_AIStanceHandling.SetStance(MotionController, ECharacterStance.STAND); Advance(16); return; }
    vector pose[4]; MotionActor.GetWorldTransform(pose);
    InitialForward = pose[2]; InitialForward[1] = 0; InitialForward.Normalize();
    LookDirection = Vector(-0.5 * InitialForward[0] + 0.8660254 * InitialForward[2], 0, -0.8660254 * InitialForward[0] - 0.5 * InitialForward[2]);
    vector lookTarget = MotionActor.GetOrigin() + Vector(0, 1.5, 0) + LookDirection * 20.0;
    MotionUtility.m_LookAction.LookAt(lookTarget, SCR_AILookAction.PRIO_COMMANDER + 1, 4.0);
    Advance(17); return;
   }
   vector currentPose[4]; MotionActor.GetWorldTransform(currentPose);
   vector forward = currentPose[2]; forward[1] = 0; forward.Normalize();
   vector head = MotionController.GetHeadAimingComponent().GetAimingDirectionWorld(); head[1] = 0; head.Normalize();
   float bodyDot = vector.Dot(InitialForward, forward);
   float headDot = vector.Dot(LookDirection, head);
   if (bodyDot < 0.94) ObservedBodyTurn = true;
   if (headDot > 0.85) ObservedHeadAim = true;
   if (Now() - PhaseStarted < 4) return;
   PrintFormat("[EXPG LOOK] bodyTurn=%1 headAim=%2 bodyDot=%3 headDot=%4 postActive=1", ObservedBodyTurn, ObservedHeadAim, bodyDot, headDot);
   // Let the bounded native look expire on its own; its timer begins when the
   // behavior tree consumes it. Do not cancel a potentially newer look owner.
   SCR_AIStanceHandling.SetStance(MotionController, OriginalStance);
   if (!Check(ObservedBodyTurn && ObservedHeadAim, "held survivor turns body and aims head through native look action")) { Finish("native look response"); return; }
   ForceMove = AIWaypoint.Cast(GetGame().SpawnEntityPrefab(Resource.Load("{06E1B6EBD480C6E0}Prefabs/AI/Waypoints/AIWaypoint_ForcedMove.et"), GetGame().GetWorld(), Params(Point + "50 0 0")));
   if (!Check(ForceMove != null, "native Force Move waypoint spawned")) { Finish("force move setup"); return; }
   Group.AddWaypoint(ForceMove); Advance(8); return;
  }
  if (Phase == 8)
  {
   if (Manager.Find(Group) || Group.EXPG_Active) return;
   bool controlsReleased = Record.Finished;
   foreach (EXPG_GarrisonMember member : Record.Members) if (member.Post || member.Patrol) controlsReleased = false;
   bool releasedSame = SameActors();
   bool releasedVisible = Presentation(false);
   if (!controlsReleased || !releasedSame || !releasedVisible)
   {
    PrintFormat("[EXPG FORCE MOVE STATE] finished=%1 controlsReleased=%2 sameActors=%3 presentation=%4 agents=%5 living=%6", Record.Finished, controlsReleased, releasedSame, releasedVisible, Group.GetAgentsCount(), LivingCount());
    foreach (int releasedIndex, EXPG_GarrisonMember releasedMember : Record.Members)
    {
     EXPG_GameplayActor releasedActor = Originals[releasedIndex];
     int releasedFlags;
     bool releasedCached;
     if (releasedActor.Actor) { releasedFlags = releasedActor.Actor.GetFlags() & (EntityFlags.VISIBLE | EntityFlags.TRACEABLE); releasedCached = releasedActor.Actor.EBG_IsSimulationCached(); }
     PrintFormat("[EXPG FORCE MOVE MEMBER] member=%1 post=%2 patrol=%3 dead=%4 flags=%5 expected=%6 cached=%7", releasedIndex + 1, releasedMember.Post != null, releasedMember.Patrol != null, releasedActor.Dead, releasedFlags, releasedActor.Presentation, releasedCached);
    }
   }
   if (!Check(controlsReleased && releasedSame && releasedVisible, "native Force Move released owned controls and preserved original squad")) { Finish("force move release"); return; }
   Advance(9); return;
  }
  if (Phase == 9)
  {
   if (Now() - PhaseStarted < 10) return;
   Check(!Manager.Find(Group) && !Group.EXPG_Active && SameActors(), "garrison remains released ten seconds after Force Move");
   ReportActors(); Advance(10); Finish("completed");
  }
 }
}
