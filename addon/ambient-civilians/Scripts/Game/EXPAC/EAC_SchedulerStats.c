// Scheduler cost ledger. Every number here is MEASURED with System.GetTickCount()
// around a real call site in EAC_AmbientModule.EOnFrame or EAC_PedestrianSpawner;
// nothing in this file estimates, models or predicts anything. It exists because
// the 2026-09-17 idle-and-loop audit had to argue about per-tick cost from source
// reading alone, and because "almost zero fps impact" is a claim that needs a
// number attached to it.
//
// It writes no console line of its own: it only builds strings for callers that
// are already DebugLevel-gated (EAC_AmbientModule.BuildDebugSummary and the QA
// fixture summary). Nothing here is ever consulted for admission.
//
// Statics reset per world through CheckWorld(), called from
// EAC_AmbientModule.EOnInit beside EAC_SceneIndex.CheckWorld().
class EAC_SchedulerStats
{
 // Sub-steps of the shared 0.5 s scheduler tick, in the order EOnFrame runs them.
 // STEP_FRAME is the whole tick and is the figure the headline reports.
 static const int STEP_FRAME = 0;
 static const int STEP_INDEX = 1;    // theme, isolation policy, reconcile, player discovery
 static const int STEP_PREWARM = 2;  // EAC_HomeIndex.StepPrewarm
 static const int STEP_SCENE = 3;    // EAC_SceneIndex.Step
 static const int STEP_MONITOR = 4;  // EAC_PedestrianSpawner.MonitorOrders
 static const int STEP_SERVICE = 5;  // walk/routine maintenance batch
 static const int STEP_MAINTAIN = 6; // cache rotation, rollback, release
 static const int STEP_PENDING = 7;  // the pending activation ladder
 static const int STEP_ADMIT = 8;    // EAC_PedestrianSpawner.SelectNext
 static const int STEP_TRAFFIC = 9;  // EAC_TrafficDirector.Step
 static const int STEP_VOICE = 10;   // EAC_CivilianVoice.Step
 static const int STEP_HORN = 11;    // the per-frame native horn input read
 // Appended rather than inserted in run order: every recorded campaign log reads
 // these ordinals, and renumbering them would silently re-label eight campaigns'
 // worth of `sched_steps`. STEP_FORCE is the idle watchdog, measured apart from
 // the ordinary maintenance batch it used to be folded into.
 static const int STEP_FORCE = 12;   // EAC_PedestrianSpawner.ForceIdle
 static const int STEPS = 13;
 // System.GetTickCount() is integer milliseconds, so one bucket per millisecond
 // to 30 plus one overflow bucket is an EXACT histogram of everything this
 // scheduler is allowed to cost, and answers a percentile without sorting or
 // retaining samples. A tick that reaches the overflow bucket is already a
 // defect, and s_Max keeps its true value regardless.
 static const int BUCKETS = 32;
 static const int LIMIT = 1000000;

 protected static BaseWorld s_World;
 protected static ref array<int> s_Samples = {};
 protected static ref array<int> s_Total;
 protected static ref array<int> s_Max;
 protected static ref array<int> s_Hist;
 // Actual actor/group births and removals per tick. One native full-cache
 // transaction removes its actor and empty group together (two entities).
 protected static int s_Spawns, s_Despawns, s_TickSpawns, s_TickDespawns, s_PeakTickSpawns, s_PeakTickDespawns;
 // The traffic director's own creations, counted apart from the pedestrian ladder
 // because the two are separate one-slot rotations: the pedestrian pending ladder
 // and the traffic party cursor can each legitimately create one entity on the
 // same tick, so a single peak of two would say nothing about either.
 protected static int s_TrafficSpawns, s_TickTrafficSpawns, s_PeakTickTrafficSpawns;
 protected static int s_CreationRollbacks, s_RemovalsDeferred;
 // Idle watchdog (deliverable 1). Forced routine starts, forced wander legs, the
 // attempts that produced neither, the attempts that arrived with the shared
 // start token already spent, and the last-resort emergence re-arms.
 protected static int s_ForceAttempts, s_ForcedStarts, s_ForcedWalks, s_ForcedNone;
 protected static int s_ForcedDeferred, s_ForcedRearms;
 protected static float s_ForcedPeakIdle;
 // Why a forced intervention produced nothing. City benchmark 06 read
 // "attempts=725 starts=35 walks=15 refused=675" and could not say which gate
 // did the refusing, so the fix could not be aimed. One bucket per refusing gate,
 // in the order ForceOne consults them.
 static const int GATE_ACTIVITY_LIMIT = 0; // every routine slot is occupied
 static const int GATE_ACTIVITY_OTHER = 1; // cooldown, relevance, alarm, emergence
 static const int GATE_START = 2;          // the routine began and the start refused
 static const int GATE_WALK = 3;           // the wander leg produced no waypoint
 static const int GATES = 4;
 protected static ref array<int> s_ForceGates;
 // Navmesh-projected positions accepted by the wider height tolerance that the
 // old 0.75 m terrain rule would have refused outright.
 protected static int s_SurfaceRecovered;

 static void CheckWorld()
 {
		EXPBG_LazyStatics_EAC_SchedulerStats();
  BaseWorld world;
  if (GetGame()) world = GetGame().GetWorld();
  if (world == s_World && s_Samples.Count() == STEPS && s_ForceGates.Count() == GATES) return;
  s_World = world;
  Reset();
 }

 static void Reset()
 {
		EXPBG_LazyStatics_EAC_SchedulerStats();
  s_ForceGates.Clear(); s_ForceGates.Resize(GATES);
  s_SurfaceRecovered = 0;
  s_Samples.Clear(); s_Samples.Resize(STEPS);
  s_Total.Clear(); s_Total.Resize(STEPS);
  s_Max.Clear(); s_Max.Resize(STEPS);
  s_Hist.Clear(); s_Hist.Resize(STEPS * BUCKETS);
  s_Spawns = 0; s_Despawns = 0;
  s_TickSpawns = 0; s_TickDespawns = 0;
  s_PeakTickSpawns = 0; s_PeakTickDespawns = 0;
  s_TrafficSpawns = 0; s_TickTrafficSpawns = 0; s_PeakTickTrafficSpawns = 0;
  s_CreationRollbacks = 0; s_RemovalsDeferred = 0;
  s_ForceAttempts = 0; s_ForcedStarts = 0; s_ForcedWalks = 0; s_ForcedNone = 0;
  s_ForcedDeferred = 0; s_ForcedRearms = 0;
  s_ForcedPeakIdle = 0;
 }

 // Two zero-argument reads and a subtraction, the same form MonitorOrders and the
 // prewarm timer already use, rather than the System.GetTickCount(prev) overload
 // whose subtraction order this project has never exercised.
 static void Record(int step, int startTick)
 {
		EXPBG_LazyStatics_EAC_SchedulerStats();
  if (step < 0 || step >= STEPS || s_Samples.Count() != STEPS) return;
  int spent = System.GetTickCount() - startTick;
  if (spent < 0) spent = 0;
  // Saturating the three accumulators independently was a silent decay: a long
  // session reached LIMIT samples while the total was still climbing (or the
  // other way round), and GetAverage = total / samples then drifted away from the
  // real average for the rest of the session. Rescale the whole step at once
  // instead, so the average, the percentile and the histogram all keep their
  // shape (audit item 9).
  if (s_Samples[step] + 1 >= LIMIT || s_Total[step] + spent >= LIMIT) Halve(step);
  s_Samples[step] = s_Samples[step] + 1;
  s_Total[step] = s_Total[step] + spent;
  if (spent > s_Max[step]) s_Max[step] = spent;
  int bucket = spent;
  if (bucket >= BUCKETS) bucket = BUCKETS - 1;
  int slot = step * BUCKETS + bucket;
  s_Hist[slot] = s_Hist[slot] + 1;
 }

 // One rescale of a step's entire record. The sample count is rebuilt from the
 // halved buckets rather than halved on its own, so the histogram keeps summing
 // to exactly the sample count and Percentile's target stays reachable.
 protected static void Halve(int step)
 {
		EXPBG_LazyStatics_EAC_SchedulerStats();
  if (step < 0 || step >= STEPS || s_Hist.Count() != STEPS * BUCKETS) return;
  int origin = step * BUCKETS;
  int samples;
  for (int i = 0; i < BUCKETS; i++)
  {
   s_Hist[origin + i] = s_Hist[origin + i] / 2;
   samples += s_Hist[origin + i];
  }
  s_Samples[step] = samples;
  s_Total[step] = s_Total[step] / 2;
 }

 // Closes the previous scheduler tick's spawn/despawn accounting and opens the
 // next one. Called once at the top of the 0.5 s gate in EOnFrame.
 // Also the flush: the peaks are only true once the tick that set them has been
 // folded, so a reader that stops at the last tick (the QA summary) calls this
 // once more before reading (audit item 10).
 static void BeginTick()
 {
  if (s_TickSpawns > s_PeakTickSpawns) s_PeakTickSpawns = s_TickSpawns;
  if (s_TickDespawns > s_PeakTickDespawns) s_PeakTickDespawns = s_TickDespawns;
  if (s_TickTrafficSpawns > s_PeakTickTrafficSpawns) s_PeakTickTrafficSpawns = s_TickTrafficSpawns;
  s_TickSpawns = 0; s_TickDespawns = 0; s_TickTrafficSpawns = 0;
 }

 // The pedestrian channel: the pending activation ladder and the cache wake, both
 // of which are single-slot rotations.
 static void RecordSpawn()
 {
  if (s_Spawns < LIMIT) s_Spawns++;
  if (s_TickSpawns < LIMIT) s_TickSpawns++;
 }

 // The traffic channel: a party's group, its car and each crew member, one per
 // advanced party per tick. Waypoints and helper points are NOT counted here -
 // they are not characters or vehicles and several of them can legitimately be
 // created around one resident.
 static void RecordTrafficSpawn()
 {
  if (s_TrafficSpawns < LIMIT) s_TrafficSpawns++;
  if (s_TickTrafficSpawns < LIMIT) s_TickTrafficSpawns++;
 }

 static void RecordDespawn()
 {
  if (s_Despawns < LIMIT) s_Despawns++;
  if (s_TickDespawns < LIMIT) s_TickDespawns++;
 }

 // Un-creating something this same tick created: a group or character whose
 // ownership registration was refused microseconds after it spawned. It is a real
 // deletion and belongs in the total, but it is not a REMOVAL of an established
 // entity, and it cannot be deferred - nothing owns the entity yet, so a deferral
 // would leak it. Counted apart so the per-tick removal peak keeps measuring the
 // steady-state bound: one ordinary deletion or one complete cache transaction.
 static void RecordCreationRollback()
 {
  if (s_Despawns < LIMIT) s_Despawns++;
  if (s_CreationRollbacks < LIMIT) s_CreationRollbacks++;
 }

 // A removal that was ready this tick and waited because its removal transaction
 // was already spent. The retained-cleanup rotation revisits it.
 static void RecordRemovalDeferred() { if (s_RemovalsDeferred < LIMIT) s_RemovalsDeferred++; }
 static int GetCreationRollbacks() { return s_CreationRollbacks; }
 static int GetRemovalsDeferred() { return s_RemovalsDeferred; }

 static void RecordForceAttempt(float idleFor)
 {
  if (s_ForceAttempts < LIMIT) s_ForceAttempts++;
  if (idleFor > s_ForcedPeakIdle) s_ForcedPeakIdle = idleFor;
 }

 static void RecordForcedStart() { if (s_ForcedStarts < LIMIT) s_ForcedStarts++; }
 static void RecordForcedWalk() { if (s_ForcedWalks < LIMIT) s_ForcedWalks++; }
 static void RecordForceRefused() { if (s_ForcedNone < LIMIT) s_ForcedNone++; }
 // The intervention arrived after this second's start token was already spent. It
 // still cleared the resident's cooldown and walk backoff, so it is not a refusal.
 static void RecordForceDeferred() { if (s_ForcedDeferred < LIMIT) s_ForcedDeferred++; }
 // The refusal AND the gate that caused it, so the two always agree.
 static void RecordForceRefusedAt(int gate)
 {
		EXPBG_LazyStatics_EAC_SchedulerStats();
  RecordForceRefused();
  if (gate < 0 || gate >= GATES || s_ForceGates.Count() != GATES) return;
  if (s_ForceGates[gate] < LIMIT) s_ForceGates[gate] = s_ForceGates[gate] + 1;
 }

 static int GetForceGate(int gate)
 {
		EXPBG_LazyStatics_EAC_SchedulerStats();
  if (gate < 0 || gate >= s_ForceGates.Count()) return 0;
  return s_ForceGates[gate];
 }

 static void RecordSurfaceRecovered() { if (s_SurfaceRecovered < LIMIT) s_SurfaceRecovered++; }
 static int GetSurfaceRecovered() { return s_SurfaceRecovered; }
 // The last resort: both ordinary interventions were refused and the resident is
 // standing inside its own house, so emergence is re-armed for the next sweep.
 static void RecordForcedRearm() { if (s_ForcedRearms < LIMIT) s_ForcedRearms++; }

 static int GetSamples(int step)
 {
  if (step < 0 || step >= s_Samples.Count()) return 0;
  return s_Samples[step];
 }

 static int GetMax(int step)
 {
		EXPBG_LazyStatics_EAC_SchedulerStats();
  if (step < 0 || step >= s_Max.Count()) return 0;
  return s_Max[step];
 }

 static int GetAverage(int step)
 {
		EXPBG_LazyStatics_EAC_SchedulerStats();
  if (step < 0 || step >= s_Samples.Count()) return 0;
  int samples = s_Samples[step];
  if (samples <= 0) return 0;
  return s_Total[step] / samples;
 }

 // Exact over the histogram: the smallest millisecond bucket at or below which
 // `percent` of the samples fall. LIMIT keeps samples * percent inside int range.
 static int Percentile(int step, int percent)
 {
		EXPBG_LazyStatics_EAC_SchedulerStats();
  if (step < 0 || step >= s_Samples.Count() || percent < 0 || percent > 100) return 0;
  int samples = s_Samples[step];
  if (samples <= 0) return 0;
  int target = samples * percent / 100;
  if (target < 1) target = 1;
  int origin = step * BUCKETS;
  int seen;
  for (int i = 0; i < BUCKETS; i++)
  {
   seen += s_Hist[origin + i];
   if (seen >= target) return i;
  }
  return BUCKETS - 1;
 }

 static int GetForcedStarts() { return s_ForcedStarts; }
 static int GetForcedWalks() { return s_ForcedWalks; }
 static int GetForceAttempts() { return s_ForceAttempts; }
 static int GetForceRefused() { return s_ForcedNone; }
 static float GetForcedPeakIdle() { return s_ForcedPeakIdle; }
 static int GetForceDeferred() { return s_ForcedDeferred; }
 static int GetForcedRearms() { return s_ForcedRearms; }
 static int GetPeakTickSpawns() { return s_PeakTickSpawns; }
 static int GetPeakTickDespawns() { return s_PeakTickDespawns; }
 static int GetPeakTickTrafficSpawns() { return s_PeakTickTrafficSpawns; }
 static int GetTrafficSpawns() { return s_TrafficSpawns; }
 static int GetSpawns() { return s_Spawns; }
 static int GetDespawns() { return s_Despawns; }

 // An if-ladder rather than a switch: every other name mapping in this addon
 // (EAC_VanillaSeat.RungName) is written this way because the case labels are
 // static const members rather than enum ordinals.
 static string StepName(int step)
 {
  if (step == STEP_FRAME) return "tick";
  if (step == STEP_INDEX) return "index";
  if (step == STEP_PREWARM) return "prewarm";
  if (step == STEP_SCENE) return "scene";
  if (step == STEP_MONITOR) return "monitor";
  if (step == STEP_SERVICE) return "service";
  if (step == STEP_MAINTAIN) return "maintain";
  if (step == STEP_PENDING) return "pending";
  if (step == STEP_ADMIT) return "admit";
  if (step == STEP_TRAFFIC) return "traffic";
  if (step == STEP_VOICE) return "voice";
  if (step == STEP_HORN) return "horn";
  if (step == STEP_FORCE) return "force";
  return "step";
 }

 // The headline, without its tag so both callers can prefix their own. Every term
 // is its own statement: Enforce refuses a long '+' chain as "Formula too complex".
 static string Describe()
 {
  int frames = GetSamples(STEP_FRAME);
  int average = GetAverage(STEP_FRAME);
  int p50 = Percentile(STEP_FRAME, 50);
  int p95 = Percentile(STEP_FRAME, 95);
  int peak = GetMax(STEP_FRAME);
  string line = "step_ms=" + average.ToString();
  line += " p50=" + p50.ToString();
  line += " p95=" + p95.ToString();
  line += " max=" + peak.ToString();
  line += " frames=" + frames.ToString();
  return line;
 }

 // Per-sub-step averages and peaks, so a regression names the step that grew.
 static string DescribeSteps()
 {
  string line = "sched_steps";
  for (int step = 1; step < STEPS; step++)
  {
   int samples = GetSamples(step);
   if (samples == 0) continue;
   int average = GetAverage(step);
   int peak = GetMax(step);
   line += " " + StepName(step);
   line += "=" + average.ToString();
   line += "/" + peak.ToString();
  }
  return line;
 }

 // Gradual spawn/despawn and the idle watchdog, one line.
 static string DescribePacing()
 {
  int spawns = s_Spawns;
  int despawns = s_Despawns;
  int peakSpawn = s_PeakTickSpawns;
  int peakDespawn = s_PeakTickDespawns;
  int trafficSpawns = s_TrafficSpawns;
  int peakTraffic = s_PeakTickTrafficSpawns;
  string line = "pacing spawned=" + spawns.ToString();
  line += " despawned=" + despawns.ToString();
  line += " max_spawn_per_tick=" + peakSpawn.ToString();
  line += " max_despawn_per_tick=" + peakDespawn.ToString();
  int recovered = s_SurfaceRecovered;
  line += " traffic_spawned=" + trafficSpawns.ToString();
  line += " max_traffic_spawn_per_tick=" + peakTraffic.ToString();
  int rollbacks = s_CreationRollbacks;
  int deferred = s_RemovalsDeferred;
  line += " navmesh_height_recovered=" + recovered.ToString();
  line += " creation_rollbacks=" + rollbacks.ToString();
  line += " removals_deferred=" + deferred.ToString();
  return line;
 }

 static string DescribeForced()
 {
  int attempts = s_ForceAttempts;
  int starts = s_ForcedStarts;
  int walks = s_ForcedWalks;
  int refused = s_ForcedNone;
  int deferred = s_ForcedDeferred;
  int rearms = s_ForcedRearms;
  float peak = s_ForcedPeakIdle;
  string line = "idle_force attempts=" + attempts.ToString();
  line += " starts=" + starts.ToString();
  line += " walks=" + walks.ToString();
  line += " refused=" + refused.ToString();
  line += " deferred=" + deferred.ToString();
  line += " rearms=" + rearms.ToString();
  line += " peak_idle_s=" + peak.ToString();
  line += " " + DescribeForceGates();
  return line;
 }

 // The refusal breakdown, as its own statement: Enforce refuses a long '+' chain
 // as "Formula too complex".
 static string DescribeForceGates()
 {
  int limitGate = GetForceGate(GATE_ACTIVITY_LIMIT);
  int otherGate = GetForceGate(GATE_ACTIVITY_OTHER);
  int startGate = GetForceGate(GATE_START);
  int walkGate = GetForceGate(GATE_WALK);
  string line = "refused_activity_limit=" + limitGate.ToString();
  line += " refused_activity_other=" + otherGate.ToString();
  line += " refused_start=" + startGate.ToString();
  line += " refused_walk=" + walkGate.ToString();
  return line;
 }

	//------------------------------------------------------------------------------------------------
	//! Creates the collections on first use (not in the global static initializer, which has a
	//! per-function instruction limit that large modsets exceed on Windows).
	protected static void EXPBG_LazyStatics_EAC_SchedulerStats()
	{
		if (!s_Total)
			s_Total = new array<int>();
		if (!s_Max)
			s_Max = new array<int>();
		if (!s_Hist)
			s_Hist = new array<int>();
		if (!s_ForceGates)
			s_ForceGates = new array<int>();
	}
}
