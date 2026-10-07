// TEST ONLY. EXPBG Time and Weather: Weather Transition and Time Skip on the dedicated
// fixture server (no players, no GM UI, no client view).
// pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -FixturePath tests/ETW_TimeWeatherGameplay.c -TimeoutSeconds 420 -OrchestratorSlotGranted -ExpectResult '\[ETW RESULT\] checks=[1-9]\d* failures=0 rollover=1 skip=1 broadcasts=3 gradual=1 finished=1 interrupt=1 skipFinish=1 foreign=1 smooth=[01] reason=complete'
// The runner copies this file to EXPG_GarrisonGameplay.c; the class names are fixed. Judge
// the run by its one [ETW RESULT] line (-ExpectResult also rejects script errors).
// Real module prefabs and public server entry points (the attribute writes call the same).
//  rollover    calendar arithmetic: year end, leap day, non-leap February, 30-day month;
//              module defaults, clamps, native-save restore, choice values and texts.
//  skip        Time Skip +26 h 30 min from 2026-12-31 22:00 lands on 2027-01-02 00:30 and
//              +2 h from 2028-02-28 23:00 on 2028-02-29 01:00 (day auto-advance off, so
//              exact); an overlapping skip is refused; nothing is drawn on a dedicated
//              server.
//  broadcasts  fade broadcasts sent (three skips); the local RPC handler ran as often.
//  gradual     Weather Transition to another state (Rainy if the start is not Rainy) in
//              1 min: rain moves monotonically from its start toward the target, about
//              half way after 30 s (ease-in-out), never jumping.
//  finished    after the minute (plus the clouds' grace): rain at target within 0.02,
//              wind 8 m/s within 0.1, the target state reached, weather held (looping).
//  interrupt   a new transition 20 s into another one continues from the rain reached
//              (no jump one second later) with a restarted clock.
//  skipFinish  a time skip during a transition completes it under the black screen.
//  foreign     a ForceWeatherTo (what Scenario Properties weather does) stops the
//              transition and hands rain back to the weather.
//  smooth      evidence only: 1 when the clouds reached the target through the engine
//              blend without the end-of-transition snap (needs native acceptance).
class EXPG_GarrisonGameplayClass : GenericEntityClass {}
class EXPG_GarrisonGameplay : GenericEntity
{
 static const float FIXTURE_SECONDS = 300;
 static const ResourceName WEATHER_MODULE = "{B291F78787B9F4B0}PrefabsEditable/EXPTW/ETW_WeatherTransition.et";
 static const ResourceName SKIP_MODULE = "{D9EDAB5E98566D9A}PrefabsEditable/EXPTW/ETW_TimeSkip.et";
 vector m_vOrigin = "4773.46 0 7094.57";
 ETW_WeatherModule m_WeatherModule;
 ETW_TimeSkipModule m_SkipModule;
 TimeAndWeatherManagerEntity m_TimeManager;
 int m_iPhase;
 int m_iChecks;
 int m_iFailures;
 float m_fStarted;
 float m_fPhaseAt;
 // Real milliseconds when a time skip started: its timers run on the real-time call
 // queue, while world time can catch up in bursts after a server hitch, so the
 // skip is awaited (at most 15 s) instead of a fixed world-time wait.
 int m_iSkipTick;
 float m_fNext;
 bool m_bFinished;
 bool m_bAutoAdvance;
 int m_iBroadcastsAtStart;
 int m_iReceivedAtStart;
 int m_iAppliedAtStart;
 string m_sStateA;
 string m_sStateB;
 float m_fRainStart;
 float m_fRainTarget;
 float m_fLastProgress;
 int m_iSamples;
 bool m_bMonotonic = true;
 bool m_bMidSeen;
 bool m_bMidOk;
 int m_iSnapsBefore;
 float m_fRainBeforeInterrupt;
 int m_iStartsBeforeInterrupt;
 bool m_bRestarted;
 int m_iForeignBefore;
 bool m_bYearEnd;
 bool m_bRolloverPass;
 bool m_bSkipPass;
 bool m_bGradualPass;
 bool m_bFinishedPass;
 bool m_bInterruptPass;
 bool m_bSkipFinishPass;
 bool m_bForeignPass;
 bool m_bSmooth;

 //------------------------------------------------------------------------------------------------
 void EXPG_GarrisonGameplay(IEntitySource src, IEntity parent)
 {
  SetEventMask(EntityEvent.INIT | EntityEvent.FRAME);
 }

 //------------------------------------------------------------------------------------------------
 float Now()
 {
  return GetGame().GetWorld().GetWorldTime() * 0.001;
 }

 //------------------------------------------------------------------------------------------------
 override void EOnInit(IEntity owner)
 {
  if (!Replication.IsServer())
  {
   ClearEventMask(EntityEvent.FRAME);
   return;
  }
  m_fStarted = Now();
  m_fNext = m_fStarted + 10;
  PrintFormat("[ETW BEGIN] origin=%1 deadline=%2", m_vOrigin, FIXTURE_SECONDS);
 }

 //------------------------------------------------------------------------------------------------
 bool Check(bool ok, string label)
 {
  m_iChecks++;
  if (!ok)
   m_iFailures++;
  PrintFormat("[ETW CHECK] pass=%1 %2", ok, label);
  return ok;
 }

 //------------------------------------------------------------------------------------------------
 int Flag(bool value)
 {
  if (value)
   return 1;
  return 0;
 }

 //------------------------------------------------------------------------------------------------
 void Wait(float seconds)
 {
  m_fNext = Now() + seconds;
 }

 //------------------------------------------------------------------------------------------------
 void Finish(string reason)
 {
  if (m_bFinished)
   return;
  m_bFinished = true;
  ClearEventMask(EntityEvent.FRAME);
  if (m_TimeManager)
   m_TimeManager.SetIsDayAutoAdvanced(m_bAutoAdvance);
  string head = string.Format("[ETW RESULT] checks=%1 failures=%2 rollover=%3 skip=%4 broadcasts=%5 gradual=%6 finished=%7 interrupt=%8 skipFinish=%9", m_iChecks, m_iFailures, Flag(m_bRolloverPass), Flag(m_bSkipPass), ETW_TimeSkip.GetBroadcasts() - m_iBroadcastsAtStart, Flag(m_bGradualPass), Flag(m_bFinishedPass), Flag(m_bInterruptPass), Flag(m_bSkipFinishPass));
  string tail = string.Format(" foreign=%1 smooth=%2 reason=%3", Flag(m_bForeignPass), Flag(m_bSmooth), reason);
  Print(head + tail, LogLevel.NORMAL);
  GetGame().RequestClose();
 }

 //------------------------------------------------------------------------------------------------
 EntitySpawnParams Params(vector p)
 {
  EntitySpawnParams spawn = new EntitySpawnParams();
  spawn.TransformMode = ETransformMode.WORLD;
  Math3D.AnglesToMatrix(vector.Zero, spawn.Transform);
  p[1] = GetGame().GetWorld().GetSurfaceY(p[0], p[2]) + 0.3;
  spawn.Transform[3] = p;
  return spawn;
 }

 //------------------------------------------------------------------------------------------------
 string StateName()
 {
  WeatherState state = m_TimeManager.GetCurrentWeatherState();
  if (!state)
   return string.Empty;
  return state.GetStateName();
 }

 //------------------------------------------------------------------------------------------------
 float Overcast()
 {
  LocalWeatherSituation situation = new LocalWeatherSituation();
  if (!m_TimeManager.TryGetCompleteLocalWeather(situation, 0, m_vOrigin))
   return -1;
  return situation.GetOvercast();
 }

 //------------------------------------------------------------------------------------------------
 void ConfigureSkip(int hours, int minutes)
 {
  m_SkipModule.ApplySetting(ETW_Settings.T_HOURS, hours);
  m_SkipModule.ApplySetting(ETW_Settings.T_MINUTES, minutes);
  m_SkipModule.ApplySetting(ETW_Settings.T_FADE_OUT, 0.5);
  m_SkipModule.ApplySetting(ETW_Settings.T_HOLD, 1);
  m_SkipModule.ApplySetting(ETW_Settings.T_FADE_IN, 0.5);
 }

 //------------------------------------------------------------------------------------------------
 void ConfigureWeather(string target, int rain, int windSpeed)
 {
  m_WeatherModule.SetTargetName(target);
  m_WeatherModule.ApplySetting(ETW_Settings.W_MINUTES, 1);
  m_WeatherModule.ApplySetting(ETW_Settings.W_RAIN, rain);
  m_WeatherModule.ApplySetting(ETW_Settings.W_FOG, 0);
  m_WeatherModule.ApplySetting(ETW_Settings.W_WIND_SPEED, windSpeed);
  m_WeatherModule.ApplySetting(ETW_Settings.W_WIND_DIR, 0);
  m_WeatherModule.ApplySetting(ETW_Settings.W_AFTER, ETW_Settings.AFTER_HOLD);
 }

 //------------------------------------------------------------------------------------------------
 bool DateIs(int year, int month, int day, float hours)
 {
  int y, m, d;
  m_TimeManager.GetDate(y, m, d);
  float h = m_TimeManager.GetTimeOfTheDay();
  PrintFormat("[ETW CLOCK] date=%1-%2-%3 time=%4 expected=%5-%6-%7 %8", y, m, d, h, year, month, day, hours);
  return y == year && m == month && d == day && Math.AbsFloat(h - hours) < 0.02;
 }

 //------------------------------------------------------------------------------------------------
 override void EOnFrame(IEntity owner, float timeSlice)
 {
  if (m_bFinished || Now() < m_fNext)
   return;
  if (Now() - m_fStarted > FIXTURE_SECONDS)
  {
   Check(false, string.Format("phase %1 finished before the fixture deadline", m_iPhase));
   Finish("timeout");
   return;
  }
  if (m_iPhase == 0)
   Setup();
  else if (m_iPhase == 1)
   SkipYearEnd();
  else if (m_iPhase == 2)
   VerifyYearEnd();
  else if (m_iPhase == 3)
   SkipLeapDay();
  else if (m_iPhase == 4)
   VerifyLeapDayStartWeather();
  else if (m_iPhase == 5)
   SampleWeather();
  else if (m_iPhase == 6)
   Interrupt();
  else if (m_iPhase == 7)
   VerifyInterruptSkip();
  else if (m_iPhase == 8)
   VerifySkipFinishStartForeign();
  else if (m_iPhase == 9)
   Foreign();
 }

 //------------------------------------------------------------------------------------------------
 void Setup()
 {
  m_TimeManager = ETW_WeatherRunner.Manager();
  if (!Check(m_TimeManager != null && m_TimeManager.GetTransitionManager() != null, "server time and weather manager with a transition manager"))
  {
   Finish("setup");
   return;
  }
  Check(GetGame().GetPlayerManager().GetPlayerCount() == 0 && !ETW_WeatherRunner.IsRunning() && !ETW_TimeSkip.IsRunning(), "isolated server: no players, no transition, no skip");
  // Keep the Resource and the spawned entity in locals before casting.
  Resource weatherPrefab = Resource.Load(WEATHER_MODULE);
  IEntity weatherEntity = GetGame().SpawnEntityPrefab(weatherPrefab, GetGame().GetWorld(), Params(m_vOrigin));
  m_WeatherModule = ETW_WeatherModule.Cast(weatherEntity);
  Resource skipPrefab = Resource.Load(SKIP_MODULE);
  IEntity skipEntity = GetGame().SpawnEntityPrefab(skipPrefab, GetGame().GetWorld(), Params(m_vOrigin + Vector(5, 0, 0)));
  m_SkipModule = ETW_TimeSkipModule.Cast(skipEntity);
  if (!Check(m_WeatherModule != null && m_SkipModule != null, "Weather Transition and Time Skip prefabs spawned"))
  {
   Finish("setup");
   return;
  }

  // Defaults, clamps, save restore and choices.
  bool weatherDefaults = m_WeatherModule.GetSetting(ETW_Settings.W_MINUTES) == 10 && m_WeatherModule.GetSetting(ETW_Settings.W_RAIN) == 0 && m_WeatherModule.GetSetting(ETW_Settings.W_FOG) == 0;
  weatherDefaults = weatherDefaults && m_WeatherModule.GetSetting(ETW_Settings.W_WIND_SPEED) == 0 && m_WeatherModule.GetSetting(ETW_Settings.W_WIND_DIR) == 0 && m_WeatherModule.GetSetting(ETW_Settings.W_AFTER) == ETW_Settings.AFTER_HOLD;
  weatherDefaults = weatherDefaults && m_WeatherModule.GetSetting(ETW_Settings.W_SMOOTH) == 1 && m_WeatherModule.GetTargetName().IsEmpty();
  Check(weatherDefaults, "weather defaults: 10 min, rain/fog/wind left to the weather, hold, smooth Scenario Properties ON, clouds kept");
  bool skipDefaults = m_SkipModule.GetSetting(ETW_Settings.T_HOURS) == 6 && m_SkipModule.GetSetting(ETW_Settings.T_MINUTES) == 0 && m_SkipModule.GetSetting(ETW_Settings.T_FADE_OUT) == 2;
  skipDefaults = skipDefaults && m_SkipModule.GetSetting(ETW_Settings.T_HOLD) == 3 && m_SkipModule.GetSetting(ETW_Settings.T_FADE_IN) == 2 && m_SkipModule.GetText() == "{hours} hours later";
  skipDefaults = skipDefaults && m_SkipModule.GetSetting(ETW_Settings.T_SHOW_TIME) == 1 && m_SkipModule.GetSetting(ETW_Settings.T_INCLUDE_GM) == 1;
  Check(skipDefaults, "time skip defaults: 6 h 0 min, fades 2/3/2 s, text {hours} hours later, time shown, Game Masters included");
  m_WeatherModule.ApplySetting(ETW_Settings.W_MINUTES, 500);
  bool clampHigh = m_WeatherModule.GetSetting(ETW_Settings.W_MINUTES) == 120;
  m_WeatherModule.ApplySetting(ETW_Settings.W_MINUTES, 0);
  bool clampLow = m_WeatherModule.GetSetting(ETW_Settings.W_MINUTES) == 1;
  m_SkipModule.ApplySetting(ETW_Settings.T_HOURS, 99);
  bool clampHours = m_SkipModule.GetSetting(ETW_Settings.T_HOURS) == 48;
  m_SkipModule.ApplySetting(ETW_Settings.T_HOLD, 0);
  bool clampHold = m_SkipModule.GetSetting(ETW_Settings.T_HOLD) == 1;
  Check(clampHigh && clampLow && clampHours && clampHold, "clamps: transition 1-120 min, skip at most 48 h, black screen at least 1 s");
  array<int> saved = {7, 4, 0, 5, 3, 1, 0};
  bool restored = m_WeatherModule.RestoreSettings(saved, "Rainy");
  restored = restored && m_WeatherModule.GetSetting(ETW_Settings.W_MINUTES) == 7 && m_WeatherModule.GetSetting(ETW_Settings.W_SMOOTH) == 0 && m_WeatherModule.GetTargetName() == "Rainy";
  array<int> wrong = {1, 2};
  bool refused = !m_WeatherModule.RestoreSettings(wrong, string.Empty);
  array<float> skipSaved = {3, 15, 1, 4, 1.5, 0, 1};
  bool skipRestored = m_SkipModule.RestoreSettings(skipSaved, "Dawn") && m_SkipModule.GetSetting(ETW_Settings.T_MINUTES) == 15 && m_SkipModule.GetSetting(ETW_Settings.T_SHOW_TIME) == 0 && m_SkipModule.GetText() == "Dawn";
  Check(restored && refused && skipRestored, "native save restore: etwVersion 1 settings and text restore, a wrong count is refused");
  array<int> defaults = {10, 0, 0, 0, 0, 0, 1};
  m_WeatherModule.RestoreSettings(defaults, string.Empty);
  bool choices = ETW_Settings.ChoiceValue(ETW_Settings.W_RAIN, 4) == 0.5 && ETW_Settings.ChoiceValue(ETW_Settings.W_RAIN, 0) == -1;
  choices = choices && ETW_Settings.ChoiceValue(ETW_Settings.W_WIND_SPEED, 5) == 8 && ETW_Settings.ChoiceValue(ETW_Settings.W_WIND_DIR, 3) == 90;
  Check(choices, "choices: rain Moderate 0.5, weather default -1, wind 8 m/s, east 90 deg");

  // Calendar arithmetic.
  int y, m, d;
  float h;
  ETW_TimeMath.AddHours(2026, 12, 31, 22, 26.5, y, m, d, h);
  bool yearEnd = y == 2027 && m == 1 && d == 2 && Math.AbsFloat(h - 0.5) < 0.001;
  ETW_TimeMath.AddHours(2028, 2, 28, 23, 2, y, m, d, h);
  bool leap = y == 2028 && m == 2 && d == 29 && Math.AbsFloat(h - 1) < 0.001;
  ETW_TimeMath.AddHours(2027, 2, 28, 23, 2, y, m, d, h);
  bool common = y == 2027 && m == 3 && d == 1 && Math.AbsFloat(h - 1) < 0.001;
  ETW_TimeMath.AddHours(2026, 4, 30, 10, 48, y, m, d, h);
  bool shortMonth = y == 2026 && m == 5 && d == 2 && Math.AbsFloat(h - 10) < 0.001;
  bool century = !ETW_TimeMath.IsLeapYear(2100) && ETW_TimeMath.IsLeapYear(2000);
  m_bRolloverPass = Check(yearEnd && leap && common && shortMonth && century, "calendar: year end, leap day, non-leap February, 30-day month, century rule");
  bool texts = ETW_TimeMath.SkipText(ETW_Settings.TEXT_DEFAULT, 60, "07:00", "2026-01-01") == "1 hour later";
  texts = texts && ETW_TimeMath.SkipText(ETW_Settings.TEXT_DEFAULT, 390, "14:30", "2026-01-02") == "6 hours 30 minutes later";
  texts = texts && ETW_TimeMath.SkipText(string.Empty, 45, "14:30", "2026-01-02") == "45 minutes later";
  texts = texts && ETW_TimeMath.SkipText("Day {date}, {time} ({hours}h{minutes})", 390, "14:30", "2026-01-02") == "Day 2026-01-02, 14:30 (6h30)";
  texts = texts && ETW_TimeMath.FormatClock(0.5) == "00:30" && ETW_TimeMath.FormatClock(23.999) == "00:00";
  m_bRolloverPass = Check(texts, "texts: automatic phrase with singular and minutes, tokens, clock") && m_bRolloverPass;

  // Time skip across the year end with the clock frozen.
  m_bAutoAdvance = m_TimeManager.GetIsDayAutoAdvanced();
  m_TimeManager.SetIsDayAutoAdvanced(false);
  bool dateSet = m_TimeManager.SetDate(2026, 12, 31, true);
  bool timeSet = m_TimeManager.SetTimeOfTheDay(22, true);
  Check(dateSet && timeSet, "clock set to 2026-12-31 22:00 with day auto-advance off");
  ConfigureSkip(26, 30);
  m_iBroadcastsAtStart = ETW_TimeSkip.GetBroadcasts();
  m_iReceivedAtStart = ETW_FadeOverlay.GetReceived();
  m_iAppliedAtStart = ETW_TimeSkip.GetApplied();
  m_iPhase = 1;
  Wait(1);
 }

 //------------------------------------------------------------------------------------------------
 void SkipYearEnd()
 {
  bool before = DateIs(2026, 12, 31, 22);
  bool first = ETW_TimeSkip.Start(m_SkipModule, -1);
  bool running = ETW_TimeSkip.IsRunning();
  bool second = ETW_TimeSkip.Start(m_SkipModule, -1);
  Check(before && first && running && !second, "skip started; an overlapping skip is refused");
  Check(ETW_TimeSkip.GetBroadcasts() - m_iBroadcastsAtStart == 1 && ETW_FadeOverlay.GetReceived() - m_iReceivedAtStart == 1, "one fade broadcast; the local fade handler ran once");
  Check(ETW_FadeOverlay.GetShown() == 0 && !ETW_FadeOverlay.IsShowing(), "a dedicated server draws no black screen");
  m_iSkipTick = System.GetTickCount();
  m_iPhase = 2;
  Wait(2.6);
 }

 //------------------------------------------------------------------------------------------------
 void VerifyYearEnd()
 {
  if (SkipPending())
   return;
  m_bYearEnd = Check(DateIs(2027, 1, 2, 0.5), "skip +26 h 30 min from 2026-12-31 22:00 lands on 2027-01-02 00:30");
  Check(!ETW_TimeSkip.IsRunning() && ETW_TimeSkip.GetApplied() - m_iAppliedAtStart == 1, "the skip applied once and ended after fade in");
  m_TimeManager.SetDate(2028, 2, 28, true);
  m_TimeManager.SetTimeOfTheDay(23, true);
  ConfigureSkip(2, 0);
  m_iPhase = 3;
  Wait(1);
 }

 //------------------------------------------------------------------------------------------------
 void SkipLeapDay()
 {
  bool before = DateIs(2028, 2, 28, 23);
  Check(before && ETW_TimeSkip.Start(m_SkipModule, -1), "second skip started after the first ended");
  m_iSkipTick = System.GetTickCount();
  m_iPhase = 4;
  Wait(2.6);
 }

 //------------------------------------------------------------------------------------------------
 // A skip still running (its real-time timers have not fired yet): look again soon.
 bool SkipPending()
 {
  if (!ETW_TimeSkip.IsRunning() || System.GetTickCount() - m_iSkipTick > 15000)
   return false;
  Wait(0.25);
  return true;
 }

 //------------------------------------------------------------------------------------------------
 void VerifyLeapDayStartWeather()
 {
  if (SkipPending())
   return;
  bool leap = Check(DateIs(2028, 2, 29, 1), "skip +2 h from 2028-02-28 23:00 lands on 2028-02-29 01:00");
  bool counts = ETW_TimeSkip.GetBroadcasts() - m_iBroadcastsAtStart == 2 && ETW_FadeOverlay.GetReceived() - m_iReceivedAtStart == 2 && ETW_TimeSkip.GetRefused() > 0;
  m_bSkipPass = Check(m_bYearEnd && leap && counts && !ETW_TimeSkip.IsRunning(), "time skips: exact hours with rollover, one broadcast each, overlap refused");
  // Weather with the clock running, as in a mission.
  m_TimeManager.SetIsDayAutoAdvanced(true);
  array<ref WeatherState> states = {};
  m_TimeManager.GetWeatherStatesList(states);
  m_sStateA = StateName();
  m_sStateB = string.Empty;
  foreach (WeatherState state : states)
  {
   if (!state || state.GetStateName() == m_sStateA)
    continue;
   if (m_sStateB.IsEmpty() || state.GetStateName() == "Rainy")
    m_sStateB = state.GetStateName();
  }
  if (!Check(!m_sStateA.IsEmpty() && !m_sStateB.IsEmpty(), string.Format("two weather states: start %1, target %2", m_sStateA, m_sStateB)))
  {
   Finish("setup");
   return;
  }
  m_fRainStart = m_TimeManager.GetRainIntensity();
  int rainChoice = 6;
  m_fRainTarget = 1;
  if (m_fRainStart >= 0.5)
  {
   rainChoice = 1;
   m_fRainTarget = 0;
  }
  ConfigureWeather(m_sStateB, rainChoice, 5);
  m_iSnapsBefore = ETW_WeatherRunner.GetCloudSnaps();
  bool started = ETW_WeatherRunner.StartFromModule(m_WeatherModule, -1);
  bool modes = ETW_WeatherRunner.GetChannelMode(ETW_WeatherRunner.CH_RAIN) == ETW_WeatherRunner.MODE_SET && ETW_WeatherRunner.GetChannelMode(ETW_WeatherRunner.CH_WIND_SPEED) == ETW_WeatherRunner.MODE_SET;
  PrintFormat("[ETW WEATHER] start=%1 target=%2 rain=%3 rainTarget=%4 wind=%5 overcast=%6 day=%7", m_sStateA, m_sStateB, m_fRainStart, m_fRainTarget, m_TimeManager.GetWindSpeed(), Overcast(), m_TimeManager.GetDayDuration());
  Check(started && ETW_WeatherRunner.IsRunning() && ETW_WeatherRunner.GetTarget() == m_sStateB && modes, "transition started toward the target with rain and wind set");
  m_fPhaseAt = Now();
  m_fLastProgress = 0;
  m_iPhase = 5;
  Wait(5);
 }

 //------------------------------------------------------------------------------------------------
 void SampleWeather()
 {
  float elapsed = Now() - m_fPhaseAt;
  float rain = m_TimeManager.GetRainIntensity();
  float progressed = (rain - m_fRainStart) / (m_fRainTarget - m_fRainStart);
  bool running = ETW_WeatherRunner.IsRunning();
  if (running)
  {
   m_iSamples++;
   if (progressed < m_fLastProgress - 0.01)
    m_bMonotonic = false;
   m_fLastProgress = progressed;
   if (!m_bMidSeen && elapsed >= 27 && elapsed <= 33)
   {
    m_bMidSeen = true;
    m_bMidOk = progressed > 0.2 && progressed < 0.8;
   }
  }
  PrintFormat("[ETW SAMPLE] t=%1 rain=%2 progress=%3 wind=%4 overcast=%5 state=%6 running=%7 status=%8", elapsed, rain, progressed, m_TimeManager.GetWindSpeed(), Overcast(), StateName(), running, ETW_WeatherRunner.GetStatus());
  if (running && elapsed < 100)
  {
   Wait(2);
   return;
  }
  m_bGradualPass = Check(m_bMonotonic && m_bMidSeen && m_bMidOk && m_iSamples >= 10, string.Format("rain moved gradually: %1 samples, monotonic %2, half way at 30 s %3", m_iSamples, m_bMonotonic, m_bMidOk));
  bool rainEnd = Math.AbsFloat(rain - m_fRainTarget) < 0.02 && m_TimeManager.IsRainIntensityOverridden();
  bool windEnd = Math.AbsFloat(m_TimeManager.GetWindSpeed() - 8) < 0.1;
  bool stateEnd = StateName() == m_sStateB;
  m_bSmooth = ETW_WeatherRunner.GetCloudSnaps() == m_iSnapsBefore;
  PrintFormat("[ETW FINISHED] rain=%1 wind=%2 state=%3 looping=%4 snaps=%5 elapsed=%6", rain, m_TimeManager.GetWindSpeed(), StateName(), m_TimeManager.IsWeatherLooping(), ETW_WeatherRunner.GetCloudSnaps() - m_iSnapsBefore, elapsed);
  m_bFinishedPass = Check(!running && rainEnd && windEnd && stateEnd && m_TimeManager.IsWeatherLooping(), "transition finished: rain and wind at target, target weather reached and held");

  // Interrupt: a transition back to the start weather, replaced 20 s in.
  float rainBack = 0;
  int choiceBack = 1;
  if (m_fRainTarget < 0.5)
  {
   rainBack = 1;
   choiceBack = 6;
  }
  ConfigureWeather(m_sStateA, choiceBack, 0);
  Check(ETW_WeatherRunner.StartFromModule(m_WeatherModule, -1) && ETW_WeatherRunner.GetChannelMode(ETW_WeatherRunner.CH_WIND_SPEED) == ETW_WeatherRunner.MODE_RELEASE, string.Format("transition back to %1 started (rain to %2, wind handed back)", m_sStateA, rainBack));
  m_iPhase = 6;
  Wait(20);
 }

 //------------------------------------------------------------------------------------------------
 void Interrupt()
 {
  m_fRainBeforeInterrupt = m_TimeManager.GetRainIntensity();
  m_iStartsBeforeInterrupt = ETW_WeatherRunner.GetStarts();
  m_WeatherModule.ApplySetting(ETW_Settings.W_RAIN, 4);
  m_bRestarted = ETW_WeatherRunner.StartFromModule(m_WeatherModule, -1);
  PrintFormat("[ETW INTERRUPT] rainBefore=%1 rainNow=%2 restarted=%3 elapsed01=%4", m_fRainBeforeInterrupt, m_TimeManager.GetRainIntensity(), m_bRestarted, ETW_WeatherRunner.Elapsed01());
  m_iPhase = 7;
  Wait(1);
 }

 //------------------------------------------------------------------------------------------------
 void VerifyInterruptSkip()
 {
  float rain = m_TimeManager.GetRainIntensity();
  bool continued = Math.AbsFloat(rain - m_fRainBeforeInterrupt) < 0.05;
  bool restarted = m_bRestarted && ETW_WeatherRunner.GetStarts() - m_iStartsBeforeInterrupt == 1 && ETW_WeatherRunner.IsRunning() && ETW_WeatherRunner.Elapsed01() < 0.1;
  PrintFormat("[ETW INTERRUPT] rainBefore=%1 rainAfter1s=%2 elapsed01=%3", m_fRainBeforeInterrupt, rain, ETW_WeatherRunner.Elapsed01());
  m_bInterruptPass = Check(continued && restarted, "a replaced transition continues from the rain reached, with a restarted clock");
  ConfigureSkip(1, 0);
  Check(ETW_TimeSkip.Start(m_SkipModule, -1), "time skip started during the transition");
  m_iSkipTick = System.GetTickCount();
  m_iPhase = 8;
  Wait(2.6);
 }

 //------------------------------------------------------------------------------------------------
 void VerifySkipFinishStartForeign()
 {
  if (SkipPending())
   return;
  float rain = m_TimeManager.GetRainIntensity();
  bool done = !ETW_WeatherRunner.IsRunning() && Math.AbsFloat(rain - 0.5) < 0.02 && StateName() == m_sStateA;
  PrintFormat("[ETW SKIP FINISH] running=%1 rain=%2 state=%3 status=%4", ETW_WeatherRunner.IsRunning(), rain, StateName(), ETW_WeatherRunner.GetStatus());
  m_bSkipFinishPass = Check(done && !ETW_TimeSkip.IsRunning(), "the time skip completed the running transition under the black screen");
  Check(ETW_TimeSkip.GetBroadcasts() - m_iBroadcastsAtStart == 3 && ETW_FadeOverlay.GetReceived() - m_iReceivedAtStart == 3, "three skips: three broadcasts, three local handler calls");
  ConfigureWeather(m_sStateB, 3, 0);
  m_iForeignBefore = ETW_WeatherRunner.GetForeignStops();
  Check(ETW_WeatherRunner.StartFromModule(m_WeatherModule, -1), "transition started for the foreign change case");
  m_iPhase = 9;
  Wait(3);
 }

 //------------------------------------------------------------------------------------------------
 void Foreign()
 {
  bool overriddenBefore = m_TimeManager.IsRainIntensityOverridden();
  m_TimeManager.ForceWeatherTo(true, m_sStateA);
  bool stopped = !ETW_WeatherRunner.IsRunning() && ETW_WeatherRunner.GetForeignStops() - m_iForeignBefore == 1;
  bool released = !m_TimeManager.IsRainIntensityOverridden();
  PrintFormat("[ETW FOREIGN] overriddenBefore=%1 stopped=%2 released=%3 status=%4", overriddenBefore, stopped, released, ETW_WeatherRunner.GetStatus());
  m_bForeignPass = Check(overriddenBefore && stopped && released, "ForceWeatherTo stops the transition and hands rain back to the weather");
  m_TimeManager.SetWindSpeedOverride(false);
  m_TimeManager.SetWindDirectionOverride(false);
  m_TimeManager.SetFogAmountOverride(false);
  Finish("complete");
 }
}
