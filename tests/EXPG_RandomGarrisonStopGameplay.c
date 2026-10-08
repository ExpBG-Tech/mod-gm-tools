// TEST ONLY. Random Garrison stops and refusals (addon/random-garrison).
// pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -FixturePath tests/EXPG_RandomGarrisonStopGameplay.c -ExpectResult '\[EXPG RANDOM STOP RESULT\] checks=[1-9]\d* failures=0 busyRun=1 busyClear=1 stopTold=1 stopSelf=1 aiStopped=1 aiStayed=1 reason=completed' -TimeoutSeconds 600 -OrchestratorSlotGranted
// The runner copies this file to EXPG_GarrisonGameplay.c; the class names are fixed.
// The GM_Eden town centre 4773 169 7094 of tests/EXPG_RandomGarrisonGameplay.c, no
// player, radius 150, 4 buildings, one squad per building, fire teams and squads,
// seed 101, faction US, cache Off. Every notice the zone gives (Tell) is recorded.
//  1. Generate, then Generate again at once: refused with "Busy (...): wait for Done
//     or use Stop first". Stop by another Game Master (player 7): the starter and
//     player 7 are each told "Stopped (stopped by the Game Master)", and the zone is
//     Stopped. Clear, then Generate at once: refused with "Busy (Clearing): wait until
//     it has finished".
//  2. Generate, Stop by the starter: one notice only.
//  3. The AI limit set to the active AI count + 1 (no squad has room), Generate: after
//     the zone's 60 s wait it is Stopped with "Stopped (the AI limit was reached" in
//     the status and in the one notice it gave; 3 s later it is still Stopped (never
//     Done on top of it) with no second notice. The limit is restored.
// No GM UI, no save/load, no multiplayer.
modded class EXPG_RandomGarrisonModule
{
 ref array<int> m_aEXPGTestToldTo = {};
 ref array<string> m_aEXPGTestTold = {};

 override protected void Tell(int playerId, string message)
 {
  if (m_aEXPGTestTold.Count() < 64)
  {
   m_aEXPGTestToldTo.Insert(playerId);
   m_aEXPGTestTold.Insert(message);
  }
  super.Tell(playerId, message);
 }

 int EXPG_TestToldCount()
 {
  return m_aEXPGTestTold.Count();
 }

 int EXPG_TestToldTo(int index)
 {
  return m_aEXPGTestToldTo[index];
 }

 string EXPG_TestTold(int index)
 {
  return m_aEXPGTestTold[index];
 }

 void EXPG_TestForget()
 {
  m_aEXPGTestToldTo.Clear();
  m_aEXPGTestTold.Clear();
 }
}

class EXPG_GarrisonGameplayClass : GenericEntityClass {}
class EXPG_GarrisonGameplay : GenericEntity
{
 static const float FIXTURE_SECONDS = 570;
 static const float PHASE_SECONDS = 240;
 static const ResourceName ZONE_PREFAB = "{1B71589F70C61F8E}PrefabsEditable/EXPGR/EXPG_RandomGarrison.et";
 static const int TARGET = 4;
 static const int SEED = 101;
 static const int STARTER = -1;
 static const int OTHER_GM = 7;
 static const string STOPPED_BY_GM = "Stopped (stopped by the Game Master)";
 static const string STOPPED_BY_LIMIT = "Stopped (the AI limit was reached";
 vector Center = "4773 0 7094";
 EXPG_RandomGarrisonModule Zone;
 AIWorld WorldAI;
 int Phase;
 int Checks;
 int Failures;
 int BusyRun;
 int BusyClear;
 int StopTold;
 int StopSelf;
 int AIStopped;
 int AIStayed;
 int OriginalLimit = -1;
 float Started;
 float Next;
 float PhaseStarted;
 bool Finished;

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
  PrintFormat("[EXPG RANDOM STOP BEGIN] center=%1 radius=150 target=%2 seed=%3 starter=%4 otherGm=%5 deadline=%6", Center, TARGET, SEED, STARTER, OTHER_GM, FIXTURE_SECONDS);
 }

 bool Check(bool value, string label)
 {
  Checks++;
  if (!value) { Failures++; }
  PrintFormat("[EXPG RANDOM STOP CHECK] pass=%1 %2", value, label);
  return value;
 }

 // The AI limit this fixture changed goes back, whatever the outcome.
 void RestoreLimit()
 {
  if (WorldAI && OriginalLimit >= 0)
  {
   WorldAI.SetLimitOfActiveAIs(OriginalLimit);
   PrintFormat("[EXPG RANDOM STOP LIMIT] restored=%1", OriginalLimit);
  }
  OriginalLimit = -1;
 }

 void Finish(string reason)
 {
  if (Finished) { return; }
  Finished = true;
  ClearEventMask(EntityEvent.FRAME);
  RestoreLimit();
  string first = string.Format("checks=%1 failures=%2 busyRun=%3 busyClear=%4", Checks, Failures, BusyRun, BusyClear);
  string second = string.Format("stopTold=%1 stopSelf=%2 aiStopped=%3 aiStayed=%4 reason=%5", StopTold, StopSelf, AIStopped, AIStayed, reason);
  PrintFormat("[EXPG RANDOM STOP RESULT] %1 %2", first, second);
  GetGame().RequestClose();
 }

 void Advance(int phase)
 {
  Phase = phase;
  PhaseStarted = Now();
  string status;
  int noticed;
  if (Zone)
  {
   status = Zone.GetStatus();
   noticed = Zone.EXPG_TestToldCount();
  }
  PrintFormat("[EXPG RANDOM STOP PHASE] phase=%1 elapsed=%2 notices=%3 status=%4", Phase, Now() - Started, noticed, status);
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

 bool Idle()
 {
  return Zone.State() == EXPG_RandomGarrisonModule.STATE_IDLE && Zone.RetireCount() == 0 && EXPG_RandomGarrisonDirector.JanitorCount() == 0;
 }

 string LastTold()
 {
  int count = Zone.EXPG_TestToldCount();
  if (count == 0)
  {
   return string.Empty;
  }
  return Zone.EXPG_TestTold(count - 1);
 }

 void Generate()
 {
  Check(Zone.Run(EXPG_RandomGarrisonModule.ACTION_GENERATE, STARTER), "Generate accepted");
 }

 override void EOnFrame(IEntity owner, float timeSlice)
 {
  if (Finished || Now() < Next) { return; }
  Next = Now() + 0.5;
  if (Now() - Started > FIXTURE_SECONDS)
  {
   Check(false, string.Format("%1 second deadline; last phase %2", FIXTURE_SECONDS, Phase));
   Finish("timeout");
   return;
  }
  if (Phase == 0)
  {
   if (!Check(EXPG_GarrisonManager.Get() != null, "garrison manager on the server")) { Finish("manager"); return; }
   WorldAI = GetGame().GetAIWorld();
   if (!Check(WorldAI != null, "AI world")) { Finish("ai world"); return; }
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
  // Refusals while busy, and a Stop by another Game Master.
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
   Zone.ApplySetting(EXPG_RandomGarrisonModule.KEY_SEED, SEED, false);
   Zone.ApplySetting(EXPG_RandomGarrisonModule.KEY_SIZES, EXPG_RGRules.BUCKET_FIRETEAM | EXPG_RGRules.BUCKET_SQUAD, false);
   Zone.SetFactionSetting(0, "US", false);
   Zone.SetFactionSetting(1, "", false);
   Check(Zone.ResolveFaction(0) == "US", "faction US");
   Zone.EXPG_TestForget();
   Generate();
   Check(!Zone.Run(EXPG_RandomGarrisonModule.ACTION_GENERATE, STARTER), "a second Generate during the run is refused");
   string busy = LastTold();
   if (Check(busy.StartsWith("Busy (") && busy.EndsWith("): wait for Done or use Stop first"), "the refusal says how far the zone is, and to wait or Stop: " + busy)) { BusyRun = 1; }
   Zone.EXPG_TestForget();
   Check(Zone.Run(EXPG_RandomGarrisonModule.ACTION_STOP, OTHER_GM), "Stop by another Game Master accepted");
   int told = Zone.EXPG_TestToldCount();
   bool both = told == 2 && Zone.EXPG_TestToldTo(0) == STARTER && Zone.EXPG_TestToldTo(1) == OTHER_GM && Zone.EXPG_TestTold(0).StartsWith(STOPPED_BY_GM) && Zone.EXPG_TestTold(1) == Zone.EXPG_TestTold(0);
   if (Check(Zone.State() == EXPG_RandomGarrisonModule.STATE_STOPPED && both, string.Format("the starter and the Game Master who stopped are each told (%1 notices): %2", told, LastTold()))) { StopTold = 1; }
   Check(Zone.Run(EXPG_RandomGarrisonModule.ACTION_CLEAR, STARTER), "Clear accepted");
   Zone.EXPG_TestForget();
   Check(!Zone.Run(EXPG_RandomGarrisonModule.ACTION_GENERATE, STARTER), "Generate during the clear is refused");
   string clearing = LastTold();
   if (Check(clearing.StartsWith("Busy (Clearing") && clearing.EndsWith("): wait until it has finished"), "the refusal during a clear says to wait, not to Stop: " + clearing)) { BusyClear = 1; }
   Advance(2);
   return;
  }
  // A Stop by the starter: one notice.
  if (Phase == 2)
  {
   if (!Idle())
   {
    Waited(60, "Clear finished; status " + Zone.GetStatus());
    return;
   }
   Zone.EXPG_TestForget();
   Generate();
   Check(Zone.Run(EXPG_RandomGarrisonModule.ACTION_STOP, STARTER), "Stop by the starter accepted");
   int notices = Zone.EXPG_TestToldCount();
   bool single = notices == 1 && Zone.EXPG_TestToldTo(0) == STARTER && Zone.EXPG_TestTold(0).StartsWith(STOPPED_BY_GM);
   if (Check(Zone.State() == EXPG_RandomGarrisonModule.STATE_STOPPED && single, string.Format("a Stop by the starter gives one notice (%1): %2", notices, LastTold()))) { StopSelf = 1; }
   Check(Zone.Run(EXPG_RandomGarrisonModule.ACTION_CLEAR, STARTER), "Clear accepted");
   Advance(3);
   return;
  }
  // No squad has room under the AI limit.
  if (Phase == 3)
  {
   if (!Idle())
   {
    Waited(60, "Clear finished; status " + Zone.GetStatus());
    return;
   }
   int active = WorldAI.GetCurrentNumOfActiveAIs();
   OriginalLimit = WorldAI.GetLimitOfActiveAIs();
   WorldAI.SetLimitOfActiveAIs(active + 1);
   Check(WorldAI.GetLimitOfActiveAIs() == active + 1, string.Format("AI limit set to %1 (active %2, was %3)", active + 1, active, OriginalLimit));
   Zone.EXPG_TestForget();
   Generate();
   Advance(4);
   return;
  }
  if (Phase == 4)
  {
   int state = Zone.State();
   if (state == EXPG_RandomGarrisonModule.STATE_DONE)
   {
    Check(false, string.Format("the AI-limit stop must not end as Done (%1 notices): %2", Zone.EXPG_TestToldCount(), Zone.GetStatus()));
    Finish("done");
    return;
   }
   if (state != EXPG_RandomGarrisonModule.STATE_STOPPED)
   {
    Waited(PHASE_SECONDS, "AI-limit stop; status " + Zone.GetStatus());
    return;
   }
   string status = Zone.GetStatus();
   bool head = status.Contains(STOPPED_BY_LIMIT);
   bool once = Zone.EXPG_TestToldCount() == 1 && Zone.EXPG_TestTold(0).StartsWith(STOPPED_BY_LIMIT);
   Check(head, "the status keeps the AI-limit reason: " + status);
   Check(once, string.Format("one notice, with the AI-limit reason (%1): %2", Zone.EXPG_TestToldCount(), LastTold()));
   if (head && once) { AIStopped = 1; }
   Advance(5);
   return;
  }
  if (Phase == 5)
  {
   if (Now() - PhaseStarted < 3) { return; }
   bool stayed = Zone.State() == EXPG_RandomGarrisonModule.STATE_STOPPED && Zone.EXPG_TestToldCount() == 1 && Zone.GetStatus().Contains(STOPPED_BY_LIMIT);
   if (Check(stayed, string.Format("still Stopped with its reason, no second notice (%1): %2", Zone.EXPG_TestToldCount(), Zone.GetStatus()))) { AIStayed = 1; }
   RestoreLimit();
   Check(Zone.Run(EXPG_RandomGarrisonModule.ACTION_CLEAR, STARTER), "Clear accepted");
   Advance(6);
   return;
  }
  if (Phase == 6)
  {
   if (!Idle())
   {
    Waited(60, "Clear finished; status " + Zone.GetStatus());
    return;
   }
   Finish("completed");
  }
 }
}
