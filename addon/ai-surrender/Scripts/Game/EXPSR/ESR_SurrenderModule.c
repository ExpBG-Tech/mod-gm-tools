// EXPBG AI Surrender global module (Game Master Systems entity). Every copy mirrors the
// one global configuration in replicated properties, so Game Master dialogs read the
// same values on server and clients, including late joiners.
[EntityEditorProps(category: "EXPBG/AI Surrender", description: "Global AI surrender and interrogation settings")]
class ESR_SurrenderModuleClass : GenericEntityClass {}

class ESR_SurrenderModule : GenericEntity
{
 // Non-owning: entries become null when their module is deleted.
 protected static ref array<ESR_SurrenderModule> s_aModules = {};

 [Attribute("1", UIWidgets.CheckBox, "Enable surrender", category: "EXPBG AI Surrender"), RplProp()]
 protected bool m_bEnabled;
 [Attribute("30", UIWidgets.Slider, "Surrender chance (%)", "0 100 5", category: "EXPBG AI Surrender"), RplProp()]
 protected int m_iChance;
 [Attribute("50", UIWidgets.Slider, "Squad casualty threshold (%)", "10 100 5", category: "EXPBG AI Surrender"), RplProp()]
 protected int m_iThreshold;
 [Attribute("15", UIWidgets.Slider, "Random factor (+/- %)", "0 50 5", category: "EXPBG AI Surrender"), RplProp()]
 protected int m_iRandom;
 [Attribute("40", UIWidgets.Slider, "Interrogation: reveal squad (%)", "0 100 5", category: "EXPBG AI Surrender"), RplProp()]
 protected int m_iReveal;
 [Attribute("40", UIWidgets.Slider, "Interrogation: identity (%)", "0 100 5", category: "EXPBG AI Surrender"), RplProp()]
 protected int m_iIdentity;
 [Attribute("1000", UIWidgets.Slider, "Reveal search radius (m)", "100 3000 50", category: "EXPBG AI Surrender"), RplProp()]
 protected int m_iRadius;
 [Attribute("3", UIWidgets.Slider, "Interrogation attempts", "1 5 1", category: "EXPBG AI Surrender"), RplProp()]
 protected int m_iAttempts;
 [Attribute("0", UIWidgets.Slider, "Intel marker lifetime (min, 0 = until removed)", "0 120 5", category: "EXPBG AI Surrender"), RplProp()]
 protected int m_iLifetime;
 [Attribute("0", UIWidgets.CheckBox, "Diagnostics", category: "EXPBG AI Surrender"), RplProp()]
 protected bool m_bDiagnostics;
 [RplProp()]
 protected int m_iPrisoners;

 void ESR_SurrenderModule(IEntitySource src, IEntity parent) { SetEventMask(EntityEvent.INIT); }

 void ~ESR_SurrenderModule()
 {
  if (s_aModules) s_aModules.RemoveItem(this);
  ESR_SurrenderManager.Refresh();
 }

 override void EOnInit(IEntity owner)
 {
  super.EOnInit(owner);
  if (!GetGame().InPlayMode()) return;
  bool first = ActiveCount() == 0;
  if (!s_aModules.Contains(this)) s_aModules.Insert(this);
  if (!Replication.IsServer()) return;
  // The first module of a session brings its own values (prefab or World Editor);
  // later copies show the global configuration.
  if (first || !ESR_Settings.IsSet())
  {
   array<int> own = {};
   for (int key = 0; key < ESR_Settings.COUNT; key++) own.Insert(GetSetting(key));
   ESR_Settings.Adopt(own);
   ESR_SurrenderManager.OnFirstModule();
  }
  MirrorAll();
  ESR_SurrenderManager.Refresh();
  ESR_SurrenderManager.Trace(string.Format("module registered first=%1 modules=%2 enabled=%3", first, ActiveCount(), ESR_Settings.Get(ESR_Settings.ENABLED)));
 }

 static int ActiveCount()
 {
  for (int i = s_aModules.Count() - 1; i >= 0; i--)
  {
   if (!s_aModules[i]) s_aModules.Remove(i);
  }
  return s_aModules.Count();
 }

 int GetSetting(int key)
 {
  if (key == ESR_Settings.ENABLED) return ESR_Settings.FromBool(m_bEnabled);
  if (key == ESR_Settings.CHANCE) return m_iChance;
  if (key == ESR_Settings.THRESHOLD) return m_iThreshold;
  if (key == ESR_Settings.RANDOM) return m_iRandom;
  if (key == ESR_Settings.REVEAL) return m_iReveal;
  if (key == ESR_Settings.IDENTITY) return m_iIdentity;
  if (key == ESR_Settings.RADIUS) return m_iRadius;
  if (key == ESR_Settings.ATTEMPTS) return m_iAttempts;
  if (key == ESR_Settings.LIFETIME) return m_iLifetime;
  if (key == ESR_Settings.DIAGNOSTICS) return ESR_Settings.FromBool(m_bDiagnostics);
  return 0;
 }

 int GetPrisonerCount() { return m_iPrisoners; }

 // Server only: Game Master edit of one value. Every module mirrors the result.
 void ApplySetting(int key, float value)
 {
  if (!Replication.IsServer() || key < 0 || key >= ESR_Settings.COUNT) return;
  if (!ESR_Settings.Set(key, value)) return;
  MirrorAll();
  ESR_SurrenderManager.Refresh();
  ESR_SurrenderManager.Trace(string.Format("setting %1 = %2", key, ESR_Settings.Get(key)));
 }

 // Server only: complete set from a native or CDF mission load.
 bool RestoreSettings(notnull array<int> values)
 {
  if (!Replication.IsServer() || values.Count() != ESR_Settings.COUNT) return false;
  ESR_Settings.Adopt(values);
  MirrorAll();
  ESR_SurrenderManager.Refresh();
  ESR_SurrenderManager.Trace("settings restored from save");
  return true;
 }

 protected void Mirror()
 {
  bool enabled = ESR_Settings.Get(ESR_Settings.ENABLED) != 0;
  bool diagnostics = ESR_Settings.Get(ESR_Settings.DIAGNOSTICS) != 0;
  int chance = ESR_Settings.Get(ESR_Settings.CHANCE);
  int threshold = ESR_Settings.Get(ESR_Settings.THRESHOLD);
  int spread = ESR_Settings.Get(ESR_Settings.RANDOM);
  int reveal = ESR_Settings.Get(ESR_Settings.REVEAL);
  int identity = ESR_Settings.Get(ESR_Settings.IDENTITY);
  int radius = ESR_Settings.Get(ESR_Settings.RADIUS);
  int attempts = ESR_Settings.Get(ESR_Settings.ATTEMPTS);
  int lifetime = ESR_Settings.Get(ESR_Settings.LIFETIME);
  if (m_bEnabled == enabled && m_bDiagnostics == diagnostics && m_iChance == chance && m_iThreshold == threshold && m_iRandom == spread && m_iReveal == reveal && m_iIdentity == identity && m_iRadius == radius && m_iAttempts == attempts && m_iLifetime == lifetime) return;
  m_bEnabled = enabled; m_bDiagnostics = diagnostics; m_iChance = chance; m_iThreshold = threshold; m_iRandom = spread;
  m_iReveal = reveal; m_iIdentity = identity; m_iRadius = radius; m_iAttempts = attempts; m_iLifetime = lifetime;
  Replication.BumpMe();
 }

 static void MirrorAll()
 {
  if (!Replication.IsServer()) return;
  foreach (ESR_SurrenderModule module : s_aModules)
  {
   if (module) module.Mirror();
  }
 }

 protected void SetPrisonerCount(int count)
 {
  if (m_iPrisoners == count) return;
  m_iPrisoners = count;
  Replication.BumpMe();
 }

 static void PublishPrisoners(int count)
 {
  if (!Replication.IsServer()) return;
  foreach (ESR_SurrenderModule module : s_aModules)
  {
   if (module) module.SetPrisonerCount(count);
  }
 }
}

// Native mission saves (1.8 persistence): the base restores prefab and transform; only
// the ten operator settings are custom data.
class ESR_SurrenderModuleSerializer : GenericEntitySerializer
{
 override static typename GetTargetType() { return ESR_SurrenderModule; }
 override static EEntityDeserializeEvent GetDeserializeEvent() { return EEntityDeserializeEvent.AFTER_FINALIZE; }
 override static EDeserializeFailHandling GetDeserializeFailHandling() { return EDeserializeFailHandling.ERROR; }

 override protected ESerializeResult Serialize(notnull IEntity entity, notnull SaveContext context)
 {
  ESR_SurrenderModule module = ESR_SurrenderModule.Cast(entity);
  if (!module) return ESerializeResult.ERROR;
  array<int> settings = {};
  for (int key = 0; key < ESR_Settings.COUNT; key++) settings.Insert(module.GetSetting(key));
  if (!context.StartObject("base")) return ESerializeResult.ERROR;
  ESerializeResult result = super.Serialize(entity, context);
  bool ended = context.EndObject();
  if (!ended || result == ESerializeResult.ERROR || !context.WriteValue("esrVersion", 1) || !context.WriteValue("settings", settings)) return ESerializeResult.ERROR;
  return ESerializeResult.OK;
 }

 override protected bool Deserialize(notnull IEntity entity, notnull LoadContext context)
 {
  ESR_SurrenderModule module = ESR_SurrenderModule.Cast(entity);
  int version;
  array<int> settings = {};
  if (!module || !context.ReadValue("esrVersion", version) || !context.ReadValue("settings", settings)) return false;
  if (version != 1 || settings.Count() != ESR_Settings.COUNT) return false;
  // Native saves omit an empty base object when the prefab supplies its defaults.
  if (context.DoesObjectExist("base"))
  {
   if (!context.StartObject("base")) return false;
   bool restored = super.Deserialize(entity, context);
   bool ended = context.EndObject();
   if (!restored || !ended) return false;
  }
  return module.RestoreSettings(settings);
 }
}
