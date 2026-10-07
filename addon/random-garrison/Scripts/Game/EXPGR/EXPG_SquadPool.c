// Squad sources of the Random Garrison module. One validated catalog per faction and
// world: the faction's GROUP entity catalog, entries placeable in the Game Master's
// Edit mode, each checked by the shared squad validator (EXPG_SquadPrefab), at least
// one prefab per tick, de-duplicated by lower-cased path and sorted by path. A
// mission maker's explicit prefab list (the module's Squad prefabs attribute) is a
// catalog of its own. Server only.
class EXPG_SquadEntry
{
 ResourceName Prefab;
 int Members;
 int Bucket;
 // Medical, logistics or essential squads (skipped while Exclude support squads is on).
 bool Support;
 // The squad's own faction (empty: any); explicit lists may mix factions.
 string FactionId;
}

class EXPG_SquadCatalog
{
 string FactionId;
 bool Explicit;
 bool Ready;
 // Why there is nothing to pick from (no faction, no catalog), shown in the status.
 string Problem;
 int Rejected;
 ref array<ref EXPG_SquadEntry> Entries = {};
 protected ref array<string> m_Paths = {};
 protected ref map<string, ResourceName> m_Prefabs = new map<string, ResourceName>();
 protected ref map<string, bool> m_Support = new map<string, bool>();
 protected int m_Cursor;

 int Total()
 {
  return m_Paths.Count();
 }

 int Checked()
 {
  return m_Cursor;
 }

 // The faction's GROUP catalog: enabled entries valid in Edit mode.
 void BeginFaction(string factionId)
 {
  FactionId = factionId;
  Explicit = false;
  Ready = false;
  Problem = "";
  FactionManager factions;
  if (GetGame()) { factions = GetGame().GetFactionManager(); }
  SCR_Faction scripted;
  if (factions) { scripted = SCR_Faction.Cast(factions.GetFactionByKey(factionId)); }
  if (!scripted || !scripted.EXPG_CatalogReady())
  {
   Problem = string.Format("faction %1 is not in this mission", factionId);
   Ready = true;
   return;
  }
  SCR_EntityCatalog catalog = scripted.GetFactionEntityCatalogOfType(EEntityCatalogType.GROUP, false);
  if (!catalog)
  {
   Problem = string.Format("faction %1 has no squad catalog", factionId);
   Ready = true;
   return;
  }
  array<SCR_EntityCatalogEntry> list = {};
  catalog.GetEntityList(list);
  foreach (SCR_EntityCatalogEntry entry : list)
  {
   if (!entry || !entry.IsEnabled()) { continue; }
   SCR_EntityCatalogEditorData editorData = SCR_EntityCatalogEditorData.Cast(entry.GetEntityDataOfType(SCR_EntityCatalogEditorData));
   if (!editorData || !editorData.IsValidInEditorMode(EEditorMode.EDIT)) { continue; }
   bool support = entry.HasEditableEntityLabel(EEditableEntityLabel.TRAIT_MEDICAL) || entry.HasEditableEntityLabel(EEditableEntityLabel.TRAIT_LOGISTICS);
   if (entry.HasEditableEntityLabel(EEditableEntityLabel.TRAIT_ESSENTIAL) || entry.HasEditableEntityLabel(EEditableEntityLabel.GROUPTYPE_ESSENTIAL)) { support = true; }
   AddCandidate(entry.GetPrefab(), support);
  }
  m_Paths.Sort();
  if (m_Paths.IsEmpty())
  {
   Problem = string.Format("faction %1 has no squads placeable in Edit mode", factionId);
   Ready = true;
  }
 }

 // A mission maker's list replaces the faction catalogs; its squads keep their factions.
 void BeginExplicit(notnull array<ResourceName> prefabs)
 {
  FactionId = "";
  Explicit = true;
  Ready = false;
  Problem = "";
  foreach (ResourceName prefab : prefabs) { AddCandidate(prefab, false); }
  m_Paths.Sort();
  if (m_Paths.IsEmpty())
  {
   Problem = "the module's squad prefab list is empty";
   Ready = true;
  }
 }

 protected void AddCandidate(ResourceName prefab, bool support)
 {
  if (prefab.IsEmpty()) { return; }
  string path = prefab;
  path.ToLower();
  if (m_Prefabs.Contains(path))
  {
   if (support) { m_Support.Set(path, true); }
   return;
  }
  m_Prefabs.Insert(path, prefab);
  m_Support.Insert(path, support);
  m_Paths.Insert(path);
 }

 // Validates candidates until the tick deadline, at least one per call. True when ready.
 bool Step(int deadline)
 {
  if (Ready)
  {
   return true;
  }
  int checkedNow;
  while (m_Cursor < m_Paths.Count() && (checkedNow == 0 || System.GetTickCount() < deadline))
  {
   string path = m_Paths[m_Cursor];
   m_Cursor++;
   checkedNow++;
   ResourceName prefab = m_Prefabs.Get(path);
   int members;
   string reason;
   if (!EXPG_SquadPrefab.Validate(prefab, members, reason, FactionId))
   {
    Rejected++;
    continue;
   }
   EXPG_SquadEntry entry = new EXPG_SquadEntry();
   entry.Prefab = prefab;
   entry.Members = members;
   entry.Bucket = EXPG_RGRules.Bucket(members);
   entry.Support = m_Support.Get(path);
   entry.FactionId = FactionId;
   if (Explicit) { entry.FactionId = SquadFaction(prefab); }
   Entries.Insert(entry);
  }
  if (m_Cursor < m_Paths.Count())
  {
   return false;
  }
  Ready = true;
  if (Entries.IsEmpty() && Problem.IsEmpty())
  {
   Problem = string.Format("none of the %1 squads passed the garrison checks", m_Paths.Count());
  }
  PrintFormat("[EXPG RANDOM] squad catalog %1: %2 of %3 squads usable", Describe(), Entries.Count(), m_Paths.Count());
  return true;
 }

 string Describe()
 {
  if (Explicit)
  {
   return "explicit list";
  }
  return "faction " + FactionId;
 }

 protected static string SquadFaction(ResourceName prefab)
 {
  Resource resource = Resource.Load(prefab);
  if (!resource || !resource.IsValid())
  {
   return string.Empty;
  }
  IEntitySource source = resource.GetResource().ToEntitySource();
  string squadFaction;
  if (source) { source.Get("m_faction", squadFaction); }
  return squadFaction;
 }

 protected bool Usable(EXPG_SquadEntry entry, int bucket, int budget, bool excludeSupport, string factionId)
 {
  if (entry.Bucket != bucket || entry.Members > budget)
  {
   return false;
  }
  if (excludeSupport && entry.Support)
  {
   return false;
  }
  return factionId.IsEmpty() || entry.FactionId.IsEmpty() || entry.FactionId == factionId;
 }

 // Squads of this bucket with at most budget soldiers, in path order.
 int Collect(int bucket, int budget, bool excludeSupport, string factionId, notnull array<EXPG_SquadEntry> outEntries)
 {
  outEntries.Clear();
  foreach (EXPG_SquadEntry entry : Entries)
  {
   if (Usable(entry, bucket, budget, excludeSupport, factionId)) { outEntries.Insert(entry); }
  }
  return outEntries.Count();
 }

 bool Fits(int bucket, int budget, bool excludeSupport, string factionId)
 {
  foreach (EXPG_SquadEntry entry : Entries)
  {
   if (Usable(entry, bucket, budget, excludeSupport, factionId))
   {
    return true;
   }
  }
  return false;
 }

 // Squads a generation can draw for this faction and size mask, whatever the budget
 // (the filters of Usable without the building's budget).
 int CountUsable(int sizes, bool excludeSupport, string factionId)
 {
  int usable;
  foreach (EXPG_SquadEntry entry : Entries)
  {
   if ((sizes & entry.Bucket) == 0 || (excludeSupport && entry.Support)) { continue; }
   if (factionId.IsEmpty() || entry.FactionId.IsEmpty() || entry.FactionId == factionId) { usable++; }
  }
  return usable;
 }
}

class EXPG_SquadPool
{
 static const int MAX_FACTIONS = 24;
 protected static ref map<string, ref EXPG_SquadCatalog> s_Catalogs;
 protected static BaseWorld s_World;

 protected static void CheckWorld()
 {
		EXPBG_LazyStatics_EXPG_SquadPool();
  if (!GetGame()) { return; }
  BaseWorld world = GetGame().GetWorld();
  if (world == s_World) { return; }
  s_World = world;
  s_Catalogs.Clear();
 }

 // The faction's catalog for this world, started on first use.
 static EXPG_SquadCatalog Get(string factionId)
 {
		EXPBG_LazyStatics_EXPG_SquadPool();
  CheckWorld();
  EXPG_SquadCatalog catalog;
  if (s_Catalogs.Find(factionId, catalog) && catalog)
  {
   return catalog;
  }
  catalog = new EXPG_SquadCatalog();
  catalog.BeginFaction(factionId);
  s_Catalogs.Set(factionId, catalog);
  return catalog;
 }

 // Military factions with a squad catalog, sorted by key, at most 24. Computed the same
 // way on the server and every client (the faction attributes exchange list rows).
 static int MilitaryFactions(notnull array<string> outKeys)
 {
  outKeys.Clear();
  if (!GetGame())
  {
   return 0;
  }
  FactionManager factions = GetGame().GetFactionManager();
  if (!factions)
  {
   return 0;
  }
  array<Faction> list = {};
  factions.GetFactionsList(list);
  foreach (Faction candidate : list)
  {
   SCR_Faction scripted = SCR_Faction.Cast(candidate);
   if (!scripted || !scripted.IsMilitary() || !scripted.EXPG_CatalogReady()) { continue; }
   if (!scripted.GetFactionEntityCatalogOfType(EEntityCatalogType.GROUP, false)) { continue; }
   string key = scripted.GetFactionKey();
   if (!key.IsEmpty() && !outKeys.Contains(key)) { outKeys.Insert(key); }
  }
  outKeys.Sort();
  while (outKeys.Count() > MAX_FACTIONS) { outKeys.Remove(outKeys.Count() - 1); }
  return outKeys.Count();
 }

 // The faction's display name for the attribute rows (the key when it has none).
 static string FactionName(string factionId)
 {
  if (!GetGame() || factionId.IsEmpty())
  {
   return factionId;
  }
  FactionManager factions = GetGame().GetFactionManager();
  if (!factions)
  {
   return factionId;
  }
  Faction found = factions.GetFactionByKey(factionId);
  if (!found)
  {
   return factionId;
  }
  string name = found.GetFactionName();
  if (name.IsEmpty())
  {
   return factionId;
  }
  return name;
 }

	//------------------------------------------------------------------------------------------------
	//! Creates the collections on first use (not in the global static initializer, which has a
	//! per-function instruction limit that large modsets exceed on Windows).
	protected static void EXPBG_LazyStatics_EXPG_SquadPool()
	{
		if (!s_Catalogs)
			s_Catalogs = new map<string, ref EXPG_SquadCatalog>();
	}
}

// The faction's entity catalogs are built in its Init; reading them earlier logs an error.
modded class SCR_Faction
{
 bool EXPG_CatalogReady()
 {
  return m_bCatalogInitDone;
 }
}
