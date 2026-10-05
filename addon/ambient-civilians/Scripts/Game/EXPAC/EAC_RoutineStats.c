// Mission counters for the believability features, so a native run can measure
// them instead of being told they work. Counts are gate evaluations, not unique
// residents. Bounded, reset per world, never used for admission decisions.
class EAC_RoutineStats
{
 static const int LIMIT = 100000;
 // APR_NAV_START: the actor's own standing position projects more than 0.5 m off
 // the navmesh, so the native move would start nowhere (H3, run 93 diagnosis).
 static const int APR_RANGE = 0, APR_ANCHOR = 1, APR_NEAR = 2, APR_ROAD = 3, APR_TRANSIT = 4, APR_ZONE = 5, APR_LEASH = 6, APR_NAV = 7, APR_OK = 8, APR_BODY = 9, APR_RETIRED = 10, APR_NAV_START = 11, APR_REASONS = 12;
 static const int VREJ_COOLDOWN = 0, VREJ_CLAIM = 1, VREJ_ALARM = 2, VREJ_STALE = 3, VREJ_CONTROL = 4, VREJ_OPTIMIZER = 5, VREJ_POSSESSED = 6, VREJ_FAR = 7, VREJ_REASONS = 8;
 static const int INVITE_NONE = 0, INVITE_STALE = 1, INVITE_FAR = 2, INVITE_OK = 3, INVITE_REASONS = 4;
 static const int ACQ_NOPATH = 0, ACQ_HOMEHAS = 1, ACQ_MAXSTATIONS = 2, ACQ_NOINTERIOR = 3, ACQ_PLACEMENT = 4;
 // ACQ_BUDGET: the interior probe ladder ran out of its per-call budget before it
 // finished (audit B2). NOT a refusal - the next call resumes the sweep.
 static const int ACQ_OVERLAP = 5, ACQ_HIDDEN = 6, ACQ_SPAWN = 7, ACQ_CREATED = 8, ACQ_RETIRED = 9, ACQ_BUDGET = 10, ACQ_REASONS = 11;
 static const int SEAT_ACQUIRED = 0, SEAT_TOONEAR = 1, SEAT_APPROACH = 2, SEAT_ANIM = 3, SEAT_POINT = 4;
 static const int SEAT_ACTION = 5, SEAT_PREPARE = 6, SEAT_CONTAINS = 7, SEAT_ENTER = 8, SEAT_OCCUPIED = 9, SEAT_REASONS = 10;
 static const int JOIN_CANDIDATE = 0, JOIN_FAR = 1, JOIN_NAVMESH = 2, JOIN_ROAD = 3, JOIN_ZONE = 4, JOIN_INTERIOR = 5, JOIN_OK = 6, JOIN_REASONS = 7;
 // Why a walk attempt was refused, in the order EAC_PedestrianWalk.Step runs its
 // gates. walk_attempts counts the attempts; these say which line ended each one.
 // WLK_NAV_START: the walker's own standing position is off the navmesh (H3).
 // WLK_BODY is NOT a refusal. The straight body sweep no longer refuses a wander
 // leg (audit item 2: it turned away 74 % of all attempts and the move-failure
 // guard on the order is the real backstop); it is still evaluated once per
 // otherwise-accepted leg purely so a campaign can say how many good legs it
 // used to eat. body >= ok means the sweep was the wander's whole problem.
 // Audit S5. WLK_CLEAR used to absorb every ClearLeg refusal, so "the wander is
 // being refused" could not be told apart into its four causes. WLK_ZONE is an
 // exclusion zone, WLK_DEST the destination's own body volume, WLK_SAMPLE one of
 // the five interior navmesh projections, and WLK_CLEAR now means specifically
 // body clearance at a projected interior sample.
 static const int WLK_TILE = 0, WLK_NAV = 1, WLK_BAND = 2, WLK_LEASH = 3, WLK_CLEAR = 4, WLK_OK = 5, WLK_NAV_START = 6, WLK_BODY = 7;
 static const int WLK_ZONE = 8, WLK_DEST = 9, WLK_SAMPLE = 10, WLK_REASONS = 11;
 // Which BeginLeg clause refused a routine start. Only the two the idle audit
 // named: an order already on the group (item 8, which used to stop a live
 // wander leg as well as refusing) and the loiter-animation gate, which is the
 // one a stand-up debt keeps failing. Both cost the 20 s TryActivity retry, not
 // a rest; the counters exist so the next campaign can prove that.
 // Every refusal path in BeginLeg, not just two of them. Campaign 118 and the
 // Activities fixture both had to be diagnosed without this: the idle ladder
 // could only report "tryactivity.next_activity" while the real answer was an
 // order on the group that the walker kept replacing.
 static const int BLG_ORDERS = 0, BLG_ANIM = 1, BLG_ORDERS_CLEARED = 2, BLG_OWNERSHIP = 3;
 static const int BLG_CONTROLLER = 4, BLG_STANDING = 5, BLG_PERFORMER = 6, BLG_ANCHOR = 7;
 static const int BLG_STATION = 8, BLG_ROUTINE = 9;
 // Start-level gates, ABOVE BeginLeg. Campaign 127 and the Danger 23/1 both
 // refused here: the 20 s TryActivity re-arm proves Start() was reached and
 // returned false, but every gate it can return on was invisible, so the idle
 // ladder could only ever say "tryactivity.next_activity".
 static const int BLG_START_MODULE = 10, BLG_START_HELD = 11, BLG_START_ALARM = 12;
 static const int BLG_START_COOLDOWN = 13, BLG_REASONS = 14;
 // Which sub-rule inside the navmesh clearance refused. EAC_PedestrianSpawner
 // returns one GEOMETRY verdict for both the height rule and the body box, so a
 // single GetSurfaceY at the call site separates them. dest refuses; sample is an
 // OBSERVATION on otherwise-accepted legs (see EAC_PedestrianWalk.ClearLeg).
 static const int CLR_HEIGHT = 0, CLR_WATER = 1, CLR_BOX = 2, CLR_REASONS = 3;
 // Native group move failures, seen through SCR_AIGroupUtilityComponent's
 // OnMoveFailed invoker (EAC_MoveFailureGuard). UNKNOWN on a waypoint move is the
 // one result vanilla turns into NodeError (runs 83 and 85); DEFUSED counts the
 // times the guard nulled the activity's related waypoint before that line ran;
 // OTHER is every other EMoveError, which vanilla already handles gracefully;
 // RETIRED is a destination struck off for the rest of the mission; ATTACHED is
 // guards created. REMOVED is an UNKNOWN whose activity had already lost its
 // related waypoint - vanilla's own "removed waypoint" branch
 // (SCR_AIProcessFailedMovementResult.c:87-95), reached whenever this addon
 // removes its own order under a running move - nothing to defuse and no order
 // to charge. STALE is an UNKNOWN reported for a move whose location lies more
 // than MATCH_RADIUS from the group's current order: a previous order's failure
 // arriving after the next order was issued, so charging it would abort a good
 // stop. unknown > defused + removed + stale in a log means a group failed a move
 // with no current activity to defuse, and a Diag executable will have raised.
 static const int MOVE_UNKNOWN = 0, MOVE_DEFUSED = 1, MOVE_OTHER = 2, MOVE_RETIRED = 3, MOVE_ATTACHED = 4, MOVE_REMOVED = 5, MOVE_STALE = 6, MOVE_REASONS = 7;
 // Why EAC_CivilianActivity.Monitor ended an approach it did not enter, one per
 // stop (the first cause only; later RequestStop calls on a stop already
 // stopping are not counted). Run 93's stopless routines carried no line at
 // all, and the danger gate in particular prints nothing of its own.
 // DANGER: EAC_CivilianDanger.HasThreat. FOREIGN_WP: an order on the group that
 // is not this stop's approach. WP_CHANGED: the approach is no longer the
 // group's current order. ROUTE: the straight line to the point, or a segment
 // of the native route, crosses an exclusion zone. DEADLINE: the entry deadline
 // passed before the pose began. DISTANT: no movement component, or the native
 // route exceeds the 64-segment cap. POSE: the approach finished and the action
 // was performed, but no pose began inside the two 15 s windows (audit A8) -
 // without it the resident stood until the 90-240 s entry deadline. DRAIN: the
 // stop was told to end and its loiter never reported stopped, so the hard drain
 // deadline released the slot (audit item 9); a non-zero drain count means an
 // activity would otherwise have held its slot and blocked caching for ever.
 static const int ABORT_DANGER = 0, ABORT_FOREIGN_WP = 1, ABORT_WP_CHANGED = 2, ABORT_ROUTE = 3, ABORT_DEADLINE = 4, ABORT_DISTANT = 5, ABORT_POSE = 6, ABORT_DRAIN = 7, ABORT_REASONS = 8;
 // All-vanilla seating spike. Every rung between "the routine asked for a pose"
 // and "the character is in one" is counted separately, because the three native
 // rungs fail in ways that inspection cannot tell apart: a construction failure
 // means the API shape is not what it looks like, a timeout means the API shape
 // is right and a plain civilian group does not honour it.
 static const int SEATV_START = 0, SEATV_NOAGENT = 1, SEATV_NOSCRIPT = 2, SEATV_BADINDEX = 3, SEATV_BADXFORM = 4;
 static const int SEATV_WP_SPAWN = 5, SEATV_WP_ADDED = 6, SEATV_WP_POSED = 7, SEATV_WP_TIMEOUT = 8;
 static const int SEATV_MSG_SENT = 9, SEATV_MSG_POSED = 10, SEATV_MSG_TIMEOUT = 11;
 static const int SEATV_DIRECT_SENT = 12, SEATV_DIRECT_POSED = 13, SEATV_DIRECT_TIMEOUT = 14;
 static const int SEATV_LOST = 15, SEATV_REASONS = 16;
 // Optional ACE animation probe. ACE is never a dependency; these counters exist
 // so a server that happens to run ACE can say, from measurement rather than
 // assertion, exactly why each candidate pose was or was not adopted. With ACE
 // absent every counter except ace_probed stays at zero, and the candidate table
 // is empty anyway, so probed=1 with candidates=0 is the expected vanilla shape.
 static const int ACE_PROBED = 0, ACE_NOSAMPLE = 1, ACE_NOANIM = 2, ACE_ADDON = 3, ACE_CANDIDATE = 4;
 static const int ACE_NOADDON = 5, ACE_NOGRAPH = 6, ACE_NOINSTANCE = 7, ACE_UNBOUND = 8, ACE_VALID = 9, ACE_REASONS = 10;
 // Bounded independently of the allowlist length so a longer vocabulary can
 // never resize a live counter array mid-world.
 static const int VOICE_SLOTS = 14;
 // Roaming routines. A routine is a chain of occupied stops, so "how many
 // routines ran" and "how many occupations happened" are different numbers and
 // the difference between them is the whole feature. s_Stops[k] counts how many
 // routines reached stop k+1, bounded by the module's own 1..4 clamp.
 static const int STOP_SLOTS = 4;
 protected static BaseWorld s_World;
 protected static ref array<int> s_Stops = {};
 protected static int s_Routines, s_RoutinesPlanned, s_PlannedStops, s_Legs, s_MultiStop, s_MaxStops;
 protected static int s_StopTravel, s_StopTravelCount, s_MaxStopTravel, s_RoutineSeconds, s_RoutineTravel, s_LegDeferred;
 protected static int s_IdleSpans, s_IdleSeconds, s_MaxIdleSeconds;
 protected static ref array<int> s_Attempts = {};
 protected static ref array<int> s_Accepts = {};
 protected static ref array<int> s_Wanted = {};
 protected static ref array<float> s_NextTrace = {};
 protected static int s_IndoorAdmitted, s_IndoorRejected, s_Emerged, s_EmergeFailed, s_Fallback, s_IdleRecovered;
 protected static int s_TableStarted, s_TableJoined, s_TableInvited, s_MaxHomeResidents;
 protected static int s_TableWanted, s_MaxStep;
 // table_joined has never once been non-zero across six campaigns. Which gate
 // rejects the join was guessed at twice and both guesses were wrong, so count
 // the actual reason instead.
 protected static ref array<int> s_Invite = {};
 protected static ref array<int> s_Join = {};
 protected static ref array<int> s_Seat = {};
 protected static ref array<int> s_Acq = {};
 protected static ref array<int> s_VoiceReject = {};
 protected static ref array<int> s_Approach = {};
 protected static ref array<int> s_Walk = {};
 protected static ref array<int> s_MoveFailure = {};
 protected static ref array<int> s_Abort = {};
 protected static ref array<int> s_BeginLeg = {};
 protected static ref array<int> s_ClearDest = {};
 protected static ref array<int> s_ClearSample = {};
 // Cover-pose ladder (campaign 127). retry: the four-request batch was renewed
 // because the character was still busy. timeout: the pose never applied inside
 // COVER_WINDOW_SECONDS and the response proceeded without it.
 protected static int s_CoverTimeouts, s_CoverRetries;
 protected static ref array<int> s_SeatV = {};
 protected static ref array<int> s_AceProbe = {};
 // The ambient cough has never once been confirmed audible. Separate the three
 // things that were previously indistinguishable: the interval firing, a speaker
 // actually being found and broadcast, and which event was chosen.
 protected static int s_VoiceRequested, s_VoiceIssued;
 protected static ref array<int> s_Voice = {};
 // Prewarm design 4.1 and 4.2 prerequisites. Nothing here changes behaviour;
 // these are the numbers that have to exist before the LOD pin can be shortened
 // or the walker's attempt ladder touched. Land the counters, run a campaign,
 // then decide - which is the opposite of what the last two tuning guesses did.
 protected static int s_PinnedSeconds, s_PinnedIssues;
 protected static int s_WalkAttempts, s_WalkAccepted, s_WalkExhausted;
 // Which bearing produced an accepted wander leg. The fan used to spend attempts
 // 1-6 inside one 100-degree arc around the goal (audit item 10); it now
 // alternates goal-biased and full-circle bearings, and circle > 0 is the proof
 // that the alternation actually supplies legs the goal fan could not.
 protected static int s_WalkGoalLegs, s_WalkCircleLegs;
 // Goal-biased bearings ATTEMPTED. fan_goal alone (accepted) could not tell "the
 // fan is never reached" from "its bearings are always refused"; tried vs
 // accepted says which.
 protected static int s_WalkGoalTried;
 // No height-recovery counter lives here. EAC_PedestrianSpawner.GetClearReason
 // records EAC_SchedulerStats.RecordSurfaceRecovered() from inside the same
 // evaluation, so counting it again on this side would double-count one event.
 // The cost is that the pooled number no longer splits walker from anchor; if
 // that split is wanted it belongs in the spawner's own counter as a caller tag,
 // not as a second count here.
 // How a closed routine was charged (audit item 1). supplied: a stop was entered,
 // a point was built or a station was acquired - that routine pays RoutineGap.
 // nosupply: nothing at all was produced, the only case that still consults
 // ActivityInterval, and even then the rest is clamped.
 protected static int s_RestSupplied, s_RestNoSupply, s_MaxRestSeconds;
 // Surveyed-spot supply seen by EAC_CivilianActivity.OutdoorAnchor's three-attempt
 // loop. retry counts attempts after the first; repeat counts a retry that was
 // handed the SAME position again, which is exactly what the zeroed anti-repeat
 // stamp used to guarantee (audit item 5). repeat near zero means the three
 // attempts genuinely offer three different spots.
 protected static int s_SceneRetries, s_SceneRepeats;
 // Audit B2. How the bounded anchor sweep is being spent. deferred: a BeginLeg
 // that used its whole probe budget without finishing the sweep and will resume
 // on the next retry. swept: a sweep that completed all sixteen probes and fell
 // through to the actor-centred last resort. deferred far above swept means the
 // budget is doing its job; swept far above deferred means homes are accepting
 // early, which is the normal case.
 protected static int s_AnchorDeferred, s_AnchorSwept;
 // Campaign 118. Point deletions held back because vanilla's animate behaviour
 // could still resolve the point as its animation root. Non-zero is the guard
 // working; it should be small and must never be a monotonic climb, which would
 // mean a root that is never released.
 protected static int s_RootDeferred;
 // Audit S13. Shelter interior probes granted and denied by the per-tick budget.
 // denied rising while an alarm runs means PROBE_BUDGET is the binding constraint
 // on how fast a town takes cover.
 protected static int s_ShelterProbes, s_ShelterProbesDenied;
 // An emergence order whose native move failed (audit item 6). Emergence used to
 // issue orders with no move-failure guard at all, so this was invisible.
 protected static int s_EmergeMoveFailed;

 protected static void CheckWorld()
 {
  BaseWorld world = GetGame().GetWorld();
  // s_Stops must be in this guard. Leaving a counter array out of it resizes the
  // array on every call and silently resets every counter mid-world - this
  // file's own standing trap.
  // Split: a single long && chain here fails Enforce with "Formula too complex".
  bool sized = world == s_World;
  sized = sized && s_Approach.Count() == APR_REASONS;
  sized = sized && s_VoiceReject.Count() == VREJ_REASONS;
  sized = sized && s_Acq.Count() == ACQ_REASONS;
  sized = sized && s_Seat.Count() == SEAT_REASONS;
  sized = sized && s_SeatV.Count() == SEATV_REASONS;
  sized = sized && s_AceProbe.Count() == ACE_REASONS;
  sized = sized && s_Voice.Count() == VOICE_SLOTS;
  sized = sized && s_Stops.Count() == STOP_SLOTS;
  sized = sized && s_Invite.Count() == INVITE_REASONS;
  sized = sized && s_Join.Count() == JOIN_REASONS;
  sized = sized && s_Walk.Count() == WLK_REASONS;
  sized = sized && s_MoveFailure.Count() == MOVE_REASONS;
  sized = sized && s_Abort.Count() == ABORT_REASONS;
  sized = sized && s_BeginLeg.Count() == BLG_REASONS;
  sized = sized && s_ClearDest.Count() == CLR_REASONS;
  sized = sized && s_ClearSample.Count() == CLR_REASONS;
  sized = sized && s_Attempts.Count() == EAC_RoutineAnchors.KINDS;
  // s_Accepts was resized below but never checked here: on the first call of a
  // new world the guard could pass on s_Attempts alone while s_Accepts still held
  // the previous world's array (audit nit).
  sized = sized && s_Accepts.Count() == EAC_RoutineAnchors.KINDS;
  sized = sized && s_Wanted.Count() == EAC_RoutineAnchors.KINDS;
  sized = sized && s_NextTrace.Count() == EAC_RoutineAnchors.KINDS;
  if (sized) return;
  s_World = world;
  s_Attempts.Clear(); s_Attempts.Resize(EAC_RoutineAnchors.KINDS);
  s_Accepts.Clear(); s_Accepts.Resize(EAC_RoutineAnchors.KINDS);
  s_Wanted.Clear(); s_Wanted.Resize(EAC_RoutineAnchors.KINDS);
  s_NextTrace.Clear(); s_NextTrace.Resize(EAC_RoutineAnchors.KINDS);
  s_IndoorAdmitted = 0; s_IndoorRejected = 0; s_Emerged = 0; s_EmergeFailed = 0; s_Fallback = 0; s_IdleRecovered = 0;
  s_TableStarted = 0; s_TableJoined = 0; s_TableInvited = 0; s_MaxHomeResidents = 0;
  s_TableWanted = 0; s_MaxStep = 0;
  s_Invite.Clear(); s_Invite.Resize(INVITE_REASONS);
  s_Join.Clear(); s_Join.Resize(JOIN_REASONS);
  s_Seat.Clear(); s_Seat.Resize(SEAT_REASONS);
  s_Acq.Clear(); s_Acq.Resize(ACQ_REASONS);
  s_VoiceReject.Clear(); s_VoiceReject.Resize(VREJ_REASONS);
  s_Approach.Clear(); s_Approach.Resize(APR_REASONS);
  s_Walk.Clear(); s_Walk.Resize(WLK_REASONS);
  s_MoveFailure.Clear(); s_MoveFailure.Resize(MOVE_REASONS);
  s_Abort.Clear(); s_Abort.Resize(ABORT_REASONS);
  s_BeginLeg.Clear(); s_BeginLeg.Resize(BLG_REASONS);
  s_ClearDest.Clear(); s_ClearDest.Resize(CLR_REASONS);
  s_ClearSample.Clear(); s_ClearSample.Resize(CLR_REASONS);
  s_CoverTimeouts = 0; s_CoverRetries = 0;
  s_SeatV.Clear(); s_SeatV.Resize(SEATV_REASONS);
  s_AceProbe.Clear(); s_AceProbe.Resize(ACE_REASONS);
  s_VoiceRequested = 0; s_VoiceIssued = 0;
  s_Voice.Clear(); s_Voice.Resize(VOICE_SLOTS);
  s_Stops.Clear(); s_Stops.Resize(STOP_SLOTS);
  s_Routines = 0; s_RoutinesPlanned = 0; s_PlannedStops = 0; s_Legs = 0; s_MultiStop = 0; s_MaxStops = 0;
  s_StopTravel = 0; s_StopTravelCount = 0; s_MaxStopTravel = 0; s_RoutineSeconds = 0; s_RoutineTravel = 0; s_LegDeferred = 0;
  s_IdleSpans = 0; s_IdleSeconds = 0; s_MaxIdleSeconds = 0;
  s_PinnedSeconds = 0; s_PinnedIssues = 0;
  s_WalkAttempts = 0; s_WalkAccepted = 0; s_WalkExhausted = 0;
  s_WalkGoalLegs = 0; s_WalkCircleLegs = 0;
  s_WalkGoalTried = 0;
  s_RestSupplied = 0; s_RestNoSupply = 0; s_MaxRestSeconds = 0;
  s_SceneRetries = 0; s_SceneRepeats = 0;
  s_AnchorDeferred = 0; s_AnchorSwept = 0;
  s_RootDeferred = 0;
  s_ShelterProbes = 0; s_ShelterProbesDenied = 0;
  s_EmergeMoveFailed = 0;
 }

 protected static void Bump(array<int> counter, int index)
 {
  if (index < 0 || index >= counter.Count() || counter[index] >= LIMIT) return;
  counter[index] = counter[index] + 1;
 }

 static void RecordAnchor(EAC_EAnchorKind kind, bool accepted)
 {
  if (!Replication.IsServer()) return;
  CheckWorld();
  Bump(s_Attempts, kind);
  if (accepted) Bump(s_Accepts, kind);
 }

 // Every anchor kind failed and the actor-centred last resort was used instead.
 // A high count means the catalog is degrading to plain open ground.
 // The anchor kind the selected routine actually asked for, recorded before any
 // sweep or degradation. A kind with wanted=0 was never chosen by any resident,
 // which is a catalog-reachability problem, not a geometry one. A kind with
 // wanted>0 but attempts=0 was replaced by the unsupported-animation fallback.
 // Two corner predicates have now returned 0/40. Guessing at the cause has cost
 // two campaigns, so report the actual rejection reason for any kind that has
 // never once been accepted. One line per kind per ten seconds, server only.
 static void TraceRejection(EAC_EAnchorKind kind, string reason, float now)
 {
  if (!Replication.IsServer()) return;
  CheckWorld();
  // Log hygiene (readiness plan S1). This class holds no module, so the level
  // comes from the static mirror EAC_AmbientModule writes from NormalizeSettings,
  // SetSetting case 10 and every scheduler tick. Level 2 matches every other line
  // this class emits: Describe/DescribeRoutines/DescribeSeats are all printed by
  // BuildDebugSummary at DebugLevel >= 2, so the anchor trace and the counters it
  // explains appear and disappear together. A stock DebugLevel 0 server prints
  // nothing from here at all.
  int level = EAC_AmbientModule.GetDebugLevelMirror();
  if (level < 2) return;
  if (kind < 0 || kind >= EAC_RoutineAnchors.KINDS) return;
  if (s_Accepts[kind] > 0 || s_Attempts[kind] < 4) return;
  if (now < s_NextTrace[kind]) return;
  s_NextTrace[kind] = now + 10;
  PrintFormat("[EAC ANCHOR REJECT] kind=%1 attempts=%2 reason=%3", EAC_RoutineAnchors.ShortName(kind), s_Attempts[kind], reason);
 }

 static void RecordWanted(EAC_EAnchorKind kind)
 {
  if (!Replication.IsServer()) return;
  CheckWorld();
  Bump(s_Wanted, kind);
 }

 // A resident was found completely inert and nudged. A rising count means the
 // ordinary paths are failing, not that the watchdog is working well.
 static void RecordIdleRecovery()
 {
  if (!Replication.IsServer()) return;
  CheckWorld();
  if (s_IdleRecovered < LIMIT) s_IdleRecovered++;
 }

 static void RecordFallback()
 {
  if (!Replication.IsServer()) return;
  CheckWorld();
  if (s_Fallback < LIMIT) s_Fallback++;
 }

 static void RecordIndoorSpawn(bool admitted)
 {
  if (!Replication.IsServer()) return;
  CheckWorld();
  if (admitted) { if (s_IndoorAdmitted < LIMIT) s_IndoorAdmitted++; }
  else if (s_IndoorRejected < LIMIT) s_IndoorRejected++;
 }

 static void RecordEmergence(bool emerged)
 {
  if (!Replication.IsServer()) return;
  CheckWorld();
  if (emerged) { if (s_Emerged < LIMIT) s_Emerged++; }
  else if (s_EmergeFailed < LIMIT) s_EmergeFailed++;
 }

 // A table was raised, a second seat was taken, or a housemate was redirected
 // to join one. peak_seated never exceeded 1 in any campaign and nothing
 // recorded which of the three steps was failing.
 static void RecordTable(int started, int joined, int invited)
 {
  if (!Replication.IsServer()) return;
  CheckWorld();
  if (started > 0 && s_TableStarted < LIMIT) s_TableStarted++;
  if (joined > 0 && s_TableJoined < LIMIT) s_TableJoined++;
  if (invited > 0 && s_TableInvited < LIMIT) s_TableInvited++;
 }

 // Three campaigns raised tables that nobody ever sat at. Every gate between a
 // station being acquired and an occupant entering it is counted here, because
 // the failure is somewhere in that run of nine and guessing has a poor record.
 // Acquire returned null with table_wanted=1 and every downstream seat gate at
 // zero, so the refusal is inside the creation path itself.
 // Which Eligible gate turned a voice candidate away. Counted per candidate,
 // so one interval that scans twelve residents adds twelve.
 // Which CanApproach clause turned a TABLE occupation away. Run 45: two
 // acquisitions, two approach rejections, zero information about which line.
 static void RecordApproach(int reason)
 {
  if (!Replication.IsServer()) return;
  CheckWorld();
  Bump(s_Approach, reason);
 }

 static string DescribeApproach()
 {
  CheckWorld();
  string result = "approach range=" + s_Approach[APR_RANGE].ToString();
  result += " anchor=" + s_Approach[APR_ANCHOR].ToString();
  result += " near=" + s_Approach[APR_NEAR].ToString();
  result += " road=" + s_Approach[APR_ROAD].ToString();
  result += " transit=" + s_Approach[APR_TRANSIT].ToString();
  result += " zone=" + s_Approach[APR_ZONE].ToString();
  result += " leash=" + s_Approach[APR_LEASH].ToString();
  result += " nav=" + s_Approach[APR_NAV].ToString();
  result += " body=" + s_Approach[APR_BODY].ToString();
  result += " ok=" + s_Approach[APR_OK].ToString();
  result += " retired=" + s_Approach[APR_RETIRED].ToString();
  result += " nav_start=" + s_Approach[APR_NAV_START].ToString();
  result += " | ";
  result += DescribeMoveFailures();
  return result;
 }

 // Every PreventMaxLOD issue asks the engine to hold one agent at full simulation
 // for a number of seconds. The pin is the largest single per-agent lever in the
 // system and nobody has ever measured what it costs, so count the seconds asked
 // for. Counted per issue, so an agent and its group pinned together count twice,
 // which is what the engine is actually being asked to do.
 static void RecordPinnedSeconds(float seconds)
 {
  if (!Replication.IsServer() || seconds <= 0) return;
  CheckWorld();
  int whole = Math.Round(seconds);
  if (whole < 1) whole = 1;
  if (s_PinnedSeconds < LIMIT) s_PinnedSeconds += whole;
  if (s_PinnedSeconds > LIMIT) s_PinnedSeconds = LIMIT;
  if (s_PinnedIssues < LIMIT) s_PinnedIssues++;
 }

 // The walker's attempt ladder. If walk_exhausted is high against walk_accepted
 // the geometry is refusing legs and the fix is in ClearLeg or the fan, not in
 // the attempt count - which is the correction prewarm design 4.2 records.
 static void RecordWalkAttempt()
 {
  if (!Replication.IsServer()) return;
  CheckWorld();
  if (s_WalkAttempts < LIMIT) s_WalkAttempts++;
 }

 static void RecordWalkAccepted()
 {
  if (!Replication.IsServer()) return;
  CheckWorld();
  if (s_WalkAccepted < LIMIT) s_WalkAccepted++;
 }

 static void RecordWalkExhausted()
 {
  if (!Replication.IsServer()) return;
  CheckWorld();
  if (s_WalkExhausted < LIMIT) s_WalkExhausted++;
 }

 // One refusal reason from EAC_PedestrianWalk.Step, or WLK_OK on acceptance.
 static void RecordWalk(int reason)
 {
  if (!Replication.IsServer()) return;
  CheckWorld();
  Bump(s_Walk, reason);
 }

 // A wander leg was accepted. `goalBiased` says the bearing came from the goal
 // fan rather than from the full-circle sweep, so a campaign can see whether the
 // alternation introduced in audit item 10 is what supplied the leg.
 static void RecordWalkFan(bool goalBiased)
 {
  if (!Replication.IsServer()) return;
  CheckWorld();
  if (goalBiased) { if (s_WalkGoalLegs < LIMIT) s_WalkGoalLegs++; return; }
  if (s_WalkCircleLegs < LIMIT) s_WalkCircleLegs++;
 }

 // A goal-biased bearing was generated for this attempt.
 // `destination` false means the interior-sample observation, which no longer
 // refuses anything - it only says what the removed box would have cost.
 static void RecordClearance(bool destination, int reason)
 {
  if (!Replication.IsServer()) return;
  CheckWorld();
  if (destination) { Bump(s_ClearDest, reason); return; }
  Bump(s_ClearSample, reason);
 }

 static void RecordCoverTimeout()
 {
  if (!Replication.IsServer()) return;
  CheckWorld();
  if (s_CoverTimeouts < LIMIT) s_CoverTimeouts++;
 }

 static void RecordCoverRetry()
 {
  if (!Replication.IsServer()) return;
  CheckWorld();
  if (s_CoverRetries < LIMIT) s_CoverRetries++;
 }

 static int GetCoverTimeouts() { CheckWorld(); return s_CoverTimeouts; }

 static void RecordWalkFanTried()
 {
  if (!Replication.IsServer()) return;
  CheckWorld();
  if (s_WalkGoalTried < LIMIT) s_WalkGoalTried++;
 }

 static int GetWalk(int reason)
 {
  CheckWorld();
  if (reason < 0 || reason >= WLK_REASONS) return 0;
  return s_Walk[reason];
 }

 // One '+=' per term, same shape as DescribeApproach.
 static string DescribeWalk()
 {
  CheckWorld();
  string result = "walk tile=" + s_Walk[WLK_TILE].ToString();
  result += " nav=" + s_Walk[WLK_NAV].ToString();
  result += " band=" + s_Walk[WLK_BAND].ToString();
  result += " leash=" + s_Walk[WLK_LEASH].ToString();
  result += " clear=" + s_Walk[WLK_CLEAR].ToString();
  result += " ok=" + s_Walk[WLK_OK].ToString();
  result += " nav_start=" + s_Walk[WLK_NAV_START].ToString();
  result += " body_obs=" + s_Walk[WLK_BODY].ToString();
  result += " zone=" + s_Walk[WLK_ZONE].ToString();
  result += " dest=" + s_Walk[WLK_DEST].ToString();
  result += " sample=" + s_Walk[WLK_SAMPLE].ToString();
  result += " fan_goal=" + s_WalkGoalLegs.ToString();
  result += "/" + s_WalkGoalTried.ToString();
  result += " fan_circle=" + s_WalkCircleLegs.ToString();
  // Which sub-rule refused the destination, and what the removed interior box
  // would have refused on legs that are now accepted.
  result += " dest_height=" + s_ClearDest[CLR_HEIGHT].ToString();
  result += " dest_water=" + s_ClearDest[CLR_WATER].ToString();
  result += " dest_box=" + s_ClearDest[CLR_BOX].ToString();
  result += " sample_height_obs=" + s_ClearSample[CLR_HEIGHT].ToString();
  result += " sample_water_obs=" + s_ClearSample[CLR_WATER].ToString();
  result += " sample_box_obs=" + s_ClearSample[CLR_BOX].ToString();
  return result;
 }

 static void RecordMoveFailure(int reason)
 {
  if (!Replication.IsServer()) return;
  CheckWorld();
  Bump(s_MoveFailure, reason);
 }

 static int GetMoveFailure(int reason)
 {
  CheckWorld();
  if (reason < 0 || reason >= MOVE_REASONS) return 0;
  return s_MoveFailure[reason];
 }

 // One '+=' per term, same shape as DescribeWalk.
 static string DescribeMoveFailures()
 {
  CheckWorld();
  string result = "move_failed unknown=" + s_MoveFailure[MOVE_UNKNOWN].ToString();
  result += " defused=" + s_MoveFailure[MOVE_DEFUSED].ToString();
  result += " other=" + s_MoveFailure[MOVE_OTHER].ToString();
  result += " spot_retired=" + s_MoveFailure[MOVE_RETIRED].ToString();
  result += " attached=" + s_MoveFailure[MOVE_ATTACHED].ToString();
  result += " removed=" + s_MoveFailure[MOVE_REMOVED].ToString();
  result += " stale=" + s_MoveFailure[MOVE_STALE].ToString();
  return result;
 }

 // One approach abort from EAC_CivilianActivity.Monitor, by first cause.
 static void RecordAbort(int reason)
 {
  if (!Replication.IsServer()) return;
  CheckWorld();
  Bump(s_Abort, reason);
 }

 static int GetAbort(int reason)
 {
  CheckWorld();
  if (reason < 0 || reason >= ABORT_REASONS) return 0;
  return s_Abort[reason];
 }

 // One '+=' per term, same shape as DescribeMoveFailures.
 static string DescribeAborts()
 {
  CheckWorld();
  string result = "aborts danger=" + s_Abort[ABORT_DANGER].ToString();
  result += " foreign_wp=" + s_Abort[ABORT_FOREIGN_WP].ToString();
  result += " wp_changed=" + s_Abort[ABORT_WP_CHANGED].ToString();
  result += " route=" + s_Abort[ABORT_ROUTE].ToString();
  result += " deadline=" + s_Abort[ABORT_DEADLINE].ToString();
  result += " distant=" + s_Abort[ABORT_DISTANT].ToString();
  result += " pose=" + s_Abort[ABORT_POSE].ToString();
  result += " drain=" + s_Abort[ABORT_DRAIN].ToString();
  return result;
 }

 // Which BeginLeg clause refused a routine start. Both are 20 s TryActivity
 // retries, never a rest: see EAC_PedestrianSpawner.TryActivity.
 static void RecordBeginLegGate(int reason)
 {
  if (!Replication.IsServer()) return;
  CheckWorld();
  Bump(s_BeginLeg, reason);
 }

 static int GetBeginLegGate(int reason)
 {
  CheckWorld();
  if (reason < 0 || reason >= BLG_REASONS) return 0;
  return s_BeginLeg[reason];
 }

 // How a closed routine was charged, and the largest rest any routine handed
 // out. The owner rule is that no active resident near a player stands with no
 // order for longer than 120 s, so rest_max is the number that rule is read off:
 // a rest is the longest single component of an idle gap.
 static void RecordRoutineRest(bool supplied, float seconds)
 {
  if (!Replication.IsServer()) return;
  CheckWorld();
  if (supplied) { if (s_RestSupplied < LIMIT) s_RestSupplied++; }
  else if (s_RestNoSupply < LIMIT) s_RestNoSupply++;
  int whole = Math.Round(seconds);
  if (whole < 0) whole = 0;
  if (whole > s_MaxRestSeconds) s_MaxRestSeconds = whole;
 }

 static int GetMaxRestSeconds() { CheckWorld(); return s_MaxRestSeconds; }

 // One retry of OutdoorAnchor's three-attempt surveyed-spot loop. `repeated`
 // means the index handed back a position it had already offered this routine.
 static void RecordSceneRetry(bool repeated)
 {
  if (!Replication.IsServer()) return;
  CheckWorld();
  if (s_SceneRetries < LIMIT) s_SceneRetries++;
  if (repeated && s_SceneRepeats < LIMIT) s_SceneRepeats++;
 }

 static int GetSceneRepeats() { CheckWorld(); return s_SceneRepeats; }

 // Audit B2. `swept` false means the probe budget ran out mid-sweep and the next
 // retry resumes it; true means the whole sweep finished without an accepted
 // anchor and the caller fell through to its last resort.
 static void RecordAnchorBudget(bool swept)
 {
  if (!Replication.IsServer()) return;
  CheckWorld();
  if (swept) { if (s_AnchorSwept < LIMIT) s_AnchorSwept++; return; }
  if (s_AnchorDeferred < LIMIT) s_AnchorDeferred++;
 }

 // Audit S13. One shelter interior probe asked for; `granted` says whether the
 // per-tick budget allowed it.
 static void RecordShelterProbe(bool granted)
 {
  if (!Replication.IsServer()) return;
  CheckWorld();
  if (granted) { if (s_ShelterProbes < LIMIT) s_ShelterProbes++; return; }
  if (s_ShelterProbesDenied < LIMIT) s_ShelterProbesDenied++;
 }

 // A point deletion deferred because the animation root was still borrowed.
 static void RecordRootDeferred()
 {
  if (!Replication.IsServer()) return;
  CheckWorld();
  if (s_RootDeferred < LIMIT) s_RootDeferred++;
 }

 static int GetRootDeferred() { CheckWorld(); return s_RootDeferred; }

 // Short label for a BLG_ gate, for the BeginLeg diagnostic line.
 static string BeginLegGateName(int gate)
 {
  switch (gate)
  {
   case BLG_ORDERS: return "orders";
   case BLG_ANIM: return "anim";
   case BLG_ORDERS_CLEARED: return "orders_cleared";
   case BLG_OWNERSHIP: return "ownership";
   case BLG_CONTROLLER: return "controller";
   case BLG_STANDING: return "standing";
   case BLG_PERFORMER: return "performer";
   case BLG_ANCHOR: return "anchor";
   case BLG_STATION: return "station";
   case BLG_ROUTINE: return "routine";
   case BLG_START_MODULE: return "start_module";
   case BLG_START_HELD: return "start_held";
   case BLG_START_ALARM: return "start_alarm";
   case BLG_START_COOLDOWN: return "start_cooldown";
  }
  return "other";
 }

 static int GetAnchorDeferred() { CheckWorld(); return s_AnchorDeferred; }
 static int GetShelterProbesDenied() { CheckWorld(); return s_ShelterProbesDenied; }

 // An emergence order whose native move failed and was charged against a sweep.
 static void RecordEmergeMoveFailed()
 {
  if (!Replication.IsServer()) return;
  CheckWorld();
  if (s_EmergeMoveFailed < LIMIT) s_EmergeMoveFailed++;
 }

 // One term per statement: a long '+' chain does not compile in this dialect.
 static string DescribePrewarm()
 {
  CheckWorld();
  string result = "levers pinned_agent_seconds=" + s_PinnedSeconds.ToString();
  result += " pinned_issues=" + s_PinnedIssues.ToString();
  result += " walk_attempts=" + s_WalkAttempts.ToString();
  result += " walk_accepted=" + s_WalkAccepted.ToString();
  result += " walk_exhausted=" + s_WalkExhausted.ToString();
  return result;
 }

 static int GetVoiceIssued() { CheckWorld(); return s_VoiceIssued; }

 static void RecordVoiceReject(int reason)
 {
  if (!Replication.IsServer()) return;
  CheckWorld();
  Bump(s_VoiceReject, reason);
 }

 static void RecordAcquire(int reason)
 {
  if (!Replication.IsServer()) return;
  CheckWorld();
  Bump(s_Acq, reason);
 }

 static string DescribeAcquire()
 {
  CheckWorld();
  string result = "acq nopath=" + s_Acq[ACQ_NOPATH].ToString();
  result += " homehas=" + s_Acq[ACQ_HOMEHAS].ToString();
  result += " maxstations=" + s_Acq[ACQ_MAXSTATIONS].ToString();
  result += " nointerior=" + s_Acq[ACQ_NOINTERIOR].ToString();
  result += " placement=" + s_Acq[ACQ_PLACEMENT].ToString();
  result += " overlap=" + s_Acq[ACQ_OVERLAP].ToString();
  result += " hidden=" + s_Acq[ACQ_HIDDEN].ToString();
  result += " spawn=" + s_Acq[ACQ_SPAWN].ToString();
  result += " created=" + s_Acq[ACQ_CREATED].ToString();
  result += " retired=" + s_Acq[ACQ_RETIRED].ToString();
  result += " budget=" + s_Acq[ACQ_BUDGET].ToString();
  return result;
 }

 // A vocalisation interval elapsed and a speaker was sought. Counted whether or
 // not one was found, so requested-with-zero-issued is a gate problem and
 // issued-with-nothing-heard is an audio problem. Those were indistinguishable
 // before, which is why six campaigns could not say why the cough was silent.
 static void RecordVoiceRequest()
 {
  if (!Replication.IsServer()) return;
  CheckWorld();
  if (s_VoiceRequested < LIMIT) s_VoiceRequested++;
 }

 // A speaker was chosen and the event was broadcast. Still says nothing about
 // whether a client resolved the name; only the client probe can say that.
 static void RecordVoiceIssued(int eventIndex)
 {
  if (!Replication.IsServer()) return;
  CheckWorld();
  if (s_VoiceIssued < LIMIT) s_VoiceIssued++;
  Bump(s_Voice, eventIndex);
 }

 static string DescribeVoice()
 {
  CheckWorld();
  string result = "voice requested=" + s_VoiceRequested.ToString();
  result += " issued=" + s_VoiceIssued.ToString();
  int named = EAC_CivilianVoice.EventCount();
  if (named > VOICE_SLOTS) named = VOICE_SLOTS;
  for (int slot = 0; slot < named; slot++)
  {
   result += " " + EAC_CivilianVoice.ShortName(slot);
   result += "=" + s_Voice[slot].ToString();
  }
  result += " | reject cooldown=" + s_VoiceReject[VREJ_COOLDOWN].ToString();
  result += " claim=" + s_VoiceReject[VREJ_CLAIM].ToString();
  result += " alarm=" + s_VoiceReject[VREJ_ALARM].ToString();
  result += " stale=" + s_VoiceReject[VREJ_STALE].ToString();
  result += " control=" + s_VoiceReject[VREJ_CONTROL].ToString();
  result += " optimizer=" + s_VoiceReject[VREJ_OPTIMIZER].ToString();
  result += " possessed=" + s_VoiceReject[VREJ_POSSESSED].ToString();
  result += " far=" + s_VoiceReject[VREJ_FAR].ToString();
  return result;
 }

 static void RecordSeatGate(int reason)
 {
  if (!Replication.IsServer()) return;
  CheckWorld();
  Bump(s_Seat, reason);
 }

 static string DescribeSeats()
 {
  CheckWorld();
  string result = "seatgate acquired=" + s_Seat[SEAT_ACQUIRED].ToString();
  result += " toonear=" + s_Seat[SEAT_TOONEAR].ToString();
  result += " approach=" + s_Seat[SEAT_APPROACH].ToString();
  result += " anim=" + s_Seat[SEAT_ANIM].ToString();
  result += " point=" + s_Seat[SEAT_POINT].ToString();
  result += " action=" + s_Seat[SEAT_ACTION].ToString();
  result += " prepare=" + s_Seat[SEAT_PREPARE].ToString();
  result += " contains=" + s_Seat[SEAT_CONTAINS].ToString();
  result += " enter=" + s_Seat[SEAT_ENTER].ToString();
  result += " occupied=" + s_Seat[SEAT_OCCUPIED].ToString();
  return result;
 }

 // The all-vanilla seating rungs. Read start against the three *_posed counters:
 // start>0 with every posed at zero means no native rung put a civilian into an
 // authored pose at all, which settles the route question outright.
 static void RecordVanillaSeat(int reason)
 {
  if (!Replication.IsServer()) return;
  CheckWorld();
  Bump(s_SeatV, reason);
 }

 static int GetVanillaSeat(int reason)
 {
  CheckWorld();
  if (reason < 0 || reason >= SEATV_REASONS) return 0;
  return s_SeatV[reason];
 }

 static string DescribeVanillaSeats()
 {
  CheckWorld();
  string result = "seatv start=" + s_SeatV[SEATV_START].ToString();
  result += " noagent=" + s_SeatV[SEATV_NOAGENT].ToString();
  result += " noscript=" + s_SeatV[SEATV_NOSCRIPT].ToString();
  result += " badindex=" + s_SeatV[SEATV_BADINDEX].ToString();
  result += " badxform=" + s_SeatV[SEATV_BADXFORM].ToString();
  result += " | wp spawnfail=" + s_SeatV[SEATV_WP_SPAWN].ToString();
  result += " added=" + s_SeatV[SEATV_WP_ADDED].ToString();
  result += " posed=" + s_SeatV[SEATV_WP_POSED].ToString();
  result += " timeout=" + s_SeatV[SEATV_WP_TIMEOUT].ToString();
  result += " | msg sent=" + s_SeatV[SEATV_MSG_SENT].ToString();
  result += " posed=" + s_SeatV[SEATV_MSG_POSED].ToString();
  result += " timeout=" + s_SeatV[SEATV_MSG_TIMEOUT].ToString();
  result += " | direct sent=" + s_SeatV[SEATV_DIRECT_SENT].ToString();
  result += " posed=" + s_SeatV[SEATV_DIRECT_POSED].ToString();
  result += " timeout=" + s_SeatV[SEATV_DIRECT_TIMEOUT].ToString();
  result += " | lost=" + s_SeatV[SEATV_LOST].ToString();
  return result;
 }

 // One outcome from the optional ACE animation probe. Recorded once per world
 // per candidate, never per frame, and never used for admission: a rejected
 // candidate simply does not exist as far as the catalog is concerned.
 static void RecordAceProbe(int reason)
 {
  if (!Replication.IsServer()) return;
  CheckWorld();
  Bump(s_AceProbe, reason);
 }

 static int GetAceProbe(int reason)
 {
  CheckWorld();
  if (reason < 0 || reason >= ACE_REASONS) return 0;
  return s_AceProbe[reason];
 }

 // Read valid against candidates: candidates=0 means this build ships no ACE
 // candidate at all (the measured vanilla case), candidates>0 with valid=0 means
 // ACE is loaded but every pose failed a gate, and the named reason says which.
 static string DescribeAceProbe()
 {
  CheckWorld();
  string result = "ace probed=" + s_AceProbe[ACE_PROBED].ToString();
  result += " nosample=" + s_AceProbe[ACE_NOSAMPLE].ToString();
  result += " noanim=" + s_AceProbe[ACE_NOANIM].ToString();
  result += " loaded=" + s_AceProbe[ACE_ADDON].ToString();
  result += " candidates=" + s_AceProbe[ACE_CANDIDATE].ToString();
  result += " noaddon=" + s_AceProbe[ACE_NOADDON].ToString();
  result += " nograph=" + s_AceProbe[ACE_NOGRAPH].ToString();
  result += " noinstance=" + s_AceProbe[ACE_NOINSTANCE].ToString();
  result += " unbound=" + s_AceProbe[ACE_UNBOUND].ToString();
  result += " valid=" + s_AceProbe[ACE_VALID].ToString();
  return result;
 }

 static void RecordInvite(int reason)
 {
  if (!Replication.IsServer()) return;
  CheckWorld();
  Bump(s_Invite, reason);
 }

 static void RecordJoinGate(int reason)
 {
  if (!Replication.IsServer()) return;
  CheckWorld();
  Bump(s_Join, reason);
 }

 static string DescribeJoins()
 {
  CheckWorld();
  string result = "invite none=" + s_Invite[INVITE_NONE].ToString();
  result += " stale=" + s_Invite[INVITE_STALE].ToString();
  result += " far=" + s_Invite[INVITE_FAR].ToString();
  result += " ok=" + s_Invite[INVITE_OK].ToString();
  result += " | join cand=" + s_Join[JOIN_CANDIDATE].ToString();
  result += " far=" + s_Join[JOIN_FAR].ToString();
  result += " nav=" + s_Join[JOIN_NAVMESH].ToString();
  result += " road=" + s_Join[JOIN_ROAD].ToString();
  result += " zone=" + s_Join[JOIN_ZONE].ToString();
  result += " interior=" + s_Join[JOIN_INTERIOR].ToString();
  result += " ok=" + s_Join[JOIN_OK].ToString();
  return result;
 }

 // A routine needing a table was selected. Sits upstream of every station gate,
 // so table_wanted=0 with table_started=0 is a selection problem and
 // table_wanted>0 with table_started=0 is a station problem.
 static void RecordTableWanted()
 {
  if (!Replication.IsServer()) return;
  CheckWorld();
  if (s_TableWanted < LIMIT) s_TableWanted++;
 }

 // Selection is step modulo the eligible count, so a resident that never gets
 // past a low step can only ever reach the first few slots of its role block.
 static void RecordStep(int step)
 {
  if (!Replication.IsServer()) return;
  CheckWorld();
  if (step > s_MaxStep) s_MaxStep = step;
 }

 // The largest household seen. A second seat is impossible unless this is 2 or
 // more, so a failing seat check must report whether the world offered one.
 static void RecordHousehold(int residents)
 {
  if (!Replication.IsServer()) return;
  CheckWorld();
  if (residents > s_MaxHomeResidents) s_MaxHomeResidents = residents;
 }

 // A routine was admitted, and how many stops it planned. Sits upstream of every
 // stop counter, so planned>0 with legs=0 is an admission or geometry problem
 // and planned==routines with legs==routines is the chain never forming.
 static void RecordRoutineStarted(int planned)
 {
  if (!Replication.IsServer()) return;
  CheckWorld();
  if (s_RoutinesPlanned < LIMIT) s_RoutinesPlanned++;
  int intended = planned;
  if (intended < 1) intended = 1;
  if (intended > STOP_SLOTS) intended = STOP_SLOTS;
  if (s_PlannedStops < LIMIT) s_PlannedStops += intended;
 }

 // One occupation was genuinely entered, at leg index `leg`, after walking
 // `travel` metres. Travel comes from the per-leg m_ApproachTravel, which already
 // discards per-tick jumps over three metres as teleports.
 static void RecordStop(int leg, float travel)
 {
  if (!Replication.IsServer()) return;
  CheckWorld();
  if (s_Legs < LIMIT) s_Legs++;
  Bump(s_Stops, leg);
  int metres = Math.Round(travel);
  if (metres < 0) metres = 0;
  if (s_StopTravel < LIMIT) s_StopTravel += metres;
  if (s_StopTravelCount < LIMIT) s_StopTravelCount++;
  if (metres > s_MaxStopTravel) s_MaxStopTravel = metres;
  // A routine still in flight has entered leg+1 stops. Without this, max_stops
  // reports only routines that have already closed, so a live four-stop chain
  // reads as max_stops=1 for its whole duration.
  if (leg + 1 > s_MaxStops) s_MaxStops = leg + 1;
 }

 // A routine closed. Recorded exactly once, from EAC_CivilianActivity.FinishRoutine.
 static void RecordRoutineFinished(int stops, float travel, float seconds)
 {
  if (!Replication.IsServer()) return;
  CheckWorld();
  if (s_Routines < LIMIT) s_Routines++;
  if (stops > 1 && s_MultiStop < LIMIT) s_MultiStop++;
  if (stops > s_MaxStops) s_MaxStops = stops;
  int elapsed = Math.Round(seconds);
  if (elapsed < 0) elapsed = 0;
  if (s_RoutineSeconds < LIMIT) s_RoutineSeconds += elapsed;
  int walked = Math.Round(travel);
  if (walked < 0) walked = 0;
  if (s_RoutineTravel < LIMIT) s_RoutineTravel += walked;
 }

 // A leg transition was postponed by the one-per-tick token. Near zero means the
 // ceiling is not binding; comparable with legs means chains are being starved.
 static void RecordLegDeferred()
 {
  if (!Replication.IsServer()) return;
  CheckWorld();
  if (s_LegDeferred < LIMIT) s_LegDeferred++;
 }

 // A continuous span with no activity, no emergence, no alarm and no group
 // waypoint just ended. This is the run-69 quantity, and it is measurable only
 // because it reads IdleSince rather than the watchdog's own LastBusyAt.
 static void RecordIdleSpan(float seconds)
 {
  if (!Replication.IsServer()) return;
  CheckWorld();
  int span = Math.Round(seconds);
  if (span < 0) span = 0;
  if (s_IdleSpans < LIMIT) s_IdleSpans++;
  if (s_IdleSeconds < LIMIT) s_IdleSeconds += span;
  if (span > s_MaxIdleSeconds) s_MaxIdleSeconds = span;
 }

 static int GetRoutines() { CheckWorld(); return s_Routines; }
 static int GetLegs() { CheckWorld(); return s_Legs; }
 // Stops entered as the FIRST stop of a routine; GetLegs() minus this is every stop a
 // routine chained on after its first.
 static int GetFirstStops()
 {
  CheckWorld();
  if (s_Stops.IsEmpty()) return 0;
  return s_Stops[0];
 }
 static int GetMultiStopRoutines() { CheckWorld(); return s_MultiStop; }
 static int GetMaxStops() { CheckWorld(); return s_MaxStops; }
 static int GetMaxStopTravel() { CheckWorld(); return s_MaxStopTravel; }
 static int GetMaxIdleSeconds() { CheckWorld(); return s_MaxIdleSeconds; }
 static int GetIdleSpans() { CheckWorld(); return s_IdleSpans; }

 static float GetStopTravelAverage()
 {
  CheckWorld();
  if (s_StopTravelCount <= 0) return 0;
  float total = s_StopTravel;
  float count = s_StopTravelCount;
  return total / count;
 }

 // One '+=' per term. A single long '+' chain fails Enforce compilation with
 // "Formula too complex".
 static string DescribeRoutines()
 {
  CheckWorld();
  string result = "routines=" + s_Routines.ToString();
  result += "/" + s_RoutinesPlanned.ToString();
  result += " legs=" + s_Legs.ToString();
  result += " multi=" + s_MultiStop.ToString();
  result += " max_stops=" + s_MaxStops.ToString();
  int plannedAverage = 0;
  if (s_RoutinesPlanned > 0) plannedAverage = s_PlannedStops / s_RoutinesPlanned;
  result += " plan_avg=" + plannedAverage.ToString();
  result += " stops=";
  for (int slot = 0; slot < STOP_SLOTS; slot++)
  {
   if (slot > 0) result += "/";
   result += s_Stops[slot].ToString();
  }
  int averageTravel = 0;
  if (s_StopTravelCount > 0) averageTravel = s_StopTravel / s_StopTravelCount;
  result += " stop_travel_m=" + averageTravel.ToString();
  result += "/" + s_MaxStopTravel.ToString();
  int averageSeconds = 0;
  if (s_Routines > 0) averageSeconds = s_RoutineSeconds / s_Routines;
  result += " routine_s=" + averageSeconds.ToString();
  int averageRoutineTravel = 0;
  if (s_Routines > 0) averageRoutineTravel = s_RoutineTravel / s_Routines;
  result += " routine_m=" + averageRoutineTravel.ToString();
  int averageIdle = 0;
  if (s_IdleSpans > 0) averageIdle = s_IdleSeconds / s_IdleSpans;
  result += " idle_avg=" + averageIdle.ToString();
  result += " idle_max=" + s_MaxIdleSeconds.ToString();
  result += " idle_spans=" + s_IdleSpans.ToString();
  result += " leg_deferred=" + s_LegDeferred.ToString();
  // Audit item 1. rest_gap is a routine that produced supply (a stop, a point or
  // a station) and therefore pays RoutineGap; rest_interval is the "no supply at
  // all" case, the only one that still consults ActivityInterval. rest_max is the
  // longest rest handed out and is what the 120 s owner rule is read off.
  result += " rest_gap=" + s_RestSupplied.ToString();
  result += " rest_interval=" + s_RestNoSupply.ToString();
  result += " rest_max=" + s_MaxRestSeconds.ToString();
  result += " beginleg_orders=" + s_BeginLeg[BLG_ORDERS].ToString();
  result += " beginleg_anim=" + s_BeginLeg[BLG_ANIM].ToString();
  result += " beginleg_cleared=" + s_BeginLeg[BLG_ORDERS_CLEARED].ToString();
  result += " beginleg_own=" + s_BeginLeg[BLG_OWNERSHIP].ToString();
  result += " beginleg_ctrl=" + s_BeginLeg[BLG_CONTROLLER].ToString();
  result += " beginleg_stand=" + s_BeginLeg[BLG_STANDING].ToString();
  result += " beginleg_anchor=" + s_BeginLeg[BLG_ANCHOR].ToString();
  result += " beginleg_station=" + s_BeginLeg[BLG_STATION].ToString();
  result += " root_deferred=" + s_RootDeferred.ToString();
  result += " scene_retry=" + s_SceneRetries.ToString();
  result += " scene_repeat=" + s_SceneRepeats.ToString();
  result += " anchor_deferred=" + s_AnchorDeferred.ToString();
  result += " anchor_swept=" + s_AnchorSwept.ToString();
  // Carried on the routines line itself so every print site of DescribeRoutines
  // (the module's debug summary and the fixture's MEASURED lines) shows why
  // approaches were aborted, without adding a print site elsewhere.
  result += " | ";
  result += DescribeAborts();
  return result;
 }

 // Bounds checked, like every other Get* in this class: an out-of-range kind
 // reads past the array rather than returning nothing (audit nit).
 static int GetAttempts(EAC_EAnchorKind kind)
 {
  CheckWorld();
  if (kind < 0 || kind >= EAC_RoutineAnchors.KINDS) return 0;
  return s_Attempts[kind];
 }

 static int GetAccepts(EAC_EAnchorKind kind)
 {
  CheckWorld();
  if (kind < 0 || kind >= EAC_RoutineAnchors.KINDS) return 0;
  return s_Accepts[kind];
 }

 // "accepted/attempted" per anchor kind. A kind that never accepts means real
 // towns lack that geometry and those routine slots are degrading to open ground.
 static string Describe()
 {
  CheckWorld();
  string result = "anchors";
  for (int kind = 0; kind < EAC_RoutineAnchors.KINDS; kind++)
  {
   string name = EAC_RoutineAnchors.ShortName(kind);
   result += " " + name + "=" + s_Accepts[kind].ToString();
   result += "/" + s_Attempts[kind].ToString();
   result += "w" + s_Wanted[kind].ToString();
  }
  return result;
 }

 static string DescribeIndoor()
 {
  CheckWorld();
  string result = "fallback_anchor=" + s_Fallback.ToString();
  result += " indoor_spawn=" + s_IndoorAdmitted.ToString();
  result += "/" + (s_IndoorAdmitted + s_IndoorRejected).ToString();
  result += " emerged=" + s_Emerged.ToString();
  result += " emerge_failed=" + s_EmergeFailed.ToString();
  result += " emerge_move_failed=" + s_EmergeMoveFailed.ToString();
  result += " shelter_probes=" + s_ShelterProbes.ToString();
  result += " shelter_probes_denied=" + s_ShelterProbesDenied.ToString();
  result += " cover_timeout=" + s_CoverTimeouts.ToString();
  result += " cover_retry=" + s_CoverRetries.ToString();
  result += " door_homes=" + EAC_DoorIndex.CountKnownHomes().ToString();
  result += " idle_recovered=" + s_IdleRecovered.ToString();
  result += " table_wanted=" + s_TableWanted.ToString();
  result += " table_started=" + s_TableStarted.ToString();
  result += " table_joined=" + s_TableJoined.ToString();
  result += " table_invited=" + s_TableInvited.ToString();
  result += " max_home=" + s_MaxHomeResidents.ToString();
  result += " max_step=" + s_MaxStep.ToString();
  // Carried on the existing indoor line so the optional ACE probe is visible in
  // every campaign log without adding another print site.
  result += " | ";
  result += DescribeAceProbe();
  return result;
 }
}
