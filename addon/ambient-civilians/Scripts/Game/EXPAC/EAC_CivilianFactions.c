// Which faction the residents are drawn from (developer request 2026-09-19: "when we
// run ME Civs it should use them exclusively").
//
// Index 0 is always the shipped vanilla themes. Every further entry is a faction
// found in THIS mission's faction manager that is not military
// (SCR_Faction.IsMilitary), is not vanilla CIV and carries at least one character in
// its entity catalog - so a civilian faction from any loaded mod appears on its own
// and nothing is hard-coded to a particular mod. Keys are sorted, so server and
// clients build the same list in the same order.
//
// Only the APPEARANCE comes from the chosen faction. A spawned resident is still
// affiliated to vanilla CIV, as before: that faction is guaranteed to exist, is
// neutral, and is what every ownership, danger and caching rule here was tested on.
class EAC_CivilianFactions
{
 static const int MAX_FACTIONS = 16;
 protected static ref array<string> s_Keys = {};
 protected static ref array<string> s_Names = {};
 protected static BaseWorld s_World;

 protected static void Build()
 {
  BaseWorld world;
  if (GetGame()) world = GetGame().GetWorld();
  if (world && world == s_World && !s_Keys.IsEmpty()) return;
  s_World = null;
  s_Keys.Clear(); s_Names.Clear();
  s_Keys.Insert(EAC_AmbientModule.CIV_FACTION); s_Names.Insert("Vanilla civilians");
  if (!GetGame() || !world) return;
  FactionManager manager = GetGame().GetFactionManager();
  if (!manager) return;
  array<Faction> factions = {};
  manager.GetFactionsList(factions);
  // An editor request before mission factions exist must not freeze an empty list.
  if (factions.IsEmpty()) return;
  s_World = world;
  array<string> found = {};
  foreach (Faction faction : factions)
  {
   SCR_Faction scripted = SCR_Faction.Cast(faction);
   if (!scripted || scripted.IsMilitary()) continue;
   string key = scripted.GetFactionKey();
   if (key.IsEmpty() || key == EAC_AmbientModule.CIV_FACTION || found.Contains(key)) continue;
   if (Characters(key, null) < 1) continue;
   found.Insert(key);
  }
  found.Sort();
  foreach (string sortedKey : found)
  {
   if (s_Keys.Count() >= MAX_FACTIONS) break;
   s_Keys.Insert(sortedKey);
   string label = sortedKey;
   Faction named = manager.GetFactionByKey(sortedKey);
   if (named && named.GetUIInfo() && !named.GetUIInfo().GetName().IsEmpty()) label = named.GetUIInfo().GetName();
   s_Names.Insert(label);
  }
 }

 static int Count() { Build(); return s_Keys.Count(); }
 static string KeyAt(int index)
 {
  Build();
  if (index < 0 || index >= s_Keys.Count()) return EAC_AmbientModule.CIV_FACTION;
  return s_Keys[index];
 }
 static string NameAt(int index)
 {
  Build();
  if (index < 0 || index >= s_Names.Count()) return "Vanilla civilians";
  return s_Names[index];
 }
 static string Describe()
 {
  Build();
  string text = "civilian_factions=" + s_Keys.Count().ToString();
  for (int i = 0; i < s_Keys.Count(); i++) text += " " + i.ToString() + ":" + s_Keys[i];
  return text;
 }

 // Character prefabs of one faction's entity catalog; returns how many.
 static int Characters(string factionKey, array<ResourceName> outPrefabs)
 {
  FactionManager manager = GetGame().GetFactionManager();
  if (!manager) return 0;
  SCR_Faction faction = SCR_Faction.Cast(manager.GetFactionByKey(factionKey));
  if (!faction) return 0;
  SCR_EntityCatalog catalog = faction.GetFactionEntityCatalogOfType(EEntityCatalogType.CHARACTER, false);
  if (!catalog) return 0;
  array<SCR_EntityCatalogEntry> entries = {};
  catalog.GetEntityList(entries);
  int count;
  foreach (SCR_EntityCatalogEntry entry : entries)
  {
   if (!entry || entry.GetPrefab().IsEmpty()) continue;
   count++;
   if (outPrefabs && outPrefabs.Count() < 64) outPrefabs.Insert(entry.GetPrefab());
  }
  return count;
 }

 // A theme made of the chosen faction's characters, equal weights, with the cars of
 // the shipped theme it replaces. Null when the faction offers nobody.
 static EAC_ThemeDefinition BuildTheme(int index, EAC_ThemeDefinition vehiclesFrom)
 {
  string key = KeyAt(index);
  array<ResourceName> prefabs = {};
  if (Characters(key, prefabs) < 1) return null;
  EAC_ThemeDefinition theme = new EAC_ThemeDefinition();
  theme.Name = "Faction " + key;
  foreach (ResourceName prefab : prefabs)
  {
   EAC_ThemeAsset asset = new EAC_ThemeAsset();
   asset.Prefab = prefab; asset.Weight = 1; asset.SourceFaction = key;
   theme.Characters.Insert(asset);
  }
  if (vehiclesFrom) theme.Vehicles = vehiclesFrom.Vehicles;
  return theme;
 }
}
