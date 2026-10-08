// TEST ONLY. EXPBG Time and Weather: the Weather Transition blends the clouds through the
// engine (no end-of-transition snap) and everything arrives together. Dedicated fixture
// server, no players, real Weather Transition module prefab, public server entry points.
// pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -FixturePath tests/ETW_CloudBlendGameplay.c -TimeoutSeconds 600 -OrchestratorSlotGranted -ExpectResult '\[ETW BLEND RESULT\] checks=[1-9]\d* failures=0 smooth=1 heading=1 together=1 replace=1 pinned=1 stop=1 automatic=1 foreign=1 path=\S+ early=\S+ paused=\w+ pinJump=\S+ reason=complete'
// The runner copies this file to EXPG_GarrisonGameplay.c; the class names are fixed. Judge
// the run by its one [ETW BLEND RESULT] line (-ExpectResult also rejects script errors).
// The day is 24 real minutes (SetDayDuration(1440): one in-game minute per real second), so
// the engine's 10-in-game-minute cloud minimum is 10 s and a 1-minute transition fits. A is
// the start weather (Clear when the terrain has it), B another one (Rainy when there is one),
// C a third one when the terrain has one.
//  smooth     A held for HOLD_WAIT (15 s, longer than the 10 s hold ForceWeatherTo leaves),
//             then a 1-minute transition A -> B (rain to 100% or 0%, wind 8 m/s, hold)
//             reaches B with no cloud snap, the clouds seen blending through the engine.
//  heading    while it runs the clouds blend (runner stage BLENDING) and the engine's next
//             weather is B.
//  together   rain has moved at most 20% when the clouds start blending, the planned end is
//             within 3 s of the cloud end, and at the end rain is at target with B reached.
//  replace    a transition to A started right after B arrived (inside B's hold), replaced
//             12 s in by one to B, ends on B without a snap.
//  pinned     rain and fog left to the weather: a transition to A replaced in the same frame
//             by one to C pins the clouds on B (a node of ours still queued); C is reached
//             through the engine blend without a snap (two-state terrain: the replacement
//             keeps B, set at once). The pin check: rain and fog moved less than 0.05 (plus
//             their drift in the second before) in the second after the pin.
//  stop       "Stop here and hold" 12 s into a transition: stopped, held, and the weather
//             does not change in the next 15 s (no node of ours left queued).
//  automatic  "Return to automatic weather" pressed while a transition's clouds blend (within
//             30 s of its start): the blend goes on (kept clouds never jump) and finishes on
//             its target, rain handed back, weather no longer held, no snap.
//  foreign    ForceWeatherTo (Scenario Properties weather) stops the transition and hands
//             rain back.
//  path       evidence: how the clouds of the smooth case started (expected direct).
//  early      evidence: how the clouds of the replace case's first transition started
//             inside B's hold, and when (e.g. direct@10s; <path>_waiting when not yet).
//  paused     evidence: with day auto-advance off the clouds blend (blend) or wait and are
//             set at the end (wait); B is reached either way.
//  pinJump    evidence: the largest move of rain or fog across the pin (pinned case).
// Deadline 520 s of world time (Run-Gameplay allows at most -TimeoutSeconds 600).
class EXPG_GarrisonGameplayClass : GenericEntityClass {}
class EXPG_GarrisonGameplay : GenericEntity
{
 // About 450 s expected (setup 25, smooth 62, replace 82, pinned 68, stop 27, automatic 90,
 // foreign 6, paused 61-91), leaving room for several direct-then-pin fallbacks (12 s each)
 // or late cloud blends (up to 30 s grace each). Boot (20-70 s seen) and shutdown still fit
 // inside Run-Gameplay's largest timeout of 600 s.
 static const float FIXTURE_SECONDS = 520;
 static const float DAY_SECONDS = 1440;
 static const float HOLD_WAIT = 15;
 static const float RUN_LIMIT = 150;
 static const float PIN_JUMP_LIMIT = 0.05;
 static const ResourceName WEATHER_MODULE = "{B291F78787B9F4B0}PrefabsEditable/EXPTW/ETW_WeatherTransition.et";
 vector m_vOrigin = "4773.46 0 7094.57";
 ETW_WeatherModule m_WeatherModule;
 TimeAndWeatherManagerEntity m_TimeManager;
 int m_iPhase;
 int m_iChecks;
 int m_iFailures;
 float m_fStarted;
 float m_fNext;
 float m_fPhaseAt;
 bool m_bFinished;
 float m_fDayLength;
 bool m_bAutoAdvance;
 string m_sA;
 string m_sB;
 string m_sC;
 int m_iSnapsBefore;
 int m_iBlendsBefore;
 int m_iStartsBefore;
 int m_iForeignBefore;
 int m_iPinsBefore;
 float m_fRainStart;
 float m_fRainTarget;
 float m_fLastProgress;
 float m_fRainAtClouds = -1;
 bool m_bMonotonic = true;
 string m_sStopFrom;
 string m_sStopState;
 string m_sAutoTarget;
 bool m_bAutoOverridden;
 bool m_bAutoWhileBlending;
 string m_sPinnedTarget;
 float m_fRainPre;
 float m_fFogPre;
 float m_fRainPin;
 float m_fFogPin;
 float m_fPinDrift;
 float m_fPinJump = -1;
 int m_iJumpSamples;
 bool m_bPausedBlend;
 bool m_bSmooth;
 bool m_bHeading;
 bool m_bTogether;
 bool m_bReplace;
 bool m_bPinned;
 bool m_bStop;
 bool m_bAutomatic;
 bool m_bForeign;
 string m_sPath = "none";
 string m_sEarly = "none";
 string m_sPaused = "none";

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
  PrintFormat("[ETW BLEND BEGIN] origin=%1 deadline=%2", m_vOrigin, FIXTURE_SECONDS);
 }

 //------------------------------------------------------------------------------------------------
 bool Check(bool ok, string label)
 {
  m_iChecks++;
  if (!ok)
   m_iFailures++;
  PrintFormat("[ETW BLEND CHECK] pass=%1 %2", ok, label);
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
 void PauseFor(float seconds)
 {
  m_fNext = Now() + seconds;
 }

 //------------------------------------------------------------------------------------------------
 // A runner cloud path as one word ("direct then pinned" -> direct_then_pinned).
 string Word(string text)
 {
  string word = text;
  word.Replace(", ", "_");
  word.Replace(" ", "_");
  return word;
 }

 //------------------------------------------------------------------------------------------------
 void Finish(string reason)
 {
  if (m_bFinished)
   return;
  m_bFinished = true;
  ClearEventMask(EntityEvent.FRAME);
  if (m_TimeManager)
  {
   m_TimeManager.SetIsDayAutoAdvanced(m_bAutoAdvance);
   if (m_fDayLength > 0)
    m_TimeManager.SetDayDuration(m_fDayLength);
  }
  string head = string.Format("[ETW BLEND RESULT] checks=%1 failures=%2 smooth=%3 heading=%4 together=%5 replace=%6 pinned=%7 stop=%8 automatic=%9", m_iChecks, m_iFailures, Flag(m_bSmooth), Flag(m_bHeading), Flag(m_bTogether), Flag(m_bReplace), Flag(m_bPinned), Flag(m_bStop), Flag(m_bAutomatic));
  string tail = string.Format(" foreign=%1 path=%2 early=%3 paused=%4 pinJump=%5 reason=%6", Flag(m_bForeign), Word(m_sPath), Word(m_sEarly), m_sPaused, m_fPinJump, reason);
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
 string NextStateName()
 {
  BaseWeatherStateTransitionManager transitions = m_TimeManager.GetTransitionManager();
  if (!transitions)
   return string.Empty;
  WeatherState state = transitions.GetNextState();
  if (!state)
   return string.Empty;
  return state.GetStateName();
 }

 //------------------------------------------------------------------------------------------------
 // A weather blend in flight (the first queue entries and the node in place).
 bool AnyMoving()
 {
  BaseWeatherStateTransitionManager transitions = m_TimeManager.GetTransitionManager();
  if (!transitions)
   return false;
  int count = transitions.GetStateTransitionsCount();
  if (count > 4)
   count = 4;
  for (int i = 0; i < count; i++)
  {
   WeatherStateTransitionNode node = transitions.GetStateTransitionNode(i);
   if (node && node.IsTransitioning())
    return true;
  }
  WeatherStateTransitionNode head = transitions.GetCurrentStateTransitionNode();
  return head != null && head.IsTransitioning();
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
 // One sample line of the running transition.
 void Sample(string tag)
 {
  PrintFormat("[ETW BLEND SAMPLE] %1 t=%2 running=%3 stage=%4 state=%5 next=%6 rain=%7 path=%8 status=%9", tag, Now() - m_fPhaseAt, ETW_WeatherRunner.IsRunning(), ETW_WeatherRunner.GetCloudStage(), StateName(), NextStateName(), m_TimeManager.GetRainIntensity(), ETW_WeatherRunner.GetCloudPath(), ETW_WeatherRunner.GetStatus());
 }

 //------------------------------------------------------------------------------------------------
 // Still running and within the limit: look again in a second.
 bool StillRunning()
 {
  if (!ETW_WeatherRunner.IsRunning() || Now() - m_fPhaseAt > RUN_LIMIT)
   return false;
  PauseFor(1);
  return true;
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
   StartBlend();
  else if (m_iPhase == 2)
   SampleBlend();
  else if (m_iPhase == 3)
   Replace();
  else if (m_iPhase == 4)
   SampleReplace();
  else if (m_iPhase == 5)
   BeforePin();
  else if (m_iPhase == 6)
   StartPinned();
  else if (m_iPhase == 7)
   SamplePinJump();
  else if (m_iPhase == 8)
   SamplePinned();
  else if (m_iPhase == 9)
   StopNow();
  else if (m_iPhase == 10)
   VerifyStop();
  else if (m_iPhase == 11)
   AutomaticWhileBlending();
  else if (m_iPhase == 12)
   SampleAutomatic();
  else if (m_iPhase == 13)
   Foreign();
  else if (m_iPhase == 14)
   StartPaused();
  else if (m_iPhase == 15)
   SamplePaused();
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
  Check(GetGame().GetPlayerManager().GetPlayerCount() == 0 && !ETW_WeatherRunner.IsRunning(), "isolated server: no players, no transition");
  Resource weatherPrefab = Resource.Load(WEATHER_MODULE);
  IEntity weatherEntity = GetGame().SpawnEntityPrefab(weatherPrefab, GetGame().GetWorld(), Params(m_vOrigin));
  m_WeatherModule = ETW_WeatherModule.Cast(weatherEntity);
  if (!Check(m_WeatherModule != null, "Weather Transition prefab spawned"))
  {
   Finish("setup");
   return;
  }
  array<ref WeatherState> states = {};
  m_TimeManager.GetWeatherStatesList(states);
  m_sA = string.Empty;
  m_sB = string.Empty;
  m_sC = string.Empty;
  foreach (WeatherState state : states)
  {
   if (state && (m_sA.IsEmpty() || state.GetStateName() == "Clear"))
    m_sA = state.GetStateName();
  }
  foreach (WeatherState other : states)
  {
   if (!other || other.GetStateName() == m_sA)
    continue;
   if (m_sB.IsEmpty() || other.GetStateName() == "Rainy")
    m_sB = other.GetStateName();
  }
  foreach (WeatherState third : states)
  {
   if (third && m_sC.IsEmpty() && third.GetStateName() != m_sA && third.GetStateName() != m_sB)
    m_sC = third.GetStateName();
  }
  if (!Check(!m_sA.IsEmpty() && !m_sB.IsEmpty(), string.Format("two weather states: A %1, B %2, C %3", m_sA, m_sB, m_sC)))
  {
   Finish("setup");
   return;
  }
  m_bAutoAdvance = m_TimeManager.GetIsDayAutoAdvanced();
  m_fDayLength = m_TimeManager.GetDayDuration();
  m_TimeManager.SetIsDayAutoAdvanced(true);
  Check(m_TimeManager.SetDayDuration(DAY_SECONDS), "day length 1440 s (one in-game minute per real second)");
  // Held on A, as after a Scenario Properties weather change, for longer than the hold
  // ForceWeatherTo leaves: the smooth case is the normal production case (direct start).
  m_TimeManager.ForceWeatherTo(true, m_sA);
  m_iPhase = 1;
  PauseFor(HOLD_WAIT);
 }

 //------------------------------------------------------------------------------------------------
 void StartBlend()
 {
  Check(StateName() == m_sA, "start weather is A");
  m_fRainStart = m_TimeManager.GetRainIntensity();
  int rainChoice = 6;
  m_fRainTarget = 1;
  if (m_fRainStart >= 0.5)
  {
   rainChoice = 1;
   m_fRainTarget = 0;
  }
  ConfigureWeather(m_sB, rainChoice, 5);
  m_iSnapsBefore = ETW_WeatherRunner.GetCloudSnaps();
  m_iBlendsBefore = ETW_WeatherRunner.GetCloudBlends();
  m_fPhaseAt = Now();
  bool started = ETW_WeatherRunner.StartFromModule(m_WeatherModule, -1);
  Check(started && ETW_WeatherRunner.IsRunning() && ETW_WeatherRunner.GetTarget() == m_sB, string.Format("transition %1 -> %2 started (rain to %3, wind 8 m/s)", m_sA, m_sB, m_fRainTarget));
  Sample("start");
  m_iPhase = 2;
  PauseFor(1);
 }

 //------------------------------------------------------------------------------------------------
 void SampleBlend()
 {
  Sample("blend");
  float rain = m_TimeManager.GetRainIntensity();
  float progressed = (rain - m_fRainStart) / (m_fRainTarget - m_fRainStart);
  if (ETW_WeatherRunner.IsRunning())
  {
   if (progressed < m_fLastProgress - 0.01)
    m_bMonotonic = false;
   m_fLastProgress = progressed;
   if (ETW_WeatherRunner.GetCloudStage() == ETW_WeatherRunner.CLOUD_BLENDING)
   {
    if (m_fRainAtClouds < 0)
     m_fRainAtClouds = progressed;
    if (NextStateName() == m_sB)
     m_bHeading = true;
   }
  }
  if (StillRunning())
   return;
  bool running = ETW_WeatherRunner.IsRunning();
  bool reached = !running && StateName() == m_sB && m_TimeManager.IsWeatherLooping();
  bool rainEnd = Math.AbsFloat(m_TimeManager.GetRainIntensity() - m_fRainTarget) < 0.02 && Math.AbsFloat(m_TimeManager.GetWindSpeed() - 8) < 0.1;
  int snaps = ETW_WeatherRunner.GetCloudSnaps() - m_iSnapsBefore;
  int blends = ETW_WeatherRunner.GetCloudBlends() - m_iBlendsBefore;
  float endGap = Math.AbsFloat(ETW_WeatherRunner.GetCloudEnd() - ETW_WeatherRunner.GetPlannedEnd());
  m_sPath = ETW_WeatherRunner.GetCloudPath();
  PrintFormat("[ETW BLEND FINISHED] state=%1 looping=%2 snaps=%3 blends=%4 rainAtClouds=%5 endGap=%6 monotonic=%7 path=%8 elapsed=%9", StateName(), m_TimeManager.IsWeatherLooping(), snaps, blends, m_fRainAtClouds, endGap, m_bMonotonic, m_sPath, Now() - m_fPhaseAt);
  m_bSmooth = Check(reached && snaps == 0 && blends == 1, "B reached and held through the engine blend, no cloud snap");
  m_bHeading = Check(m_bHeading, "while running, the clouds blended with the engine's next weather B");
  m_bTogether = Check(m_fRainAtClouds >= 0 && m_fRainAtClouds <= 0.2 && endGap <= 3 && rainEnd && m_bMonotonic, "rain and wind moved with the clouds and arrived with them");

  // Replace: a transition to A right after B arrived (inside B's hold), replaced 12 s in by
  // one to B.
  int rainBack = 1;
  if (m_fRainTarget < 0.5)
   rainBack = 6;
  ConfigureWeather(m_sA, rainBack, 0);
  m_iSnapsBefore = ETW_WeatherRunner.GetCloudSnaps();
  m_iStartsBefore = ETW_WeatherRunner.GetStarts();
  m_fPhaseAt = Now();
  Check(ETW_WeatherRunner.StartFromModule(m_WeatherModule, -1), "transition back to A started");
  m_iPhase = 3;
  PauseFor(12);
 }

 //------------------------------------------------------------------------------------------------
 void Replace()
 {
  Sample("before replace");
  int stage = ETW_WeatherRunner.GetCloudStage();
  if (stage == ETW_WeatherRunner.CLOUD_BLENDING || stage == ETW_WeatherRunner.CLOUD_ARRIVED)
  {
   int after = Math.Round(ETW_WeatherRunner.GetCloudStart() - m_fPhaseAt);
   m_sEarly = ETW_WeatherRunner.GetCloudPath() + "@" + after.ToString() + "s";
  }
  else
   m_sEarly = ETW_WeatherRunner.GetCloudPath() + " waiting";
  PrintFormat("[ETW BLEND EARLY] %1 (the clouds of a transition started inside B's hold)", m_sEarly);
  ConfigureWeather(m_sB, 4, 0);
  Check(ETW_WeatherRunner.StartFromModule(m_WeatherModule, -1) && ETW_WeatherRunner.GetTarget() == m_sB, "replaced 12 s in by a transition to B");
  m_fPhaseAt = Now();
  m_iPhase = 4;
  PauseFor(1);
 }

 //------------------------------------------------------------------------------------------------
 void SampleReplace()
 {
  Sample("replace");
  if (StillRunning())
   return;
  int snaps = ETW_WeatherRunner.GetCloudSnaps() - m_iSnapsBefore;
  m_bReplace = Check(!ETW_WeatherRunner.IsRunning() && StateName() == m_sB && snaps == 0 && ETW_WeatherRunner.GetStarts() - m_iStartsBefore == 2, string.Format("the replacing transition ended on B without a snap (snaps %1, path %2)", snaps, ETW_WeatherRunner.GetCloudPath()));

  // Pinned: rain and fog are left to the weather from here on.
  m_TimeManager.SetRainIntensityOverride(false);
  m_TimeManager.SetFogAmountOverride(false);
  m_iPhase = 5;
  PauseFor(5);
 }

 //------------------------------------------------------------------------------------------------
 // Natural rain and fog a second before the pin (their drift is allowed for).
 void BeforePin()
 {
  m_fRainPre = m_TimeManager.GetRainIntensity();
  m_fFogPre = m_TimeManager.GetFogAmount();
  m_iPhase = 6;
  PauseFor(1);
 }

 //------------------------------------------------------------------------------------------------
 // A transition to A replaced in the same frame by one to C: its node is still queued, so
 // the runner pins the clouds on B (two-state terrain: the replacement keeps B, set at once).
 void StartPinned()
 {
  m_fRainPin = m_TimeManager.GetRainIntensity();
  m_fFogPin = m_TimeManager.GetFogAmount();
  m_fPinDrift = Math.Max(Math.AbsFloat(m_fRainPin - m_fRainPre), Math.AbsFloat(m_fFogPin - m_fFogPre));
  m_sPinnedTarget = m_sC;
  if (m_sPinnedTarget.IsEmpty())
   m_sPinnedTarget = m_sB;
  m_iSnapsBefore = ETW_WeatherRunner.GetCloudSnaps();
  m_iPinsBefore = ETW_WeatherRunner.GetPins();
  ConfigureWeather(m_sA, 0, 0);
  bool first = ETW_WeatherRunner.StartFromModule(m_WeatherModule, -1);
  ConfigureWeather(m_sPinnedTarget, 0, 0);
  bool second = ETW_WeatherRunner.StartFromModule(m_WeatherModule, -1);
  bool untouched = ETW_WeatherRunner.GetChannelMode(ETW_WeatherRunner.CH_RAIN) == ETW_WeatherRunner.MODE_NONE && ETW_WeatherRunner.GetChannelMode(ETW_WeatherRunner.CH_FOG) == ETW_WeatherRunner.MODE_NONE;
  if (ETW_WeatherRunner.HOLD_ACROSS_PIN)
   untouched = true;
  bool pinned = ETW_WeatherRunner.GetPins() - m_iPinsBefore >= 1 || m_sC.IsEmpty();
  Check(first && second && untouched && pinned, string.Format("transition to A replaced at once by one to %1: clouds pinned on B (pins %2), rain and fog left to the weather", m_sPinnedTarget, ETW_WeatherRunner.GetPins() - m_iPinsBefore));
  Sample("pinned");
  m_fPinJump = 0;
  m_iJumpSamples = 0;
  m_fPhaseAt = Now();
  m_iPhase = 7;
  PauseFor(0.5);
 }

 //------------------------------------------------------------------------------------------------
 void SamplePinJump()
 {
  float rain = m_TimeManager.GetRainIntensity();
  float fog = m_TimeManager.GetFogAmount();
  m_fPinJump = Math.Max(m_fPinJump, Math.Max(Math.AbsFloat(rain - m_fRainPin), Math.AbsFloat(fog - m_fFogPin)));
  m_iJumpSamples++;
  PrintFormat("[ETW BLEND PIN] t=%1 rain %2 -> %3 fog %4 -> %5 jump=%6 drift=%7 runner=%8", Now() - m_fPhaseAt, m_fRainPin, rain, m_fFogPin, fog, m_fPinJump, m_fPinDrift, ETW_WeatherRunner.GetPinJump());
  if (m_iJumpSamples < 2)
  {
   PauseFor(0.5);
   return;
  }
  Check(m_fPinJump < PIN_JUMP_LIMIT + m_fPinDrift, string.Format("rain and fog left to the weather did not jump across the pin (moved %1, drift %2)", m_fPinJump, m_fPinDrift));
  m_iPhase = 8;
  PauseFor(1);
 }

 //------------------------------------------------------------------------------------------------
 void SamplePinned()
 {
  Sample("pinned");
  if (StillRunning())
   return;
  int snaps = ETW_WeatherRunner.GetCloudSnaps() - m_iSnapsBefore;
  string path = ETW_WeatherRunner.GetCloudPath();
  bool reached = !ETW_WeatherRunner.IsRunning() && StateName() == m_sPinnedTarget && snaps == 0;
  if (!m_sC.IsEmpty())
   reached = reached && path.Contains("pinned");
  m_bPinned = Check(reached, string.Format("the pinned transition reached %1 without a snap (path %2, snaps %3)", m_sPinnedTarget, path, snaps));

  // Stop here and hold, 12 s into a transition to A.
  m_sStopFrom = StateName();
  ConfigureWeather(m_sA, 2, 0);
  m_iSnapsBefore = ETW_WeatherRunner.GetCloudSnaps();
  Check(ETW_WeatherRunner.StartFromModule(m_WeatherModule, -1), "transition to A started for the stop case");
  m_fPhaseAt = Now();
  m_iPhase = 9;
  PauseFor(12);
 }

 //------------------------------------------------------------------------------------------------
 void StopNow()
 {
  Sample("before stop");
  bool stopped = ETW_WeatherRunner.Stop(-1);
  m_sStopState = StateName();
  Check(stopped && !ETW_WeatherRunner.IsRunning() && m_TimeManager.IsWeatherLooping(), string.Format("stopped and held on %1", m_sStopState));
  m_fPhaseAt = Now();
  m_iPhase = 10;
  PauseFor(15);
 }

 //------------------------------------------------------------------------------------------------
 void VerifyStop()
 {
  Sample("after stop");
  int snaps = ETW_WeatherRunner.GetCloudSnaps() - m_iSnapsBefore;
  bool known = m_sStopState == m_sA || m_sStopState == m_sStopFrom;
  bool stays = known && StateName() == m_sStopState && !AnyMoving();
  m_bStop = Check(stays && snaps == 0, string.Format("the stopped weather %1 stays for 15 s with nothing blending (no node of ours left queued), no snap", m_sStopState));

  // A transition with rain left to the weather (rain is overridden by the stop case), then
  // Return to automatic weather once its clouds blend.
  m_bAutoOverridden = m_TimeManager.IsRainIntensityOverridden();
  m_sAutoTarget = m_sB;
  if (StateName() == m_sB)
   m_sAutoTarget = m_sA;
  ConfigureWeather(m_sAutoTarget, 0, 0);
  m_iSnapsBefore = ETW_WeatherRunner.GetCloudSnaps();
  Check(ETW_WeatherRunner.StartFromModule(m_WeatherModule, -1), string.Format("transition to %1 started for the automatic case", m_sAutoTarget));
  m_fPhaseAt = Now();
  m_iPhase = 11;
  PauseFor(1);
 }

 //------------------------------------------------------------------------------------------------
 // Return to automatic weather as soon as the clouds blend (at most 30 s): kept clouds, so the
 // blend under way goes on.
 void AutomaticWhileBlending()
 {
  bool blending = ETW_WeatherRunner.GetCloudStage() == ETW_WeatherRunner.CLOUD_BLENDING;
  if (!blending && ETW_WeatherRunner.IsRunning() && Now() - m_fPhaseAt < 30)
  {
   PauseFor(1);
   return;
  }
  Sample("before automatic");
  m_bAutoWhileBlending = blending;
  bool started = ETW_WeatherRunner.StartAutomatic(m_WeatherModule, -1);
  bool kept = ETW_WeatherRunner.GetTarget() == m_sAutoTarget && ETW_WeatherRunner.GetCloudStage() == ETW_WeatherRunner.CLOUD_BLENDING;
  bool rainBack = ETW_WeatherRunner.GetChannelMode(ETW_WeatherRunner.CH_RAIN) == ETW_WeatherRunner.MODE_RELEASE;
  Check(m_bAutoOverridden && blending && started && kept && rainBack, string.Format("automatic weather requested while the clouds blend to %1 (blending %2 after %3 s): the blend goes on, rain handed back", m_sAutoTarget, blending, Now() - m_fPhaseAt));
  m_fPhaseAt = Now();
  m_iPhase = 12;
  PauseFor(1);
 }

 //------------------------------------------------------------------------------------------------
 void SampleAutomatic()
 {
  Sample("automatic");
  if (StillRunning())
   return;
  int snaps = ETW_WeatherRunner.GetCloudSnaps() - m_iSnapsBefore;
  bool reached = m_bAutoWhileBlending && StateName() == m_sAutoTarget;
  m_bAutomatic = Check(!ETW_WeatherRunner.IsRunning() && reached && !m_TimeManager.IsRainIntensityOverridden() && !m_TimeManager.IsWeatherLooping() && snaps == 0, string.Format("automatic weather: the cloud blend under way finished on %1, rain handed back, weather no longer held, no snap (snaps %2)", m_sAutoTarget, snaps));

  // Foreign change during a transition.
  ConfigureWeather(m_sB, 3, 0);
  m_iForeignBefore = ETW_WeatherRunner.GetForeignStops();
  Check(ETW_WeatherRunner.StartFromModule(m_WeatherModule, -1), "transition started for the foreign change case");
  m_fPhaseAt = Now();
  m_iPhase = 13;
  PauseFor(3);
 }

 //------------------------------------------------------------------------------------------------
 void Foreign()
 {
  bool overriddenBefore = m_TimeManager.IsRainIntensityOverridden();
  m_TimeManager.ForceWeatherTo(true, m_sA);
  bool stopped = !ETW_WeatherRunner.IsRunning() && ETW_WeatherRunner.GetForeignStops() - m_iForeignBefore == 1;
  bool released = !m_TimeManager.IsRainIntensityOverridden();
  m_bForeign = Check(overriddenBefore && stopped && released, "ForceWeatherTo stops the transition and hands rain back");
  m_TimeManager.SetWindSpeedOverride(false);
  m_TimeManager.SetWindDirectionOverride(false);
  m_TimeManager.SetFogAmountOverride(false);
  m_TimeManager.SetIsDayAutoAdvanced(false);
  m_iPhase = 14;
  PauseFor(3);
 }

 //------------------------------------------------------------------------------------------------
 void StartPaused()
 {
  ConfigureWeather(m_sB, 6, 0);
  m_bPausedBlend = false;
  Check(ETW_WeatherRunner.StartFromModule(m_WeatherModule, -1), "transition started with time paused");
  m_fPhaseAt = Now();
  m_iPhase = 15;
  PauseFor(1);
 }

 //------------------------------------------------------------------------------------------------
 void SamplePaused()
 {
  Sample("paused");
  if (ETW_WeatherRunner.GetCloudStage() == ETW_WeatherRunner.CLOUD_BLENDING)
   m_bPausedBlend = true;
  if (StillRunning())
   return;
  m_sPaused = "wait";
  if (m_bPausedBlend)
   m_sPaused = "blend";
  Check(!ETW_WeatherRunner.IsRunning() && StateName() == m_sB, string.Format("with time paused B is still reached (%1)", m_sPaused));
  m_TimeManager.SetIsDayAutoAdvanced(true);
  m_TimeManager.SetRainIntensityOverride(false);
  Finish("complete");
 }
}
