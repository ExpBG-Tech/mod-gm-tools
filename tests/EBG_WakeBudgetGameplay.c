// TEST ONLY. Unit Caching wake budget (#12), native save gate with enabled zones (#23) and
// the normal saved-settings hold release (#11, regression guard for the new watchdog).
// pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -FixturePath tests/EBG_WakeBudgetGameplay.c -ExpectResult '\[EBG WAKE TEST RESULT\] checks=[1-9]\d* failures=0 phase=51 reason=complete mismatches=0' -TimeoutSeconds 420 -OrchestratorSlotGranted
// Real zone prefab and public SetValue/EBG_QueueSavedAttribute, real USSR squads, production
// Tick/coordinator wake and the production mission-save Export. Presence is injected through
// EBG_CacheManager.UpdatePlayers; the server has no players. No GM UI, no file save/load.
// The runner copies this file to EXPG_GarrisonGameplay.c; the class names are fixed.
class EBGWakeSquad
{
 SCR_AIGroup Group;
 EBG_CacheGroup Record;
 float WokeAt = -1;
 int Members;
}
class EXPG_GarrisonGameplayClass : GenericEntityClass {}
class EXPG_GarrisonGameplay : GenericEntity
{
 static const float FIXTURE_SECONDS = 300;
 static const string SQUAD = "{E552DABF3636C2AD}Prefabs/Groups/OPFOR/Group_USSR_RifleSquad.et";
 static const string ZONE = "{7E1080ED8F0633FD}PrefabsEditable/EXPBG/EBG_CacheZone.et";
 static ref array<vector> s_Presence;
 ref array<ref EBGWakeSquad> Burst = {};
 ref EBGWakeSquad Single;
 ref EBGWakeSquad FullSquad;
 EBG_CacheZone SettingsZone;
 EBG_CacheZone SingleZone;
 EBG_CacheZone BurstZone;
 EBG_CacheZone FullZone;
 int Phase;
 int Checks;
 int Failures;
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
  Started = Now(); Next = Started + 15;
  PrintFormat("[EBG WAKE TEST BEGIN] budgetRate=%1 budgetBurst=%2 connectedPlayers=0 presence=injected deadline=%3", EBG_CacheManager.WAKE_BUDGET_RATE, EBG_CacheManager.WAKE_BUDGET_BURST, FIXTURE_SECONDS);
 }
 bool Check(bool ok, string label)
 {
  Checks++;
  if (!ok) Failures++;
  PrintFormat("[EBG WAKE TEST CHECK] pass=%1 %2", ok, label);
  return ok;
 }
 void Finish(string reason)
 {
  if (Finished) return;
  Finished = true;
  s_Presence.Clear();
  ClearEventMask(EntityEvent.FRAME);
  Check(EBG_DebugChecks.Mismatches == 0, "index cross-checks matched their old full scans");
  PrintFormat("[EBG WAKE TEST RESULT] checks=%1 failures=%2 phase=%3 reason=%4 mismatches=%5", Checks, Failures, Phase, reason, EBG_DebugChecks.Mismatches);
  GetGame().RequestClose();
 }
 vector Ground(vector p, float lift) { p[1] = GetGame().GetWorld().GetSurfaceY(p[0], p[2]) + lift; return p; }
 IEntity Spawn(string prefab, vector point)
 {
  EntitySpawnParams params = new EntitySpawnParams();
  params.TransformMode = ETransformMode.WORLD;
  Math3D.MatrixIdentity4(params.Transform);
  params.Transform[3] = Ground(point, 0.3);
  // Keep the Resource in a local before spawning (inline temporaries returned null spawns).
  Resource resource = Resource.Load(prefab);
  return GetGame().SpawnEntityPrefab(resource, GetGame().GetWorld(), params);
 }
 EBG_CacheZone SpawnZone(vector point, int mode)
 {
  EBG_CacheZone zone = EBG_CacheZone.Cast(Spawn(ZONE, point));
  if (!zone) return null;
  zone.SetValue(1, mode); zone.SetValue(2, 0); zone.SetValue(3, 60);
  zone.SetValue(4, 100); zone.SetValue(5, 300); zone.SetValue(12, 1);
  zone.SetValue(15, 0); zone.SetValue(18, 0); zone.SetValue(21, 1);
  zone.SetValue(0, 1);
  return zone;
 }
 EBGWakeSquad SpawnSquad(vector point)
 {
  EBGWakeSquad squad = new EBGWakeSquad();
  squad.Group = SCR_AIGroup.Cast(Spawn(SQUAD, point));
  return squad;
 }
 // True once the manager owns the whole squad.
 bool Enrolled(EBGWakeSquad squad)
 {
  // Full caching deletes the native group; keep the record found while it existed.
  if (!squad.Group) return squad.Record != null;
  if (!squad.Group.EBG_HasCompletedInitialSpawn()) return false;
  squad.Record = EBG_CacheManager.Get().FindGroup(squad.Group);
  if (!squad.Record || squad.Record.Members.Count() != 6) return false;
  squad.Members = squad.Record.Members.Count();
  return true;
 }
 bool SimCached(EBGWakeSquad squad) { return squad.Record && squad.Record.Simulation && squad.Record.Simulation.Suspended; }
 bool TryExport(out string reason)
 {
  EBG_MissionPersistenceState state = EBG_MissionPersistenceState.Get();
  if (!state) { reason = "mission persistence state unavailable"; return false; }
  return EBG_MissionPersistence.Export(state, reason);
 }
 void Presence(vector point)
 {
  s_Presence.Insert(Ground(point, 1.8));
  PresenceAt = Now();
 }
 override void EOnFrame(IEntity owner, float timeSlice)
 {
  if (Finished) return;
  // The burst is observed every frame; everything else steps every 0.25 s.
  if (Phase == 32) ObserveBurst();
  if (Now() < Next) return;
  Next = Now() + 0.25;
  if (Now() - Started > FIXTURE_SECONDS) { Check(false, "all phases finished before the fixture deadline"); Finish("timeout"); return; }
  Step();
 }
 void Advance(int phase) { Phase = phase; PhaseAt = Now(); }
 bool Waited(float seconds, string label)
 {
  if (Now() - PhaseAt <= seconds) return false;
  Check(false, label);
  Finish("phase " + Phase.ToString());
  return true;
 }
 void Step()
 {
  if (Phase == 0)
  {
   PrintFormat("[EBG WAKE TEST RUNTIME] systems='%1' players=%2 zones=%3", GetGame().GetSystemsConfig(), GetGame().GetPlayerManager().GetPlayerCount(), EBG_CacheZone.Zones.Count());
   if (!Check(GetGame().GetPlayerManager().GetPlayerCount() == 0 && EBG_CacheZone.Zones.IsEmpty(), "isolated server: no players, no zones")) { Finish("setup"); return; }
   SettingsZone = EBG_CacheZone.Cast(Spawn(ZONE, Origin + Vector(0, 0, -600)));
   if (!Check(SettingsZone != null, "settings zone spawned")) { Finish("setup"); return; }
   Advance(10); return;
  }
  // #11: a saved-settings batch through the session attribute path (as CDF applies it)
  // releases its hold on its own within seconds; the watchdog must stay silent.
  if (Phase == 10)
  {
   if (Now() - PhaseAt < 2) return;
   // Only the keys the GM attribute list serializes, as CDF applies them.
   array<int> keys = {0, 22, 1, 2, 3, 4, 5, 9, 10, 12, 15, 16, 18, 21, 23};
   foreach (int key : keys)
   {
    float value = SettingsZone.GetValue(key);
    if (key == 0) value = 1;
    if (key == 3) value = 40;
    if (key == 12) value = 7;
    if (key == 18) value = 0;
    SettingsZone.EBG_QueueSavedAttribute(key, value);
   }
   Check(SettingsZone.EBG_HasSettingsLoadHold() && SettingsZone.Editing, "saved settings batch arms the load hold");
   Advance(11); return;
  }
  if (Phase == 11)
  {
   if (SettingsZone.EBG_HasSettingsLoadHold() || SettingsZone.Editing)
   {
    if (Waited(8, "saved settings hold released on its own")) PrintFormat("[EBG WAKE TEST SETTINGS] status='%1' reason='%2'", SettingsZone.Status, SettingsZone.EBG_SettingsLoadReason());
    return;
   }
   Check(SettingsZone.Enabled == 1 && SettingsZone.Affected == 40 && SettingsZone.SleepDelay == 7 && !SettingsZone.HasPendingSettings(), string.Format("saved settings applied after release in %1 s", Now() - PhaseAt));
   SCR_EntityHelper.DeleteEntityAndChildren(SettingsZone);
   Single = SpawnSquad(Origin + Vector(-600, 0, -600));
   SingleZone = SpawnZone(Origin + Vector(-600, 0, -600), 0);
   if (!Check(Single.Group && SingleZone, "single squad and Simulation zone spawned")) { Finish("setup"); return; }
   Advance(20); return;
  }
  // #12 baseline: one group still wakes at once.
  if (Phase == 20)
  {
   if (!Enrolled(Single) || !SimCached(Single)) { Waited(60, "single squad enrolled and Simulation cached"); return; }
   Advance(21); return;
  }
  if (Phase == 21)
  {
   if (Now() - PhaseAt < 4) return;
   Presence(Origin + Vector(-600, 0, -600));
   Advance(22); return;
  }
  if (Phase == 22)
  {
   if (Single.Record.Simulation) { Waited(10, "single squad woke when presence arrived"); return; }
   float latency = Now() - PresenceAt;
   PrintFormat("[EBG WAKE TEST SINGLE] latency=%1 members=%2", latency, Single.Members);
   Check(latency <= 1.5, "a single group wakes immediately (no budget wait)");
   s_Presence.Clear();
   SCR_EntityHelper.DeleteEntityAndChildren(SingleZone);
   vector burstPoint = Origin + Vector(600, 0, -600);
   array<vector> offsets = {Vector(0, 0, 0), Vector(15, 0, 0), Vector(-15, 0, 0), Vector(0, 0, 15), Vector(0, 0, -15)};
   foreach (vector offset : offsets) Burst.Insert(SpawnSquad(burstPoint + offset));
   BurstZone = SpawnZone(burstPoint, 0);
   bool spawned = BurstZone != null;
   foreach (EBGWakeSquad spawnedSquad : Burst) { if (!spawnedSquad.Group) spawned = false; }
   if (!Check(spawned, "five squads and one whole-zone Simulation zone spawned")) { Finish("setup"); return; }
   Advance(30); return;
  }
  if (Phase == 30)
  {
   foreach (EBGWakeSquad waiting : Burst)
   {
    if (!Enrolled(waiting) || !SimCached(waiting)) { Waited(90, "five squads enrolled and Simulation cached"); return; }
   }
   // #23: a cached group still refuses the native save, with the existing reason.
   string cachedReason;
   bool cachedExport = TryExport(cachedReason);
   PrintFormat("[EBG WAKE TEST SAVE] case=simulation-cached exported=%1 reason='%2'", cachedExport, cachedReason);
   Check(!cachedExport && cachedReason == "Restore ALL cache/recovery state before saving", "native save refused while a group is Simulation cached");
   Advance(31); return;
  }
  if (Phase == 31)
  {
   // Let the shared budget refill completely before the burst.
   if (Now() - PhaseAt < 4) return;
   Presence(Origin + Vector(600, 0, -600));
   Advance(32); return;
  }
  if (Phase == 32)
  {
   int awake;
   foreach (EBGWakeSquad observed : Burst) { if (observed.WokeAt >= 0) awake++; }
   if (awake < Burst.Count()) { Waited(30, "all five squads woke"); return; }
   CheckBurst();
   Advance(40); return;
  }
  // #23: zones enabled and every group awake: the native save is allowed.
  if (Phase == 40)
  {
   string awakeReason;
   bool awakeExport = TryExport(awakeReason);
   PrintFormat("[EBG WAKE TEST SAVE] case=enabled-all-awake exported=%1 reason='%2' zonesEnabled=%3", awakeExport, awakeReason, BurstZone.Enabled && !BurstZone.Editing);
   Check(awakeExport && awakeReason == "", "native save accepted with enabled zones and every group awake");
   FullSquad = SpawnSquad(Origin + Vector(-600, 0, 600));
   FullZone = SpawnZone(Origin + Vector(-600, 0, 600), 1);
   if (!Check(FullSquad.Group && FullZone, "Full squad and zone spawned")) { Finish("setup"); return; }
   Advance(50); return;
  }
  if (Phase == 50)
  {
   if (!Enrolled(FullSquad) || !FullSquad.Record.Full || FullSquad.Record.Full.GetState() != EBG_FullGroupPhase.CACHED) { Waited(90, "Full squad cached"); return; }
   SaveGameManager saving = GetGame().GetSaveGameManager();
   bool enabled = saving && saving.IsSavingEnabled();
   string fullReason;
   bool fullExport = TryExport(fullReason);
   PrintFormat("[EBG WAKE TEST SAVE] case=full-cached exported=%1 reason='%2' savingEnabled=%3 held=%4", fullExport, fullReason, enabled, EBG_FullSaveGate.IsHeld());
   Check(!fullExport && fullReason == "Restore ALL cache/recovery state before saving", "native save refused while a group is Full cached");
   Check(EBG_FullSaveGate.IsHeld() == enabled && (!enabled || !saving.IsSavingAllowed()), "native save permission withheld while Full survivors are absent");
   Presence(Origin + Vector(-600, 0, 600));
   Advance(51); return;
  }
  if (Phase == 51)
  {
   if (FullSquad.Record.Full || EBG_FullSaveGate.IsHeld()) { Waited(60, "Full squad woke and the save hold returned on its own"); return; }
   SaveGameManager released = GetGame().GetSaveGameManager();
   bool releasedEnabled = released && released.IsSavingEnabled();
   string restoredReason;
   bool restoredExport = TryExport(restoredReason);
   int agents = -1;
   if (FullSquad.Record.Group) agents = FullSquad.Record.Group.GetAgentsCount();
   PrintFormat("[EBG WAKE TEST SAVE] case=full-awake exported=%1 reason='%2' savingEnabled=%3 allowed=%4 agents=%5", restoredExport, restoredReason, releasedEnabled, released && released.IsSavingAllowed(), agents);
   Check(agents == 6 && (!releasedEnabled || released.IsSavingAllowed()), "native save permission returns once every Full group is awake, without Prepare for save");
   Check(restoredExport && restoredReason == "", "native save accepted after the Full group woke");
   Finish("complete");
   return;
  }
 }
 void ObserveBurst()
 {
  foreach (EBGWakeSquad squad : Burst)
  {
   if (squad.WokeAt < 0 && squad.Record && !squad.Record.Simulation) squad.WokeAt = Now();
  }
 }
 // Token bucket: restored characters by time t never exceed burst + rate * (t - first wake).
 void CheckBurst()
 {
  array<float> times = {};
  foreach (EBGWakeSquad squad : Burst) times.Insert(squad.WokeAt);
  times.Sort();
  float first = times[0];
  float last = times[times.Count() - 1];
  bool bounded = true;
  for (int i = 0; i < times.Count(); i++)
  {
   float restored = (i + 1) * 6;
   float allowed = EBG_CacheManager.WAKE_BUDGET_BURST + EBG_CacheManager.WAKE_BUDGET_RATE * (times[i] - first) + 1;
   PrintFormat("[EBG WAKE TEST BURST WAKE] index=%1 t=%2 restored=%3 allowed=%4", i, times[i] - first, restored, allowed);
   if (restored > allowed) bounded = false;
  }
  float spread = last - first;
  float latency = first - PresenceAt;
  bool intact = true;
  foreach (EBGWakeSquad woken : Burst)
  {
   if (!woken.Record || woken.Record.Recovery != "" || !woken.Group || woken.Group.GetAgentsCount() != 6) intact = false;
  }
  PrintFormat("[EBG WAKE TEST BURST] groups=%1 characters=%2 firstLatency=%3 spreadSeconds=%4 bounded=%5 intact=%6", times.Count(), times.Count() * 6, latency, spread, bounded, intact);
  Check(latency <= 1.5, "the first group of a burst wakes immediately");
  Check(bounded, "restored characters never exceed the shared wake budget");
  Check(spread >= 4.2, "five groups woken at once are spread over successive ticks (old pacing: 2 s)");
  Check(spread <= 12, "the burst still completes in bounded time");
  Check(intact, "every burst group restored intact, no recovery");
 }
}
// Presence seam, as in the unit-cleanup fixture.
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
