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
 protected float m_FailedDistance = -1;
 protected bool m_ApproachRecoveryUsed;
 protected float m_RecordingEnd = -1;
 float NextDue = -1;
 // Common finite-player bounds; each module further restricts its own choices.
 static bool ValidValue(int key, float value)
 {
  if (value != value || value < 0 || value != Math.Floor(value)) return false;
  if (key == 0) return value <= 12 || (value >= 100 && value <= 103);
  if (key == 1) return value <= 100;
  if (key == 2 || key == 3 || key == 6) return value <= 1;
  if (key == 4) return value <= 600;
  return key == 5 && value >= 10 && value <= 150 && Math.Floor(value / 10) * 10 == value;
 }
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
 // Three failed starts park only this module. A substantial approach permits
 // one extra batch; only a real start or explicit restart rearms that allowance.
 int FailedStart(float now, float distance = -1)
 {
  m_StartFailures++;
  m_FailedDistance = distance;
  NextDue = now + 5 * m_StartFailures;
  return m_StartFailures;
 }
 bool RecoverAfterApproach(float distance, float range)
 {
  if (m_StartFailures < 3 || m_ApproachRecoveryUsed) return false;
  // Positive comparisons also reject NaN. Unknown metadata failures use -1.
  if (!(range > 0 && distance >= 0 && distance <= range * 0.8 && m_FailedDistance - distance >= Math.Max(1, range * 0.2))) return false;
  m_StartFailures = 0; m_FailedDistance = -1; m_ApproachRecoveryUsed = true;
  return true; // Keep the last backoff deadline, random history and one-shot state.
 }
 void Started(float now, float duration, int recording = -1)
 {
  m_StartFailures = 0; m_FailedDistance = -1; m_ApproachRecoveryUsed = false; m_Played = true; LastRecording = recording;
  m_RecordingEnd = now + duration; NextDue = m_RecordingEnd + PauseSeconds;
 }
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
 void Restart(float now) { m_StartFailures = 0; m_FailedDistance = -1; m_ApproachRecoveryUsed = false; m_Played = false; m_RecordingEnd = -1; LastRecording = -1; NextDue = now - 0.01; }
}
