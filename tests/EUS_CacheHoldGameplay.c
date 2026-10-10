// TEST ONLY. EXPBG Unit Scripts on a squad that Unit Caching already manages.
// pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -FixturePath tests/EUS_CacheHoldGameplay.c -ExpectResult '\[EUS CACHE HOLD RESULT\] checks=[1-9]\d* failures=0 reason=complete mismatches=0' -OrchestratorSlotGranted
// The runner copies this file to EXPG_GarrisonGameplay.c; the class names are fixed.
// Real USSR rifle squad and Unit Caching zone prefab (Full mode, 5 s sleep delay). The
// squad is enrolled first and only then scripted through the production EUS_Manager
// entry points (the ones the context actions and attributes call); enrollment itself
// already refuses scripted squads. While night discipline runs, the production scheduler
// must keep the record awake (no Full transaction, original actors kept) well past the
// sleep delay. Hold and Freeze no longer hold a squad awake since 0.1.20: they Full
// cache and bind again on the respawned soldiers (EBG_FullScriptCarryGameplay.c). After
// the Game Master releases the discipline, the same record must Full-cache and wake
// again on injected presence. No players, no GM
// UI, no save/load. Server log: "[EBG KEEP AWAKE] ... held: ..." on each hold change
// and "[EBG KEEP AWAKE] ... released; normal sleep rules resume" after the release.
class EXPG_GarrisonGameplayClass : GenericEntityClass {}
class EXPG_GarrisonGameplay : GenericEntity
{
 static const float FIXTURE_SECONDS = 240;
 static const float HOLD_SECONDS = 20;
 static const float DISCIPLINE_SECONDS = 12;
 static const string SQUAD_PREFAB = "{E552DABF3636C2AD}Prefabs/Groups/OPFOR/Group_USSR_RifleSquad.et";
 static const string ZONE_PREFAB = "{7E1080ED8F0633FD}PrefabsEditable/EXPBG/EBG_CacheZone.et";
 static const string HOLD_PREFIX = "Held awake by EXPBG Unit Scripts:";
 static ref array<vector> s_Presence;
 SCR_AIGroup Squad;
 EBG_CacheZone FullZone;
 EBG_CacheGroup Record;
 SCR_ChimeraCharacter Holder;
 EntityID HolderId;
 EUS_Manager Manager;
 ref EUS_Report Report = new EUS_Report();
 int Phase;
 int Checks;
 int Failures;
 float Started;
 float Next;
 float PhaseAt;
 float PresenceAt;
 bool Finished;
 bool HoldSeen;
 bool DisciplineSeen;
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
  PrintFormat("[EUS CACHE HOLD BEGIN] holdSeconds=%1 disciplineSeconds=%2 connectedPlayers=0 presence=injected deadline=%3", HOLD_SECONDS, DISCIPLINE_SECONDS, FIXTURE_SECONDS);
 }
 bool Check(bool ok, string label)
 {
  Checks++;
  if (!ok) Failures++;
  PrintFormat("[EUS CACHE HOLD CHECK] pass=%1 %2", ok, label);
  return ok;
 }
 void Finish(string reason)
 {
  if (Finished) return;
  Finished = true;
  if (s_Presence) s_Presence.Clear();
  ClearEventMask(EntityEvent.FRAME);
  Check(EBG_DebugChecks.Mismatches == 0, "index cross-checks matched their old full scans");
  PrintFormat("[EUS CACHE HOLD RESULT] checks=%1 failures=%2 reason=%3 mismatches=%4", Checks, Failures, reason, EBG_DebugChecks.Mismatches);
  GetGame().RequestClose();
 }
 vector Ground(vector p, float lift) { p[1] = GetGame().GetWorld().GetSurfaceY(p[0], p[2]) + lift; return p; }
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
 // Full mode (1), 60 m zone, 5 s sleep delay (MinimumActive follows it), debug messages on.
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
 void Advance(int phase) { Phase = phase; PhaseAt = Now(); }
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
  if (Now() - Started > FIXTURE_SECONDS) { Check(false, "all phases finished before the fixture deadline"); Finish("timeout"); return; }
  Step();
 }

 void Step()
 {
  if (Phase == 0)
  {
   PrintFormat("[EUS CACHE HOLD RUNTIME] systems='%1' players=%2 zones=%3", GetGame().GetSystemsConfig(), GetGame().GetPlayerManager().GetPlayerCount(), EBG_CacheZone.Zones.Count());
   if (!Check(GetGame().GetPlayerManager().GetPlayerCount() == 0 && EBG_CacheZone.Zones.IsEmpty(), "isolated server: no players, no zones")) { Finish("setup"); return; }
   Squad = SCR_AIGroup.Cast(Spawn(SQUAD_PREFAB, Origin));
   if (!Check(Squad != null, "USSR rifle squad spawned")) { Finish("setup"); return; }
   Advance(10); return;
  }
  if (Phase == 10)
  {
   if (!Squad || !Squad.EBG_HasCompletedInitialSpawn() || Squad.GetAgentsCount() != 6) { Waited(60, "squad finished its initial spawn with six members"); return; }
   FullZone = SpawnZone(Origin);
   if (!Check(FullZone != null, "Full cache zone spawned over the unscripted squad")) { Finish("setup"); return; }
   Advance(20); return;
  }
  // Script the squad the moment it is enrolled: well inside the 5 s sleep delay.
  if (Phase == 20)
  {
   if (!Squad) { Check(false, "squad enrolled before it was cached"); Finish("enroll"); return; }
   Record = EBG_CacheManager.Get().FindGroup(Squad);
   if (!Record || Record.Members.Count() != 6) { Waited(60, "unscripted squad enrolled by the Full zone"); return; }
   if (!Check(!Record.Full && !Record.Simulation, "enrolled squad is still awake when the Game Master scripts it")) { Finish("enroll"); return; }
   array<AIAgent> agents = {};
   Squad.GetAgents(agents);
   Holder = SCR_ChimeraCharacter.Cast(agents[agents.Count() - 1].GetControlledEntity());
   if (!Check(Holder != null, "squad member chosen for Hold")) { Finish("setup"); return; }
   HolderId = Holder.GetID();
   Manager = EUS_Manager.Get();
   Check(Manager != null && Manager.SetDiscipline(Squad, EUS_Codes.DISCIPLINE_LIGHT, Report), "Light Discipline applied to the enrolled squad");
   string hookReason = EBG_CacheManager.Get().KeepAwakeReason(Squad);
   PrintFormat("[EUS CACHE HOLD APPLIED] record=%1 hook='%2' reason='%3'", Record.Id, hookReason, Record.Reason);
   Check(hookReason.StartsWith(HOLD_PREFIX), "Unit Caching sees the Unit Scripts hold through its published seam");
   Advance(21); return;
  }
  if (!Record) { Check(false, "logical cache record retained across the test"); Finish("record"); return; }
  if (Phase == 21)
  {
   if (Record.Full || Record.Simulation)
   {
    Check(false, "squad never starts a cache transition while night discipline runs; reason='" + Record.Reason + "'");
    Finish("hold"); return;
   }
   if (!Holder || !EUS_Manager.Reserves(Squad))
   {
    Check(false, "held squad and its discipline survive the hold window");
    Finish("hold"); return;
   }
   // The hold itself must be the scheduler's effective reason, not another blocker.
   if (Record.KeepAwake.StartsWith(HOLD_PREFIX) && Record.Reason == Record.KeepAwake) HoldSeen = true;
   if (Now() - PhaseAt < HOLD_SECONDS) return;
   PrintFormat("[EUS CACHE HOLD WINDOW] stage=hold seconds=%1 keepAwake='%2' reason='%3' clearSince=%4", HOLD_SECONDS, Record.KeepAwake, Record.Reason, Record.ClearSince);
   Check(HoldSeen, "scheduler reports the record held awake by Unit Scripts");
   Check(Squad != null && Squad.GetAgentsCount() == 6 && Holder.GetID() == HolderId, "all six original members kept 20 s past the 5 s sleep delay");
   Check(Manager.SetDiscipline(Squad, EUS_Codes.DISCIPLINE_OFF, Report) && !EUS_Manager.Reserves(Squad), "night discipline released; squad no longer reserved");
   string releasedHold = EBG_CacheManager.Get().KeepAwakeReason(Squad);
   Check(releasedHold.IsEmpty(), "no Unit Scripts hold remains after the release");
   Advance(23); return;
  }
  if (Phase == 22)
  {
   if (Record.Full || Record.Simulation)
   {
    Check(false, "night discipline alone keeps the squad awake; reason='" + Record.Reason + "'");
    Finish("discipline"); return;
   }
   if (Record.KeepAwake.StartsWith(HOLD_PREFIX) && Record.KeepAwake.Contains("Light Discipline") && !Record.KeepAwake.Contains("scripted") && Record.Reason == Record.KeepAwake) DisciplineSeen = true;
   if (Now() - PhaseAt < DISCIPLINE_SECONDS) return;
   PrintFormat("[EUS CACHE HOLD WINDOW] stage=discipline seconds=%1 keepAwake='%2' reason='%3' clearSince=%4", DISCIPLINE_SECONDS, Record.KeepAwake, Record.Reason, Record.ClearSince);
   Check(DisciplineSeen, "hold follows the remaining night discipline after Hold is released");
   Check(Squad != null && Squad.GetAgentsCount() == 6 && Holder != null && Holder.GetID() == HolderId, "original members kept while night discipline runs");
   Check(Manager.SetDiscipline(Squad, EUS_Codes.DISCIPLINE_OFF, Report) && !EUS_Manager.Reserves(Squad), "night discipline released; squad no longer reserved");
   string releasedReason = EBG_CacheManager.Get().KeepAwakeReason(Squad);
   Check(releasedReason.IsEmpty(), "no Unit Scripts hold remains after the release");
   Advance(23); return;
  }
  // Normal sleep rules resume: clear delay, then the production Full capture.
  if (Phase == 23)
  {
   if (!Record.Full || Record.Full.GetState() != EBG_FullGroupPhase.CACHED) { Waited(90, "released squad Full cached; reason='" + Record.Reason + "' recovery='" + Record.Recovery + "'"); return; }
   IEntity oldHolder = GetGame().GetWorld().FindEntityByID(HolderId);
   PrintFormat("[EUS CACHE HOLD CACHED] latency=%1 keepAwake='%2'", Now() - PhaseAt, Record.KeepAwake);
   Check(Record.KeepAwake.IsEmpty(), "hold cleared before the Full capture");
   Check(!oldHolder && !Holder, "Full capture deleted the original characters only after the release");
   Advance(24); return;
  }
  if (Phase == 24)
  {
   if (Now() - PhaseAt < 3) return;
   Presence(Origin);
   Advance(25); return;
  }
  if (Phase == 25)
  {
   if (Record.Full || !Record.Group || Record.Group.GetAgentsCount() != 6 || Record.Recovery != "") { Waited(60, "squad woke from Full with six members; reason='" + Record.Reason + "' recovery='" + Record.Recovery + "'"); return; }
   PrintFormat("[EUS CACHE HOLD WAKE] latency=%1", Now() - PresenceAt);
   array<AIAgent> woken = {};
   Record.Group.GetAgents(woken);
   int stillScripted;
   foreach (AIAgent wokenAgent : woken)
   {
    SCR_ChimeraCharacter wokenMember = SCR_ChimeraCharacter.Cast(wokenAgent.GetControlledEntity());
    if (wokenMember && wokenMember.EUS_Script != EUS_Codes.NONE) stillScripted++;
   }
   Check(stillScripted == 0 && Record.KeepAwake.IsEmpty(), "restored squad has six members, no unit script and no hold");
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
