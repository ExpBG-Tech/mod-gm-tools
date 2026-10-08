// TEST ONLY. EXPBG Time and Weather cloud probe: how the engine's weather state queue starts
// a queued node, measured directly on the transition manager (no Weather Transition module,
// no players). Evidence for ETW_WeatherRunner's direct / pinned / start-request paths.
// pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -FixturePath tests/ETW_CloudProbeGameplay.c -TimeoutSeconds 420 -OrchestratorSlotGranted -ExpectResult '\[ETW PROBE RESULT\] checks=[1-9]\d* failures=0 direct=\w+ directAuto=\w+ directEarly=\w+ pin=\w+ kick=\w+ paused=\w+ predicted=\S+ pinHold=\S+ blendFloor=\S+ headHold=\S+ setBlend=\S+ setHold=\S+ queueB=-?\d+ reason=complete'
// The runner copies this file to EXPG_GarrisonGameplay.c; the class names are fixed. Judge
// the run by its one [ETW PROBE RESULT] line; every case also prints [ETW PROBE QUEUE]
// lines (node count, the node in place, the first queued nodes, whether the node in place
// is queue entry 0, where our node sits, the time left until the next weather).
// The day is 24 real minutes (SetDayDuration(1440): one in-game minute per real second), so
// the engine's 10-in-game-minute node minimum (0.167 h, also the hold ForceWeatherTo leaves)
// is 10 real seconds. A is the weather in place (Clear when the terrain has it), B another
// one (Rainy when there is one). Direct cases report whole seconds until B transitions
// (e.g. 0s), misdirected (something else moves) or never (nothing within 15 s):
//  direct       ForceWeatherTo(true, A) (held, as Scenario Properties weather does), then
//               HOLD_WAIT (12 s, longer than the hold) later node B inserted at 1 and the
//               node in place unlooped with the shortest hold. Expected: 0s.
//  directAuto   the same after ForceWeatherTo(false, A) (automatic weather) with the node
//               in place given a long hold first, as in a mission. Expected: 0s.
//  directEarly  as direct, but only EARLY_WAIT (3 s) after ForceWeatherTo, inside the hold:
//               about 7s when a direct start waits out the hold left over (the runner's
//               DIRECT_WAITS_HOLD), about 10s when setting the hold restarts it, never when
//               it does not start at all (set DIRECT_WAITS_HOLD false).
//  predicted    hold left over in real seconds as the runner reads it (time left until the
//               next weather minus B's blend), direct case / early case (expected 0/7).
//  pin          pin A and node B at the back of the queue, the pin set at once
//               (RequestStateTransitionImmediately): whole seconds until B transitions
//               (expected: about the pin's hold, 10s), or never (25 s).
//  kick         as pin, then one RequestStateTransition() 0.5 s later: started (B
//               transitions within 3 s), misdirected (something else moves) or nothing.
//  paused       as pin with day auto-advance off: blend (B transitions within 25 s) or wait.
//  pinHold / blendFloor  the pin's hold and B's blend as the engine kept them, in in-game
//               hours (asked: 0.001 and 0.001 on new nodes; expected about 0.167).
//  headHold     the hold of the node in place after SetStateDurationHours(0.001) in the held
//               direct case: about 0.167 when the hold setter is raised to the minimum,
//               0.001 when it is not (the direct start then waits out no hold at all).
//  setBlend / setHold  a node already queued (behind the held weather, so it never plays)
//               given SetTransitionDurationHours(0.001) and SetStateDurationHours(0.001),
//               read back. About 0.167: the setters are raised to the engine minimum.
//               0.001: they are not, and only the runner's SetBlend floor keeps a shortened
//               cloud blend from switching the sky at once.
//  queueB       queue index of node B right after the held direct case queued it (0 or 1).
//               -1: the engine does not hand back the same node object, so the runner can
//               never find its node by identity, always pins and never asks for a start.
class EXPG_GarrisonGameplayClass : GenericEntityClass {}
class EXPG_GarrisonGameplay : GenericEntity
{
 static const float FIXTURE_SECONDS = 240;
 static const float DAY_SECONDS = 1440;
 static const float SHORTEST_HOURS = 0.001;
 static const float HOLD_WAIT = 12;
 static const float EARLY_WAIT = 3;
 static const float DIRECT_SECONDS = 15;
 static const float KICK_SECONDS = 3;
 static const float PIN_SECONDS = 25;
 TimeAndWeatherManagerEntity m_TimeManager;
 BaseWeatherStateTransitionManager m_Transitions;
 ref WeatherStateTransitionNode m_Node;
 ref WeatherStateTransitionNode m_Pin;
 int m_iPhase;
 int m_iChecks;
 int m_iFailures;
 float m_fStarted;
 float m_fNext;
 float m_fCaseAt;
 bool m_bFinished;
 float m_fDayLength;
 bool m_bAutoAdvance;
 string m_sA;
 string m_sB;
 string m_sDirect = "none";
 string m_sDirectAuto = "none";
 string m_sDirectEarly = "none";
 string m_sPin = "none";
 string m_sKick = "none";
 string m_sPaused = "none";
 float m_fPredicted = -1;
 float m_fPredictedDirect = -1;
 float m_fPredictedEarly = -1;
 float m_fPinHold = -1;
 float m_fBlendFloor = -1;
 float m_fHeadHold = -1;
 float m_fSetBlend = -1;
 float m_fSetHold = -1;
 int m_iQueueB = -2;

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
  PrintFormat("[ETW PROBE BEGIN] deadline=%1", FIXTURE_SECONDS);
 }

 //------------------------------------------------------------------------------------------------
 bool Check(bool ok, string label)
 {
  m_iChecks++;
  if (!ok)
   m_iFailures++;
  PrintFormat("[ETW PROBE CHECK] pass=%1 %2", ok, label);
  return ok;
 }

 //------------------------------------------------------------------------------------------------
 void PauseFor(float seconds)
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
  {
   m_TimeManager.SetIsDayAutoAdvanced(m_bAutoAdvance);
   if (m_fDayLength > 0)
    m_TimeManager.SetDayDuration(m_fDayLength);
  }
  string predicted = string.Format("%1/%2", Math.Round(m_fPredictedDirect), Math.Round(m_fPredictedEarly));
  string head = string.Format("[ETW PROBE RESULT] checks=%1 failures=%2 direct=%3 directAuto=%4 directEarly=%5 pin=%6 kick=%7 paused=%8", m_iChecks, m_iFailures, m_sDirect, m_sDirectAuto, m_sDirectEarly, m_sPin, m_sKick, m_sPaused);
  string tail = string.Format(" predicted=%1 pinHold=%2 blendFloor=%3 headHold=%4 setBlend=%5 setHold=%6 queueB=%7 reason=%8", predicted, m_fPinHold, m_fBlendFloor, m_fHeadHold, m_fSetBlend, m_fSetHold, m_iQueueB, reason);
  Print(head + tail, LogLevel.NORMAL);
  GetGame().RequestClose();
 }

 //------------------------------------------------------------------------------------------------
 string StateLabel(WeatherState state)
 {
  if (!state)
   return "none";
  return state.GetStateName();
 }

 //------------------------------------------------------------------------------------------------
 // Real seconds of in-game hours at the probe's day length.
 float HoursToReal(float hours)
 {
  return hours * DAY_SECONDS / 24;
 }

 //------------------------------------------------------------------------------------------------
 bool AnyMoving()
 {
  int count = m_Transitions.GetStateTransitionsCount();
  if (count > 4)
   count = 4;
  for (int i = 0; i < count; i++)
  {
   WeatherStateTransitionNode node = m_Transitions.GetStateTransitionNode(i);
   if (node && node.IsTransitioning())
    return true;
  }
  WeatherStateTransitionNode head = m_Transitions.GetCurrentStateTransitionNode();
  return head != null && head.IsTransitioning();
 }

 //------------------------------------------------------------------------------------------------
 int QueueIndexOf(WeatherStateTransitionNode wanted)
 {
  if (!wanted)
   return -1;
  int count = m_Transitions.GetStateTransitionsCount();
  if (count > 8)
   count = 8;
  for (int i = 0; i < count; i++)
  {
   if (m_Transitions.GetStateTransitionNode(i) == wanted)
    return i;
  }
  return -1;
 }

 //------------------------------------------------------------------------------------------------
 string DescribeNode(WeatherStateTransitionNode node)
 {
  if (!node)
   return "none";
  string owner = string.Empty;
  if (node == m_Node)
   owner = " B";
  else if (node == m_Pin)
   owner = " pin";
  return string.Format("(to %1 blend %2 hold %3 loop %4 moving %5%6)", node.GetDestinationStateIndex(), node.GetTransitionDurationHours(), node.GetStateDurationHours(), node.IsLooping(), node.IsTransitioning(), owner);
 }

 //------------------------------------------------------------------------------------------------
 void LogQueue(string tag)
 {
  int count = m_Transitions.GetStateTransitionsCount();
  WeatherStateTransitionNode head = m_Transitions.GetCurrentStateTransitionNode();
  bool headFirst = count > 0 && m_Transitions.GetStateTransitionNode(0) == head;
  string text = string.Format("[ETW PROBE QUEUE] %1 t=%2 count=%3 state=%4 next=%5 left=%6 inPlaceIsFirst=%7 B=%8 pin=%9", tag, Now() - m_fCaseAt, count, StateLabel(m_Transitions.GetCurrentState()), StateLabel(m_Transitions.GetNextState()), m_Transitions.GetTimeLeftUntilNextState(), headFirst, QueueIndexOf(m_Node), QueueIndexOf(m_Pin));
  text += " inPlace=" + DescribeNode(head);
  int shown = count;
  if (shown > 5)
   shown = 5;
  for (int i = 0; i < shown; i++)
  {
   text += " [" + i.ToString() + "]=" + DescribeNode(m_Transitions.GetStateTransitionNode(i));
  }
  Print(text, LogLevel.NORMAL);
 }

 //------------------------------------------------------------------------------------------------
 // Back to A at once, held or automatic, through the vanilla path (drops every queued node).
 // The forced node holds for the engine's minimum (10 s here) before anything queued starts.
 void Normalize(bool looping)
 {
  m_Node = null;
  m_Pin = null;
  m_TimeManager.ForceWeatherTo(looping, m_sA);
 }

 //------------------------------------------------------------------------------------------------
 // Node B right behind the node in place; the node in place stops looping, shortest hold.
 // Records the hold left over as the runner reads it (time left minus B's blend).
 void QueueDirect()
 {
  m_fCaseAt = Now();
  m_Node = m_Transitions.CreateStateTransition(m_sB, 0.25, 1);
  if (!Check(m_Node != null, "node B created"))
  {
   Finish("setup");
   return;
  }
  m_Node.SetLooping(true);
  bool queued;
  if (m_Transitions.GetStateTransitionsCount() >= 1)
   queued = m_Transitions.InsertStateTransition(1, m_Node, false);
  else
   queued = m_Transitions.EnqueueStateTransition(m_Node, false);
  WeatherStateTransitionNode head = m_Transitions.GetCurrentStateTransitionNode();
  float headHold = -1;
  if (head && head != m_Node)
  {
   head.SetLooping(false);
   head.SetStateDurationHours(SHORTEST_HOURS);
   headHold = head.GetStateDurationHours();
  }
  float timeLeft = m_Transitions.GetTimeLeftUntilNextState();
  m_fPredicted = HoursToReal(timeLeft - m_Node.GetTransitionDurationHours());
  // The held case (the first) records the hold setter's read-back and B's queue index.
  if (m_iQueueB == -2)
  {
   m_fHeadHold = headHold;
   m_iQueueB = QueueIndexOf(m_Node);
  }
  PrintFormat("[ETW PROBE] direct queued=%1 headHold=%2 timeLeft=%3 blend=%4 predicted=%5 s B=%6", queued, headHold, timeLeft, m_Node.GetTransitionDurationHours(), m_fPredicted, QueueIndexOf(m_Node));
  LogQueue("direct queued");
 }

 //------------------------------------------------------------------------------------------------
 // A node already queued behind the held weather (it never plays) given the shortest blend
 // and hold through the setters, read back: are the setters raised to the engine minimum?
 void ProbeSetters()
 {
  m_fCaseAt = Now();
  m_Node = m_Transitions.CreateStateTransition(m_sB, 0.25, 1);
  if (!Check(m_Node != null, "setter node created"))
  {
   Finish("setup");
   return;
  }
  m_Node.SetLooping(true);
  bool queued = m_Transitions.EnqueueStateTransition(m_Node, false);
  float keptBlend = m_Node.GetTransitionDurationHours();
  float keptHold = m_Node.GetStateDurationHours();
  m_Node.SetTransitionDurationHours(SHORTEST_HOURS);
  m_Node.SetStateDurationHours(SHORTEST_HOURS);
  m_fSetBlend = m_Node.GetTransitionDurationHours();
  m_fSetHold = m_Node.GetStateDurationHours();
  Check(queued, "setter node queued behind the held weather");
  PrintFormat("[ETW PROBE] setters queued=%1 index=%2 asked=%3 before blend=%4 hold=%5 after blend=%6 hold=%7 headHold=%8 blendFloor=%9", queued, QueueIndexOf(m_Node), SHORTEST_HOURS, keptBlend, keptHold, m_fSetBlend, m_fSetHold, m_fHeadHold, m_fBlendFloor);
  LogQueue("setters");
 }

 //------------------------------------------------------------------------------------------------
 // Pin A and node B at the back of the queue; the pin is set at once.
 void QueuePinned()
 {
  m_fCaseAt = Now();
  m_Pin = m_Transitions.CreateStateTransition(m_sA, SHORTEST_HOURS, SHORTEST_HOURS);
  m_Node = m_Transitions.CreateStateTransition(m_sB, SHORTEST_HOURS, 1);
  if (!Check(m_Pin != null && m_Node != null, "pin and node created"))
  {
   Finish("setup");
   return;
  }
  m_Pin.SetLooping(false);
  bool pinQueued = m_Transitions.EnqueueStateTransition(m_Pin, false);
  m_Node.SetLooping(true);
  bool nodeQueued = m_Transitions.EnqueueStateTransition(m_Node, false);
  m_fPinHold = m_Pin.GetStateDurationHours();
  m_fBlendFloor = m_Node.GetTransitionDurationHours();
  LogQueue("pin queued");
  WeatherTransitionRequestResponse response = m_Transitions.RequestStateTransitionImmediately(m_Pin);
  PrintFormat("[ETW PROBE] pin queued=%1 node queued=%2 immediate=%3 pinHold=%4 blendFloor=%5", pinQueued, nodeQueued, response, m_fPinHold, m_fBlendFloor);
  LogQueue("pin set");
 }

 //------------------------------------------------------------------------------------------------
 // <whole seconds>s / misdirected / never after a direct start, or "" while still looking.
 string DirectVerdict()
 {
  float elapsed = Now() - m_fCaseAt;
  if (m_Node.IsTransitioning())
  {
   int whole = Math.Round(elapsed);
   return whole.ToString() + "s";
  }
  if (AnyMoving())
   return "misdirected";
  if (elapsed >= DIRECT_SECONDS)
   return "never";
  return string.Empty;
 }

 //------------------------------------------------------------------------------------------------
 // started / misdirected / nothing within KICK_SECONDS of the start request (shorter than the
 // pin's hold, so a start at the end of the hold is not taken for the request's).
 string KickVerdict()
 {
  if (m_Node.IsTransitioning())
   return "started";
  if (AnyMoving())
   return "misdirected";
  if (Now() - m_fCaseAt >= KICK_SECONDS)
   return "nothing";
  return string.Empty;
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
   StartDirect("held");
  else if (m_iPhase == 2)
   SampleDirect();
  else if (m_iPhase == 3)
   StartDirect("automatic");
  else if (m_iPhase == 4)
   SampleDirect();
  else if (m_iPhase == 5)
   StartDirect("early");
  else if (m_iPhase == 6)
   SampleDirect();
  else if (m_iPhase == 7)
   StartPin();
  else if (m_iPhase == 8)
   SamplePin();
  else if (m_iPhase == 9)
   StartKick();
  else if (m_iPhase == 10)
   SendStartRequest();
  else if (m_iPhase == 11)
   SampleKick();
  else if (m_iPhase == 12)
   StartPaused();
  else if (m_iPhase == 13)
   SamplePaused();
  else if (m_iPhase == 14)
   SampleSetters();
 }

 //------------------------------------------------------------------------------------------------
 void Setup()
 {
  m_TimeManager = ETW_WeatherRunner.Manager();
  if (m_TimeManager)
   m_Transitions = m_TimeManager.GetTransitionManager();
  if (!Check(m_TimeManager != null && m_Transitions != null, "server time and weather manager with a transition manager"))
  {
   Finish("setup");
   return;
  }
  array<ref WeatherState> states = {};
  m_TimeManager.GetWeatherStatesList(states);
  m_sA = string.Empty;
  m_sB = string.Empty;
  string listed = string.Empty;
  foreach (WeatherState state : states)
  {
   if (!state)
    continue;
   listed += " " + state.GetStateName();
   if (m_sA.IsEmpty() || state.GetStateName() == "Clear")
    m_sA = state.GetStateName();
  }
  foreach (WeatherState other : states)
  {
   if (!other || other.GetStateName() == m_sA)
    continue;
   if (m_sB.IsEmpty() || other.GetStateName() == "Rainy")
    m_sB = other.GetStateName();
  }
  PrintFormat("[ETW PROBE] states:%1 A=%2 B=%3 day=%4 autoAdvance=%5", listed, m_sA, m_sB, m_TimeManager.GetDayDuration(), m_TimeManager.GetIsDayAutoAdvanced());
  if (!Check(!m_sA.IsEmpty() && !m_sB.IsEmpty(), "two weather states"))
  {
   Finish("setup");
   return;
  }
  LogQueue("mission start");
  m_bAutoAdvance = m_TimeManager.GetIsDayAutoAdvanced();
  m_fDayLength = m_TimeManager.GetDayDuration();
  m_TimeManager.SetIsDayAutoAdvanced(true);
  Check(m_TimeManager.SetDayDuration(DAY_SECONDS), "day length set to 1440 s");
  Normalize(true);
  m_iPhase = 1;
  PauseFor(HOLD_WAIT);
 }

 //------------------------------------------------------------------------------------------------
 void StartDirect(string mode)
 {
  LogQueue(mode);
  QueueDirect();
  m_iPhase++;
  PauseFor(0.5);
 }

 //------------------------------------------------------------------------------------------------
 // Phase 2 (held), 4 (automatic) or 6 (early): verdict, then set up the next case.
 void SampleDirect()
 {
  string verdict = DirectVerdict();
  if (verdict.IsEmpty())
  {
   PauseFor(0.5);
   return;
  }
  LogQueue("direct " + verdict);
  if (m_iPhase == 2)
  {
   m_sDirect = verdict;
   m_fPredictedDirect = m_fPredicted;
   // Automatic weather with the long hold of a mission's weather, past the engine minimum.
   Normalize(false);
   WeatherStateTransitionNode head = m_Transitions.GetCurrentStateTransitionNode();
   if (head)
    head.SetStateDurationHours(1);
   m_iPhase = 3;
   PauseFor(HOLD_WAIT);
   return;
  }
  if (m_iPhase == 4)
  {
   m_sDirectAuto = verdict;
   // Held again, but queued inside the hold ForceWeatherTo leaves.
   Normalize(true);
   m_iPhase = 5;
   PauseFor(EARLY_WAIT);
   return;
  }
  m_sDirectEarly = verdict;
  m_fPredictedEarly = m_fPredicted;
  Normalize(true);
  m_iPhase = 7;
  PauseFor(3);
 }

 //------------------------------------------------------------------------------------------------
 void StartPin()
 {
  LogQueue("held");
  QueuePinned();
  m_iPhase = 8;
  PauseFor(0.5);
 }

 //------------------------------------------------------------------------------------------------
 void SamplePin()
 {
  float elapsed = Now() - m_fCaseAt;
  if (m_Node.IsTransitioning())
  {
   int whole = Math.Round(elapsed);
   m_sPin = whole.ToString() + "s";
  }
  else if (elapsed >= PIN_SECONDS)
   m_sPin = "never";
  else
  {
   if (Math.AbsFloat(elapsed - 5) < 0.3)
    LogQueue("pin waiting");
   PauseFor(0.5);
   return;
  }
  LogQueue("pin " + m_sPin);
  Normalize(true);
  m_iPhase = 9;
  PauseFor(3);
 }

 //------------------------------------------------------------------------------------------------
 void StartKick()
 {
  QueuePinned();
  m_iPhase = 10;
  PauseFor(0.5);
 }

 //------------------------------------------------------------------------------------------------
 void SendStartRequest()
 {
  LogQueue("before the start request");
  WeatherTransitionRequestResponse response = m_Transitions.RequestStateTransition();
  PrintFormat("[ETW PROBE] start request response=%1 B index before=%2", response, QueueIndexOf(m_Node));
  m_fCaseAt = Now();
  m_iPhase = 11;
  PauseFor(0.5);
 }

 //------------------------------------------------------------------------------------------------
 void SampleKick()
 {
  string verdict = KickVerdict();
  if (verdict.IsEmpty())
  {
   PauseFor(0.5);
   return;
  }
  m_sKick = verdict;
  LogQueue("start request " + verdict);
  Normalize(true);
  m_TimeManager.SetIsDayAutoAdvanced(false);
  m_iPhase = 12;
  PauseFor(3);
 }

 //------------------------------------------------------------------------------------------------
 void StartPaused()
 {
  QueuePinned();
  m_iPhase = 13;
  PauseFor(0.5);
 }

 //------------------------------------------------------------------------------------------------
 void SamplePaused()
 {
  float elapsed = Now() - m_fCaseAt;
  if (m_Node.IsTransitioning())
   m_sPaused = "blend";
  else if (elapsed >= PIN_SECONDS)
   m_sPaused = "wait";
  else
  {
   PauseFor(0.5);
   return;
  }
  LogQueue("paused " + m_sPaused);
  m_TimeManager.SetIsDayAutoAdvanced(true);
  Normalize(true);
  m_iPhase = 14;
  PauseFor(3);
 }

 //------------------------------------------------------------------------------------------------
 // Last: the setter read-back on a queued node, then back to A held.
 void SampleSetters()
 {
  ProbeSetters();
  if (m_bFinished)
   return;
  Normalize(true);
  Check(true, "all cases ran");
  Finish("complete");
 }
}
