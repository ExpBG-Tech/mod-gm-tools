// Base-game-only format. Optional persistence adapters own file IO and transactions.
class EAD_ZoneSnapshot
{
 ref array<int> Settings = {};
 vector Origin;
 bool Generated;
 ref array<ref EAD_PropRecord> Records = {};
}
class EAD_Snapshot
{
 static bool Loading;
 static BaseWorld ImportWorld;
 static bool Finite(float value) { return value == value && value > -10000000 && value < 10000000; }
 static bool FiniteVector(vector value)
 {
  return Finite(value[0]) && Finite(value[1]) && Finite(value[2]);
 }
 // Zone settings 0-12; schema 3 added key 12 (vehicle types).
 static const int SETTING_COUNT = 13;
 static bool ValidSettings(array<int> values)
 {
  if (!values || values.Count() != SETTING_COUNT || values[11] < 0 || values[11] > 2 || values[12] < 0 || values[12] > 2) return false;
  if (values[0] < 25 || values[0] > 1000 || values[7] < 1 || values[7] > 1000000) return false;
  for (int i = 1; i <= 4; i++) { if (values[i] < 0 || values[i] > 100) return false; }
  return values[5] >= 100 && values[5] <= 3000 && values[6] > values[5] && values[6] <= 4000 && values[8] >= 0 && values[8] <= 1 && values[9] >= 0 && values[9] <= 2 && values[10] >= 0 && values[10] <= 1;
 }
 static bool ValidRecord(EAD_PropRecord record, vector origin, float radius)
 {
  if (!record || record.Asset < 2 || record.Asset >= EAD_Catalog.COUNT) return false;
  for (int axis = 0; axis < 4; axis++) { if (!FiniteVector(record.Transform[axis])) return false; }
  if (EAD_Policy.DistanceSq(origin, record.Transform[3]) > radius * radius) return false;
  // Orthogonal right-handed unit transform; imported geometry must never be scaled or mirrored.
  for (int unit = 0; unit < 3; unit++) { if (Math.AbsFloat(record.Transform[unit].LengthSq() - 1) > 0.002) return false; }
  if (Math.AbsFloat(vector.Dot(record.Transform[0], record.Transform[1])) > 0.002 || Math.AbsFloat(vector.Dot(record.Transform[0], record.Transform[2])) > 0.002 || Math.AbsFloat(vector.Dot(record.Transform[1], record.Transform[2])) > 0.002) return false;
  return vector.Dot(SCR_Math3D.Cross(record.Transform[0], record.Transform[1]), record.Transform[2]) > 0.998;
 }
 static bool WriteZone(EAD_Zone zone, out string payload)
 {
  if (!Replication.IsServer() || Loading || !zone || !zone.CanCapture()) return false;
  EAD_ZoneSnapshot data = new EAD_ZoneSnapshot();
  for (int key = 0; key < SETTING_COUNT; key++) data.Settings.Insert(zone.GetSetting(key));
  if (!ValidSettings(data.Settings)) return false;
  data.Origin = zone.GetOrigin(); data.Generated = zone.HasSavedLayout();
  if (data.Generated)
  {
   foreach (EAD_PropRecord record : zone.Records) data.Records.Insert(record);
  }
  payload = EncodeZone(data);
  zone.Diagnostics("save-snapshot");
  return !payload.IsEmpty();
 }
 static string EncodeZone(EAD_ZoneSnapshot data)
 {
  JsonSaveContext ctx = new JsonSaveContext();
  ctx.WriteValue("schema", 3); ctx.WriteValue("catalog", EAD_Catalog.COUNT);
  ctx.WriteValue("world", GetGame().GetWorldFile());
  ctx.WriteValue("settings", data.Settings); ctx.WriteValue("origin", data.Origin);
  int count = data.Records.Count();
  ctx.WriteValue("generated", data.Generated); ctx.WriteValue("count", count);
  for (int index = 0; index < count; index++)
  {
   EAD_PropRecord record = data.Records[index];
   ctx.StartObject("prop" + index.ToString());
   ctx.WriteValue("asset", record.Asset); ctx.WriteValue("suppressed", record.Suppressed);
   for (int axis = 0; axis < 4; axis++) ctx.WriteValue("t" + axis.ToString(), record.Transform[axis]);
   ctx.EndObject();
  }
  return ctx.SaveToString();
 }
 static EAD_ZoneSnapshot ReadZone(string payload, bool loadResources = true)
 {
  if (payload.IsEmpty() || payload.Length() > 131072) return null;
  JsonLoadContext ctx = new JsonLoadContext();
  int schema, catalog, count; string world;
  EAD_ZoneSnapshot data = new EAD_ZoneSnapshot();
  if (!ctx.LoadFromString(payload) || !ctx.ReadValue("schema", schema) || schema < 1 || schema > 3 || !ctx.ReadValue("catalog", catalog) || catalog != EAD_Catalog.COUNT) return null;
  if (!ctx.ReadValue("world", world) || world != GetGame().GetWorldFile() || !ctx.ReadValue("settings", data.Settings)) return null;
  if (schema == 1)
  {
   if (data.Settings.Count() != 11) return null;
   data.Settings.Insert(2); // Old layouts retain their records and mixed-body setting.
  }
  if (schema < 3)
  {
   if (data.Settings.Count() != 12) return null;
   data.Settings.Insert(EAD_Catalog.BOTH); // Saved before vehicle types: every wreck, as generated.
  }
  if (!ValidSettings(data.Settings)) return null;
  if (!ctx.ReadValue("origin", data.Origin) || !FiniteVector(data.Origin) || !ctx.ReadValue("generated", data.Generated) || !ctx.ReadValue("count", count) || count < 0 || count > 170 || (schema == 1 && count > 32)) return null;
  if (!data.Generated && (count != 0 || data.Settings[8] != 0)) return null;
  for (int index = 0; index < count; index++)
  {
   EAD_PropRecord record = new EAD_PropRecord();
   if (!ctx.StartObject("prop" + index.ToString()) || !ctx.ReadValue("asset", record.Asset) || !ctx.ReadValue("suppressed", record.Suppressed)) return null;
   for (int axis = 0; axis < 4; axis++)
   {
    vector value;
    if (!ctx.ReadValue("t" + axis.ToString(), value)) return null;
    record.Transform[axis] = value;
   }
   if (!ctx.EndObject() || !ValidRecord(record, data.Origin, data.Settings[0])) return null;
   foreach (EAD_PropRecord previous : data.Records)
   {
    if (vector.DistanceSq(previous.Transform[3], record.Transform[3]) < 0.0001) return null;
   }
   if (loadResources)
   {
    Resource prefab = Resource.Load(EAD_Catalog.Prefab(record.Asset));
    if (!prefab || !prefab.IsValid()) return null;
   }
   data.Records.Insert(record);
  }
  return data;
 }
}
