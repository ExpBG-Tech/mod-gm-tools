// Settlement centres taken from the terrain author's own map labels, resolved
// once per world and shared by every caller.
//
// The body of Prepare is EAC_TrafficDirector.PrepareTowns lifted verbatim: the
// same four descriptor types, the same >1024 guard, the same 100 m dedupe, the
// same 256 cap and the same four-attempt limit, so the traffic goal list keeps
// the behaviour 116 recorded dedicated-server runs measured (towns=34 on Everon).
// One thing is added: the descriptor type is retained per centre, which is what
// lets a caller treat a city differently from a hamlet without a second query.
//
// This class issues no world query of its own and holds no entity handle - only
// positions copied out of the map descriptors. It follows the world-scoped cache
// shape of EAC_DoorIndex.CheckWorld.
class EAC_SettlementIndex
{
 static const int MAX_CENTRES = 256;
 static const int MAX_ITEMS = 1024;
 static const int MAX_ATTEMPTS = 4;
 static const float DEDUPE_METRES = 100;

 protected static ref array<vector> s_Centres;
 protected static ref array<int> s_Types;
 protected static BaseWorld s_World;
 protected static int s_Attempts;

 // Static arrays must be reset behind a world guard or they survive a world
 // change holding another mission's geography.
 static void CheckWorld()
 {
		EXPBG_LazyStatics_EAC_SettlementIndex();
  // GetGame() is null while the game instance is being torn down, and this is
  // reached from static entry points that do not own a world (audit item 12).
  if (!GetGame()) return;
  BaseWorld world = GetGame().GetWorld();
  if (world == s_World) return;
  s_World = world;
  s_Centres.Clear();
  s_Types.Clear();
  s_Attempts = 0;
 }

 // Self-limiting: four attempts, then a single early return for the rest of the
 // mission. 802 recorded evidence lines show towns=0 on early ticks, because the
 // map instance is not always resolvable when the first tick runs, so a caller is
 // expected to call this every tick and pay nothing after it succeeds or gives up.
 static void Prepare()
 {
		EXPBG_LazyStatics_EAC_SettlementIndex();
  CheckWorld();
  if (!s_Centres.IsEmpty() || s_Attempts >= MAX_ATTEMPTS) return;
  s_Attempts++;
  SCR_MapEntity terrainMap = SCR_MapEntity.GetMapInstance();
  if (!terrainMap) return;
  array<int> types = {EMapDescriptorType.MDT_NAME_CITY, EMapDescriptorType.MDT_NAME_TOWN, EMapDescriptorType.MDT_NAME_VILLAGE, EMapDescriptorType.MDT_NAME_SETTLEMENT};
  foreach (int type : types)
  {
   array<MapItem> items = {}; terrainMap.GetByType(items, type);
   if (items.Count() > MAX_ITEMS) continue;
   foreach (MapItem item : items)
   {
    if (s_Centres.Count() >= MAX_CENTRES) return;
    if (!item || !item.Entity()) continue;
    vector position = item.Entity().GetOrigin();
    bool duplicate;
    foreach (vector existing : s_Centres) if (vector.Distance(existing, position) < DEDUPE_METRES) duplicate = true;
    if (duplicate) continue;
    s_Centres.Insert(position);
    s_Types.Insert(type);
   }
  }
 }

 static bool HasData() {
		EXPBG_LazyStatics_EAC_SettlementIndex(); CheckWorld(); return !s_Centres.IsEmpty(); }
 static int GetCount() {
		EXPBG_LazyStatics_EAC_SettlementIndex(); CheckWorld(); return s_Centres.Count(); }
 static int GetAttemptCount() { CheckWorld(); return s_Attempts; }

 static vector GetCentreAt(int index)
 {
		EXPBG_LazyStatics_EAC_SettlementIndex();
  CheckWorld();
  if (index < 0 || index >= s_Centres.Count()) return vector.Zero;
  return s_Centres[index];
 }

 // The retained descriptor type, or -1 when the index has no such centre. The
 // value is an EMapDescriptorType ordinal, kept as an int so a caller does not
 // have to name the enum to store it.
 static int GetTypeAt(int index)
 {
		EXPBG_LazyStatics_EAC_SettlementIndex();
  CheckWorld();
  if (index < 0 || index >= s_Types.Count()) return -1;
  return s_Types[index];
 }

 static void CopyTo(array<vector> centres)
 {
		EXPBG_LazyStatics_EAC_SettlementIndex();
  CheckWorld();
  if (!centres) return;
  centres.Clear();
  foreach (vector centre : s_Centres) centres.Insert(centre);
 }

 // Fails OPEN, and that is mandatory. An empty list means "this terrain has no
 // readable map labels", never "this position is outside every settlement": a
 // terrain whose map instance never resolves headless would otherwise depopulate
 // completely.
 static bool Within(vector position, float radius)
 {
		EXPBG_LazyStatics_EAC_SettlementIndex();
  CheckWorld();
  if (s_Centres.IsEmpty()) return true;
  return Within(position, radius, s_Centres);
 }

 // Pure overload with no global state, so a fixture can exercise the rule
 // deterministically. Ground plane only, matching
 // EAC_ExclusionZone.IsPopulationAllowed: height must not decide membership.
 static bool Within(vector position, float radius, array<vector> centres)
 {
  if (!centres || centres.IsEmpty()) return true;
  float limit = radius * radius;
  foreach (vector centre : centres)
  {
   float dx = position[0] - centre[0];
   float dz = position[2] - centre[2];
   if (dx * dx + dz * dz <= limit) return true;
  }
  return false;
 }

	//------------------------------------------------------------------------------------------------
	//! Creates the collections on first use (not in the global static initializer, which has a
	//! per-function instruction limit that large modsets exceed on Windows).
	protected static void EXPBG_LazyStatics_EAC_SettlementIndex()
	{
		if (!s_Centres)
			s_Centres = new array<vector>();
		if (!s_Types)
			s_Types = new array<int>();
	}
}
