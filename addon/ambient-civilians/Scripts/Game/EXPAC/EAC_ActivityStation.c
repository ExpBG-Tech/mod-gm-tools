// One shared indoor sitting spot, two exclusive participant positions. Nothing is
// spawned any more: by owner decision the residents simply sit down together, and
// the native seat never needed a prop between them. No world scan or timer.
class EAC_ActivityStation
{
 protected static ref array<ref EAC_ActivityStation> s_Stations = {};
 protected static const int MAX_STATIONS = 32;
 // How long a spot with a free position keeps drawing someone over. The invitation
 // is also retired when the second position fills and when the spot is released, so
 // this is a backstop rather than the mechanism, and may span a full dwell.
 protected static const float INVITE_SECONDS = 280;
 // How far a neighbour may walk to take the free position: the invitation range,
 // the join candidate range and the visiting leash in CanApproach all use it.
 // It was 60 m, the same as the routine leash, and a small village never had two
 // active residents that close: the live Morton run (5 active, 10 homes, 400 m
 // module) logged `invite none=40 far=41 ok=0 | join cand=10 far=10 ok=0` with
 // table_started=10 and table_joined=0 over forty minutes. 150 m is a short walk
 // across a village, and the entry deadline already scales with distance (240 s
 // cap). Each join is still one native path and the usual approach gates.
 static const float JOIN_RANGE = 150;
 protected static EAC_ActivityStation s_OpenStation;
 protected static float s_OpenStationUntil;
 protected static BaseWorld s_World;
 // Homes whose shared spot a native move could not reach (EAC_MoveFailureGuard).
 // A retired home neither offers its live station to joiners nor gets a new one
 // this mission. Bounded and reset per world like s_Stations.
 protected static const int MAX_FAILED_HOMES = 32;
 protected static ref array<int> s_FailedHomes = {};
 protected BaseWorld m_World;
 protected EAC_HouseholdRecord m_Home;
 protected EAC_ResidentClaim m_First, m_Second;
 // Live from the moment this record is registered until its last participant
 // leaves. With no prop to look for, this is what presence means.
 protected bool m_Live;
 protected vector m_Transform[4];

 protected static void CheckWorld()
 {
  BaseWorld world = GetGame().GetWorld();
  if (world == s_World) return;
  // Drop previous mission records only; never delete entities belonging to it.
  s_Stations.Clear(); s_FailedHomes.Clear(); s_World = world;
  // Audit S8. s_OpenStation is a plain pointer into the array just cleared and
  // s_OpenStationUntil holds a previous world's clock, which restarts near zero:
  // leaving them behind offered every resident of the new world an invitation to
  // a spot belonging to the old one.
  s_OpenStation = null; s_OpenStationUntil = 0;
 }

 int ParticipantCount()
 {
  int count; if (m_First) count++; if (m_Second) count++; return count;
 }

 // The household whose shared spot this is. A resident sitting down at another
 // household's spot is visiting and may walk up to JOIN_RANGE (CanApproach).
 EAC_HouseholdRecord GetHome() { return m_Home; }

 int Assign(EAC_ResidentClaim claim)
 {
  if (!claim) return -1;
  if (m_First == claim) return 0;
  if (m_Second == claim) return 1;
  if (!m_First) { m_First = claim; return 0; }
  if (!m_Second) { m_Second = claim; return 1; }
  return -1;
 }

 void Position(int slot, out vector transform[4])
 {
  Math3D.MatrixIdentity4(transform);
  float side = -1; if (slot == 1) side = 1;
  vector forward = m_Transform[2] * -side;
  Math3D.DirectionAndUpMatrix(forward, "0 1 0", transform);
  vector position = m_Transform[3] + m_Transform[2] * (1.25 * side);
  transform[3] = position;
 }

 // Match the walker's bounded native road-polyline projection; stationary
 // activities need additional room beyond the road edge, not just its centre.
 // `margin` is the room demanded beyond the road edge: two metres for a place to
 // stand for minutes, less for a wander leg that only has to end off the lane.
 static bool ClearOfRoad(array<vector> points, float width, vector position, float margin = 2)
 {
  if (!points || points.Count() < 2 || points.Count() > 256 || width <= 0 || width > 30) return false;
  float clearance = width * 0.5 + margin;
  bool segmentFound;
  for (int index = 1; index < points.Count(); index++)
  {
   vector delta = points[index] - points[index - 1]; delta[1] = 0;
   if (delta.LengthSq() < 0.01) continue;
   segmentFound = true;
   vector offset = position - points[index - 1]; offset[1] = 0;
   float along = Math.Clamp(vector.Dot(offset, delta) / delta.LengthSq(), 0, 1);
   vector nearest = points[index - 1] + delta * along;
   if (vector.DistanceXZ(position, nearest) < clearance) return false;
  }
  return segmentFound;
 }

 static bool OffRoad(vector position, float margin = 2)
 {
  ChimeraAIWorld world = ChimeraAIWorld.Cast(GetGame().GetAIWorld());
  if (!world || !world.GetRoadNetworkManager()) return false;
  BaseRoad road; float distance;
  world.GetRoadNetworkManager().GetClosestRoad(position, road, distance, true);
  if (!road) return true;
  array<vector> points = {}; road.GetPoints(points);
  return ClearOfRoad(points, road.GetWidth(), position, margin);
 }

 // The observer sweep that used to stand here existed only so a prop could not
 // appear in front of a player. Nothing is created any more, so a resident
 // sitting down is exactly as visible as a resident standing, and needs no gate.

 bool Contains(IEntity actor)
 {
  if (!actor || !m_Home || !m_Home.BuildingEntity) return false;
  IEntity house = m_Home.BuildingEntity;
  vector mins, maxs; house.GetBounds(mins, maxs);
  vector local = house.CoordToLocal(actor.GetOrigin());
  if (local[0] <= mins[0] || local[0] >= maxs[0] || local[2] <= mins[2] || local[2] >= maxs[2]) return false;
  TraceParam roof = new TraceParam(); roof.Start = actor.GetOrigin() + "0 1.7 0"; roof.End = actor.GetOrigin() + "0 12 0";
  roof.Flags = TraceFlags.WORLD | TraceFlags.ENTS; roof.Exclude = actor;
  return actor.GetWorld().TraceMove(roof, null) < 1 && roof.TraceEnt == house;
 }

 // A live spot that has one occupant and a free position, within walking range.
 // Held as one pointer rather than scanned, so the routine hot path stays O(1).
 static bool InviteOpen(vector position, float now)
 {
  EAC_ActivityStation station = s_OpenStation;
  if (!station || now > s_OpenStationUntil) { EAC_RoutineStats.RecordInvite(EAC_RoutineStats.INVITE_NONE); return false; }
  bool live = station.IsPresent();
  int taken = station.ParticipantCount();
  if (!live || taken >= 2)
  {
   s_OpenStation = null;
   EAC_RoutineStats.RecordInvite(EAC_RoutineStats.INVITE_STALE);
   return false;
  }
  if (vector.Distance(station.m_Transform[3], position) >= JOIN_RANGE)
  {
   EAC_RoutineStats.RecordInvite(EAC_RoutineStats.INVITE_FAR);
   return false;
  }
  EAC_RoutineStats.RecordInvite(EAC_RoutineStats.INVITE_OK);
  return true;
 }

 protected static void CloseInvite(EAC_ActivityStation station)
 {
  if (s_OpenStation == station) { s_OpenStation = null; s_OpenStationUntil = 0; }
 }

 // Audit B2. Each probe is an InteriorPoint (navmesh projection plus an
 // InteriorVolume body and roof trace) and, when that passes, a ClearPlacement of
 // three more navmesh projections, three road queries, three interior volumes and
 // a box sweep. Nine of those in the same BeginLeg as the join scan was half the
 // per-tick trace spike. The ladder draws from EAC_RoutineAnchors' shared per-tick
 // budget - the same pool the outdoor sweep uses - and resumes from a cursor on
 // the retained resident record, so the cap is on what one TICK costs rather than
 // on what one resident may attempt.

 // `deferred` is true when the probe budget ran out before the ladder finished.
 // It is NOT "this household cannot host a spot": the caller must not charge it
 // as a failed table attempt, or a budget stop would retire the resident's table
 // purpose after three ticks.
 static EAC_ActivityStation Acquire(EAC_ResidentClaim claim, out int slot, out bool deferred)
 {
  slot = -1;
  deferred = false;
  CheckWorld();
  if (!Replication.IsServer() || !claim || !claim.Resident || !claim.Character || !claim.Home || !claim.Home.BuildingEntity || !claim.Group) return null;
  AIPathfindingComponent path = AIPathfindingComponent.Cast(claim.Group.FindComponent(AIPathfindingComponent));
  if (!path) { EAC_RoutineStats.RecordAcquire(EAC_RoutineStats.ACQ_NOPATH); return null; }
  EAC_ActivityStation nearest; float nearestDistance = JOIN_RANGE; bool homeHasStation;
  foreach (EAC_ActivityStation existing : s_Stations)
  {
   if (existing.m_Home == claim.Home) homeHasStation = true;
   bool existingLive = existing.IsPresent();
   int existingTaken = existing.ParticipantCount();
   if (!existingLive || existingTaken >= 2 || !existing.m_Home || !existing.m_Home.BuildingEntity) continue;
   if (HomeRetired(existing.m_Home.Id)) continue;
   EAC_RoutineStats.RecordJoinGate(EAC_RoutineStats.JOIN_CANDIDATE);
   int availableSlot; if (existing.m_First) availableSlot = 1;
   vector candidate[4]; existing.Position(availableSlot, candidate);
   vector candidatePosition = candidate[3];
   float distance = vector.Distance(candidatePosition, claim.Character.GetOrigin());
   if (distance > nearestDistance) { EAC_RoutineStats.RecordJoinGate(EAC_RoutineStats.JOIN_FAR); continue; }
   string reason;
   vector projected;
   if (!path.GetClosestPositionOnNavmesh(candidatePosition, "0.2 0.3 0.2", projected) || vector.Distance(candidatePosition, projected) > 0.2)
   {
    EAC_RoutineStats.RecordJoinGate(EAC_RoutineStats.JOIN_NAVMESH); continue;
   }
   if (!EAC_ActivityStation.OffRoad(candidatePosition)) { EAC_RoutineStats.RecordJoinGate(EAC_RoutineStats.JOIN_ROAD); continue; }
   if (!EAC_ExclusionZone.IsPopulationAllowed(candidatePosition) || !EAC_ExclusionZone.IsTransitAllowed(claim.Character.GetOrigin(), candidatePosition))
   {
    EAC_RoutineStats.RecordJoinGate(EAC_RoutineStats.JOIN_ZONE); continue;
   }
   if (!EAC_CivilianShelter.InteriorVolume(existing.m_Home.BuildingEntity, claim.Character, candidatePosition, reason))
   {
    EAC_RoutineStats.RecordJoinGate(EAC_RoutineStats.JOIN_INTERIOR); continue;
   }
   EAC_RoutineStats.RecordJoinGate(EAC_RoutineStats.JOIN_OK);
   nearestDistance = distance; nearest = existing;
  }
  if (nearest)
  {
   slot = nearest.Assign(claim);
   // The invitation has done its work. Left open it would keep drawing people
   // to a spot with no free position, costing each of them their beat.
   if (nearest.ParticipantCount() >= 2) CloseInvite(nearest);
   EAC_RoutineStats.RecordTable(0, 1, 0);
   return nearest;
  }
  // Nearby neighbours may join; a household with a retained spot cannot multiply it.
  if (homeHasStation) { EAC_RoutineStats.RecordAcquire(EAC_RoutineStats.ACQ_HOMEHAS); return null; }
  if (HomeRetired(claim.Home.Id)) { EAC_RoutineStats.RecordAcquire(EAC_RoutineStats.ACQ_RETIRED); return null; }
  if (s_Stations.Count() >= MAX_STATIONS) { EAC_RoutineStats.RecordAcquire(EAC_RoutineStats.ACQ_MAXSTATIONS); return null; }
  EAC_ActivityStation station = new EAC_ActivityStation(); station.m_Home = claim.Home; station.m_World = s_World;
  claim.Home.BuildingEntity.GetWorldTransform(station.m_Transform);
  station.m_Transform[0][1] = 0; station.m_Transform[2][1] = 0;
  Math3D.DirectionAndUpMatrix(station.m_Transform[2].Normalized(), "0 1 0", station.m_Transform);
  bool found;
  int cursor = claim.Resident.StationCursor;
  if (cursor < 0 || cursor >= 9) cursor = 0;
  bool swept = false;
  float probeNow = GetGame().GetWorld().GetWorldTime() * 0.001;
  for (int probe = 0; probe < 9; probe++)
  {
   if (!EAC_RoutineAnchors.TakeProbe(s_World, probeNow)) break;
   vector position; string reason;
   int index = (cursor + claim.Resident.Id) % 9;
   cursor++;
   if (cursor >= 9) { cursor = 0; swept = true; }
   if (!EAC_CivilianShelter.InteriorPoint(claim.Home.BuildingEntity, claim.Character, path, index, position, reason)) { EAC_RoutineStats.RecordAcquire(EAC_RoutineStats.ACQ_NOINTERIOR); continue; }
   station.m_Transform[3] = position;
   if (!station.ClearPlacement(claim.Character, path)) { EAC_RoutineStats.RecordAcquire(EAC_RoutineStats.ACQ_PLACEMENT); continue; }
   bool overlap;
   foreach (EAC_ActivityStation other : s_Stations)
   {
    bool otherLive = other.IsPresent();
    if (otherLive && vector.Distance(other.m_Transform[3], position) < 4) overlap = true;
   }
   if (overlap) { EAC_RoutineStats.RecordAcquire(EAC_RoutineStats.ACQ_OVERLAP); continue; }
   found = true; break;
  }
  claim.Resident.StationCursor = cursor;
  // Every rejected probe already recorded its own reason above, and there is no
  // prop left to hide from anyone, so no further gate stands between a validated
  // spot and its first occupant.
  if (!found)
  {
   // Part-way through the ladder: the next call resumes at `cursor` and the full
   // nine probes still get tried, across ticks rather than inside one.
   if (!swept) { deferred = true; EAC_RoutineStats.RecordAcquire(EAC_RoutineStats.ACQ_BUDGET); }
   return null;
  }
  slot = station.Assign(claim);
  station.m_Live = true;
  s_Stations.Insert(station);
  // One person sitting down is an invitation to anyone nearby. This is what makes
  // the second position reachable at all: without it two residents would each
  // have to pick the single shared spot out of roughly fourteen eligible ones,
  // independently and inside the same window.
  s_OpenStation = station;
  s_OpenStationUntil = GetGame().GetWorld().GetWorldTime() * 0.001 + INVITE_SECONDS;
  EAC_RoutineStats.RecordTable(1, 0, 0);
  EAC_RoutineStats.RecordAcquire(EAC_RoutineStats.ACQ_CREATED);
  // The invitation is only read when a neighbour starts its next routine, which
  // could be minutes away; ask the nearest idle resident in reach to start now.
  // One bounded pass over the tracked residents per spot opened (rare); it only
  // clears a cooldown, the ordinary start ladder still decides everything.
  EAC_AmbientModule module = EAC_AmbientModule.GetActive();
  if (module && module.GetSpawner() && module.GetSpawner().CallToStation(module, claim, station.m_Transform[3], probeNow))
   EAC_RoutineStats.RecordInvite(EAC_RoutineStats.INVITE_CALLED);
  return station;
 }

 protected bool ClearPlacement(IEntity actor, AIPathfindingComponent path)
 {
  BaseWorld world = GetGame().GetWorld();
  vector center = m_Transform[3];
  bool centerOffRoad = EAC_ActivityStation.OffRoad(center);
  if (!EAC_ExclusionZone.IsPopulationAllowed(center) || !centerOffRoad) return false;
  // Test the whole sitting spot before anyone is sent to it, including both assigned positions.
  for (int i = -1; i <= 1; i++)
  {
   vector point = m_Transform[3] + m_Transform[2] * (i * 1.25);
   vector floor; string reason;
   if (!path.GetClosestPositionOnNavmesh(point, "0.2 0.3 0.2", floor) || vector.Distance(point, floor) > 0.2) return false;
   bool pointOffRoad = EAC_ActivityStation.OffRoad(point);
   if (!pointOffRoad) return false;
   if (!EAC_ExclusionZone.IsPopulationAllowed(point) || !EAC_CivilianShelter.InteriorVolume(m_Home.BuildingEntity, actor, point, reason)) return false;
  }
  // Both occupants still need the volume between and around the two positions
  // clear; this is the sweep that made the slots valid in the measured campaigns.
  TraceBox trace = new TraceBox(); trace.Start = m_Transform[3] + "0 0.1 0"; trace.End = trace.Start;
  trace.Mins = "-1.1 0 -1.1"; trace.Maxs = "1.1 1 1.1"; trace.Flags = TraceFlags.WORLD | TraceFlags.ENTS; trace.Exclude = actor;
  return world.TraceMove(trace, null) == 1;
 }

 // "Present" is a record question now, not a world question: this station was
 // registered, has not been retired, and belongs to the running mission.
 bool IsPresent()
 {
  if (!m_Live) return false;
  if (!m_World) return true;
  return m_World == GetGame().GetWorld();
 }

 // Bookkeeping only. Nothing was created for this station, so nothing has to be
 // destroyed, protected from a player or retried on a later pass.
 bool Release(EAC_ResidentClaim claim)
 {
  if (!Replication.IsServer()) return false;
  if (m_World && m_World != GetGame().GetWorld()) return true;
  if (m_First != claim && m_Second != claim) return true;
  if (ParticipantCount() == 1)
  {
   CloseInvite(this);
   m_Live = false;
   s_Stations.RemoveItem(this);
  }
  if (m_First == claim) m_First = null;
  if (m_Second == claim) m_Second = null;
  return true;
 }

 static bool HomeRetired(int homeId)
 {
  CheckWorld();
  return s_FailedHomes.Contains(homeId);
 }

 // The station record itself is left alone: a second participant may still be
 // posed there, and Release retires the record when the last one leaves.
 static void RetireHome(EAC_HouseholdRecord home)
 {
  if (!Replication.IsServer() || !home) return;
  CheckWorld();
  if (s_FailedHomes.Contains(home.Id)) return;
  if (s_FailedHomes.Count() >= MAX_FAILED_HOMES) s_FailedHomes.RemoveOrdered(0);
  s_FailedHomes.Insert(home.Id);
 }

 static int Count() { CheckWorld(); return s_Stations.Count(); }
}
