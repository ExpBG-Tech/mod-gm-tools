// EXPBG Time and Weather: the one weather transition of the session (server only).
// Clouds blend through the engine's own weather state transition: a node to the target
// state is queued with a duration in in-game hours that matches the requested real
// minutes at the current day length, then RequestStateTransition() starts it smoothly
// (the vanilla Game Master uses RequestStateTransitionImmediately, which is instant).
// Rain, fog and wind are stepped every 0.5 s from their live values along an ease-in-out
// curve through the replicated weather overrides. Nothing here calls ForceWeatherTo, so
// any ForceWeatherTo (Scenario Properties weather, mission load) is a foreign change that
// stops the transition (ETW_Hooks.c). State lives in statics; the stored weather manager
// is a weak reference, so a transition never leaks into the next mission.
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
 // Extra real seconds the engine may take to finish the cloud blend before it is set.
 static const float CLOUD_GRACE_S = 30;
 // Channel target meaning "leave an existing override alone" (Scenario Properties weather).
 static const float UNTOUCHED = -2;
 // Last override value of a channel before Tick's first write of a transition (every
 // written value is 0 or more).
 static const float UNWRITTEN = -9999;
 static const string STATUS_IDLE = "Idle. Change a setting and press Save to start a transition.";

 protected static TimeAndWeatherManagerEntity s_Manager;
 protected static bool s_bRunning;
 protected static float s_fStart;
 protected static float s_fDuration;
 protected static string s_sTarget;
 protected static bool s_bHold;
 protected static float s_fStateHours;
 protected static bool s_bCloudChange;
 protected static bool s_bCloudPending;
 protected static ref WeatherStateTransitionNode s_CloudNode;
 protected static ref array<int> s_aMode;
 protected static ref array<float> s_aFrom;
 protected static ref array<float> s_aTo;
 // Override value Tick last wrote per channel: an unchanged value is not written (and
 // broadcast to every client) again.
 protected static ref array<float> s_aLast;
 protected static int s_iPlayer;
 protected static string s_sSource;
 protected static string s_sStatus = STATUS_IDLE;
 protected static int s_iProgress;
 // Evidence for logs and fixtures.
 protected static int s_iStarts;
 protected static int s_iCompleted;
 protected static int s_iCloudSnaps;
 protected static int s_iForeignStops;

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
 // 0-1 by the real-time clock of the running transition.
 static float Elapsed01()
 {
  if (!IsRunning() || s_fDuration <= 0)
   return 1;
  return Math.Clamp((Now() - s_fStart) / s_fDuration, 0, 1);
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
  string target = requested;
  if (FindStateIndex(states, target) < 0)
   target = current;

  // A running transition is replaced from the values reached so far.
  bool wasRunning = IsRunning();
  string runningTarget = s_sTarget;
  float runningProgress = Elapsed01();
  s_bRunning = false;
  GetGame().GetCallqueue().Remove(ETW_WeatherRunner.Tick);

  s_Manager = manager;
  s_sTarget = target;
  s_bHold = hold;
  s_iPlayer = playerId;
  s_sSource = source;
  s_fDuration = Math.ClampInt(minutes, ETW_Settings.MINUTES_MIN, ETW_Settings.MINUTES_MAX) * 60;
  s_fStart = Now();
  s_fStateHours = StateHours(states, target);

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
  s_bCloudPending = false;
  bool moving = IsMoving(transitions);
  string next = NextName(transitions);
  bool adopt = moving && wasRunning && runningTarget == target && next == target;
  bool snapped = false;
  if (!adopt)
   s_CloudNode = null;
  if (moving && !adopt)
  {
   snapped = true;
   // The running blend completes at once to the nearer weather, then the new one starts.
   float progress = NativeProgress(transitions);
   if (wasRunning && runningTarget == next)
    progress = runningProgress;
   string nearer = current;
   if (progress >= 0.5 && !next.IsEmpty())
    nearer = next;
   Snap(transitions, nearer, true, s_fStateHours);
   Log(string.Format("running cloud blend %1 -> %2 completed at once to %3 (progress %4)", current, next, nearer, progress));
   current = nearer;
   moving = false;
  }
  s_bCloudChange = adopt || current != target;
  if (s_bCloudChange && !adopt)
   QueueClouds(manager, transitions, !snapped);
  else if (!s_bCloudChange && hold)
   ApplyLooping(transitions, true);

  if (s_aMode[CH_WIND_SPEED] != MODE_NONE || s_aMode[CH_WIND_DIR] != MODE_NONE)
   manager.ETW_SetWindFlag(true);

  s_bRunning = true;
  s_iStarts++;
  Log(string.Format("transition start: %1 -> %2 over %3 min, rain %4, fog %5, wind %6 m/s %7 deg, %8", current, target, s_fDuration / 60, Describe(CH_RAIN), Describe(CH_FOG), Describe(CH_WIND_SPEED), Describe(CH_WIND_DIR), HoldText()) + string.Format(", source %1, player %2", source, playerId));
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
 // nearer weather and hold.
 static bool Stop(int playerId)
 {
  if (!IsRunning())
  {
   SetStatus("Nothing to stop: no transition is running.", s_iProgress);
   return false;
  }
  BaseWeatherStateTransitionManager transitions = s_Manager.GetTransitionManager();
  int percent = Math.Round(Elapsed01() * 100);
  if (transitions)
  {
   if (IsMoving(transitions) || s_bCloudPending)
   {
    string nearer = CurrentName(transitions);
    if (!s_bCloudPending && Elapsed01() >= 0.5 && NextName(transitions) == s_sTarget)
     nearer = s_sTarget;
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
 // ForceWeatherTo from anything else (Scenario Properties weather or automated weather,
 // mission load): stop and hand back the values this transition overrode.
 static void OnForeignWeather()
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
  Halt("Stopped: the weather was changed in Scenario Properties; rain, fog and wind went back to the weather.", s_iProgress);
  Log("transition stopped: weather changed elsewhere (ForceWeatherTo)");
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
  float elapsed = Now() - s_fStart;
  float t = 1;
  if (s_fDuration > 0)
   t = Math.Clamp(elapsed / s_fDuration, 0, 1);
  float eased = t * t * (3 - 2 * t);
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
  if (s_bCloudPending)
   RetryClouds(transitions, elapsed);
  bool cloudsThere = !s_bCloudChange || (CurrentName(transitions) == s_sTarget && !IsMoving(transitions));
  if (t >= 1)
  {
   bool finishing = !cloudsThere && !s_bCloudPending && IsMoving(transitions) && NextName(transitions) == s_sTarget;
   if (!finishing || elapsed >= s_fDuration + CLOUD_GRACE_S)
   {
    Complete("finished");
    return;
   }
   SetStatus("Clouds are finishing the blend to " + s_sTarget + ".", 99);
   return;
  }
  int percent = Math.Round(t * 100);
  int left = Math.Ceil((s_fDuration - elapsed) / 60);
  SetStatus("Blending to " + s_sTarget + ": " + percent.ToString() + "%, about " + left.ToString() + " min left (then " + HoldText() + ").", percent);
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
   Snap(transitions, s_sTarget, s_bHold, s_fStateHours);
   Log(string.Format("clouds had not finished blending to %1 (%2): set at once", s_sTarget, reason));
  }
  else
   ApplyLooping(transitions, s_bHold);
  manager.ETW_SetLoopingFlag(s_bHold);
  manager.ETW_SetWindFlag(manager.IsWindSpeedOverridden() || manager.IsWindDirectionOverridden());
  s_iCompleted++;
  string after = "holding this weather.";
  if (!s_bHold)
   after = "automatic weather continues.";
  Halt("Done: " + s_sTarget + " reached; " + after, 100);
  Log(string.Format("transition done (%1): %2, snapped %3, rain %4, fog %5, wind %6 m/s %7 deg, %8", reason, s_sTarget, snapped, manager.GetRainIntensity(), manager.GetFogAmount(), manager.GetWindSpeed(), manager.GetWindDirection(), HoldText()));
 }

 //------------------------------------------------------------------------------------------------
 protected static void Halt(string status, int progress)
 {
  s_bRunning = false;
  s_bCloudPending = false;
  s_CloudNode = null;
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
 // Clouds: queue one node to the target after the current state and start it smoothly.
 // dropAhead: no instant change was requested in this call, so pending nodes queued ahead
 // of ours can be dropped safely.
 protected static void QueueClouds(notnull TimeAndWeatherManagerEntity manager, notnull BaseWeatherStateTransitionManager transitions, bool dropAhead)
 {
  float hours = s_fDuration * 24 / DayLength(manager);
  WeatherStateTransitionNode node = transitions.CreateStateTransition(s_sTarget, hours, s_fStateHours);
  if (!node)
  {
   Log("could not create a weather transition to " + s_sTarget + "; the clouds will be set at the end");
   return;
  }
  node.SetLooping(s_bHold);
  // Right after the current node, ahead of anything queued; nothing is removed
  // (RemoveStateTransition crashed the server natively with a null write).
  bool queued = transitions.GetStateTransitionsCount() >= 1 && transitions.InsertStateTransition(1, node, false);
  if (!queued)
   queued = transitions.EnqueueStateTransition(node, false);
  if (!queued)
  {
   Log("could not queue a weather transition to " + s_sTarget + "; the clouds will be set at the end");
   return;
  }
  s_CloudNode = node;
  if (dropAhead && !IsMoving(transitions))
   DropAhead(transitions, node);
  // A held (looping) weather would never move on by itself; this transition replaces it.
  WeatherStateTransitionNode head = transitions.GetCurrentStateTransitionNode();
  if (head && head != node)
   head.SetLooping(false);
  WeatherTransitionRequestResponse response = transitions.RequestStateTransition();
  s_bCloudPending = response != WeatherTransitionRequestResponse.SUCCESS;
  if (s_bCloudPending)
   Log(string.Format("smooth cloud transition to %1 not accepted yet (response %2); retrying", s_sTarget, response));
  else
   CheckStartedTarget();
 }

 //------------------------------------------------------------------------------------------------
 // RequestStateTransition() starts the head of the queue. A pending node queued ahead of
 // ours (an automatic next weather) would start instead, so it is dropped. The current
 // node, a node that is transitioning and our own node are always kept. Bounded.
 protected static void DropAhead(notnull BaseWeatherStateTransitionManager transitions, notnull WeatherStateTransitionNode node)
 {
  WeatherStateTransitionNode current = transitions.GetCurrentStateTransitionNode();
  int removed;
  for (int guard = 0; guard < 8; guard++)
  {
   if (transitions.GetStateTransitionsCount() < 2)
    return;
   WeatherStateTransitionNode first = transitions.GetStateTransitionNode(0);
   if (!first || first == node || first == current || first.IsTransitioning())
    return;
   if (!transitions.RemoveStateTransition(0, false, removed))
    return;
  }
 }

 //------------------------------------------------------------------------------------------------
 // Evidence: the engine accepted a smooth request; log when it is heading elsewhere (the
 // clouds are then set at the end and the fixture reports smooth=0).
 protected static void CheckStartedTarget()
 {
  if (!s_Manager || !s_Manager.GetTransitionManager())
   return;
  string next = NextName(s_Manager.GetTransitionManager());
  if (next != s_sTarget)
   Log(string.Format("smooth cloud transition accepted but heading to %1 instead of %2", next, s_sTarget));
 }

 //------------------------------------------------------------------------------------------------
 // A request refused while an immediate change settles is retried with the time left.
 protected static void RetryClouds(notnull BaseWeatherStateTransitionManager transitions, float elapsed)
 {
  if (!s_CloudNode)
  {
   s_bCloudPending = false;
   return;
  }
  float left = Math.Max(s_fDuration - elapsed, 1);
  s_CloudNode.SetTransitionDurationHours(left * 24 / DayLength(s_Manager));
  if (!IsMoving(transitions))
   DropAhead(transitions, s_CloudNode);
  WeatherStateTransitionNode head = transitions.GetCurrentStateTransitionNode();
  if (head && head != s_CloudNode)
   head.SetLooping(false);
  if (transitions.RequestStateTransition() == WeatherTransitionRequestResponse.SUCCESS)
  {
   s_bCloudPending = false;
   Log(string.Format("smooth cloud transition to %1 started, %2 s left", s_sTarget, left));
   CheckStartedTarget();
  }
 }

 //------------------------------------------------------------------------------------------------
 // Immediate state change through our own node (never ForceWeatherTo).
 protected static bool Snap(notnull BaseWeatherStateTransitionManager transitions, string stateName, bool looping, float stateHours)
 {
  if (stateName.IsEmpty())
   return false;
  WeatherStateTransitionNode node = transitions.CreateStateTransition(stateName, 0, stateHours);
  if (!node)
   return false;
  node.SetLooping(looping);
  // Our node goes right after the current one; nothing is removed from the queue here
  // (removing a queued node mid-blend crashed the server natively).
  bool queued = transitions.GetStateTransitionsCount() >= 1 && transitions.InsertStateTransition(1, node, false);
  if (!queued)
   queued = transitions.EnqueueStateTransition(node, false);
  if (!queued)
   return false;
  return transitions.RequestStateTransitionImmediately(node) == WeatherTransitionRequestResponse.SUCCESS;
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
