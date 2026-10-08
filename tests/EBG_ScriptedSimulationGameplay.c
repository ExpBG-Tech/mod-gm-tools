// TEST ONLY. Unit Caching Simulation cache of a squad whose soldiers run EXPBG Unit Scripts.
// pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -FixturePath tests/EBG_ScriptedSimulationGameplay.c -ExpectResult '\[EBG SCRIPTED SIM RESULT\] checks=[1-9]\d* failures=0 scripted=5 restarted=\d+ reason=complete mismatches=0' -TimeoutSeconds 420 -OrchestratorSlotGranted
// The runner copies this file to EXPG_GarrisonGameplay.c; the class names are fixed.
// Real USSR rifle squad and Unit Caching zone prefab, production EUS_Manager entry points
// (the ones the context actions and attributes call), production scheduler. Five of the
// six soldiers are scripted: Freeze, Hold, "Smoke", "Sit on the ground", "Sit on a chair"
// (the three animations are vanilla loiters). Presence is injected through
// EBG_CacheManager.UpdatePlayers; the server has no players. No GM UI, no save/load.
//  1. Full zone over the scripted squad: never enrolled for 15 s, and the zone's
//     enrollment note names the Full refusal for Unit Scripts soldiers.
//  2. Same zone switched to Simulation: the squad enrolls with no keep-awake hold and is
//     Simulation cached with all six original actors; every scripted soldier's state is
//     captured (code, animation playing).
//  3. 30 s cached (longer than Unit Scripts' four loiter retries), never woken by a hold.
//  4. Injected presence wakes it: same entity IDs, each soldier within 0.25 m of where he
//     was paused, every control still bound with the same script, and the wake outcome
//     recorded per soldier.
//  5. 20 s awake: every animation plays again at the end, Freeze within 0.5 m and Hold
//     and the animations within 1.5 m of their Unit Scripts spot at every sample (no
//     running around), scripts still bound.
class EXPG_GarrisonGameplayClass : GenericEntityClass {}
class EXPG_GarrisonGameplay : GenericEntity
{
 static const float FIXTURE_SECONDS = 300;
 static const float FULL_SECONDS = 15;
 static const float CACHED_SECONDS = 30;
 static const float AWAKE_SECONDS = 20;
 static const string SQUAD_PREFAB = "{E552DABF3636C2AD}Prefabs/Groups/OPFOR/Group_USSR_RifleSquad.et";
 static const string ZONE_PREFAB = "{7E1080ED8F0633FD}PrefabsEditable/EXPBG/EBG_CacheZone.et";
 static ref array<vector> s_Presence;
 SCR_AIGroup Squad;
 EBG_CacheZone TestZone;
 EBG_CacheGroup Record;
 // Kept past the wake (the record drops it) to read each soldier's wake outcome.
 ref EBG_SimulationState Held;
 EUS_Manager Manager;
 ref EUS_Report Report = new EUS_Report();
 ref array<SCR_ChimeraCharacter> Actors = {};
 ref array<int> Codes = {};
 ref array<EntityID> Ids = {};
 ref array<vector> Spots = {};
 ref array<float> Limits = {};
 ref array<float> Drift = {};
 int Phase;
 int Checks;
 int Failures;
 int Restarted;
 float Started;
 float Next;
 float PhaseAt;
 float PresenceAt;
 bool Finished;
 vector Origin = "4773.46 0 7094.57";

 void EXPG_GarrisonGameplay(IEntitySource src, IEntity parent) { SetEventMask(EntityEvent.INIT | EntityEvent.FRAME); }
 float Now() { return GetGame().GetWorld().GetWorldTime() * 0.001; }
 override void EOnInit(IEntity owner)
 {
  if (!Replication.IsServer()) { ClearEventMask(EntityEvent.FRAME); return; }
  EBG_DebugChecks.Enabled = true; EBG_DebugChecks.Mismatches = 0; // indexes also run their old full scans
  s_Presence = {};
  Started = Now();
  Next = Started + 10;
  PrintFormat("[EBG SCRIPTED SIM BEGIN] full=%1 cached=%2 awake=%3 connectedPlayers=0 presence=injected deadline=%4", FULL_SECONDS, CACHED_SECONDS, AWAKE_SECONDS, FIXTURE_SECONDS);
 }
 bool Check(bool ok, string label)
 {
  Checks++;
  if (!ok) Failures++;
  PrintFormat("[EBG SCRIPTED SIM CHECK] pass=%1 %2", ok, label);
  return ok;
 }
 void Finish(string reason)
 {
  if (Finished) return;
  Finished = true;
  if (s_Presence) s_Presence.Clear();
  ClearEventMask(EntityEvent.FRAME);
  Check(EBG_DebugChecks.Mismatches == 0, "index cross-checks matched their old full scans");
  PrintFormat("[EBG SCRIPTED SIM RESULT] checks=%1 failures=%2 scripted=%3 restarted=%4 reason=%5 mismatches=%6", Checks, Failures, Actors.Count(), Restarted, reason, EBG_DebugChecks.Mismatches);
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
  // Keep the Resource and the spawned entity in locals (inline temporaries returned null spawns).
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
 void Presence(vector point)
 {
  s_Presence.Insert(Ground(point, 1.8));
  PresenceAt = Now();
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
 bool Playing(SCR_ChimeraCharacter actor)
 {
  SCR_CharacterControllerComponent controller;
  if (actor) controller = SCR_CharacterControllerComponent.Cast(actor.GetCharacterController());
  if (!controller) return false;
  return controller.IsLoitering();
 }
 // Bound with the same script, and the replicated code agrees.
 bool Scripted(int index)
 {
  SCR_ChimeraCharacter actor = Actors[index];
  if (!actor || actor.EUS_Script != Codes[index]) return false;
  EUS_UnitControl control = Manager.FindControl(actor);
  if (!control) return false;
  return control.IsBound() && control.GetCode() == Codes[index];
 }
 bool AllPlaying()
 {
  for (int i = 0; i < Actors.Count(); i++)
  {
   if (EUS_Codes.IsAnimation(Codes[i]) && !Playing(Actors[i])) return false;
  }
  return true;
 }
 // Largest XZ drift from each soldier's Unit Scripts spot, per sample.
 void Sample()
 {
  for (int i = 0; i < Actors.Count(); i++)
  {
   if (!Actors[i]) continue;
   Drift[i] = Math.Max(Drift[i], vector.DistanceXZ(Actors[i].GetOrigin(), Spots[i]));
  }
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
   PrintFormat("[EBG SCRIPTED SIM RUNTIME] systems='%1' players=%2 zones=%3", GetGame().GetSystemsConfig(), GetGame().GetPlayerManager().GetPlayerCount(), EBG_CacheZone.Zones.Count());
   if (!Check(GetGame().GetPlayerManager().GetPlayerCount() == 0 && EBG_CacheZone.Zones.IsEmpty(), "isolated server: no players, no zones")) { Finish("setup"); return; }
   Squad = SCR_AIGroup.Cast(Spawn(SQUAD_PREFAB, Origin));
   if (!Check(Squad != null, "USSR rifle squad spawned")) { Finish("setup"); return; }
   Advance(10); return;
  }
  if (!Squad) { Check(false, "squad kept across the test"); Finish("squad"); return; }
  // Five scripted soldiers once the squad stands in formation; the leader stays free.
  if (Phase == 10)
  {
   if (!Squad.EBG_HasCompletedInitialSpawn() || Squad.GetAgentsCount() != 6) { Waited(60, "squad finished its initial spawn with six members"); return; }
   if (Now() - PhaseAt < 4) return;
   Manager = EUS_Manager.Get();
   if (!Check(Manager != null, "Unit Scripts manager available on the server")) { Finish("setup"); return; }
   // Freeze, Hold, "Smoke", "Sit on the ground", "Sit on a chair".
   array<int> scripts = {};
   scripts.Insert(EUS_Codes.FREEZE); scripts.Insert(EUS_Codes.HOLD); scripts.Insert(EUS_Codes.ANIMATION + 2);
   scripts.Insert(EUS_Codes.ANIMATION); scripts.Insert(EUS_Codes.ANIMATION + 1);
   IEntity leader = Squad.GetLeaderEntity();
   array<AIAgent> agents = {};
   Squad.GetAgents(agents);
   foreach (AIAgent agent : agents)
   {
    SCR_ChimeraCharacter member = SCR_ChimeraCharacter.Cast(agent.GetControlledEntity());
    if (!member || member == leader || Actors.Count() >= scripts.Count()) continue;
    int code = scripts[Actors.Count()];
    bool applied = Manager.ApplyUnit(member, code, Report);
    Check(applied && member.EUS_Script == code, "unit script bound: " + EUS_Codes.Describe(code) + " (" + Report.LastReason + ")");
    Actors.Insert(member); Codes.Insert(code); Ids.Insert(member.GetID()); Spots.Insert(member.GetOrigin()); Limits.Insert(1.5); Drift.Insert(0);
   }
   if (!Check(Actors.Count() == scripts.Count(), "five followers scripted")) { Finish("setup"); return; }
   Advance(11); return;
  }
  // Every animation plays before the zone exists; the spots are Unit Scripts' own.
  if (Phase == 11)
  {
   if (!AllPlaying()) { Waited(60, "every Unit Scripts animation plays its vanilla loiter"); return; }
   if (Now() - PhaseAt < 3) return;
   for (int spotIndex = 0; spotIndex < Actors.Count(); spotIndex++)
   {
    EUS_UnitControl control = Manager.FindControl(Actors[spotIndex]);
    if (!Check(control != null && control.IsBound(), "script still bound before caching: " + EUS_Codes.Describe(Codes[spotIndex]))) { Finish("setup"); return; }
    Spots[spotIndex] = control.GetAnchor();
    if (Codes[spotIndex] == EUS_Codes.FREEZE) Limits[spotIndex] = 0.5;
   }
   TestZone = SpawnZone(Origin);
   if (!Check(TestZone != null, "Full cache zone spawned over the scripted squad")) { Finish("setup"); return; }
   Advance(20); return;
  }
  if (!TestZone) { Check(false, "cache zone kept across the test"); Finish("zone"); return; }
  // Full refuses the scripted squad and says why.
  if (Phase == 20)
  {
   if (EBG_CacheManager.Get().FindGroup(Squad)) { Check(false, "a Full zone never enrolls a squad with Unit Scripts soldiers"); Finish("full"); return; }
   if (Now() - PhaseAt < FULL_SECONDS) return;
   PrintFormat("[EBG SCRIPTED SIM FULL] note='%1' status='%2'", TestZone.EnrollmentNote, TestZone.Status);
   Check(TestZone.EnrollmentNote.Contains(EBG_ScriptedUnits.FULL_NOTE), "the Full zone's enrollment note names the Full refusal for Unit Scripts soldiers");
   Check(AllPlaying(), "animations untouched by the refused Full zone");
   TestZone.SetValue(1, 0);
   Check(TestZone.Mode == 0, "zone switched to Simulation");
   Advance(30); return;
  }
  if (Phase == 30)
  {
   Record = EBG_CacheManager.Get().FindGroup(Squad);
   if (!Record || Record.Members.Count() != 6) { Waited(60, "Simulation zone enrolled the scripted squad; note='" + TestZone.EnrollmentNote + "'"); return; }
   Check(!Record.Full, "no Full transaction on the scripted squad");
   Advance(31); return;
  }
  if (!Record) { Check(false, "logical cache record retained across the test"); Finish("record"); return; }
  if (Phase == 31)
  {
   if (Record.Full) { Check(false, "scripted squad never Full cached"); Finish("cache"); return; }
   if (!Record.Simulation || !Record.Simulation.Suspended) { Waited(90, "scripted squad Simulation cached; reason='" + Record.Reason + "' keepAwake='" + Record.KeepAwake + "' rejection='" + Record.LastCacheRejection + "'"); return; }
   Held = Record.Simulation;
   PrintFormat("[EBG SCRIPTED SIM CACHED] latency=%1 keepAwake='%2' members=%3", Now() - PhaseAt, Record.KeepAwake, Held.Members.Count());
   Check(Record.KeepAwake.IsEmpty(), "no keep-awake hold for Unit Scripts soldiers in a Simulation zone");
   int captured;
   foreach (EBG_SimulationAgent cached : Held.Members)
   {
    int cachedIndex = Actors.Find(cached.Character);
    if (cachedIndex < 0) continue;
    if (cached.Script && cached.Script.Code == Codes[cachedIndex] && (!EUS_Codes.IsAnimation(Codes[cachedIndex]) || cached.Script.Loitering)) captured++;
   }
   Check(captured == Actors.Count(), string.Format("every scripted soldier's script captured with its pose playing (%1 of %2)", captured, Actors.Count()));
   Advance(32); return;
  }
  // Longer than Unit Scripts' loiter retries; the hold must never wake it.
  if (Phase == 32)
  {
   if (Record.Simulation != Held || !Held.Suspended) { Check(false, "scripted squad stays Simulation cached; reason='" + Record.Reason + "' keepAwake='" + Record.KeepAwake + "'"); Finish("cached"); return; }
   if (Now() - PhaseAt < CACHED_SECONDS) return;
   int present;
   for (int pausedIndex = 0; pausedIndex < Actors.Count(); pausedIndex++)
   {
    if (Actors[pausedIndex] && Actors[pausedIndex].GetID() == Ids[pausedIndex] && Actors[pausedIndex].EBG_IsSimulationCached()) present++;
    PrintFormat("[EBG SCRIPTED SIM PAUSED] script='%1' bound=%2 playing=%3", EUS_Codes.Describe(Codes[pausedIndex]), Scripted(pausedIndex), Playing(Actors[pausedIndex]));
   }
   Check(present == Actors.Count(), "all scripted originals kept and paused (no deletion, no respawn)");
   Presence(Origin);
   Advance(33); return;
  }
  if (Phase == 33)
  {
   if (Record.Simulation || Record.Full) { Waited(30, "scripted squad woke from Simulation; reason='" + Record.Reason + "' recovery='" + Record.Recovery + "'"); return; }
   PrintFormat("[EBG SCRIPTED SIM WAKE] latency=%1 reason='%2'", Now() - PresenceAt, Record.Reason);
   Check(!Held.Suspended && Record.Recovery == "", "wake restored the snapshot without a recovery hold");
   Check(Squad.GetAgentsCount() == 6, "six members after the wake");
   foreach (EBG_SimulationAgent woken : Held.Members)
   {
    int wokenIndex = Actors.Find(woken.Character);
    if (wokenIndex < 0) continue;
    string outcome = "none";
    if (woken.Script) outcome = woken.Script.Outcome;
    if (outcome == "animation restarted") Restarted++;
    string described = EUS_Codes.Describe(Codes[wokenIndex]);
    PrintFormat("[EBG SCRIPTED SIM OUTCOME] script='%1' outcome='%2' paused=%3", described, outcome, woken.Position);
    Check(outcome.StartsWith("kept") || outcome == "animation restarted", "wake resumed the script: " + described + " (" + outcome + ")");
    Check(Actors[wokenIndex] && Actors[wokenIndex].GetID() == Ids[wokenIndex], "same original actor after the wake: " + described);
    Check(vector.DistanceXZ(Actors[wokenIndex].GetOrigin(), woken.Position) <= 0.25, "woke where he was paused: " + described);
    Check(Scripted(wokenIndex), "script bound with the same code after the wake: " + described);
   }
   Advance(34); return;
  }
  // Awake: no running around, scripts held, animations playing again.
  if (Phase == 34)
  {
   if (Record.Simulation || Record.Full) { Check(false, "squad stays awake while presence is injected"); Finish("awake"); return; }
   Sample();
   if (Now() - PhaseAt < AWAKE_SECONDS) return;
   for (int awakeIndex = 0; awakeIndex < Actors.Count(); awakeIndex++)
   {
    string script = EUS_Codes.Describe(Codes[awakeIndex]);
    PrintFormat("[EBG SCRIPTED SIM AWAKE] script='%1' drift=%2 limit=%3 bound=%4 playing=%5", script, Drift[awakeIndex], Limits[awakeIndex], Scripted(awakeIndex), Playing(Actors[awakeIndex]));
    Check(Drift[awakeIndex] <= Limits[awakeIndex], "stayed on his Unit Scripts spot after the wake: " + script);
    Check(Scripted(awakeIndex), "script still bound 20 s after the wake: " + script);
    if (EUS_Codes.IsAnimation(Codes[awakeIndex])) Check(Playing(Actors[awakeIndex]), "animation plays again after the wake: " + script);
   }
   Check(Record.KeepAwake.IsEmpty(), "no keep-awake hold after the wake");
   Finish("complete"); return;
  }
 }
}
// Presence seam, as in the wake budget, unit-cleanup and dialog cache fixtures.
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
