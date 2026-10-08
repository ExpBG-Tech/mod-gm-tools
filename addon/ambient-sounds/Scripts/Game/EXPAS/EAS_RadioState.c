// Pure configuration and loop timing shared by native fixtures and playback.
class EAS_RadioState
{
 int Recording;
 int Volume = 35;
 int Enabled = 1;
 int Loop = 1;
 int PauseSeconds = 1;
 int Range = 30;
 int DebugEnabled;
 int LastRecording = -1;
 protected bool m_Played;
 protected int m_StartFailures;
 protected int m_Refusals;
 protected int m_InaudibleStreak;
 protected int m_Notices;
 protected float m_StartedAt = -1;
 protected float m_EvictionRetry;
 protected float m_RecordingEnd = -1;
 float NextDue = -1;
 // Common finite-player bounds; each module further restricts its own choices.
 static bool ValidValue(int key, float value)
 {
  if (value != value || value < 0 || value != Math.Floor(value)) return false;
  // Permanent recording IDs 0-99 and random groups 100-103.
  if (key == 0) return value <= 103;
  if (key == 1) return value <= 100;
  if (key == 2 || key == 3 || key == 6) return value <= 1;
  if (key == 4) return value <= 600;
  // Radios, crowds and TVs keep their 150 m limits; placed sounds reach 1500 m.
  return key == 5 && value >= 10 && value <= 1500 && Math.Floor(value / 10) * 10 == value;
 }
 // Starts keep a 10% margin (at least 1 m) inside the audible range: the logarithmic curve
 // is near silent at the edge, where the engine refuses a start as inaudible (27 m of a
 // radio's 30 m). A playing voice keeps the full range, so the margin is also hysteresis.
 static float StartRange(float range) { return range - Math.Max(range * 0.1, 1); }
 // The engine reported a voice finished at least 2 s before its recording ends.
 static bool EndedEarly(float now, float recordingEnd) { return now < recordingEnd - 2; }
 bool Apply(array<int> values)
 {
  if (!values || (values.Count() != 3 && values.Count() != 4 && values.Count() != 6 && values.Count() != 7)) return false;
  for (int i = 0; i < values.Count(); i++) if (!ValidValue(i, values[i])) return false;
  Recording = values[0]; Volume = values[1]; Enabled = values[2];
  Loop = 1; PauseSeconds = 1; Range = 30; DebugEnabled = 0;
  if (values.Count() >= 4) Loop = values[3];
  if (values.Count() >= 6) { PauseSeconds = values[4]; Range = values[5]; }
  if (values.Count() == 7) DebugEnabled = values[6];
  return true;
 }
 bool CanStart(float now, int voices) { return Enabled == 1 && Volume > 0 && m_StartFailures < 3 && (Loop == 1 || !m_Played) && now > NextDue && voices < 4; }
 // Invalid recording metadata: three tries park only this module until its settings change.
 int FailedStart(float now)
 {
  m_StartFailures++;
  NextDue = now + 5 * m_StartFailures;
  return m_StartFailures;
 }
 // The engine refused a start the listener should hear (playing-source limit, paused
 // audio): retry after 5, 10, 15 ... s, at most 30 s, and never park. A start resets it.
 int Refused(float now)
 {
  m_Refusals++;
  NextDue = now + Math.Min(5 * m_Refusals, 30);
  return m_Refusals;
 }
 // A refusal below the threshold of audibility (range edge, or masked by louder sounds) is
 // not a failure: try again in 3 s. Returns the streak length since the last start.
 int Inaudible(float now)
 {
  m_InaudibleStreak++;
  NextDue = now + 3;
  return m_InaudibleStreak;
 }
 void Started(float now, float duration, int recording = -1)
 {
  m_StartFailures = 0; m_Refusals = 0; m_InaudibleStreak = 0; m_StartedAt = now; m_Played = true; LastRecording = recording;
  m_RecordingEnd = now + duration; NextDue = m_RecordingEnd + PauseSeconds;
 }
 // Normal-log budget for refused starts: true for the first and third notice, then never
 // again until the settings change (Restart). Starts do not refill it, so a source cycling
 // between refusal, start and eviction in a busy scene logs at most two lines per placement.
 bool Notice()
 {
  if (m_Notices >= 3) return false;
  m_Notices++;
  return m_Notices != 2;
 }
 // EndedEarly on the real clock: heard is the real time since the start. World time can fall
 // behind the audio after a client hitch, so an eviction needs both clocks to agree.
 bool EndedEarlyHeard(float heard) { return m_StartedAt >= 0 && m_RecordingEnd >= 0 && EndedEarly(m_StartedAt + heard, m_RecordingEnd); }
 // The engine ended the recording early while its listener was in range (playing-source
 // limit, or below audibility under louder sounds). Retry soon instead of waiting out the
 // recording: 3 s, doubling to 30 s while evictions follow each other, and never later than
 // the recording's own end plus its pause. A completed recording or a voice that lasted 60 s
 // starts a new streak. A one-shot stays used, as when its listener leaves. Returns the delay.
 float Evicted(float now)
 {
  if (m_StartedAt < 0 || now - m_StartedAt >= 60) m_EvictionRetry = 0;
  m_EvictionRetry = Math.Clamp(m_EvictionRetry * 2, 3, 30);
  NextDue = now + m_EvictionRetry;
  if (m_RecordingEnd >= 0) NextDue = Math.Min(NextDue, m_RecordingEnd + PauseSeconds);
  m_RecordingEnd = -1;
  return NextDue - now;
 }
 // The recording played to its end: the next eviction starts a new streak.
 void Completed() { m_EvictionRetry = 0; }
 // Leaving audible range discards only an unfinished recording's deadline.
 // A completed recording still owns its configured pause; a one-shot remains used.
 void Interrupted(float now)
 {
  if (m_StartFailures == 0 && m_Played && now < m_RecordingEnd) NextDue = now;
  m_RecordingEnd = -1;
 }
 // Waking adds a small stagger without skipping a completed loop pause or retry backoff.
 void Resume(float now, float stagger)
 {
  NextDue = Math.Max(NextDue, now + stagger);
 }
 void Restart(float now) { m_StartFailures = 0; m_Refusals = 0; m_InaudibleStreak = 0; m_Notices = 0; m_StartedAt = -1; m_EvictionRetry = 0; m_Played = false; m_RecordingEnd = -1; LastRecording = -1; NextDue = now - 0.01; }
}
