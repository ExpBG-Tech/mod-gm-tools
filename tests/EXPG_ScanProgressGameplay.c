// TEST ONLY. Structure analysis progress and the deferred squad picker. Live report
// on 0.1.8: on House_Town_E_2I02 the first EXPBG Add Garrison squad choice was
// refused three times ("Structure analysis is still running") during a 90 s
// analysis, with no feedback. The picker now opens only when the plan is ready;
// until then the server reports the analysis progress.
// pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -FixturePath tests/EXPG_ScanProgressGameplay.c -ExpectResult '\[EXPG SCAN RESULT\] checks=[1-9]\d* failures=0 samples=[1-9]\d* decreases=0 plans=1 joined=1 readyA=1 readyB=1 queuedB=1 readyD=1 cancelled=0 cachedReady=1 cachedProgress=0 failedF=1 reason=completed' -TimeoutSeconds 420 -OrchestratorSlotGranted
// The runner copies this file to EXPG_GarrisonGameplay.c; the class names are fixed.
// Spawns the vanilla House_Town_E_2I01 on the generated GM_Eden world and drives the
// production server seam the editor uses (EXPG_GarrisonManager.Wait with a waiter;
// the editor's waiter turns these events into owner RPCs: progress hint, picker).
// A: first request. B: a second Game Master on the same building, whose picker is
// held by another Game Master twice when the plan is ready (queued). C: cancelled
// at once. D: joins when the analysis is past 30 %. Checks: one plan for the
// building, the plan's progress and every reported percentage never decrease and
// reach 100, OnReady fires once per request (B: twice refused, then once
// accepted), C hears nothing, D starts at the running percentage (no restart),
// a request after the analysis (E) is ready at once without progress, and a
// request whose building is deleted (F) fails once with a reason.
// No players, no GM UI, no squads.
class EXPG_ScanWaiter : EXPG_PlanWaiter
{
 string Who;
 int Refuse;
 int ProgressCalls;
 int QueuedReports;
 int ReadyCalls;
 int Accepted;
 int FailedCalls;
 int FirstPercent = -1;
 int LastPercent = -1;
 int Decreases;
 string FailReason;

 override void OnProgress(int percent, bool queued)
 {
  ProgressCalls++;
  if (queued) QueuedReports++;
  if (FirstPercent < 0) FirstPercent = percent;
  if (percent < LastPercent) Decreases++;
  LastPercent = percent;
  PrintFormat("[EXPG SCAN PROGRESS] waiter=%1 percent=%2 queued=%3", Who, percent, queued);
 }

 override bool OnReady()
 {
  ReadyCalls++;
  PrintFormat("[EXPG SCAN READY] waiter=%1 call=%2 refuse=%3", Who, ReadyCalls, Refuse);
  if (ReadyCalls <= Refuse)
   return false;
  Accepted++;
  return true;
 }

 override void OnFailed(string reason)
 {
  FailedCalls++;
  FailReason = reason;
  PrintFormat("[EXPG SCAN FAILED] waiter=%1 reason=%2", Who, reason);
 }
}

modded class EXPG_GarrisonManager
{
 int EXPG_ScanPlansFor(IEntity building)
 {
  int count;
  foreach (EXPG_BuildingPlan plan : m_Plans)
  {
   if (plan.Structure == building) count++;
  }
  return count;
 }
}

class EXPG_GarrisonGameplayClass : GenericEntityClass {}
class EXPG_GarrisonGameplay : GenericEntity
{
 static const float FIXTURE_SECONDS = 300;
 IEntity Structure;
 IEntity Doomed;
 EXPG_GarrisonManager Manager;
 EXPG_BuildingPlan Plan;
 ref EXPG_ScanWaiter WaiterA;
 ref EXPG_ScanWaiter WaiterB;
 ref EXPG_ScanWaiter WaiterC;
 ref EXPG_ScanWaiter WaiterD;
 ref EXPG_ScanWaiter WaiterE;
 ref EXPG_ScanWaiter WaiterF;
 ObserversSystem Observers;
 int ObserverKey;
 int Phase;
 int Checks;
 int Failures;
 int Samples;
 int SampleDecreases;
 int PlansFor;
 int Joined;
 float LastSample = -1;
 float Started;
 float Next;
 float PhaseStarted;
 float AnalysisStarted;
 bool Finished;
 vector Point = "4773.46 0 7094.57";
 vector DoomedPoint = "4813.46 0 7094.57";

 void EXPG_GarrisonGameplay(IEntitySource src, IEntity parent) { SetEventMask(EntityEvent.INIT | EntityEvent.FRAME); }
 float Now() { return GetGame().GetWorld().GetWorldTime() * 0.001; }
 override void EOnInit(IEntity owner)
 {
  if (!Replication.IsServer()) { ClearEventMask(EntityEvent.FRAME); return; }
  Started = Now();
  Next = Started + 15;
  ObserverKey = "EXPG_ScanProgress.Observer".Hash();
  PrintFormat("[EXPG SCAN BEGIN] deadline=%1 connectedPlayer=0", FIXTURE_SECONDS);
 }
 bool Check(bool value, string label)
 {
  Checks++;
  if (!value) Failures++;
  PrintFormat("[EXPG SCAN CHECK] pass=%1 %2", value, label);
  return value;
 }
 void RemoveObserver()
 {
  if (Observers && GetGame() && GetGame().GetWorld() && GetGame().GetWorld().FindSystem(ObserversSystem) == Observers)
   Observers.RemoveObserverSP(ObserverKey);
 }
 void ~EXPG_GarrisonGameplay() { RemoveObserver(); }
 int ReadyOf(EXPG_ScanWaiter waiter)
 {
  if (!waiter)
   return -1;
  return waiter.Accepted;
 }
 void Finish(string reason)
 {
  if (Finished) return;
  Finished = true;
  RemoveObserver();
  ClearEventMask(EntityEvent.FRAME);
  int queuedB = 0;
  if (WaiterB && WaiterB.QueuedReports > 0 && WaiterB.ReadyCalls == 3) queuedB = 1;
  int cancelled = -1;
  if (WaiterC) cancelled = WaiterC.ProgressCalls + WaiterC.ReadyCalls + WaiterC.FailedCalls;
  int cachedProgress = -1;
  if (WaiterE) cachedProgress = WaiterE.ProgressCalls;
  int failedF = -1;
  if (WaiterF && WaiterF.ReadyCalls == 0) failedF = WaiterF.FailedCalls;
  string first = string.Format("checks=%1 failures=%2 samples=%3 decreases=%4 plans=%5 joined=%6", Checks, Failures, Samples, SampleDecreases, PlansFor, Joined);
  string second = string.Format("readyA=%1 readyB=%2 queuedB=%3 readyD=%4 cancelled=%5 cachedReady=%6", ReadyOf(WaiterA), ReadyOf(WaiterB), queuedB, ReadyOf(WaiterD), cancelled, ReadyOf(WaiterE));
  string third = string.Format("cachedProgress=%1 failedF=%2 reason=%3", cachedProgress, failedF, reason);
  PrintFormat("[EXPG SCAN RESULT] %1 %2 %3", first, second, third);
  GetGame().RequestClose();
 }
 void Advance(int phase)
 {
  Phase = phase;
  PhaseStarted = Now();
  PrintFormat("[EXPG SCAN PHASE] phase=%1 elapsed=%2", Phase, Now() - Started);
 }
 EntitySpawnParams Params(vector point)
 {
  EntitySpawnParams spawn = new EntitySpawnParams();
  spawn.TransformMode = ETransformMode.WORLD;
  Math3D.AnglesToMatrix(vector.Zero, spawn.Transform);
  point[1] = GetGame().GetWorld().GetSurfaceY(point[0], point[2]);
  spawn.Transform[3] = point;
  return spawn;
 }
 IEntity SpawnHouse(vector point)
 {
  ResourceName house = "{38A5F3E4578087AB}Prefabs/Structures/Houses/Town/House_Town_E_2I01/House_Town_E_2I01.et";
  Resource houseResource = Resource.Load(house);
  return GetGame().SpawnEntityPrefab(houseResource, GetGame().GetWorld(), Params(point));
 }
 EXPG_ScanWaiter NewWaiter(string who, IEntity building, int refuse = 0)
 {
  EXPG_ScanWaiter waiter = new EXPG_ScanWaiter();
  waiter.Who = who;
  waiter.Structure = building;
  waiter.Refuse = refuse;
  return waiter;
 }
 bool Join(EXPG_ScanWaiter waiter)
 {
  string reason;
  bool waiting = Manager.Wait(waiter, reason);
  return Check(waiting && reason.IsEmpty(), string.Format("request %1 accepted by Wait: %2", waiter.Who, reason));
 }
 // The plan's own progress, sampled every half second, never decreases.
 void Sample()
 {
  float progress = Plan.Progress();
  Samples++;
  if (progress + 0.0001 < LastSample)
  {
   SampleDecreases++;
   PrintFormat("[EXPG SCAN DECREASE] from=%1 to=%2", LastSample, progress);
  }
  LastSample = progress;
 }
 override void EOnFrame(IEntity owner, float timeSlice)
 {
  if (Finished || Now() < Next) return;
  Next = Now() + 0.5;
  if (Now() - Started > FIXTURE_SECONDS) { Check(false, string.Format("%1 second deadline; last phase %2", FIXTURE_SECONDS, Phase)); Finish("timeout"); return; }
  if (Phase == 0)
  {
   array<int> players = {};
   GetGame().GetPlayerManager().GetPlayers(players);
   Manager = EXPG_GarrisonManager.Get();
   Observers = ObserversSystem.Cast(GetGame().GetWorld().FindSystem(ObserversSystem));
   if (!Check(Manager && Observers && players.IsEmpty(), "isolated server with no connected players")) { Finish("setup"); return; }
   Observers.InsertObserverSP(ObserverKey, Point[0], Point[2], null);
   Structure = SpawnHouse(Point);
   if (!Check(SCR_DestructibleBuildingEntity.Cast(Structure) != null, "vanilla two-storey town house spawned")) { Finish("building"); return; }
   AnalysisStarted = Now();
   WaiterA = NewWaiter("A", Structure);
   if (!Join(WaiterA)) { Finish("wait"); return; }
   Plan = Manager.FindPlan(Structure);
   if (!Check(Plan && WaiterA.Plan == Plan && !Plan.Done && WaiterA.ReadyCalls == 0, "first request started the analysis and waits (no picker yet)")) { Finish("plan"); return; }
   Check(WaiterA.ProgressCalls == 1 && WaiterA.LastPercent >= 0 && WaiterA.LastPercent < 100, string.Format("first request got its first progress at once: %1 percent", WaiterA.LastPercent));
   // A second Game Master on the same building joins the same plan; the picker is
   // held by someone else the first two times the plan is ready.
   WaiterB = NewWaiter("B", Structure, 2);
   if (!Join(WaiterB)) { Finish("wait"); return; }
   WaiterC = NewWaiter("C", Structure);
   if (!Join(WaiterC)) { Finish("wait"); return; }
   int heardC = WaiterC.ProgressCalls;
   Manager.StopWaiting(WaiterC);
   WaiterC.ProgressCalls = WaiterC.ProgressCalls - heardC;
   PlansFor = Manager.EXPG_ScanPlansFor(Structure);
   Check(WaiterB.Plan == Plan && Manager.Prepare(Structure) == Plan && PlansFor == 1, string.Format("second request joined the running analysis: plans for the building %1", PlansFor));
   Check(Manager.WaiterCount(Structure) == 2, string.Format("two requests wait after one cancel: %1", Manager.WaiterCount(Structure)));
   Sample();
   Advance(1);
   return;
  }
  if (Phase == 1)
  {
   Sample();
   if (!WaiterD && !Plan.Done && Plan.Progress() >= 0.3)
   {
    float before = Plan.Progress();
    WaiterD = NewWaiter("D", Structure);
    if (!Join(WaiterD)) { Finish("wait"); return; }
    if (WaiterD.Plan == Plan && WaiterD.FirstPercent >= 29 && Manager.EXPG_ScanPlansFor(Structure) == 1) Joined = 1;
    Check(Joined == 1, string.Format("late request joined at the running progress: plan progress %1, first reported %2 percent", before, WaiterD.FirstPercent));
   }
   if (!Plan.Done)
   {
    if (Now() - PhaseStarted > 200) { Check(false, "production analysis finished within 200 seconds"); Finish("analysis"); }
    return;
   }
   PrintFormat("[EXPG SCAN ANALYSIS] seconds=%1 nodes=%2 slots=%3 error=%4", Now() - AnalysisStarted, Plan.Nodes.Count(), Plan.Slots.Count(), Plan.Error);
   Check(Plan.Error.IsEmpty() && !Plan.Slots.IsEmpty() && Plan.Progress() == 1, "analysis finished with slots and progress 1: " + Plan.Error);
   Check(Manager.EXPG_ScanPlansFor(Structure) == 1, "still one plan for the building");
   Advance(2);
   return;
  }
  if (Phase == 2)
  {
   // Let the pump run on: no request may hear OnReady a second time.
   if (Now() - PhaseStarted < 3) return;
   Check(WaiterA.Accepted == 1 && WaiterA.ReadyCalls == 1 && WaiterA.FailedCalls == 0, string.Format("A: picker event exactly once: %1", WaiterA.ReadyCalls));
   Check(WaiterA.Decreases == 0 && WaiterA.LastPercent == 100 && WaiterA.ProgressCalls >= 2, string.Format("A: progress never decreased and reached 100 percent: last %1, reports %2", WaiterA.LastPercent, WaiterA.ProgressCalls));
   Check(WaiterB.ReadyCalls == 3 && WaiterB.Accepted == 1 && WaiterB.QueuedReports >= 1 && WaiterB.FailedCalls == 0, string.Format("B: queued behind another picker, then exactly one picker event: calls %1, queued reports %2", WaiterB.ReadyCalls, WaiterB.QueuedReports));
   Check(WaiterD && WaiterD.Accepted == 1 && WaiterD.ReadyCalls == 1 && WaiterD.Decreases == 0, "D: one picker event, progress never decreased");
   Check(WaiterC.ProgressCalls + WaiterC.ReadyCalls + WaiterC.FailedCalls == 0, "C: nothing after its cancel");
   Check(Manager.WaiterCount(Structure) == 0, "no request left waiting");
   // An analysed building opens at once and silently (no progress hint).
   WaiterE = NewWaiter("E", Structure);
   if (!Join(WaiterE)) { Finish("wait"); return; }
   Check(WaiterE.Accepted == 1 && WaiterE.ProgressCalls == 0, string.Format("E: cached plan ready inside Wait without progress: ready %1, progress %2", WaiterE.Accepted, WaiterE.ProgressCalls));
   Doomed = SpawnHouse(DoomedPoint);
   if (!Check(Doomed != null, "second house spawned for the failure case")) { Finish("building"); return; }
   WaiterF = NewWaiter("F", Doomed);
   if (!Join(WaiterF)) { Finish("wait"); return; }
   Advance(3);
   return;
  }
  if (Phase == 3)
  {
   if (Now() - PhaseStarted < 1) return;
   SCR_EntityHelper.DeleteEntityAndChildren(Doomed);
   Advance(4);
   return;
  }
  if (Phase == 4)
  {
   if (WaiterF.FailedCalls == 0 && Now() - PhaseStarted < 5) return;
   Check(WaiterF.FailedCalls == 1 && WaiterF.ReadyCalls == 0 && !WaiterF.FailReason.IsEmpty(), string.Format("F: deleted building fails once with a reason: %1", WaiterF.FailReason));
   Finish("completed");
  }
 }
}
