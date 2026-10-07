// EXPBG Weather Transition module (Game Master Systems entity). Each module holds its own
// target weather, rain, fog, wind and duration. Changing any of them in the attributes
// and pressing Save starts a transition there and then ("apply now"); the session runs
// one transition at a time (ETW_WeatherRunner), so starting another replaces it from
// the values reached so far. Every module shows the session's transition status.
[EntityEditorProps(category: "EXPBG/Time and Weather", description: "Weather Transition: blends to a target weather over real minutes")]
class ETW_WeatherModuleClass : GenericEntityClass {}

class ETW_WeatherModule : GenericEntity
{
 // Non-owning: entries become null when their module is deleted.
 protected static ref array<ETW_WeatherModule> s_aModules = {};

 [Attribute("", UIWidgets.EditBox, "Target weather state name (Clear, Cloudy, Overcast, Rainy on vanilla terrains); empty keeps the current clouds", category: "EXPBG Weather Transition")]
 protected string m_sTarget;
 [Attribute("10", UIWidgets.Slider, "Transition time (min)", "1 120 1", category: "EXPBG Weather Transition")]
 protected int m_iMinutes;
 [Attribute("0", UIWidgets.Slider, "Rain choice (0 = weather default)", "0 6 1", category: "EXPBG Weather Transition")]
 protected int m_iRain;
 [Attribute("0", UIWidgets.Slider, "Fog choice (0 = weather default)", "0 6 1", category: "EXPBG Weather Transition")]
 protected int m_iFog;
 [Attribute("0", UIWidgets.Slider, "Wind speed choice (0 = weather default)", "0 8 1", category: "EXPBG Weather Transition")]
 protected int m_iWindSpeed;
 [Attribute("0", UIWidgets.Slider, "Wind direction choice (0 = weather default)", "0 8 1", category: "EXPBG Weather Transition")]
 protected int m_iWindDirection;
 [Attribute("0", UIWidgets.Slider, "After the transition (0 = hold this weather, 1 = automatic weather)", "0 1 1", category: "EXPBG Weather Transition")]
 protected int m_iAfter;
 [Attribute("1", UIWidgets.CheckBox, "Smooth Scenario Properties weather changes", category: "EXPBG Weather Transition")]
 protected bool m_bSmooth;
 [RplProp()]
 protected string m_sStatus;
 [RplProp()]
 protected int m_iProgress;

 protected bool m_bQueued;
 protected bool m_bQueuedApply;
 protected int m_iQueuedAction;
 protected int m_iQueuedPlayer;

 //------------------------------------------------------------------------------------------------
 void ETW_WeatherModule(IEntitySource src, IEntity parent)
 {
  SetEventMask(EntityEvent.INIT);
 }

 //------------------------------------------------------------------------------------------------
 void ~ETW_WeatherModule()
 {
  if (s_aModules)
   s_aModules.RemoveItem(this);
  ArmaReforgerScripted game = GetGame();
  if (game && game.GetCallqueue())
   game.GetCallqueue().Remove(RunQueued);
 }

 //------------------------------------------------------------------------------------------------
 override void EOnInit(IEntity owner)
 {
  super.EOnInit(owner);
  if (!GetGame().InPlayMode())
   return;
  if (!s_aModules.Contains(this))
   s_aModules.Insert(this);
  if (!Replication.IsServer())
   return;
  for (int key = ETW_Settings.W_MINUTES; key < ETW_Settings.W_COUNT; key++)
  {
   StoreSetting(key, ETW_Settings.ClampWeather(key, GetSetting(key)));
  }
  SetStatus(ETW_WeatherRunner.GetStatus(), ETW_WeatherRunner.GetProgress());
 }

 //------------------------------------------------------------------------------------------------
 int GetSetting(int key)
 {
  if (key == ETW_Settings.W_MINUTES)
   return m_iMinutes;
  if (key == ETW_Settings.W_RAIN)
   return m_iRain;
  if (key == ETW_Settings.W_FOG)
   return m_iFog;
  if (key == ETW_Settings.W_WIND_SPEED)
   return m_iWindSpeed;
  if (key == ETW_Settings.W_WIND_DIR)
   return m_iWindDirection;
  if (key == ETW_Settings.W_AFTER)
   return m_iAfter;
  if (key == ETW_Settings.W_SMOOTH && m_bSmooth)
   return 1;
  return 0;
 }

 //------------------------------------------------------------------------------------------------
 protected void StoreSetting(int key, int value)
 {
  if (key == ETW_Settings.W_MINUTES)
   m_iMinutes = value;
  else if (key == ETW_Settings.W_RAIN)
   m_iRain = value;
  else if (key == ETW_Settings.W_FOG)
   m_iFog = value;
  else if (key == ETW_Settings.W_WIND_SPEED)
   m_iWindSpeed = value;
  else if (key == ETW_Settings.W_WIND_DIR)
   m_iWindDirection = value;
  else if (key == ETW_Settings.W_AFTER)
   m_iAfter = value;
  else if (key == ETW_Settings.W_SMOOTH)
   m_bSmooth = value != 0;
 }

 //------------------------------------------------------------------------------------------------
 // Server only: one Game Master edit, native load or test. Never starts a transition.
 void ApplySetting(int key, float value)
 {
  if (!Replication.IsServer() || key <= ETW_Settings.W_TARGET || key >= ETW_Settings.W_COUNT)
   return;
  StoreSetting(key, ETW_Settings.ClampWeather(key, value));
 }

 //------------------------------------------------------------------------------------------------
 string GetTargetName()
 {
  return m_sTarget;
 }

 //------------------------------------------------------------------------------------------------
 // Server only. Empty keeps the clouds the weather has when the transition starts.
 void SetTargetName(string stateName)
 {
  if (!Replication.IsServer())
   return;
  m_sTarget = stateName;
 }

 //------------------------------------------------------------------------------------------------
 // Server only: complete settings from a native mission save (etwVersion 1).
 bool RestoreSettings(notnull array<int> values, string target)
 {
  if (!Replication.IsServer() || values.Count() != ETW_Settings.W_COUNT - ETW_Settings.W_MINUTES)
   return false;
  for (int i = 0; i < values.Count(); i++)
  {
   int key = ETW_Settings.W_MINUTES + i;
   StoreSetting(key, ETW_Settings.ClampWeather(key, values[i]));
  }
  m_sTarget = target;
  return true;
 }

 //------------------------------------------------------------------------------------------------
 // Server only: a confirmed Game Master edit asks for a transition. Deferred by one call
 // queue tick so every attribute of the same Save lands first.
 void QueueApply(int playerId)
 {
  if (!Replication.IsServer())
   return;
  m_bQueuedApply = true;
  m_iQueuedPlayer = playerId;
  Schedule();
 }

 //------------------------------------------------------------------------------------------------
 void QueueAction(int action, int playerId)
 {
  if (!Replication.IsServer())
   return;
  m_iQueuedAction = action;
  m_iQueuedPlayer = playerId;
  Schedule();
 }

 //------------------------------------------------------------------------------------------------
 protected void Schedule()
 {
  if (m_bQueued)
   return;
  m_bQueued = true;
  GetGame().GetCallqueue().CallLater(RunQueued, 0, false);
 }

 //------------------------------------------------------------------------------------------------
 protected void RunQueued()
 {
  int action = m_iQueuedAction;
  bool apply = m_bQueuedApply;
  int playerId = m_iQueuedPlayer;
  m_bQueued = false;
  m_bQueuedApply = false;
  m_iQueuedAction = ETW_Settings.ACTION_NONE;
  if (action == ETW_Settings.ACTION_STOP)
  {
   ETW_WeatherRunner.Stop(playerId);
   return;
  }
  if (action == ETW_Settings.ACTION_AUTOMATIC)
  {
   ETW_WeatherRunner.StartAutomatic(this, playerId);
   return;
  }
  if (action == ETW_Settings.ACTION_START || apply)
   ETW_WeatherRunner.StartFromModule(this, playerId);
 }

 //------------------------------------------------------------------------------------------------
 string GetStatusText()
 {
  return m_sStatus;
 }

 //------------------------------------------------------------------------------------------------
 int GetProgress()
 {
  return m_iProgress;
 }

 //------------------------------------------------------------------------------------------------
 protected void SetStatus(string text, int progress)
 {
  if (m_sStatus == text && m_iProgress == progress)
   return;
  m_sStatus = text;
  m_iProgress = progress;
  Replication.BumpMe();
 }

 //------------------------------------------------------------------------------------------------
 // Server only: every module shows the one session status.
 static void PublishStatusAll(string text, int progress)
 {
  if (!Replication.IsServer())
   return;
  foreach (ETW_WeatherModule module : s_aModules)
  {
   if (module)
    module.SetStatus(text, progress);
  }
 }

 //------------------------------------------------------------------------------------------------
 // The newest module with "Smooth Scenario Properties weather changes" ON, or null.
 static ETW_WeatherModule FindSmoothing()
 {
  for (int i = s_aModules.Count() - 1; i >= 0; i--)
  {
   ETW_WeatherModule module = s_aModules[i];
   if (!module)
   {
    s_aModules.Remove(i);
    continue;
   }
   if (module.GetSetting(ETW_Settings.W_SMOOTH) != 0)
    return module;
  }
  return null;
 }
}

// Native mission saves (1.8 persistence): the base restores prefab and transform; the
// module settings are custom data. A running transition is not saved (see README).
class ETW_WeatherModuleSerializer : GenericEntitySerializer
{
 //------------------------------------------------------------------------------------------------
 override static typename GetTargetType()
 {
  return ETW_WeatherModule;
 }

 //------------------------------------------------------------------------------------------------
 override static EEntityDeserializeEvent GetDeserializeEvent()
 {
  return EEntityDeserializeEvent.AFTER_FINALIZE;
 }

 //------------------------------------------------------------------------------------------------
 override static EDeserializeFailHandling GetDeserializeFailHandling()
 {
  return EDeserializeFailHandling.IGNORE;
 }

 //------------------------------------------------------------------------------------------------
 override protected ESerializeResult Serialize(notnull IEntity entity, notnull SaveContext context)
 {
  ETW_WeatherModule module = ETW_WeatherModule.Cast(entity);
  if (!module)
   return ESerializeResult.ERROR;
  array<int> settings = {};
  for (int key = ETW_Settings.W_MINUTES; key < ETW_Settings.W_COUNT; key++)
  {
   settings.Insert(module.GetSetting(key));
  }
  string target = module.GetTargetName();
  if (!context.StartObject("base"))
   return ESerializeResult.ERROR;
  ESerializeResult result = super.Serialize(entity, context);
  bool ended = context.EndObject();
  if (!ended || result == ESerializeResult.ERROR)
   return ESerializeResult.ERROR;
  if (!context.WriteValue("etwVersion", 1) || !context.WriteValue("settings", settings) || !context.WriteValue("target", target))
   return ESerializeResult.ERROR;
  return ESerializeResult.OK;
 }

 //------------------------------------------------------------------------------------------------
 override protected bool Deserialize(notnull IEntity entity, notnull LoadContext context)
 {
  ETW_WeatherModule module = ETW_WeatherModule.Cast(entity);
  int version;
  array<int> settings = {};
  string target;
  if (!module || !context.ReadValue("etwVersion", version) || version != 1)
   return false;
  if (!context.ReadValue("settings", settings) || !context.ReadValue("target", target))
   return false;
  // Native saves omit an empty base object when the prefab supplies its defaults.
  if (context.DoesObjectExist("base"))
  {
   if (!context.StartObject("base"))
    return false;
   bool restored = super.Deserialize(entity, context);
   bool ended = context.EndObject();
   if (!restored || !ended)
    return false;
  }
  return module.RestoreSettings(settings, target);
 }
}
