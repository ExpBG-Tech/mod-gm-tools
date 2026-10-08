// TEST ONLY. EXPBG Ambient Sounds finite-player retry rules (EAS_RadioState), the companion of
// tests/EAS_RadioStateGameplay.c. No audio: a dedicated server has no listener, so this proves
// the decisions EAS_RadioRuntime takes on clients, not the engine's playback.
// pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -FixturePath tests/EAS_RadioRetryGameplay.c -OrchestratorSlotGranted -ExpectResult '\[EAS TEST RESULT\] checks=[1-9]\d* failures=0 reason=completed'
// The runner copies this file to EXPG_GarrisonGameplay.c; the class names are fixed. Judge the
// run by its one [EAS TEST RESULT] line (12 checks); every check also prints an [EAS TEST CHECK] line.
// Cases:
//  cap         a looping 21.57 s emergency-alert TV evicted 2 s into every play backs off 3, 6,
//              12 s and then waits only for its own end plus its 1 s pause (20.57 s), never 24
//              or 30 s: a retry is never later than the schedule without eviction handling.
//  long pause  a TV with a 600 s pause still retries an eviction in 3 s.
//  completed   a TV evicted five times, with a complete play after each retry, retries in 3 s
//              every time: a recording heard to its end (Completed) ends the eviction streak.
//  streak      back-to-back evictions of a 177.7 s radio still back off (3, then 6 s); after a
//              complete play the next eviction retries in 3 s again.
//  clocks      the real clock (EndedEarlyHeard) also needs an early end; a natural end that
//              lagging world time sees as early after a client hitch is not an eviction.
//  notices     refused starts log only the first and third notice per placement; starts do not
//              refill that budget; a settings change (Restart) does.
class EXPG_GarrisonGameplayClass : GenericEntityClass {}
class EXPG_GarrisonGameplay : GenericEntity
{
 static const float FIXTURE_SECONDS = 60;
 int m_iChecks;
 int m_iFailures;
 float m_fStarted;
 float m_fNext;
 bool m_bFinished;

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
  m_fNext = m_fStarted + 2;
  PrintFormat("[EAS TEST BEGIN] deadline=%1", FIXTURE_SECONDS);
 }

 //------------------------------------------------------------------------------------------------
 bool Check(bool ok, string label)
 {
  m_iChecks++;
  if (!ok)
   m_iFailures++;
  PrintFormat("[EAS TEST CHECK] pass=%1 %2", ok, label);
  return ok;
 }

 //------------------------------------------------------------------------------------------------
 bool Near(float value, float expected)
 {
  return Math.AbsFloat(value - expected) < 0.001;
 }

 //------------------------------------------------------------------------------------------------
 // EAS_TVBank.Duration(0): SOUND_EAS_EMERGENCYALERT.
 float TvLength()
 {
  return 21.5713333;
 }

 //------------------------------------------------------------------------------------------------
 // Recording 0, volume 20, on, looping, the given pause, 30 m, debug off.
 EAS_RadioState NewSource(int pause)
 {
  EAS_RadioState state = new EAS_RadioState();
  array<int> values = {0, 20, 1, 1, 1, 30, 0};
  values[4] = pause;
  if (!state.Apply(values))
   Check(false, "source settings apply");
  state.Restart(0);
  return state;
 }

 //------------------------------------------------------------------------------------------------
 void CheckCap()
 {
  EAS_RadioState tv = NewSource(1);
  float now = 10;
  float uncapped = 3;
  bool capped = true;
  bool delays = true;
  string got;
  for (int i = 0; i < 5; i++)
  {
   tv.Started(now, TvLength(), 0);
   float natural = now + TvLength() + 1;
   now += 2;
   float delay = tv.Evicted(now);
   got += string.Format(" %1", delay);
   if (tv.NextDue > natural + 0.001)
    capped = false;
   float wanted = Math.Min(Math.Min(uncapped, 30), natural - now);
   if (!Near(delay, wanted) || !Near(tv.NextDue, now + wanted))
    delays = false;
   uncapped *= 2;
   now = tv.NextDue + 0.1;
  }
  Check(capped, "an evicted TV is never due later than its own end plus its pause (delays" + got + ")");
  Check(delays, "a TV evicted 2 s into every play backs off 3, 6, 12 s, then 20.57 s, not 24 or 30 s (got" + got + ")");
  EAS_RadioState paused = NewSource(600);
  paused.Started(10, TvLength(), 0);
  float pausedDelay = paused.Evicted(12);
  Check(Near(pausedDelay, 3) && Near(paused.NextDue, 15), string.Format("a TV with a 600 s pause retries an eviction in 3 s (got %1)", pausedDelay));
 }

 //------------------------------------------------------------------------------------------------
 void CheckCompleted()
 {
  EAS_RadioState tv = NewSource(1);
  float now = 10;
  bool fresh = true;
  string got;
  for (int i = 0; i < 5; i++)
  {
   tv.Started(now, TvLength(), 0);
   now += 2;
   float delay = tv.Evicted(now);
   got += string.Format(" %1", delay);
   if (!Near(delay, 3))
    fresh = false;
   now = tv.NextDue + 0.1;
   if (!tv.CanStart(now, 0))
    fresh = false;
   // The retry plays to its end; the runtime's finished path calls Completed.
   tv.Started(now, TvLength(), 0);
   now += TvLength() + 0.3;
   tv.Completed();
   now = Math.Max(tv.NextDue, now) + 0.1;
  }
  Check(fresh, "a TV evicted five times with a complete play after each retry retries in 3 s every time (got" + got + ")");
  EAS_RadioState radio = NewSource(1);
  radio.Started(10, 177.707, 2);
  float first = radio.Evicted(12);
  radio.Started(15.1, 177.707, 2);
  float second = radio.Evicted(17.1);
  Check(Near(first, 3) && Near(second, 6), string.Format("back-to-back evictions still back off (got %1, %2)", first, second));
  radio.Started(23.2, 177.707, 2);
  radio.Completed();
  radio.Started(202, 177.707, 2);
  float afterPlay = radio.Evicted(204);
  Check(Near(afterPlay, 3), string.Format("after a complete play the next eviction retries in 3 s (got %1)", afterPlay));
 }

 //------------------------------------------------------------------------------------------------
 void CheckClocks()
 {
  EAS_RadioState clock = NewSource(1);
  Check(!clock.EndedEarlyHeard(1), "without a recording the real clock never reports an early end");
  clock.Started(100, 177.707, 2);
  Check(clock.EndedEarlyHeard(12) && !clock.EndedEarlyHeard(176) && !clock.EndedEarlyHeard(180), "the real clock sees an end 12 s in as early and one within 2 s of the recording as natural");
  EAS_RadioState hitch = NewSource(600);
  hitch.Started(100, TvLength(), 0);
  float worldEnd = 100 + TvLength() + 0.25;
  Check(EAS_RadioState.EndedEarly(115, worldEnd) && !hitch.EndedEarlyHeard(21.6), "a natural end that lagging world time sees as early (115 of 121.82 s) is not an eviction on the real clock");
  clock.Evicted(112);
  Check(!clock.EndedEarlyHeard(1), "an evicted voice has no recording left to end early");
 }

 //------------------------------------------------------------------------------------------------
 void CheckNotices()
 {
  EAS_RadioState noisy = NewSource(1);
  string pattern;
  for (int i = 0; i < 6; i++)
  {
   noisy.Refused(i * 40);
   if (noisy.Notice())
    pattern += "1";
   else
    pattern += "0";
   noisy.Started(i * 40 + 1, TvLength(), 0);
  }
  Check(pattern == "101000", "refused starts log only the first and third notice; starts do not refill the budget (got " + pattern + ")");
  noisy.Restart(500);
  Check(noisy.Notice(), "a settings change re-arms the log budget");
 }

 //------------------------------------------------------------------------------------------------
 override void EOnFrame(IEntity owner, float timeSlice)
 {
  if (m_bFinished || Now() < m_fNext)
   return;
  if (Now() - m_fStarted > FIXTURE_SECONDS)
  {
   Check(false, "checks finished before the fixture deadline");
   Finish("timeout");
   return;
  }
  CheckCap();
  CheckCompleted();
  CheckClocks();
  CheckNotices();
  Finish("completed");
 }

 //------------------------------------------------------------------------------------------------
 void Finish(string reason)
 {
  if (m_bFinished)
   return;
  m_bFinished = true;
  ClearEventMask(EntityEvent.FRAME);
  PrintFormat("[EAS TEST RESULT] checks=%1 failures=%2 reason=%3", m_iChecks, m_iFailures, reason);
  GetGame().RequestClose();
 }
}
