// A resident created inside its own house walks out before ordinary routines
// begin. One owned move order, the same exclusion/route rules as walking, and a
// bounded probe sweep. Failure is not fatal: the resident simply stays indoors
// and remains eligible for ordinary activities and caching.
class EAC_RoutineEmerge
{
 // How long an issued emergence order may stand still before it is dropped and a
 // new doorstep is probed. It was 45 s, and the idle clock counts an emerging
 // resident as "ordered", so a failing native move here was a 45 s motionless
 // stall that no gate could see (audit item 6). A walk to a doorstep is metres.
 static const float ORDER_DEADLINE_SECONDS = 20;
 protected AIWaypoint m_Order;
 protected SCR_AIGroup m_Group;
 // The group's move-failure guard (plain pointer; the guard registry owns it).
 // Emergence used to issue native orders with no guard at all, so vanilla could
 // reach its NodeError line on an unpathable doorstep and this class never
 // learned the move had failed.
 protected EAC_MoveFailureGuard m_Guard;
 protected vector m_Destination;
 protected float m_Deadline, m_NextAttempt;
 protected int m_Probe, m_Sweeps;
 // Audit S3. One scratch array each, cleared before every fill, instead of two
 // fresh allocations per emerging resident per 2 Hz monitor tick.
 protected ref array<AIWaypoint> m_ScratchOrders = {};
 protected ref array<vector> m_ScratchRoute = {};
 protected bool m_Required, m_Finished;
 protected string m_LastReason;

 string GetState()
 {
  if (!m_Required || m_Finished) return "";
  if (m_Order) return "leaving the house";
  return "indoors";
 }

 string GetReason() { return m_LastReason; }

 // Called once at commit when the resident was placed inside its own house.
 void Require() { m_Required = true; m_Finished = false; m_Probe = 0; m_Sweeps = 0; }

 bool IsActive() { return m_Required && !m_Finished; }

 // True once emergence gave up. A failure must not seal a resident indoors for
 // the rest of the session, so the idle watchdog can re-arm it.
 bool HasFinished() { return m_Required && m_Finished; }

 void Rearm()
 {
  if (!m_Required || !m_Finished) return;
  m_Finished = false; m_Probe = 0; m_Sweeps = 0; m_NextAttempt = 0;
  m_LastReason = "re-armed after idle recovery";
 }

 void Stop()
 {
  if (m_Order)
  {
   if (m_Group) m_Group.RemoveWaypoint(m_Order);
   SCR_EntityHelper.DeleteEntityAndChildren(m_Order);
  }
  m_Order = null; m_Group = null;
  // Audit S9. EAC_MoveFailureGuard may rebind this registry slot to a different
  // group once ours is gone, so the pointer is only valid for the live order.
  m_Guard = null;
 }

 protected void Finish(string reason)
 {
  Stop();
  m_Finished = true;
  m_LastReason = reason;
  EAC_RoutineStats.RecordEmergence(reason == "emerged");
 }

 void Monitor(EAC_AmbientModule module, EAC_PedestrianActivation activation, float now)
 {
  if (!Replication.IsServer() || !IsActive()) return;
  if (!module || module != EAC_AmbientModule.GetActive() || !activation) { Stop(); return; }
  EAC_ResidentClaim claim = activation.Claim;
  IEntity actor;
  if (claim) actor = claim.Character;
  if (!claim || !claim.Committed || claim.Cache || !actor || !claim.Home.BuildingEntity || activation.PlayerTouched || claim.Resident.Dead || !claim.Resident.Wanted || module.GetResidentActivation(claim.Home, claim.Resident) != claim || !EAC_PedestrianSpawner.HasCivilianControl(actor, claim.Group))
  { Stop(); return; }
  // Danger owns the actor: abandon the walk out and let shelter respond.
  if (claim.AlarmUntil > now) { Finish("danger during emergence"); return; }
  if (CompartmentAccessComponent.GetVehicleIn(actor)) { Finish("boarded a vehicle"); return; }
  if (m_Order)
  {
   if (claim.Group != m_Group) { Finish("group replaced"); return; }
   // The native move to this doorstep failed on the AI's own tick. The guard has
   // already kept vanilla off its NodeError line; this side drops the order, and
   // a destination with no path is struck off for the mission. A failed order is
   // charged against m_Sweeps so a house whose every doorstep is unreachable
   // gives up after three rather than re-probing for the rest of the session.
   vector failedAt; bool failedForGood;
   if (m_Guard && m_Guard.TakeFailure(m_Order, failedAt, failedForGood))
   {
    if (failedForGood) EAC_MoveFailureGuard.Retire(m_Order.GetOrigin());
    Stop();
    m_Sweeps++;
    m_NextAttempt = now + 5;
    m_LastReason = "native move to the doorstep failed";
    EAC_RoutineStats.RecordEmergeMoveFailed();
    if (m_Sweeps >= 3) { Finish("no reachable doorstep: " + m_LastReason); }
    return;
   }
   m_ScratchOrders.Clear(); m_Group.GetWaypoints(m_ScratchOrders);
   if (m_ScratchOrders.Count() != 1 || m_ScratchOrders[0] != m_Order || m_Group.GetCurrentWaypoint() != m_Order) { Finish("order replaced"); return; }
   AICharacterMovementComponent movement = AICharacterMovementComponent.Cast(actor.FindComponent(AICharacterMovementComponent));
   m_ScratchRoute.Clear(); if (movement) movement.GetCurrentPath(m_ScratchRoute);
   // RouteAllowed itself skips its per-point sweep when no zone blocks transit
   // (audit B1), so this stays one call rather than a loop here.
   if (!movement || !EAC_CivilianShelter.RouteAllowed(actor.GetOrigin(), m_Destination, m_ScratchRoute)) { Finish("route rejected"); return; }
   if (vector.DistanceXZ(actor.GetOrigin(), m_Destination) <= 1.5)
   {
    // Outside means the assigned roof is no longer overhead.
    string arrival;
    if (!EAC_CivilianShelter.InteriorVolume(claim.Home.BuildingEntity, actor, actor.GetOrigin(), arrival)) { Finish("emerged"); return; }
    Finish("reached doorstep still indoors");
    return;
   }
   // A motionless order is a stall the idle clock cannot see, so it ends quickly
   // now and is charged against a sweep exactly like a reported move failure.
   if (now >= m_Deadline)
   {
    Stop();
    m_Sweeps++;
    m_NextAttempt = now + 5;
    m_LastReason = "doorstep order stood still";
    if (m_Sweeps >= 3) { Finish("no reachable doorstep: " + m_LastReason); }
   }
   return;
  }
  if (now < m_NextAttempt) return;
  m_NextAttempt = now + 1;
  m_ScratchOrders.Clear(); claim.Group.GetWaypoints(m_ScratchOrders);
  if (!m_ScratchOrders.IsEmpty()) return;
  AIPathfindingComponent path = AIPathfindingComponent.Cast(claim.Group.FindComponent(AIPathfindingComponent));
  if (!path || !path.GetNavmeshComponent()) { Finish("no navmesh component"); return; }
  int probe = m_Probe++;
  if (m_Probe >= EAC_RoutineAnchors.PROBES)
  {
   m_Probe = 0; m_Sweeps++; m_NextAttempt = now + 8;
   // Three failed sweeps: stay indoors rather than forcing an unnatural exit.
   if (m_Sweeps >= 3) { Finish("no reachable doorstep: " + m_LastReason); return; }
  }
  vector transform[4]; string reason;
  if (!EAC_RoutineAnchors.Find(EAC_EAnchorKind.DOORSTEP, module.GetWorld(), actor, claim.Home.BuildingEntity, path, probe, transform, reason))
  { m_LastReason = reason; return; }
  vector destination = transform[3];
  if (!EAC_ExclusionZone.IsTransitAllowed(actor.GetOrigin(), destination)) { m_LastReason = "exclusion between home and doorstep"; return; }
  EntitySpawnParams params = new EntitySpawnParams(); params.TransformMode = ETransformMode.WORLD;
  Math3D.MatrixIdentity4(params.Transform); params.Transform[3] = destination;
  m_Order = AIWaypoint.Cast(GetGame().SpawnEntityPrefab(Resource.Load("{750A8D1695BD6998}Prefabs/AI/Waypoints/AIWaypoint_Move.et"), module.GetWorld(), params));
  if (!m_Order) { m_LastReason = "waypoint spawn failed"; return; }
  m_Group = claim.Group; m_Destination = destination;
  m_Order.SetCompletionRadius(1.0);
  m_Group.AddWaypoint(m_Order);
  // Once per group, before the order can fail. The same guard serves the walk and
  // the activity approach on this group; Attach clears any failure left over from
  // the previous order, so this order starts clean.
  m_Guard = EAC_MoveFailureGuard.Attach(m_Group, "emerge");
  m_Group.ActivateAI(); m_Group.PreventMaxLOD(60);
  EAC_RoutineStats.RecordPinnedSeconds(60);
  AIControlComponent control = AIControlComponent.Cast(actor.FindComponent(AIControlComponent));
  if (control)
  {
   control.ActivateAI();
   AIAgent agent = control.GetAIAgent();
   if (agent)
   {
    agent.PreventMaxLOD(60);
    EAC_RoutineStats.RecordPinnedSeconds(60);
   }
  }
  m_Deadline = now + ORDER_DEADLINE_SECONDS;
  m_LastReason = "walking to " + destination.ToString();
 }

 bool EAC_ContainsSessionHelper(SCR_EditableEntityComponent candidate) { return EAC_SessionLifecycle.ContainsOwned(candidate, m_Order); }
}
