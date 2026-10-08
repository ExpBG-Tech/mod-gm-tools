// TEST ONLY. EXPBG Ambient Sounds finite-player timing (EAS_RadioState), the pure state behind
// placed radios, TVs, crowds and placed sounds. No audio: a dedicated server has no listener, so
// this proves the decisions EAS_RadioRuntime takes on clients, not the engine's playback.
// pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -FixturePath tests/EAS_RadioStateGameplay.c -OrchestratorSlotGranted -ExpectResult '\[EAS TEST RESULT\] checks=[1-9]\d* failures=0 reason=completed'
// The runner copies this file to EXPG_GarrisonGameplay.c; the class names are fixed. Judge the
// run by its one [EAS TEST RESULT] line; every check also prints an [EAS TEST CHECK] line.
// Cases:
//  start range   starts keep a 10% margin (at least 1 m) inside the audible range: 27 of 30 m.
//  early end     an end at least 2 s before the recording's own end counts as an eviction.
//  eviction      a looping radio evicted 12 s into a 177.7 s recording is due again within
//                5 s (3 s), not after the rest of the recording; back-to-back evictions back
//                off 3, 6, 12, 24, 30, 30 s; a voice that lasted 60 s starts a new streak.
//  one-shot      an evicted one-shot stays used, as when its listener leaves.
//  inaudible     ten refusals below audibility never park the radio; each retries in 3 s.
//  refused       other refusals back off 5, 10 ... 30 s and never park the radio.
//  metadata      three invalid-metadata tries park the module until its settings change.
//  voices        four radios in range play at once; a fifth waits for a free voice.
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
 void Finish(string reason)
 {
  if (m_bFinished)
   return;
  m_bFinished = true;
  ClearEventMask(EntityEvent.FRAME);
  PrintFormat("[EAS TEST RESULT] checks=%1 failures=%2 reason=%3", m_iChecks, m_iFailures, reason);
  GetGame().RequestClose();
 }

 //------------------------------------------------------------------------------------------------
 // Recording 2 (Apache2, 177.707 s), volume 20, on, looping, 1 s pause, 30 m, debug off.
 EAS_RadioState NewRadio(int loop = 1)
 {
  EAS_RadioState state = new EAS_RadioState();
  array<int> values = {2, 20, 1, 1, 1, 30, 0};
  values[3] = loop;
  if (!state.Apply(values))
   Check(false, "radio settings apply");
  state.Restart(0);
  return state;
 }

 //------------------------------------------------------------------------------------------------
 void CheckRanges()
 {
  Check(Near(EAS_RadioState.StartRange(30), 27), string.Format("radio starts within 27 m of its 30 m (got %1)", EAS_RadioState.StartRange(30)));
  Check(Near(EAS_RadioState.StartRange(10), 9), string.Format("10 m crowd starts within 9 m (got %1)", EAS_RadioState.StartRange(10)));
  Check(Near(EAS_RadioState.StartRange(150), 135), string.Format("150 m crowd starts within 135 m (got %1)", EAS_RadioState.StartRange(150)));
  Check(Near(EAS_RadioState.StartRange(1500), 1350), string.Format("1500 m sound starts within 1350 m (got %1)", EAS_RadioState.StartRange(1500)));
  Check(EAS_RadioState.EndedEarly(22, 187.957) && EAS_RadioState.EndedEarly(185.9, 187.957), "an end 2 s or more before the recording is early");
  Check(!EAS_RadioState.EndedEarly(186, 187.957) && !EAS_RadioState.EndedEarly(187.957, 187.957), "a natural end within 2 s is not early");
 }

 //------------------------------------------------------------------------------------------------
 void CheckEviction()
 {
  EAS_RadioState radio = NewRadio();
  Check(radio.CanStart(0.25, 0), "a fresh radio may start");
  radio.Started(10, 177.707, 2);
  Check(!radio.CanStart(100, 0), "a started recording owns its length");
  float retry = radio.Evicted(22);
  Check(Near(retry, 3) && Near(radio.NextDue, 25), string.Format("eviction 12 s in retries in 3 s (retry %1 due %2)", retry, radio.NextDue));
  Check(!radio.CanStart(24.9, 0) && radio.CanStart(25.01, 0), "the evicted radio is due again within 5 s, not after its recording");
  array<float> expected = {6.0, 12.0, 24.0, 30.0, 30.0};
  float now = 25.1;
  string got;
  bool backoff = true;
  foreach (float wanted : expected)
  {
   radio.Started(now, 177.707, 2);
   now += 1;
   retry = radio.Evicted(now);
   got += string.Format(" %1", retry);
   if (!Near(retry, wanted) || !Near(radio.NextDue, now + wanted))
    backoff = false;
   now = radio.NextDue + 0.1;
  }
  Check(backoff, "back-to-back evictions back off 6, 12, 24, 30, 30 s (got" + got + ")");
  radio.Started(now, 177.707, 2);
  retry = radio.Evicted(now + 70);
  Check(Near(retry, 3), string.Format("a voice that lasted 70 s starts a new streak (retry %1)", retry));
  radio.Started(200, 177.707, 2);
  radio.Interrupted(205);
  Check(radio.CanStart(205.01, 0), "leaving range still frees an unfinished recording at once");
  EAS_RadioState once = NewRadio(0);
  once.Started(10, 177.707, 2);
  once.Evicted(22);
  Check(!once.CanStart(60, 0), "an evicted one-shot stays used");
 }

 //------------------------------------------------------------------------------------------------
 void CheckRefusals()
 {
  EAS_RadioState radio = NewRadio();
  bool inaudible = true;
  float now = 1;
  for (int i = 1; i <= 10; i++)
  {
   if (radio.Inaudible(now) != i || !Near(radio.NextDue, now + 3) || !radio.CanStart(now + 3.01, 0))
    inaudible = false;
   now += 3.01;
  }
  Check(inaudible, "ten inaudible refusals never park the radio; each retries in 3 s");
  radio.Started(now, 177.707, 2);
  Check(radio.Inaudible(now + 200) == 1, "a start ends the inaudible streak");
  EAS_RadioState refused = NewRadio();
  bool capped = true;
  now = 1;
  for (int n = 1; n <= 10; n++)
  {
   float backoffSeconds = Math.Min(5 * n, 30);
   if (refused.Refused(now) != n || !Near(refused.NextDue, now + backoffSeconds) || !refused.CanStart(now + backoffSeconds + 0.01, 0))
    capped = false;
   now += backoffSeconds + 0.01;
  }
  Check(capped, "ten refused starts back off 5 ... 30 s and never park the radio");
  refused.Started(now, 177.707, 2);
  Check(refused.Refused(now + 200) == 1, "a start resets the refusal count");
  EAS_RadioState broken = NewRadio();
  broken.FailedStart(1);
  broken.FailedStart(7);
  Check(broken.CanStart(17.01, 0), "two invalid-metadata tries still retry");
  broken.FailedStart(18);
  Check(!broken.CanStart(1000, 0), "three invalid-metadata tries park the module");
  broken.Restart(1000);
  Check(broken.CanStart(1000, 0), "a settings change re-arms a parked module");
 }

 //------------------------------------------------------------------------------------------------
 void CheckVoices()
 {
  int voices;
  for (int i = 0; i < 5; i++)
  {
   EAS_RadioState radio = NewRadio();
   if (!radio.CanStart(1, voices))
    break;
   radio.Started(1, 177.707, 2);
   voices++;
  }
  Check(voices == 4, string.Format("four radios in range play at once, a fifth waits (%1 started)", voices));
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
  CheckRanges();
  CheckEviction();
  CheckRefusals();
  CheckVoices();
  Finish("completed");
 }
}
