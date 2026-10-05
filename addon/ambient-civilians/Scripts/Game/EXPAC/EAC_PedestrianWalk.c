// One owned native order per resident. Advanced only by the shared scheduler.
class EAC_PedestrianWalk
{
 protected AIWaypoint m_Waypoint;
 protected SCR_AIGroup m_Group;
 // The group's move-failure guard (plain pointer; the guard registry owns it).
 protected EAC_MoveFailureGuard m_Guard;
 protected float m_Deadline, m_NextAttempt;
 protected int m_Attempt;
 // Audit S3. One scratch array per walker, cleared before every GetCurrentPath,
 // instead of a fresh allocation per resident per 2 Hz monitor tick.
 protected ref array<vector> m_ScratchRoute = {};

 void Stop()
 {
  if (m_Waypoint)
  {
   if (m_Group) m_Group.RemoveWaypoint(m_Waypoint);
   SCR_EntityHelper.DeleteEntityAndChildren(m_Waypoint);
  }
  m_Waypoint = null; m_Group = null;
  // The yield is deliberately NOT cleared here: BeginLeg stops this leg in order
  // to take the group, and clearing it would let Step issue a replacement on the
  // very next tick - the livelock again.
  // Audit S9. EAC_MoveFailureGuard's registry may rebind this slot to another
  // group once ours is gone; the guard is only ever valid for the order that was
  // live when Attach returned it.
  m_Guard = null;
 }

 // Nearest segment in ground plane, retaining the pedestrian's side of the road.
 static bool Shoulder(array<vector> points, float width, vector from, bool reverse, out vector candidate)
 {
  if (!points || points.Count() < 2 || points.Count() > 256 || width <= 0 || width > 30) return false;
  float closest = 1000000000;
  vector center, direction;
  for (int i = 1; i < points.Count(); i++)
  {
   vector delta = points[i] - points[i - 1]; delta[1] = 0;
   float lengthSquared = delta.LengthSq();
   if (lengthSquared < 0.01) continue;
   vector offset = from - points[i - 1]; offset[1] = 0;
   float factor = Math.Clamp(vector.Dot(offset, delta) / lengthSquared, 0, 1);
   vector projected = points[i - 1] + delta * factor;
   offset = from - projected; offset[1] = 0;
   if (offset.LengthSq() >= closest) continue;
   closest = offset.LengthSq(); center = projected; direction = delta.Normalized();
  }
  if (closest > 400) return false;
  vector side = Vector(-direction[2], 0, direction[0]);
  if (vector.Dot(from - center, side) < 0) side = -side;
  if (reverse) direction = -direction;
  candidate = center + side * (width * 0.5 + 1.5) + direction * 10;
  return vector.Distance(from, candidate) <= 20;
 }

 // Clears only the walk backoff timer so a recovered resident can try again
 // immediately. m_Attempt is deliberately left alone: the scheduler visits a
 // resident once per tracked*0.5 s, so wiping the sweep index on every
 // IdleRecoverySeconds recovery pinned m_Attempt below 9 forever, made the 7.5 m
 // short-leg fallback (m_Attempt >= 9) unreachable and made walk_exhausted
 // structurally zero. m_Attempt is still reset where a leg genuinely ends, in
 // Monitor, and at the exhaustion reset in the m_Attempt > 14 branch.
 void ResetBackoff() { m_NextAttempt = 0; }

 // Keep the walker from issuing a leg before `until`. EAC_CivilianActivity.BeginLeg
 // refuses a start while an order is live and TryActivity retries it after
 // START_RETRY_SECONDS; Stop() alone leaves m_NextAttempt where it was, so a new
 // wander leg could be issued a second later and the retry would refuse again.
 // Never shortens an existing backoff; ResetBackoff still clears it.
 void HoldUntil(float until) { if (until > m_NextAttempt) m_NextAttempt = until; }

 // Campaign 118 / Danger fixture 23/1: HoldUntil is NOT a hold. It writes
 // m_NextAttempt, and two other paths overwrite that field outright - Monitor
 // assigns `now + RandomFloat(5, 15)` on both of its leg-end branches, and
 // ResetBackoff (reached from SetGoal on any goal move over 2 m, which the
 // scheduler evaluates every tick) zeroes it. So the 30 s hold
 // EAC_CivilianActivity.BeginLeg sets when it refuses on a live order was being
 // erased within seconds, the walker issued a fresh leg, and the 20 s activity
 // retry found the group busy again: the ladder shows walk_ok rising 4 -> 5
 // twenty seconds inside a hold, with next_activity_in cycling
 // 2.8 -> 17.8 -> 12.8 -> 7.7 -> 2.6 -> 17.6 and beginleg_orders ticking once per
 // cycle. A routine start and a wander leg chasing each other, indefinitely.
 //
 // m_YieldUntil is a separate field that nothing else writes, checked in Step
 // before the backoff is consulted at all, so neither clobber can defeat it.
 protected float m_YieldUntil;
 void YieldTo(float until) { if (until > m_YieldUntil) m_YieldUntil = until; }
 void ClearYield() { m_YieldUntil = 0; }

 // True when every order currently on the group is this walker's own leg, so the
 // caller knows it may clear the group by stopping the walk and nothing else.
 bool OwnsOnly(array<AIWaypoint> orders)
 {
  if (!m_Waypoint || !orders || orders.IsEmpty()) return false;
  foreach (AIWaypoint order : orders)
  {
   if (order != m_Waypoint) return false;
  }
  return true;
 }

 // Drift toward the next scene instead of wandering a random ring. The goal only
 // biases candidate generation; every gate after it is unchanged.
 protected vector m_Goal;
 protected bool m_HasGoal;
 void SetGoal(vector goal)
 {
  if (!m_HasGoal || vector.Distance(m_Goal, goal) > 2) { m_Goal = goal; m_HasGoal = true; ResetBackoff(); }
 }
 void ClearGoal() { m_HasGoal = false; }

 // `refusal` is the EAC_RoutineStats WLK_ reason for a false return, so Step can
 // tell the three things this function refuses for apart: an exclusion zone
 // (WLK_ZONE), the destination's own body volume (WLK_DEST), and the five
 // interior samples, which fail either on the navmesh (WLK_SAMPLE) or on body
 // clearance along the line (WLK_CLEAR).
 //
 // Audit S5. WLK_NAV_START and WLK_BODY below are NOT refusals and never reach
 // `refusal`: they are observations recorded on legs this function goes on to
 // accept, which is why they are recorded directly rather than returned.
 // -1 when the position is clear, otherwise the EAC_RoutineStats CLR_ reason.
 // EAC_PedestrianSpawner returns one GEOMETRY verdict for both the height rule
 // and the body box; a single GetSurfaceY separates them, paid only on a position
 // that already failed.
 protected static int ClearanceReason(BaseWorld world, vector position, IEntity exclude)
 {
  int reason = EAC_PedestrianSpawner.GetNavmeshClearReason(world, position, exclude);
  if (reason == EAC_ESpawnReason.NONE) return -1;
  if (reason == EAC_ESpawnReason.WATER) return EAC_RoutineStats.CLR_WATER;
  if (!world) return EAC_RoutineStats.CLR_BOX;
  float gap = Math.AbsFloat(position[1] - world.GetSurfaceY(position[0], position[2]));
  if (gap > EAC_PedestrianSpawner.NAVMESH_HEIGHT_TOLERANCE) return EAC_RoutineStats.CLR_HEIGHT;
  return EAC_RoutineStats.CLR_BOX;
 }

 protected bool ClearLeg(IEntity actor, IEntity home, AIPathfindingComponent path, vector destination, out int refusal)
 {
  refusal = EAC_RoutineStats.WLK_CLEAR;
  BaseWorld world = actor.GetWorld();
  vector start = actor.GetOrigin();
  // The leg starts where the actor stands. Off the navmesh by more than half a
  // metre (a pose exit, a doorstep, furniture) the native move has nowhere to
  // start from, which is one candidate cause of run 93's UNKNOWN results.
  // Counted, NOT refused, since campaign 96 (see EAC_CivilianActivity.CanApproach):
  // a native move that truly cannot start is defused by EAC_MoveFailureGuard, so
  // this is an observation on an otherwise-accepted leg and never sets `refusal`
  // (audit S5). Same tolerance as EAC_CivilianActivity.CanApproach.
  vector startProjected;
  if (!path.GetClosestPositionOnNavmesh(start, "0.5 1 0.5", startProjected) || vector.Distance(start, startProjected) > 0.5) EAC_RoutineStats.RecordWalk(EAC_RoutineStats.WLK_NAV_START);
  refusal = EAC_RoutineStats.WLK_ZONE;
  if (!EAC_ExclusionZone.IsTransitAllowed(start, destination) || !EAC_ExclusionZone.IsPopulationAllowed(destination)) return false;
  refusal = EAC_RoutineStats.WLK_DEST;
  if (EAC_MoveFailureGuard.IsRetired(destination)) return false;
  // The destination is where the civilian actually comes to a stop, so it must
  // genuinely fit: the body box still refuses here. It is a navmesh projection,
  // so the navmesh-aware height tolerance applies.
  int destReason = ClearanceReason(world, destination, actor);
  if (destReason >= 0)
  {
   EAC_RoutineStats.RecordClearance(true, destReason);
   return false;
  }
  // Short legs only: five interior projections reject gaps and steep terrain.
  for (int i = 1; i <= 5; i++)
  {
   vector sample = vector.Lerp(start, destination, i / 6.0);
   vector projected;
   refusal = EAC_RoutineStats.WLK_SAMPLE;
   if (!path.GetClosestPositionOnNavmesh(sample, "0.5 1 0.5", projected) || vector.Distance(sample, projected) > 1) return false;
   // The body box USED to refuse here, and that was the wander's real problem.
   // These five samples sit on the straight line between the actor and the
   // destination, so demanding a clear 0.9 x 1.75 m box at each of them is the
   // continuous straight-line body sweep audit item 2 already removed for
   // refusing 74 % of attempts - re-implemented discretely. The proof it was
   // doing exactly that: campaign 124 read body_obs=0, because any leg with an
   // obstruction on the line had already been rejected here before the
   // observation could fire, and acceptance sat at 23-25 % across three builds.
   //
   // What these samples are FOR, per the comment above, is rejecting gaps and
   // steep terrain - and the navmesh projection is what does that. Native pathing
   // walks around fences and house corners; the leg is still bounded by the
   // destination box, the band, the leash, the exclusion zones, the group's 30 s
   // deadline and EAC_MoveFailureGuard. The box survives as an observation only,
   // taken below on legs this function is about to ACCEPT.
  }
  // Audit item 2. The straight body sweep no longer refuses a leg. A wander leg
  // is 6-20 m and a fence, a garden wall or a house corner on the straight line
  // is exactly what native pathing walks around, which is why CanApproach had
  // already dropped it for anything over 12 m; here it refused 74 % of every
  // attempt and left the wander unable to fill a single idle gap. What bounds a
  // leg now is the navmesh projection above, the five interior samples, the
  // group's own 30 s deadline and EAC_MoveFailureGuard on the order - a move
  // that genuinely cannot be pathed ends the leg and retires the destination.
  //
  // It is still evaluated once, outside a house (a resident indoors fails the
  // sweep against its own exterior wall on every bearing, the same exemption
  // CanApproach makes), purely as an observation on a leg this function is about
  // to ACCEPT: body_obs against ok is how the next campaign says what this
  // removal actually bought. It never sets `refusal` (audit S5).
  // Audit-item-2 pattern: evaluated only on an otherwise-accepted leg, so it
  // costs traces on accepted legs alone and answers "how many legs did removing
  // the interior box unlock?" for benchmark 08. First failing sample only.
  for (int obs = 1; obs <= 5; obs++)
  {
   vector obsSample = vector.Lerp(start, destination, obs / 6.0);
   vector obsProjected;
   if (!path.GetClosestPositionOnNavmesh(obsSample, "0.5 1 0.5", obsProjected)) continue;
   int obsReason = ClearanceReason(world, obsProjected, actor);
   if (obsReason < 0) continue;
   EAC_RoutineStats.RecordClearance(false, obsReason);
   break;
  }
  if (!EAC_PedestrianSpawner.InsideHouse(world, home, start, actor))
  {
   TraceBox trace = new TraceBox(); trace.Start = start; trace.End = destination;
   trace.Mins = "-0.4 0.2 -0.4"; trace.Maxs = "0.4 1.8 0.4";
   trace.Flags = TraceFlags.WORLD | TraceFlags.ENTS; trace.Exclude = actor;
   if (world.TraceMove(trace, null) < 1) EAC_RoutineStats.RecordWalk(EAC_RoutineStats.WLK_BODY);
  }
  return true;
 }

 void Monitor(EAC_AmbientModule module, EAC_PedestrianActivation activation, float now)
 {
  if (!m_Group && !m_Waypoint) return;
  EAC_ResidentClaim claim = activation.Claim;
  IEntity actor = claim.Character;
  if (!Replication.IsServer() || !module || module != EAC_AmbientModule.GetActive()) return;
  if (claim.AlarmUntil > now) { Stop(); return; }
  if (!m_Waypoint || !claim.Committed || !actor || !claim.Home.BuildingEntity || claim.Group != m_Group || !claim.Resident.Wanted || claim.Resident.Dead || activation.PlayerTouched || CompartmentAccessComponent.GetVehicleIn(actor) || !EAC_PedestrianSpawner.HasCivilianControl(actor, claim.Group)) { Stop(); return; }
  // A wander leg the native pathing could not complete. The guard already kept
  // vanilla off its NodeError line; drop the order here and, when the leg's
  // destination itself has no path, keep any later stop from being placed there.
  vector failedAt; bool failedForGood;
  if (m_Guard && m_Guard.TakeFailure(m_Waypoint, failedAt, failedForGood))
  {
   if (failedForGood) EAC_MoveFailureGuard.Retire(m_Waypoint.GetOrigin());
   // Continue the candidate sweep: restarting it reissues the same unreachable
   // shoulder or bearing while the resident has made no progress.
   Stop(); m_NextAttempt = now + Math.RandomFloat(5, 15);
   return;
  }
  bool allowed = EAC_ExclusionZone.IsPopulationAllowed(m_Waypoint.GetOrigin()) && EAC_ExclusionZone.IsTransitAllowed(actor.GetOrigin(), m_Waypoint.GetOrigin());
  AICharacterMovementComponent movement = AICharacterMovementComponent.Cast(actor.FindComponent(AICharacterMovementComponent));
  if (!movement) allowed = false;
  if (allowed)
  {
   m_ScratchRoute.Clear(); movement.GetCurrentPath(m_ScratchRoute);
   if (m_ScratchRoute.Count() > 64) allowed = false;
   // Audit B1. The per-segment sweep exists solely for transit-blocking zones;
   // with none in the mission it is skipped rather than run against an empty list
   // for up to 64 points per resident per tick.
   if (EAC_ExclusionZone.AnyTransitBlocked())
   {
    vector previous = actor.GetOrigin();
    for (int segment = 0; allowed && segment < m_ScratchRoute.Count(); segment++)
    {
     allowed = EAC_ExclusionZone.IsTransitAllowed(previous, m_ScratchRoute[segment]); previous = m_ScratchRoute[segment];
    }
   }
  }
  if (!allowed || now >= m_Deadline || claim.Group.GetCurrentWaypoint() != m_Waypoint || vector.Distance(actor.GetOrigin(), m_Waypoint.GetOrigin()) < 2)
  {
   // Owner feedback 2026-09-19: residents should keep moving. The pause between two
   // wander legs was 5-15 s of standing still; it is now a short breather.
   Stop(); m_Attempt = 0; m_NextAttempt = now + Math.RandomFloat(2, 6);
  }
 }

 void Step(EAC_AmbientModule module, EAC_PedestrianActivation activation)
 {
  if (!Replication.IsServer() || !module || module != EAC_AmbientModule.GetActive() || m_Waypoint || (activation.Activity && activation.Activity.BlocksWalking())) return;
  EAC_ResidentClaim claim = activation.Claim;
  IEntity actor = claim.Character;
  if (claim.AlarmUntil > module.GetWorld().GetWorldTime() * 0.001) return;
  if (!claim.Committed || !actor || !claim.Home.BuildingEntity || !claim.Resident.Wanted || claim.Resident.Dead || activation.PlayerTouched || CompartmentAccessComponent.GetVehicleIn(actor) || !EAC_PedestrianSpawner.IsCivilian(actor, claim.Group)) { Stop(); return; }
  AIControlComponent control = AIControlComponent.Cast(actor.FindComponent(AIControlComponent));
  AIAgent agent = control.GetAIAgent();
  float now = actor.GetWorld().GetWorldTime() * 0.001;
  // Checked before the backoff, and writable only by YieldTo: a routine start has
  // asked for the group and must be allowed to get it.
  if (now < m_YieldUntil) return;
  // A resident left standing in a traffic lane - a leg that timed out halfway
  // across, a routine that ended there, an alarm that lapsed - does not wait out the
  // backoff: the shoulder candidates below take it to the roadside first (owner
  // report 2026-09-19, civilians standing in the road). One road query per visit,
  // and only for a walker that is actually waiting.
  if (now < m_NextAttempt)
  {
   if (EAC_ActivityStation.OffRoad(actor.GetOrigin(), 0.5)) return;
   m_Attempt = 0;
  }
  m_NextAttempt = now + 1;
  array<AIWaypoint> existing = {}; claim.Group.GetWaypoints(existing);
  if (!existing.IsEmpty() || !EAC_ExclusionZone.IsPopulationAllowed(actor.GetOrigin())) return;
  AIPathfindingComponent path = AIPathfindingComponent.Cast(claim.Group.FindComponent(AIPathfindingComponent));
  if (!path || !path.GetNavmeshComponent()) return;
  vector candidate;
  bool shoulder;
  // Whether the bearing this attempt used came from the goal fan. Recorded only
  // on acceptance, so the split is "which bearing supplied the leg", not "which
  // bearing was tried".
  bool goalBiased;
  m_Attempt++;
  if (m_Attempt > 14) { m_Attempt = 0; m_NextAttempt = now + 10; EAC_RoutineStats.RecordWalkExhausted(); return; }
  EAC_RoutineStats.RecordWalkAttempt();
  if (m_Attempt <= 2)
  {
   ChimeraAIWorld aiWorld = ChimeraAIWorld.Cast(GetGame().GetAIWorld());
   if (aiWorld && aiWorld.GetRoadNetworkManager())
   {
    BaseRoad road; float distance;
    aiWorld.GetRoadNetworkManager().GetClosestRoad(actor.GetOrigin(), road, distance, true);
    if (road)
    {
     array<vector> points = {}; road.GetPoints(points);
     shoulder = Shoulder(points, road.GetWidth(), actor.GetOrigin(), m_Attempt == 2, candidate);
    }
   }
  }
  if (!shoulder)
  {
   // Cover the local circle once rather than repeatedly retrying blocked shoulders.
   int directionIndex = claim.Resident.Id % 12 + m_Attempt - 3;
   float angle = directionIndex * Math.PI2 / 12;
   // Fan around the goal bearing rather than committing to one direction: a
   // single fence between here and the next scene would otherwise stall travel.
   //
   // Audit item 10: the fan used to own attempts 1-6 outright, so the first six
   // attempts all sat inside one 100-degree arc and a resident boxed in on the
   // goal side never sampled the other 260 degrees before the ladder gave up.
   // The bearings now alternate - odd attempts aim at the goal (centre, then
   // +-36 degrees), even attempts take the next bearing of the full circle - so
   // three of the first six are goal-biased and three sweep the circle. Integer
   // arithmetic: '%' is invalid in a float expression in Enforce.
   int parity = m_Attempt % 2;
   if (m_HasGoal && m_Attempt <= 6 && parity == 1)
   {
    vector toGoal = m_Goal - actor.GetOrigin(); toGoal[1] = 0;
    if (toGoal.LengthSq() > 4)
    {
     int fan = (m_Attempt - 1) / 2;
     float spread = 0;
     if (fan == 1) spread = 0.63;
     else if (fan == 2) spread = -0.63;
     angle = Math.Atan2(toGoal[2], toGoal[0]) + spread;
     goalBiased = true;
     // Counted on the ATTEMPT, not only on acceptance: fan_goal=0 across three
     // campaigns could not distinguish "the goal fan is never reached" from "its
     // bearings are always refused", which is what made this take so long to
     // find. fan_goal_tried against fan_goal answers it outright.
     EAC_RoutineStats.RecordWalkFanTried();
    }
   }
   // Two sweeps of the local circle: a full leg first, then a shorter one. A
   // short step clears fences and garden walls far more often and still reads
   // as movement, and a resident with no activity slot has only this path.
   float reach = 10;
   if (m_Attempt >= 9) reach = 7.5;
   candidate = actor.GetOrigin() + Vector(Math.Cos(angle) * reach, 0, Math.Sin(angle) * reach);
  }
  candidate[1] = actor.GetWorld().GetSurfaceY(candidate[0], candidate[2]) + 0.1;
  NavmeshWorldComponent mesh = path.GetNavmeshComponent();
  // One counter per refusal, in gate order. walk_attempts already says how many
  // attempts were made; these say which line ended each one, which is the
  // question the attempt ladder could not answer on its own.
  if (!mesh.IsTileLoaded(candidate))
  {
   EAC_RoutineStats.RecordWalk(EAC_RoutineStats.WLK_TILE);
   if (!mesh.IsTileRequested(candidate)) mesh.LoadTileIn(candidate);
   return;
  }
  vector destination;
  if (!mesh.IsTileValid(candidate) || !path.GetClosestPositionOnNavmesh(candidate, "1 2 1", destination) || vector.Distance(candidate, destination) > 1.5)
  {
   EAC_RoutineStats.RecordWalk(EAC_RoutineStats.WLK_NAV);
   return;
  }
  if (shoulder && vector.Distance(candidate, destination) > 1)
  {
   EAC_RoutineStats.RecordWalk(EAC_RoutineStats.WLK_NAV);
   return;
  }
  // Hoisted before the counter calls: instance reads never sit inside a static
  // call's argument list, and each distance is evaluated once instead of twice.
  vector actorOrigin = actor.GetOrigin();
  IEntity homeEntity = claim.Home.BuildingEntity;
  vector homeOrigin = homeEntity.GetOrigin();
  float legLength = vector.Distance(actorOrigin, destination);
  float homeLength = vector.Distance(homeOrigin, destination);
  float leash = module.RoutineRange;
  if (legLength < 6 || legLength > 20)
  {
   EAC_RoutineStats.RecordWalk(EAC_RoutineStats.WLK_BAND);
   return;
  }
  if (homeLength > leash)
  {
   EAC_RoutineStats.RecordWalk(EAC_RoutineStats.WLK_LEASH);
   return;
  }
  // A wander leg may cross a road but never ends on one: the destination is where
  // the resident stands until the next order. One metre beyond the edge keeps the
  // pavement and the shoulder candidates (edge + 1.5 m) usable.
  if (!EAC_ActivityStation.OffRoad(destination, 1))
  {
   EAC_RoutineStats.RecordWalk(EAC_RoutineStats.WLK_DEST);
   return;
  }
  int legRefusal;
  if (!ClearLeg(actor, homeEntity, path, destination, legRefusal))
  {
   EAC_RoutineStats.RecordWalk(legRefusal);
   return;
  }
  EntitySpawnParams params = new EntitySpawnParams(); params.TransformMode = ETransformMode.WORLD;
  Math3D.MatrixIdentity4(params.Transform); params.Transform[3] = destination;
  m_Waypoint = AIWaypoint.Cast(GetGame().SpawnEntityPrefab(Resource.Load("{750A8D1695BD6998}Prefabs/AI/Waypoints/AIWaypoint_Move.et"), actor.GetWorld(), params));
  if (!m_Waypoint) return;
  m_Group = claim.Group; m_Deadline = now + 30;
  m_Waypoint.SetCompletionRadius(1.5);
  control.ActivateAI(); agent.PreventMaxLOD(30);
  m_Group.ActivateAI(); m_Group.PreventMaxLOD(30);
  // Two pin issues of 30 s each, already bounded by m_Deadline above.
  EAC_RoutineStats.RecordPinnedSeconds(30);
  EAC_RoutineStats.RecordPinnedSeconds(30);
  m_Group.AddWaypoint(m_Waypoint);
  // Once per group; the same guard serves the activity approach on this group.
  m_Guard = EAC_MoveFailureGuard.Attach(m_Group, "walk");
  EAC_RoutineStats.RecordWalkAccepted();
  EAC_RoutineStats.RecordWalk(EAC_RoutineStats.WLK_OK);
  // Hoisted, so the static call carries a local rather than a member read.
  bool acceptedFromGoal = goalBiased;
  EAC_RoutineStats.RecordWalkFan(acceptedFromGoal);
 }

 bool EAC_ContainsSessionHelper(SCR_EditableEntityComponent candidate) { return EAC_SessionLifecycle.ContainsOwned(candidate, m_Waypoint); }
}
