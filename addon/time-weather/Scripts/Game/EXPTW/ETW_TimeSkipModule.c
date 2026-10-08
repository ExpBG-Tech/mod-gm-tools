// EXPBG Time Skip module (Game Master Systems entity). "Skip time now" in its attributes
// fades every player's screen to black, shows the text (default "6 hours later") and the
// new time, moves the clock forward under the black screen and fades back in. The fade
// is one reliable broadcast RPC from this always-relevant entity; a player who joins
// during the fade simply sees the normal view.
[EntityEditorProps(category: "EXPBG/Time and Weather", description: "Time Skip: fade to black, move the clock forward, fade back in")]
class ETW_TimeSkipModuleClass : GenericEntityClass {}

class ETW_TimeSkipModule : GenericEntity
{
 // Non-owning: entries become null when their module is deleted.
 protected static ref array<ETW_TimeSkipModule> s_aModules;

 [Attribute("6", UIWidgets.Slider, "Hours to skip", "0 48 1", category: "EXPBG Time Skip")]
 protected int m_iHours;
 [Attribute("0", UIWidgets.Slider, "Minutes to skip", "0 55 5", category: "EXPBG Time Skip")]
 protected int m_iMinutes;
 [Attribute("2", UIWidgets.Slider, "Fade to black (s)", "0.5 10 0.5", category: "EXPBG Time Skip")]
 protected float m_fFadeOut;
 [Attribute("3", UIWidgets.Slider, "Black screen (s)", "1 15 0.5", category: "EXPBG Time Skip")]
 protected float m_fHold;
 [Attribute("2", UIWidgets.Slider, "Fade back in (s)", "0.5 10 0.5", category: "EXPBG Time Skip")]
 protected float m_fFadeIn;
 [Attribute("{hours} hours later", UIWidgets.EditBox, "Text on the black screen; tokens {hours} {minutes} {time} {date}; a #key is translated", category: "EXPBG Time Skip")]
 protected string m_sText;
 [Attribute("1", UIWidgets.CheckBox, "Show the new time under the text", category: "EXPBG Time Skip")]
 protected bool m_bShowTime;
 [Attribute("1", UIWidgets.CheckBox, "Black screen for Game Masters in the editor too", category: "EXPBG Time Skip")]
 protected bool m_bIncludeGm;
 [RplProp()]
 protected string m_sStatus;
 [RplProp()]
 protected bool m_bBusy;

 protected bool m_bSkipQueued;
 protected int m_iSkipPlayer;

 //------------------------------------------------------------------------------------------------
 void ETW_TimeSkipModule(IEntitySource src, IEntity parent)
 {
  SetEventMask(EntityEvent.INIT);
 }

 //------------------------------------------------------------------------------------------------
 void ~ETW_TimeSkipModule()
 {
  if (s_aModules)
   s_aModules.RemoveItem(this);
  ArmaReforgerScripted game = GetGame();
  if (game && game.GetCallqueue())
   game.GetCallqueue().Remove(RunQueuedSkip);
 }

 //------------------------------------------------------------------------------------------------
 override void EOnInit(IEntity owner)
 {
  super.EOnInit(owner);
  if (!GetGame().InPlayMode())
   return;
  EXPBG_LazyStatics_ETW_TimeSkipModule();
  if (!s_aModules.Contains(this))
   s_aModules.Insert(this);
  if (!Replication.IsServer())
   return;
  for (int key = 0; key < ETW_Settings.T_COUNT; key++)
  {
   StoreSetting(key, ETW_Settings.ClampSkip(key, GetSetting(key)));
  }
  if (m_sText.Length() > ETW_Settings.TEXT_LIMIT)
   m_sText = m_sText.Substring(0, ETW_Settings.TEXT_LIMIT);
  SetStatus(ETW_TimeSkip.GetStatus(), ETW_TimeSkip.IsRunning());
 }

 //------------------------------------------------------------------------------------------------
 float GetSetting(int key)
 {
  if (key == ETW_Settings.T_HOURS)
   return m_iHours;
  if (key == ETW_Settings.T_MINUTES)
   return m_iMinutes;
  if (key == ETW_Settings.T_FADE_OUT)
   return m_fFadeOut;
  if (key == ETW_Settings.T_HOLD)
   return m_fHold;
  if (key == ETW_Settings.T_FADE_IN)
   return m_fFadeIn;
  if (key == ETW_Settings.T_SHOW_TIME && m_bShowTime)
   return 1;
  if (key == ETW_Settings.T_INCLUDE_GM && m_bIncludeGm)
   return 1;
  return 0;
 }

 //------------------------------------------------------------------------------------------------
 protected void StoreSetting(int key, float value)
 {
  if (key == ETW_Settings.T_HOURS)
   m_iHours = Math.Round(value);
  else if (key == ETW_Settings.T_MINUTES)
   m_iMinutes = Math.Round(value);
  else if (key == ETW_Settings.T_FADE_OUT)
   m_fFadeOut = value;
  else if (key == ETW_Settings.T_HOLD)
   m_fHold = value;
  else if (key == ETW_Settings.T_FADE_IN)
   m_fFadeIn = value;
  else if (key == ETW_Settings.T_SHOW_TIME)
   m_bShowTime = value != 0;
  else if (key == ETW_Settings.T_INCLUDE_GM)
   m_bIncludeGm = value != 0;
 }

 //------------------------------------------------------------------------------------------------
 // Server only: one Game Master edit, native load or test. Never starts a skip.
 void ApplySetting(int key, float value)
 {
  if (!Replication.IsServer() || key < 0 || key >= ETW_Settings.T_COUNT)
   return;
  StoreSetting(key, ETW_Settings.ClampSkip(key, value));
 }

 //------------------------------------------------------------------------------------------------
 string GetText()
 {
  return m_sText;
 }

 //------------------------------------------------------------------------------------------------
 // Server only; refuses text over the limit (whole, never cut).
 bool SetText(string text)
 {
  if (!Replication.IsServer() || text.Length() > ETW_Settings.TEXT_LIMIT)
   return false;
  m_sText = text;
  return true;
 }

 //------------------------------------------------------------------------------------------------
 // Server only: complete settings from a native mission save (etwVersion 1).
 bool RestoreSettings(notnull array<float> values, string text)
 {
  if (!Replication.IsServer() || values.Count() != ETW_Settings.T_COUNT || text.Length() > ETW_Settings.TEXT_LIMIT)
   return false;
  for (int key = 0; key < ETW_Settings.T_COUNT; key++)
  {
   StoreSetting(key, ETW_Settings.ClampSkip(key, values[key]));
  }
  m_sText = text;
  return true;
 }

 //------------------------------------------------------------------------------------------------
 // Server only: "Skip time now" from a confirmed Game Master edit, after every other
 // attribute of the same Save has landed.
 void QueueSkip(int playerId)
 {
  if (!Replication.IsServer())
   return;
  m_iSkipPlayer = playerId;
  if (m_bSkipQueued)
   return;
  m_bSkipQueued = true;
  GetGame().GetCallqueue().CallLater(RunQueuedSkip, 0, false);
 }

 //------------------------------------------------------------------------------------------------
 protected void RunQueuedSkip()
 {
  m_bSkipQueued = false;
  ETW_TimeSkip.Start(this, m_iSkipPlayer);
 }

 //------------------------------------------------------------------------------------------------
 // Server: the host and single player show their own fade (a broadcast RPC does not run
 // on its sender); every client gets the RPC. flags: 1 = Game Masters too, 2 = show time.
 void BroadcastFade(float fadeOut, float hold, float fadeIn, string text, int totalMinutes, string clock, string date, int flags)
 {
  RpcDo_Fade(fadeOut, hold, fadeIn, text, totalMinutes, clock, date, flags);
  Rpc(RpcDo_Fade, fadeOut, hold, fadeIn, text, totalMinutes, clock, date, flags);
 }

 //------------------------------------------------------------------------------------------------
 [RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
 protected void RpcDo_Fade(float fadeOut, float hold, float fadeIn, string text, int totalMinutes, string clock, string date, int flags)
 {
  string timeLine;
  if ((flags & 2) != 0)
   timeLine = clock;
  ETW_FadeOverlay.Show(fadeOut, hold, fadeIn, ETW_TimeMath.SkipText(text, totalMinutes, clock, date), timeLine, (flags & 1) != 0);
 }

 //------------------------------------------------------------------------------------------------
 string GetStatusText()
 {
  return m_sStatus;
 }

 //------------------------------------------------------------------------------------------------
 bool IsBusy()
 {
  return m_bBusy;
 }

 //------------------------------------------------------------------------------------------------
 protected void SetStatus(string text, bool busy)
 {
  if (m_sStatus == text && m_bBusy == busy)
   return;
  m_sStatus = text;
  m_bBusy = busy;
  Replication.BumpMe();
 }

 //------------------------------------------------------------------------------------------------
 static void PublishStatusAll(string text, bool busy)
 {
  if (!Replication.IsServer())
   return;
  EXPBG_LazyStatics_ETW_TimeSkipModule();
  foreach (ETW_TimeSkipModule module : s_aModules)
  {
   if (module)
    module.SetStatus(text, busy);
  }
 }

 //------------------------------------------------------------------------------------------------
 //! Creates the collections on first use (not in the global static initializer, which has a
 //! per-function instruction limit that large modsets exceed on Windows).
 protected static void EXPBG_LazyStatics_ETW_TimeSkipModule()
 {
  if (!s_aModules)
   s_aModules = new array<ETW_TimeSkipModule>();
 }
}

// Server flow of the one time skip of the session: fade out, change the clock at full
// black (date rollover included), fade in. Overlapping skips are refused. Calls are static
// with a serial, so deleting the module or ending the mission never leaves a stale call.
class ETW_TimeSkip
{
 protected static TimeAndWeatherManagerEntity s_Manager;
 protected static bool s_bRunning;
 protected static int s_iSerial;
 protected static int s_iTotalMinutes;
 protected static int s_iPlayer;
 protected static string s_sLastTo;
 static const string STATUS_READY = "Ready. Choose Skip time now and press Save.";
 // Empty until the first skip: reads as STATUS_READY.
 protected static string s_sStatus;
 // Evidence for logs and fixtures.
 protected static int s_iBroadcasts;
 protected static int s_iApplied;
 protected static int s_iRefused;

 //------------------------------------------------------------------------------------------------
 static bool IsRunning()
 {
  return s_bRunning && s_Manager != null;
 }

 //------------------------------------------------------------------------------------------------
 static string GetStatus()
 {
  // A mission that ended during a skip leaves no "Skipping..." status for the next one.
  if (s_bRunning && !s_Manager)
  {
   s_bRunning = false;
   s_sStatus = STATUS_READY;
  }
  if (s_sStatus.IsEmpty())
   return STATUS_READY;
  return s_sStatus;
 }

 //------------------------------------------------------------------------------------------------
 static int GetBroadcasts()
 {
  return s_iBroadcasts;
 }

 //------------------------------------------------------------------------------------------------
 static int GetApplied()
 {
  return s_iApplied;
 }

 //------------------------------------------------------------------------------------------------
 static int GetRefused()
 {
  return s_iRefused;
 }

 //------------------------------------------------------------------------------------------------
 static string GetLastTo()
 {
  return s_sLastTo;
 }

 //------------------------------------------------------------------------------------------------
 static bool Start(notnull ETW_TimeSkipModule module, int playerId)
 {
  if (!Replication.IsServer())
   return false;
  if (IsRunning())
  {
   Refuse("Refused: a time skip is already running.", playerId);
   return false;
  }
  TimeAndWeatherManagerEntity manager = ETW_WeatherRunner.Manager();
  if (!manager)
  {
   Refuse("Refused: this world has no time manager.", playerId);
   return false;
  }
  int hours = Math.Round(module.GetSetting(ETW_Settings.T_HOURS));
  int minutes = Math.Round(module.GetSetting(ETW_Settings.T_MINUTES));
  int totalMinutes = hours * 60 + minutes;
  if (totalMinutes <= 0)
  {
   Refuse("Refused: hours and minutes are both 0.", playerId);
   return false;
  }
  float fadeOut = ETW_Settings.ClampSkip(ETW_Settings.T_FADE_OUT, module.GetSetting(ETW_Settings.T_FADE_OUT));
  float hold = ETW_Settings.ClampSkip(ETW_Settings.T_HOLD, module.GetSetting(ETW_Settings.T_HOLD));
  float fadeIn = ETW_Settings.ClampSkip(ETW_Settings.T_FADE_IN, module.GetSetting(ETW_Settings.T_FADE_IN));

  // The time shown on the black screen; the clock itself changes at full black.
  int year, month, day;
  manager.GetDate(year, month, day);
  int toYear, toMonth, toDay;
  float toHours;
  ETW_TimeMath.AddHours(year, month, day, manager.GetTimeOfTheDay(), totalMinutes / 60.0, toYear, toMonth, toDay, toHours);
  string clock = ETW_TimeMath.FormatClock(toHours);
  string date = ETW_TimeMath.FormatDate(toYear, toMonth, toDay);
  int flags = 0;
  if (module.GetSetting(ETW_Settings.T_INCLUDE_GM) != 0)
   flags = flags | 1;
  if (module.GetSetting(ETW_Settings.T_SHOW_TIME) != 0)
   flags = flags | 2;

  s_Manager = manager;
  s_bRunning = true;
  s_iSerial++;
  s_iTotalMinutes = totalMinutes;
  s_iPlayer = playerId;
  s_iBroadcasts++;
  module.BroadcastFade(fadeOut, hold, fadeIn, module.GetText(), totalMinutes, clock, date, flags);
  int applyMs = Math.Round((fadeOut + hold * 0.5) * 1000);
  int endMs = Math.Round((fadeOut + hold + fadeIn) * 1000) + 250;
  ScriptCallQueue queue = GetGame().GetCallqueue();
  queue.CallLater(ETW_TimeSkip.Apply, applyMs, false, s_iSerial);
  queue.CallLater(ETW_TimeSkip.End, endMs, false, s_iSerial);
  SetStatus("Skipping " + ETW_TimeMath.FormatSkip(totalMinutes) + ": black screen.", true);
  Print(string.Format("[ETW] time skip start +%1 player=%2 fade=%3/%4/%5 s text='%6' gm=%7 showTime=%8", ETW_TimeMath.FormatSkip(totalMinutes), playerId, fadeOut, hold, fadeIn, module.GetText(), flags & 1, (flags & 2) != 0), LogLevel.NORMAL);
  return true;
 }

 //------------------------------------------------------------------------------------------------
 protected static void Refuse(string status, int playerId)
 {
  s_iRefused++;
  SetStatus(status, IsRunning());
  Print(string.Format("[ETW] time skip refused (player %1): %2", playerId, status), LogLevel.NORMAL);
 }

 //------------------------------------------------------------------------------------------------
 // At full black: a running weather transition completes, then date and time move on.
 protected static void Apply(int serial)
 {
  if (serial != s_iSerial || !IsRunning())
   return;
  ETW_WeatherRunner.FinishNow("time skip");
  int year, month, day;
  s_Manager.GetDate(year, month, day);
  float now = s_Manager.GetTimeOfTheDay();
  int toYear, toMonth, toDay;
  float toHours;
  ETW_TimeMath.AddHours(year, month, day, now, s_iTotalMinutes / 60.0, toYear, toMonth, toDay, toHours);
  bool dateSet = true;
  if (toYear != year || toMonth != month || toDay != day)
   dateSet = s_Manager.SetDate(toYear, toMonth, toDay, true);
  bool timeSet = s_Manager.SetTimeOfTheDay(toHours, true);
  s_iApplied++;
  string before = ETW_TimeMath.FormatDate(year, month, day) + " " + ETW_TimeMath.FormatClock(now);
  s_sLastTo = ETW_TimeMath.FormatDate(toYear, toMonth, toDay) + " " + ETW_TimeMath.FormatClock(toHours);
  Print(string.Format("[ETW] time skip +%1 from %2 to %3 player=%4 dateSet=%5 timeSet=%6", ETW_TimeMath.FormatSkip(s_iTotalMinutes), before, s_sLastTo, s_iPlayer, dateSet, timeSet), LogLevel.NORMAL);
 }

 //------------------------------------------------------------------------------------------------
 protected static void End(int serial)
 {
  if (serial != s_iSerial)
   return;
  s_bRunning = false;
  SetStatus("Ready. Last skip: " + ETW_TimeMath.FormatSkip(s_iTotalMinutes) + ", to " + s_sLastTo + ".", false);
  Print(string.Format("[ETW] time skip finished (+%1)", ETW_TimeMath.FormatSkip(s_iTotalMinutes)), LogLevel.NORMAL);
 }

 //------------------------------------------------------------------------------------------------
 protected static void SetStatus(string text, bool busy)
 {
  s_sStatus = text;
  ETW_TimeSkipModule.PublishStatusAll(text, busy);
 }
}

// Native mission saves (1.8 persistence): the module settings and text. The game clock is
// saved by the vanilla time and weather persistence; a save taken during the black screen
// before the clock changed does not contain that skip.
class ETW_TimeSkipModuleSerializer : GenericEntitySerializer
{
 //------------------------------------------------------------------------------------------------
 override static typename GetTargetType()
 {
  return ETW_TimeSkipModule;
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
  ETW_TimeSkipModule module = ETW_TimeSkipModule.Cast(entity);
  if (!module)
   return ESerializeResult.ERROR;
  array<float> settings = {};
  for (int key = 0; key < ETW_Settings.T_COUNT; key++)
  {
   settings.Insert(module.GetSetting(key));
  }
  string text = module.GetText();
  if (!context.StartObject("base"))
   return ESerializeResult.ERROR;
  ESerializeResult result = super.Serialize(entity, context);
  bool ended = context.EndObject();
  if (!ended || result == ESerializeResult.ERROR)
   return ESerializeResult.ERROR;
  if (!context.WriteValue("etwVersion", 1) || !context.WriteValue("settings", settings) || !context.WriteValue("text", text))
   return ESerializeResult.ERROR;
  return ESerializeResult.OK;
 }

 //------------------------------------------------------------------------------------------------
 override protected bool Deserialize(notnull IEntity entity, notnull LoadContext context)
 {
  ETW_TimeSkipModule module = ETW_TimeSkipModule.Cast(entity);
  int version;
  array<float> settings = {};
  string text;
  if (!module || !context.ReadValue("etwVersion", version) || version != 1)
   return false;
  if (!context.ReadValue("settings", settings) || !context.ReadValue("text", text))
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
  return module.RestoreSettings(settings, text);
 }
}
