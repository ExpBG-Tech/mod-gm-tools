// Native base restores the prefab/transform; only operator configuration is custom data.
class EAS_RadioModuleSerializer : GenericEntitySerializer
{
 override static typename GetTargetType() { return EAS_RadioModule; }
 override static EEntityDeserializeEvent GetDeserializeEvent() { return EEntityDeserializeEvent.AFTER_FINALIZE; }
 override static EDeserializeFailHandling GetDeserializeFailHandling() { return EDeserializeFailHandling.ERROR; }

 override protected ESerializeResult Serialize(notnull IEntity entity, notnull SaveContext context)
 {
  EAS_RadioModule module = EAS_RadioModule.Cast(entity);
  if (!module) return ESerializeResult.ERROR;
  module.TraceSettings("native-save");
  array<int> settings = {};
  for (int key = 0; key < 7; key++)
  {
   int value = module.Value(key);
   if (!module.ValidSetting(key, value)) return ESerializeResult.ERROR;
   settings.Insert(value);
  }
  if (!context.StartObject("base")) return ESerializeResult.ERROR;
  ESerializeResult result = super.Serialize(entity, context);
  bool ended = context.EndObject();
  if (!ended || result == ESerializeResult.ERROR || !context.WriteValue("easRadioVersion", 4) || !context.WriteValue("settings", settings)) return ESerializeResult.ERROR;
  return ESerializeResult.OK;
 }

 override protected bool Deserialize(notnull IEntity entity, notnull LoadContext context)
 {
  EAS_RadioModule module = EAS_RadioModule.Cast(entity);
  int version;
  array<int> settings = {};
  if (!module || !context.ReadValue("easRadioVersion", version) || !context.ReadValue("settings", settings)) return false;
  if (version < 1 || version > 4) return false;
  int expected = version + 2;
  if (version == 3) expected = 6;
  if (version == 4) expected = 7;
  if (settings.Count() != expected || !module.NormalizeSettings(settings)) return false;
  // Native saves omit an empty base object when the prefab supplies its defaults.
  if (context.DoesObjectExist("base"))
  {
   if (!context.StartObject("base")) return false;
   bool restored = super.Deserialize(entity, context);
   bool ended = context.EndObject();
   if (!restored || !ended) return false;
  }
  bool result = module.RestoreSettings(settings);
  if (result) module.TraceSettings("native-loaded");
  return result;
 }
}
