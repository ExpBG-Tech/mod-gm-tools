// Pure rules shared by server scheduling, client admission and offline tests.
class EAS_Logic
{
 static bool ValidSettings(int mode, int intensity, int spread, int range, int volume, int minutes)
 {
  return mode >= -1 && mode <= 4 && intensity >= 0 && intensity <= 2
   && spread >= 0 && spread <= 500 && spread % 25 == 0 && range >= 100 && range <= 3000 && range % 50 == 0
   && volume >= 0 && volume <= 100 && minutes >= 0 && minutes <= 120;
 }

 static float VoiceLifetime(float duration, int range) { return duration + range / 343.0 + 1; }
 static bool JetFits(float started, int minutes, float now, float duration, int range)
 {
  return minutes == 0 || now + VoiceLifetime(duration, range) <= started + minutes * 60;
 }
 static float JetHeight(float anchor, float surface, int range) { return Math.Max(anchor, surface) + Math.Min(100, range / 4.0); }
 static int OwnerVoiceCap(int category, int intensity)
 {
  if (category == 3) return 1;
  if (intensity == 3 || category == 1) return 2;
  if (category >= 2) return 1;
  if (intensity == 2) return 3;
  return 2;
 }
 static bool CanStartVoice(int category, int total, int explosions, int jets, bool ownerFull, bool budget, float sinceExplosion)
 {
  return !ownerFull && total < 8 && budget && (category != 2 || jets < 2)
   && (category != 1 || (explosions < 2 && sinceExplosion >= 1.5));
 }

 static float NextStart(float now, float duration, float gap, float quiet, bool phraseEnd)
 {
  if (phraseEnd) return now + duration + Math.Max(gap, quiet);
  return now + gap;
 }

 static int HistoryLimit(int count) { return Math.Max(0, Math.Min(3, count - 1)); }
 static vector AnchorPosition(vector origin) { origin[1] = origin[1] + 1; return origin; }
 // Ranking estimate only: normalized linear outer-range taper, not native rolloff.
 static float EstimatedAudibility(float gain, float distance, int range)
 {
  if (distance < 0 || distance >= range) return 0;
  return gain * Math.Clamp((range - distance) / (range - 50.0), 0, 1);
 }
 static bool Before(float score, float accepted, string identity, float otherScore, float otherAccepted, string otherIdentity)
 {
  if (score != otherScore) return score > otherScore;
  if (accepted != otherAccepted) return accepted < otherAccepted;
  return identity.Compare(otherIdentity) < 0;
 }
 static bool Expired(float started, int minutes, float now) { return minutes > 0 && now >= started + minutes * 60; }
 static bool AcceptEvent(int revision, int eventRevision, int sequence, int lastSequence, float emitted, float now)
 {
  return revision == eventRevision && sequence > lastSequence && now >= emitted - 0.25 && now - emitted <= 0.75;
 }
}

// A service tick drains this entire current cohort. Nothing waits for later capacity.
class EAS_Pending
{
 EAS_AmbientModule Owner;
 string StableKey;
 int ConfigRevision;
 int EventSequence;
 int Clip;
 WorldTimestamp Emitted;
 vector Position;
 float Gain;
 int Range;
 float Score;
 float LastAccepted;
}

class EAS_PlaybackCohort
{
 protected ref array<ref EAS_Pending> m_Items = {};
 bool Add(EAS_Pending candidate)
 {
  if (m_Items.Count() >= 32) return false;
  foreach (EAS_Pending item : m_Items) if (item.StableKey == candidate.StableKey && item.EventSequence == candidate.EventSequence) return false;
  m_Items.Insert(candidate);
  return true;
 }
 void Cancel(string identity)
 {
  for (int i = m_Items.Count() - 1; i >= 0; i--) if (m_Items[i].StableKey == identity) m_Items.Remove(i);
 }
 bool Empty() { return m_Items.IsEmpty(); }
 void Clear() { m_Items.Clear(); }
 array<ref EAS_Pending> Items() { return m_Items; }
 EAS_Pending TakeBest()
 {
  if (m_Items.IsEmpty()) return null;
  int best;
  // ponytail: bounded O(n^2) drain, at most 32; use native sorting if cap grows.
  for (int i = 1; i < m_Items.Count(); i++)
  {
   EAS_Pending item = m_Items[i];
   EAS_Pending top = m_Items[best];
   if (EAS_Logic.Before(item.Score, item.LastAccepted, item.StableKey, top.Score, top.LastAccepted, top.StableKey)) best = i;
  }
  EAS_Pending selected = m_Items[best];
  m_Items.Remove(best);
  return selected;
 }
}

class EAS_StartBudget
{
 protected ref array<float> m_Starts = {};

 bool Available(float now, int maximum)
 {
  while (!m_Starts.IsEmpty() && m_Starts[0] <= now - 10) m_Starts.RemoveOrdered(0);
  return m_Starts.Count() < maximum;
 }

 void Record(float now) { m_Starts.Insert(now); }
}

class EAS_Clip
{
 string EventName;
 float Duration;
 int Category; // 0 firearm, 1 explosion, 2 jet, 3 premixed
 int Activity; // 0 report, 1 short burst, 2 dense burst
 int DistanceSet; // 0 Distant, 1 Near; source perspective, not emitter position
 int Variant; // stable jet 1..2 or premixed recording 1..4
 float PlaybackGain; // native event attenuation; source PCM remains unchanged

 void EAS_Clip(string eventName, float duration, int category, int activity, int distanceSet = 0, int variant = 0, float playbackGain = 1)
 {
  EventName = eventName;
  Duration = duration;
  Category = category;
  Activity = activity;
  DistanceSet = distanceSet;
  Variant = variant;
  PlaybackGain = playbackGain;
 }
}
