enum EAC_EAnchorKind
{
 YARD_OPEN,
 WALL_BACK,
 CORNER,
 LOW_EDGE,
 OVERHEAD,
 DOORSTEP,
 // Kinds a pre-survey can identify from an object's prefab family rather than
 // by guessing with traces. Unused until the scene index produces them.
 SEAT,
 PROP_FACE,
 // An actual chair, bench or sofa, identified by prefab family. Distinct from
 // SEAT, which means "stand beside a bench". Survey only: no pose sits on one
 // yet, the kind exists so real seats can be counted and measured.
 SEAT_REAL
}

// Believable stationary places are found in existing geometry, never created.
// One bounded candidate per call: a fixed probe sweep, short traces and the
// existing navmesh/exclusion/road gates. No world scan and no entity queries.
class EAC_RoutineAnchors
{
 static const int PROBES = 8;
 static const int KINDS = 9;
 protected static const float CHEST = 1.2;
 // Audit B2. One anchor probe runs up to fifteen of these traces and a routine
 // start sweeps several probes, so a fresh TraceParam per trace was the dominant
 // allocation of the whole routine path. Both are overwritten completely before
 // every use (Start, End, Flags, Exclude) and read back only immediately after,
 // so a shared instance carries nothing between calls. Lazily created, and safe
 // across worlds: a TraceParam holds no world state and Exclude is reassigned on
 // entry every time.
 protected static ref TraceParam s_HitTrace;
 protected static ref TraceParam s_AboveTrace;
 // Probe budget, shared by every caller, spent per SCHEDULER TICK.
 //
 // This replaces a per-CALL budget, which was the wrong shape: audit B2's concern
 // is the aggregate trace cost of one tick (~250 traces when sixteen probes ran
 // inside a single BeginLeg), not how much any one resident may do. Capping each
 // call instead made a routine start take up to six retries - twenty seconds
 // apart in production - and that latency is what pushed the Activities fixture's
 // phase 12 past its forty-second window. A shared per-tick budget bounds exactly
 // the thing that needed bounding: a lone resident finishes its sweep in one or
 // two ticks, and a full town is capped harder than the per-call rule ever was.
 static const int TICK_BUDGET = 12;
 protected static BaseWorld s_BudgetWorld;
 protected static float s_BudgetTick;
 protected static int s_BudgetSpent;

 // `now` is the scheduler's shared tick clock, so every resident visited in one
 // tick draws from the same window.
 static bool TakeProbe(BaseWorld world, float now)
 {
  if (s_BudgetWorld != world) { s_BudgetWorld = world; s_BudgetTick = now; s_BudgetSpent = 0; }
  if (now > s_BudgetTick) { s_BudgetTick = now; s_BudgetSpent = 0; }
  if (s_BudgetSpent >= TICK_BUDGET) return false;
  s_BudgetSpent++;
  return true;
 }

 static string ShortName(int kind)
 {
  if (kind == EAC_EAnchorKind.WALL_BACK) return "wall";
  if (kind == EAC_EAnchorKind.CORNER) return "corner";
  if (kind == EAC_EAnchorKind.LOW_EDGE) return "edge";
  if (kind == EAC_EAnchorKind.OVERHEAD) return "cover";
  if (kind == EAC_EAnchorKind.DOORSTEP) return "door";
  if (kind == EAC_EAnchorKind.SEAT) return "seat";
  if (kind == EAC_EAnchorKind.PROP_FACE) return "prop";
  if (kind == EAC_EAnchorKind.SEAT_REAL) return "seat_real";
  return "open";
 }

 static string Name(EAC_EAnchorKind kind)
 {
  if (kind == EAC_EAnchorKind.WALL_BACK) return "against a wall";
  if (kind == EAC_EAnchorKind.CORNER) return "in a corner";
  if (kind == EAC_EAnchorKind.LOW_EDGE) return "at a low edge";
  if (kind == EAC_EAnchorKind.OVERHEAD) return "under cover";
  if (kind == EAC_EAnchorKind.DOORSTEP) return "on the doorstep";
  if (kind == EAC_EAnchorKind.SEAT) return "beside a bench";
  if (kind == EAC_EAnchorKind.PROP_FACE) return "at the well";
  if (kind == EAC_EAnchorKind.SEAT_REAL) return "at a real seat";
  return "in the open";
 }

 // Distance band from the home centre. Wall, corner and doorstep candidates must
 // sit close enough that the house itself is the geometry they use; open and
 // covered places belong further out, where the resident visibly relocates.
 protected static void Band(EAC_EAnchorKind kind, IEntity home, out float minimum, out float maximum)
 {
  vector mins, maxs; home.GetBounds(mins, maxs);
  float reach = Math.Max(maxs[0] - mins[0], maxs[2] - mins[2]) * 0.5;
  reach = Math.Clamp(reach, 3, 20);
  minimum = reach + 5; maximum = reach + 22;
  if (kind == EAC_EAnchorKind.WALL_BACK) { minimum = reach + 0.6; maximum = reach + 1.4; }
  else if (kind == EAC_EAnchorKind.CORNER) { minimum = reach + 0.6; maximum = reach + 1.6; }
  else if (kind == EAC_EAnchorKind.DOORSTEP) { minimum = reach + 1.0; maximum = reach + 3.0; }
  else if (kind == EAC_EAnchorKind.LOW_EDGE) { minimum = reach + 4; maximum = reach + 16; }
  else if (kind == EAC_EAnchorKind.OVERHEAD) { minimum = reach + 4; maximum = reach + 20; }
 }

 // Wall, corner and doorstep candidates must follow the building's actual
 // footprint. A circle around the origin runs inside a rectangular house along
 // its long sides and sits far off it at the corners, which is why a circular
 // sweep produced zero wall anchors in the first live campaign (wall=0/72).
 // Faces and corners are taken from the model's local bounds and pushed out
 // along the home's own basis vectors.
 static bool FaceCandidate(IEntity home, int probe, float outward, out vector candidate, out vector normal)
 {
  vector mins, maxs, transform[4];
  home.GetBounds(mins, maxs); home.GetWorldTransform(transform);
  int face = probe % 4;
  float along = 0.34;
  if (probe >= 4) along = 0.66;
  vector local = mins;
  local[1] = mins[1];
  if (face == 0) { local[0] = mins[0] + (maxs[0] - mins[0]) * along; local[2] = maxs[2] + outward; normal = transform[2]; }
  else if (face == 1) { local[0] = maxs[0] + outward; local[2] = mins[2] + (maxs[2] - mins[2]) * along; normal = transform[0]; }
  else if (face == 2) { local[0] = mins[0] + (maxs[0] - mins[0]) * along; local[2] = mins[2] - outward; normal = -transform[2]; }
  else { local[0] = mins[0] - outward; local[2] = mins[2] + (maxs[2] - mins[2]) * along; normal = -transform[0]; }
  candidate = local.Multiply4(transform);
  normal[1] = 0;
  if (normal.LengthSq() < 0.0001) return false;
  normal = normal.Normalized();
  return true;
 }

 static bool CornerCandidate(IEntity home, int probe, float outward, out vector candidate, out vector normal)
 {
  vector mins, maxs, transform[4];
  home.GetBounds(mins, maxs); home.GetWorldTransform(transform);
  int corner = probe % 4;
  float signX = 1; float signZ = 1;
  if (corner == 1) { signX = 1; signZ = -1; }
  else if (corner == 2) { signX = -1; signZ = 1; }
  else if (corner == 3) { signX = -1; signZ = -1; }
  vector local = mins;
  local[1] = mins[1];
  if (signX > 0) local[0] = maxs[0] + outward; else local[0] = mins[0] - outward;
  if (signZ > 0) local[2] = maxs[2] + outward; else local[2] = mins[2] - outward;
  candidate = local.Multiply4(transform);
  normal = transform[0] * signX + transform[2] * signZ;
  normal[1] = 0;
  if (normal.LengthSq() < 0.0001) return false;
  normal = normal.Normalized();
  return true;
 }

 // A doorstep must be an actual opening. Prefer a discovered door pivot and
 // step outward from it; fall back to the footprint face only while this home's
 // doors are still unknown.
 protected static bool DoorstepCandidate(BaseWorld world, IEntity home, int probe, out vector candidate, out vector normal)
 {
  int step = probe % 2;
  float push = 1.6 + step * 0.8;
  array<vector> doors = EAC_DoorIndex.Get(home, world.GetWorldTime() * 0.001);
  if (doors && !doors.IsEmpty())
  {
   int index = probe % doors.Count();
   vector pivot = doors[index];
   vector away = pivot - home.GetOrigin(); away[1] = 0;
   if (away.LengthSq() > 0.0001)
   {
    normal = away.Normalized();
    candidate = pivot + normal * push;
    return true;
   }
  }
  return FaceCandidate(home, probe, push, candidate, normal);
 }

 static float HitDistance(BaseWorld world, vector from, vector direction, float length, IEntity exclude, out IEntity blocker)
 {
  blocker = null;
  if (!s_HitTrace) s_HitTrace = new TraceParam();
  TraceParam trace = s_HitTrace;
  trace.Start = from; trace.End = from + direction * length;
  trace.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
  trace.Exclude = exclude;
  float fraction = world.TraceMove(trace, null);
  if (fraction >= 1) return length + 1;
  blocker = trace.TraceEnt;
  return fraction * length;
 }

 // MEASURED, not guessed (runs lean-measure-01/02, 2026-09-19, seven samples): the
 // vanilla lean animation turns the character a quarter turn and carries it 0.36 m
 // FORWARD along the direction it was facing when the pose began (0.04-0.08 m
 // sideways), and the leaning shoulder ends up pointing that same way. So the pose
 // has to START facing the wall; the animation then brings the shoulder onto it.
 // The code this replaces started with the wall behind the resident, so the
 // animation carried it 0.36 m AWAY from the wall - "leaning on air" - and the first
 // fix, which started along the wall, made the animation turn it to face the wall.
 static const float LEAN_ANIMATION_ADVANCE = 0.36;
 // Feet this far from the wall once the pose has settled: a shoulder's width.
 static const float LEAN_SETTLED_GAP = 0.30;
 // A lean is only believable with the shoulder on a wall. This finds the real
 // surface beside the anchor, checks that it is a flat upright face wide enough to
 // lean on, and rewrites the transform so the pose starts facing that wall from
 // LEAN_ANIMATION_ADVANCE + LEAN_SETTLED_GAP away. False means there is nothing to
 // lean on here; the caller then plays a plain standing idle instead. At most
 // fourteen short traces, once per lean stop. `leftShoulder` is unused for placement
 // - both leans start facing the wall and differ only in the way they turn - and is
 // kept so a future measured asymmetry has somewhere to go.
 // `transform` is where the pose is played; `approach` is the same spot, which at
 // 0.66 m from the wall lies outside the margin the navmesh keeps from it.
 static bool LeanAgainst(BaseWorld world, IEntity actor, bool leftShoulder, inout vector transform[4], out vector approach)
 {
  if (!world) return false;
  vector feet = transform[3];
  vector chest = feet + Vector(0, CHEST, 0);
  float best = 10; vector bestDirection;
  IEntity blocker;
  for (int i = 0; i < 8; i++)
  {
   float angle = i * Math.PI / 4;
   vector direction = Vector(Math.Cos(angle), 0, Math.Sin(angle));
   float hit = HitDistance(world, chest, direction, 1.6, actor, blocker);
   if (hit > 1.6 || !blocker || ChimeraCharacter.Cast(blocker) || Vehicle.Cast(blocker)) continue;
   if (hit < best) { best = hit; bestDirection = direction; }
  }
  if (best > 1.6) return false;
  // The eight bearings only find the wall; its own normal says which way it faces.
  HitDistance(world, chest, bestDirection, 1.6, actor, blocker);
  vector normal = s_HitTrace.TraceNorm; normal[1] = 0;
  if (normal.LengthSq() < 0.25) return false;
  normal.Normalize();
  vector toWall = -normal;
  float distance = HitDistance(world, chest, toWall, 1.8, actor, blocker);
  if (distance > 1.8 || !blocker || ChimeraCharacter.Cast(blocker) || Vehicle.Cast(blocker)) return false;
  // Upright: hip and head height meet the same face.
  float hip = HitDistance(world, feet + Vector(0, 0.8, 0), toWall, 1.8, actor, blocker);
  float head = HitDistance(world, feet + Vector(0, 1.6, 0), toWall, 1.8, actor, blocker);
  if (Math.AbsFloat(hip - distance) > 0.2 || Math.AbsFloat(head - distance) > 0.2) return false;
  // Wide enough: a pole, a trunk or a wall end does not carry a shoulder.
  vector along = Vector(-normal[2], 0, normal[0]);
  float sideA = HitDistance(world, chest + along * 0.4, toWall, 1.8, actor, blocker);
  float sideB = HitDistance(world, chest - along * 0.4, toWall, 1.8, actor, blocker);
  if (Math.AbsFloat(sideA - distance) > 0.2 || Math.AbsFloat(sideB - distance) > 0.2) return false;
  vector position = feet + toWall * (distance - (LEAN_ANIMATION_ADVANCE + LEAN_SETTLED_GAP));
  position[1] = feet[1];
  vector forward = toWall;
  Math3D.DirectionAndUpMatrix(forward, "0 1 0", transform);
  transform[3] = position;
  approach = position;
  return true;
 }

 // Clearance for a position that came OUT OF THE NAVMESH, which is walkable by
 // construction.
 //
 // EAC_PedestrianSpawner.GetClearReason opens with a 0.75 m comparison against
 // world.GetSurfaceY, and GetSurfaceY is the TERRAIN heightmap - it ignores
 // meshes. Applied to a navmesh projection that rejects every position standing
 // on a road surface, a kerb, a slab, a porch, a doorstep or a bridge deck. That
 // one line is the single biggest refusal in the system: campaign 121 counted
 // walk dest=28 + clear=31 out of 97 attempts (61 %), and roughly 78 % of all
 // anchor probes (21 accepts from 95) died on it. Campaign 118's own sweep line
 // isolates it - nav=8 end_clear=5 with interior_nav=0 and body=5 - three of
 // eight bearings had a valid navmesh projection and were still refused, with no
 // body-box or interior-navmesh failure behind it.
 //
 // The rule is widened, not dropped: 2.5 m still rejects an upper storey (the
 // reason the height test exists at all) while accepting camber, kerbs, porches
 // and slabs. Water and the body box are byte-for-byte the spawner's.
 //
 // The tolerance, the water test and the body box now live in
 // EAC_PedestrianSpawner.GetClearReason behind its navmeshProjected flag, and it
 // records EAC_SchedulerStats.RecordSurfaceRecovered() for every position the old
 // rule would have refused. This is the one seat the anchor sweep and the walker
 // route through, so a future change to what "navmesh-derived clearance" means has
 // a single place to happen; the body itself is now a straight alias.
 static bool BodyClear(BaseWorld world, vector position, IEntity exclude)
 {
  return EAC_PedestrianSpawner.IsNavmeshClear(world, position, exclude);
 }

 static bool Clear(BaseWorld world, vector from, vector direction, float length, IEntity exclude)
 {
  IEntity blocker;
  return HitDistance(world, from, direction, length, exclude, blocker) > length;
 }

 static bool Above(BaseWorld world, vector position, IEntity exclude, out IEntity cover)
 {
  cover = null;
  if (!s_AboveTrace) s_AboveTrace = new TraceParam();
  TraceParam trace = s_AboveTrace;
  trace.Start = position + Vector(0, 1.8, 0); trace.End = position + Vector(0, 12, 0);
  trace.Flags = TraceFlags.WORLD | TraceFlags.ENTS; trace.Exclude = exclude;
  if (world.TraceMove(trace, null) >= 1) return false;
  cover = trace.TraceEnt;
  return true;
 }

 static void Face(vector direction, vector position, out vector transform[4])
 {
  vector flat = direction; flat[1] = 0;
  if (flat.LengthSq() < 0.0001) flat = "0 0 1";
  Math3D.DirectionAndUpMatrix(flat.Normalized(), "0 1 0", transform);
  transform[3] = position;
 }

 // Shared gates every anchor must pass before its own geometry is examined.
 static bool Usable(BaseWorld world, IEntity actor, AIPathfindingComponent path, vector candidate, out vector settled, out string reason)
 {
  reason = "water or surface";
  if (ChimeraWorldUtils.TryGetWaterSurfaceSimple(world, candidate - "0 0.1 0")) return false;
  reason = "no navmesh component";
  NavmeshWorldComponent mesh = path.GetNavmeshComponent();
  if (!mesh) return false;
  reason = "tile pending";
  if (!mesh.IsTileLoaded(candidate)) { if (!mesh.IsTileRequested(candidate)) mesh.LoadTileIn(candidate); return false; }
  reason = "invalid tile";
  if (!mesh.IsTileValid(candidate)) return false;
  reason = "no navmesh position";
  if (!path.GetClosestPositionOnNavmesh(candidate, "1 2 1", settled) || vector.Distance(candidate, settled) > 1.5) return false;
  reason = "projected tile unavailable";
  if (!mesh.IsTileLoaded(settled) || !mesh.IsTileValid(settled)) return false;
  reason = "exclusion";
  if (!EAC_ExclusionZone.IsPopulationAllowed(settled)) return false;
  reason = "road lane";
  if (!EAC_ActivityStation.OffRoad(settled)) return false;
  reason = "body clearance";
  // `settled` is the navmesh projection, so the terrain-height rule does not
  // apply to it; see BodyClear.
  if (!BodyClear(world, settled, actor)) return false;
  return true;
 }

 protected static bool WallBack(BaseWorld world, IEntity actor, IEntity home, vector settled, vector outward, out vector transform[4], out string reason)
 {
  vector chest = settled + Vector(0, CHEST, 0);
  IEntity blocker;
  reason = "no wall behind";
  float wall = HitDistance(world, chest, -outward, 1.8, actor, blocker);
  if (wall < 0.2 || wall > 1.4 || blocker != home) return false;
  reason = "obstructed facing";
  if (!Clear(world, chest, outward, 2.0, actor)) return false;
  Face(outward, settled, transform);
  return true;
 }

 // The candidate is placed at a bounding-box corner by construction, so the
 // predicate only has to confirm the building is really there. Two earlier
 // attempts could not: axis-aligned traces from a point offset diagonally past
 // the corner run ALONGSIDE the footprint and never enter it ("no wall on the
 // first face", campaign 23), and the original short diagonal fell short because
 // model bounds include roof overhang, leaving the real wall corner a couple of
 // metres inside the bounding corner. Trace the diagonal toward the building
 // centre, far enough to cross that overhang.
 protected static bool Corner(BaseWorld world, IEntity actor, IEntity home, vector settled, vector outward, out vector transform[4], out string reason)
 {
  vector chest = settled + Vector(0, CHEST, 0);
  vector inward = home.GetOrigin() - settled; inward[1] = 0;
  reason = "degenerate home position";
  if (inward.LengthSq() < 0.0001) return false;
  inward = inward.Normalized();
  IEntity blocker;
  reason = "no corner wall";
  if (HitDistance(world, chest, inward, 4.0, actor, blocker) > 3.5 || blocker != home) return false;
  reason = "obstructed facing";
  if (!Clear(world, chest, outward, 1.8, actor)) return false;
  Face(outward, settled, transform);
  return true;
 }

 protected static bool LowEdge(BaseWorld world, IEntity actor, IEntity home, vector settled, float angle, out vector transform[4], out string reason)
 {
  reason = "no low edge";
  vector low = settled + Vector(0, 0.5, 0);
  vector head = settled + Vector(0, 1.6, 0);
  IEntity blocker;
  for (int side = 0; side < 8; side++)
  {
   vector direction = Vector(Math.Cos(angle + side * Math.PI / 4), 0, Math.Sin(angle + side * Math.PI / 4));
   if (HitDistance(world, low, direction, 1.5, actor, blocker) > 1.2 || blocker == home) continue;
   // A wall is not a low edge: head height must stay open over the obstacle.
   if (!Clear(world, head, direction, 1.4, actor)) continue;
   Face(direction, settled, transform);
   return true;
  }
  return false;
 }

 protected static bool Overhead(BaseWorld world, IEntity actor, IEntity home, vector settled, vector outward, out vector transform[4], out string reason)
 {
  IEntity cover;
  // One vertical line under a canopy is a near miss most of the time (1 accept
  // in 48 attempts). Probe a small cross and stand wherever is actually covered.
  vector chosen = settled;
  bool covered = Above(world, settled, actor, cover) && cover && cover != home;
  for (int probe = 0; !covered && probe < 4; probe++)
  {
   vector offset = Vector(Math.Cos(probe * Math.PI / 2) * 1.5, 0, Math.Sin(probe * Math.PI / 2) * 1.5);
   vector sample = settled + offset;
   sample[1] = world.GetSurfaceY(sample[0], sample[2]) + 0.1;
   if (!EAC_PedestrianSpawner.IsClear(world, sample, actor)) continue;
   if (!Above(world, sample, actor, cover) || !cover || cover == home) continue;
   chosen = sample; covered = true;
  }
  reason = "no cover above";
  if (!covered) return false;
  reason = "obstructed facing";
  if (!Clear(world, chosen + Vector(0, CHEST, 0), -outward, 2.0, actor)) return false;
  Face(-outward, chosen, transform);
  return true;
 }

 protected static bool Doorstep(BaseWorld world, IEntity actor, IEntity home, vector settled, vector outward, out vector transform[4], out string reason)
 {
  vector chest = settled + Vector(0, CHEST, 0);
  IEntity blocker;
  reason = "not beside the home";
  if (HitDistance(world, chest, -outward, 3.5, actor, blocker) > 3.2 || blocker != home) return false;
  IEntity cover;
  reason = "inside the home roof volume";
  if (Above(world, settled, actor, cover) && cover == home) return false;
  reason = "obstructed facing";
  if (!Clear(world, chest, outward, 2.0, actor)) return false;
  Face(outward, settled, transform);
  return true;
 }

 protected static bool YardOpen(BaseWorld world, IEntity actor, vector settled, vector outward, float angle, out vector transform[4], out string reason)
 {
  reason = "not open ground";
  vector chest = settled + Vector(0, CHEST, 0);
  for (int quarter = 0; quarter < 4; quarter++)
  {
   vector direction = Vector(Math.Cos(angle + quarter * Math.PI / 2), 0, Math.Sin(angle + quarter * Math.PI / 2));
   if (!Clear(world, chest, direction, 2.0, actor)) return false;
  }
  Face(-outward, settled, transform);
  return true;
 }

 // Returns one verified anchor transform for this probe index, or false with a
 // recorded reason. One call is up to fifteen short traces plus, through Usable,
 // a navmesh projection and a road query.
 //
 // Audit B2. This comment used to claim callers already swept 0..PROBES-1 across
 // separate ticks; they did not - EAC_CivilianActivity.OutdoorAnchor ran the whole
 // two-pass sweep inside one BeginLeg. It now genuinely does spend a few probes
 // per tick against a cursor retained on the resident record, and
 // EAC_RoutineEmerge has always advanced its own m_Probe one per tick.
 static bool Find(EAC_EAnchorKind kind, BaseWorld world, IEntity actor, IEntity home, AIPathfindingComponent path, int probe, out vector transform[4], out string reason)
 {
  Math3D.MatrixIdentity4(transform);
  reason = "missing world, actor, home or path";
  if (!world || !actor || !home || !path || probe < 0 || probe >= PROBES) return false;
  float angle = probe * Math.PI2 / PROBES;
  vector candidate, outward;
  if (kind == EAC_EAnchorKind.DOORSTEP)
  {
   reason = "no doorway candidate";
   if (!DoorstepCandidate(world, home, probe, candidate, outward)) { EAC_RoutineStats.RecordAnchor(kind, false); return false; }
  }
  else if (kind == EAC_EAnchorKind.WALL_BACK)
  {
   reason = "degenerate home transform";
   if (!FaceCandidate(home, probe, 0.7, candidate, outward)) { EAC_RoutineStats.RecordAnchor(kind, false); return false; }
  }
  else if (kind == EAC_EAnchorKind.CORNER)
  {
   reason = "degenerate home transform";
   if (!CornerCandidate(home, probe, 0.8, candidate, outward)) { EAC_RoutineStats.RecordAnchor(kind, false); return false; }
  }
  else
  {
   float minimum, maximum;
   Band(kind, home, minimum, maximum);
   int band = probe % 3;
   float radius = minimum + (maximum - minimum) * band * 0.5;
   outward = Vector(Math.Cos(angle), 0, Math.Sin(angle));
   candidate = home.GetOrigin() + outward * radius;
  }
  candidate[1] = world.GetSurfaceY(candidate[0], candidate[2]) + 0.1;
  vector settled;
  if (!Usable(world, actor, path, candidate, settled, reason))
  {
   EAC_RoutineStats.RecordAnchor(kind, false);
   EAC_RoutineStats.TraceRejection(kind, reason, world.GetWorldTime() * 0.001);
   return false;
  }
  bool accepted;
  if (kind == EAC_EAnchorKind.WALL_BACK) accepted = WallBack(world, actor, home, settled, outward, transform, reason);
  else if (kind == EAC_EAnchorKind.CORNER) accepted = Corner(world, actor, home, settled, outward, transform, reason);
  else if (kind == EAC_EAnchorKind.LOW_EDGE) accepted = LowEdge(world, actor, home, settled, angle, transform, reason);
  else if (kind == EAC_EAnchorKind.OVERHEAD) accepted = Overhead(world, actor, home, settled, outward, transform, reason);
  else if (kind == EAC_EAnchorKind.DOORSTEP) accepted = Doorstep(world, actor, home, settled, outward, transform, reason);
  else accepted = YardOpen(world, actor, settled, outward, angle, transform, reason);
  EAC_RoutineStats.RecordAnchor(kind, accepted);
  if (!accepted) EAC_RoutineStats.TraceRejection(kind, reason, world.GetWorldTime() * 0.001);
  return accepted;
 }
}
