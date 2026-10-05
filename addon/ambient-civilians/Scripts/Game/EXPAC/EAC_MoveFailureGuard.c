// One vanilla hook per AI group, so a native move that cannot be pathed ends the
// order instead of the mission's scripts.
//
// Vanilla's group tree runs SCR_AIProcessFailedMovementResult when a move fails.
// For EMoveError.UNKNOWN on a waypoint-related move it first invokes
// SCR_AIGroupUtilityComponent.OnMoveFailed (SCR_AIProcessFailedMovementResult.c:78)
// and then, if the current activity still holds its m_RelatedWaypoint, calls
// NodeErrorOnce (:99), which is Debug.Error (NodeError.c:8) with no WORKBENCH
// guard - a Virtual Machine Exception on the Diag executables (runs 83 and 85) -
// and parks the node in RUNNING for ever (:100-101). If the activity's
// m_RelatedWaypoint is already null it fails the action and returns FAIL with no
// error at all (:85-95); that is vanilla's own path for "the waypoint went away
// under a running move", and SCR_AIActivityBase.Fail(bool) nulls the same public
// field deliberately (SCR_AIActivity.c:75-83). This class subscribes to the
// invoker once per group and takes that path on the group's behalf.
//
// Nothing else is touched inside the handler: no Fail(), no RemoveWaypoint, no
// delete. The node reads GetCurrentAction() after the invoker returns (:85), and
// anything that changes the current action there puts the error back. The
// waypoint stays on the group; its owner (EAC_CivilianActivity, EAC_PedestrianWalk,
// a traffic party) sees the pending failure on its next monitor tick, removes the
// order and retires the destination. Until then vanilla may re-issue the same
// move once per group evaluation; every repeat is defused the same way.
//
// Not every UNKNOWN is a failure of ours to defuse (run 93 diagnosis). Vanilla
// itself nulls m_RelatedWaypoint when a waypoint is removed under a running
// move (SCR_AIGroupUtilityComponent.OnWaypointToRemove:519-529 ->
// SCR_AIWaypointState.OnDeselected -> CancelActivitiesRelatedToWaypoint(...,
// doNotCompleteWaypoint: true) -> SCR_AIActivityBase.Fail(true)), and the node
// then reports the abandoned move as UNKNOWN on its next tick and takes the
// graceful branch on its own (:87-95). This addon removes its own orders under
// running moves all the time (a wander leg stopped short of its completion
// radius, an approach stopped at 1 m, a shelter order cleared on arrival), so
// the handler classifies before it charges: REMOVED when the field was already
// null, STALE when the failed location is not the current order's, DEFUSED
// only when it actually stood between vanilla and its NodeError line. Only a
// DEFUSED failure is left pending for the order's owner.
//
// Bounded: one guard per group, MAX_GUARDS guards, MAX_RETIRED positions, all
// reset per world. No timer, no scan. Traffic crews attach with the same call:
// EAC_MoveFailureGuard.Attach(party.Group, "traffic") after the group is spawned.
// Vanilla SCR_AIProcessFailedMovementResult.EOnTaskSimulate reads
// m_Group.GetLeaderEntity().GetOrigin() without a null check (line 59). A group whose
// last member has just been cached, deleted or moved to another group while one of
// its moves is failing has no leader, and that line is then a Virtual Machine
// Exception on the server (seen 2026-09-19 in the Activities fixture the moment a
// resident was reassigned mid-approach). A group with nobody in it has no move to
// process, so the node simply fails for that tick. Every group with a leader goes
// through vanilla unchanged.
modded class SCR_AIProcessFailedMovementResult
{
 override ENodeResult EOnTaskSimulate(AIAgent owner, float dt)
 {
  if (m_Group && !m_Group.GetLeaderEntity()) return ENodeResult.FAIL;
  return super.EOnTaskSimulate(owner, dt);
 }
}

class EAC_MoveFailureGuard
{
 protected static const int MAX_GUARDS = 256;
 protected static const int MAX_RETIRED = 128;
 // Diagnostic lines per world at DebugLevel >= 2; nothing at the stock level.
 protected static const int MAX_TRACES = 40;
 // A start or destination further than this from its navmesh projection is
 // reported off-mesh in the trace (the same tolerance CanApproach applies).
 protected static const float NAV_TOLERANCE = 0.5;
 // A retired position also blocks anything this close to it in the ground plane:
 // a probe that lands 0.5 m from a failed surveyed spot is the same place.
 protected static const float RETIRED_RADIUS = 1.5;
 // How far the failed move location may sit from the order it is charged to.
 // Vanilla reports the waypoint position plus a few centimetres of Y.
 protected static const float MATCH_RADIUS = 3;
 // How long after the first guard attaches the invoker may stay silent before
 // that silence is itself reported. Five minutes of ambient traffic and routine
 // walking on any populated terrain produces move failures; none at all means the
 // subscription is not being raised.
 static const float SILENCE_WINDOW_SECONDS = 300;
 protected static ref array<ref EAC_MoveFailureGuard> s_Guards = {};
 protected static ref array<vector> s_Retired = {};
 protected static BaseWorld s_World;
 protected static int s_Traces;
 // Mission-level evidence that the vanilla hook this whole class depends on is
 // actually live. s_Fired counts every OnMoveFailed entry, before any
 // classification, so it is zero only when SCR_AIGroupUtilityComponent never
 // raised GetOnMoveFailed() at all. A replacement AI mod that overrides that
 // component without raising the invoker (the CRX Enfusion A.I. family does this)
 // silently disables the guard and hands vanilla's unguarded NodeError - a
 // Virtual Machine Exception on the Diag executables - back to the mission.
 // Nothing here changes behaviour; it makes an otherwise invisible failure
 // reportable.
 protected static int s_Fired;
 protected static float s_FirstAttachAt;
 // Plain pointers: Enforce nulls them when the native objects are deleted, and
 // the registry's ref keeps this object alive for the invoker's delegate.
 protected SCR_AIGroup m_Group;
 protected SCR_AIGroupUtilityComponent m_Utility;
 protected AIWaypoint m_FailedWaypoint;
 protected vector m_FailedLocation;
 protected int m_FailedResult;
 protected bool m_Pending;
 // Which owner issued the group's latest order ("approach", "walk", "shelter",
 // "traffic"); diagnostic only, stamped by every Attach caller.
 protected string m_Kind;

 protected static void CheckWorld()
 {
  BaseWorld world = GetGame().GetWorld();
  if (world == s_World) return;
  s_Guards.Clear(); s_Retired.Clear(); s_Traces = 0; s_World = world;
  s_Fired = 0; s_FirstAttachAt = 0;
 }

 // Idempotent per group. Returns the group's guard so the caller can poll it in
 // O(1); the registry lookup itself is a bounded linear pass made once per order,
 // never per tick. Deleted or confirmed cached groups leave reusable slots.
 // `kind` names the caller for the diagnostic trace and nothing else.
 static EAC_MoveFailureGuard Attach(SCR_AIGroup group, string kind = "")
 {
  if (!Replication.IsServer() || !group) return null;
  CheckWorld();
  int dead = -1;
  for (int index = 0; index < s_Guards.Count(); index++)
  {
   EAC_MoveFailureGuard existing = s_Guards[index];
   // Audit S9. A guard whose group has been deleted is NOT removed any more.
   // EAC_CivilianActivity, EAC_PedestrianWalk, EAC_RoutineEmerge and
   // EAC_CivilianShelter each hold a plain pointer into this registry, and the
   // registry's ref is the only thing keeping the object alive: dropping it here
   // left every one of those owners pointing at freed memory until its own next
   // tick. The slot is remembered and rebound below instead, and the four owners
   // now also clear their pointer when their order ends.
   if (!existing.m_Group) { if (dead < 0) dead = index; continue; }
   if (existing.m_Group != group) continue;
   // A fresh order supersedes any failure left over from the previous one.
   existing.m_Pending = false;
   existing.m_Kind = kind;
   return existing;
  }
  SCR_AIGroupUtilityComponent utility = SCR_AIGroupUtilityComponent.Cast(group.FindComponent(SCR_AIGroupUtilityComponent));
  if (!utility) return null;
  if (s_FirstAttachAt <= 0 && GetGame() && GetGame().GetWorld()) s_FirstAttachAt = GetGame().GetWorld().GetWorldTime() * 0.001;
  if (dead >= 0)
  {
   EAC_MoveFailureGuard recycled = s_Guards[dead];
   recycled.m_Group = group;
   recycled.m_FailedWaypoint = null;
   recycled.m_Pending = false;
   recycled.m_Kind = kind;
   recycled.Subscribe(utility);
   EAC_RoutineStats.RecordMoveFailure(EAC_RoutineStats.MOVE_ATTACHED);
   return recycled;
  }
  if (s_Guards.Count() >= MAX_GUARDS) return null;
  EAC_MoveFailureGuard guard = new EAC_MoveFailureGuard();
  guard.m_Group = group;
  guard.m_Kind = kind;
  guard.Subscribe(utility);
  s_Guards.Insert(guard);
  EAC_RoutineStats.RecordMoveFailure(EAC_RoutineStats.MOVE_ATTACHED);
  return guard;
 }

 // Automatic cache admission confirms deletion and suspended capacity first;
 // its callers detach their order owners in the same scheduler step. Keep the
 // guard object alive, but release the empty retained group's subscription.
 static void ReleaseCachedGroup(SCR_AIGroup group)
 {
  if (!Replication.IsServer() || !group || group.GetAgentsCount() != 0) return;
  CheckWorld();
  foreach (EAC_MoveFailureGuard guard : s_Guards)
  {
   if (guard.m_Group != group) continue;
   guard.Unsubscribe();
   guard.m_Group = null;
   guard.m_FailedWaypoint = null;
   guard.m_Pending = false;
   guard.m_Kind = "";
   return;
  }
 }

 // Same shape as vanilla's own subscription in SCR_AIGroupUtilityComponent.EOnInit
 // (m_Owner.GetOnAgentAdded().Insert(OnAgentAdded)): an instance method handed to
 // the invoker from inside the instance.
 protected void Subscribe(SCR_AIGroupUtilityComponent utility)
 {
  Unsubscribe();
  m_Utility = utility;
  m_Utility.GetOnMoveFailed().Insert(OnMoveFailed);
 }

 protected void Unsubscribe()
 {
  if (m_Utility) m_Utility.GetOnMoveFailed().Remove(OnMoveFailed);
  m_Utility = null;
 }

 // Fires from SCR_AIProcessFailedMovementResult.EOnTaskSimulate on the group's
 // own tick, after the WAITING_ON_NAVLINK early return (:72-75) and before the
 // node chooses between the graceful branch and NodeError.
 void OnMoveFailed(int moveResult, IEntity vehicleUsed, bool isWaypointRelated, vector moveLocation)
 {
  if (s_Fired < 100000) s_Fired++;
  m_FailedLocation = moveLocation; m_FailedResult = moveResult; m_Pending = true;
  m_FailedWaypoint = null;
  if (m_Group) m_FailedWaypoint = m_Group.GetCurrentWaypoint();
  // Ground-plane-free distance from the failed location to the order this
  // failure would be charged to; -1 when the group holds no current order.
  float currentDistance = -1;
  if (m_FailedWaypoint) currentDistance = vector.Distance(m_FailedWaypoint.GetOrigin(), moveLocation);
  // -1: the activity was not examined (not the UNKNOWN branch, or no activity).
  int alreadyNull = -1;
  // Every other result already has a graceful vanilla branch (:113-149:
  // complete the waypoint, fail the action, or leave the vehicle and retry).
  if (moveResult != EMoveError.UNKNOWN || !isWaypointRelated)
  {
   EAC_RoutineStats.RecordMoveFailure(EAC_RoutineStats.MOVE_OTHER);
   TraceFailure(moveResult, isWaypointRelated, moveLocation, alreadyNull, currentDistance);
   return;
  }
  EAC_RoutineStats.RecordMoveFailure(EAC_RoutineStats.MOVE_UNKNOWN);
  if (!m_Group) return;
  SCR_AIGroupUtilityComponent utility = SCR_AIGroupUtilityComponent.Cast(m_Group.FindComponent(SCR_AIGroupUtilityComponent));
  if (!utility) return;
  // The exact object :85 will read. No activity means :88 cannot take the
  // graceful branch and the error fires regardless; it is counted as undefused.
  SCR_AIActivityBase activity = SCR_AIActivityBase.Cast(utility.GetCurrentAction());
  if (!activity)
  {
   TraceFailure(moveResult, isWaypointRelated, moveLocation, alreadyNull, currentDistance);
   return;
  }
  alreadyNull = 0;
  if (!activity.m_RelatedWaypoint) alreadyNull = 1;
  activity.m_RelatedWaypoint = null;
  if (alreadyNull == 1)
  {
   // Vanilla's own removed-waypoint branch (:87-95): the order this move served
   // was taken off the group before the node reported, and Fail(true) had
   // already nulled the field. There was nothing to defuse, and m_FailedWaypoint
   // is whatever order is current now, not the one that went away - charging it
   // would abort a good stop for a failure that was ours.
   EAC_RoutineStats.RecordMoveFailure(EAC_RoutineStats.MOVE_REMOVED);
   m_Pending = false;
   TraceFailure(moveResult, isWaypointRelated, moveLocation, alreadyNull, currentDistance);
   return;
  }
  if (m_FailedWaypoint && currentDistance > MATCH_RADIUS)
  {
   // The field was live, so the null above did keep vanilla off its error line,
   // but the failed move was not the current order's: a previous order's result
   // arriving after the next order was issued. Nothing to charge.
   EAC_RoutineStats.RecordMoveFailure(EAC_RoutineStats.MOVE_STALE);
   m_Pending = false;
   TraceFailure(moveResult, isWaypointRelated, moveLocation, alreadyNull, currentDistance);
   return;
  }
  EAC_RoutineStats.RecordMoveFailure(EAC_RoutineStats.MOVE_DEFUSED);
  TraceFailure(moveResult, isWaypointRelated, moveLocation, alreadyNull, currentDistance);
 }

 // One diagnostic line per failure, capped per world and printed only at
 // DebugLevel >= 2 (the static mirror; this class holds no module). start_nav
 // and dest_nav say whether the leader's position and the failed location each
 // project onto the navmesh within NAV_TOLERANCE, which is the question the
 // run-93 diagnosis could not answer from the counters alone. Every value is a
 // local before the PrintFormat, and there are exactly nine of them.
 protected void TraceFailure(int moveResult, bool isWaypointRelated, vector moveLocation, int alreadyNull, float currentDistance)
 {
  if (s_Traces >= MAX_TRACES) return;
  int level = EAC_AmbientModule.GetDebugLevelMirror();
  if (level < 2) return;
  s_Traces++;
  string kind = m_Kind;
  vector from = vector.Zero;
  int startNav = -1;
  int destNav = -1;
  if (m_Group)
  {
   IEntity leader = m_Group.GetLeaderEntity();
   if (leader)
   {
    from = leader.GetOrigin();
    RplComponent replica = RplComponent.Cast(leader.FindComponent(RplComponent));
    if (replica) kind += string.Format(" actor_rpl=%1", replica.Id());
   }
   AIPathfindingComponent path = AIPathfindingComponent.Cast(m_Group.FindComponent(AIPathfindingComponent));
   if (path)
   {
    vector projected;
    startNav = 0;
    if (leader && path.GetClosestPositionOnNavmesh(from, "0.5 1 0.5", projected) && vector.Distance(from, projected) <= NAV_TOLERANCE) startNav = 1;
    destNav = 0;
    if (path.GetClosestPositionOnNavmesh(moveLocation, "0.5 1 0.5", projected) && vector.Distance(moveLocation, projected) <= NAV_TOLERANCE) destNav = 1;
   }
  }
  PrintFormat("[EAC move failed] kind=%1 result=%2 wp_related=%3 already_null=%4 current_wp_m=%5 from=%6 to=%7 start_nav=%8 dest_nav=%9", kind, moveResult, isWaypointRelated, alreadyNull, currentDistance, from, moveLocation, startNav, destNav);
 }

 // The owner of `order` asks whether its move failed since it was issued. True
 // consumes the record. `permanent` says the destination itself is the problem
 // (no path, unreachable) rather than a transient stop or a stuck body, and is
 // what decides whether the position is retired.
 bool TakeFailure(AIWaypoint order, out vector location, out bool permanent)
 {
  location = m_FailedLocation; permanent = false;
  if (!m_Pending || !order) return false;
  bool matches = m_FailedWaypoint == order;
  if (!matches) matches = vector.Distance(order.GetOrigin(), m_FailedLocation) < MATCH_RADIUS;
  if (!matches) return false;
  m_Pending = false;
  permanent = m_FailedResult == EMoveError.UNKNOWN || m_FailedResult == EMoveError.UNREACHABLE;
  return true;
 }

 // A position the native pathing could not reach this mission. Consulted by
 // EAC_CivilianActivity.CanApproach, which every stop destination passes through
 // (station slot, surveyed spot, anchor probe, legacy ring), so the same place is
 // never ordered again. Oldest entry drops first past the cap.
 static void Retire(vector position)
 {
  if (!Replication.IsServer()) return;
  CheckWorld();
  if (IsRetired(position)) return;
  if (s_Retired.Count() >= MAX_RETIRED) s_Retired.RemoveOrdered(0);
  s_Retired.Insert(position);
  EAC_RoutineStats.RecordMoveFailure(EAC_RoutineStats.MOVE_RETIRED);
 }

 static bool IsRetired(vector position)
 {
  CheckWorld();
  foreach (vector retired : s_Retired)
  {
   if (vector.DistanceXZ(retired, position) < RETIRED_RADIUS) { return true; }
  }
  return false;
 }

 // The last native move result `group` reported and where, or -1 when none is
 // pending. Read-only: nothing is consumed, so an owner's TakeFailure is
 // unaffected. One bounded registry pass, made only for the rare traffic stop
 // line of a crew that left a moving car (EAC_TrafficDirector, DRIVE phase).
 static int PeekResult(SCR_AIGroup group, out vector location)
 {
  location = vector.Zero;
  if (!Replication.IsServer() || !group) return -1;
  CheckWorld();
  foreach (EAC_MoveFailureGuard guard : s_Guards)
  {
   if (guard.m_Group != group) continue;
   location = guard.m_FailedLocation;
   if (!guard.m_Pending) return -1;
   return guard.m_FailedResult;
  }
  return -1;
 }

 static int GuardCount() { CheckWorld(); return s_Guards.Count(); }
 static int RetiredCount() { CheckWorld(); return s_Retired.Count(); }
 static int FiredCount() { CheckWorld(); return s_Fired; }

 // True when guards have been attached, SILENCE_WINDOW_SECONDS have passed since
 // the first one, and the vanilla invoker has still never fired. On a populated
 // terrain that combination means SCR_AIGroupUtilityComponent is not raising
 // GetOnMoveFailed() - almost always a replacement AI mod overriding the
 // component - and vanilla's fatal NodeError is unguarded again. Read by the QA
 // fixture and reported on the Describe line below.
 static bool GuardNeverFired()
 {
  CheckWorld();
  if (s_Guards.IsEmpty() || s_FirstAttachAt <= 0) return false;
  if (s_Fired > 0) return false;
  if (!GetGame() || !GetGame().GetWorld()) return false;
  float elapsed = GetGame().GetWorld().GetWorldTime() * 0.001 - s_FirstAttachAt;
  return elapsed >= SILENCE_WINDOW_SECONDS;
 }

 // One '+=' per term: a long '+' chain fails Enforce compilation.
 static string Describe()
 {
  CheckWorld();
  string result = EAC_RoutineStats.DescribeMoveFailures();
  result += " guards=" + s_Guards.Count().ToString();
  result += " retired_points=" + s_Retired.Count().ToString();
  result += " traces=" + s_Traces.ToString();
  result += " fired=" + s_Fired.ToString();
  // One flag, printed only when it is true, so an ordinary log is unchanged and
  // a broken subscription is impossible to miss.
  if (GuardNeverFired()) result += " guard_never_fired=1";
  return result;
 }
}
