// TEST ONLY. Unit Caching performance soak (A/B), from the 0.1.15 performance plan (WP0).
// pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -FixturePath tests/EBG_PerfSoakGameplay.c -ExpectResult '\[EBG PERF SOAK RESULT\] checks=[1-9]\d* failures=0 mismatches=0 reason=complete' -TimeoutSeconds 600 -OrchestratorSlotGranted
// The runner copies this file to EXPG_GarrisonGameplay.c; the class names are fixed.
// Run it on the baseline snapshot (0.1.14 plus the WP0 commit, which only adds
// EBG_DebugChecks) and on the candidate snapshot, then compare the [EBG PERF] lines.
// Acceptance: tick_ms_total at most 50% of the baseline, no higher pump/tick max, and
// mismatches=0. Workload: 40 injected presence origins through the UpdatePlayers seam
// (20 inside the Simulation zone's wake radius, 20 on a 2 km ring), 16 USSR rifle squads
// (8 in one Simulation zone, 8 in one Full zone), half of every squad killed, and one
// magazine per Simulation squad moved between a dead and a living soldier's inventory
// every second (slot events). The Full zone caches its survivors once its setup
// presence leaves. Timing (System.GetTickCount deltas around EBG_CacheManager.Pump and
// Tick, EBG_CacheCleanup.CheckGroupTransfers and EXPG_GarrisonManager.Pump) runs with the
// index cross-checks off, so both snapshots pay the same cost; a following window with
// EBG_DebugChecks.Enabled counts mismatches with the same workload.
// The runner caps a run at 600 s, so the timed window defaults to 270 s. For a longer
// soak (the plan's 10 minutes) start the server by hand with -ebgSoakSeconds 600.
// No players, no GM UI, no save/load.
// Possession (UpdatePlayers' per-player PlayerPossession) is not in this A/B: with no
// connected players its loop runs zero times on both snapshots, and stand-in players need
// the candidate's player seam (EBG_CollectPlayers), which the 0.1.14 baseline lacks. The
// possession-on-change logic is exercised by the possession probe of
// EBG_UnitCleanupGameplay.c; measure its gain on the dedicated-server e2e with clients
// (EBG_CacheManager.Tick timing with players near and far from enrolled groups).
class EBGSoakSquad
{
 SCR_AIGroup Group;
 EBG_CacheGroup Record;
 bool Full;
 ref array<IEntity> Bodies = {};
 ref array<IEntity> Survivors = {};
 int Turn;
}
class EXPG_GarrisonGameplayClass : GenericEntityClass {}
class EXPG_GarrisonGameplay : GenericEntity
{
 static const float SETUP_SECONDS = 150;
 static const float CHECK_SECONDS = 45;
 static const float DEFAULT_SOAK_SECONDS = 270;
 static const int SQUADS_PER_ZONE = 8;
 static const int SQUAD_SIZE = 6;
 static const int KILLS_PER_SQUAD = 3;
 static const int NEAR_PRESENCE = 20;
 static const int FAR_PRESENCE = 20;
 static const string SQUAD_PREFAB = "{E552DABF3636C2AD}Prefabs/Groups/OPFOR/Group_USSR_RifleSquad.et";
 static const string ZONE_PREFAB = "{7E1080ED8F0633FD}PrefabsEditable/EXPBG/EBG_CacheZone.et";
 static ref array<vector> s_Presence;
 // Measurement, written by the modded overrides below. Primitive statics, no initializers.
 static bool s_Measuring;
 static int s_Pumps;
 static int s_PumpMs;
 static int s_PumpMax;
 static int s_Ticks;
 static int s_TickMs;
 static int s_TickMax;
 static int s_Transfers;
 static int s_TransferMs;
 static int s_TransferMax;
 static int s_TransferDepth;
 static int s_GarrisonPumps;
 static int s_GarrisonMs;
 static int s_GarrisonMax;
 ref array<ref EBGSoakSquad> Squads = {};
 EBG_CacheZone SimZone;
 EBG_CacheZone FullZone;
 vector SimPoint;
 vector FullPoint;
 vector SetupPresence;
 float SoakSeconds = DEFAULT_SOAK_SECONDS;
 int Phase;
 int Checks;
 int Failures;
 int MovesRequested;
 int MovesDone;
 int FullCachedSeen;
 int Frames;
 float FrameSeconds;
 float FrameMax;
 float Started;
 float Next;
 float PhaseAt;
 float NextMove;
 float NextSample;
 bool Finished;
 vector Origin = "4773.46 0 7094.57";

 void EXPG_GarrisonGameplay(IEntitySource src, IEntity parent) { SetEventMask(EntityEvent.INIT | EntityEvent.FRAME); }
 float Now()
 {
  return GetGame().GetWorld().GetWorldTime() * 0.001;
 }
 override void EOnInit(IEntity owner)
 {
  if (!Replication.IsServer()) { ClearEventMask(EntityEvent.FRAME); return; }
  // Timing runs without the cross-checks; the check window turns them on.
  EBG_DebugChecks.Enabled = false; EBG_DebugChecks.Mismatches = 0;
  s_Presence = {};
  ResetCounters();
  string value;
  if (System.GetCLIParam("ebgSoakSeconds", value)) SoakSeconds = Math.Clamp(value.ToFloat(), 60, 3600);
  SimPoint = Origin + Vector(-600, 0, -600);
  FullPoint = Origin + Vector(600, 0, -600);
  Started = Now(); Next = Started + 15;
  PrintFormat("[EBG PERF SOAK BEGIN] squads=%1 presenceOrigins=%2 soakSeconds=%3 checkSeconds=%4 connectedPlayers=0 presence=injected", SQUADS_PER_ZONE * 2, NEAR_PRESENCE + FAR_PRESENCE, SoakSeconds, CHECK_SECONDS);
 }
 static void ResetCounters()
 {
  s_Pumps = 0; s_PumpMs = 0; s_PumpMax = 0;
  s_Ticks = 0; s_TickMs = 0; s_TickMax = 0;
  s_Transfers = 0; s_TransferMs = 0; s_TransferMax = 0; s_TransferDepth = 0;
  s_GarrisonPumps = 0; s_GarrisonMs = 0; s_GarrisonMax = 0;
 }
 static void NotePump(int ms)
 {
  s_Pumps++; s_PumpMs += ms;
  if (ms > s_PumpMax) s_PumpMax = ms;
 }
 static void NoteTick(int ms)
 {
  s_Ticks++; s_TickMs += ms;
  if (ms > s_TickMax) s_TickMax = ms;
 }
 static void NoteTransfer(int ms)
 {
  s_Transfers++; s_TransferMs += ms;
  if (ms > s_TransferMax) s_TransferMax = ms;
 }
 static void NoteGarrisonPump(int ms)
 {
  s_GarrisonPumps++; s_GarrisonMs += ms;
  if (ms > s_GarrisonMax) s_GarrisonMax = ms;
 }
 bool Check(bool ok, string label)
 {
  Checks++;
  if (!ok) Failures++;
  PrintFormat("[EBG PERF SOAK CHECK] pass=%1 %2", ok, label);
  return ok;
 }
 void Finish(string reason)
 {
  if (Finished) return;
  Finished = true;
  s_Measuring = false;
  if (s_Presence) s_Presence.Clear();
  ClearEventMask(EntityEvent.FRAME);
  PrintFormat("[EBG PERF SOAK RESULT] checks=%1 failures=%2 mismatches=%3 reason=%4", Checks, Failures, EBG_DebugChecks.Mismatches, reason);
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
  // Keep the Resource in a local before spawning (inline temporaries returned null spawns).
  Resource resource = Resource.Load(prefab);
  return GetGame().SpawnEntityPrefab(resource, GetGame().GetWorld(), params);
 }
 // Cleanup on with the longest corpse age, so the ledger keeps every body for the whole run.
 EBG_CacheZone SpawnZone(vector point, int mode)
 {
  EBG_CacheZone zone = EBG_CacheZone.Cast(Spawn(ZONE_PREFAB, point));
  if (!zone)
  {
   return null;
  }
  zone.SetValue(1, mode); zone.SetValue(2, 0); zone.SetValue(3, 60);
  zone.SetValue(4, 100); zone.SetValue(5, 300); zone.SetValue(12, 5);
  zone.SetValue(15, 1); zone.SetValue(16, 3600); zone.SetValue(18, 0); zone.SetValue(21, 0);
  zone.SetValue(0, 1);
  return zone;
 }
 // Eight squads on a 20 m ring around a zone centre (dry points of the other fixtures).
 vector RingPoint(vector centre, int index, int count, float radius)
 {
  float angle = Math.PI2 * index / count;
  return centre + Vector(Math.Cos(angle) * radius, 0, Math.Sin(angle) * radius);
 }
 void AddPresenceRing(vector centre, int count, float radius)
 {
  for (int i = 0; i < count; i++) s_Presence.Insert(Ground(RingPoint(centre, i, count, radius), 1.8));
 }
 override void EOnFrame(IEntity owner, float timeSlice)
 {
  if (Finished) return;
  if (s_Measuring)
  {
   Frames++; FrameSeconds += timeSlice;
   if (timeSlice > FrameMax) FrameMax = timeSlice;
  }
  if (Now() < Next) return;
  Next = Now() + 0.25;
  if (Now() - Started > SETUP_SECONDS + SoakSeconds + CHECK_SECONDS + 30) { Check(false, "soak finished before the fixture deadline"); Finish("timeout"); return; }
  Step();
 }
 void Advance(int phase)
 {
  Phase = phase;
  PhaseAt = Now();
 }
 bool Waited(float seconds, string label)
 {
  if (Now() - PhaseAt <= seconds)
  {
   return false;
  }
  Check(false, label);
  Finish("phase " + Phase.ToString());
  return true;
 }
 void Step()
 {
  if (Phase == 0)
  {
   PrintFormat("[EBG PERF SOAK RUNTIME] systems='%1' players=%2 zones=%3", GetGame().GetSystemsConfig(), GetGame().GetPlayerManager().GetPlayerCount(), EBG_CacheZone.Zones.Count());
   if (!Check(GetGame().GetPlayerManager().GetPlayerCount() == 0 && EBG_CacheZone.Zones.IsEmpty(), "isolated server: no players, no zones")) { Finish("setup"); return; }
   for (int i = 0; i < SQUADS_PER_ZONE * 2; i++)
   {
    EBGSoakSquad squad = new EBGSoakSquad();
    squad.Full = i >= SQUADS_PER_ZONE;
    vector centre = SimPoint;
    if (squad.Full) centre = FullPoint;
    squad.Group = SCR_AIGroup.Cast(Spawn(SQUAD_PREFAB, RingPoint(centre, i % SQUADS_PER_ZONE, SQUADS_PER_ZONE, 20)));
    Squads.Insert(squad);
   }
   SimZone = SpawnZone(SimPoint, 0);
   FullZone = SpawnZone(FullPoint, 1);
   bool spawned = SimZone != null && FullZone != null;
   foreach (EBGSoakSquad spawnedSquad : Squads) { if (!spawnedSquad.Group) spawned = false; }
   if (!Check(spawned, "16 squads, one Simulation zone and one Full zone spawned")) { Finish("setup"); return; }
   // 20 presence origins inside the Simulation zone's wake radius, 20 on a 2 km ring
   // (more than 800 m from the Full zone), plus a setup presence holding the Full zone awake.
   AddPresenceRing(SimPoint, NEAR_PRESENCE, 40);
   AddPresenceRing(SimPoint, FAR_PRESENCE, 2000);
   SetupPresence = Ground(FullPoint, 1.8);
   s_Presence.Insert(SetupPresence);
   Advance(10); return;
  }
  if (Phase == 10)
  {
   foreach (EBGSoakSquad waiting : Squads)
   {
    if (!Enrolled(waiting)) { Waited(120, "all 16 squads spawned whole and enrolled with a cleanup ledger"); return; }
   }
   int swimming;
   foreach (EBGSoakSquad dry : Squads)
   {
    foreach (EBG_CacheMember member : dry.Record.Members)
    {
     if (member.Entity && member.Entity.GetCharacterController() && member.Entity.GetCharacterController().IsSwimming()) swimming++;
    }
   }
   Check(swimming == 0, string.Format("every squad stands on dry land (swimming=%1)", swimming));
   Kill();
   Advance(20); return;
  }
  if (Phase == 20)
  {
   if (Now() - PhaseAt < 5) return;
   int dead;
   foreach (EBGSoakSquad killed : Squads)
   {
    foreach (EBG_CacheMember casualty : killed.Record.Members) { if (casualty.Dead) dead++; }
   }
   if (dead < Squads.Count() * KILLS_PER_SQUAD) { Waited(20, string.Format("all %1 casualties confirmed (dead=%2)", Squads.Count() * KILLS_PER_SQUAD, dead)); return; }
   Check(true, string.Format("all %1 casualties confirmed", dead));
   // The Full zone's setup presence leaves: its survivors Full-cache during the soak.
   s_Presence.RemoveItem(SetupPresence);
   ResetCounters();
   Frames = 0; FrameSeconds = 0; FrameMax = 0;
   MovesRequested = 0; MovesDone = 0;
   s_Measuring = true;
   NextMove = Now(); NextSample = Now() + 60;
   PrintFormat("[EBG PERF SOAK WINDOW] timed=%1 crossChecks=0", SoakSeconds);
   Advance(30); return;
  }
  if (Phase == 30 || Phase == 40)
  {
   if (Now() >= NextMove) { NextMove = Now() + 1; MoveMagazines(); }
   ObserveFull();
  }
  if (Phase == 30)
  {
   if (Now() >= NextSample)
   {
    NextSample = Now() + 60;
    PrintFormat("[EBG PERF SAMPLE] t=%1 pumps=%2 pump_ms_total=%3 tick_ms_total=%4 transfer_calls=%5 transfer_ms_total=%6 moves=%7/%8 fullCached=%9", Now() - PhaseAt, s_Pumps, s_PumpMs, s_TickMs, s_Transfers, s_TransferMs, MovesDone, MovesRequested, FullCachedSeen);
   }
   if (Now() - PhaseAt < SoakSeconds) return;
   // Freeze the timing, then count index mismatches with the cross-checks on.
   s_Measuring = false;
   EBG_DebugChecks.Mismatches = 0;
   EBG_DebugChecks.Enabled = true;
   PrintFormat("[EBG PERF SOAK WINDOW] check=%1 crossChecks=1", CHECK_SECONDS);
   Advance(40); return;
  }
  if (Phase == 40)
  {
   if (Now() - PhaseAt < CHECK_SECONDS) return;
   Report();
   Finish("complete");
  }
 }
 // True once the manager owns the whole squad with a cleanup ledger.
 bool Enrolled(EBGSoakSquad squad)
 {
  if (!squad.Group || !squad.Group.EBG_HasCompletedInitialSpawn() || squad.Group.GetAgentsCount() != SQUAD_SIZE)
  {
   return false;
  }
  squad.Record = EBG_CacheManager.Get().FindGroup(squad.Group);
  return squad.Record != null && squad.Record.Members.Count() == SQUAD_SIZE && squad.Record.CleanupRegistered;
 }
 // Native death with a null instigator for three soldiers of every squad, never the leader.
 void Kill()
 {
  foreach (EBGSoakSquad squad : Squads)
  {
   IEntity leader = squad.Group.GetLeaderEntity();
   foreach (EBG_CacheMember member : squad.Record.Members)
   {
    if (!member.Entity) continue;
    if (squad.Bodies.Count() < KILLS_PER_SQUAD && member.Entity != leader) squad.Bodies.Insert(member.Entity);
    else squad.Survivors.Insert(member.Entity);
   }
   foreach (IEntity victim : squad.Bodies)
   {
    SCR_ChimeraCharacter victimCharacter = SCR_ChimeraCharacter.Cast(victim);
    SCR_CharacterDamageManagerComponent damage;
    if (victimCharacter) damage = SCR_CharacterDamageManagerComponent.Cast(victimCharacter.GetDamageManager());
    if (damage) damage.Kill(Instigator.CreateInstigator(null));
   }
  }
  PrintFormat("[EBG PERF SOAK KILLED] squads=%1 casualtiesPerSquad=%2", Squads.Count(), KILLS_PER_SQUAD);
 }
 // One magazine per awake Simulation squad and second, alternately from a body to a living
 // squadmate and back, through the destination's inventory manager (native slot events).
 void MoveMagazines()
 {
  foreach (EBGSoakSquad squad : Squads)
  {
   if (squad.Full || squad.Bodies.IsEmpty() || squad.Survivors.IsEmpty()) continue;
   squad.Turn++;
   IEntity body = squad.Bodies[squad.Turn % squad.Bodies.Count()];
   IEntity survivor = squad.Survivors[squad.Turn % squad.Survivors.Count()];
   if (!body || !survivor) continue;
   IEntity giver = body;
   IEntity taker = survivor;
   if (squad.Turn % 2 == 0) { giver = survivor; taker = body; }
   MovesRequested++;
   if (MoveOneMagazine(giver, taker)) MovesDone++;
  }
 }
 bool MoveOneMagazine(IEntity giver, IEntity taker)
 {
  SCR_InventoryStorageManagerComponent source = SCR_InventoryStorageManagerComponent.Cast(giver.FindComponent(SCR_InventoryStorageManagerComponent));
  SCR_InventoryStorageManagerComponent target = SCR_InventoryStorageManagerComponent.Cast(taker.FindComponent(SCR_InventoryStorageManagerComponent));
  if (!source || !target)
  {
   return false;
  }
  array<IEntity> magazines = {};
  source.FindItemsWithComponents(magazines, {MagazineComponent}, EStoragePurpose.PURPOSE_DEPOSIT);
  if (magazines.IsEmpty())
  {
   return false;
  }
  IEntity magazine = magazines[0];
  BaseInventoryStorageComponent storage = target.FindStorageForItem(magazine, EStoragePurpose.PURPOSE_DEPOSIT);
  if (!storage)
  {
   return false;
  }
  return target.TryMoveItemToStorage(magazine, storage);
 }
 void ObserveFull()
 {
  int cached;
  foreach (EBGSoakSquad squad : Squads)
  {
   if (squad.Full && squad.Record && squad.Record.Full && squad.Record.Full.GetState() == EBG_FullGroupPhase.CACHED) cached++;
  }
  if (cached > FullCachedSeen) FullCachedSeen = cached;
 }
 void Report()
 {
  int simSuspended;
  foreach (EBGSoakSquad squad : Squads)
  {
   if (!squad.Full && squad.Record && squad.Record.Simulation && squad.Record.Simulation.Suspended) simSuspended++;
  }
  float frameAverage = 0;
  if (Frames > 0) frameAverage = FrameSeconds * 1000 / Frames;
  PrintFormat("[EBG PERF] pumps=%1 pump_ms_total=%2 pump_ms_max=%3 tick_ms_total=%4 tick_ms_max=%5 transfer_calls=%6 transfer_ms_total=%7 mismatches=%8 seconds=%9", s_Pumps, s_PumpMs, s_PumpMax, s_TickMs, s_TickMax, s_Transfers, s_TransferMs, EBG_DebugChecks.Mismatches, SoakSeconds);
  PrintFormat("[EBG PERF DETAIL] ticks=%1 transfer_ms_max=%2 garrison_pumps=%3 garrison_ms_total=%4 garrison_ms_max=%5 frames=%6 frame_ms_avg=%7 frame_ms_max=%8", s_Ticks, s_TransferMax, s_GarrisonPumps, s_GarrisonMs, s_GarrisonMax, Frames, frameAverage, FrameMax * 1000);
  PrintFormat("[EBG PERF WORKLOAD] moves=%1/%2 fullCached=%3/%4 simSuspended=%5 presence=%6", MovesDone, MovesRequested, FullCachedSeen, SQUADS_PER_ZONE, simSuspended, s_Presence.Count());
  Check(s_Pumps > 0 && s_Ticks > 0, "the cache manager pumped and ticked during the timed window");
  Check(MovesDone > 0, "magazines moved between dead and living inventories");
  Check(FullCachedSeen == SQUADS_PER_ZONE, "every Full zone squad Full-cached its survivors during the soak");
  Check(simSuspended == 0, "the Simulation zone squads stayed awake beside the near presence");
  Check(EBG_DebugChecks.Mismatches == 0, "index cross-checks matched their old full scans");
 }
}
// Presence seam and timing wrappers. Each override calls super and adds no behaviour.
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
 override void Pump()
 {
  if (!EXPG_GarrisonGameplay.s_Measuring)
  {
   super.Pump();
   return;
  }
  int started = System.GetTickCount();
  super.Pump();
  EXPG_GarrisonGameplay.NotePump(System.GetTickCount() - started);
 }
 override void Tick()
 {
  if (!EXPG_GarrisonGameplay.s_Measuring)
  {
   super.Tick();
   return;
  }
  int started = System.GetTickCount();
  super.Tick();
  EXPG_GarrisonGameplay.NoteTick(System.GetTickCount() - started);
 }
}
modded class EBG_CacheCleanup
{
 // Outermost call only: nested transfer checks are part of their caller's time.
 override void CheckGroupTransfers(EBG_CacheGroup record)
 {
  if (!EXPG_GarrisonGameplay.s_Measuring || EXPG_GarrisonGameplay.s_TransferDepth > 0)
  {
   super.CheckGroupTransfers(record);
   return;
  }
  EXPG_GarrisonGameplay.s_TransferDepth++;
  int started = System.GetTickCount();
  super.CheckGroupTransfers(record);
  EXPG_GarrisonGameplay.NoteTransfer(System.GetTickCount() - started);
  EXPG_GarrisonGameplay.s_TransferDepth--;
 }
}
modded class EXPG_GarrisonManager
{
 override protected void Pump()
 {
  if (!EXPG_GarrisonGameplay.s_Measuring)
  {
   super.Pump();
   return;
  }
  int started = System.GetTickCount();
  super.Pump();
  EXPG_GarrisonGameplay.NoteGarrisonPump(System.GetTickCount() - started);
 }
}
