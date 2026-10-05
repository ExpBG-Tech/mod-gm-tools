// Native base restores the prefab/transform; only operator configuration is custom data.
class EAS_AmbientModuleSerializer : GenericEntitySerializer
{
 override static typename GetTargetType() { return EAS_AmbientModule; }
 override static EEntityDeserializeEvent GetDeserializeEvent() { return EEntityDeserializeEvent.AFTER_FINALIZE; }
 override static EDeserializeFailHandling GetDeserializeFailHandling() { return EDeserializeFailHandling.ERROR; }

 override protected ESerializeResult Serialize(notnull IEntity entity, notnull SaveContext context)
 {
  EAS_AmbientModule module = EAS_AmbientModule.Cast(entity);
  if (!module) return ESerializeResult.ERROR;
  module.TraceSettings("native-save");
  array<int> settings = {};
  for (int key = 0; key < 23; key++)
  {
   int value = module.Value(key);
   if (!EAS_EditorAttribute.ValidValue(key, value)) return ESerializeResult.ERROR;
   settings.Insert(value);
  }
  if (!context.StartObject("base")) return ESerializeResult.ERROR;
  ESerializeResult result = super.Serialize(entity, context);
  bool ended = context.EndObject();
  if (!ended || result == ESerializeResult.ERROR || !context.WriteValue("easVersion", 4) || !context.WriteValue("settings", settings)) return ESerializeResult.ERROR;
  return ESerializeResult.OK;
 }

 override protected bool Deserialize(notnull IEntity entity, notnull LoadContext context)
 {
  EAS_AmbientModule module = EAS_AmbientModule.Cast(entity);
  int version;
  array<int> settings = {};
  if (!module || !context.ReadValue("easVersion", version) || !context.ReadValue("settings", settings)) return false;
  if ((version != 1 && version != 2 && version != 3 && version != 4) || (version == 1 && settings.Count() != 8) || (version == 2 && settings.Count() != 15) || (version == 3 && settings.Count() != 16) || (version == 4 && settings.Count() != 23)) return false;
  settings = EAS_ContentSettings.Normalize(settings);
  if (!settings) return false;
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

