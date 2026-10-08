// EXPBG Time and Weather: the one weather transition of the session (server only).
// Clouds blend through the engine's own weather state transition queue. The engine cannot
// blend from the middle of a running blend, raises a node's blend and hold to at least
// 10 in-game minutes, and starts a queued node only once the node in place stops looping
// and its hold is over. RequestStateTransition() on its own restarted the weather already
// in place (0.1.14 native fixtures: smooth=0, clouds set at the end). A cloud change
// therefore starts one of two ways:
//  direct  our node goes right behind the node in place, which stops looping and gets the
//          shortest hold, so the engine blends to ours once that hold is over: at once when
//          the weather in place is older than the hold, else after the hold left over
//          (read back from the engine; at most the 10-in-game-minute floor).
//  pinned  when the direct start has not taken by then (DIRECT_MARGIN_S later), something
//          else starts blending or our node is no longer next, or when a blend is running:
//          a "pin" node aimed at the weather in place (the nearer one of a running blend)
//          and our node go to the back of the queue and the pin is set at once.
//          RequestStateTransitionImmediately drops every node ahead of it (as vanilla
//          ForceWeatherTo does), so the clouds stay as they are and our node blends after
//          the pin's hold. When our node is then first in the queue, RequestStateTransition()
//          is asked once to start it now; a request that moves anything else is undone by
//          one more pin. A pin takes precedence over another weather source once; if our
//          node is lost after it, the clouds are left to that source.
// Node durations are read back after queueing. The engine raises a new node's blend to its
// minimum; a queued node's blend is shortened only through SetBlend, never below that
// minimum (or the requested time when shorter), so no setter can ask for an instant switch
// whether the engine raises it or not. Rain, fog and wind ease from the moment the
// clouds start to the moment they arrive, so everything arrives together; the transition
// takes longer than set when the engine's minimum requires it (the status says so).
// A blend in flight cannot be redirected: a new target completes it at once on the nearer
// weather. Keeping the clouds (no target, "Return to automatic weather") never jumps: a
// blend of ours goes on and one of the weather's own is left to run.
// Nothing here calls ForceWeatherTo, so any ForceWeatherTo (Scenario Properties weather,
// mission load, another mod) is a foreign change that stops the transition (ETW_Hooks.c).
// Nodes are never removed by hand (RemoveStateTransition crashed the server natively).
// State lives in statics; the stored weather manager is a weak reference, so a transition
// never leaks into the next mission.
class ETW_WeatherRunner
{
 static const int TICK_MS = 500;
 static const int CHANNELS = 4;
 static const int CH_RAIN = 0;
 static const int CH_FOG = 1;
 static const int CH_WIND_SPEED = 2;
 static const int CH_WIND_DIR = 3;
 // Channel modes: untouched (left to the weather), blended to a fixed value and kept,
 // or overridden now and handed back to the weather at the end.
 static const int MODE_NONE = 0;
 static const int MODE_SET = 1;
 static const int MODE_RELEASE = 2;
 // Cloud stages of the running transition.
 static const int CLOUD_NONE = 0;
 static const int CLOUD_DIRECT = 1;
 static const int CLOUD_PINNED = 2;
 static const int CLOUD_KICKED = 3;
 static const int CLOUD_BLENDING = 4;
 static const int CLOUD_ARRIVED = 5;
 // Extra real seconds the engine may take to finish the cloud blend before it is set.
 static const float CLOUD_GRACE_S = 30;
 // Real seconds a direct start may take beyond the hold left over before the pinned start
 // replaces it.
 static const float DIRECT_MARGIN_S = 2;
 // Longest hold left over (in-game hours) a direct start waits out: the engine's
 // 10-in-game-minute floor (0.167 h) with a margin. Bounds a hold read back as longer.
 static const float DIRECT_MAX_HOURS = 0.2;
 // A direct start waits out the hold left over of the weather in place (the native cloud
 // probe reports directEarly=<seconds>). Set false if it reports directEarly=never: a
 // direct start is then only tried when the hold is over, otherwise the clouds pin at once.
 static const bool DIRECT_WAITS_HOLD = true;
 // Queue our node behind the weather in place at all (Direct). Off: the native cloud probe
 // (2026-10-08, Reforger 1.8) reported direct=misdirected for every direct case: the engine
 // moved to another weather instead of ours, which looked like a switch and a reset. Every
 // transition with a cloud change pins (Pin), the path the cloud blend fixture proves.
 static const bool DIRECT_START = false;
 // Asked-for node duration meaning "as short as possible": the engine raises it to its
 // minimum and the duration it kept is read back from the node.
 static const float SHORTEST_HOURS = 0.001;
 // The engine's minimum cloud blend in in-game hours (10 in-game minutes), as seen on nodes
 // it queues (native logs, Dynamic Weather Mod notes); replaced by the minimum the engine
 // shows when it raises a node of ours. Whether SetTransitionDurationHours on a node already
 // queued is raised to it as well is unknown (the cloud probe reports setBlend), so a blend
 // is never shortened below it, or below the requested time when that is shorter (SetBlend).
 static const float BLEND_FLOOR_HOURS = 0.167;
 // After a pin, ask the engine once to start our node when it is first in the queue
 // (saves the pin's hold). Set false if the native cloud probe reports kick=misdirected.
 static const bool START_REQUEST = true;
 // Rain and fog left to the weather keep their value across a pin, then ease to the target
 // weather's typical value and are handed back at the end. Off: the pin leaves them to the
 // weather; set true if the native cloud blend fixture reports a pin jump (pinJump) of 0.05
 // or more (the weather in place restarting on its start variant).
 static const bool HOLD_ACROSS_PIN = false;
 // Queue entries looked at when searching for our node (bounded).
 static const int QUEUE_SCAN = 8;
 // Queue dumps per transition (diagnostics, bounded).
 static const int QUEUE_LOGS = 3;
 // Consecutive ticks without our node (and nothing heading to the target) before the
 // clouds are left to whatever rebuilt the queue.
 static const int LOST_TICKS = 4;
 // Channel target meaning "leave an existing override alone" (Scenario Properties weather).
 static const float UNTOUCHED = -2;
 // Last override value of a channel before Tick's first write of a transition (every
 // written value is 0 or more).
 static const float UNWRITTEN = -9999;
 static const string STATUS_IDLE = "Idle. Change a setting and press Save to start a transition.";

 protected static TimeAndWeatherManagerEntity s_Manager;
 protected static bool s_bRunning;
 // World seconds: transition start, requested length, planned end (everything arrives),
 // and the start of the rain, fog and wind ease (the moment the clouds start).
 protected static float s_fBegin;
 protected static float s_fRequested;
 protected static float s_fEnd;
 protected static float s_fEaseFrom;
 protected static string s_sTarget;
 protected static int s_iTargetIndex;
 protected static bool s_bHold;
 protected static float s_fStateHours;
 // Clouds: whether they change, the stage, and the predicted (then observed) blend.
 protected static bool s_bCloudChange;
 protected static int s_iCloudStage;
 protected static float s_fStageAt;
 // World seconds after which a direct start that has not taken is replaced by a pin, and
 // whether the planned cloud start was read from the engine (else it is the latest one).
 protected static float s_fDirectUntil;
 protected static bool s_bStartKnown;
 protected static float s_fCloudStart;
 protected static float s_fCloudEnd;
 // Real seconds of the cloud blend the pin planned: a start request that starts nothing
 // gives it back (Kick lengthened the blend by the pin's hold).
 protected static float s_fPinBlend;
 // In-game hours of the engine's minimum blend once it raised a node of ours (0 until then).
 protected static float s_fFloorHours;
 protected static bool s_bKickTried;
 protected static bool s_bRepinned;
 protected static bool s_bDeferred;
 protected static int s_iQueueLogs;
 protected static int s_iLostTicks;
 // Rain and fog left to the weather, read just before an immediate weather change; the
 // next tick logs how far they moved across it (largest move of the transition kept).
 protected static bool s_bWatchRain;
 protected static bool s_bWatchFog;
 protected static float s_fWatchAt;
 protected static float s_fWatchRain;
 protected static float s_fWatchFog;
 protected static float s_fPinJump;
 protected static string s_sCloudPath;
 protected static ref WeatherStateTransitionNode s_CloudNode;
 protected static ref WeatherStateTransitionNode s_PinNode;
 protected static ref array<int> s_aMode;
 protected static ref array<float> s_aFrom;
 protected static ref array<float> s_aTo;
 // Override value Tick last wrote per channel: an unchanged value is not written (and
 // broadcast to every client) again.
 protected static ref array<float> s_aLast;
 protected static int s_iPlayer;
 protected static string s_sSource;
 protected static string s_sStatus;
 protected static int s_iProgress;
 // Evidence for logs and fixtures.
 protected static int s_iStarts;
 protected static int s_iCompleted;
 protected static int s_iCloudSnaps;
 protected static int s_iForeignStops;
 protected static int s_iCloudBlends;
 protected static int s_iPins;
 protected static int s_iKicks;
 protected static int s_iDeferrals;

 //------------------------------------------------------------------------------------------------
 static TimeAndWeatherManagerEntity Manager()
 {
  ChimeraWorld world = GetGame().GetWorld();
  if (!world)
   return null;
  return world.GetTimeAndWeatherManager();
 }

 //------------------------------------------------------------------------------------------------
 static float Now()
 {
  BaseWorld world = GetGame().GetWorld();
  if (!world)
   return 0;
  return world.GetWorldTime() * 0.001;
 }

 //------------------------------------------------------------------------------------------------
 static bool IsRunning()
 {
  return s_bRunning && s_Manager != null;
 }

 //------------------------------------------------------------------------------------------------
 static string GetStatus()
 {
  DropStale();
  if (s_sStatus.IsEmpty())
   return STATUS_IDLE;
  return s_sStatus;
 }

 //------------------------------------------------------------------------------------------------
 static int GetProgress()
 {
  DropStale();
  return s_iProgress;
 }

 //------------------------------------------------------------------------------------------------
 // A mission that ended during a transition leaves no "Blending..." status for the next one.
 protected static void DropStale()
 {
  if (!s_bRunning || s_Manager)
   return;
  s_bRunning = false;
  s_CloudNode = null;
  s_PinNode = null;
  s_iCloudStage = CLOUD_NONE;
  s_sStatus = STATUS_IDLE;
  s_iProgress = 0;
 }

 //------------------------------------------------------------------------------------------------
 static string GetTarget()
 {
  return s_sTarget;
 }

 //------------------------------------------------------------------------------------------------
 static int GetChannelMode(int channel)
 {
  EXPBG_LazyStatics_ETW_WeatherRunner();
  if (!s_aMode.IsIndexValid(channel))
   return MODE_NONE;
  return s_aMode[channel];
 }

 //------------------------------------------------------------------------------------------------
 static int GetStarts()
 {
  return s_iStarts;
 }

 //------------------------------------------------------------------------------------------------
 static int GetCompleted()
 {
  return s_iCompleted;
 }

 //------------------------------------------------------------------------------------------------
 static int GetCloudSnaps()
 {
  return s_iCloudSnaps;
 }

 //------------------------------------------------------------------------------------------------
 static int GetForeignStops()
 {
  return s_iForeignStops;
 }

 //------------------------------------------------------------------------------------------------
 // Transitions whose clouds were seen blending through the engine.
 static int GetCloudBlends()
 {
  return s_iCloudBlends;
 }

 //------------------------------------------------------------------------------------------------
 static int GetPins()
 {
  return s_iPins;
 }

 //------------------------------------------------------------------------------------------------
 static int GetKicks()
 {
  return s_iKicks;
 }

 //------------------------------------------------------------------------------------------------
 static int GetDeferrals()
 {
  return s_iDeferrals;
 }

 //------------------------------------------------------------------------------------------------
 // Largest move of rain or fog left to the weather across an immediate weather change of
 // the latest transition, a tick later; -1 when none was measured.
 static float GetPinJump()
 {
  return s_fPinJump;
 }

 //------------------------------------------------------------------------------------------------
 static int GetCloudStage()
 {
  return s_iCloudStage;
 }

 //------------------------------------------------------------------------------------------------
 // How the clouds of the latest transition started: none, direct, pinned, ...
 static string GetCloudPath()
 {
  if (s_sCloudPath.IsEmpty())
   return "none";
  return s_sCloudPath;
 }

 //------------------------------------------------------------------------------------------------
 // World seconds: the planned end of the running transition and the cloud blend.
 static float GetPlannedEnd()
 {
  return s_fEnd;
 }

 //------------------------------------------------------------------------------------------------
 static float GetCloudStart()
 {
  return s_fCloudStart;
 }

 //------------------------------------------------------------------------------------------------
 static float GetCloudEnd()
 {
  return s_fCloudEnd;
 }

 //------------------------------------------------------------------------------------------------
 // 0-1 by the real-time clock of the running transition (start to planned end).
 static float Elapsed01()
 {
  if (!IsRunning() || s_fEnd <= s_fBegin)
   return 1;
  return Math.Clamp((Now() - s_fBegin) / (s_fEnd - s_fBegin), 0, 1);
 }

 //------------------------------------------------------------------------------------------------
 static int FindStateIndex(notnull array<ref WeatherState> states, string stateName)
 {
  if (stateName.IsEmpty())
   return -1;
  for (int i = 0; i < states.Count(); i++)
  {
   if (states[i] && states[i].GetStateName() == stateName)
    return i;
  }
  return -1;
 }

 //------------------------------------------------------------------------------------------------
 // Module settings: the module's target clouds, rain, fog, wind and duration.
 static bool StartFromModule(notnull ETW_WeatherModule module, int playerId)
 {
  array<float> targets = {};
  targets.Insert(ETW_Settings.ChoiceValue(ETW_Settings.W_RAIN, module.GetSetting(ETW_Settings.W_RAIN)));
  targets.Insert(ETW_Settings.ChoiceValue(ETW_Settings.W_FOG, module.GetSetting(ETW_Settings.W_FOG)));
  targets.Insert(ETW_Settings.ChoiceValue(ETW_Settings.W_WIND_SPEED, module.GetSetting(ETW_Settings.W_WIND_SPEED)));
  targets.Insert(ETW_Settings.ChoiceValue(ETW_Settings.W_WIND_DIR, module.GetSetting(ETW_Settings.W_WIND_DIR)));
  bool hold = module.GetSetting(ETW_Settings.W_AFTER) == ETW_Settings.AFTER_HOLD;
  return Start(module.GetTargetName(), module.GetSetting(ETW_Settings.W_MINUTES), targets, hold, playerId, "module");
 }

 //------------------------------------------------------------------------------------------------
 // "Return to automatic weather": keep the clouds, blend every overridden rain, fog and
 // wind value back to the weather over the module's duration, then let the weather run.
 static bool StartAutomatic(notnull ETW_WeatherModule module, int playerId)
 {
  array<float> targets = {-1, -1, -1, -1};
  return Start(string.Empty, module.GetSetting(ETW_Settings.W_MINUTES), targets, false, playerId, "automatic");
 }

 //------------------------------------------------------------------------------------------------
 // Scenario Properties weather (vanilla instant attribute) while a Weather Transition
 // module has "Smooth Scenario Properties weather changes" ON: blend the clouds over that
 // module's duration instead. Rain, fog and wind stay as they are. Returns false to let
 // the vanilla instant change happen.
 static bool SmoothVanillaRequest(int stateIndex, int playerId)
 {
  if (!Replication.IsServer())
   return false;
  ETW_WeatherModule module = ETW_WeatherModule.FindSmoothing();
  TimeAndWeatherManagerEntity manager = Manager();
  if (!module || !manager || !manager.GetTransitionManager())
   return false;
  array<ref WeatherState> states = {};
  manager.GetWeatherStatesList(states);
  if (!states.IsIndexValid(stateIndex) || !states[stateIndex])
   return false;
  string stateName = states[stateIndex].GetStateName();
  int minutes = module.GetSetting(ETW_Settings.W_MINUTES);
  // After the rest of the same Save (the vanilla automated-weather switch may call
  // ForceWeatherTo, which would stop a transition started earlier in the same Save).
  GetGame().GetCallqueue().CallLater(ETW_WeatherRunner.StartVanilla, 0, false, stateName, minutes, playerId);
  Log(string.Format("Scenario Properties weather %1 will blend over %2 min (player %3)", stateName, minutes, playerId));
  return true;
 }

 //------------------------------------------------------------------------------------------------
 protected static void StartVanilla(string stateName, int minutes, int playerId)
 {
  array<float> targets = {-2, -2, -2, -2};
  Start(stateName, minutes, targets, true, playerId, "Scenario Properties");
 }

 //------------------------------------------------------------------------------------------------
 // requested: target weather state name (empty or unknown keeps the current clouds).
 // targets: rain, fog, wind speed, wind direction. A value of 0 or more is blended to and
 // kept; -1 leaves it to the weather (an existing override is blended back and released
 // at the end); UNTOUCHED (-2) leaves an override alone, unless the transition being
 // replaced was moving that value, which is then handed back like -1.
 static bool Start(string requested, int minutes, notnull array<float> targets, bool hold, int playerId, string source)
 {
  EXPBG_LazyStatics_ETW_WeatherRunner();
  if (!Replication.IsServer())
   return false;
  TimeAndWeatherManagerEntity manager = Manager();
  BaseWeatherStateTransitionManager transitions;
  if (manager)
   transitions = manager.GetTransitionManager();
  if (!transitions)
  {
   SetStatus("Refused: this world has no weather manager.", 0);
   Log("transition refused: no weather manager");
   return false;
  }
  if (transitions.IsPreviewingState())
   manager.SetWeatherStatePreview(false);
  string current = CurrentName(transitions);
  array<ref WeatherState> states = {};
  manager.GetWeatherStatesList(states);

  // A running transition is replaced from the values reached so far. Its cloud node,
  // still queued or blending, is kept when the target stays the same.
  float now = Now();
  bool wasRunning = IsRunning();
  string runningTarget = s_sTarget;
  bool oursPending = wasRunning && s_CloudNode != null && s_iCloudStage >= CLOUD_DIRECT && s_iCloudStage <= CLOUD_BLENDING;
  bool oursBlending = oursPending && s_iCloudStage == CLOUD_BLENDING;
  float runningCloud = CloudProgress(now);
  // A blend in flight (ours or the weather's own) and the nearer of its two weathers.
  bool moving = IsMoving(transitions);
  string next = NextName(transitions);
  float progress = 0;
  string nearer = current;
  if (moving)
  {
   progress = NativeProgress(transitions);
   if (oursPending && runningTarget == next)
    progress = runningCloud;
   if (progress >= 0.5 && !next.IsEmpty())
    nearer = next;
  }
  // No or an unknown target keeps the clouds: a blend of ours under way goes on, a blend of
  // another weather source is left to run (no jump), and a cloud change of ours that has
  // not started is dropped (on the nearer weather when something else blends).
  string target = requested;
  bool keepClouds = FindStateIndex(states, target) < 0;
  bool leaveBlend = false;
  if (keepClouds)
  {
   target = current;
   if (oursBlending)
    target = runningTarget;
   else if (moving && !oursPending)
   {
    leaveBlend = true;
    if (!next.IsEmpty())
     target = next;
   }
   else if (moving)
    target = nearer;
  }
  s_bRunning = false;
  GetGame().GetCallqueue().Remove(ETW_WeatherRunner.Tick);

  s_Manager = manager;
  s_sTarget = target;
  s_iTargetIndex = FindStateIndex(states, target);
  s_bHold = hold;
  s_iPlayer = playerId;
  s_sSource = source;
  s_fRequested = Math.ClampInt(minutes, ETW_Settings.MINUTES_MIN, ETW_Settings.MINUTES_MAX) * 60;
  s_fBegin = now;
  s_fEaseFrom = now;
  s_fEnd = now + s_fRequested;
  s_fStateHours = StateHours(states, target);
  s_bDeferred = false;
  s_iQueueLogs = 0;
  s_iLostTicks = 0;
  s_bWatchRain = false;
  s_bWatchFog = false;
  s_fPinJump = -1;

  bool anyChannel = false;
  for (int c = 0; c < CHANNELS; c++)
  {
   float live = ReadChannel(manager, c);
   float wanted = -1;
   if (targets.IsIndexValid(c))
    wanted = targets[c];
   if (wanted <= UNTOUCHED)
   {
    if (wasRunning && s_aMode[c] != MODE_NONE)
     wanted = -1;
    else
     wanted = UNTOUCHED;
   }
   s_aFrom[c] = live;
   s_aTo[c] = live;
   s_aLast[c] = UNWRITTEN;
   s_aMode[c] = MODE_NONE;
   if (wanted >= 0)
   {
    s_aMode[c] = MODE_SET;
    s_aTo[c] = wanted;
   }
   else if (wanted > UNTOUCHED && IsOverridden(manager, c))
   {
    s_aMode[c] = MODE_RELEASE;
    s_aTo[c] = NaturalValue(states, target, c, live);
   }
   if (s_aMode[c] != MODE_NONE)
    anyChannel = true;
  }
  // Wind direction turns the short way round.
  if (s_aMode[CH_WIND_DIR] != MODE_NONE)
  {
   float turn = s_aTo[CH_WIND_DIR] - s_aFrom[CH_WIND_DIR];
   turn = Math.Repeat(turn + 180, 360) - 180;
   s_aTo[CH_WIND_DIR] = s_aFrom[CH_WIND_DIR] + turn;
  }

  // Clouds.
  bool adopt = oursPending && runningTarget == target;
  if (adopt)
  {
   s_bCloudChange = true;
   s_CloudNode.SetLooping(hold);
   AdoptClouds(now);
  }
  else
  {
   s_bCloudChange = false;
   s_CloudNode = null;
   s_PinNode = null;
   s_iCloudStage = CLOUD_NONE;
   s_bKickTried = false;
   s_bRepinned = false;
   s_bStartKnown = false;
   s_sCloudPath = "none";
   s_fCloudStart = now;
   s_fCloudEnd = now;
   s_fPinBlend = 0;
   if (leaveBlend)
    Log(string.Format("clouds kept: the running cloud blend %1 -> %2 of another weather source is left to run", current, next));
   else if (moving)
   {
    // A running blend cannot be continued from where it is: it completes at once on the
    // nearer weather, and the new blend starts from there.
    Log(string.Format("running cloud blend %1 -> %2 completes at once on %3 (progress %4)", current, next, nearer, progress));
    if (nearer == target)
    {
     if (SnapAtStart(transitions, target, hold, now))
      anyChannel = true;
    }
    else
    {
     s_bCloudChange = true;
     Pin(transitions, nearer, now);
    }
    current = nearer;
   }
   else if (current != target)
   {
    s_bCloudChange = true;
    // A node of ours still queued for another target is dropped by the pin; so is
    // anything queued when there is no node in place to queue behind.
    if (oursPending || !DIRECT_START || transitions.GetStateTransitionsCount() < 1 || !Direct(transitions, now))
     Pin(transitions, current, now);
   }
   else if (oursPending)
   {
    if (SnapAtStart(transitions, current, hold, now))
     anyChannel = true;
   }
  }
  if (!s_bCloudChange && hold)
   ApplyLooping(transitions, true);

  if (s_aMode[CH_WIND_SPEED] != MODE_NONE || s_aMode[CH_WIND_DIR] != MODE_NONE)
   manager.ETW_SetWindFlag(true);

  s_bRunning = true;
  s_iStarts++;
  Log(string.Format("transition start: %1 -> %2 over %3 min (%4 s planned), rain %5, fog %6, wind %7 m/s %8 deg, %9", current, target, s_fRequested / 60, Math.Round(s_fEnd - now), Describe(CH_RAIN), Describe(CH_FOG), Describe(CH_WIND_SPEED), Describe(CH_WIND_DIR), HoldText()) + string.Format(", clouds %1, source %2, player %3", CloudPlanText(now), source, playerId));
  if (!s_bCloudChange && !anyChannel)
  {
   Complete("nothing to blend");
   return true;
  }
  GetGame().GetCallqueue().CallLater(ETW_WeatherRunner.Tick, TICK_MS, true);
  Tick();
  return true;
 }

 //------------------------------------------------------------------------------------------------
 // "Stop here and hold": rain, fog and wind keep the values reached; clouds settle on the
 // nearer weather and hold, unless they were left to another weather source.
 static bool Stop(int playerId)
 {
  if (!IsRunning())
  {
   SetStatus("Nothing to stop: no transition is running.", s_iProgress);
   return false;
  }
  BaseWeatherStateTransitionManager transitions = s_Manager.GetTransitionManager();
  float now = Now();
  int percent = Math.Round(Elapsed01() * 100);
  if (s_bDeferred)
  {
   Halt(string.Format("Stopped by a Game Master at %1 of 100: rain, fog and wind keep the values reached; the clouds were left to another weather source.", percent), percent);
   Log(string.Format("transition stopped by player %1 at %2 percent; clouds left to another weather source", playerId, percent));
   return true;
  }
  if (transitions)
  {
   bool pending = s_bCloudChange && s_iCloudStage >= CLOUD_DIRECT && s_iCloudStage <= CLOUD_BLENDING;
   bool moving = IsMoving(transitions);
   if (moving || pending)
   {
    // A node of ours that has not started yet is dropped by this immediate change; a blend
    // in flight (ours or the weather's own) settles on the nearer of its two weathers.
    string nearer = CurrentName(transitions);
    string next = NextName(transitions);
    float progress = NativeProgress(transitions);
    if (s_iCloudStage == CLOUD_BLENDING && next == s_sTarget)
     progress = CloudProgress(now);
    if (moving && progress >= 0.5 && !next.IsEmpty())
     nearer = next;
    Snap(transitions, nearer, true, s_fStateHours);
   }
   else
    ApplyLooping(transitions, true);
  }
  s_Manager.ETW_SetLoopingFlag(true);
  Halt(string.Format("Stopped by a Game Master at %1 of 100: holding the current weather.", percent), percent);
  Log(string.Format("transition stopped by player %1 at %2 percent", playerId, percent));
  return true;
 }

 //------------------------------------------------------------------------------------------------
 // Completes a running transition at once (a time skip does this under the black screen).
 static void FinishNow(string reason)
 {
  if (!IsRunning())
   return;
  Log("transition finished at once: " + reason);
  Complete(reason);
 }

 //------------------------------------------------------------------------------------------------
 // ForceWeatherTo from anything else (Scenario Properties weather or automated weather with
 // the Game Master's player id, else the mission or another mod): that weather wins; stop
 // and hand back the values this transition overrode.
 static void OnForeignWeather(int playerId = 0)
 {
  EXPBG_LazyStatics_ETW_WeatherRunner();
  if (!IsRunning())
   return;
  for (int c = 0; c < CHANNELS; c++)
  {
   if (s_aMode[c] != MODE_NONE)
    ReleaseChannel(s_Manager, c);
  }
  s_Manager.ETW_SetWindFlag(s_Manager.IsWindSpeedOverridden() || s_Manager.IsWindDirectionOverridden());
  s_iForeignStops++;
  string status = "Stopped: the mission or another mod set the weather; rain, fog and wind went back to the weather.";
  if (playerId > 0)
   status = "Stopped: a Game Master changed the weather in Scenario Properties; rain, fog and wind went back to the weather.";
  Halt(status, s_iProgress);
  Log(string.Format("transition stopped: weather changed elsewhere (ForceWeatherTo, player %1)", playerId));
 }

 //------------------------------------------------------------------------------------------------
 // Scenario Properties wind: that wind wins; the transition leaves wind alone from now on.
 static void OnForeignWind()
 {
  EXPBG_LazyStatics_ETW_WeatherRunner();
  if (!IsRunning())
   return;
  if (s_aMode[CH_WIND_SPEED] == MODE_NONE && s_aMode[CH_WIND_DIR] == MODE_NONE)
   return;
  s_aMode[CH_WIND_SPEED] = MODE_NONE;
  s_aMode[CH_WIND_DIR] = MODE_NONE;
  Log("wind changed in Scenario Properties: the transition no longer moves the wind");
 }

 //------------------------------------------------------------------------------------------------
 protected static void Tick()
 {
  EXPBG_LazyStatics_ETW_WeatherRunner();
  if (!IsRunning())
  {
   GetGame().GetCallqueue().Remove(ETW_WeatherRunner.Tick);
   s_bRunning = false;
   return;
  }
  BaseWeatherStateTransitionManager transitions = s_Manager.GetTransitionManager();
  if (!transitions || s_Manager != Manager())
  {
   Halt("Stopped: the weather manager is gone.", s_iProgress);
   return;
  }
  float now = Now();
  if (s_bCloudChange)
   UpdateClouds(transitions, now);
  if (s_bWatchRain || s_bWatchFog)
   CheckUntouched(now);
  float eased = Ease(now);
  for (int c = 0; c < CHANNELS; c++)
  {
   if (s_aMode[c] == MODE_NONE)
    continue;
   float value = s_aFrom[c] + (s_aTo[c] - s_aFrom[c]) * eased;
   float written = WrittenValue(c, value);
   if (written != s_aLast[c])
   {
    WriteChannel(s_Manager, c, value);
    s_aLast[c] = written;
   }
  }
  bool cloudsThere = !s_bCloudChange || (CurrentName(transitions) == s_sTarget && !IsMoving(transitions));
  if (now >= s_fEnd)
  {
   bool finishing = !cloudsThere && s_iCloudStage == CLOUD_BLENDING;
   if (!finishing || now >= s_fEnd + CLOUD_GRACE_S)
   {
    Complete("finished");
    return;
   }
   SetStatus("Clouds are finishing the blend to " + s_sTarget + ".", 99);
   return;
  }
  int percent = Math.Round(Elapsed01() * 100);
  int left = Math.Ceil((s_fEnd - now) / 60);
  SetStatus("Blending to " + s_sTarget + ": " + percent.ToString() + "%, about " + left.ToString() + " min left (then " + HoldText() + ")." + CloudText(now), percent);
 }

 //------------------------------------------------------------------------------------------------
 // Ease-in-out factor of rain, fog and wind: 0 until the clouds start, 1 at the planned end.
 protected static float Ease(float now)
 {
  float t = 1;
  if (s_fEnd > s_fEaseFrom)
   t = Math.Clamp((now - s_fEaseFrom) / (s_fEnd - s_fEaseFrom), 0, 1);
  float eased = t * t * (3 - 2 * t);
  return eased;
 }

 //------------------------------------------------------------------------------------------------
 // New timing for rain, fog and wind: they continue from the values reached, ease from
 // easeFrom and arrive at end (never before the requested length).
 protected static void Retime(float easeFrom, float end)
 {
  float now = Now();
  float eased = Ease(now);
  for (int c = 0; c < CHANNELS; c++)
  {
   if (s_aMode[c] != MODE_NONE)
    s_aFrom[c] = s_aFrom[c] + (s_aTo[c] - s_aFrom[c]) * eased;
  }
  s_fEaseFrom = Math.Max(easeFrom, now);
  s_fEnd = Math.Max(end, s_fBegin + s_fRequested);
  if (s_fEnd < s_fEaseFrom + 1)
   s_fEnd = s_fEaseFrom + 1;
 }

 //------------------------------------------------------------------------------------------------
 // The cloud blend starts at start and takes blend real seconds; everything else follows.
 protected static void PlanClouds(float start, float blend)
 {
  s_fCloudStart = start;
  s_fCloudEnd = start + blend;
  Retime(start, s_fCloudEnd);
 }

 //------------------------------------------------------------------------------------------------
 // Same target as the transition being replaced: our node keeps going. A node that has not
 // started yet is resized to the new time (never below the shortest blend, also when its
 // start is further away than the new time); rain, fog and wind restart from the live values.
 protected static void AdoptClouds(float now)
 {
  if (s_iCloudStage == CLOUD_BLENDING)
  {
   s_fEaseFrom = now;
   s_fEnd = Math.Max(now + s_fRequested, s_fCloudEnd);
   return;
  }
  float start = Math.Max(s_fCloudStart, now);
  SetBlend(s_CloudNode, now + s_fRequested - start);
  PlanClouds(start, HoursToReal(s_CloudNode.GetTransitionDurationHours()));
  s_fPinBlend = s_fCloudEnd - s_fCloudStart;
 }

 //------------------------------------------------------------------------------------------------
 // In-game hours of the engine's minimum blend: the one it kept when it raised a node of
 // ours, else BLEND_FLOOR_HOURS.
 protected static float FloorHours()
 {
  if (s_fFloorHours > 0)
   return s_fFloorHours;
  return BLEND_FLOOR_HOURS;
 }

 //------------------------------------------------------------------------------------------------
 // A node we queued kept a longer blend than asked: the engine raised it to its minimum.
 protected static void NoteFloor(float askedHours, float keptHours)
 {
  if (keptHours > askedHours + 0.0005 && keptHours < 1)
   s_fFloorHours = keptHours;
 }

 //------------------------------------------------------------------------------------------------
 // Real seconds of the shortest cloud blend ever asked for: the engine's minimum, or the
 // requested time when that is shorter.
 protected static float ShortestBlend()
 {
  return Math.Min(s_fRequested, HoursToReal(FloorHours()));
 }

 //------------------------------------------------------------------------------------------------
 // Every blend change of a queued node of ours goes through here, never below the shortest
 // blend: if the engine does not raise a queued node's blend to its minimum, a shorter one
 // would switch the sky at once. The caller reads the kept blend back from the node.
 protected static void SetBlend(notnull WeatherStateTransitionNode node, float seconds)
 {
  node.SetTransitionDurationHours(RealToHours(Math.Max(seconds, ShortestBlend())));
 }

 //------------------------------------------------------------------------------------------------
 // Our node right behind the node in place, which stops looping and gets the shortest
 // hold: the engine moves on to ours once that hold is over. The hold left over is read
 // back (time left until the next weather is the hold left plus our blend), bounded by the
 // hold of the node in place; without a reading the latest start is planned, so rain, fog
 // and wind never move ahead of the clouds. Checked by UpdateClouds.
 protected static bool Direct(notnull BaseWeatherStateTransitionManager transitions, float now)
 {
  WeatherStateTransitionNode node = transitions.CreateStateTransition(s_sTarget, RealToHours(s_fRequested), s_fStateHours);
  if (!node)
   return false;
  node.SetLooping(s_bHold);
  if (!transitions.InsertStateTransition(1, node, false))
   return false;
  NoteFloor(RealToHours(s_fRequested), node.GetTransitionDurationHours());
  WeatherStateTransitionNode head = transitions.GetCurrentStateTransitionNode();
  float holdHours = 0;
  if (head && head != node)
  {
   head.SetLooping(false);
   head.SetStateDurationHours(SHORTEST_HOURS);
   holdHours = Math.Min(head.GetStateDurationHours(), DIRECT_MAX_HOURS);
  }
  float leftHours = holdHours;
  s_bStartKnown = false;
  float timeLeft = transitions.GetTimeLeftUntilNextState();
  float reading = timeLeft - node.GetTransitionDurationHours();
  if (timeLeft > 0 && reading <= holdHours + 0.001)
  {
   leftHours = Math.Max(reading, 0);
   s_bStartKnown = true;
  }
  float wait = HoursToReal(leftHours);
  if (!DIRECT_WAITS_HOLD && wait > DIRECT_MARGIN_S)
  {
   Log(string.Format("the weather in place holds for %1 s more; the clouds pin instead of waiting", Math.Round(wait)));
   return false;
  }
  // The hold left over counts toward the requested time when the engine told us its length
  // (the blend never drops below the shortest one).
  if (s_bStartKnown && wait >= DIRECT_MARGIN_S)
   SetBlend(node, s_fRequested - wait);
  s_CloudNode = node;
  s_iCloudStage = CLOUD_DIRECT;
  s_fStageAt = now;
  s_fDirectUntil = now + wait + DIRECT_MARGIN_S;
  s_sCloudPath = "direct";
  PlanClouds(now + wait, HoursToReal(node.GetTransitionDurationHours()));
  if (wait >= 1)
   Log(string.Format("clouds: the weather in place holds for %1 s more (%2) before %3 blends", Math.Round(wait), StartWord(), s_sTarget));
  return true;
 }

 //------------------------------------------------------------------------------------------------
 // Our direct node is the next one the engine plays: first in the queue, or right behind the
 // node in place when the queue lists it.
 protected static bool DirectNext(notnull BaseWeatherStateTransitionManager transitions)
 {
  int index = QueueIndex(transitions, s_CloudNode);
  if (index == 0)
   return true;
  return index == 1 && transitions.GetStateTransitionNode(0) == transitions.GetCurrentStateTransitionNode();
 }

 //------------------------------------------------------------------------------------------------
 protected static string StartWord()
 {
  if (s_bStartKnown)
   return "read from the game";
  return "at most";
 }

 //------------------------------------------------------------------------------------------------
 // Pin node aimed at "from" and our node at the back of the queue; the pin is set at once,
 // which drops every node ahead of it. The sky stays as it is (from is the weather in
 // place, or the nearer one of a running blend); our node blends after the pin's hold.
 protected static bool Pin(notnull BaseWeatherStateTransitionManager transitions, string from, float now)
 {
  // An empty name makes the engine write through memory it cannot find.
  if (from.IsEmpty())
  {
   s_iCloudStage = CLOUD_NONE;
   Log("no weather in place to settle the clouds on; they will be set at the end");
   return false;
  }
  WeatherStateTransitionNode pin = transitions.CreateStateTransition(from, SHORTEST_HOURS, SHORTEST_HOURS);
  if (!pin)
  {
   s_iCloudStage = CLOUD_NONE;
   Log("could not create a weather node for " + from + "; the clouds will be set at the end");
   return false;
  }
  pin.SetLooping(false);
  if (!transitions.EnqueueStateTransition(pin, false))
  {
   s_iCloudStage = CLOUD_NONE;
   Log("could not queue a weather node for " + from + "; the clouds will be set at the end");
   return false;
  }
  NoteFloor(SHORTEST_HOURS, pin.GetTransitionDurationHours());
  float hold = HoursToReal(pin.GetStateDurationHours());
  float blend = Math.Max(s_fBegin + s_fRequested - (now + hold), ShortestBlend());
  WeatherStateTransitionNode node = transitions.CreateStateTransition(s_sTarget, RealToHours(blend), s_fStateHours);
  bool queued = false;
  if (node)
  {
   node.SetLooping(s_bHold);
   queued = transitions.EnqueueStateTransition(node, false);
  }
  WatchUntouched(now);
  if (!queued)
  {
   // Keep the weather in place, held, rather than half queued.
   pin.SetLooping(true);
   transitions.RequestStateTransitionImmediately(pin);
   s_iCloudStage = CLOUD_NONE;
   Log("could not queue a weather transition to " + s_sTarget + "; the clouds will be set at the end");
   return false;
  }
  WeatherTransitionRequestResponse response = transitions.RequestStateTransitionImmediately(pin);
  if (response != WeatherTransitionRequestResponse.SUCCESS)
  {
   s_iCloudStage = CLOUD_NONE;
   Log(string.Format("the game refused to settle the clouds on %1 (response %2); they will be set at the end", from, response));
   return false;
  }
  s_PinNode = pin;
  s_CloudNode = node;
  s_iCloudStage = CLOUD_PINNED;
  s_fStageAt = now;
  s_bStartKnown = true;
  s_iPins++;
  if (s_sCloudPath == "direct")
   s_sCloudPath = "direct then pinned";
  else
   s_sCloudPath = "pinned";
  PlanClouds(now + hold, HoursToReal(node.GetTransitionDurationHours()));
  s_fPinBlend = s_fCloudEnd - s_fCloudStart;
  HoldUntouched();
  Log(string.Format("clouds settled on %1; %2 blends after the game's hold of %3 s, over %4 s", from, s_sTarget, Math.Round(hold), Math.Round(s_fCloudEnd - s_fCloudStart)));
  return true;
 }

 //------------------------------------------------------------------------------------------------
 // Our node is first in the queue after the pin: ask once for it to start now, over the
 // time left (never below the shortest blend).
 protected static void Kick(notnull BaseWeatherStateTransitionManager transitions, float now)
 {
  s_bKickTried = true;
  s_iKicks++;
  SetBlend(s_CloudNode, s_fBegin + s_fRequested - now);
  WeatherTransitionRequestResponse response = transitions.RequestStateTransition();
  s_iCloudStage = CLOUD_KICKED;
  s_fStageAt = now;
  s_sCloudPath = s_sCloudPath + ", start asked";
  Log(string.Format("clouds: our node is first in the queue; asked the game to start it now (response %1)", response));
 }

 //------------------------------------------------------------------------------------------------
 // Cloud stage machine, once per tick while the clouds change. Bounded: a direct start,
 // at most two pins and one start request per transition.
 protected static void UpdateClouds(notnull BaseWeatherStateTransitionManager transitions, float now)
 {
  if (s_iCloudStage == CLOUD_NONE || s_iCloudStage == CLOUD_ARRIVED)
   return;
  if (!s_CloudNode)
  {
   s_iCloudStage = CLOUD_NONE;
   return;
  }
  bool blending = s_CloudNode.IsTransitioning();
  if (s_iCloudStage == CLOUD_BLENDING)
  {
   if (blending)
    return;
   if (CurrentName(transitions) == s_sTarget)
   {
    s_iCloudStage = CLOUD_ARRIVED;
    Log(string.Format("clouds reached %1 after %2 s of blending", s_sTarget, Math.Round(now - s_fCloudStart)));
    return;
   }
   CheckLost(transitions);
   return;
  }
  if (blending)
  {
   Started(now);
   return;
  }
  bool moving = IsMoving(transitions);
  if (s_iCloudStage == CLOUD_DIRECT)
  {
   // The weather in place finishes its hold first. Something else blending, our node no
   // longer next, or no start by the end of the hold left over: pin instead.
   string reason;
   string from = CurrentName(transitions);
   if (moving)
   {
    reason = "something else is blending";
    if (NativeProgress(transitions) >= 0.5 && !NextName(transitions).IsEmpty())
     from = NextName(transitions);
   }
   else if (!DirectNext(transitions))
    reason = "our cloud node is no longer next";
   else if (now >= s_fDirectUntil)
    reason = "direct start did not take";
   if (!reason.IsEmpty())
   {
    LogQueue(transitions, reason);
    Pin(transitions, from, now);
   }
   return;
  }
  if (s_iCloudStage == CLOUD_KICKED)
  {
   if (moving && !s_bRepinned)
   {
    // The request moved something else (the pin again, presumably): settle once more.
    s_bRepinned = true;
    LogQueue(transitions, "start request moved another node");
    Pin(transitions, CurrentName(transitions), now);
    return;
   }
   // The request started nothing: the pin's plan stands, with the blend it chose (Kick
   // lengthened it by the pin's hold).
   s_iCloudStage = CLOUD_PINNED;
   SetBlend(s_CloudNode, s_fPinBlend);
   PlanClouds(s_fCloudStart, HoursToReal(s_CloudNode.GetTransitionDurationHours()));
   return;
  }
  // Pinned: waiting for the pin's hold.
  if (START_REQUEST && !s_bKickTried && !moving && now - s_fStageAt >= 0.4 && QueueIndex(transitions, s_CloudNode) == 0)
  {
   Kick(transitions, now);
   return;
  }
  CheckLost(transitions);
 }

 //------------------------------------------------------------------------------------------------
 // Our node started blending: rain, fog and wind follow its real start and length.
 protected static void Started(float now)
 {
  s_iCloudStage = CLOUD_BLENDING;
  s_iCloudBlends++;
  float blend = HoursToReal(s_CloudNode.GetTransitionDurationHours());
  float end = now + blend;
  if (Math.AbsFloat(now - s_fEaseFrom) > 2 || end > s_fEnd + 2)
   Retime(now, end);
  s_fCloudStart = now;
  s_fCloudEnd = end;
  Log(string.Format("clouds are blending to %1 (%2), %3 s to go", s_sTarget, s_sCloudPath, Math.Round(blend)));
 }

 //------------------------------------------------------------------------------------------------
 // Something else rebuilt the weather queue (our node is gone and nothing heads to the
 // target): the clouds are left to it, rain, fog and wind continue. No end snap.
 protected static void CheckLost(notnull BaseWeatherStateTransitionManager transitions)
 {
  if (QueueIndex(transitions, s_CloudNode) >= 0 || HeadsToTarget(transitions) || CurrentName(transitions) == s_sTarget)
  {
   s_iLostTicks = 0;
   return;
  }
  s_iLostTicks++;
  if (s_iLostTicks < LOST_TICKS)
   return;
  LogQueue(transitions, "our cloud node is gone");
  s_bDeferred = true;
  s_bCloudChange = false;
  s_iCloudStage = CLOUD_NONE;
  s_iDeferrals++;
  Log("clouds: the weather queue was changed by something else; the clouds are left to it, rain, fog and wind continue");
 }

 //------------------------------------------------------------------------------------------------
 protected static void Complete(string reason)
 {
  EXPBG_LazyStatics_ETW_WeatherRunner();
  TimeAndWeatherManagerEntity manager = s_Manager;
  BaseWeatherStateTransitionManager transitions;
  if (manager)
   transitions = manager.GetTransitionManager();
  if (!transitions)
  {
   Halt("Stopped: the weather manager is gone.", s_iProgress);
   return;
  }
  for (int c = 0; c < CHANNELS; c++)
  {
   if (s_aMode[c] == MODE_SET)
    WriteChannel(manager, c, s_aTo[c]);
   else if (s_aMode[c] == MODE_RELEASE)
    ReleaseChannel(manager, c);
  }
  bool snapped = false;
  if (s_bCloudChange && (CurrentName(transitions) != s_sTarget || IsMoving(transitions)))
  {
   snapped = true;
   s_iCloudSnaps++;
   LogQueue(transitions, "clouds not there at the end");
   Snap(transitions, s_sTarget, s_bHold, s_fStateHours);
   Log(string.Format("clouds had not finished blending to %1 (%2, stage %3, %4): set at once", s_sTarget, reason, s_iCloudStage, GetCloudPath()));
  }
  else if (!s_bDeferred)
   ApplyLooping(transitions, s_bHold);
  // Clouds left to another weather source keep its queue and its looping as they are.
  if (!s_bDeferred)
   manager.ETW_SetLoopingFlag(s_bHold);
  manager.ETW_SetWindFlag(manager.IsWindSpeedOverridden() || manager.IsWindDirectionOverridden());
  s_iCompleted++;
  string after = "holding this weather.";
  if (!s_bHold)
   after = "automatic weather continues.";
  string done = "Done: " + s_sTarget + " reached; " + after;
  if (s_bDeferred)
   done = "Done: rain, fog and wind reached; the clouds were left to another weather source.";
  else if (!s_bCloudChange && (CurrentName(transitions) != s_sTarget || IsMoving(transitions)))
   done = "Done: rain, fog and wind reached; the clouds are still changing to " + s_sTarget + " by themselves; " + after;
  Halt(done, 100);
  Log(string.Format("transition done (%1): %2, snapped %3, clouds %4, rain %5, fog %6, wind %7 m/s %8 deg, %9", reason, s_sTarget, snapped, GetCloudPath(), manager.GetRainIntensity(), manager.GetFogAmount(), manager.GetWindSpeed(), manager.GetWindDirection(), HoldText()) + string.Format(", deferred %1", s_bDeferred));
 }

 //------------------------------------------------------------------------------------------------
 protected static void Halt(string status, int progress)
 {
  s_bRunning = false;
  s_CloudNode = null;
  s_PinNode = null;
  s_iCloudStage = CLOUD_NONE;
  ArmaReforgerScripted game = GetGame();
  if (game && game.GetCallqueue())
   game.GetCallqueue().Remove(ETW_WeatherRunner.Tick);
  SetStatus(status, progress);
 }

 //------------------------------------------------------------------------------------------------
 protected static void SetStatus(string text, int progress)
 {
  s_sStatus = text;
  s_iProgress = progress;
  ETW_WeatherModule.PublishStatusAll(text, progress);
 }

 //------------------------------------------------------------------------------------------------
 protected static void Log(string message)
 {
  Print("[ETW] " + message, LogLevel.NORMAL);
 }

 //------------------------------------------------------------------------------------------------
 protected static string HoldText()
 {
  if (s_bHold)
   return "hold";
  return "automatic weather";
 }

 //------------------------------------------------------------------------------------------------
 protected static string Describe(int channel)
 {
  EXPBG_LazyStatics_ETW_WeatherRunner();
  if (s_aMode[channel] == MODE_NONE)
   return "weather";
  if (s_aMode[channel] == MODE_RELEASE)
   return "back to weather";
  return s_aTo[channel].ToString();
 }

 //------------------------------------------------------------------------------------------------
 // Cloud plan for the start log line.
 protected static string CloudPlanText(float now)
 {
  if (!s_bCloudChange)
   return "unchanged";
  if (s_iCloudStage == CLOUD_NONE)
   return "set at the end";
  return string.Format("%1, start in %2 s, blend %3 s", GetCloudPath(), Math.Round(s_fCloudStart - now), Math.Round(s_fCloudEnd - s_fCloudStart));
 }

 //------------------------------------------------------------------------------------------------
 // Status note about the clouds (rounded, so the replicated status changes rarely).
 protected static string CloudText(float now)
 {
  if (s_bDeferred)
   return " The weather queue was changed by something else; the clouds are left to it.";
  if (!s_bCloudChange)
   return string.Empty;
  string text;
  if (s_iCloudStage == CLOUD_NONE)
   text = " The clouds are set at the end.";
  else if (s_iCloudStage == CLOUD_BLENDING)
   text = " Clouds are blending.";
  else if (s_iCloudStage == CLOUD_ARRIVED)
   text = " Clouds are there.";
  else if (!s_Manager.GetIsDayAutoAdvanced())
   text = " Time is paused, so the clouds may not move; they are set at the end if they have not arrived.";
  else if (s_fCloudStart - now >= 1 && s_bStartKnown)
   text = " Clouds start changing in about " + Approx(s_fCloudStart - now) + ".";
  else if (s_fCloudStart - now >= 1)
   text = " Clouds start changing within about " + Approx(s_fCloudStart - now) + ".";
  else
   text = " Clouds start changing now.";
  if (s_fEnd > s_fBegin + s_fRequested + 2)
   text += " This takes longer than set: the game needs at least 10 in-game minutes to change the clouds and may first hold the current sky as long.";
  return text;
 }

 //------------------------------------------------------------------------------------------------
 protected static string Approx(float seconds)
 {
  if (seconds >= 90)
  {
   int minutes = Math.Ceil(seconds / 60);
   return minutes.ToString() + " min";
  }
  int tens = Math.Ceil(seconds / 10) * 10;
  return tens.ToString() + " s";
 }

 //------------------------------------------------------------------------------------------------
 // Queue state in one line (diagnostics, at most QUEUE_LOGS times per transition).
 protected static void LogQueue(notnull BaseWeatherStateTransitionManager transitions, string reason)
 {
  if (s_iQueueLogs >= QUEUE_LOGS)
   return;
  s_iQueueLogs++;
  string text = string.Format("queue (%1): %2 nodes, weather %3, next %4, %5 h to the next weather;", reason, transitions.GetStateTransitionsCount(), CurrentName(transitions), NextName(transitions), transitions.GetTimeLeftUntilNextState());
  text += DescribeNode(" in place", transitions.GetCurrentStateTransitionNode());
  int count = ScanCount(transitions);
  for (int i = 0; i < count; i++)
  {
   text += DescribeNode(" [" + i.ToString() + "]", transitions.GetStateTransitionNode(i));
  }
  Log(text);
 }

 //------------------------------------------------------------------------------------------------
 protected static string DescribeNode(string label, WeatherStateTransitionNode node)
 {
  if (!node)
   return label + " none";
  string owner = string.Empty;
  if (node == s_CloudNode)
   owner = " ours";
  else if (node == s_PinNode)
   owner = " pin";
  return string.Format("%1 to %2 blend %3 h hold %4 h looping %5 moving %6%7", label, node.GetDestinationStateIndex(), node.GetTransitionDurationHours(), node.GetStateDurationHours(), node.IsLooping(), node.IsTransitioning(), owner);
 }

 //------------------------------------------------------------------------------------------------
 // Immediate state change through our own node (never ForceWeatherTo). At the back of the
 // queue like vanilla ForceWeatherTo: setting it at once drops every node ahead of it,
 // ours included.
 protected static bool Snap(notnull BaseWeatherStateTransitionManager transitions, string stateName, bool looping, float stateHours)
 {
  if (stateName.IsEmpty())
   return false;
  WeatherStateTransitionNode node = transitions.CreateStateTransition(stateName, 0, stateHours);
  if (!node)
   return false;
  node.SetLooping(looping);
  if (!transitions.EnqueueStateTransition(node, false))
   return false;
  return transitions.RequestStateTransitionImmediately(node) == WeatherTransitionRequestResponse.SUCCESS;
 }

 //------------------------------------------------------------------------------------------------
 // Immediate change when a transition starts (a running blend completed on the nearer
 // weather, or a node of ours dropped): rain and fog left to the weather are watched across
 // it, or held (HOLD_ACROSS_PIN). True when a value is now held by the transition.
 protected static bool SnapAtStart(notnull BaseWeatherStateTransitionManager transitions, string stateName, bool looping, float now)
 {
  WatchUntouched(now);
  Snap(transitions, stateName, looping, s_fStateHours);
  return HoldUntouched();
 }

 //------------------------------------------------------------------------------------------------
 // Before an immediate weather change: rain and fog the transition leaves to the weather.
 protected static void WatchUntouched(float now)
 {
  EXPBG_LazyStatics_ETW_WeatherRunner();
  s_bWatchRain = s_aMode[CH_RAIN] == MODE_NONE && !s_Manager.IsRainIntensityOverridden();
  s_bWatchFog = s_aMode[CH_FOG] == MODE_NONE && !s_Manager.IsFogAmountOverridden();
  s_fWatchAt = now;
  s_fWatchRain = s_Manager.GetRainIntensity();
  s_fWatchFog = s_Manager.GetFogAmount();
 }

 //------------------------------------------------------------------------------------------------
 // A tick after an immediate weather change: how far rain and fog left to the weather moved
 // (one log line per change, at most three changes per transition).
 protected static void CheckUntouched(float now)
 {
  if (now < s_fWatchAt + 0.4)
   return;
  float jump = 0;
  float rain = s_Manager.GetRainIntensity();
  float fog = s_Manager.GetFogAmount();
  if (s_bWatchRain)
   jump = Math.Max(jump, Math.AbsFloat(rain - s_fWatchRain));
  if (s_bWatchFog)
   jump = Math.Max(jump, Math.AbsFloat(fog - s_fWatchFog));
  s_fPinJump = Math.Max(s_fPinJump, jump);
  Log(string.Format("across the immediate weather change: rain %1 -> %2 (watched %3), fog %4 -> %5 (watched %6), largest move %7", s_fWatchRain, rain, s_bWatchRain, s_fWatchFog, fog, s_bWatchFog, jump));
  s_bWatchRain = false;
  s_bWatchFog = false;
 }

 //------------------------------------------------------------------------------------------------
 // HOLD_ACROSS_PIN: the watched rain and fog keep the value read before the immediate change,
 // then ease to the target weather's typical value and are handed back at the end, as
 // "weather default" does for an override. Call after the new timing is planned (the ease
 // then starts at 0, so the first write is the value read before).
 protected static bool HoldUntouched()
 {
  if (!HOLD_ACROSS_PIN || !s_Manager || (!s_bWatchRain && !s_bWatchFog))
   return false;
  array<ref WeatherState> states = {};
  s_Manager.GetWeatherStatesList(states);
  if (s_bWatchRain)
   HoldChannel(states, CH_RAIN, s_fWatchRain);
  if (s_bWatchFog)
   HoldChannel(states, CH_FOG, s_fWatchFog);
  s_bWatchRain = false;
  s_bWatchFog = false;
  Log("rain and fog left to the weather are held across the immediate weather change and handed back at the end");
  return true;
 }

 //------------------------------------------------------------------------------------------------
 protected static void HoldChannel(notnull array<ref WeatherState> states, int channel, float value)
 {
  s_aMode[channel] = MODE_RELEASE;
  s_aFrom[channel] = value;
  s_aTo[channel] = NaturalValue(states, s_sTarget, channel, value);
  s_aLast[channel] = UNWRITTEN;
 }

 //------------------------------------------------------------------------------------------------
 protected static void ApplyLooping(notnull BaseWeatherStateTransitionManager transitions, bool looping)
 {
  for (int i = 0; i < transitions.GetStateTransitionsCount(); i++)
  {
   WeatherStateTransitionNode node = transitions.GetStateTransitionNode(i);
   if (node)
    node.SetLooping(looping);
  }
  WeatherStateTransitionNode head = transitions.GetCurrentStateTransitionNode();
  if (head)
   head.SetLooping(looping);
 }

 //------------------------------------------------------------------------------------------------
 protected static bool IsMoving(notnull BaseWeatherStateTransitionManager transitions)
 {
  int count = transitions.GetStateTransitionsCount();
  if (count > 3)
   count = 3;
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
 protected static int ScanCount(notnull BaseWeatherStateTransitionManager transitions)
 {
  int count = transitions.GetStateTransitionsCount();
  if (count > QUEUE_SCAN)
   count = QUEUE_SCAN;
  return count;
 }

 //------------------------------------------------------------------------------------------------
 // Position of a node among the first QUEUE_SCAN queue entries, or -1.
 protected static int QueueIndex(notnull BaseWeatherStateTransitionManager transitions, WeatherStateTransitionNode node)
 {
  if (!node)
   return -1;
  int count = ScanCount(transitions);
  for (int i = 0; i < count; i++)
  {
   if (transitions.GetStateTransitionNode(i) == node)
    return i;
  }
  return -1;
 }

 //------------------------------------------------------------------------------------------------
 protected static bool HeadsToTarget(notnull BaseWeatherStateTransitionManager transitions)
 {
  if (s_iTargetIndex < 0)
   return false;
  int count = ScanCount(transitions);
  for (int i = 0; i < count; i++)
  {
   WeatherStateTransitionNode node = transitions.GetStateTransitionNode(i);
   if (node && node.GetDestinationStateIndex() == s_iTargetIndex)
    return true;
  }
  return false;
 }

 //------------------------------------------------------------------------------------------------
 // 0-1 of our own cloud blend (0 while it has not started).
 protected static float CloudProgress(float now)
 {
  if (s_iCloudStage != CLOUD_BLENDING || s_fCloudEnd <= s_fCloudStart)
   return 0;
  return Math.Clamp((now - s_fCloudStart) / (s_fCloudEnd - s_fCloudStart), 0, 1);
 }

 //------------------------------------------------------------------------------------------------
 protected static float NativeProgress(notnull BaseWeatherStateTransitionManager transitions)
 {
  WeatherStateTransitionNode head = transitions.GetCurrentStateTransitionNode();
  if (!head || head.GetTransitionDurationHours() <= 0)
   return 0;
  return Math.Clamp(1 - transitions.GetTimeLeftUntilNextState() / head.GetTransitionDurationHours(), 0, 1);
 }

 //------------------------------------------------------------------------------------------------
 protected static string CurrentName(notnull BaseWeatherStateTransitionManager transitions)
 {
  WeatherState state = transitions.GetCurrentState();
  if (!state)
   return string.Empty;
  return state.GetStateName();
 }

 //------------------------------------------------------------------------------------------------
 protected static string NextName(notnull BaseWeatherStateTransitionManager transitions)
 {
  WeatherState state = transitions.GetNextState();
  if (!state)
   return string.Empty;
  return state.GetStateName();
 }

 //------------------------------------------------------------------------------------------------
 protected static float DayLength(TimeAndWeatherManagerEntity manager)
 {
  if (!manager || manager.GetDayDuration() <= 0)
   return 86400;
  return manager.GetDayDuration();
 }

 //------------------------------------------------------------------------------------------------
 // Real seconds to in-game hours at the current day length, and back.
 protected static float RealToHours(float seconds)
 {
  return seconds * 24 / DayLength(s_Manager);
 }

 //------------------------------------------------------------------------------------------------
 protected static float HoursToReal(float hours)
 {
  return hours * DayLength(s_Manager) / 24;
 }

 //------------------------------------------------------------------------------------------------
 // In-game hours the target weather lasts once reached when it is not held.
 protected static float StateHours(notnull array<ref WeatherState> states, string stateName)
 {
  int index = FindStateIndex(states, stateName);
  if (index < 0)
   return 6;
  float hours = (states[index].GetDurationMin() + states[index].GetDurationMax()) * 0.5;
  if (hours <= 0)
   return 6;
  return hours;
 }

 //------------------------------------------------------------------------------------------------
 // Typical value of the target weather (middle of its start variant's range), so a value
 // handed back to the weather does not jump. Wind direction is handed back unchanged.
 protected static float NaturalValue(notnull array<ref WeatherState> states, string stateName, int channel, float fallback)
 {
  if (channel == CH_WIND_DIR)
   return fallback;
  int index = FindStateIndex(states, stateName);
  if (index < 0)
   return fallback;
  array<ref WeatherVariant> variants = {};
  states[index].GetVariantsList(variants);
  int start = states[index].GetStartVariantIndex();
  if (!variants.IsIndexValid(start) || !variants[start])
   return fallback;
  WeatherVariant first = variants[start];
  if (channel == CH_RAIN)
  {
   WeatherRainPattern rain = first.GetRainPattern();
   if (!rain)
    return fallback;
   return (rain.GetMinIntensity() + rain.GetMaxIntensity()) * 0.5;
  }
  if (channel == CH_FOG)
  {
   WeatherFogPattern fog = first.GetFogPattern();
   if (!fog)
    return fallback;
   return (fog.GetMinFogAmount() + fog.GetMaxFogAmount()) * 0.5;
  }
  WeatherWindPattern wind = first.GetWindPattern();
  if (!wind)
   return fallback;
  return (wind.GetMinSpeed() + wind.GetMaxSpeed()) * 0.5;
 }

 //------------------------------------------------------------------------------------------------
 protected static float ReadChannel(notnull TimeAndWeatherManagerEntity manager, int channel)
 {
  if (channel == CH_RAIN)
   return manager.GetRainIntensity();
  if (channel == CH_FOG)
   return manager.GetFogAmount();
  if (channel == CH_WIND_SPEED)
   return manager.GetWindSpeed();
  return manager.GetWindDirection();
 }

 //------------------------------------------------------------------------------------------------
 protected static bool IsOverridden(notnull TimeAndWeatherManagerEntity manager, int channel)
 {
  if (channel == CH_RAIN)
   return manager.IsRainIntensityOverridden();
  if (channel == CH_FOG)
   return manager.IsFogAmountOverridden();
  if (channel == CH_WIND_SPEED)
   return manager.IsWindSpeedOverridden();
  return manager.IsWindDirectionOverridden();
 }

 //------------------------------------------------------------------------------------------------
 // The override value WriteChannel sets for a channel value (range clamp or turn).
 protected static float WrittenValue(int channel, float value)
 {
  if (channel == CH_RAIN || channel == CH_FOG)
   return Math.Clamp(value, 0, 1);
  if (channel == CH_WIND_SPEED)
   return Math.Max(value, 0);
  return Math.Repeat(value, 360);
 }

 //------------------------------------------------------------------------------------------------
 protected static void WriteChannel(notnull TimeAndWeatherManagerEntity manager, int channel, float value)
 {
  float written = WrittenValue(channel, value);
  if (channel == CH_RAIN)
   manager.SetRainIntensityOverride(true, written);
  else if (channel == CH_FOG)
   manager.SetFogAmountOverride(true, written);
  else if (channel == CH_WIND_SPEED)
   manager.SetWindSpeedOverride(true, written);
  else
   manager.SetWindDirectionOverride(true, written);
 }

 //------------------------------------------------------------------------------------------------
 protected static void ReleaseChannel(notnull TimeAndWeatherManagerEntity manager, int channel)
 {
  if (channel == CH_RAIN)
   manager.SetRainIntensityOverride(false);
  else if (channel == CH_FOG)
   manager.SetFogAmountOverride(false);
  else if (channel == CH_WIND_SPEED)
   manager.SetWindSpeedOverride(false);
  else
   manager.SetWindDirectionOverride(false);
 }

 //------------------------------------------------------------------------------------------------
 //! Creates the collections on first use (not in the global static initializer, which has a
 //! per-function instruction limit that large modsets exceed on Windows).
 protected static void EXPBG_LazyStatics_ETW_WeatherRunner()
 {
  if (!s_aMode)
   s_aMode = {0, 0, 0, 0};
  if (!s_aFrom)
   s_aFrom = {0, 0, 0, 0};
  if (!s_aTo)
   s_aTo = {0, 0, 0, 0};
  if (!s_aLast)
   s_aLast = {UNWRITTEN, UNWRITTEN, UNWRITTEN, UNWRITTEN};
 }
}
