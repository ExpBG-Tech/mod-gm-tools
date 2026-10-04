// TEST ONLY. Real world/prefabs and production scheduler; no fabricated plan,
// forced cache state, replaced actors, synthetic player, or shortened timers.
class EXPG_GameplayActor
{
 SCR_ChimeraCharacter Actor;
 EntityID Id;
 vector Post;
 vector SleepingPosition;
 bool Fixed;
 int Presentation;
}

class EXPG_GarrisonGameplayClass : GenericEntityClass {}
class EXPG_GarrisonGameplay : GenericEntity
{
 IEntity Structure;
 SCR_AIGroup Group;
 AIWaypoint ForceMove;
 EXPG_GarrisonManager Manager;
 ref EXPG_GarrisonRecord Record;
 ref array<ref EXPG_GameplayActor> Originals = {};
 ObserversSystem Observers;
 int ObserverKey, Phase, Checks, Failures, FixedCount;
 float Started, Next, PhaseStarted;
 bool Finished;
 vector Point = "4773.46 0 7094.57";

 void EXPG_GarrisonGameplay(IEntitySource src, IEntity parent) { SetEventMask(EntityEvent.INIT | EntityEvent.FRAME); }
 float Now() { return GetGame().GetWorld().GetWorldTime() * 0.001; }
 override void EOnInit(IEntity owner)
 {
  if (!Replication.IsServer()) { ClearEventMask(EntityEvent.FRAME); return; }
  Started = Now(); Next = Started + 15;
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
  if (!Group || Originals.Count() != 4 || Group.GetAgentsCount() != Originals.Count()) return false;
  array<AIAgent> agents = {}; Group.GetAgents(agents);
  foreach (EXPG_GameplayActor saved : Originals)
  {
   if (!saved.Actor || saved.Actor.GetID() != saved.Id || saved.Actor.GetCharacterGroup() != Group) return false;
   if (!saved.Actor.GetCharacterController() || saved.Actor.GetCharacterController().IsDead()) return false;
   bool found;
   foreach (AIAgent agent : agents) { if (agent && agent.GetControlledEntity() == saved.Actor) found = true; }
   if (!found) return false;
  }
  return true;
 }
 bool PostsHeld()
 {
  foreach (EXPG_GameplayActor saved : Originals)
   if (saved.Fixed && (!saved.Actor || vector.DistanceSq(saved.Actor.GetOrigin(), saved.Post) > 0.25)) return false;
  return FixedCount > 0;
 }
 bool Presentation(bool sleeping)
 {
  foreach (EXPG_GameplayActor saved : Originals)
  {
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
   if (saved.Actor) PrintFormat("[EXPG GAMEPLAY ACTOR] phase=%1 original=%2 current=%3 group=%4 position=%5 post=%6 fixed=%7 cached=%8", Phase, saved.Id, saved.Actor.GetID(), saved.Actor.GetCharacterGroup(), saved.Actor.GetOrigin(), saved.Post, saved.Fixed, saved.Actor.EBG_IsSimulationCached());
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
  for (int n = 0; n < plan.Nodes.Count(); n++)
  {
   EXPG_BuildingNode sample = plan.Nodes[n];
   if (n < 128 || sample.Entrance || sample.Reachable || plan.Slots.Contains(n))
    PrintFormat("[EXPG PLAN NODE] index=%1 position=%2 entrance=%3 reachable=%4 links=%5 score=%6 selected=%7", n, sample.Position, sample.Entrance, sample.Reachable, sample.Links.Count(), sample.Score, plan.Slots.Contains(n));
   // Diagnose ground-level entry candidates without changing production state.
   if (detailed >= 32 || Math.AbsFloat(sample.Position[1] - plan.Origin[1]) > 1) continue;
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
  if (Now() - Started > 240) { Check(false, "240 second gameplay deadline; see last phase/status"); ReportActors(); Finish("timeout"); return; }
  if (Phase >= 3 && !SameActors()) { Check(false, "original four actors must remain alive in the original single group"); Finish("roster changed"); return; }
  if (Phase >= 3 && Phase < 8 && (!Group.EXPG_Active || !Manager.Find(Group))) { Check(false, "garrison released before Force Move"); Finish("premature release"); return; }
  if (Phase == 0)
  {
   array<int> players = {}; GetGame().GetPlayerManager().GetPlayers(players);
   Manager = EXPG_GarrisonManager.Get();
   Observers = ObserversSystem.Cast(GetGame().GetWorld().FindSystem(ObserversSystem));
   if (!Check(Manager && Observers && players.IsEmpty() && EBG_CacheZone.Zones.IsEmpty(), "isolated server with no connected players or Optimizer zones")) { Finish("setup"); return; }
   Observers.InsertObserverSP(ObserverKey, Point[0], Point[2], null);
   Structure = GetGame().SpawnEntityPrefab(Resource.Load("{EDBC0E94793BA9F1}Prefabs/Structures/Houses/Village/House_Village_E_1I01/House_Village_E_1I01.et"), GetGame().GetWorld(), Params(Point));
   if (!Check(SCR_DestructibleBuildingEntity.Cast(Structure) != null, "native enterable village house spawned")) { Finish("building"); return; }
   Manager.Prepare(Structure); Advance(1); return;
  }
  if (Phase == 1)
  {
   EXPG_BuildingPlan plan = Manager.FindPlan(Structure);
   if (!plan || !plan.Done) return;
   ReportPlan(plan);
   string reason;
   bool fits = Manager.CanFit(Structure, 4, reason);
   if (!Check(fits, "production building plan fits four: " + reason)) { Finish("unsafe building plan"); return; }
   Group = SCR_AIGroup.Cast(GetGame().SpawnEntityPrefab(Resource.Load("{84E5BBAB25EA23E5}Prefabs/Groups/BLUFOR/Group_US_FireTeam.et"), GetGame().GetWorld(), Params(Point + "12 0 0", 0.3)));
   if (!Check(Group && Manager.Adopt(Group, Structure, 0), "native four-member fireteam adopted once")) { Finish("adopt"); return; }
   Check(!Manager.Adopt(Group, Structure, 0), "duplicate adoption refused");
   Advance(2); return;
  }
  if (Phase == 2)
  {
   Record = Manager.Find(Group);
   if (!Record || !Group.EXPG_Active) { Check(false, "garrison released during initial member completion"); Finish("initialization release"); return; }
   if (!Record || !Record.Ready) return;
   if (!Check(Record.Members.Count() == 4 && Group.GetAgentsCount() == 4, "four original spawned members without splitting")) { Finish("initial roster"); return; }
   foreach (EXPG_GarrisonMember member : Record.Members)
   {
    EXPG_GameplayActor saved = new EXPG_GameplayActor(); saved.Actor = member.CacheMember.Entity;
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
   ForceMove = AIWaypoint.Cast(GetGame().SpawnEntityPrefab(Resource.Load("{06E1B6EBD480C6E0}Prefabs/AI/Waypoints/AIWaypoint_ForcedMove.et"), GetGame().GetWorld(), Params(Point + "50 0 0")));
   if (!Check(ForceMove != null, "native Force Move waypoint spawned")) { Finish("force move setup"); return; }
   Group.AddWaypoint(ForceMove); Advance(8); return;
  }
  if (Phase == 8)
  {
   if (Manager.Find(Group) || Group.EXPG_Active) return;
   bool controlsReleased = Record.Finished;
   foreach (EXPG_GarrisonMember member : Record.Members) if (member.Post || member.Patrol) controlsReleased = false;
   if (!Check(controlsReleased && SameActors() && Presentation(false), "native Force Move released owned controls and preserved original squad")) { Finish("force move release"); return; }
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
