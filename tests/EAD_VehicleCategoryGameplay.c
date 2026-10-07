// TEST ONLY. EXPBG Ambient Destruction (addon/ambient-destruction) native server fixture:
// the zone setting "Vehicle types" (Civilian / Military / Both) decides which wrecks spawn.
// pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -FixturePath tests/EAD_VehicleCategoryGameplay.c -TimeoutSeconds 420 -OrchestratorSlotGranted -ExpectResult '\[EXPG EAD VEHICLE RESULT\] checks=[1-9]\d* failures=0 military=([4-9]|[1-9]\d+) civilian=([4-9]|[1-9]\d+) both=([6-9]|[1-9]\d+) mixed=1 replay=1 legacy=1 reason=complete'
// The runner copies this file to EXPG_GarrisonGameplay.c, so the driver class name is fixed.
// Accept on exactly one passing result line, no script errors and "Game destroyed".
// A real EAD_Zone (production prefab, scheduler and generator) in Morton, GM_Eden, radius
// 150: Wrecks 100, Bodies 0, Destruction 0, one seed. The packed Edit.conf entry
// EAD_VehicleTypesAttribute (key 12; Civilian 0, Military 1, Both 2) switches the zone
// through the server-only system write that CDF restore uses: Military, Civilian, Both,
// then Military again. A Military or Civilian layout must hold only wrecks of that
// EAD_Catalog category (at least four); Both must hold at least six wrecks of both
// categories; the second Military layout must equal the first exactly (same seed and
// setting, same scene). Snapshots: schema 3 keeps the setting; a schema 2 payload (saved
// before the setting existed) reads back as Both with every record, and importing it
// keeps those records without regenerating. No players, no GM UI, no CDF.
class EXPG_GarrisonGameplayClass : GenericEntityClass {}
class EXPG_GarrisonGameplay : GenericEntity
{
 static const float FIXTURE_SECONDS = 270;
 static const float PHASE_SECONDS = 90;
 static const ResourceName ZONE_PREFAB = "{EAD1000000000010}PrefabsEditable/EXPAD/EAD_Zone.et";
 static const ResourceName ATTRIBUTE_LIST = "{F3D6C6D25642352C}Configs/Editor/AttributeLists/Edit.conf";
 static const vector ZONE_POINT = "4981.42 0 4026.65";
 static const int SEED = 472685;
 static const int MIN_SINGLE = 4;
 static const int MIN_BOTH = 6;
 int m_Phase;
 int m_Checks;
 int m_Failures;
 int m_Revision;
 int m_MilitaryCount;
 int m_CivilianCount;
 int m_BothCount;
 int m_Mixed;
 int m_Replay;
 int m_Legacy;
 bool m_LegacyRead;
 float m_Started;
 float m_PhaseAt;
 float m_PhaseStarted;
 bool m_Finished;
 EAD_Zone m_Zone;
 SCR_EditableEntityComponent m_Editable;
 ref EAD_Attribute m_Attribute;
 ref EAD_ZoneSnapshot m_LegacyData;
 ref array<ref EAD_PropRecord> m_Military = {};
 ref array<ref EAD_PropRecord> m_Layout = {};

 void EXPG_GarrisonGameplay(IEntitySource src, IEntity parent) { SetEventMask(EntityEvent.INIT | EntityEvent.FRAME); }
 float Now() { return GetGame().GetWorld().GetWorldTime() * 0.001; }

 override void EOnInit(IEntity owner)
 {
  if (!Replication.IsServer()) { ClearEventMask(EntityEvent.FRAME); return; }
  m_Started = Now();
  m_PhaseAt = m_Started + 5;
  m_PhaseStarted = m_PhaseAt;
  PrintFormat("[EXPG EAD VEHICLE BEGIN] deadline=%1 zone=%2 seed=%3", FIXTURE_SECONDS, ZONE_POINT, SEED);
 }

 bool Check(bool ok, string label)
 {
  m_Checks++;
  if (!ok) m_Failures++;
  PrintFormat("[EXPG EAD VEHICLE CHECK] pass=%1 %2", ok, label);
  return ok;
 }

 void Finish(string reason)
 {
  if (m_Finished) return;
  m_Finished = true;
  ClearEventMask(EntityEvent.FRAME);
  PrintFormat("[EXPG EAD VEHICLE RESULT] checks=%1 failures=%2 military=%3 civilian=%4 both=%5 mixed=%6 replay=%7 legacy=%8 reason=%9", m_Checks, m_Failures, m_MilitaryCount, m_CivilianCount, m_BothCount, m_Mixed, m_Replay, m_Legacy, reason);
  GetGame().RequestClose();
 }

 void Advance(int phase)
 {
  m_Phase = phase;
  m_PhaseAt = Now();
  m_PhaseStarted = m_PhaseAt;
 }

 IEntity SpawnAt(ResourceName prefab, vector position)
 {
  Resource resource = Resource.Load(prefab);
  if (!resource || !resource.IsValid()) return null;
  EntitySpawnParams params = new EntitySpawnParams();
  params.TransformMode = ETransformMode.WORLD;
  Math3D.MatrixIdentity4(params.Transform);
  params.Transform[3] = position;
  IEntity spawned = GetGame().SpawnEntityPrefab(resource, GetGame().GetWorld(), params);
  return spawned;
 }

 override void EOnFrame(IEntity owner, float timeSlice)
 {
  if (m_Finished) return;
  float now = Now();
  if (now - m_Started > FIXTURE_SECONDS) { Check(false, string.Format("phase %1 finished before the fixture deadline", m_Phase)); Finish("timeout"); return; }
  if (now < m_PhaseAt) return;
  if (m_Phase > 0 && now - m_PhaseStarted > PHASE_SECONDS) { Check(false, string.Format("phase %1 finished within %2 s", m_Phase, PHASE_SECONDS)); Finish("phase-timeout"); return; }
  if (m_Phase == 0) { Setup(); return; }
  if (m_Phase == 1) { MilitaryLayout(); return; }
  if (m_Phase == 2) { CivilianLayout(); return; }
  if (m_Phase == 3) { BothLayout(); return; }
  if (m_Phase == 4) { MilitaryAgain(); return; }
  if (m_Phase == 5) { LegacyImport(); return; }
 }

 bool LayoutReady()
 {
  if (!m_Zone) return false;
  return m_Zone.GetRevision() != m_Revision && m_Zone.IsReady();
 }

 void CopyRecords(array<ref EAD_PropRecord> into)
 {
  into.Clear();
  foreach (EAD_PropRecord source : m_Zone.Records)
  {
   EAD_PropRecord copy = new EAD_PropRecord();
   copy.Asset = source.Asset;
   copy.Suppressed = source.Suppressed;
   for (int axis = 0; axis < 4; axis++) copy.Transform[axis] = source.Transform[axis];
   into.Insert(copy);
  }
 }

 // Index of the first differing record, -1 when both layouts match within tolerance.
 int FirstDifference(array<ref EAD_PropRecord> a, array<ref EAD_PropRecord> b, float position, float rotation)
 {
  int shorter = a.Count();
  if (b.Count() < shorter) shorter = b.Count();
  if (a.Count() != b.Count()) return shorter;
  for (int index = 0; index < a.Count(); index++)
  {
   if (a[index].Asset != b[index].Asset) return index;
   for (int axis = 0; axis < 4; axis++)
   {
    float tolerance = rotation;
    if (axis == 3) tolerance = position;
    if (vector.Distance(a[index].Transform[axis], b[index].Transform[axis]) > tolerance) return index;
   }
  }
  return -1;
 }

 // Category census of a layout: civilian, military and anything else (bodies, uncategorised).
 void Census(string phase, array<ref EAD_PropRecord> records, out int civilian, out int military, out int other)
 {
  civilian = 0;
  military = 0;
  other = 0;
  for (int index = 0; index < records.Count(); index++)
  {
   EAD_PropRecord record = records[index];
   int category = EAD_Catalog.Category(record.Asset);
   if (category == EAD_Catalog.CIVILIAN) civilian++;
   else if (category == EAD_Catalog.MILITARY) military++;
   else other++;
   PrintFormat("[EXPG EAD VEHICLE WRECK] phase=%1 slot=%2 asset=%3 category=%4 prefab=%5", phase, index, record.Asset, category, EAD_Catalog.Prefab(record.Asset));
  }
  PrintFormat("[EXPG EAD VEHICLE LAYOUT] phase=%1 records=%2 civilian=%3 military=%4 other=%5 setting=%6", phase, records.Count(), civilian, military, other, m_Zone.GetSetting(12));
 }

 // The packed Edit.conf entry, instantiated from its container as CDF reads the list.
 bool LoadAttribute()
 {
  Resource attributeResource = Resource.Load(ATTRIBUTE_LIST);
  BaseResourceObject attributeObject;
  if (attributeResource && attributeResource.IsValid()) attributeObject = attributeResource.GetResource();
  BaseContainer attributeRoot;
  if (attributeObject) attributeRoot = attributeObject.ToBaseContainer();
  BaseContainerList attributeEntries;
  if (attributeRoot) attributeEntries = attributeRoot.GetObjectArray("m_aAttributes");
  int attributeCount = 0;
  if (attributeEntries) attributeCount = attributeEntries.Count();
  if (attributeCount > 4096) attributeCount = 4096;
  int found = 0;
  BaseContainer typesEntry;
  for (int attributeIndex = 0; attributeIndex < attributeCount; attributeIndex++)
  {
   BaseContainer attributeEntry = attributeEntries.Get(attributeIndex);
   if (!attributeEntry || attributeEntry.GetClassName() != "EAD_VehicleTypesAttribute") continue;
   found++;
   typesEntry = attributeEntry;
  }
  if (!Check(found == 1 && typesEntry != null, string.Format("the packed Edit.conf holds one Vehicle types attribute (%1)", found))) return false;
  int key = -1;
  typesEntry.Get("m_Key", key);
  Check(key == 12, string.Format("the Vehicle types attribute writes zone key 12 (%1)", key));
  string choices = string.Empty;
  BaseContainerList choiceEntries = typesEntry.GetObjectArray("m_aValues");
  int choiceCount = 0;
  if (choiceEntries) choiceCount = choiceEntries.Count();
  if (choiceCount > 16) choiceCount = 16;
  for (int choiceIndex = 0; choiceIndex < choiceCount; choiceIndex++)
  {
   BaseContainer choiceEntry = choiceEntries.Get(choiceIndex);
   if (!choiceEntry) continue;
   string choiceName = string.Empty;
   float choiceValue = 0;
   choiceEntry.Get("m_sEntryName", choiceName);
   choiceEntry.Get("m_fEntryFloatValue", choiceValue);
   int choiceCode = Math.Round(choiceValue);
   choices += string.Format("%1=%2;", choiceName, choiceCode);
  }
  Check(choices == "Civilian=0;Military=1;Both=2;", string.Format("Vehicle types choices are Civilian, Military, Both (%1)", choices));
  Managed created = BaseContainerTools.CreateInstanceFromContainer(typesEntry);
  m_Attribute = EAD_Attribute.Cast(created);
  bool instantiated = Check(m_Attribute != null, "the Vehicle types attribute instantiates as an EAD_Attribute");
  return instantiated;
 }

 // Server-only system write (no manager, player -1): the path CDF restore uses.
 bool Choose(int types, string label)
 {
  m_Attribute.WriteVariable(m_Editable, SCR_BaseEditorAttributeVar.CreateInt(types), null, -1);
  SCR_BaseEditorAttributeVar readBack = m_Attribute.ReadVariable(m_Editable, null);
  int shown = -1;
  if (readBack) shown = readBack.GetInt();
  bool applied = Check(m_Zone.GetSetting(12) == types && shown == types, string.Format("Vehicle types set to %1 through the attribute (zone %2, attribute reads %3)", label, m_Zone.GetSetting(12), shown));
  return applied;
 }

 int VehicleSetting(EAD_ZoneSnapshot data)
 {
  if (!data || data.Settings.Count() < 13) return -1;
  return data.Settings[12];
 }

 void Setup()
 {
  ChimeraAIWorld ai = ChimeraAIWorld.Cast(GetGame().GetAIWorld());
  bool network = ai != null && ai.GetRoadNetworkManager() != null;
  if (!Check(network, "GM_Eden AI world has a road network")) { Finish("roads"); return; }
  BaseRoad road;
  float distance = 1000;
  ai.GetRoadNetworkManager().GetClosestRoad(ZONE_POINT, road, distance, true);
  if (!Check(road != null && distance <= 90, string.Format("a road within 90 m of the zone centre (%1 m)", distance))) { Finish("roads"); return; }
  vector origin = ZONE_POINT;
  origin[1] = GetGame().GetWorld().GetSurfaceY(origin[0], origin[2]);
  m_Zone = EAD_Zone.Cast(SpawnAt(ZONE_PREFAB, origin));
  if (!Check(m_Zone != null, "production EAD_Zone prefab spawned")) { Finish("zone"); return; }
  Check(m_Zone.GetSetting(12) == EAD_Catalog.BOTH, string.Format("a new zone defaults to Both vehicle types (%1)", m_Zone.GetSetting(12)));
  m_Editable = SCR_EditableEntityComponent.Cast(m_Zone.FindComponent(SCR_EditableEntityComponent));
  if (!Check(m_Editable != null, "the zone has its editable entity component")) { Finish("editable"); return; }
  if (!LoadAttribute()) { Finish("attribute"); return; }
  m_Zone.SetSetting(12, 9);
  Check(m_Zone.GetSetting(12) == EAD_Catalog.BOTH, string.Format("vehicle types clamp high values to Both (%1)", m_Zone.GetSetting(12)));
  m_Zone.SetSetting(12, -4);
  Check(m_Zone.GetSetting(12) == EAD_Catalog.CIVILIAN, string.Format("vehicle types clamp low values to Civilian (%1)", m_Zone.GetSetting(12)));
  m_Zone.SetSetting(9, 1);
  m_Zone.SetSetting(0, 150);
  m_Zone.SetSetting(1, 0);
  m_Zone.SetSetting(3, 100);
  m_Zone.SetSetting(4, 0);
  m_Zone.SetSetting(7, SEED);
  if (!Choose(EAD_Catalog.MILITARY, "Military")) { Finish("attribute"); return; }
  m_Revision = m_Zone.GetRevision();
  m_Zone.SetSetting(8, 1);
  Advance(1);
 }

 void MilitaryLayout()
 {
  if (!LayoutReady()) return;
  CopyRecords(m_Military);
  int civilian, military, other;
  Census("military", m_Military, civilian, military, other);
  m_MilitaryCount = military;
  Check(civilian == 0 && other == 0, string.Format("Military zone holds military wrecks only (%1 civilian, %2 other of %3)", civilian, other, m_Military.Count()));
  Check(military >= MIN_SINGLE, string.Format("Military zone places at least %1 wrecks (%2)", MIN_SINGLE, military));
  RoundTrip();
  m_Revision = m_Zone.GetRevision();
  if (!Choose(EAD_Catalog.CIVILIAN, "Civilian")) { Finish("attribute"); return; }
  Advance(2);
 }

 void CivilianLayout()
 {
  if (!LayoutReady()) return;
  CopyRecords(m_Layout);
  int civilian, military, other;
  Census("civilian", m_Layout, civilian, military, other);
  m_CivilianCount = civilian;
  Check(military == 0 && other == 0, string.Format("Civilian zone holds civilian wrecks only (%1 military, %2 other of %3)", military, other, m_Layout.Count()));
  Check(civilian >= MIN_SINGLE, string.Format("Civilian zone places at least %1 wrecks (%2)", MIN_SINGLE, civilian));
  m_Revision = m_Zone.GetRevision();
  if (!Choose(EAD_Catalog.BOTH, "Both")) { Finish("attribute"); return; }
  Advance(3);
 }

 void BothLayout()
 {
  if (!LayoutReady()) return;
  CopyRecords(m_Layout);
  int civilian, military, other;
  Census("both", m_Layout, civilian, military, other);
  m_BothCount = civilian + military;
  if (civilian > 0 && military > 0) m_Mixed = 1;
  Check(other == 0, string.Format("Both zone (Bodies 0) holds categorised wrecks only (%1 other of %2)", other, m_Layout.Count()));
  Check(m_BothCount >= MIN_BOTH, string.Format("Both zone places at least %1 wrecks (%2)", MIN_BOTH, m_BothCount));
  Check(m_Mixed == 1, string.Format("Both zone mixes civilian and military wrecks (%1 civilian, %2 military)", civilian, military));
  m_Revision = m_Zone.GetRevision();
  if (!Choose(EAD_Catalog.MILITARY, "Military again")) { Finish("attribute"); return; }
  Advance(4);
 }

 void MilitaryAgain()
 {
  if (!LayoutReady()) return;
  CopyRecords(m_Layout);
  int difference = FirstDifference(m_Military, m_Layout, 0.001, 0.001);
  if (difference >= 0 && difference < m_Military.Count() && difference < m_Layout.Count())
   PrintFormat("[EXPG EAD VEHICLE REPLAY] index=%1 first=%2 asset=%3 again=%4 asset=%5", difference, m_Military[difference].Transform[3], m_Military[difference].Asset, m_Layout[difference].Transform[3], m_Layout[difference].Asset);
  if (difference < 0) m_Replay = 1;
  Check(difference < 0, string.Format("the same seed and Military setting regenerate the identical layout (%1 vs %2 records, first difference %3)", m_Layout.Count(), m_Military.Count(), difference));
  if (!m_LegacyData) { Finish("complete"); return; }
  // Cache every prop: a snapshot import needs an empty live list.
  m_Zone.SetSetting(8, 0);
  Advance(5);
 }

 // An old (schema 2) save restored the way the CDF bridge does: settings, then exact records.
 void LegacyImport()
 {
  if (!m_Zone.Live.IsEmpty()) return;
  EAD_Snapshot.Loading = true;
  bool imported = m_Zone.ImportSnapshot(m_LegacyData);
  EAD_Snapshot.Loading = false;
  Check(imported, "the schema 2 snapshot imports into the zone");
  int restored = m_Zone.GetSetting(12);
  Check(restored == EAD_Catalog.BOTH, string.Format("an imported schema 2 snapshot restores Both vehicle types (%1)", restored));
  int difference = FirstDifference(m_Military, m_Zone.Records, 0.05, 0.002);
  Check(difference < 0, string.Format("the import keeps every saved record (%1 vs %2 records, first difference %3)", m_Zone.Records.Count(), m_Military.Count(), difference));
  Check(m_Zone.IsReady() && m_Zone.HasSavedLayout(), "the imported layout is ready without regenerating");
  if (m_LegacyRead && imported && restored == EAD_Catalog.BOTH && difference < 0) m_Legacy = 1;
  Finish("complete");
 }

 // The schema 2 format as 0.1.9 wrote it: twelve settings, no vehicle types.
 string LegacyPayload(EAD_ZoneSnapshot data)
 {
  JsonSaveContext ctx = new JsonSaveContext();
  ctx.WriteValue("schema", 2);
  ctx.WriteValue("catalog", EAD_Catalog.COUNT);
  ctx.WriteValue("world", GetGame().GetWorldFile());
  array<int> settings = {};
  for (int key = 0; key < 12; key++) settings.Insert(data.Settings[key]);
  ctx.WriteValue("settings", settings);
  ctx.WriteValue("origin", data.Origin);
  int count = data.Records.Count();
  ctx.WriteValue("generated", data.Generated);
  ctx.WriteValue("count", count);
  for (int index = 0; index < count; index++)
  {
   EAD_PropRecord record = data.Records[index];
   ctx.StartObject("prop" + index.ToString());
   ctx.WriteValue("asset", record.Asset);
   ctx.WriteValue("suppressed", record.Suppressed);
   for (int axis = 0; axis < 4; axis++) ctx.WriteValue("t" + axis.ToString(), record.Transform[axis]);
   ctx.EndObject();
  }
  string legacy = ctx.SaveToString();
  return legacy;
 }

 // The base-game snapshot that saves and the CDF bridge store verbatim.
 void RoundTrip()
 {
  string payload;
  bool written = EAD_Snapshot.WriteZone(m_Zone, payload);
  if (!Check(written, "zone snapshot written from the Military layout")) return;
  JsonLoadContext header = new JsonLoadContext();
  int schema = 0;
  bool parsed = header.LoadFromString(payload) && header.ReadValue("schema", schema);
  Check(parsed && schema == 3, string.Format("new snapshots use schema 3 (%1)", schema));
  EAD_ZoneSnapshot data = EAD_Snapshot.ReadZone(payload);
  if (!Check(data != null, "the schema 3 snapshot reads back")) return;
  Check(data.Settings.Count() == 13 && VehicleSetting(data) == EAD_Catalog.MILITARY, string.Format("the schema 3 snapshot keeps Military (%1 settings, vehicle types %2)", data.Settings.Count(), VehicleSetting(data)));
  int difference = FirstDifference(m_Military, data.Records, 0.05, 0.002);
  Check(difference < 0, string.Format("the schema 3 snapshot keeps every wreck (%1 vs %2 records, first difference %3)", data.Records.Count(), m_Military.Count(), difference));
  string legacy = LegacyPayload(data);
  EAD_ZoneSnapshot legacyData = EAD_Snapshot.ReadZone(legacy);
  if (!Check(legacyData != null, "a schema 2 snapshot (saved before vehicle types) reads back")) return;
  int legacyDifference = FirstDifference(m_Military, legacyData.Records, 0.05, 0.002);
  Check(legacyData.Settings.Count() == 13 && VehicleSetting(legacyData) == EAD_Catalog.BOTH, string.Format("a schema 2 snapshot defaults to Both (%1 settings, vehicle types %2)", legacyData.Settings.Count(), VehicleSetting(legacyData)));
  Check(legacyDifference < 0, string.Format("a schema 2 snapshot keeps every wreck (%1 vs %2 records, first difference %3)", legacyData.Records.Count(), m_Military.Count(), legacyDifference));
  m_LegacyRead = VehicleSetting(legacyData) == EAD_Catalog.BOTH && legacyDifference < 0;
  m_LegacyData = legacyData;
 }
}
