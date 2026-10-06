// Native 1.8 GM saves keep the zone and its seven settings. The crowd itself is
// transient (EAU_Director.KeepOutOfSaves), and so is a sleeping zone's memory of
// it: a loaded zone that is On gathers a fresh crowd once a player is near, so a
// restore can never duplicate protesters.
class EAU_ProtestZoneSerializer : GenericEntitySerializer
{
 // 2 added the Crowd sound setting, 3 the Wake distance. Version 1 saves hold five
 // settings and load with Alternate, version 1 and 2 saves load with the 300 m
 // wake distance (EAU_ProtestZone.RestoreSettings).
 static const int VERSION = 3;

 override static typename GetTargetType() { return EAU_ProtestZone; }
 override static EEntityDeserializeEvent GetDeserializeEvent() { return EEntityDeserializeEvent.AFTER_FINALIZE; }
 override static EDeserializeFailHandling GetDeserializeFailHandling() { return EDeserializeFailHandling.ERROR; }

 override protected ESerializeResult Serialize(notnull IEntity entity, notnull SaveContext context)
 {
  EAU_ProtestZone zone = EAU_ProtestZone.Cast(entity);
  if (!zone) return ESerializeResult.ERROR;
  array<int> settings = {};
  for (int key = 0; key < EAU_ProtestZone.SETTING_COUNT; key++)
  {
   int value = zone.GetSetting(key);
   if (!EAU_ProtestZone.ValidSetting(key, value)) return ESerializeResult.ERROR;
   settings.Insert(value);
  }
  if (!context.StartObject("base")) return ESerializeResult.ERROR;
  ESerializeResult result = super.Serialize(entity, context);
  bool ended = context.EndObject();
  if (!ended || result == ESerializeResult.ERROR || !context.WriteValue("eauVersion", VERSION) || !context.WriteValue("settings", settings)) return ESerializeResult.ERROR;
  return ESerializeResult.OK;
 }

 override protected bool Deserialize(notnull IEntity entity, notnull LoadContext context)
 {
  EAU_ProtestZone zone = EAU_ProtestZone.Cast(entity);
  int version;
  array<int> settings = {};
  if (!zone || !context.ReadValue("eauVersion", version) || !context.ReadValue("settings", settings)) return false;
  int expected = EAU_ProtestZone.SETTING_COUNT;
  if (version == 1) expected = EAU_ProtestZone.LEGACY_SETTING_COUNT;
  else if (version == 2) expected = EAU_ProtestZone.SOUND_SETTING_COUNT;
  else if (version != VERSION) return false;
  if (settings.Count() != expected) return false;
  // Native saves omit an empty base object when the prefab supplies its defaults.
  if (context.DoesObjectExist("base"))
  {
   if (!context.StartObject("base")) return false;
   bool restored = super.Deserialize(entity, context);
   bool ended = context.EndObject();
   if (!restored || !ended) return false;
  }
  return zone.RestoreSettings(settings);
 }
}
