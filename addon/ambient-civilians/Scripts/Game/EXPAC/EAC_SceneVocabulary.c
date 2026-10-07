// What an object IS, decided from its prefab family rather than guessed with
// traces. A bench is a seat because its prefab is benchwooden_01, not because a
// trace lottery happened to land near it. Measured across campaigns 12-24, the
// trace lottery accepted 0% of corner candidates, 2.5% of overhead and 9% of low
// edge; identity costs one memoised string split per distinct prefab per mission.
//
// Pure classification. No world calls, no entity handles, no traces.
class EAC_ScenePropSample
{
 IEntity Furniture;
 vector Transform[4];
 vector Mins, Maxs;
 int Kind, Mask, Spots;
}

class EAC_SceneVocabulary
{
 static const int MAX_MEMO = 512;
 protected static ref map<string, int> s_Memo;
 protected static BaseWorld s_World;

 static void CheckWorld()
 {
		EXPBG_LazyStatics_EAC_SceneVocabulary();
  BaseWorld world = GetGame().GetWorld();
  if (world == s_World) return;
  s_Memo.Clear(); s_World = world;
 }

 // Packed as kind+1 in bits 0..7 so that zero unambiguously means "unclassified"
 // and does not collide with YARD_OPEN being enum zero.
 static int KindOf(int packed)
 {
  int stored = packed & 255;
  if (stored == 0) return -1;
  return stored - 1;
 }
 static int MaskOf(int packed) { return (packed / 256) & 255; }
 static int SpotsOf(int packed) { return (packed / 65536) & 255; }

 protected static int Pack(int kind, int mask, int spots)
 {
  return (kind + 1) + mask * 256 + spots * 65536;
 }

 // Which animations a position of this kind may host. Bits over EAC_ERoutineAnim,
 // not ELoiteringType, because catalog slots carry EAC_ERoutineAnim.
 // SEATED is never set here: that belongs to EAC_ActivityStation's indoor tables.
 static int AnimMaskForKind(int kind, bool fromProp)
 {
  // A runtime-detected slot is a standing occupation, so it is offered exactly
  // where a plain idle is offered and nowhere else. No vanilla routine ever asks
  // for this bit, so setting it cannot change any vanilla spot decision, and on
  // a server with no detected animation no routine asks for it either.
  int idle = (1 << EAC_ERoutineAnim.IDLE) | (1 << EAC_ERoutineAnim.CUSTOM_ANIM);
  int smoke = 1 << EAC_ERoutineAnim.SMOKE;
  int sit = 1 << EAC_ERoutineAnim.SIT_GROUND;
  int leanLeft = 1 << EAC_ERoutineAnim.LEAN_LEFT;
  int leanRight = 1 << EAC_ERoutineAnim.LEAN_RIGHT;
  if (kind == EAC_EAnchorKind.WALL_BACK || kind == EAC_EAnchorKind.CORNER) return idle | smoke | leanLeft | leanRight;
  if (kind == EAC_EAnchorKind.DOORSTEP) return idle | smoke | sit | leanRight;
  if (kind == EAC_EAnchorKind.OVERHEAD) return idle | smoke | sit;
  // Native SIT plays at a transform and snaps to no mesh, so a position beside
  // real furniture must not offer it until a client campaign proves it reads
  // correctly. Ground and derived edges are unaffected; they are actual ground.
  // SEAT_REAL is a measurement kind for now: it identifies an actual chair or
  // bench so its bounds can be dumped, and until a campaign measures seat height
  // it offers exactly what standing beside a bench offers. No pose uses it.
  if (kind == EAC_EAnchorKind.SEAT || kind == EAC_EAnchorKind.SEAT_REAL || kind == EAC_EAnchorKind.PROP_FACE) return idle | smoke;
  if (kind == EAC_EAnchorKind.LOW_EDGE)
  {
   if (fromProp) return idle | smoke;
   return idle | smoke | sit;
  }
  return idle | smoke | sit;
 }

 // Family tables, matched against the prefab's parent directory AND its file
 // stem. Vanilla furniture is mostly flat files under one category folder, so the
 // original directory-only match recognised almost nothing: the parent of
 // Prefabs/Props/Furniture/BenchWooden_01.et is "furniture", and only the few
 // props that own a folder each (StreetBench_01, ForestBench_01) ever matched.
 // Every entry carries its index suffix, so no entry is a bare word: "chair_01"
 // cannot swallow chairdental_01 and "benchwooden_01" cannot swallow benchdrill_01.
 protected static ref array<string> s_SeatRealOutdoor;
 protected static ref array<string> s_SeatRealIndoor;
 // Kept as authored, and deliberately shadowed: every family here is also a real
 // seat, so a real bench now classifies as SEAT_REAL and routines that want SEAT
 // reach it by substitution. The list stays so the SEAT kind keeps its authored
 // meaning - "stand beside a bench" - for anything removed from the tables above.
 protected static ref array<string> s_Seats;
 protected static ref array<string> s_Overheads;
 protected static ref array<string> s_PropFaces;
 protected static ref array<string> s_LowEdges;

 // Built on first use rather than as static field initializers: the engine compiles
 // every static initializer of every loaded addon into one shared function, and a
 // large mod list overflows it ("Too many instructions per function", 2026-09-18).
 // Resolve is the only reader, and the tables are constant, so one build per process
 // needs no per-world reset.
 protected static void EnsureFamilies()
 {
  if (s_LowEdges) return;
  array<string> seatrealoutdoor = {"benchwooden_01", "benchwooden_02", "benchwooden_03", "benchstadium_01", "benchstreet_01", "benchstreet_02", "streetbench_01", "forestbench_01", "picnictable_01", "benchchurch_02", "benchchurch_small_01", "deckchair_01"};
  array<string> seatrealindoor = {"chair_01", "chair_02", "chair_03", "chairold_01", "chairold_02", "chairoffice_01", "chairschool_01", "chairdecorative_01", "chairrecreation_01", "armchair_01", "armchair_02", "armchair_03", "sofa_01", "sofa_02", "stool_01", "stool_02", "stoolpiano_01", "benchkitchen_01", "benchoffice_01"};
  array<string> seats = {"benchwooden_01", "benchwooden_03", "benchstadium_01", "benchstreet_01", "benchstreet_02", "streetbench_01", "forestbench_01", "picnictable_01"};
  array<string> overheads = {"busshed_e_01", "sunshade_01", "sunshadeclosed_01"};
  array<string> propfaces = {"well_01", "well_02", "wellpump_01", "fountain_01", "hydrantstreet_01", "messageboard_01", "messageboard_02", "messageboardsmall_01", "boardmap_01", "postercolumn_01", "newspaperbooth_e_01", "marketstand_01"};
  array<string> lowedges = {"woodpile_01", "woodpile_02", "carpetframe_01", "clotheslineframe_01", "cart_01", "wheelbarrow_01", "logchopping_01"};
  s_SeatRealOutdoor = seatrealoutdoor;
  s_SeatRealIndoor = seatrealindoor;
  s_Seats = seats;
  s_Overheads = overheads;
  s_PropFaces = propfaces;
  s_LowEdges = lowedges;
 }

 // A token is the family exactly, or the family followed by a variant suffix:
 // chair_01_red is a chair_01, chair_011 and chairdental_01 are not.
 protected static bool TokenIs(string token, string family)
 {
  if (token == family) return true;
  int length = family.Length();
  if (token.Length() <= length) return false;
  if (token.IndexOf(family) != 0) return false;
  string tail = token.Substring(length, 1);
  if (tail == "_") return true;
  return false;
 }

 protected static bool InFamilies(array<string> families, string parent, string stem)
 {
  if (!families) return false;
  foreach (string family : families)
  {
   if (TokenIs(stem, family)) return true;
   if (TokenIs(parent, family)) return true;
  }
  return false;
 }

 // The family token a bounds dump prints. Only the rate-limited seat dump calls
 // this, so the extra split is bounded by that rate limit, not by entity count.
 static string FamilyOf(ResourceName resource)
 {
  string path = resource;
  path.ToLower();
  array<string> parts = {};
  path.Split("/", parts, true);
  if (parts.IsEmpty()) return "";
  return Stem(parts[parts.Count() - 1]);
 }

 protected static string Stem(string leaf)
 {
  int dot = leaf.IndexOf(".");
  if (dot > 0) return leaf.Substring(0, dot);
  return leaf;
 }

 // Memoised: the split and lowercase run once per distinct prefab per mission,
 // not once per entity per query. Unknown families return 0 and fall through to
 // trace classification, which is safe; they are never guessed at.
 static int Classify(ResourceName resource)
 {
		EXPBG_LazyStatics_EAC_SceneVocabulary();
  CheckWorld();
  string path = resource;
  if (path == "") return 0;
  int cached;
  if (s_Memo.Find(path, cached)) return cached;
  int packed = Resolve(path);
  if (s_Memo.Count() < MAX_MEMO) s_Memo.Insert(path, packed);
  return packed;
 }

 protected static int Resolve(string original)
 {
  string path = original;
  path.ToLower();
  EnsureFamilies();
  if (path.Contains("/dst/") || path.Contains("ruin") || path.Contains("_base.et")) return 0;
  // A composition is a layout of other prefabs; matching one would classify the
  // whole arrangement from whatever its folder happens to be called.
  if (path.Contains("/compositions/")) return 0;
  array<string> parts = {};
  path.Split("/", parts, true);
  int count = parts.Count();
  if (count < 2) return 0;
  string parent = parts[count - 2];
  string stem = Stem(parts[count - 1]);
  if (InFamilies(s_SeatRealOutdoor, parent, stem) || InFamilies(s_SeatRealIndoor, parent, stem))
  {
   vector seat, facing; float spacing;
   int slots = EAC_FurnitureSeat.Profile(original, seat, facing, spacing);
   if (slots > 0) return Pack(EAC_EAnchorKind.SEAT_REAL, 1 << EAC_ERoutineAnim.SEATED, slots);
   return Pack(EAC_EAnchorKind.SEAT_REAL, AnimMaskForKind(EAC_EAnchorKind.SEAT_REAL, true), 2);
  }
  if (InFamilies(s_Seats, parent, stem)) return Pack(EAC_EAnchorKind.SEAT, AnimMaskForKind(EAC_EAnchorKind.SEAT, true), 2);
  if (InFamilies(s_Overheads, parent, stem)) return Pack(EAC_EAnchorKind.OVERHEAD, AnimMaskForKind(EAC_EAnchorKind.OVERHEAD, true), 1);
  if (InFamilies(s_PropFaces, parent, stem)) return Pack(EAC_EAnchorKind.PROP_FACE, AnimMaskForKind(EAC_EAnchorKind.PROP_FACE, true), 2);
  if (InFamilies(s_LowEdges, parent, stem)) return Pack(EAC_EAnchorKind.LOW_EDGE, AnimMaskForKind(EAC_EAnchorKind.LOW_EDGE, true), 2);
  return 0;
 }

 // Deterministic arithmetic on the captured prop transform. No traces here; the
 // caller runs the shared acceptance ladder and at most two confirming traces.
 static bool SpotTransform(EAC_ScenePropSample prop, int index, BaseWorld world, vector homeCentre, out vector transform[4])
 {
  Math3D.MatrixIdentity4(transform);
  if (!prop || !world || index < 0 || index >= prop.Spots) return false;
  vector axisX = prop.Transform[0]; axisX[1] = 0;
  vector axisZ = prop.Transform[2]; axisZ[1] = 0;
  if (axisX.LengthSq() < 0.0001 || axisZ.LengthSq() < 0.0001) return false;
  axisX = axisX.Normalized(); axisZ = axisZ.Normalized();
  vector centre = prop.Transform[3];
  float halfDepth = (prop.Maxs[2] - prop.Mins[2]) * 0.5;
  float halfWidth = (prop.Maxs[0] - prop.Mins[0]) * 0.5;
  vector position = centre;
  vector facing = axisZ;
  if (prop.Kind == EAC_EAnchorKind.OVERHEAD)
  {
   // Stand under it, looking back toward the street rather than the building.
   position = centre;
   facing = centre - homeCentre; facing[1] = 0;
   if (facing.LengthSq() < 0.0001) facing = axisZ;
  }
  else if (prop.Kind == EAC_EAnchorKind.PROP_FACE)
  {
   float side = 1;
   if (index == 1) side = -1;
   position = centre + axisX * (halfWidth + 1.3) * side;
   facing = centre - position;
  }
  else
  {
   // Seats and low edges: one standing position per long side, facing the object.
   float side = 1;
   if (index == 1) side = -1;
   position = centre + axisZ * (halfDepth + 0.55) * side;
   facing = centre - position;
  }
  facing[1] = 0;
  if (facing.LengthSq() < 0.0001) return false;
  position[1] = world.GetSurfaceY(position[0], position[2]) + 0.1;
  EAC_RoutineAnchors.Face(facing.Normalized(), position, transform);
  return true;
 }

	//------------------------------------------------------------------------------------------------
	//! Creates the collections on first use (not in the global static initializer, which has a
	//! per-function instruction limit that large modsets exceed on Windows).
	protected static void EXPBG_LazyStatics_EAC_SceneVocabulary()
	{
		if (!s_Memo)
			s_Memo = new map<string, int>();
	}
}
