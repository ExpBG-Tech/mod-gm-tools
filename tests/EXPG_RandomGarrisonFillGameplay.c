// TEST ONLY. Random Garrison reaching its building target (addon/random-garrison).
// pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -FixturePath tests/EXPG_RandomGarrisonFillGameplay.c -ExpectResult '\[EXPG RANDOM FILL RESULT\] checks=[1-9]\d* failures=0 seeds=3 reached=3 repeated=1 reported=1 explained=1 maxTries=\d+ maxFailed=\d+ reason=completed' -TimeoutSeconds 600 -OrchestratorSlotGranted
// The runner copies this file to EXPG_GarrisonGameplay.c; the class names are fixed.
// The GM_Eden town centre 4773 169 7094 of tests/EXPG_RandomGarrisonGameplay.c, no
// player, radius 150, 4 buildings, one squad per building, faction US, cache Off.
//  1. Fire teams and squads (the preset of the Chernarus Minus report), seeds 101, 202
//     and 303 one after the other: each ends Done with 4 distinct buildings; the
//     tries stay within EXPG_RGRules.AttemptCap(4, eligible); a generation with
//     failed buildings lists them by reason in the status ("N failed: ..."). Clear
//     between them, and wait until the zone is Idle.
//  2. Seed 101 again: the same buildings in the same order.
//  3. Squads only (6-9 soldiers, which most houses and sheds cannot take), seed 404:
//     Done; with 4 buildings, or with "x of 4 buildings" and the reason no more were
//     tried ("none left" or the try limit) in the status.
// No GM UI, no save/load, no multiplayer.
modded class EXPG_RandomGarrisonModule
{
 int EXPG_TestAttempts()
 {
  return m_iAttempts;
 }

 int EXPG_TestAttemptCap()
 {
  return m_iAttemptCap;
 }

 int EXPG_TestFailures()
 {
  return m_iFailures;
 }
}

class EXPG_GarrisonGameplayClass : GenericEntityClass {}
class EXPG_GarrisonGameplay : GenericEntity
{
 static const float FIXTURE_SECONDS = 570;
 static const float PHASE_SECONDS = 150;
 static const ResourceName ZONE_PREFAB = "{1B71589F70C61F8E}PrefabsEditable/EXPGR/EXPG_RandomGarrison.et";
 static const int TARGET = 4;
 static const int SEED_COUNT = 3;
 static const int REPEAT_SEED = 101;
 static const int SQUADS_SEED = 404;
 vector Center = "4773 0 7094";
 EXPG_RandomGarrisonModule Zone;
 EXPG_GarrisonManager Manager;
 int Phase;
 int RunIndex;
 int Checks;
 int Failures;
 int Seeds;
 int Reached;
 int Repeated;
 int Reported = 1;
 int Explained;
 int MaxTries;
 int MaxFailed;
 float Started;
 float Next;
 float PhaseStarted;
 bool Finished;
 ref array<IEntity> FirstBuildings = {};

 void EXPG_GarrisonGameplay(IEntitySource src, IEntity parent)
 {
  SetEventMask(EntityEvent.INIT | EntityEvent.FRAME);
 }

 float Now()
 {
  return GetGame().GetWorld().GetWorldTime() * 0.001;
 }

 override void EOnInit(IEntity owner)
 {
  if (!Replication.IsServer())
  {
   ClearEventMask(EntityEvent.FRAME);
   return;
  }
  Started = Now();
  Next = Started + 15;
  PrintFormat("[EXPG RANDOM FILL BEGIN] center=%1 radius=150 target=%2 seeds=101,202,303 repeat=%3 squadsSeed=%4 deadline=%5", Center, TARGET, REPEAT_SEED, SQUADS_SEED, FIXTURE_SECONDS);
 }

 int SeedOf(int run)
 {
  if (run == 0)
  {
   return 101;
  }
  if (run == 1)
  {
   return 202;
  }
  return 303;
 }

 bool Check(bool value, string label)
 {
  Checks++;
  if (!value) { Failures++; }
  PrintFormat("[EXPG RANDOM FILL CHECK] pass=%1 %2", value, label);
  return value;
 }

 void Finish(string reason)
 {
  if (Finished) { return; }
  Finished = true;
  ClearEventMask(EntityEvent.FRAME);
  string first = string.Format("checks=%1 failures=%2 seeds=%3 reached=%4 repeated=%5", Checks, Failures, Seeds, Reached, Repeated);
  string second = string.Format("reported=%1 explained=%2 maxTries=%3 maxFailed=%4 reason=%5", Reported, Explained, MaxTries, MaxFailed, reason);
  PrintFormat("[EXPG RANDOM FILL RESULT] %1 %2", first, second);
  GetGame().RequestClose();
 }

 void Advance(int phase)
 {
  Phase = phase;
  PhaseStarted = Now();
  string status;
  if (Zone) { status = Zone.GetStatus(); }
  PrintFormat("[EXPG RANDOM FILL PHASE] phase=%1 run=%2 elapsed=%3 status=%4", Phase, RunIndex, Now() - Started, status);
 }

 bool Waited(float seconds, string label)
 {
  if (Now() - PhaseStarted <= seconds)
  {
   return false;
  }
  Check(false, label);
  Finish("phase " + Phase.ToString());
  return true;
 }

 void Generate(int seed, int sizes)
 {
  Zone.ApplySetting(EXPG_RandomGarrisonModule.KEY_SEED, seed, false);
  Zone.ApplySetting(EXPG_RandomGarrisonModule.KEY_SIZES, sizes, false);
  Check(Zone.Run(EXPG_RandomGarrisonModule.ACTION_GENERATE, -1), string.Format("Generate accepted (seed %1, sizes %2)", seed, sizes));
 }

 // A finished generation: buildings, tries within the limit, failures in the status.
 int CheckDone(string label, array<IEntity> outStructures)
 {
  array<string> prefabs = {};
  Zone.PlacedSites(outStructures, prefabs);
  int eligible = Zone.EligibleCount();
  int tries = Zone.EXPG_TestAttempts();
  int cap = Zone.EXPG_TestAttemptCap();
  int failed = Zone.EXPG_TestFailures();
  string status = Zone.GetStatus();
  PrintFormat("[EXPG RANDOM FILL DONE] %1 eligible=%2 buildings=%3 tries=%4/%5 failed=%6 status=%7", label, eligible, outStructures.Count(), tries, cap, failed, status);
  if (tries > MaxTries) { MaxTries = tries; }
  if (failed > MaxFailed) { MaxFailed = failed; }
  Check(cap == EXPG_RGRules.AttemptCap(TARGET, eligible) && tries <= cap, string.Format("%1: %2 tries within the limit %3 for %4 eligible buildings", label, tries, cap, eligible));
  bool distinct = true;
  for (int i = 0; i < outStructures.Count(); i++)
  {
   for (int j = i + 1; j < outStructures.Count(); j++)
   {
    if (outStructures[j] == outStructures[i]) { distinct = false; }
   }
  }
  Check(distinct, label + ": distinct buildings");
  bool listed = failed == 0 || status.Contains(string.Format("%1 failed: ", failed));
  if (!Check(listed, string.Format("%1: the status lists the %2 failed buildings by reason", label, failed))) { Reported = 0; }
  return outStructures.Count();
 }

 override void EOnFrame(IEntity owner, float timeSlice)
 {
  if (Finished || Now() < Next) { return; }
  Next = Now() + 0.5;
  if (Now() - Started > FIXTURE_SECONDS)
  {
   Check(false, string.Format("%1 second deadline; last phase %2 run %3", FIXTURE_SECONDS, Phase, RunIndex));
   Finish("timeout");
   return;
  }
  if (Phase == 0)
  {
   Manager = EXPG_GarrisonManager.Get();
   if (!Check(Manager != null, "garrison manager on the server")) { Finish("manager"); return; }
   EntitySpawnParams spawn = new EntitySpawnParams();
   spawn.TransformMode = ETransformMode.WORLD;
   Math3D.AnglesToMatrix(vector.Zero, spawn.Transform);
   vector point = Center;
   point[1] = GetGame().GetWorld().GetSurfaceY(point[0], point[2]);
   spawn.Transform[3] = point;
   Resource zoneResource = Resource.Load(ZONE_PREFAB);
   Zone = EXPG_RandomGarrisonModule.Cast(GetGame().SpawnEntityPrefab(zoneResource, GetGame().GetWorld(), spawn));
   if (!Check(Zone != null, "Random Garrison prefab spawned")) { Finish("spawn"); return; }
   Advance(1);
   return;
  }
  if (Phase == 1)
  {
   if (Zone.Token().IsEmpty())
   {
    Waited(10, "zone registered with a token");
    return;
   }
   Zone.ApplySetting(EXPG_RandomGarrisonModule.KEY_RADIUS, 150, false);
   Zone.ApplySetting(EXPG_RandomGarrisonModule.KEY_BUILDINGS, TARGET, false);
   Zone.ApplySetting(EXPG_RandomGarrisonModule.KEY_SHARE, 100, false);
   Zone.ApplySetting(EXPG_RandomGarrisonModule.KEY_SQUADS_MIN, 1, false);
   Zone.ApplySetting(EXPG_RandomGarrisonModule.KEY_SQUADS_MAX, 1, false);
   Zone.ApplySetting(EXPG_RandomGarrisonModule.KEY_PLAYER_DISTANCE, 0, false);
   Zone.ApplySetting(EXPG_RandomGarrisonModule.KEY_CACHE_MODE, 0, false);
   Zone.SetFactionSetting(0, "US", false);
   Zone.SetFactionSetting(1, "", false);
   Check(Zone.ResolveFaction(0) == "US", "faction US");
   RunIndex = 0;
   Generate(SeedOf(RunIndex), EXPG_RGRules.BUCKET_FIRETEAM | EXPG_RGRules.BUCKET_SQUAD);
   Advance(2);
   return;
  }
  // Fire teams and squads, one seed after the other.
  if (Phase == 2)
  {
   if (Zone.State() == EXPG_RandomGarrisonModule.STATE_STOPPED)
   {
    Check(false, "the generation stopped: " + Zone.GetStatus());
    Finish("stopped");
    return;
   }
   if (Zone.State() != EXPG_RandomGarrisonModule.STATE_DONE)
   {
    Waited(PHASE_SECONDS, "generation done; status " + Zone.GetStatus());
    return;
   }
   array<IEntity> structures = {};
   int buildings = CheckDone(string.Format("seed %1", SeedOf(RunIndex)), structures);
   Seeds++;
   if (Check(buildings == TARGET, string.Format("seed %1: %2 buildings garrisoned (expected %3); status %4", SeedOf(RunIndex), buildings, TARGET, Zone.GetStatus()))) { Reached++; }
   Check(Zone.GetStatus().StartsWith("Done: 4 buildings"), "the status starts with Done: 4 buildings");
   if (RunIndex == 0) { FirstBuildings.Copy(structures); }
   Check(Zone.Run(EXPG_RandomGarrisonModule.ACTION_CLEAR, -1), "Clear accepted");
   Advance(3);
   return;
  }
  if (Phase == 3)
  {
   bool idle = Zone.State() == EXPG_RandomGarrisonModule.STATE_IDLE && Zone.RetireCount() == 0 && EXPG_RandomGarrisonDirector.JanitorCount() == 0;
   if (!idle)
   {
    Waited(60, "Clear finished; status " + Zone.GetStatus());
    return;
   }
   RunIndex++;
   if (RunIndex < SEED_COUNT)
   {
    Generate(SeedOf(RunIndex), EXPG_RGRules.BUCKET_FIRETEAM | EXPG_RGRules.BUCKET_SQUAD);
    Advance(2);
    return;
   }
   Generate(REPEAT_SEED, EXPG_RGRules.BUCKET_FIRETEAM | EXPG_RGRules.BUCKET_SQUAD);
   Advance(4);
   return;
  }
  // The first seed again: the same buildings in the same order.
  if (Phase == 4)
  {
   if (Zone.State() != EXPG_RandomGarrisonModule.STATE_DONE)
   {
    if (Zone.State() == EXPG_RandomGarrisonModule.STATE_STOPPED) { Check(false, "the repeat stopped: " + Zone.GetStatus()); Finish("stopped"); return; }
    Waited(PHASE_SECONDS, "repeat done; status " + Zone.GetStatus());
    return;
   }
   array<IEntity> again = {};
   CheckDone("repeat", again);
   Repeated = 1;
   if (again.Count() != FirstBuildings.Count()) { Repeated = 0; }
   for (int k = 0; k < again.Count() && k < FirstBuildings.Count(); k++)
   {
    if (again[k] != FirstBuildings[k]) { Repeated = 0; }
   }
   Check(Repeated == 1, "the same seed repeats its buildings");
   Check(Zone.Run(EXPG_RandomGarrisonModule.ACTION_CLEAR, -1), "Clear accepted");
   Advance(5);
   return;
  }
  if (Phase == 5)
  {
   bool cleared = Zone.State() == EXPG_RandomGarrisonModule.STATE_IDLE && Zone.RetireCount() == 0 && EXPG_RandomGarrisonDirector.JanitorCount() == 0;
   if (!cleared)
   {
    Waited(60, "Clear finished; status " + Zone.GetStatus());
    return;
   }
   Generate(SQUADS_SEED, EXPG_RGRules.BUCKET_SQUAD);
   Advance(6);
   return;
  }
  // Squads only: the target, or the reason it was missed.
  if (Phase == 6)
  {
   if (Zone.State() != EXPG_RandomGarrisonModule.STATE_DONE)
   {
    if (Zone.State() == EXPG_RandomGarrisonModule.STATE_STOPPED) { Check(false, "the squads-only generation stopped: " + Zone.GetStatus()); Finish("stopped"); return; }
    Waited(PHASE_SECONDS, "squads-only generation done; status " + Zone.GetStatus());
    return;
   }
   array<IEntity> squadsOnly = {};
   int placed = CheckDone("squads only", squadsOnly);
   string status = Zone.GetStatus();
   bool full = placed == TARGET && status.StartsWith("Done: 4 buildings");
   bool missed = placed < TARGET && status.StartsWith(string.Format("Done: %1 of 4 buildings", placed)) && status.Contains("tried ") && (status.Contains("none left") || status.Contains("tries reached"));
   if (Check(full || missed, string.Format("squads only: %1 buildings, and the status explains a missed target: %2", placed, status))) { Explained = 1; }
   Finish("completed");
  }
 }
}
