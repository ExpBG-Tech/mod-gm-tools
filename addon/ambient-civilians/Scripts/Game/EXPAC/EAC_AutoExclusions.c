// Immutable authority-side geography, prepared before the first house/spawn work.
// Map labels have no compound perimeter. These discs are conservative policy
// buffers; unlabelled/custom perimeters still need an authored No Civilian Zone.
class EAC_AutoExclusions
{
 static const int MILITARY_RADIUS = 350;
 static const int AIRPORT_RADIUS = 1000;
 static const int MAX_AREAS = 256;
 static const int MAX_ITEMS = 4096;
 protected static BaseWorld s_World;
 protected static ref array<vector> s_Centres;
 protected static ref array<float> s_Radii;
 protected static bool s_Complete, s_Failed, s_WaitReported;
 protected static int s_Queries;

 static void CheckWorld()
 {
		EXPBG_LazyStatics_EAC_AutoExclusions();
  if (!GetGame()) return;
  BaseWorld world = GetGame().GetWorld();
  if (world == s_World) return;
  s_World = world;
  s_Centres.Clear(); s_Radii.Clear();
  s_Complete = false; s_Failed = false; s_WaitReported = false; s_Queries = 0;
 }

 static bool IsReady()
 {
  return GetGame() && GetGame().GetWorld() == s_World && s_Complete && !s_Failed;
 }

 // Exact identifiers, never translated substring guesses such as "Airport Bay".
 static int LabelRadius(int type, string name)
 {
  name.ToLower();
  if (type == EMapDescriptorType.MDT_AIRPORT || name == "#ar-maplocation_airport" || name == "#ar-maplocation_airfield" || name == "airport" || name == "airfield") return AIRPORT_RADIUS;
  if (type == EMapDescriptorType.MDT_BASE || name == "#ar-maplocation_military" || name == "#ar-maplocation_militarybase" || name == "#ar-maplocation_militarytrainingarea" || name == "military" || name == "military base" || name == "military training area") return MILITARY_RADIUS;
  return 0;
 }

 protected static void Fail(string reason)
 {
  s_Failed = true; s_Complete = true;
  Print("[EAC auto exclusions] population blocked: " + reason, LogLevel.ERROR);
 }

 protected static void Add(vector centre, float radius)
 {
		EXPBG_LazyStatics_EAC_AutoExclusions();
  // Only identical centres merge; nearby labels may cover distinct perimeters.
  for (int i = 0; i < s_Centres.Count(); i++)
  {
   if (vector.DistanceSqXZ(centre, s_Centres[i]) > 0.01) continue;
   s_Radii[i] = Math.Max(s_Radii[i], radius);
   return;
  }
  if (s_Centres.Count() >= MAX_AREAS) { Fail("restricted-area capacity exceeded"); return; }
  s_Centres.Insert(centre); s_Radii.Insert(radius);
 }

 static void Prepare()
 {
		EXPBG_LazyStatics_EAC_AutoExclusions();
  if (!GetGame() || !Replication.IsServer()) return;
  CheckWorld();
  if (!s_World || s_Complete) return;
  // Let terrain INIT and deferred descriptor registration finish. Also works
  // when the controller is placed by GM later in an already running mission.
  if (s_World.GetWorldTime() < 2000) return;
  SCR_MapEntity terrainMap = SCR_MapEntity.GetMapInstance();
  if (!terrainMap)
  {
   if (!s_WaitReported) Print("[EAC auto exclusions] waiting for terrain map; population blocked");
   s_WaitReported = true;
   return;
  }
  array<int> types = {EMapDescriptorType.MDT_BASE, EMapDescriptorType.MDT_AIRPORT, EMapDescriptorType.MDT_NAME_GENERIC, EMapDescriptorType.MDT_NAME_LOCAL, EMapDescriptorType.MDT_NAME_SETTLEMENT, EMapDescriptorType.MDT_NAME_VILLAGE, EMapDescriptorType.MDT_NAME_TOWN, EMapDescriptorType.MDT_NAME_CITY, EMapDescriptorType.MDT_LANDMARK};
  foreach (int type : types)
  {
   array<MapItem> items = {};
   terrainMap.GetByType(items, type); s_Queries++;
   if (items.Count() > MAX_ITEMS) { Fail("terrain descriptor capacity exceeded"); return; }
   foreach (MapItem item : items)
   {
    if (!item) continue;
    int radius = LabelRadius(type, item.GetDisplayName());
    if (radius <= 0) continue;
    // GetRange is radio coverage, not a compound boundary. Never use it here.
    vector position = item.GetPos();
    if (item.Entity()) position = item.Entity().GetOrigin();
    Add(position, radius);
    if (s_Failed) return;
    if (EAC_AmbientModule.GetDebugLevelMirror() >= 2) PrintFormat("[EAC auto exclusions] label=%1 position=%2 radius=%3", item.GetDisplayName(), position, radius);
   }
  }
  SCR_MilitaryBaseSystem bases = SCR_MilitaryBaseSystem.GetInstance();
  if (bases)
  {
   if (bases.GetBasesCount() > MAX_AREAS) { Fail("military-base capacity exceeded"); return; }
   array<SCR_MilitaryBaseComponent> registered = {};
   bases.GetBases(registered);
   foreach (SCR_MilitaryBaseComponent base : registered)
   {
    if (!base || !base.GetOwner()) continue;
    Add(base.GetOwner().GetOrigin(), Math.Max(base.GetRadius(), 0) + 25);
    if (s_Failed) return;
   }
  }
  s_Complete = true;
  PrintFormat("[EAC auto exclusions] ready areas=%1 descriptor_queries=%2 military_label_m=%3 airport_label_m=%4", s_Centres.Count(), s_Queries, MILITARY_RADIUS, AIRPORT_RADIUS);
 }

 static int GetCount() {
		EXPBG_LazyStatics_EAC_AutoExclusions(); return s_Centres.Count(); }
 static int GetQueryCount() { return s_Queries; }
 static bool AnyTransitBlocked() {
		EXPBG_LazyStatics_EAC_AutoExclusions(); return !IsReady() || !s_Centres.IsEmpty(); }

 static bool IsPopulationAllowed(vector position)
 {
		EXPBG_LazyStatics_EAC_AutoExclusions();
  if (!IsReady()) return false;
  // ponytail: linear in at most 256 fixed discs; use a grid only if profiled hot.
  for (int i = 0; i < s_Centres.Count(); i++)
   if (vector.DistanceSqXZ(position, s_Centres[i]) <= s_Radii[i] * s_Radii[i]) return false;
  return true;
 }

 static bool IsTransitAllowed(vector from, vector to)
 {
		EXPBG_LazyStatics_EAC_AutoExclusions();
  if (!IsReady()) return false;
  for (int i = 0; i < s_Centres.Count(); i++)
   if (EAC_ExclusionZone.SegmentIntersectsDisc(from, to, s_Centres[i], s_Radii[i])) return false;
  return true;
 }

 static string Describe()
 {
		EXPBG_LazyStatics_EAC_AutoExclusions();
  if (s_Failed) return "auto exclusions FAILED (population blocked)";
  if (!IsReady()) return "auto exclusions preparing (population blocked)";
  return "auto exclusions ready areas=" + s_Centres.Count().ToString() + " queries=" + s_Queries.ToString();
 }

	//------------------------------------------------------------------------------------------------
	//! Creates the collections on first use (not in the global static initializer, which has a
	//! per-function instruction limit that large modsets exceed on Windows).
	protected static void EXPBG_LazyStatics_EAC_AutoExclusions()
	{
		if (!s_Centres)
			s_Centres = new array<vector>();
		if (!s_Radii)
			s_Radii = new array<float>();
	}
}
