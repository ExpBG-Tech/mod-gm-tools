// TEST ONLY. Unit Caching Full cache of a squad whose soldiers run EXPBG Unit Scripts Hold
// and Freeze (no animation): the squad Full caches and every respawned soldier gets his
// script back (EUS_FullCacheCarry.c).
// pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -FixturePath tests/EBG_FullScriptCarryGameplay.c -ExpectResult '\[EBG FULL CARRY RESULT\] checks=[1-9]\d* failures=0 restored=4 reason=complete' -TimeoutSeconds 420 -OrchestratorSlotGranted
// The runner copies this file to EXPG_GarrisonGameplay.c; the class names are fixed.
//  1. USSR rifle squad, two followers on Hold and two on Freeze; a Full zone over it.
//  2. No Simulation fallback (no animation): the squad Full caches (originals deleted).
//  3. Injected presence wakes it: four respawned soldiers bound again with Hold (2) and
//     Freeze (2), each anchor within 1.5 m of a saved spot.
class EXPG_GarrisonGameplayClass : GenericEntityClass {}
class EXPG_GarrisonGameplay : GenericEntity
{
 static const float FIXTURE_SECONDS = 360;
 static const string SQUAD_PREFAB = "{E552DABF3636C2AD}Prefabs/Groups/OPFOR/Group_USSR_RifleSquad.et";
 static const string ZONE_PREFAB = "{7E1080ED8F0633FD}PrefabsEditable/EXPBG/EBG_CacheZone.et";
 static ref array<vector> s_Presence;
 SCR_AIGroup Squad;
 EBG_CacheZone TestZone;
 EBG_CacheGroup Record;
 EUS_Manager Manager;
 ref EUS_Report Report = new EUS_Report();
 ref array<SCR_ChimeraCharacter> Actors = {};
 ref array<vector> Spots = {};
 int Phase;
 int Checks;
 int Failures;
 int Restored;
 float Started;
 float Next;
 float PhaseAt;
 bool Finished;
 vector Origin = "4773.46 0 7094.57";

 void EXPG_GarrisonGameplay(IEntitySource src, IEntity parent) { SetEventMask(EntityEvent.INIT | EntityEvent.FRAME); }
 float Now() { return GetGame().GetWorld().GetWorldTime() * 0.001; }
 override void EOnInit(IEntity owner)
 {
  if (!Replication.IsServer()) { ClearEventMask(EntityEvent.FRAME); return; }
  s_Presence = {};
  Started = Now();
  Next = Started + 10;
  Print("[EBG FULL CARRY BEGIN]");
 }
 bool Check(bool ok, string label)
 {
  Checks++;
  if (!ok) Failures++;
  PrintFormat("[EBG FULL CARRY CHECK] pass=%1 %2", ok, label);
  return ok;
 }
 void Finish(string reason)
 {
  if (Finished) return;
  Finished = true;
  if (s_Presence) s_Presence.Clear();
  ClearEventMask(EntityEvent.FRAME);
  PrintFormat("[EBG FULL CARRY RESULT] checks=%1 failures=%2 restored=%3 reason=%4", Checks, Failures, Restored, reason);
  GetGame().RequestClose();
 }
 vector Ground(vector p, float lift)
 {
  p[1] = GetGame().GetWorld().GetSurfaceY(p[0], p[2]) + lift;
  return p;
 }
 IEntity Spawn(string prefab, vector point)
 {
  EntitySpawnParams params = new EntitySpawnParams();
  params.TransformMode = ETransformMode.WORLD;
  Math3D.MatrixIdentity4(params.Transform);
  params.Transform[3] = Ground(point, 0.3);
  Resource resource = Resource.Load(prefab);
  IEntity spawned = GetGame().SpawnEntityPrefab(resource, GetGame().GetWorld(), params);
  return spawned;
 }
 // Full mode (1), 60 m zone, wake 100 m, sleep 300 m, 5 s sleep delay, debug messages on.
 EBG_CacheZone SpawnZone(vector point)
 {
  EBG_CacheZone zone = EBG_CacheZone.Cast(Spawn(ZONE_PREFAB, point));
  if (!zone) return null;
  zone.SetValue(1, 1); zone.SetValue(2, 0); zone.SetValue(3, 60);
  zone.SetValue(4, 100); zone.SetValue(5, 300); zone.SetValue(12, 5);
  zone.SetValue(15, 0); zone.SetValue(18, 0); zone.SetValue(21, 1);
  zone.SetValue(0, 1);
  return zone;
 }
 void Advance(int phase)
 {
  Phase = phase;
  PhaseAt = Now();
 }
 bool Waited(float seconds, string label)
 {
  if (Now() - PhaseAt <= seconds) return false;
  Check(false, label);
  Finish("phase " + Phase.ToString());
  return true;
 }
 override void EOnFrame(IEntity owner, float timeSlice)
 {
  if (Finished || Now() < Next) return;
  Next = Now() + 0.25;
  if (Now() - Started > FIXTURE_SECONDS) { Check(false, "all phases finished before the fixture deadline (phase " + Phase.ToString() + ")"); Finish("timeout"); return; }
  Step();
 }
 void Step()
 {
  if (Phase == 0)
  {
   Squad = SCR_AIGroup.Cast(Spawn(SQUAD_PREFAB, Origin));
   if (!Check(Squad != null, "USSR rifle squad spawned")) { Finish("setup"); return; }
   Advance(10); return;
  }
  if (Phase == 10)
  {
   if (!Squad || !Squad.EBG_HasCompletedInitialSpawn() || Squad.GetAgentsCount() != 6) { Waited(60, "squad finished its initial spawn with six members"); return; }
   if (Now() - PhaseAt < 4) return;
   Manager = EUS_Manager.Get();
   if (!Check(Manager != null, "Unit Scripts manager available on the server")) { Finish("setup"); return; }
   array<int> scripts = {EUS_Codes.HOLD, EUS_Codes.FREEZE, EUS_Codes.HOLD, EUS_Codes.FREEZE};
   IEntity leader = Squad.GetLeaderEntity();
   array<AIAgent> agents = {};
   Squad.GetAgents(agents);
   foreach (AIAgent agent : agents)
   {
    SCR_ChimeraCharacter member = SCR_ChimeraCharacter.Cast(agent.GetControlledEntity());
    if (!member || member == leader || Actors.Count() >= scripts.Count()) continue;
    int code = scripts[Actors.Count()];
    bool applied = Manager.ApplyUnit(member, code, Report);
    Check(applied && member.EUS_Script == code, "unit script bound: " + EUS_Codes.Describe(code));
    Actors.Insert(member);
   }
   Advance(11); return;
  }
  if (Phase == 11)
  {
   if (Now() - PhaseAt < 4) return;
   foreach (SCR_ChimeraCharacter actor : Actors)
   {
    EUS_UnitControl control = Manager.FindControl(actor);
    if (!Check(control != null && control.IsBound(), "script bound before caching")) { Finish("setup"); return; }
    Spots.Insert(control.GetAnchor());
   }
   TestZone = SpawnZone(Origin);
   if (!Check(TestZone != null, "Full cache zone spawned over the squad")) { Finish("setup"); return; }
   Advance(20); return;
  }
  if (!TestZone) { Check(false, "cache zone kept across the test"); Finish("zone"); return; }
  if (Phase == 20)
  {
   Record = EBG_CacheManager.Get().FindGroup(Squad);
   if (!Record) { Waited(60, "Full zone enrolled the Hold/Freeze squad; note='" + TestZone.EnrollmentNote + "'"); return; }
   Check(!EBG_ScriptedUnits.UsesSimulation(Record), "a Hold/Freeze-only squad does not fall back to Simulation");
   Advance(21); return;
  }
  if (Phase == 21)
  {
   if (Record.Simulation) { Check(false, "Hold/Freeze squad was Simulation cached; reason='" + Record.Reason + "'"); Finish("cache"); return; }
   if (!Record.Full || Record.Full.GetState() != EBG_FullGroupPhase.CACHED) { Waited(120, "Hold/Freeze squad Full cached; reason='" + Record.Reason + "' keepAwake='" + Record.KeepAwake + "' rejection='" + Record.LastCacheRejection + "'"); return; }
   int alive;
   foreach (SCR_ChimeraCharacter original : Actors) { if (original && !original.IsDeleted()) alive++; }
   Check(alive == 0, "the scripted originals were deleted by the Full cache");
   if (Now() - PhaseAt < 5) return;
   s_Presence.Insert(Ground(Origin, 1.8));
   Advance(30); return;
  }
  if (Phase == 30)
  {
   if (Record.Full || Record.Simulation || !Record.Group) { Waited(90, "Full squad woke; reason='" + Record.Reason + "' recovery='" + Record.Recovery + "'"); return; }
   if (Now() - PhaseAt < 8) return;
   array<AIAgent> woken = {};
   Record.Group.GetAgents(woken);
   int holds, freezes;
   foreach (AIAgent agent : woken)
   {
    SCR_ChimeraCharacter member = SCR_ChimeraCharacter.Cast(agent.GetControlledEntity());
    if (!member) continue;
    EUS_UnitControl control = Manager.FindControl(member);
    if (!control || !control.IsBound()) continue;
    bool near;
    foreach (vector spot : Spots) { if (vector.DistanceXZ(control.GetAnchor(), spot) <= 1.5) near = true; }
    PrintFormat("[EBG FULL CARRY BOUND] script='%1' anchor=%2 nearSaved=%3", EUS_Codes.Describe(control.GetCode()), control.GetAnchor(), near);
    if (!near) continue;
    if (control.GetCode() == EUS_Codes.HOLD) holds++;
    else if (control.GetCode() == EUS_Codes.FREEZE) freezes++;
   }
   Restored = holds + freezes;
   Check(holds == 2, string.Format("two Hold scripts back after the Full wake (%1)", holds));
   Check(freezes == 2, string.Format("two Freeze scripts back after the Full wake (%1)", freezes));
   Finish("complete"); return;
  }
 }
}
// Presence seam, as in the scripted Simulation fixture.
modded class EBG_CacheManager
{
 override protected void UpdatePlayers()
 {
  super.UpdatePlayers();
  if (EXPG_GarrisonGameplay.s_Presence)
  {
   foreach (vector presence : EXPG_GarrisonGameplay.s_Presence) Players.Insert(presence);
  }
 }
}
