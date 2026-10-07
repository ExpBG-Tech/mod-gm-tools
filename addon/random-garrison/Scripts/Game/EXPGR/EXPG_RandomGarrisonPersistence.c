// Native 1.8 Game Master saves keep the zone: its settings (the whole batch or
// nothing), its two faction keys, its token, whether it generated, the last seed and
// the outcome of each building tried. The garrisons themselves are saved by the
// garrison ledger (EXPG_Snapshot.c) with their GeneratedBy token, so a loaded zone
// finds them again. A loaded zone never resumes by itself (Stopped or Idle).
// Attribute savers (CDF) use the attributes instead (EXPG_RandomGarrisonAttributes.c).
class EXPG_RandomGarrisonSerializer : GenericEntitySerializer
{
 static const int VERSION = 1;

 override static typename GetTargetType()
 {
  return EXPG_RandomGarrisonModule;
 }

 override static EEntityDeserializeEvent GetDeserializeEvent()
 {
  return EEntityDeserializeEvent.AFTER_FINALIZE;
 }

 override static EDeserializeFailHandling GetDeserializeFailHandling()
 {
  return EDeserializeFailHandling.IGNORE;
 }

 override protected ESerializeResult Serialize(notnull IEntity entity, notnull SaveContext context)
 {
  EXPG_RandomGarrisonModule zone = EXPG_RandomGarrisonModule.Cast(entity);
  if (!zone)
  {
   return ESerializeResult.ERROR;
  }
  array<int> settings = {};
  for (int key = 0; key < EXPG_RandomGarrisonModule.INT_SETTINGS; key++)
  {
   int value = zone.GetSetting(key);
   if (!EXPG_RandomGarrisonModule.ValidSetting(key, value))
   {
    return ESerializeResult.ERROR;
   }
   settings.Insert(value);
  }
  array<string> factions = {zone.GetFactionSetting(0), zone.GetFactionSetting(1)};
  array<int> token = {zone.TokenHi(), zone.TokenLo()};
  array<string> outcomes = {};
  zone.GetOutcomes(outcomes);
  bool generated = zone.IsGenerated();
  int lastSeed = zone.LastSeed();
  if (!context.StartObject("base"))
  {
   return ESerializeResult.ERROR;
  }
  ESerializeResult result = super.Serialize(entity, context);
  bool ended = context.EndObject();
  if (!ended || result == ESerializeResult.ERROR)
  {
   return ESerializeResult.ERROR;
  }
  if (!context.WriteValue("rgVersion", VERSION) || !context.WriteValue("settings", settings) || !context.WriteValue("factions", factions))
  {
   return ESerializeResult.ERROR;
  }
  if (!context.WriteValue("token", token) || !context.WriteValue("generated", generated) || !context.WriteValue("lastSeed", lastSeed) || !context.WriteValue("outcomes", outcomes))
  {
   return ESerializeResult.ERROR;
  }
  return ESerializeResult.OK;
 }

 override protected bool Deserialize(notnull IEntity entity, notnull LoadContext context)
 {
  EXPG_RandomGarrisonModule zone = EXPG_RandomGarrisonModule.Cast(entity);
  int version;
  if (!zone || !context.ReadValue("rgVersion", version) || version != VERSION)
  {
   return false;
  }
  array<int> settings = {};
  array<string> factions = {};
  array<int> token = {};
  array<string> outcomes = {};
  bool generated;
  int lastSeed;
  if (!context.ReadValue("settings", settings) || !context.ReadValue("factions", factions) || !context.ReadValue("token", token))
  {
   return false;
  }
  if (!context.ReadValue("generated", generated) || !context.ReadValue("lastSeed", lastSeed) || !context.ReadValue("outcomes", outcomes))
  {
   return false;
  }
  if (token.Count() != 2 || outcomes.Count() > EXPG_RandomGarrisonModule.MAX_OUTCOMES)
  {
   return false;
  }
  // Native saves omit an empty base object when the prefab supplies its defaults.
  if (context.DoesObjectExist("base"))
  {
   if (!context.StartObject("base"))
   {
    return false;
   }
   bool restored = super.Deserialize(entity, context);
   bool ended = context.EndObject();
   if (!restored || !ended)
   {
    return false;
   }
  }
  if (!zone.RestoreSettings(settings, factions))
  {
   return false;
  }
  return zone.RestoreSaved(token[0], token[1], generated, lastSeed, outcomes);
 }
}
