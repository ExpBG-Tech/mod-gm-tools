class EAC_HomeScanCursor
{
 int X, Z, Radius, Offset;
 int LeadX, LeadZ, LeadOffset;
 bool ScanLead;
}

class EAC_HomeCell
{
 vector Origin;
 int Size;
}

// Mission-only house discovery. This class never creates a character/entity.
class EAC_HomeIndex
{
 static const int CELL_SIZE = 64;
 static const int MAX_CALLBACKS = 1024;
 static const int MAX_CELLS = 65536;
 static const ResourceName DEFAULT_CHARACTER = "{E024A74F8A4BC644}Prefabs/Characters/Factions/CIV/Businessman/Character_CIV_Businessman_1.et";
 protected ref EAC_HouseholdRegistry m_Registry = new EAC_HouseholdRegistry();
 // Keyed by a packed cell index rather than a formatted "x:z" string.
 // DiscoverNearPlayer probes up to 73 cells on every scheduler tick for the rest
 // of the mission once a ring is fully indexed, and every probe used to allocate
 // a string just to ask whether the cell was already known (audit item 8). The
 // pack is plain arithmetic - no bit operators - and covers +-16384 cells, which
 // is +-1048 km at CELL_SIZE 64.
 protected ref map<int, bool> m_Cells = new map<int, bool>();
 protected ref map<int, ref EAC_HomeScanCursor> m_Focus = new map<int, ref EAC_HomeScanCursor>();
 protected ref array<ref EAC_HomeCell> m_Pending = {};
 protected int m_Small, m_Large, m_Callbacks, m_Queries, m_Saturated, m_Rejected, m_ReconcileCursor;
 protected int m_Subdivisions;
 protected vector m_AreaCentre;
 protected int m_AreaRadius;
 protected bool m_Aborted;

 // Isolated-house policy (readiness plan section 3). Rule 0 is today's behaviour
 // exactly: IsHomeEnrolled returns true before reading any other field, Visit
 // never calls Qualifies, and not one additional world query is issued at any
 // rule. m_CellHomes is a registry-id index keyed by the same 64 m cell string
 // the discovery map already uses, so the neighbour test is a bounded map walk
 // rather than a scan of every household.
 static const int MAX_NEIGHBOUR_CELL_SPAN = 4;   // 4 x CELL_SIZE = the 256 m clamp
 static const int MAX_NEIGHBOUR_IDS = 256;
 protected int m_Rule;
 protected int m_ClusterMin = 2;
 protected int m_ClusterRadius = 140;
 protected int m_SettlementRadius = 400;
 // Starts at 1 so a fresh EAC_HouseholdRecord (EnrolStamp 0) is always stale.
 protected int m_Stamp = 1;
 protected int m_Isolated, m_IsolatedPass;
 // -1 so the first rule-2 reconcile always re-evaluates. See Reconcile.
 protected int m_SettlementCentres = -1;
 protected ref map<string, ref array<int>> m_CellHomes = new map<string, ref array<int>>();

 // Mission-start prewarm. Separate from m_Pending: m_Pending is the dense-cell
 // SUBDIVISION frontier, drained LIFO at DiscoverCellAt and asserted empty by the
 // Households fixture. Mixing prewarm cells into it would break both the
 // depth-first invariant and that assertion. One settlement is expanded at a
 // time, so this queue never exceeds one block (225 cells at the largest ring).
 protected ref array<ref EAC_HomeCell> m_Prewarm = {};
 // The three rings are constants, not settings: PrewarmCellsPerTick is the only
 // lever the operator gets, so there is one knob rather than four. The setter
 // takes them only so a fixture can pin a small deterministic block.
 static const int PREWARM_RING_VILLAGE = 5;   // 121 cells, 704 m box
 static const int PREWARM_RING_TOWN = 6;      // 169 cells, 832 m box (unverified)
 static const int PREWARM_RING_CITY = 7;      // 225 cells, 960 m box (unverified)
 static const int PREWARM_MAX_RING = 8;
 protected int m_PrewarmMode, m_PrewarmBudget;
 protected int m_PrewarmSettlement;
 protected int m_PrewarmQueued, m_PrewarmDone, m_PrewarmDuplicate, m_PrewarmSettlementsDone;
 protected float m_PrewarmStartedAt, m_PrewarmFinishedAt;
 protected bool m_PrewarmComplete;
 protected int m_PrewarmRadiusVillage = PREWARM_RING_VILLAGE;
 protected int m_PrewarmRadiusTown = PREWARM_RING_TOWN;
 protected int m_PrewarmRadiusCity = PREWARM_RING_CITY;
 // Test seat only; null means the real EAC_SettlementIndex, which is what the
 // runtime always uses. See EAC_QASetPrewarmCentres.
 protected ref array<vector> m_PrewarmCentres;
 protected ref array<int> m_PrewarmTypes;

 EAC_HouseholdRegistry GetRegistry() { return m_Registry; }
 int GetQueryCount() { return m_Queries; }
 int GetLastCallbackCount() { return m_Callbacks; }
 int GetSaturatedCellCount() { return m_Saturated; }
 int GetRejectedHomeCount() { return m_Rejected; }
 int GetRootCellCount() { return m_Cells.Count(); }
 int GetPendingCellCount() { return m_Pending.Count(); }
 int GetSubdivisionCount() { return m_Subdivisions; }
 int GetIsolatedHomeCount() { return m_Isolated; }
 int GetIsolationRule() { return m_Rule; }
 int GetPolicyStamp() { return m_Stamp; }

 // ---------------------------------------------------------------------------
 // House recognition (readiness plan S2, setting key 40).
 //
 // ResolveHouseClass and IsApprovedLeaf are static - they classify a path and
 // hold no index state - so the setting reaches them through a static mirror
 // rather than a member. EAC_AmbientModule pushes it every scheduler tick,
 // exactly as it pushes SetPolicy and SetPrewarmPolicy, so there is one owner of
 // the value and no way for a module edit to be missed.
 //
 // 0 accepts inspected homes, inherited variants and native whole buildings
 // with residential leaf names after bounded nonresidential exclusions.
 // 1 additionally admits any prefab under
 // prefabs/structures/houses/ that survives the ruin/destruction/furniture/
 // building-addon/base rejections at the top of ResolveHouseClass, as a
 // small-household home. That is the terrain safety valve for a map whose houses
 // this build has never seen, and it is UNVALIDATED: nothing proves such a prefab
 // has a navigable interior, and on a vanilla map it also admits the loose parts
 // the exact leaf lists exist to exclude.
 // ---------------------------------------------------------------------------
 static const string HOUSE_ROOT = "prefabs/structures/houses/";
 protected static int s_HouseFilter;
 static void SetHouseFilter(int filter) { s_HouseFilter = Math.Clamp(filter, 0, 1); }
 static int GetHouseFilter() { return s_HouseFilter; }

 // ---------------------------------------------------------------------------
 // Isolated-house policy.
 //
 // A separate setter rather than extra parameters on DiscoverCellAt /
 // DiscoverNearPlayer, because tests/Enforce/EAC_HomeIndexTests.c calls those
 // signatures directly. Any change bumps the stamp, which invalidates every
 // cached verdict at once without walking the registry.
 // ---------------------------------------------------------------------------
 void SetPolicy(int rule, int clusterMin, int clusterRadius, int settlementRadius)
 {
  int wantedRule = Math.Clamp(rule, 0, 3);
  int wantedMin = Math.Clamp(clusterMin, 1, 8);
  int wantedRadius = Math.Clamp(clusterRadius, 40, 256);
  int wantedSettlement = Math.Clamp(settlementRadius, 100, 1500);
  if (wantedRule == m_Rule && wantedMin == m_ClusterMin && wantedRadius == m_ClusterRadius && wantedSettlement == m_SettlementRadius) return;
  m_Rule = wantedRule;
  m_ClusterMin = wantedMin;
  m_ClusterRadius = wantedRadius;
  m_SettlementRadius = wantedSettlement;
  m_Stamp++;
  m_Isolated = 0;
  m_IsolatedPass = 0;
 }

 // How many 64 m cells the cluster radius reaches, as a float division: '%' in a
 // float expression is a compile failure in this dialect. The clamp of four is
 // what makes the block scan at most 81 cells and is why HomeClusterRadius is
 // clamped at 256 (4 x CELL_SIZE); raising one requires raising the other.
 static int ClusterSpan(int clusterRadius)
 {
  int cells = Math.Ceil(Math.Clamp(clusterRadius, 1, 4096) * 1.0 / CELL_SIZE);
  return Math.Clamp(cells, 1, MAX_NEIGHBOUR_CELL_SPAN);
 }

 // Its own helper because Enforce has no labelled break out of a nested loop.
 // Bounded twice over: at most 81 cells and at most 256 examined ids, with an
 // early return as soon as the cluster minimum is met.
 protected int CountNeighbours(vector position, int selfId)
 {
  int span = ClusterSpan(m_ClusterRadius);
  float limit = m_ClusterRadius * m_ClusterRadius;
  int baseX = Math.Floor(position[0] / CELL_SIZE);
  int baseZ = Math.Floor(position[2] / CELL_SIZE);
  int found;
  int examined;
  for (int dx = -span; dx <= span; dx++)
  {
   for (int dz = -span; dz <= span; dz++)
   {
    int cellX = baseX + dx;
    int cellZ = baseZ + dz;
    string key = cellX.ToString() + ":" + cellZ.ToString();
    array<int> ids = m_CellHomes.Get(key);
    if (!ids) { continue; }
    foreach (int id : ids)
    {
     if (examined >= MAX_NEIGHBOUR_IDS) { return found; }
     examined++;
     if (id == selfId) { continue; }
     EAC_HouseholdRecord other = m_Registry.GetHome(id - 1);
     if (!other) { continue; }
     float dxMetres = position[0] - other.Position[0];
     float dzMetres = position[2] - other.Position[2];
     if (dxMetres * dxMetres + dzMetres * dzMetres > limit) { continue; }
     found++;
     if (found >= m_ClusterMin) { return found; }
    }
   }
  }
  return found;
 }

 // Ground plane only, matching EAC_ExclusionZone.IsPopulationAllowed: height must
 // not decide membership. selfId 0 is the pre-registration case in Visit, where
 // the candidate is not in m_CellHomes yet and so cannot count itself.
 protected bool Qualifies(vector position, int selfId)
 {
  if (m_Rule <= 0) return true;
  // Hoisted: this dialect has failed on instance reads used directly as
  // arguments to a static call.
  float radius = m_SettlementRadius;
  if (m_Rule == 2) return EAC_SettlementIndex.Within(position, radius);
  bool clustered = CountNeighbours(position, selfId) >= m_ClusterMin;
  if (m_Rule == 1) return clustered;
  if (!clustered) return false;
  return EAC_SettlementIndex.Within(position, radius);
 }

 // Amortised O(1): the stamp only moves when a home registers or an operator
 // changes a setting, and stops moving once an area is indexed.
 bool IsHomeEnrolled(EAC_HouseholdRecord home)
 {
  if (!home || EAC_AmbientModule.IsOutsidePopulationArea(home.Position)) return false;
  if (m_Rule == 0) return true;
  if (home.EnrolStamp == m_Stamp) return home.Enrolled;
  home.Enrolled = Qualifies(home.Position, home.Id);
  home.EnrolStamp = m_Stamp;
  return home.Enrolled;
 }

 // Registry ids per 64 m cell, inserted once per household. The cell key matches
 // the one DiscoverCellAt uses, so the two maps describe the same grid.
 protected void RememberCell(EAC_HouseholdRecord home)
 {
  if (!home) return;
  int cellX = Math.Floor(home.Position[0] / CELL_SIZE);
  int cellZ = Math.Floor(home.Position[2] / CELL_SIZE);
  string key = cellX.ToString() + ":" + cellZ.ToString();
  array<int> ids = m_CellHomes.Get(key);
  if (!ids)
  {
   if (m_CellHomes.Count() >= MAX_CELLS) { return; }
   array<int> created = {};
   m_CellHomes.Insert(key, created);
   ids = created;
  }
  if (ids.Contains(home.Id)) { return; }
  ids.Insert(home.Id);
  // A new neighbour can change any nearby verdict, so every cached verdict is
  // invalidated at once. This is what lets a hamlet bootstrap: the third house
  // to register flips the first two.
  m_Stamp++;
 }

 // Never infer residence from a destroyed, partial or nonresidential prefab,
 // including when that prefab inherits an otherwise supported intact house.
 static bool IsRejectedHousePath(ResourceName resource, bool rejectBase = true)
 {
  string path = resource; path.ToLower();
  return path.Contains("/dst/") || path.Contains("ruin") || path.Contains("destroyed") || path.Contains("/furniture/") || path.Contains("/buildingaddons/") || path.Contains("/buildingparts/") || (rejectBase && path.Contains("_base.et")) || path.Contains("/military/") || path.Contains("barrack") || path.Contains("police") || path.Contains("hospital") || path.Contains("pumpa_food");
 }

 // These exclusions apply before unfamiliar names or inheritance can admit a
 // home. A generic Building_Base ancestor is normal; spawning that base itself
 // is still rejected by IsRejectedHousePath's default above.
 protected static bool IsRejectedGenericHousePath(ResourceName resource)
 {
  if (IsRejectedHousePath(resource, false)) return true;
  string path = resource; path.ToLower();
  if (path.Contains("composition") || path.Contains("/props/") || path.Contains("/commercial/") || path.Contains("/industrial/") || path.Contains("/infrastructure/") || path.Contains("/construction/") || path.Contains("/agriculture/")) return true;
  array<string> parts = {}; path.Split("/", parts, true);
  if (parts.IsEmpty()) return true;
  string leaf = parts[parts.Count() - 1];
  if (leaf.Contains("_dst") || leaf.Contains("damage") || leaf.Contains("rubble") || leaf.Contains("debris") || leaf.Contains("military") || leaf.Contains("bunker") || leaf.Contains("hangar") || leaf.Contains("tower")) return true;
  if (leaf.Contains("guardhouse") || leaf.Contains("gatehouse") || leaf.Contains("warehouse") || leaf.Contains("greenhouse") || leaf.Contains("boathouse") || leaf.Contains("lighthouse") || leaf.Contains("shed") || leaf.Contains("garage")) return true;
  if (leaf.Contains("barn") || leaf.Contains("office") || leaf.Contains("school") || leaf.Contains("shop") || leaf.Contains("store") || leaf.Contains("factory") || leaf.Contains("church") || leaf.Contains("chapel")) return true;
  if (leaf.Contains("mosque") || leaf.Contains("station") || leaf.Contains("door") || leaf.Contains("window") || leaf.Contains("furniture") || leaf.Contains("paneling") || leaf.Contains("roof") || leaf.Contains("wall")) return true;
  if (leaf.Contains("stairs") || leaf.Contains("terrace") || leaf.Contains("balcony") || leaf.Contains("foundation")) return true;
  return false;
 }

 protected static bool IsGenericResidentialLeaf(ResourceName resource)
 {
  string path = resource; path.ToLower();
  array<string> parts = {}; path.Split("/", parts, true);
  if (parts.IsEmpty()) return false;
  string leaf = parts[parts.Count() - 1];
  // Shared geometry bases are neutral; a named complete residence or an intact
  // inspected ancestor supplies the positive evidence instead.
  if (leaf.Contains("_base.et")) return false;
  leaf.Replace(".et", ""); leaf.Replace("_", " "); leaf.Replace("-", " ");
  array<string> words = {}; leaf.Split(" ", words, true);
  foreach (string word : words)
  {
   if (word == "house" || word == "farmhouse" || word == "cabin" || word == "cottage" || word == "apartment" || word == "apartments" || word == "apartmentbuilding" || word == "villa" || word == "residence" || word == "bungalow" || word == "tenement") return true;
  }
  return false;
 }

 // Explicit residential families, including the locally inspected ATC assets
 // used by Takistan/Kunar. No added mod dependency or world-wide scan.
 static int ResolveHouseClass(ResourceName resource)
 {
  string path = resource;
  path.ToLower();
  if (IsRejectedHousePath(resource)) return 0;
  array<string> parts = {};
  path.Split("/", parts, true);
  if (parts.Count() < 3) return 0;
  string family = parts[parts.Count() - 2];
  string leaf = parts[parts.Count() - 1];
  if (path.Contains("prefabs/structures/bulding/"))
  {
   if (family == "houseb" && leaf.IndexOf("house_b_tenement_") == 0) return 2;
   if (family == "panelak" && leaf.IndexOf("panelak_apartment_") == 0) return 2;
   if (family == "housec" && leaf.IndexOf("house_c_") == 0) return 1;
   if (family == "housek" && leaf.IndexOf("house_k_") == 0) return 1;
   if (family == "housel" && leaf.IndexOf("house_l_") == 0) return 1;
   if (family == "dumistan" && leaf.IndexOf("dum_istan") == 0) return 1;
   if (family == "favella" && (leaf.IndexOf("favella_house_") == 0 || leaf.IndexOf("favella_makako_") == 0)) return 1;
   return 0;
  }
  if (!IsApprovedLeaf(family, leaf))
  {
   // House recognition 1 only. At 0 this is the same early return it always was.
   if (s_HouseFilter == 1 && path.Contains(HOUSE_ROOT)) { return 1; }
   return 0;
  }
  if (path.Contains("prefabs/structures/houses/village/"))
  {
   if (family == "house_mountain_e_1i01") return 1;
   if (family == "house_village_e_1i01" || family == "house_village_e_1i02" || family == "house_village_e_1i03" || family == "house_village_e_1i04" || family == "house_village_e_1i05" || family == "house_village_e_1i06" || family == "house_village_e_1i08" || family == "house_village_e_1l01" || family == "house_village_e_1l02" || family == "house_village_e_1l03") return 1;
  }
  if (path.Contains("prefabs/structures/houses/town/"))
  {
   if (family == "house_town_e_2i01" || family == "house_town_e_2i02" || family == "house_town_e_2i03") return 2;
  }
  if (path.Contains("prefabs/structures/houses/wooden/") && (family == "house_wooden_e_1i01" || family == "house_wooden_e_1i02")) return 1;
  if (path.Contains("prefabs/structures/houses/farm/") && family == "farmhouse_e_1l01") return 1;
  if (path.Contains("prefabs/structures/houses/prefabricated/") && (family == "apartmentbuilding_5i01" || family == "apartmentprefab_5i01" || family == "house_prefab_2i01" || family == "house_prefab_2i02")) return 2;
  if (path.Contains("prefabs/structures/houses/villa/") && (family == "villa_e_2i01" || family == "villa_2i04")) return 2;
  return 0;
 }

 static int ResolveEntityHouseClass(IEntity entity)
 {
  if (!entity) return 0;
  EntityPrefabData data = entity.GetPrefabData();
  if (!data || IsRejectedHousePath(data.GetPrefabName())) return 0;
  int category = ResolveHouseClass(data.GetPrefabName());
  if (category != 0) return category;
  // A recoloured/repacked house need not keep its parent's directory or filename.
  // Restrict inheritance lookup to native buildings, and bound malformed chains.
  if (!SCR_DestructibleBuildingEntity.Cast(entity)) return 0;
  if (IsRejectedGenericHousePath(data.GetPrefabName())) return 0;
  bool residential = IsGenericResidentialLeaf(data.GetPrefabName());
  bool inspectedAllowed = true;
  int inheritedCategory;
  BaseContainer prefab = data.GetPrefab();
  if (prefab) prefab = prefab.GetAncestor();
  for (int depth = 0; prefab && depth < 16; depth++)
  {
   if (IsRejectedGenericHousePath(prefab.GetResourceName())) return 0;
   // Preserve the inspected-family boundary while allowing a complete generic
   // residence to inherit the normal native base prefab.
   if (IsRejectedHousePath(prefab.GetResourceName())) inspectedAllowed = false;
   if (inspectedAllowed && inheritedCategory == 0) inheritedCategory = ResolveHouseClass(prefab.GetResourceName());
   if (IsGenericResidentialLeaf(prefab.GetResourceName())) residential = true;
   prefab = prefab.GetAncestor();
  }
  // Do not accept an unfamiliar descendant before all bounded ancestors have
  // been checked: a residential filename can disguise a ruined/military parent.
  if (prefab) return 0;
  if (inheritedCategory != 0) return inheritedCategory;
  BuildingClass buildingData = BuildingClass.Cast(data);
  if (!residential || !buildingData || !buildingData.IsBuilding()) return 0;
  // ponytail: names are a conservative fallback, not universal residence metadata.
  // Unknown naming schemes remain unsupported until their native signal is known.
  return 1;
 }

 // Exact leaves from the installed native asset catalog, including intact variants.
 protected static bool IsApprovedLeaf(string family, string leaf)
 {
  // Inspected base-game .pak leaves (including USSR names under an E-family
  // folder). Exact matching excludes paneling/doors and nonresidential conversions.
  switch (family)
  {
   case "house_wooden_e_1i01": return leaf == "house_wooden_e_1i01.et" || leaf == "house_wooden_e_1i01_panels.et" || leaf == "house_wooden_ussr_1i01.et" || leaf == "house_wooden_ussr_1i01_panels.et";
   case "house_wooden_e_1i02": return leaf == "house_wooden_e_1i02.et";
   case "farmhouse_e_1l01": return leaf == "farmhouse_e_1l01.et" || leaf == "farmhouse_e_1l01_green.et" || leaf == "farmhouse_e_1l01_wood.et" || leaf == "farmhouse_ussr_1l01.et" || leaf == "farmhouse_ussr_1l01_green.et" || leaf == "farmhouse_ussr_1l01_wood.et";
   case "house_mountain_e_1i01": return leaf == "house_mountain_e_1i01.et" || leaf == "house_mountain_e_1i01_short.et";
   case "apartmentbuilding_5i01": return leaf == "apartmentbuilding_ussr_5i01.et";
   case "apartmentprefab_5i01": return leaf == "apartmentprefab_ussr_5i01_blue.et" || leaf == "apartmentprefab_ussr_5i01_gray.et";
   case "house_prefab_2i01": return leaf == "house_prefab_2i01_v1.et" || leaf == "house_prefab_2i01_v2.et";
   case "house_prefab_2i02": return leaf == "house_prefab_2i02t.et";
   case "villa_e_2i01": return leaf == "villa_e_2i01.et";
   case "villa_2i04": return leaf == "villa_ussr_2i04_guba.et";
  }
  string suffix = "";
  if (leaf.IndexOf(family) != 0) return false;
  suffix = leaf.Substring(family.Length(), leaf.Length() - family.Length());
  switch (family)
  {
   case "house_village_e_1i01": return suffix == ".et" || suffix == "_v2.et" || suffix == "_beige.et" || suffix == "_beige_v2.et";
   case "house_village_e_1i02": return suffix == ".et" || suffix == "_white.et";
   case "house_village_e_1i03": return suffix == ".et" || suffix == "_yellow.et";
   case "house_village_e_1i04": return suffix == "sf.et" || suffix == "sf_green.et" || suffix == "sf_red.et" || suffix == "sr.et" || suffix == "sr_green.et" || suffix == "sr_red.et" || suffix == "t.et" || suffix == "t_green.et" || suffix == "t_red.et";
   case "house_village_e_1i05": return suffix == "s.et" || suffix == "t.et";
   case "house_village_e_1i06": return suffix == ".et" || suffix == "_brown.et" || suffix == "_white.et";
   case "house_village_e_1i08": return suffix == "t.et" || suffix == "t_white.et";
   case "house_village_e_1l01": return suffix == ".et";
   case "house_village_e_1l02": return suffix == "t.et";
   case "house_village_e_1l03": return suffix == "t.et" || suffix == "t_yellow.et";
   case "house_town_e_2i01": return suffix == ".et" || suffix == "_orange.et";
   case "house_town_e_2i02": return suffix == ".et" || suffix == "_v2.et" || suffix == "_yellow.et";
   case "house_town_e_2i03": return suffix == ".et" || suffix == "_v2.et";
  }
  return false;
 }

 protected bool Visit(IEntity entity)
 {
  m_Callbacks++;
  if (m_Callbacks >= MAX_CALLBACKS) { m_Aborted = true; return false; }
  if (!entity) return true;
  if (EAC_AmbientModule.IsOutsidePopulationArea(entity.GetOrigin())) return true;
  int category = ResolveEntityHouseClass(entity);
  if (category == 0) return true;
  // Permanent startup geography excludes homes before allocating resident slots.
  // Manual GM zones stay dynamic and are checked at admission/wake as before.
  if (!EAC_AutoExclusions.IsPopulationAllowed(entity.GetOrigin())) return true;
  int occupancy = m_Small;
  if (category == 2) occupancy = m_Large;
  // Every supported house is still registered, at every rule. A declined house
  // gets zero residents, not zero existence: it stays in the debug draw, it is
  // reconsidered by Reconcile, and - the load-bearing part - it counts as a
  // neighbour for the houses around it, which is how a hamlet bootstraps.
  // The declined count is published by Reconcile's full-sweep accumulator, not
  // here: a registration-time increment would mix a one-off event into a figure
  // that is meant to describe the current registry.
  vector where = entity.GetOrigin();
  if (m_Rule > 0 && !Qualifies(where, 0)) { occupancy = 0; }
  EAC_HouseholdRecord home = m_Registry.Register(entity, category == 2, occupancy, DEFAULT_CHARACTER);
  if (!home) { m_Rejected++; return true; }
  RememberCell(home);
  return true;
 }

 // One int per 64 m root cell. Out-of-range coordinates are clamped rather than
 // wrapped, so two distant cells can never collide on one key.
 static int CellKey(int x, int z)
 {
  int cellX = Math.Clamp(x, -16384, 16383);
  int cellZ = Math.Clamp(z, -16384, 16383);
  return (cellX + 16384) * 32768 + (cellZ + 16384);
 }

 // Used by the shared player scheduler and native acceptance fixture.
 bool DiscoverCellAt(vector position, int small, int large)
 {
  if (!Replication.IsServer() || !GetGame() || !EAC_AutoExclusions.IsReady() || small < 0 || small > 20 || large < 0 || large > 20) return false;
  m_Small = small; m_Large = large;
  // Drain dense-cell children first, so the depth-first frontier stays at most ten.
  if (!m_Pending.IsEmpty())
  {
   EAC_HomeCell pending = m_Pending[m_Pending.Count() - 1];
   m_Pending.Remove(m_Pending.Count() - 1);
   QueryCell(pending.Origin, pending.Size);
   return true;
  }
  int x = Math.Floor(position[0] / CELL_SIZE);
  int z = Math.Floor(position[2] / CELL_SIZE);
  int key = CellKey(x, z);
  if (m_Cells.Contains(key)) return false;
  if (m_Cells.Count() >= MAX_CELLS)
  {
   return false;
  }
  vector mins = Vector(x * CELL_SIZE, -1000, z * CELL_SIZE);
  m_Cells.Insert(key, true);
  QueryCell(mins, CELL_SIZE);
  return true;
 }

 // Moving/resizing a module must make previously skipped cells eligible again.
 void SetPopulationArea(vector centre, int radius)
 {
  if (m_AreaRadius == radius && m_AreaCentre[0] == centre[0] && m_AreaCentre[2] == centre[2]) return;
  m_AreaCentre = centre; m_AreaRadius = radius;
  m_Cells.Clear(); m_Focus.Clear(); m_Pending.Clear();
 }

 protected void QueryCell(vector mins, int size)
 {
  if (m_AreaRadius > 0)
  {
   float closestX = Math.Clamp(m_AreaCentre[0], mins[0], mins[0] + size);
   float closestZ = Math.Clamp(m_AreaCentre[2], mins[2], mins[2] + size);
   if (!EAC_AmbientModule.ContainsPopulationPoint(closestX, closestZ, m_AreaCentre[0], m_AreaCentre[2], m_AreaRadius)) return;
  }
  m_Callbacks = 0; m_Aborted = false;
  vector maxs = mins + Vector(size, 11000, size);
  m_Queries++;
  GetGame().GetWorld().QueryEntitiesByAABB(mins, maxs, Visit, null, EQueryEntitiesFlags.ALL);
  if (m_Aborted)
  {
   if (size > 8)
   {
    m_Subdivisions++;
    int half = size / 2;
    for (int x = 0; x < 2; x++)
    {
     for (int z = 0; z < 2; z++)
     {
      EAC_HomeCell child = new EAC_HomeCell();
      child.Origin = mins + Vector(x * half, 0, z * half);
      child.Size = half;
      m_Pending.Insert(child);
     }
    }
   }
   else
   {
    m_Saturated++;
   }
  }
 }

 void RetainPlayers(array<int> players)
 {
  for (int i = m_Focus.Count() - 1; i >= 0; i--)
   if (!players.Contains(m_Focus.GetKey(i))) m_Focus.Remove(m_Focus.GetKey(i));
 }

 // Center-out square rings cover every cell once; an offset never starts at the
 // far corner of the configured radius while nearby households wait to index.
 static vector CellOffset(int offset)
 {
  if (offset <= 0) return vector.Zero;
  int ring = Math.Ceil((Math.Sqrt(offset + 1) - 1) * 0.5);
  int side = ring * 2;
  int inner = side - 1;
  int edge = offset - inner * inner;
  if (edge < side) return Vector(-ring + edge, 0, -ring);
  if (edge < side * 2) return Vector(ring, 0, -ring + edge - side);
  if (edge < side * 3) return Vector(ring - edge + side * 2, 0, ring);
  return Vector(-ring, 0, ring - edge + side * 3);
 }

 static vector ApproachPosition(vector position, vector travelDirection, int distance)
 {
  travelDirection[1] = 0;
  if (travelDirection.LengthSq() < 0.01) return position;
  return position + travelDirection.Normalized() * Math.Min(Math.Clamp(distance, 50, 5000) * 0.65, 500);
 }

 // Each controlled-character focus gets one query. Alternate approach and local
 // work so moving players neither discover only their old cells nor starve them.
 // Returns whether physical discovery work was performed this call, so a caller
 // can spend an otherwise idle tick elsewhere. House discovery keeps priority.
 bool DiscoverNearPlayer(int playerId, vector position, int distance, int small, int large, vector travelDirection = vector.Zero)
 {
  if (playerId <= 0 || !Replication.IsServer()) return false;
  EAC_HomeScanCursor cursor = m_Focus.Get(playerId);
  if (!cursor)
  {
   if (m_Focus.Count() >= 128) return false;
   cursor = new EAC_HomeScanCursor();
   m_Focus.Insert(playerId, cursor);
  }
  int x = Math.Floor(position[0] / CELL_SIZE);
  int z = Math.Floor(position[2] / CELL_SIZE);
  int radius = Math.Ceil(Math.Clamp(distance, 50, 5000) * 1.0 / CELL_SIZE);
  if (cursor.X != x || cursor.Z != z || cursor.Radius != radius)
  {
   cursor.X = x; cursor.Z = z; cursor.Radius = radius; cursor.Offset = 0;
  }
  int side = radius * 2 + 1;
  int total = side * side;
  // The currently occupied cell always gets first discovery.
  if (DiscoverCellAt(position, small, large)) return true;
  cursor.ScanLead = !cursor.ScanLead;
  travelDirection[1] = 0;
  if (cursor.ScanLead && travelDirection.LengthSq() >= 0.01)
  {
   vector lead = ApproachPosition(position, travelDirection, distance);
   int leadX = Math.Floor(lead[0] / CELL_SIZE);
   int leadZ = Math.Floor(lead[2] / CELL_SIZE);
   if (cursor.LeadX != leadX || cursor.LeadZ != leadZ)
   { cursor.LeadX = leadX; cursor.LeadZ = leadZ; cursor.LeadOffset = 0; }
   // At most nine existing-key checks and one existing capped spatial query.
   for (int leadAttempt = 0; leadAttempt < 9; leadAttempt++)
   {
    vector leadOffset = CellOffset(cursor.LeadOffset);
    cursor.LeadOffset = (cursor.LeadOffset + 1) % 9;
    vector leadCell = Vector((leadX + leadOffset[0]) * CELL_SIZE, 0, (leadZ + leadOffset[2]) * CELL_SIZE);
    if (DiscoverCellAt(leadCell, small, large)) return true;
   }
  }
  for (int attempt = 0; attempt < 64; attempt++)
  {
   int offset = cursor.Offset;
   cursor.Offset = (cursor.Offset + 1) % total;
   vector delta = CellOffset(offset);
   vector cell = Vector((x + delta[0]) * CELL_SIZE, 0, (z + delta[2]) * CELL_SIZE);
   if (DiscoverCellAt(cell, small, large)) return true;
  }
  return false;
 }

 // Reconcile at most eight existing households per shared scheduler tick.
 void Reconcile(int small, int large, bool allowGrowth = true)
 {
  int count = m_Registry.GetHomeCount();
  // The settlement list resolves late - 802 recorded evidence lines show towns=0
  // on early ticks - and Within deliberately fails open until it does. A verdict
  // cached inside that window would otherwise stay enrolled for the whole
  // mission, because nothing else moves the stamp once an area is indexed. Gated
  // on the rules that actually read the list, so rules 0 and 1 pay nothing.
  if (m_Rule >= 2)
  {
   int centres = EAC_SettlementIndex.GetCount();
   if (centres != m_SettlementCentres)
   {
    m_SettlementCentres = centres;
    m_Stamp++;
   }
  }
  for (int i = 0; i < Math.Min(8, count); i++)
  {
   // The wrap publishes the count accumulated over one complete sweep, so
   // isolated_homes= is a household figure rather than an evaluation counter and
   // costs no extra pass over the registry.
   if (m_ReconcileCursor >= count)
   {
    m_ReconcileCursor = 0;
    m_Isolated = m_IsolatedPass;
    m_IsolatedPass = 0;
   }
   EAC_HouseholdRecord home = m_Registry.GetHome(m_ReconcileCursor++);
   int occupancy = small;
   if (home.Large) occupancy = large;
   // Growth suppression only. SetOccupancy(home, 0, ...) would clear Wanted, and
   // nothing in this codebase deletes an unwanted committed resident - it would
   // stand forever holding a PopulationLimit slot. A home that later qualifies
   // grows back on its next reconcile pass.
   if (!IsHomeEnrolled(home))
   {
    occupancy = Math.Min(occupancy, home.Residents.Count());
    m_IsolatedPass++;
   }
   if (!allowGrowth) occupancy = Math.Min(occupancy, home.Residents.Count());
   m_Registry.SetOccupancy(home, occupancy, DEFAULT_CHARACTER);
   EAC_RoutineStats.RecordHousehold(home.Residents.Count());
  }
 }

 // ---------------------------------------------------------------------------
 // Mission-start prewarm (prewarm design section 1).
 //
 // Everything below is inert at mode 0: StepPrewarm returns immediately, no cell
 // is queued and no field is read. Player-proximity discovery is untouched and
 // remains the only mechanism for un-labelled geography, so a terrain with no map
 // descriptors behaves exactly as it does today.
 // ---------------------------------------------------------------------------

 bool IsPrewarmComplete() { return m_PrewarmComplete; }
 int GetPrewarmMode() { return m_PrewarmMode; }
 int GetPrewarmBudget() { return m_PrewarmBudget; }
 int GetPrewarmQueued() { return m_PrewarmQueued; }
 int GetPrewarmDone() { return m_PrewarmDone; }
 int GetPrewarmDuplicates() { return m_PrewarmDuplicate; }
 int GetPrewarmSettlementsDone() { return m_PrewarmSettlementsDone; }
 int GetPrewarmQueueCount() { return m_Prewarm.Count(); }
 int GetPrewarmSettlementCount() { return PrewarmCentreCount(); }

 float GetPrewarmSeconds()
 {
  if (m_PrewarmStartedAt <= 0) return 0;
  if (m_PrewarmFinishedAt > m_PrewarmStartedAt) return m_PrewarmFinishedAt - m_PrewarmStartedAt;
  if (!GetGame() || !GetGame().GetWorld()) return 0;
  return GetGame().GetWorld().GetWorldTime() * 0.001 - m_PrewarmStartedAt;
 }

 // How many 64 m cells one ring covers: side = 2r + 1, cells = side squared.
 static int PrewarmBlockSize(int ring)
 {
  int side = Math.Clamp(ring, 0, 32) * 2 + 1;
  return side * side;
 }

 // A separate setter, not extra parameters on DiscoverCellAt/DiscoverNearPlayer,
 // because the Households fixture calls those signatures directly. Prewarm is
 // additive: turning it off drops the queue and un-indexes nothing.
 void SetPrewarmPolicy(int mode, int cellsPerTick, int radiusVillage = 5, int radiusTown = 6, int radiusCity = 7)
 {
  int wanted = Math.Clamp(mode, 0, 2);
  m_PrewarmBudget = Math.Clamp(cellsPerTick, 1, 32);
  m_PrewarmRadiusVillage = Math.Clamp(radiusVillage, 1, PREWARM_MAX_RING);
  m_PrewarmRadiusTown = Math.Clamp(radiusTown, 1, PREWARM_MAX_RING);
  m_PrewarmRadiusCity = Math.Clamp(radiusCity, 1, PREWARM_MAX_RING);
  if (wanted == m_PrewarmMode) return;
  if (wanted == 0)
  {
   m_PrewarmMode = 0;
   m_Prewarm.Clear();
   return;
  }
  // A transition 0 -> non-zero re-arms from settlement 0. A transition between 1
  // and 2 keeps the cursor, because mode 2 only widens the scene survey's reach
  // and changes nothing about which cells are indexed.
  if (m_PrewarmMode == 0)
  {
   m_PrewarmSettlement = 0;
   m_PrewarmComplete = false;
   m_PrewarmFinishedAt = 0;
   m_Prewarm.Clear();
  }
  m_PrewarmMode = wanted;
 }

 // Test seat. The runtime never calls this; the fixture needs a deterministic
 // centre list to prove the empty-list no-op and the per-call budget on a terrain
 // that really carries 34 labels. Passing null restores the real index.
 void EAC_QASetPrewarmCentres(array<vector> centres, array<int> types)
 {
  m_PrewarmCentres = null;
  m_PrewarmTypes = null;
  if (centres)
  {
   array<vector> keptCentres = {};
   foreach (vector centre : centres) { keptCentres.Insert(centre); }
   m_PrewarmCentres = keptCentres;
   array<int> keptTypes = {};
   if (types)
   {
    foreach (int type : types) { keptTypes.Insert(type); }
   }
   m_PrewarmTypes = keptTypes;
  }
  m_Prewarm.Clear();
  m_PrewarmSettlement = 0;
  m_PrewarmComplete = false;
  m_PrewarmFinishedAt = 0;
 }

 protected int PrewarmCentreCount()
 {
  if (m_PrewarmCentres) return m_PrewarmCentres.Count();
  return EAC_SettlementIndex.GetCount();
 }

 protected vector PrewarmCentreAt(int index)
 {
  if (m_PrewarmCentres)
  {
   if (index < 0 || index >= m_PrewarmCentres.Count()) return vector.Zero;
   return m_PrewarmCentres[index];
  }
  return EAC_SettlementIndex.GetCentreAt(index);
 }

 protected int PrewarmTypeAt(int index)
 {
  if (m_PrewarmCentres)
  {
   if (!m_PrewarmTypes || index < 0 || index >= m_PrewarmTypes.Count()) return -1;
   return m_PrewarmTypes[index];
  }
  return EAC_SettlementIndex.GetTypeAt(index);
 }

 // Size the block by what the terrain author called the place. An unknown or
 // missing descriptor gets the village ring, which is the measured one.
 protected int PrewarmRing(int descriptorType)
 {
  if (descriptorType == EMapDescriptorType.MDT_NAME_CITY) return m_PrewarmRadiusCity;
  if (descriptorType == EMapDescriptorType.MDT_NAME_TOWN) return m_PrewarmRadiusTown;
  return m_PrewarmRadiusVillage;
 }

 // One settlement at a time, so the queue bound is one block and not the map.
 // Cells are generated centre-out through the existing CellOffset ring walk, so
 // the settlement's own centre indexes first.
 protected bool ExpandNextSettlement()
 {
  if (!m_Prewarm.IsEmpty()) return true;
  int count = PrewarmCentreCount();
  if (m_PrewarmSettlement >= count)
  {
   if (!m_PrewarmComplete)
   {
    m_PrewarmComplete = true;
    if (GetGame() && GetGame().GetWorld()) m_PrewarmFinishedAt = GetGame().GetWorld().GetWorldTime() * 0.001;
   }
   return false;
  }
  int index = m_PrewarmSettlement++;
  vector centre = PrewarmCentreAt(index);
  int ring = PrewarmRing(PrewarmTypeAt(index));
  int total = PrewarmBlockSize(ring);
  int baseX = Math.Floor(centre[0] / CELL_SIZE);
  int baseZ = Math.Floor(centre[2] / CELL_SIZE);
  for (int i = 0; i < total; i++)
  {
   vector delta = CellOffset(i);
   EAC_HomeCell cell = new EAC_HomeCell();
   cell.Origin = Vector((baseX + delta[0]) * CELL_SIZE, 0, (baseZ + delta[2]) * CELL_SIZE);
   cell.Size = CELL_SIZE;
   m_Prewarm.Insert(cell);
  }
  m_PrewarmSettlementsDone++;
  m_PrewarmQueued += total;
  return true;
 }

 // The whole prewarm work unit. Bounded, governor-gated by the caller, and never
 // a per-frame scan: called at most once per 0.5 s scheduler tick, at most
 // `budget` cells per call. It reuses QueryCell and nothing else, and it enrols
 // records only - it never spawns anything.
 int StepPrewarm(int budget, int small, int large)
 {
  if (!Replication.IsServer() || !GetGame() || !EAC_AutoExclusions.IsReady() || m_PrewarmMode <= 0 || m_PrewarmComplete) return 0;
  if (small < 0 || small > 20 || large < 0 || large > 20) return 0;
  m_Small = small; m_Large = large;
  if (m_PrewarmStartedAt <= 0 && GetGame().GetWorld()) m_PrewarmStartedAt = GetGame().GetWorld().GetWorldTime() * 0.001;
  int done = 0;
  int units = Math.Clamp(budget, 1, 32);
  for (int unit = 0; unit < units; unit++)
  {
   // The subdivision frontier keeps absolute priority, exactly as DiscoverCellAt
   // does, so a dense cell never strands its children behind a whole settlement.
   if (!m_Pending.IsEmpty())
   {
    EAC_HomeCell pending = m_Pending[m_Pending.Count() - 1];
    m_Pending.Remove(m_Pending.Count() - 1);
    QueryCell(pending.Origin, pending.Size);
    done++;
    continue;
   }
   if (!ExpandNextSettlement()) return done;
   // RemoveOrdered, not Remove: Enfusion's array.Remove fills the hole with the
   // last element, which would shuffle the centre-out block after the first pop.
   // Every cell would still be visited exactly once, but the settlement's own
   // centre would stop indexing first, which is the whole ordering guarantee.
   // The O(n) shift is on a queue that peaks at 225 entries.
   EAC_HomeCell cell = m_Prewarm[0];
   m_Prewarm.RemoveOrdered(0);
   int x = Math.Floor(cell.Origin[0] / CELL_SIZE);
   int z = Math.Floor(cell.Origin[2] / CELL_SIZE);
   int key = CellKey(x, z);
   // Overlapping settlement blocks are expected: PrepareTowns only dedupes
   // centres closer than 100 m. Counting attempted pops rather than successful
   // queries is what keeps a duplicate-heavy region draining at a bounded rate.
   if (m_Cells.Contains(key)) { m_PrewarmDuplicate++; continue; }
   if (m_Cells.Count() >= MAX_CELLS) { m_PrewarmComplete = true; return done; }
   m_Cells.Insert(key, true);
   QueryCell(Vector(x * CELL_SIZE, -1000, z * CELL_SIZE), CELL_SIZE);
   m_PrewarmDone++; done++;
  }
  return done;
 }

 // Known records only; no additional spatial query for diagnostics.
 void AppendDebugDrawData(array<vector> positions, array<int> kinds)
 {
  int count = m_Registry.GetHomeCount();
  for (int i = 0; i < Math.Min(EAC_Diagnostics.MAX_DRAW_POINTS, count); i++)
  {
   if (positions.Count() >= EAC_Diagnostics.MAX_DRAW_POINTS) return;
   EAC_HouseholdRecord home = m_Registry.GetHome((m_ReconcileCursor + i) % count);
   if (home.BuildingEntity && !EAC_AmbientModule.IsOutsidePopulationArea(home.BuildingEntity.GetOrigin())) EAC_Diagnostics.AddDrawPoint(positions, kinds, home.BuildingEntity.GetOrigin(), EAC_Diagnostics.HOME);
  }
 }
}
