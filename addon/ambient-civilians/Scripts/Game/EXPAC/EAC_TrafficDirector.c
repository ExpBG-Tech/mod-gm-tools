// Retain alongside the mission index. One physical operation per shared Step.
class EAC_TrafficDirector
{
 static const int MAX_PARTIES = 64;
 // Bound for the DRIVE-phase exclusion scan. A native path longer than this is a
 // long journey, not a blocked one: the scan work is capped, never the route.
 // EAC_TrafficRoute.Find admits 250-5000 m routes and the observed native path
 // spacing is about 7 m, so treating length itself as a block rejected most
 // legal routes on their first DRIVE tick (runs 68-88, always path_points=141).
 static const int PATH_SCAN_LIMIT = 128;
 // One arrival radius for the whole journey. The drive waypoint completes on the
 // driver, the destination is a lane-projected road point, and the native
 // reachable-road lookup is allowed 100 m of endpoint tolerance, so the car's own
 // origin legitimately stops short of the exact destination vector. The REST
 // phase already treated this same radius as arrived before swapping legs.
 static const float ARRIVAL_M = 30;
 // A crew that has not seated itself this long after the boarding order, while no
 // player can see the car, is seated directly (BoardHidden). The driver now walks
 // to the car from up to 22 m away, so the fallback waits for that walk first.
 static const float BOARD_HIDDEN_SECONDS = 30;
 // One-way model: how long a freshly placed car waits at the kerb before a driver
 // comes for it, and how far from the car that driver may appear.
 static const float PARKED_MIN_SECONDS = 20;
 static const float PARKED_MAX_SECONDS = 75;
 static const float BOARD_WALK_DEADLINE = 90;
 // A record that begins this many journeys without ever moving is broken terrain,
 // not traffic: retire it instead of cycling BOARD/DRIVE/PARK/EXIT/REST forever.
 static const int BLOCKED_TRIP_LIMIT = 3;
 static int PathScanCount(int pathPoints)
 {
  if (pathPoints <= 0) return 0;
  return Math.Min(pathPoints, PATH_SCAN_LIMIT);
 }
 protected static EAC_TrafficDirector s_Instance;
 protected BaseWorld m_World;
 protected ref array<ref EAC_TrafficParty> m_Parties = {};
 protected int m_Cursor, m_HomeCursor, m_GoalCursor, m_NextId = 1000000;
 protected float m_NextAdmission, m_NextCandidate;
 protected bool m_GapStopped;
 // m_Towns/m_TownAttempts/PrepareTowns moved wholesale to EAC_SettlementIndex so
 // one cached settlement list serves traffic and mission-start prewarm. The
 // behaviour is unchanged: same descriptor types, same guards, same caps, and
 // the road fallback still fires only when that list is genuinely empty.
 protected int m_JourneysStarted, m_JourneysCompleted, m_DespawnCount, m_HornRequests;
 // Parties whose horn was actually read on the last frame. UpdateHorn used to do
 // three FindComponent calls for every retained record on every frame, including
 // records with no car at all; only a boarded party can sound a horn.
 protected int m_HornPolled;
 // Admissions refused by the boarding screen before a car or crew was
 // created. Run 108's class: the record commits to a road point whose roadside
 // carries no navmesh, and nothing notices until BoardingGoal fails twenty
 // seconds later with a car, a group and a crew already spawned.
 protected int m_BoardingScreened;
 // Crew members seated directly because the native boarding order stalled while
 // the car was hidden (224-addon campaign 2026-09-18: first crew never boarded
 // with an AI-behaviour mod set loaded; the party was retired at the deadline).
 protected int m_BoardHidden;
 // Journeys whose crew dismounted before the car ever moved, returned to rest as
 // blocked trips instead of failing the party (see the DRIVE phase).
 protected int m_DriveAbandoned;
 // Reused by the visibility proof and the DRIVE-phase exclusion scan. One
 // TraceParam and one exclusion array for the life of the director instead of one
 // pair per observer per call, and one path array instead of one per driving
 // party per tick (audit item 6). Neither is re-entered: Hidden only traces, and
 // Monitor only reads the native path.
 protected ref TraceParam m_HiddenTrace = new TraceParam();
 protected ref array<IEntity> m_HiddenExcluded = {};
 protected ref array<vector> m_PathScratch = {};
 // PickTown's three best destination towns and their ranks, retained so an
 // admission attempt every two seconds allocates nothing.
 protected ref array<vector> m_GoalScratch = {};
 protected ref array<float> m_GoalRank = {};
 protected float m_PeakSpeed;
 protected ref map<string, int> m_AdmissionResults = new map<string, int>();
 protected string m_LastAdmission, m_LastRoute;
 protected int m_LifecycleFailures;
 protected string m_LastFailure;
 // Owned cars that disappeared without this director (see LoseCar).
 protected int m_CarsLost;
 // Controller gap cleanup (DrainParties): one pump tick a second, at most
 // GAP_PARTIES records examined and one entity deleted per tick. The removal
 // distance is the last active module's, so deleting it changes no rule.
 static const int GAP_INTERVAL_MS = 1000;
 static const int GAP_PARTIES = 4;
 protected float m_CleanupDistance = 1000;
 protected int m_GapCursor, m_GapDeletions;
 protected ref array<int> m_GapPlayers = {};
 protected ref array<IEntity> m_GapObservers = {};

 void RecordFailure(EAC_TrafficParty party, string reason)
 {
  if (Get() != this || !party) return;
  if (m_LifecycleFailures < 1000000) m_LifecycleFailures++;
  m_LastFailure = string.Format("party=%1 phase=%2 crew_spawned=%3/%4 reason=%5 wait=%6 at=%7", party.Id, typename.EnumToString(EAC_TrafficPhase, party.Phase), party.Spawned, party.Crew.Count(), reason, party.PendingReason, party.PendingPosition);
  // Native failures/deadlines share the bounded mission error ledger and remain
  // visible with diagnostics off. Expected takeovers/external removals are still
  // counted, but only requested verbose output describes them.
  EAC_AmbientModule module = EAC_AmbientModule.GetActive();
  if (IsLifecycleError(reason) && module && module.GetSpawner())
  {
   if (module.GetSpawner().GetDiagnostics().TakeFailureLog("traffic: " + reason)) Print("[EAC TRAFFIC FAILURE] " + m_LastFailure, LogLevel.WARNING);
   return;
  }
  int level = EAC_AmbientModule.GetDebugLevelMirror();
  if (level >= 1 && m_LifecycleFailures <= 128) Print("[EAC TRAFFIC STOP] " + m_LastFailure);
 }

 static bool IsLifecycleError(string reason)
 {
  if (reason == "spawn deadline" || reason == "group spawn" || reason == "CIV unavailable") return true;
  if (reason == "civilian settings" || reason == "boarding order" || reason == "crew navmesh unavailable") return true;
  if (reason == "crew spawn" || reason == "boarding deadline" || reason == "drive order") return true;
  if (reason == "crew must be unarmed CIV AI" || reason == "crew inventory unsupported" || reason == "crew carries weapon") return true;
  return reason == "dismount order" || reason == "dismount deadline" || reason == "return boarding";
 }

 protected void RecordAdmission(string reason)
 {
  // Fixed call-site labels, capped values and storage; no per-attempt log or world query.
  int count = m_AdmissionResults.Get(reason);
  if (count < 1000000 && (count > 0 || m_AdmissionResults.Count() < 32)) m_AdmissionResults.Set(reason, count + 1);
  m_LastAdmission = reason;
 }

 string BuildDebugSummary(int logLevel = 3)
 {
  if (logLevel <= 1)
  {
   string brief = string.Format("traffic reserved_parties=%1 pending=%2 despawned=%3 trips=%4/%5 failures=%6", GetActiveCount(), GetPendingCount(), m_DespawnCount, m_JourneysStarted, m_JourneysCompleted, m_LifecycleFailures) + " live_cars=" + GetLiveCarCount().ToString();
   brief += " cars_lost=" + m_CarsLost.ToString();
   return brief;
  }
  int townCount = EAC_SettlementIndex.GetCount();
  string summary = string.Format("traffic parties=%1 reserved_parties=%2 pending=%3 towns=%4 trips=%5/%6 despawned=%7", GetPartyCount(), GetActiveCount(), GetPendingCount(), townCount, m_JourneysStarted, m_JourneysCompleted, m_DespawnCount)
   + " live_cars=" + GetLiveCarCount().ToString()
   + string.Format(" peak_kmh=%1 horn_input_requests_cleared=%2 horn_polled_parties=%3 boarding_screened=%4 board_hidden=%5 drive_abandoned=%6", m_PeakSpeed, m_HornRequests, m_HornPolled, m_BoardingScreened, m_BoardHidden, m_DriveAbandoned);
  summary += string.Format(" cars_lost=%1 gap_deletions=%2", m_CarsLost, m_GapDeletions);
  summary += "\ntraffic admission last=" + m_LastAdmission;
  for (int i = 0; i < m_AdmissionResults.Count(); i++) summary += string.Format(" %1=%2", m_AdmissionResults.GetKey(i), m_AdmissionResults.GetElement(i));
  // Existing retained records only; low-frequency summary, no world queries.
  for (int i = 0; i < Math.Min(4, m_Parties.Count()); i++)
  {
   EAC_TrafficParty party = m_Parties[i];
   vector position = party.Position; if (party.Car) position = party.Car.GetOrigin();
   summary += string.Format("\ntraffic live party=%1 phase=%2 despawn=%3 retire=%4 speed=%5 remaining_m=%6 wait=%7 at=%8 goal=%9", party.Id, typename.EnumToString(EAC_TrafficPhase, party.Phase), party.WantDespawn, party.Retire, party.Speed(), vector.Distance(position, party.Destination), party.PendingReason, position, party.Destination);
   foreach (int index, EAC_TrafficOccupant row : party.Crew)
   {
    if (!row.Actor) continue;
    AIControlComponent control = AIControlComponent.Cast(row.Actor.FindComponent(AIControlComponent));
    AIAgent agent; if (control) agent = control.GetAIAgent();
    CompartmentAccessComponent access = CompartmentAccessComponent.Cast(row.Actor.FindComponent(CompartmentAccessComponent));
    if (agent && access) summary += string.Format(" crew%1(lod=%2 ai=%3 in=%4 exiting=%5)", index, agent.GetLOD(), agent.IsAIActivated(), access.IsInCompartment(), access.IsGettingOut());
   }
  }
  return summary + "\ntraffic route " + m_LastRoute + string.Format("\ntraffic failures=%1 last=%2", m_LifecycleFailures, m_LastFailure);
 }

 void EAC_TrafficDirector(BaseWorld world) { m_World = world; s_Instance = this; }
 void ~EAC_TrafficDirector()
 {
  if (GetGame()) GetGame().GetCallqueue().Remove(DrainParties);
  if (s_Instance == this) s_Instance = null;
 }
 static EAC_TrafficDirector Get()
 {
  if (!Replication.IsServer() || !GetGame() || !s_Instance || s_Instance.m_World != GetGame().GetWorld()) return null;
  return s_Instance;
 }

 static void Drain(float now)
 {
  EAC_TrafficDirector director = Get();
  if (!director || EAC_AmbientModule.GetActive() || director.m_GapStopped) return;
  director.m_GapStopped = true;
  // Removing the last controller pauses mission ownership: nothing is admitted,
  // driven or ordered again, and this operator action is not counted as a failed
  // journey. The retained parties are no longer left in the world for good
  // (issue #20): DrainParties removes them under the protected cleanup rules, and
  // a module placed before that finishes adopts whatever is left.
  foreach (EAC_TrafficParty party : director.m_Parties)
   party.Fail("module absent; retained until protected cleanup", false);
  director.StartGapCleanup();
  if (!director.m_Parties.IsEmpty() && EAC_AmbientModule.GetDebugLevelMirror() >= 1)
   PrintFormat("[EAC TRAFFIC] controller removed; %1 parties retained; protected cleanup continues without a controller", director.m_Parties.Count());
 }

 protected void StartGapCleanup()
 {
  if (!GetGame()) return;
  GetGame().GetCallqueue().Remove(DrainParties);
  if (HasRetainedEntities()) GetGame().GetCallqueue().CallLater(DrainParties, GAP_INTERVAL_MS, true);
 }

 protected bool HasRetainedEntities()
 {
  foreach (EAC_TrafficParty party : m_Parties) if (party.HasEntities()) return true;
  return false;
 }

 // The module's observer list, rebuilt for the controller gap. Unknown, Game
 // Master and spectator slots stay in it, so removal fails closed exactly as it
 // does under a module.
 protected void CollectGapObservers()
 {
  m_GapObservers.Clear(); m_GapPlayers.Clear();
  PlayerManager manager = GetGame().GetPlayerManager();
  if (!manager) return;
  manager.GetPlayers(m_GapPlayers);
  foreach (int id : m_GapPlayers) m_GapObservers.Insert(manager.GetPlayerControlledEntity(id));
 }

 // Controller gap pump (issue #20). With no module placed, retained parties are
 // removed under the same protected rules as ever: owned and never touched by a
 // player, stopped, healthy, beyond the traffic removal distance from every
 // player character and out of their sight. The one difference is that a crew
 // still seated in its stopped car goes with the car, because no dismount order
 // can be placed without a module. Bounded: GAP_PARTIES records looked at and one
 // entity deleted per pump tick. Reservations stay with their records, as the
 // pedestrian claims do; the next module releases them on its first ticks. The
 // pump stops when a module returns, the world changes or nothing is left.
 protected void DrainParties()
 {
  if (!GetGame() || GetGame().GetWorld() != m_World || Get() != this || EAC_AmbientModule.GetActive() || !HasRetainedEntities())
  {
   if (GetGame()) GetGame().GetCallqueue().Remove(DrainParties);
   if (GetGame() && Get() == this && !m_Parties.IsEmpty() && EAC_AmbientModule.GetDebugLevelMirror() >= 1 && !EAC_AmbientModule.GetActive())
    PrintFormat("[EAC TRAFFIC] controller gap: retained parties removed (deletions=%1); reservations are released by the next controller", m_GapDeletions);
   return;
  }
  CollectGapObservers();
  int count = Math.Min(GAP_PARTIES, m_Parties.Count());
  for (int i = 0; i < count; i++)
  {
   m_GapCursor = m_GapCursor % m_Parties.Count();
   EAC_TrafficParty party = m_Parties[m_GapCursor++];
   if (!party.HasEntities()) continue;
   if (party.HadCar && !party.Car) LoseCar(party);
   bool removed = RemoveOrphanGroup(party);
   if (!removed) removed = RemoveOne(party, m_GapObservers, m_CleanupDistance, true);
   if (!removed) continue;
   if (m_GapDeletions < 1000000) m_GapDeletions++;
   break;
  }
  EAC_SessionLifecycle.Sync(m_World.GetWorldTime() * 0.001);
 }

 int GetPartyCount() { return m_Parties.Count(); }
 int CountNearbyOccupants(vector position, float radius)
 {
  int count;
  foreach (EAC_TrafficParty party : m_Parties)
  {
   if (!party.Reserved) continue;
   vector origin = party.Position; if (party.Car) origin = party.Car.GetOrigin();
   if (vector.Distance(origin, position) <= radius) count += party.Crew.Count();
  }
  return count;
 }
 int GetActiveCount()
 {
  int count;
  foreach (EAC_TrafficParty party : m_Parties) if (party.Reserved) count++;
  return count;
 }
 // Diagnostics only: a removed car can leave a protected crew and reservation.
 int GetLiveCarCount()
 {
  int count;
  foreach (EAC_TrafficParty party : m_Parties) if (party.Car) count++;
  return count;
 }
 int GetPendingCount()
 {
  int count;
  foreach (EAC_TrafficParty party : m_Parties)
   if (party.Phase <= EAC_TrafficPhase.BOARD || party.Phase == EAC_TrafficPhase.CLEARING) count++;
  return count;
 }

 bool ReservesGroup(SCR_AIGroup group)
 {
  if (!group) return false;
  foreach (EAC_TrafficParty party : m_Parties)
  {
   if (party.Group == group) return true;
   foreach (EAC_TrafficOccupant row : party.Crew)
    if (row.Actor && row.Actor.GetCharacterGroup() == group) return true;
  }
  return false;
 }
 EBG_CacheMember FindMember(IEntity entity)
 {
  if (!entity) return null;
  foreach (EAC_TrafficParty party : m_Parties)
   foreach (EAC_TrafficOccupant row : party.Crew) if (row.Actor == entity) return row.Member;
  return null;
 }
 void AppendDebug(array<vector> positions, array<int> kinds)
 {
  foreach (EAC_TrafficParty party : m_Parties)
  {
   if (!party.HasEntities()) continue;
   int kind = EAC_Diagnostics.ACTIVE;
   if (party.Phase <= EAC_TrafficPhase.BOARD || party.Phase >= EAC_TrafficPhase.CLEARING) kind = EAC_Diagnostics.PENDING;
   vector position = party.Position;
   if (party.Car) position = party.Car.GetOrigin();
   EAC_Diagnostics.AddDrawPoint(positions, kinds, position, kind);
   if (positions.Count() >= 64) return;
  }
 }

 // Wire once before the module's half-second early return. No individual timers.
 void UpdateHorn()
 {
  if (Get() != this || !EAC_AmbientModule.GetActive()) return;
  m_HornPolled = 0;
  foreach (EAC_TrafficParty party : m_Parties)
  {
   if (!party.CanSoundHorn()) continue;
   m_HornPolled++;
   if (party.UpdateHorn() && m_HornRequests < 1000000) m_HornRequests++;
  }
 }

 // `counted` is the pacing evidence switch, not a behaviour switch. A party's
 // group, car and crew are characters and vehicles and belong in the per-tick
 // creation peak; the move/board waypoints this same helper spawns are neither,
 // and several of them exist around one journey (audit item 3).
 protected IEntity Spawn(ResourceName prefab, vector position, vector direction, bool counted = false)
 {
  // Groups, cars and crews appear only inside the module area. Orders are not
  // population: a departed car's drive, pull-over and dismount waypoints lie on
  // its journey, which may leave the area (EAC_TrafficParty.Departed). Refusing
  // them there failed the dismount order of a car outside the disc and left a
  // seated crew that CanRemove could never clear.
  if (counted && EAC_AmbientModule.IsOutsidePopulationArea(position)) return null;
  Resource resource = Resource.Load(prefab);
  if (!resource || !resource.IsValid()) return null;
  EntitySpawnParams params = new EntitySpawnParams(); params.TransformMode = ETransformMode.WORLD;
  Math3D.DirectionAndUpMatrix(direction, vector.Up, params.Transform); params.Transform[3] = position;
  IEntity created = GetGame().SpawnEntityPrefab(resource, m_World, params);
  if (created && counted) EAC_SchedulerStats.RecordTrafficSpawn();
  return created;
 }

 // One switch for every placement and segment test a party makes. Before its
 // first drive order everything stays inside the module area exactly as before;
 // a departed car may be anywhere on its journey, so only automatic and manual
 // exclusions apply to its kerb, dismount, stroll and return-boarding points.
 protected static bool PointAllowed(EAC_TrafficParty party, vector position)
 {
  if (party && party.Departed) return EAC_ExclusionZone.IsJourneyPointAllowed(position);
  return EAC_ExclusionZone.IsPopulationAllowed(position);
 }

 protected static bool TransitAllowed(EAC_TrafficParty party, vector from, vector to)
 {
  if (party && party.Departed) return EAC_ExclusionZone.IsJourneyTransitAllowed(from, to);
  return EAC_ExclusionZone.IsTransitAllowed(from, to);
 }

 protected bool Relevant(vector position, array<IEntity> observers, float range)
 {
  if (EAC_PedestrianSpawner.GetObserverReason(m_World, observers) != EAC_ESpawnReason.NONE) return false;
  foreach (IEntity observer : observers) if (vector.Distance(observer.GetOrigin(), position) < range) return true;
  return false;
 }

 // Admission towns and route origins never determine live-party removal.
 // Recheck every remaining occupant as well as the car, including during cleanup.
 protected bool FarFromObservers(EAC_TrafficParty party, array<IEntity> observers, float range)
 {
  if (!party || range <= 0 || EAC_PedestrianSpawner.GetObserverReason(m_World, observers) != EAC_ESpawnReason.NONE) return false;
  foreach (IEntity observer : observers)
  {
   if (party.Car && vector.Distance(observer.GetOrigin(), party.Car.GetOrigin()) <= range) return false;
   foreach (EAC_TrafficOccupant row : party.Crew)
    if (row.Actor && vector.Distance(observer.GetOrigin(), row.Actor.GetOrigin()) <= range) return false;
  }
  return true;
 }

 // `minimum` is how far every player must be, on top of having no line of sight.
 // 60 m suits a removal or a driver stepping out of a doorway; a car appearing 60 m
 // away behind one house is a car appearing "right behind me" (owner report
 // 2026-09-19), so placing a car asks for CAR_SPAWN_DISTANCE instead. The test reads
 // player characters only: a Game Master's free camera has no server-side position.
 static const float CAR_SPAWN_DISTANCE = 150;
 protected bool Hidden(EAC_TrafficParty party, vector position, array<IEntity> observers, float minimum = 60)
 {
  if (EAC_PedestrianSpawner.GetObserverReason(m_World, observers) != EAC_ESpawnReason.NONE) return false;
  foreach (IEntity observer : observers)
  {
   if (vector.Distance(observer.GetOrigin(), position) < minimum) return false;
   m_HiddenTrace.Start = ChimeraCharacter.Cast(observer).EyePosition();
   m_HiddenTrace.Flags = TraceFlags.WORLD | TraceFlags.ENTS | TraceFlags.VISIBILITY | TraceFlags.ANY_CONTACT;
   m_HiddenExcluded.Clear(); m_HiddenExcluded.Insert(observer);
   IEntity vehicle = CompartmentAccessComponent.GetVehicleIn(observer); if (vehicle) m_HiddenExcluded.Insert(vehicle);
   if (party)
   {
    if (party.Car) m_HiddenExcluded.Insert(party.Car);
    foreach (EAC_TrafficOccupant row : party.Crew) if (row.Actor) m_HiddenExcluded.Insert(row.Actor);
   }
   m_HiddenTrace.ExcludeArray = m_HiddenExcluded;
   // Cover the car's corners as well as its roof; own crew/car cannot occlude proof.
   for (int i = 0; i < 5; i++)
   {
    vector sample = position + "0 2.2 0";
    if (i > 0)
    {
     vector right = "1 0 0", forward = "0 0 1";
     if (party && party.Direction.LengthSq() > 0.5)
     {
      forward = party.Direction; right = Vector(forward[2], 0, -forward[0]);
      if (party.Car) { vector transform[4]; party.Car.GetWorldTransform(transform); right = transform[0]; forward = transform[2]; }
     }
     int cornerX = i % 2;
     int cornerZ = i / 3;
     sample = position + right * (cornerX * 2.8 - 1.4) + forward * (cornerZ * 5.8 - 2.9) + "0 1.2 0";
    }
    m_HiddenTrace.End = sample;
    if (m_World.TraceMove(m_HiddenTrace, null) >= 1) return false;
   }
  }
  return true;
 }

 protected bool PlaceOrder(EAC_TrafficParty party, ResourceName prefab, vector goal, bool boarding = false)
 {
  if (!party.Controlled() || !EAC_AmbientModule.GetActive()) return false;
  party.ClearOrder();
  if (party.Order) return false;
  party.Order = AIWaypoint.Cast(Spawn(prefab, goal, vector.Forward));
  if (!party.Order) return false;
  if (!party.Controlled()) { party.ClearOrder(); return false; }
  SCR_BoardingEntityWaypoint getIn = SCR_BoardingEntityWaypoint.Cast(party.Order);
  if (boarding)
  {
   if (!getIn) { party.ClearOrder(); return false; }
   getIn.SetEntity(party.Car); getIn.SetAllowance(true, false, true);
  }
  party.Order.SetCompletionRadius(5); party.Group.AddWaypoint(party.Order);
  EAC_SessionLifecycle.Keep(party.Order);
  return true;
 }

 // Never aim a boarding move at the car's own origin. That point lies inside the
 // vehicle's navmesh carve, the native group move returns EMoveError.UNKNOWN, and
 // vanilla SCR_AIProcessFailedMovementResult.c:99 raises a fatal NodeError that
 // takes the whole mission down - observed as the Virtual Machine Exception that
 // ended runs 83 and 85 on waypoint <4687.6,160.041,7023.68>, the frozen car of
 // party 1000000. Offer a navmesh point beside the car instead; SetEntity plus
 // the boarding allowance still own the compartment entry itself.
 // Bounded: at most four native navmesh probes, only while placing one order.
 // The same four roadside candidates BoardingGoal will ask for later - 3.5 m and
 // 5 m either side, perpendicular to the heading - probed with the group's own
 // pathfinder before a car or a crew exists. Static and car-free on purpose: it
 // runs in the NEW phase where there is no car yet, and a fixture can drive it
 // with nothing but a world, a group and a heading.
 //
 // `undecided` reports that at least one candidate could not be judged because
 // its navmesh tile is not loaded (the tile is requested). A screen that failed
 // on an unloaded tile would refuse good spots on the first pass and, since the
 // NEW phase spawns the group at the top of its own block, a "wait and retry"
 // return would spawn a second group; so an undecided screen PASSES and the
 // existing twenty-second spawn deadline remains the only bound on waiting.
 static bool BoardingScreenClear(BaseWorld world, SCR_AIGroup group, vector position, vector direction, out bool undecided)
 {
  undecided = false;
  if (!world || !group) return false;
  AIPathfindingComponent path = AIPathfindingComponent.Cast(group.FindComponent(AIPathfindingComponent));
  if (!path || !path.GetNavmeshComponent()) return false;
  NavmeshWorldComponent mesh = path.GetNavmeshComponent();
  vector forward = direction; forward[1] = 0;
  if (forward.LengthSq() < 0.5) return false;
  forward.Normalize();
  vector right = Vector(forward[2], 0, -forward[0]);
  for (int i = 0; i < 4; i++)
  {
   float side = 1;
   if (i % 2 == 1) side = -1;
   float reach = 3.5;
   if (i >= 2) reach = 5;
   vector candidate = position + right * (side * reach);
   candidate[1] = world.GetSurfaceY(candidate[0], candidate[2]);
   if (!mesh.IsTileLoaded(candidate))
   {
    if (!mesh.IsTileRequested(candidate)) mesh.LoadTileIn(candidate);
    undecided = true;
    continue;
   }
   if (!mesh.IsTileValid(candidate)) continue;
   vector projected;
   if (!path.GetClosestPositionOnNavmesh(candidate, "2 2 2", projected)) continue;
   if (vector.Distance(candidate, projected) > 2) continue;
   if (!EAC_ExclusionZone.IsTransitAllowed(position, projected)) continue;
   return true;
  }
  return false;
 }

 protected bool BoardingGoal(EAC_TrafficParty party, out vector goal)
 {
  if (!party.Car || !party.Group) return false;
  goal = party.Car.GetOrigin();
  AIPathfindingComponent path = AIPathfindingComponent.Cast(party.Group.FindComponent(AIPathfindingComponent));
  if (!path || !path.GetNavmeshComponent()) return false;
  NavmeshWorldComponent mesh = path.GetNavmeshComponent();
  vector transform[4]; party.Car.GetWorldTransform(transform);
  vector right = transform[0]; right[1] = 0;
  if (right.LengthSq() < 0.5) return false;
  right.Normalize();
  for (int i = 0; i < 4; i++)
  {
   float side = 1;
   if (i % 2 == 1) side = -1;
   float reach = 3.5;
   if (i >= 2) reach = 5;
   vector candidate = party.Car.GetOrigin() + right * (side * reach);
   candidate[1] = m_World.GetSurfaceY(candidate[0], candidate[2]);
   if (!mesh.IsTileLoaded(candidate) || !mesh.IsTileValid(candidate)) continue;
   vector projected;
   if (!path.GetClosestPositionOnNavmesh(candidate, "2 2 2", projected)) continue;
   if (vector.Distance(candidate, projected) > 2) continue;
   // Reject anything that landed back inside the carve we are avoiding.
   if (vector.Distance(projected, party.Car.GetOrigin()) < 2.5) continue;
   if (!TransitAllowed(party, party.Car.GetOrigin(), projected)) continue;
   goal = projected; return true;
  }
  return false;
 }

 protected bool HealthyParty(EAC_TrafficParty party)
 {
  if (party.Car && !EAC_TrafficRoute.Healthy(party.Car)) return false;
  foreach (EAC_TrafficOccupant row : party.Crew)
   if (row.Actor && (!EAC_TrafficRoute.Healthy(row.Actor) || EAC_CivilianDanger.HasThreat(row.Actor))) return false;
  return true;
 }

 // `distance` is the module's TrafficSleepDistance (the last module's during a
 // controller gap). `seated` is set by the controller gap pump only: see
 // DrainParties.
 protected bool CanRemove(EAC_TrafficParty party, array<IEntity> observers, float distance, bool seated = false)
 {
  if (!party.Controlled() || party.Speed() > 1 || !EBG_PrefabFullCache.CanDeleteFullEntity(party.Group) || party.Group.GetChildren()) return false;
  if (party.Car && !EBG_PrefabFullCache.CanDeleteFullEntity(party.Car)) return false;
  foreach (EAC_TrafficOccupant occupant : party.Crew)
  {
   if (!occupant.Actor) continue;
   IEntity vehicle = CompartmentAccessComponent.GetVehicleIn(occupant.Actor);
   if ((vehicle && vehicle != party.Car) || occupant.Actor.EBG_WasPlayerControlled() || !EBG_PrefabFullCache.CanDeleteFullEntity(occupant.Actor)) return false;
  }
  if (party.ExclusionRemoval || party.SessionRemoval)
  {
   // An explicit zone removes the complete stopped owned party even when seated.
   return true;
  }
  if (seated)
  {
   // In or out of the car, but never caught halfway through a get-in or get-out.
   foreach (EAC_TrafficOccupant member : party.Crew)
   {
    if (!member.Actor) continue;
    CompartmentAccessComponent access = CompartmentAccessComponent.Cast(member.Actor.FindComponent(CompartmentAccessComponent));
    if (!access || access.IsGettingIn() || access.IsGettingOut()) return false;
   }
  }
  else if (!party.AllOutside()) return false;
  if (!HealthyParty(party) || !FarFromObservers(party, observers, distance)) return false;
  if (party.Car && !Hidden(party, party.Car.GetOrigin(), observers)) return false;
  foreach (EAC_TrafficOccupant row : party.Crew)
   if (row.Actor && !Hidden(party, row.Actor.GetOrigin(), observers)) return false;
  return true;
 }

 // The native boarding order can stall: the crew stands beside the car and never
 // starts the get-in (seen with a large AI-behaviour mod set loaded). While no player
 // can see the car, seat the next unseated crew member directly through the native
 // compartment API - the mirror of DismountHidden. One member per Step, the
 // pilot seat first, never a slot another character occupies or has reserved. The
 // pending get-in waypoint is replaced by the drive order once everyone is seated.
 protected bool BoardHidden(EAC_TrafficParty party, array<IEntity> observers, float now)
 {
  party.PendingReason = "board_hidden_state";
  if (!party.Controlled() || !party.Car || !party.CarControl || party.Speed() > 1 || party.AlarmUntil > now) return false;
  party.PendingReason = "board_hidden_visible";
  if (!Hidden(party, party.Car.GetOrigin(), observers)) return false;
  party.PendingReason = "board_hidden_health";
  if (!HealthyParty(party)) return false;
  BaseCompartmentManagerComponent manager = BaseCompartmentManagerComponent.Cast(party.Car.FindComponent(BaseCompartmentManagerComponent));
  BaseCompartmentSlot pilot = party.CarControl.GetPilotCompartmentSlot();
  if (!manager || !pilot) return false;
  foreach (EAC_TrafficOccupant row : party.Crew)
  {
   if (!row.Actor) return false;
   CompartmentAccessComponent access = CompartmentAccessComponent.Cast(row.Actor.FindComponent(CompartmentAccessComponent));
   if (!access) return false;
   if (access.IsGettingIn() || access.IsGettingOut()) { party.PendingReason = "board_hidden_native_pending"; return false; }
   if (CompartmentAccessComponent.GetVehicleIn(row.Actor) == party.Car) continue;
   BaseCompartmentSlot seat;
   if (!pilot.GetOccupant() && (!pilot.IsReserved() || pilot.IsReservedBy(row.Actor))) seat = pilot;
   if (!seat)
   {
    array<BaseCompartmentSlot> slots = {}; manager.GetCompartments(slots);
    foreach (BaseCompartmentSlot slot : slots)
    {
     if (!slot || slot.GetType() != ECompartmentType.CARGO || slot.GetOccupant()) continue;
     if (slot.IsReserved() && !slot.IsReservedBy(row.Actor)) continue;
     seat = slot; break;
    }
   }
   party.PendingReason = "board_hidden_no_seat";
   if (!seat) return false;
   party.PendingReason = "board_hidden_seat";
   bool seated;
   SCR_CompartmentAccessComponent scrAccess = SCR_CompartmentAccessComponent.Cast(access);
   if (scrAccess) seated = scrAccess.MoveInVehicle(party.Car, seat.GetType(), false, seat);
   else seated = access.GetInVehicle(party.Car, seat, true, -1, ECloseDoorAfterActions.INVALID, false);
   if (!seated) return false;
   if (m_BoardHidden < 1000000) m_BoardHidden++;
   return true;
  }
  return false;
 }

 // Right-hand kerb point some 14 m ahead of the car on the road it is standing on.
 protected bool KerbAhead(EAC_TrafficParty party, out vector goal)
 {
  ChimeraAIWorld ai = ChimeraAIWorld.Cast(GetGame().GetAIWorld());
  if (!ai || !ai.GetRoadNetworkManager() || !party.Car) return false;
  vector transform[4]; party.Car.GetWorldTransform(transform);
  vector forward = transform[2]; forward[1] = 0;
  if (forward.LengthSq() < 0.01) return false;
  forward.Normalize();
  vector near = party.Car.GetOrigin() + forward * 14;
  BaseRoad road; float distance;
  ai.GetRoadNetworkManager().GetClosestRoad(near, road, distance, true);
  if (!road) return false;
  array<vector> points = {}; road.GetPoints(points);
  vector direction;
  if (!EAC_TrafficRoute.ProjectLane(points, road.GetWidth(), near, near + forward * 20, goal, direction, true)) return false;
  goal[1] = m_World.GetSurfaceY(goal[0], goal[2]);
  if (vector.DistanceXZ(goal, party.Car.GetOrigin()) > 25 || vector.DistanceXZ(goal, party.Car.GetOrigin()) < 6) return false;
  return PointAllowed(party, goal);
 }

 // One-way model: the trip is over but a player still has the car in view, so the
 // party cannot be removed yet. The driver walks somewhere nearby every half minute
 // instead of standing beside the car (owner report 2026-09-19). One order per
 // call, rotating bearings, never onto a road and never through a no-go zone.
 protected bool Stroll(EAC_AmbientModule module, EAC_TrafficParty party, float now)
 {
  if (now < party.NextStroll || party.AlarmUntil > now || !party.Car || !party.Controlled()) return false;
  party.NextStroll = now + 30;
  for (int i = 0; i < 4; i++)
  {
   int bearing = m_GoalCursor++ % 8;
   float angle = bearing * 45;
   vector goal = party.Car.GetOrigin() + vector.FromYaw(angle) * 18;
   goal[1] = m_World.GetSurfaceY(goal[0], goal[2]);
   if (!EAC_ActivityStation.OffRoad(goal, 1) || !EAC_PedestrianSpawner.IsClear(m_World, goal)) continue;
   if (!PointAllowed(party, goal) || !TransitAllowed(party, party.Car.GetOrigin(), goal)) continue;
   if (!PlaceOrder(party, "{750A8D1695BD6998}Prefabs/AI/Waypoints/AIWaypoint_Move.et", goal)) return false;
   party.Phase = EAC_TrafficPhase.WALK; party.Since = now; party.PendingReason = "stroll";
   return true;
  }
  return false;
 }

 protected bool DismountHidden(EAC_AmbientModule module, EAC_TrafficParty party, array<IEntity> observers, float now)
 {
  party.PendingReason = "despawn_exit_ownership";
  if (!party.WantDespawn || !party.Controlled() || !party.Car) return false;
  party.PendingReason = "despawn_exit_speed";
  if (party.Speed() > 1) return false;
  party.PendingReason = "despawn_exit_alarm";
  if (party.AlarmUntil > now) return false;
  party.PendingReason = "despawn_exit_car_health";
  if (!EAC_TrafficRoute.Healthy(party.Car)) return false;
  foreach (EAC_TrafficOccupant member : party.Crew)
  {
   party.PendingReason = "despawn_exit_crew_health";
   if (!member.Actor || !EAC_TrafficRoute.Healthy(member.Actor)) return false;
   party.PendingReason = "despawn_exit_crew_threat";
   if (EAC_CivilianDanger.HasThreat(member.Actor)) return false;
  }
  party.PendingReason = "despawn_exit_car_near";
  if (Relevant(party.Car.GetOrigin(), observers, module.TrafficSleepDistance)) return false;
  party.PendingReason = "despawn_exit_car_visible";
  if (!Hidden(party, party.Car.GetOrigin(), observers)) return false;
  foreach (EAC_TrafficOccupant member : party.Crew)
  {
   party.PendingReason = "despawn_exit_crew_near";
   if (Relevant(member.Actor.GetOrigin(), observers, module.TrafficSleepDistance)) return false;
   party.PendingReason = "despawn_exit_crew_visible";
   if (!Hidden(party, member.Actor.GetOrigin(), observers)) return false;
  }
  party.PendingReason = "despawn_exit_navmesh_component";
  AIPathfindingComponent path = AIPathfindingComponent.Cast(party.Group.FindComponent(AIPathfindingComponent));
  if (!path || !path.GetNavmeshComponent()) return false;
  NavmeshWorldComponent mesh = path.GetNavmeshComponent();
  foreach (EAC_TrafficOccupant row : party.Crew)
  {
   if (CompartmentAccessComponent.GetVehicleIn(row.Actor) != party.Car) continue;
   CompartmentAccessComponent access = CompartmentAccessComponent.Cast(row.Actor.FindComponent(CompartmentAccessComponent));
   party.PendingReason = "despawn_exit_compartment_busy";
   if (!access || access.IsGettingIn()) return false;
   // Native owner-side teleport avoids waiting for a distant AI/door animation.
   // Eight local ground candidates, one native dismount at most per shared step.
   for (int i = 0; i < 8; i++)
   {
    vector position = party.Car.GetOrigin() + vector.FromYaw(i * 45) * 4;
    position[1] = m_World.GetSurfaceY(position[0], position[2]);
    party.PendingPosition = position; party.PendingReason = "despawn_exit_navmesh";
    if (!mesh.IsTileLoaded(position))
    {
     if (!mesh.IsTileRequested(position)) { mesh.LoadTileIn(position); return true; }
     continue;
    }
    vector projected;
    party.PendingReason = "despawn_exit_navmesh_projection";
    if (!mesh.IsTileValid(position) || !path.GetClosestPositionOnNavmesh(position, "2 2 2", projected) || vector.Distance(position, projected) > 2) continue;
    party.PendingReason = "despawn_exit_clearance"; party.PendingPosition = projected;
    // Navmesh-projected dismount point: roads are exactly where this runs.
    if (!PointAllowed(party, projected) || !TransitAllowed(party, party.Car.GetOrigin(), projected) || !EAC_PedestrianSpawner.IsNavmeshClear(m_World, projected)) continue;
    party.PendingReason = "despawn_exit_target_visible";
    if (!Hidden(party, projected, observers)) continue;
    party.PendingReason = "despawn_exit_recheck";
    if (!party.Controlled() || !party.WantDespawn || party.Speed() > 1 || !HealthyParty(party) || CompartmentAccessComponent.GetVehicleIn(row.Actor) != party.Car) return false;
    // Stop only our normal dismount producer before replacing its queued action.
    // Each native callback gets its own shared step and a fresh ownership check.
    if (party.Order)
    {
     party.PendingReason = "despawn_exit_cancel_order";
     party.ClearOrder();
     if (!row.Actor || !access || !party.Controlled()) party.Fail("ownership changed during despawn exit order cancellation");
     return true;
    }
    if (!row.ExitQueueInterrupted)
    {
     party.PendingReason = "despawn_exit_interrupt_queue";
     row.ExitQueueInterrupted = true;
     access.InterruptVehicleActionQueue(true, true, true);
     if (!row.Actor || !access || !party.Controlled()) party.Fail("ownership changed during despawn exit queue interruption");
     return true;
    }
    vector transform[4]; Math3D.DirectionAndUpMatrix(party.Direction, vector.Up, transform); transform[3] = projected;
    party.PendingReason = "despawn_exit_native";
    bool accepted = access.GetOutVehicle_NoDoor(transform, false, true);
    if (!row.Actor || !access || !party.Controlled()) { party.Fail("ownership changed during hidden despawn dismount"); return true; }
    if (!accepted) party.PendingReason = "despawn_exit_native_refused";
    else if (access.IsInCompartment()) party.PendingReason = "despawn_exit_native_pending";
    else party.PendingReason = "despawn_exit_native_outside";
    return true;
   }
   return false;
  }
  return false;
 }

 protected bool RemoveOne(EAC_TrafficParty party, array<IEntity> observers, float distance, bool seated = false)
 {
  if (!CanRemove(party, observers, distance, seated)) return false;
  party.ClearOrder();
  if (party.Order) return false;
  party.Deleting = true;
  foreach (EAC_TrafficOccupant row : party.Crew)
  {
   if (!row.Actor) continue;
   // Recheck the exact party after every native callback; one deletion per Step.
   if (!party.Controlled()) return false;
   row.Detach(); SCR_EntityHelper.DeleteEntityAndChildren(row.Actor);
   if (row.Actor) row.Bind();
   return true;
  }
  if (party.Car)
  {
   if (!party.Controlled()) return false;
   party.UnbindCar(); party.HadCar = false; SCR_EntityHelper.DeleteEntityAndChildren(party.Car);
   if (party.Car) { party.BindCar(); party.HadCar = true; }
   return true;
  }
  if (party.Group)
  {
   if (party.Group.GetAgentsCount() != 0 || party.Group.GetPlayerCount() != 0) return false;
   SCR_EntityHelper.DeleteEntityAndChildren(party.Group); return true;
  }
  return false;
 }

 // Issue #1. An owned car can disappear without this director: vanilla puts a
 // vehicle into its garbage system when the last occupant leaves it
 // (VehicleControllerComponent.OnCompartmentLeft -> SCR_GarbageSystem.Insert; the
 // default vehicle rule removes it after 1,200 s with no player within 35 m),
 // which is what took the cars a STUCK crew had left, and a Game Master can
 // delete it. That is accepted rather than fought: count it once, drop the dead
 // car bindings and fail the journey, so the crew and group go through the
 // ordinary protected cleanup and the reservation is released with the last of
 // them. A car a player entered is never in that cleanup (PlayerTouched).
 protected void LoseCar(EAC_TrafficParty party)
 {
  party.HadCar = false; party.UnbindCar();
  if (m_CarsLost < 1000000) m_CarsLost++;
  // A zone or session removal already owns this record's end.
  if (!party.Deleting && !party.ExclusionRemoval && !party.SessionRemoval) party.Fail("vehicle externally removed");
 }

 // Issue #1. A failed party whose car and actors are all gone - the car and a
 // corpse taken by vanilla garbage collection, or deleted by a Game Master - can
 // still hold its slot through the empty group alone (SetDeleteWhenEmpty is
 // off). A dead or player-touched member makes the record permanently
 // uncontrolled, so CanRemove never cleared that group and the slot was lost
 // for the session. Same guards as the pedestrian orphan group: our own group,
 // server authority, no agents, no players, no children, deletable. Nothing but
 // the group and our own order is touched; never a car, a survivor or a corpse.
 protected bool RemoveOrphanGroup(EAC_TrafficParty party)
 {
  if (party.Phase != EAC_TrafficPhase.FAILED || party.Car || !party.Group) return false;
  foreach (EAC_TrafficOccupant row : party.Crew) if (row.Actor) return false;
  SCR_AIGroup group = party.Group;
  if (group.GetAgentsCount() != 0 || group.GetPlayerCount() != 0 || group.GetChildren()) return false;
  RplComponent groupRpl = RplComponent.Cast(group.FindComponent(RplComponent));
  if (!groupRpl || groupRpl.IsProxy() || !groupRpl.IsOwner() || !EBG_PrefabFullCache.CanDeleteFullEntity(group)) return false;
  party.ClearOrder();
  if (party.Order) return false;
  party.Deleting = true;
  SCR_EntityHelper.DeleteEntityAndChildren(group);
  return true;
 }

 protected bool OwnsSimulationAgent(EAC_TrafficParty party, AIAgent agent)
 {
  if (!agent || agent.GetPermanentLOD() >= 0 || !party.Controlled()) return false;
  if (agent == party.Group) return true;
  return agent.GetParentGroup() == party.Group && party.Owns(agent.GetControlledEntity());
 }

 protected bool RefreshSimulationAgent(EAC_TrafficParty party, AIAgent agent)
 {
  if (!OwnsSimulationAgent(party, agent)) return false;
  agent.PreventMaxLOD(5);
  if (!OwnsSimulationAgent(party, agent)) return false;
  // Native PreventMaxLOD does not change an agent already at disabled MaxLOD.
  if (agent.GetLOD() >= AIAgent.GetMaxLOD())
  {
   agent.SetLOD(Math.Max(0, AIAgent.GetMaxLOD() - 1));
   if (!OwnsSimulationAgent(party, agent)) return false;
  }
  if (!agent.IsAIActivated())
  {
   agent.ActivateAI();
   if (!OwnsSimulationAgent(party, agent)) return false;
  }
  return true;
 }

 protected void RefreshSimulation(EAC_AmbientModule module, EAC_TrafficParty party, array<IEntity> observers, float now)
 {
  if (party.Phase < EAC_TrafficPhase.BOARD || party.Phase > EAC_TrafficPhase.EXIT || !party.Car || !party.Controlled()) return;
  bool near = Relevant(party.Car.GetOrigin(), observers, module.TrafficSleepDistance);
  // Distant despawn preparation gets a bounded grace period, not a permanent LOD.
  // Failed/protected cleanup cannot keep distant AI running indefinitely.
  if (!near && (party.Retire || now - party.Since > module.ClearDelay + 60)) return;
  if (!RefreshSimulationAgent(party, party.Group)) return;
  foreach (EAC_TrafficOccupant row : party.Crew)
  {
   if (!row.Actor || !party.Controlled()) return;
   AIControlComponent control = AIControlComponent.Cast(row.Actor.FindComponent(AIControlComponent));
   if (!control || !RefreshSimulationAgent(party, control.GetAIAgent())) return;
  }
 }

 protected void Monitor(EAC_AmbientModule module, EAC_TrafficParty party, array<IEntity> observers, float now)
 {
  if (!party.HasEntities()) return;
  // Any phase, failed records included: see LoseCar.
  if (party.HadCar && !party.Car) LoseCar(party);
  if (party.ExclusionRemoval || party.SessionRemoval) return;
  m_PeakSpeed = Math.Max(m_PeakSpeed, party.Speed());
  if (!party.Controlled()) { party.Fail("ownership, death or foreign occupant"); return; }
  if (party.Phase == EAC_TrafficPhase.FAILED) return;
  if (!party.Deleting && party.Phase >= EAC_TrafficPhase.BOARD)
  {
   if (!party.Car) { party.Fail("vehicle externally removed"); return; }
   // A parked car never had a crew, in PARKED or on its way out through CLEARING.
   if (party.Phase != EAC_TrafficPhase.PARKED && party.Spawned > 0)
   {
    foreach (EAC_TrafficOccupant row : party.Crew)
     if (!row.Actor) { party.Fail("crew externally removed; never replenish"); return; }
   }
  }
  RefreshSimulation(module, party, observers, now);
  if (!party.Controlled()) { party.Fail("ownership changed during simulation refresh"); return; }
  foreach (EAC_TrafficOccupant row : party.Crew)
   if (row.Actor && EAC_CivilianDanger.HasCombatThreat(row.Actor))
   {
    if (party.AlarmUntil <= now) EAC_CivilianDanger.TraceAlarm(module, row.Actor, now);
    party.AlarmUntil = now + module.CalmDelay;
   }
  if (party.Phase == EAC_TrafficPhase.DRIVE && party.Movement)
  {
   // The native path is needed only by transit screening or the stop diagnostic.
   // With no blocking zone, skip copying the full path on every driving tick.
   // A driving party has departed: the module disc is no longer a transit limit
   // (EAC_TrafficParty.Departed), so only real transit-blocking zones sweep.
   bool sweepRoute = EAC_ExclusionZone.AnyJourneyTransitBlocked();
   if (!party.Departed) sweepRoute = EAC_ExclusionZone.AnyTransitBlocked();
   m_PathScratch.Clear();
   if (sweepRoute) party.Movement.GetCurrentPath(m_PathScratch);
   string blockedReason;
   if (!TransitAllowed(party, party.Car.GetOrigin(), party.Destination)) blockedReason = "destination_exclusion";
   bool allowed = blockedReason.IsEmpty();
   vector previous = party.Car.GetOrigin();
   // Scan the nearest PATH_SCAN_LIMIT points only. The native path holds the part
   // still to be driven, so it shrinks as the car advances and every later point
   // enters this prefix on a subsequent shared step. Per-tick work stays bounded
   // at exactly the old ceiling, and a long legal route is no longer mistaken for
   // a blocked one. The destination test above still rejects an excluded endpoint
   // immediately, whatever the path length.
   // With no transit-blocking zone placed anywhere in the mission, every one of
   // those IsTransitAllowed calls can only answer true, so the whole scan is
   // skipped rather than walked 128 points deep for every driving party on every
   // tick. The destination test above is subject to the same fact and is left
   // alone: it is one call, not a loop.
   int scan;
   if (sweepRoute) scan = PathScanCount(m_PathScratch.Count());
   for (int i = 0; allowed && i < scan; i++)
   {
    allowed = TransitAllowed(party, previous, m_PathScratch[i]); previous = m_PathScratch[i];
    if (!allowed) blockedReason = "path_exclusion";
   }
   if (!allowed || party.AlarmUntil > now)
   {
    if (module.DebugLevel >= 2)
    {
     // An alarm can stop a car without any exclusion zone. Preserve the actual
     // path count in that diagnostic without paying for it on ordinary ticks.
     if (!sweepRoute) party.Movement.GetCurrentPath(m_PathScratch);
     PrintFormat("[EAC TRAFFIC STOP] party=%1 alarm=%2 blocked=%3 path_points=%4 pos=%5", party.Id, party.AlarmUntil > now, blockedReason, m_PathScratch.Count(), party.Car.GetOrigin());
    }
    // A journey stopped by geometry before the car ever moved is not traffic.
    // Three such journeys retire the record instead of churning BOARD/DRIVE/
    // PARK/EXIT/REST forever, which is what starved admission in runs 76/77/88
    // and what put a permanently parked car under the boarding waypoint that
    // killed runs 83 and 85.
    if (!allowed && !party.Moved)
    {
     if (party.BlockedTrips < 1000000) party.BlockedTrips++;
     if (party.BlockedTrips >= BLOCKED_TRIP_LIMIT) party.Retire = true;
    }
    party.ClearOrder(); party.Movement.SetCruiseSpeed(0); party.Phase = EAC_TrafficPhase.PARK; party.Since = now;
   }
   else if (vector.Distance(party.ProgressPosition, party.Car.GetOrigin()) > 5)
   { party.ProgressAt = now; party.ProgressPosition = party.Car.GetOrigin(); party.Moved = true; party.BlockedTrips = 0; party.StallRecovered = false; }
   else if (now - party.ProgressAt > module.TrafficStuckDelay)
   {
    // A car held by a player, by another party or by a transient native path
    // failure earns exactly one fresh drive order before the record retires.
    // Advance places it: that method owns the one physical operation per Step.
    if (!party.StallRecovered) party.StallRetry = true;
    else { party.Retire = true; party.ClearOrder(); party.Movement.SetCruiseSpeed(0); party.Phase = EAC_TrafficPhase.PARK; party.Since = now; }
   }
  }
  bool far = party.Car && FarFromObservers(party, observers, module.TrafficSleepDistance);
  if (far && party.AlarmUntil <= now && Hidden(party, party.Car.GetOrigin(), observers))
  {
   if (party.ClearSince <= 0 || now - party.LastClearSample > 1.5) party.ClearSince = now;
   party.LastClearSample = now;
   if (now - party.ClearSince >= module.ClearDelay) party.WantDespawn = true;
  }
  else { party.ClearSince = 0; party.WantDespawn = false; }
 }

 protected bool Advance(EAC_AmbientModule module, EAC_TrafficParty party, array<IEntity> observers, float now)
 {
  if (!party.HasEntities() && party.Reserved && (party.Deleting || party.Phase == EAC_TrafficPhase.FAILED))
  {
   if (!module.ReleasePopulation(party.Id)) return false;
   // Keep recovery ownership and budget until every entity is gone. A later
   // admission selects a new car, crew and route through the ordinary gates.
   party.Reserved = false; m_Parties.RemoveItem(party);
   if (m_DespawnCount < 1000000) m_DespawnCount++;
   return true;
  }
  if (RemoveOrphanGroup(party)) return true;
  // Default no-civilian zones also clear traffic. Population-only zones retain
  // their explicit pass-through contract; spawn points remain excluded.
  bool excluded = party.ExclusionRemoval || party.SessionRemoval;
  if (!excluded)
  {
   vector current = party.Position;
   if (party.Car) current = party.Car.GetOrigin();
   // Before departure the module disc retires a party like any other owned
   // population (a moved or shrunk module). A departed car is expected to leave
   // the disc on its way out of town; distance and visibility remove it then.
   bool bounded = !party.Departed;
   excluded = (bounded && !module.ContainsPopulationPosition(current)) || EAC_ExclusionZone.IsManualPopulationExcluded(current, true);
   foreach (EAC_TrafficOccupant member : party.Crew)
    if (member.Actor && ((bounded && !module.ContainsPopulationPosition(member.Actor.GetOrigin())) || EAC_ExclusionZone.IsManualPopulationExcluded(member.Actor.GetOrigin(), true))) excluded = true;
  }
  if (excluded)
  {
   if (!party.HasEntities()) { party.Retire = true; party.Deleting = true; return true; }
   if (!party.Controlled()) return false;
   if (!party.SessionRemoval) party.ExclusionRemoval = true;
   party.Retire = true; party.WantDespawn = false;
   if (party.SessionRemoval) party.PendingReason = "session_load";
   else party.PendingReason = "manual_exclusion";
   // Stop the owned car before removing its driver. Other parties still get a
   // turn while this party waits for physics, protection or native deletion.
   if (party.Movement) party.Movement.SetCruiseSpeed(0);
   if (!party.Controlled()) return false;
   if (party.CarControl) party.CarControl.SetPersistentHandBrake(true);
   return RemoveOne(party, observers, module.TrafficSleepDistance);
  }
  if (party.Phase == EAC_TrafficPhase.FAILED && party.Controlled() && !party.AllOutside() && party.Car && party.CarControl)
  {
   if (party.Speed() > 2) return false;
   party.CarControl.SetPersistentHandBrake(true);
   if (PlaceOrder(party, "{C40316EE26846CAB}Prefabs/AI/Waypoints/AIWaypoint_GetOut.et", party.Car.GetOrigin()))
   { party.Phase = EAC_TrafficPhase.EXIT; party.Since = now; party.Retire = true; return true; }
  }
  if (party.Phase == EAC_TrafficPhase.CLEARING || party.Phase == EAC_TrafficPhase.FAILED) return RemoveOne(party, observers, module.TrafficSleepDistance);
  if (party.Phase <= EAC_TrafficPhase.SPAWN_CREW && now - party.Since > 20) { party.Fail("spawn deadline"); return true; }
  if (party.Phase == EAC_TrafficPhase.NEW)
  {
   party.PendingReason = "group_visible"; party.PendingPosition = party.Position;
   if (!Hidden(party, party.Position, observers)) return false;
   party.PendingReason = "group_spawn";
   party.Group = SCR_AIGroup.Cast(Spawn("{000CD338713F2B5A}Prefabs/AI/Groups/Group_Base.et", party.Position, party.Direction, true));
   if (!party.Group) { party.Fail("group spawn"); return true; }
   EAC_SessionLifecycle.Keep(party.Group);
   party.Group.SetDeleteWhenEmpty(false); party.Group.SetCanDeleteIfNoPlayer(false);
   Faction faction = GetGame().GetFactionManager().GetFactionByKey(EAC_AmbientModule.CIV_FACTION);
   if (!faction) { party.Fail("CIV unavailable"); return true; }
   party.Group.SetFaction(faction);
   SCR_AIGroupSettingsComponent settings = SCR_AIGroupSettingsComponent.Cast(party.Group.FindComponent(SCR_AIGroupSettingsComponent));
   if (!settings || !settings.AddSetting(SCR_AIGroupCombatModeSetting.Create(SCR_EAISettingOrigin.SCENARIO, EAIGroupCombatMode.HOLD_FIRE), false, true)) { party.Fail("civilian settings"); return true; }
   party.Group.ActivateAI();
   // Vanilla SCR_AIProcessFailedMovementResult raises a fatal NodeError on
   // EMoveError.UNKNOWN. An ambient traffic group must never be able to take the
   // mission down, so defuse that one branch for this group only.
   EAC_MoveFailureGuard.Attach(party.Group, "traffic");
   // Screen the boarding ground now, with the group's pathfinder and before a car
   // or crew exists. Refusing here costs one group; refusing at SPAWN_CREW costs
   // a car, a crew and the twenty-second spawn deadline.
   bool boardingUndecided;
   party.PendingReason = "boarding_navmesh_screen";
   if (!BoardingScreenClear(m_World, party.Group, party.Position, party.Direction, boardingUndecided) && !boardingUndecided)
   {
    // Retire this attempt; the bounded admission scan selects another point.
    if (m_BoardingScreened < 1000000) m_BoardingScreened++;
    party.Fail("boarding navmesh");
    return true;
   }
   party.Phase = EAC_TrafficPhase.SPAWN_CAR; return true;
  }
  if (!party.Controlled()) return false;
  if (party.Phase == EAC_TrafficPhase.SPAWN_CAR)
  {
   party.PendingReason = "car_visible"; party.PendingPosition = party.Position;
   float carDistance = 60;
   if (module.TrafficRoundTrips == 0) carDistance = CAR_SPAWN_DISTANCE;
   if (!Hidden(party, party.Position, observers, carDistance)) return false;
   party.PendingReason = "car_clearance";
   if (!EAC_TrafficRoute.Clear(m_World, party.Position)) return false;
   party.PendingReason = "car_spawn";
   party.Car = Spawn(party.CarPrefab, party.Position + "0 0.25 0", party.Direction, true);
   if (party.Car) party.HadCar = true;
   if (!party.Car || !party.BindCar() || !party.Controlled()) { party.Fail("vehicle capabilities or ownership"); return true; }
   EAC_SessionLifecycle.Keep(party.Car);
   FactionAffiliationComponent carFaction = FactionAffiliationComponent.Cast(party.Car.FindComponent(FactionAffiliationComponent));
   if (carFaction) carFaction.SetAffiliatedFactionByKey(EAC_AmbientModule.CIV_FACTION);
   if (!party.Controlled()) { party.Fail("vehicle ownership changed"); return true; }
   // One-way model: the car now simply stands at the kerb. Its driver comes later,
   // on foot, which is what the owner asked a town's traffic to look like.
   if (module.TrafficRoundTrips == 0)
   {
    if (party.CarControl) party.CarControl.SetPersistentHandBrake(true);
    party.Phase = EAC_TrafficPhase.PARKED; party.Since = now;
    party.DepartAt = now + Math.RandomFloat(PARKED_MIN_SECONDS, PARKED_MAX_SECONDS);
    party.PendingReason = "parked";
    return true;
   }
   party.Phase = EAC_TrafficPhase.SPAWN_CREW; return true;
  }
  if (party.Phase == EAC_TrafficPhase.PARKED)
  {
   // Far away and unseen, a parked car is simply removed; nothing is cached.
   if (party.WantDespawn || party.Retire) { party.Retire = true; party.Phase = EAC_TrafficPhase.CLEARING; return true; }
   if (now < party.DepartAt || party.AlarmUntil > now) return false;
   party.PendingReason = "parked_pending"; if (GetPendingCount() != 0) return false;
   party.PendingReason = "parked_load"; if (!module.CanAdmitNewWork()) return false;
   party.Phase = EAC_TrafficPhase.SPAWN_CREW; party.Since = now; party.CrewProbe = 0;
   return true;
  }
  if (party.Phase == EAC_TrafficPhase.SPAWN_CREW)
  {
   if (party.Spawned >= party.Crew.Count())
   {
    vector boardGoal;
    // No navmesh point beside the car yet: wait rather than aim at the carve.
    // The existing 20-second spawn deadline still bounds this wait.
    if (!BoardingGoal(party, boardGoal)) { party.PendingReason = "boarding_navmesh"; return false; }
    if (!PlaceOrder(party, "{8AD8C82346156494}Prefabs/AI/Waypoints/AIWaypoint_GetInSelected.et", boardGoal, true)) party.Fail("boarding order");
    else { party.Phase = EAC_TrafficPhase.BOARD; party.Since = now; }
    return true;
   }
   vector position = party.Position + Vector(-party.Direction[2], 0, party.Direction[0]) * 5 + party.Direction * (party.Spawned * 2 - 3);
   // One-way model: the driver appears on the house side, 22, 16 or 10 m from the
   // car, and walks to it. CrewProbe 3 is the old spot beside the car, so a street
   // with no usable ground further out still gets its driver.
   bool walkIn = module.TrafficRoundTrips == 0 && party.CrewProbe < 3;
   if (walkIn)
   {
    vector toHome = party.HomePosition - party.Position; toHome[1] = 0;
    if (toHome.LengthSq() < 4) toHome = Vector(-party.Direction[2], 0, party.Direction[0]);
    toHome.Normalize();
    position = party.Position + toHome * (22 - party.CrewProbe * 6) + party.Direction * (party.Spawned * 2);
   }
   position[1] = m_World.GetSurfaceY(position[0], position[2]);
   party.PendingPosition = position; party.PendingReason = "crew_navmesh_component";
   AIPathfindingComponent path = AIPathfindingComponent.Cast(party.Group.FindComponent(AIPathfindingComponent));
   if (!path || !path.GetNavmeshComponent()) { party.Fail("crew navmesh unavailable"); return true; }
   NavmeshWorldComponent mesh = path.GetNavmeshComponent();
   party.PendingReason = "crew_navmesh_tile";
   if (!mesh.IsTileLoaded(position)) { if (!mesh.IsTileRequested(position)) mesh.LoadTileIn(position); return true; }
   vector projected;
   party.PendingReason = "crew_navmesh_projection";
   if (!mesh.IsTileValid(position) || !path.GetClosestPositionOnNavmesh(position, "2 2 2", projected) || vector.Distance(projected, position) > 2)
   {
    if (walkIn) { party.CrewProbe++; party.Since = now; return true; }
    party.Fail("crew navmesh projection"); return true;
   }
   position = projected;
   party.PendingPosition = position; party.PendingReason = "crew_visible";
   if (!Hidden(party, position, observers)) return false;
   party.PendingReason = "crew_clearance";
   // `position` was replaced by its navmesh projection above.
   if (!EAC_PedestrianSpawner.IsNavmeshClear(m_World, position) || (walkIn && !EAC_ActivityStation.OffRoad(position, 0.5)))
   {
    if (walkIn) { party.CrewProbe++; party.Since = now; return true; }
    return false;
   }
   party.PendingReason = "crew_spawn";
   EAC_TrafficOccupant row = party.Crew[party.Spawned];
   row.Actor = SCR_ChimeraCharacter.Cast(Spawn(row.Prefab, position, party.Direction, true));
   if (!row.Actor) { party.Fail("crew spawn"); return true; }
   row.Bind();
   EAC_SessionLifecycle.Keep(row.Actor);
   if (!party.Controlled()) { party.Fail("crew control"); return true; }
   AIControlComponent control = AIControlComponent.Cast(row.Actor.FindComponent(AIControlComponent));
   FactionAffiliationComponent affiliation = FactionAffiliationComponent.Cast(row.Actor.FindComponent(FactionAffiliationComponent));
   if (affiliation) affiliation.SetAffiliatedFactionByKey(EAC_AmbientModule.CIV_FACTION);
   if (!party.Controlled()) { party.Fail("crew ownership changed during affiliation"); return true; }
   BaseWeaponManagerComponent weapons = BaseWeaponManagerComponent.Cast(row.Actor.FindComponent(BaseWeaponManagerComponent));
   InventoryStorageManagerComponent inventory = InventoryStorageManagerComponent.Cast(row.Actor.FindComponent(InventoryStorageManagerComponent));
   array<IEntity> carried = {}; if (weapons) weapons.GetWeaponsList(carried);
   if (!control || !control.GetAIAgent() || !affiliation || !affiliation.GetAffiliatedFaction() || affiliation.GetAffiliatedFaction().GetFactionKey() != EAC_AmbientModule.CIV_FACTION || !weapons || !inventory || inventory.GetGrenadesCount() != 0 || !carried.IsEmpty()) { party.Fail("crew must be unarmed CIV AI"); return true; }
   inventory.GetItems(carried, EStoragePurpose.PURPOSE_ANY);
   if (carried.Count() > 128) { party.Fail("crew inventory unsupported"); return true; }
   foreach (IEntity item : carried) if (item && item.FindComponent(BaseWeaponComponent)) { party.Fail("crew carries weapon"); return true; }
   party.Group.AddAgent(control.GetAIAgent()); row.Joined = true; control.GetAIAgent().ActivateAI(); party.Spawned++; return true;
  }
  if (party.Phase == EAC_TrafficPhase.BOARD)
  {
   if (!party.AllSeated())
   {
    float boardingDeadline = 45;
    if (module.TrafficRoundTrips == 0) boardingDeadline = BOARD_WALK_DEADLINE;
    if (now - party.Since > boardingDeadline) { party.Fail("boarding deadline"); return false; }
    if (now - party.Since >= BOARD_HIDDEN_SECONDS) return BoardHidden(party, observers, now);
    return false;
   }
   if (party.CarControl) party.CarControl.SetPersistentHandBrake(false);
   party.Movement.SetCruiseSpeed(EAC_TrafficParty.CruiseRequest(module.TrafficSpeed, party.Speed()));
   if (!PlaceOrder(party, "{750A8D1695BD6998}Prefabs/AI/Waypoints/AIWaypoint_Move.et", party.Destination)) party.Fail("drive order");
   else { party.Phase = EAC_TrafficPhase.DRIVE; party.Departed = true; party.PendingReason = ""; party.Since = now; party.ProgressAt = now; party.ProgressPosition = party.Car.GetOrigin(); party.Moved = false; party.StallRetry = false; party.StallRecovered = false; if (m_JourneysStarted < 1000000) m_JourneysStarted++; }
   return true;
  }
  if (party.Phase == EAC_TrafficPhase.DRIVE)
  {
   if (!party.AllSeated())
   {
    // The native move order can give up before the car ever moves: far from every
    // player the group dismounts and sets off on foot (vanilla run 128 and the
    // 224-addon run x6, both parked at the same far-LOD spot). That journey never
    // happened, so it is a blocked trip like a geometry stop, not a failed party:
    // PARK issues the ordinary dismount and the crew rests, despawns or boards again,
    // and three in a row still retire the record. A crew that leaves a car that has
    // actually driven is still a failure.
    if (party.Moved)
    {
     // Normally a native vehicle STUCK: vanilla SCR_AIProcessFailedMovementResult
     // answers it with a high-priority get-out before TrafficStuckDelay can offer
     // the re-order (issue #1). The stop line carries where the car stands and the
     // group's last native move result, so a junction is identifiable from the log.
     vector failedAt;
     int moveResult = EAC_MoveFailureGuard.PeekResult(party.Group, failedAt);
     if (party.Car) party.PendingPosition = party.Car.GetOrigin();
     party.PendingReason = "crew_left native_move=" + moveResult.ToString();
     party.Fail("crew left vehicle while driving"); return true;
    }
    if (party.BlockedTrips < 1000000) party.BlockedTrips++;
    if (party.BlockedTrips >= BLOCKED_TRIP_LIMIT) party.Retire = true;
    if (m_DriveAbandoned < 1000000) m_DriveAbandoned++;
    party.ClearOrder(); party.Movement.SetCruiseSpeed(0); party.Phase = EAC_TrafficPhase.PARK; party.Since = now; party.PendingReason = "drive_abandoned";
    return true;
   }
   party.Movement.SetCruiseSpeed(EAC_TrafficParty.CruiseRequest(module.TrafficSpeed, party.Speed()));
   if (party.StallRetry)
   {
    // The one re-order Monitor asked for. One waypoint, once per journey, inside
    // the bounded per-Step slot; a refusal retires the record on the next stall.
    party.StallRetry = false; party.StallRecovered = true;
    party.ProgressAt = now; party.ProgressPosition = party.Car.GetOrigin();
    if (!PlaceOrder(party, "{750A8D1695BD6998}Prefabs/AI/Waypoints/AIWaypoint_Move.et", party.Destination)) party.Retire = true;
    return true;
   }
   // One arrival radius for the whole journey; see ARRIVAL_M. Counting only at
   // 15 m left a 15-30 m dead band where the car had demonstrably arrived, REST
   // swapped to the return leg, and the completion counter still read zero.
   bool arrived = vector.Distance(party.Car.GetOrigin(), party.Destination) <= ARRIVAL_M;
   if (!arrived && !party.WantDespawn && !party.Retire && !party.PullingOver) return false;
   if (party.PullingOver)
   {
    // The short last move to the kerb: done when the car is there, when fifteen
    // seconds have passed, or when the party has to leave for another reason.
    if (!party.WantDespawn && !party.Retire && now < party.PullOverUntil && vector.DistanceXZ(party.Car.GetOrigin(), party.PullOverGoal) > 3.5) return false;
    party.PullingOver = false;
   }
   else
   {
    if (arrived && m_JourneysCompleted < 1000000) m_JourneysCompleted++;
    // One-way model: an arrival a player can see used to stop dead in its lane
    // (owner report 2026-09-19). One attempt per journey to pull over first.
    if (arrived && module.TrafficRoundTrips == 0 && !party.WantDespawn && !party.Retire && !party.PulledOver)
    {
     party.PulledOver = true;
     vector kerb;
     if (KerbAhead(party, kerb) && PlaceOrder(party, "{750A8D1695BD6998}Prefabs/AI/Waypoints/AIWaypoint_Move.et", kerb))
     {
      party.PullingOver = true; party.PullOverGoal = kerb; party.PullOverUntil = now + 15;
      party.Movement.SetCruiseSpeed(Math.Min(15, module.TrafficSpeed));
      party.PendingReason = "pull_over";
      return true;
     }
    }
   }
   party.ClearOrder(); party.Movement.SetCruiseSpeed(0); party.Phase = EAC_TrafficPhase.PARK; party.Since = now; return true;
  }
  if (party.Phase == EAC_TrafficPhase.PARK)
  {
   if (party.Speed() > 2) return false;
   party.CarControl.SetPersistentHandBrake(true);
   foreach (EAC_TrafficOccupant row : party.Crew) row.ExitQueueInterrupted = false;
   if (!PlaceOrder(party, "{C40316EE26846CAB}Prefabs/AI/Waypoints/AIWaypoint_GetOut.et", party.Car.GetOrigin())) party.Fail("dismount order");
   else { party.Phase = EAC_TrafficPhase.EXIT; party.Since = now; party.PendingReason = "native_dismount"; party.PendingPosition = party.Car.GetOrigin(); }
   return true;
  }
  if (party.Phase == EAC_TrafficPhase.EXIT)
  {
   if (!party.AllOutside())
   {
    if (party.WantDespawn)
    {
     if (now - party.Since >= 10) return DismountHidden(module, party, observers, now);
     return false;
    }
    if (now - party.Since > 30) party.Fail("dismount deadline");
    return false;
   }
   party.ClearOrder(); party.Phase = EAC_TrafficPhase.REST; party.NextTrip = now + module.TrafficInterval;
   // The native dismount leaves the engine running (owner report 2026-09-19).
   if (party.CarControl && party.CarControl.IsEngineOn()) party.CarControl.StopEngine(false);
   if (party.AlarmUntil > now || party.WantDespawn || party.Retire) return true;
   vector goal = party.Car.GetOrigin() + Vector(-party.Direction[2], 0, party.Direction[0]) * 10;
   goal[1] = m_World.GetSurfaceY(goal[0], goal[2]);
   if (EAC_PedestrianSpawner.IsClear(m_World, goal) && TransitAllowed(party, party.Car.GetOrigin(), goal))
    if (PlaceOrder(party, "{750A8D1695BD6998}Prefabs/AI/Waypoints/AIWaypoint_Move.et", goal)) { party.Phase = EAC_TrafficPhase.WALK; party.Since = now; }
   return true;
  }
  if (party.Phase == EAC_TrafficPhase.WALK)
  {
   // A one-way driver whose trip is over keeps strolling while a player has the car
   // in view; Retire is set for it, so Retire alone must not cut the walk short.
   bool strolling = module.TrafficRoundTrips == 0 && party.Retire && !party.WantDespawn;
   if (now - party.Since < 20 && (strolling || (!party.WantDespawn && !party.Retire)) && party.AlarmUntil <= now) return false;
   party.ClearOrder(); party.Phase = EAC_TrafficPhase.REST; return true;
  }
  if (party.Phase == EAC_TrafficPhase.REST)
  {
   if (party.WantDespawn || party.Retire)
   {
    if (!CanRemove(party, observers, module.TrafficSleepDistance))
    {
     if (module.TrafficRoundTrips == 0 && !party.WantDespawn) return Stroll(module, party, now);
     return false;
    }
    party.Phase = EAC_TrafficPhase.CLEARING; return true;
   }
   // One-way model: the trip is over. The driver has walked off, the car stands with
   // its engine off, and both are removed as soon as no player can see them.
   if (module.TrafficRoundTrips == 0) { party.Retire = true; return true; }
   if (now < party.NextTrip || party.AlarmUntil > now || !HealthyParty(party)) return false;
   // Same ARRIVAL_M the DRIVE phase now counts a completion at, so a leg can
   // never be swapped for a journey the counter refused to credit.
   if (vector.Distance(party.Car.GetOrigin(), party.Destination) <= ARRIVAL_M)
   {
    vector old = party.Destination; party.Destination = party.ReturnDestination; party.ReturnDestination = old;
   }
   if (!TransitAllowed(party, party.Car.GetOrigin(), party.Destination)) { party.Retire = true; return true; }
   vector returnGoal;
   // Same carve hazard as the first boarding order; REST simply retries later.
   if (!BoardingGoal(party, returnGoal)) { party.PendingReason = "boarding_navmesh"; return false; }
   if (!PlaceOrder(party, "{8AD8C82346156494}Prefabs/AI/Waypoints/AIWaypoint_GetInSelected.et", returnGoal, true)) party.Fail("return boarding");
   else { party.Phase = EAC_TrafficPhase.BOARD; party.Since = now; }
   return true;
  }
  return false;
 }

 // Destination town for a car leaving `home`. Bounded: one pass over at most
 // EAC_SettlementIndex.MAX_CENTRES centres, keeping the best three in two small
 // retained arrays.
 //
 // The car drives out of town, so the module area does not constrain the
 // destination (EAC_ExclusionZone.IsJourneyPointAllowed); requiring it inside the
 // disc is what left `town_goal` refusing every attempt in the live run. Towns
 // whose centre lies beyond the traffic despawn distance from the module area are
 // preferred, so the car is normally removed on the road, far away and unseen,
 // rather than parked in view of the village it left; the nearest town 500 m or
 // more away remains the fallback. Rotating over the best three keeps one town
 // centre the road lookup cannot reach (100 m tolerance) from refusing every
 // admission from this village.
 protected bool PickTown(EAC_AmbientModule module, vector home, out vector goal)
 {
  m_GoalScratch.Clear(); m_GoalRank.Clear();
  float preferred = Math.Min(module.TrafficSleepDistance + module.SettlementRadius, 4500);
  vector centre = module.GetOrigin();
  int townCount = EAC_SettlementIndex.GetCount();
  for (int i = 0; i < townCount; i++)
  {
   vector candidate = EAC_SettlementIndex.GetCentreAt(i);
   float distance = vector.Distance(home, candidate);
   if (distance < 500 || distance > 5000 || !EAC_ExclusionZone.IsJourneyPointAllowed(candidate)) continue;
   float rank = distance;
   if (vector.DistanceXZ(centre, candidate) < preferred) rank += 100000;
   int slot = m_GoalRank.Count();
   while (slot > 0 && m_GoalRank[slot - 1] > rank) slot--;
   if (slot >= 3) continue;
   if (slot == m_GoalRank.Count()) { m_GoalScratch.Insert(candidate); m_GoalRank.Insert(rank); }
   else { m_GoalScratch.InsertAt(candidate, slot); m_GoalRank.InsertAt(rank, slot); }
   if (m_GoalScratch.Count() > 3) { m_GoalScratch.RemoveOrdered(3); m_GoalRank.RemoveOrdered(3); }
  }
  int choices = m_GoalScratch.Count();
  if (choices == 0) return false;
  // Never rotate a preferred town together with a fallback one.
  int preferredCount;
  foreach (float ranked : m_GoalRank) if (ranked < 100000) preferredCount++;
  if (preferredCount > 0) choices = preferredCount;
  goal = m_GoalScratch[m_GoalCursor++ % choices];
  return true;
 }

 protected void Admit(EAC_AmbientModule module, array<IEntity> observers, float now)
 {
  if (now < m_NextAdmission || now < m_NextCandidate) return;
  // Failed placement must not spend the successful-car interval. Keep native
  // candidate work bounded to one route every two seconds on the shared timer.
  m_NextCandidate = now + 2;
  if (!module.CanAdmitNewWork()) { RecordAdmission("load"); return; }
  if (GetActiveCount() >= module.GetCarLimit()) { RecordAdmission("car_limit"); return; }
  if (GetPendingCount() != 0) { RecordAdmission("pending"); return; }
  if (m_Parties.Count() >= MAX_PARTIES) { RecordAdmission("record_limit"); return; }
  RecordAdmission("attempt");
  EAC_ThemeSelection theme = module.GetThemeSelection();
  if (!theme || !theme.IsReady() || theme.GetVehicles().GetCount() == 0 || theme.GetCharacters().GetCount() == 0) { RecordAdmission("theme"); return; }
  EAC_HouseholdRegistry homes = module.GetHomeIndex().GetRegistry();
  if (homes.GetHomeCount() < 1) { RecordAdmission("homes"); return; }
  EAC_SettlementIndex.Prepare();
  if (EAC_PedestrianSpawner.GetObserverReason(m_World, observers) != EAC_ESpawnReason.NONE) { RecordAdmission("observers"); return; }
  // Advance at most eight existing home records, never query the world for towns.
  EAC_HouseholdRecord home;
  for (int i = 0; i < 8; i++)
  {
   home = homes.GetHome(m_HomeCursor++ % homes.GetHomeCount());
   if (home && home.BuildingEntity && Relevant(home.Position, observers, module.TrafficDistance)) break;
   home = null;
  }
  if (!home) { RecordAdmission("home_range"); return; }
  vector goal;
  bool found = PickTown(module, home.Position, goal);
  // Terrains without server-side map labels still get a bounded road journey.
  // This fallback is not evidence that the road endpoint belongs to another town.
  if (!found && !EAC_SettlementIndex.HasData())
  {
   int directionIndex = m_GoalCursor++ % 8;
   float angle = directionIndex * 45;
   goal = home.Position + vector.FromYaw(angle) * Math.Clamp(module.TrafficDistance * 1.5, 700, 2000);
   found = true;
  }
  if (!found) { RecordAdmission("town_goal"); return; }
  EAC_TrafficParty party = new EAC_TrafficParty();
  string routeReason;
  bool kerbside = module.TrafficRoundTrips == 0;
  if (!EAC_TrafficRoute.Find(m_World, home.Position, goal, party.Position, party.Destination, party.Direction, routeReason, m_LastRoute, kerbside)) { RecordAdmission(routeReason); return; }
  party.HomePosition = home.Position;
  float admitDistance = 60;
  if (kerbside) admitDistance = CAR_SPAWN_DISTANCE;
  if (!Hidden(party, party.Position, observers, admitDistance)) { RecordAdmission("visible"); return; }
  if (!EAC_TrafficRoute.Clear(m_World, party.Position)) { RecordAdmission("clearance"); return; }
  party.ReturnDestination = party.Position; party.CarPrefab = theme.GetVehicles().Pick();
  if (!EAC_TrafficRoute.SupportedCar(party.CarPrefab)) { RecordAdmission("car_profile"); return; }
  for (int i = 0; i <= module.TrafficPassengers; i++)
  {
   EAC_TrafficOccupant row = new EAC_TrafficOccupant(); row.Prefab = theme.GetCharacters().Pick(); party.Crew.Insert(row);
  }
  if (m_NextId >= int.MAX - 1) { RecordAdmission("id_limit"); return; }
  party.Id = m_NextId++;
  if (!module.GetSpawner().HasLocalCapacity(module, party.Position, party.Crew.Count())) { RecordAdmission("local_budget"); return; }
  if (!module.TryReservePopulation(party.Id, party.Crew.Count())) { RecordAdmission("global_budget"); return; }
  party.Reserved = true; party.Since = now; m_Parties.Insert(party);
  m_NextAdmission = now + module.TrafficInterval;
  RecordAdmission("admitted");
 }

 void Step(EAC_AmbientModule module, array<IEntity> observers, float now)
 {
  if (Get() != this || !module || module != EAC_AmbientModule.GetActive() || module.GetWorld() != m_World) return;
  m_GapStopped = false;
  // Kept for a controller gap: DrainParties applies the same removal distance.
  m_CleanupDistance = module.TrafficSleepDistance;
  foreach (EAC_TrafficParty party : m_Parties) Monitor(module, party, observers, now);
  int count = m_Parties.Count();
  for (int i = 0; i < count && !m_Parties.IsEmpty(); i++)
  {
   m_Cursor = m_Cursor % m_Parties.Count();
   EAC_TrafficParty party = m_Parties[m_Cursor++];
   if (Advance(module, party, observers, now)) return;
  }
  Admit(module, observers, now);
 }

 void EAC_RetireSessionPopulation()
 {
  foreach (EAC_TrafficParty party : m_Parties) party.SessionRemoval = true;
 }

 bool EAC_ContainsSessionEntity(SCR_EditableEntityComponent candidate)
 {
  foreach (EAC_TrafficParty party : m_Parties)
  {
   if (party.EAC_SessionTransferred()) continue;
   if (EAC_SessionLifecycle.ContainsOwned(candidate, party.Car) || EAC_SessionLifecycle.ContainsOwned(candidate, party.Order)) return true;
   if (EAC_SessionLifecycle.ContainsOwned(candidate, party.Group) && EAC_SessionGroupExclusive(candidate, party)) return true;
   foreach (EAC_TrafficOccupant row : party.Crew)
    if (EAC_SessionLifecycle.ContainsOwned(candidate, row.Actor)) return true;
  }
  return false;
 }

 // The entities EAC_ContainsSessionEntity answers for, for EAC_SessionLifecycle.Sync.
 void EAC_KeepSessionEntities()
 {
  foreach (EAC_TrafficParty party : m_Parties)
  {
   if (party.EAC_SessionTransferred()) continue;
   EAC_SessionLifecycle.Keep(party.Car);
   EAC_SessionLifecycle.Keep(party.Order);
   foreach (EAC_TrafficOccupant row : party.Crew) EAC_SessionLifecycle.Keep(row.Actor);
   if (!party.Group) continue;
   SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.GetEditableEntity(party.Group);
   if (editable && EAC_SessionGroupExclusive(editable, party)) EAC_SessionLifecycle.Keep(party.Group);
  }
 }

 protected bool EAC_SessionGroupExclusive(SCR_EditableEntityComponent editable, EAC_TrafficParty party)
 {
  if (party.Group.GetAgentsCount() > party.Crew.Count() || party.Group.GetPlayerCount() > 0 || party.Group.GetChildren()) return false;
  for (int i = 0; i < editable.GetChildrenCount(true); i++)
  {
   SCR_EditableEntityComponent child = editable.GetChild(i);
   if (child && !party.Owns(child.GetOwner()) && child.GetOwner() != party.Order) return false;
  }
  return true;
 }
}
