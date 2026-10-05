[BaseContainerProps(), BaseContainerCustomTitleField("Prefab")]
class EAC_ThemeAsset
{
 [Attribute("", UIWidgets.ResourceNamePicker, "Character or wheeled vehicle prefab", "et")]
 ResourceName Prefab;
 [Attribute("1", UIWidgets.EditBox, "Relative integer selection weight", "1 1000 1")]
 int Weight;
 [Attribute("CIV", UIWidgets.EditBox, "Expected prefab faction; this does not set live civilian behavior")]
 string SourceFaction;
}

[BaseContainerProps(), BaseContainerCustomTitleField("Name")]
class EAC_ThemeDefinition
{
 [Attribute("", UIWidgets.EditBox, "Theme display name")]
 string Name;
 [Attribute("", UIWidgets.Object, "Weighted character candidates; maximum 64")]
 ref array<ref EAC_ThemeAsset> Characters;
 [Attribute("", UIWidgets.Object, "Weighted wheeled-vehicle candidates; maximum 64")]
 ref array<ref EAC_ThemeAsset> Vehicles;

 void EAC_ThemeDefinition()
 {
  if (!Characters) Characters = {};
  if (!Vehicles) Vehicles = {};
 }
}

[BaseContainerProps(configRoot: true)]
class EAC_ThemeCatalog
{
 [Attribute("", UIWidgets.Object, "Themes selected by zero-based index on the main module; maximum 32")]
 ref array<ref EAC_ThemeDefinition> Themes;

 void EAC_ThemeCatalog()
 {
  // Native deserialization precedes member initialization; preserve loaded arrays.
  if (!Themes) Themes = {};
 }

 static EAC_ThemeCatalog Load(ResourceName path)
 {
  return SCR_ConfigHelperT<EAC_ThemeCatalog>.GetConfigObject(path);
 }

 EAC_ThemeDefinition GetDefinition(int index)
 {
  if (!Themes || Themes.Count() > 32 || index < 0 || index >= Themes.Count()) return null;
  return Themes[index];
 }
}

// Small integer pool. Asset capability validation happens before Add in selection.
class EAC_WeightedPool
{
 protected ref array<ResourceName> m_Prefabs = {};
 protected ref array<int> m_Weights = {};
 protected ref array<string> m_Keys = {};
 protected int m_Total;

 bool Add(ResourceName prefab, int weight)
 {
  if (prefab == "" || weight < 1 || weight > 1000 || m_Prefabs.Count() >= 64 || Contains(prefab)) return false;
  m_Keys.Insert(IdentityKey(prefab));
  m_Prefabs.Insert(prefab); m_Weights.Insert(weight); m_Total += weight;
  return true;
 }

 ResourceName PickTicket(int ticket)
 {
  if (ticket < 0 || ticket >= m_Total) return "";
  foreach (int index, int weight : m_Weights)
  {
   if (ticket < weight) return m_Prefabs[index];
   ticket -= weight;
  }
  return "";
 }

 ResourceName Pick()
 {
  if (m_Total == 0) return "";
  return PickTicket(Math.RandomInt(0, m_Total));
 }
 int GetTotalWeight() { return m_Total; }
 int GetCount() { return m_Prefabs.Count(); }
 bool Contains(ResourceName prefab) { return m_Keys.Contains(IdentityKey(prefab)); }

 protected static string IdentityKey(ResourceName prefab)
 {
  // Native source names retain GUID/plain-path spelling; dedupe their path.
  string key = prefab.GetPath();
  key.Replace("\\", "/");
  key.ToLower();
  return key;
 }
}

// One candidate resource per Step; no entity creation or world scan.
class EAC_ThemeSelection
{
 protected ref EAC_ThemeDefinition m_Definition;
 protected ref EAC_WeightedPool m_Characters;
 protected ref EAC_WeightedPool m_Vehicles;
 protected int m_Checked, m_Rejected;
 protected bool m_Done;

 void Begin(EAC_ThemeDefinition definition)
 {
  m_Definition = definition;
  m_Characters = new EAC_WeightedPool(); m_Vehicles = new EAC_WeightedPool();
  m_Checked = 0; m_Rejected = 0; m_Done = false;
  if (!definition || !definition.Characters || !definition.Vehicles || definition.Characters.IsEmpty() || definition.Characters.Count() > 64 || definition.Vehicles.Count() > 64)
  {
   m_Done = true;
   Print("[EAC] Theme unavailable: undefined, empty character list or more than 64 entries per pool. New household allocation suspended.");
  }
 }

 void Step()
 {
  if (m_Done || !m_Definition) return;
  int characterCount = m_Definition.Characters.Count();
  bool vehicle = m_Checked >= characterCount;
  EAC_ThemeAsset asset;
  EAC_WeightedPool pool = m_Characters;
  if (vehicle) { asset = m_Definition.Vehicles[m_Checked - characterCount]; pool = m_Vehicles; }
  else asset = m_Definition.Characters[m_Checked];
  ResourceName canonical; string reason;
  bool accepted = ValidateAsset(asset, vehicle, canonical, reason);
  if (accepted && !pool.Add(canonical, asset.Weight)) { accepted = false; reason = "duplicate resolved prefab"; }
  if (!accepted)
  {
   m_Rejected++;
   ResourceName path;
   if (asset) path = asset.Prefab;
   PrintFormat("[EAC] Theme '%1' rejected candidate %2 (%3): %4", m_Definition.Name, m_Checked, path, reason);
  }
  m_Checked++;
  if (m_Checked == characterCount + m_Definition.Vehicles.Count())
  {
   m_Done = true;
   PrintFormat("[EAC] Theme '%1' prepared: characters=%2 vehicles=%3 rejected=%4 ready=%5", m_Definition.Name, m_Characters.GetCount(), m_Vehicles.GetCount(), m_Rejected, IsReady());
  }
 }

 static bool ValidateAsset(EAC_ThemeAsset asset, bool vehicle, out ResourceName canonical, out string reason)
 {
  canonical = ""; reason = "invalid prefab, faction or weight (1-1000 required)";
  if (!asset || asset.Prefab == "" || asset.SourceFaction == "" || asset.Weight < 1 || asset.Weight > 1000) return false;
  Resource resource = Resource.Load(asset.Prefab);
  reason = "missing or invalid resource";
  if (!resource || !resource.IsValid()) return false;
  IEntitySource source = SCR_BaseContainerTools.FindEntitySource(resource);
  reason = "resource is not an entity prefab";
  if (!source) return false;
  reason = "wrong entity kind or missing required native components";
  if (!SCR_BaseContainerTools.FindComponentSource(source, RplComponent)) return false;
  IEntityComponentSource affiliation;
  if (vehicle)
  {
   if (!source.GetClassName().ToType().IsInherited(Vehicle)) return false;
   if (!SCR_BaseContainerTools.FindComponentSource(source, VehicleWheeledSimulation) || !SCR_BaseContainerTools.FindComponentSource(source, ChimeraAIVehicleControlComponent) || !SCR_BaseContainerTools.FindComponentSource(source, SCR_BaseCompartmentManagerComponent) || !SCR_BaseContainerTools.FindComponentSource(source, ChimeraAIPathfindingComponent)) return false;
   affiliation = SCR_BaseContainerTools.FindComponentSource(source, SCR_VehicleFactionAffiliationComponent);
  }
  else
  {
   if (!source.GetClassName().ToType().IsInherited(SCR_ChimeraCharacter)) return false;
   if (!SCR_BaseContainerTools.FindComponentSource(source, SCR_CharacterControllerComponent) || !SCR_BaseContainerTools.FindComponentSource(source, ChimeraAIControlComponent) || !SCR_BaseContainerTools.FindComponentSource(source, SCR_CompartmentAccessComponent)) return false;
   affiliation = SCR_BaseContainerTools.FindComponentSource(source, SCR_CharacterFactionAffiliationComponent);
  }
  string faction;
  reason = "missing or unexpected source faction; declare it explicitly in the theme";
  if (!affiliation || !affiliation.Get("faction affiliation", faction) || faction != asset.SourceFaction) return false;
  canonical = source.GetResourceName();
  reason = "missing resolved prefab identity";
  if (canonical == "") return false;
  reason = "";
  return true;
 }

 bool IsDone() { return m_Done; }
 bool IsReady() { return m_Done && m_Characters && m_Characters.GetCount() > 0; }
 int GetCheckedCount() { return m_Checked; }
 int GetRejectedCount() { return m_Rejected; }
 EAC_WeightedPool GetCharacters() { return m_Characters; }
 EAC_WeightedPool GetVehicles() { return m_Vehicles; }
 string GetName()
 {
  if (!m_Definition) return "";
  return m_Definition.Name;
 }
}
