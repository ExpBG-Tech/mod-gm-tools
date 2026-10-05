// One bounded state per category; no entities, callbacks or playback handles.
class EAS_CategorySchedule
{
 float Due;
 float Tail;
 int PhraseLeft;
 int BurstLeft;
 int BurstClip = -1;
 float BurstUntil;
 ref array<int> History = {};
 ref array<int> Bag = {};
}

class EAS_Schedule
{
 ref RandomGenerator Random = new RandomGenerator();
 float NextDue;
 float RunStarted;
 float LastExplosion = -100000;
 // Retained pure-test API. Runtime scheduling uses the category states below.
 bool JetExhausted;
 int PhraseLeft;
 int Band = -1;
 vector Position;
 protected ref array<ref EAS_CategorySchedule> m_Categories = {};
 protected ref array<float> m_LastUsed = {};
 protected ref array<float> m_VoiceEnds = {};
 protected ref array<int> m_VoiceCategories = {};
 protected ref array<int> m_VoiceClips = {};
 protected ref array<int> m_Bands = {};
 protected int m_Cursor;

 void EAS_Schedule(int seed, int count)
 {
  Random.SetSeed(seed);
  for (int i = 0; i < count; i++) m_LastUsed.Insert(-100000);
  for (int c = 0; c < 4; c++) m_Categories.Insert(new EAS_CategorySchedule());
 }

 void Restart(float now, bool newRun, bool preserveVoices = false)
 {
  if (newRun) RunStarted = now;
  NextDue = now + 2 + Random.RandFloatXY(0, 3);
  PhraseLeft = 0;
  JetExhausted = false;
  if (!preserveVoices)
  {
   m_VoiceEnds.Clear();
   m_VoiceCategories.Clear();
   m_VoiceClips.Clear();
  }
  else ActiveVoices(now);
  foreach (int category, EAS_CategorySchedule state : m_Categories)
  {
   state.Due = NextDue;
   state.PhraseLeft = 0;
   state.BurstLeft = 0;
   state.BurstClip = -1;
   state.Tail = now;
   if (preserveVoices)
    foreach (int v, float end : m_VoiceEnds)
     if (m_VoiceCategories[v] == category) state.Tail = Math.Max(state.Tail, end);
   state.Bag.Clear();
  }
  // Preserve clip cooldown/history on live edits, sleep and rapid off/on toggles.
 }

 int ActiveVoices(float now, int category = -1)
 {
  int count;
  for (int i = m_VoiceEnds.Count() - 1; i >= 0; i--)
  {
   if (m_VoiceEnds[i] <= now) { m_VoiceEnds.RemoveOrdered(i); m_VoiceCategories.RemoveOrdered(i); m_VoiceClips.RemoveOrdered(i); }
   else if (category < 0 || m_VoiceCategories[i] == category) count++;
  }
  return count;
 }

 protected float CapacityDue(float now, int category, int intensity, bool independent = false)
 {
  int total = ActiveVoices(now);
  int same = ActiveVoices(now, category);
  int war = ActiveVoices(now, 0) + ActiveVoices(now, 1);
  bool categoryFull = (category < 2 && war >= EAS_Logic.OwnerVoiceCap(0, intensity)) || (category >= 2 && same >= 1) || (category == 1 && same >= 2);
  int totalCap = 4;
  if (independent) { totalCap = 8; categoryFull = same >= EAS_Logic.OwnerVoiceCap(category, intensity); }
  if (total < totalCap && !categoryFull) return now;
  float earliest = 1000000000;
  foreach (int i, float end : m_VoiceEnds)
   if (total >= totalCap || (!independent && category < 2 && m_VoiceCategories[i] < 2) || m_VoiceCategories[i] == category) earliest = Math.Min(earliest, end);
  return earliest;
 }

 bool HasCapacity(float now, int intensity)
 {
  float due = CapacityDue(now, 0, intensity);
  if (due <= now) return true;
  NextDue = Math.Max(NextDue, due);
  return false;
 }

 protected bool Matches(EAS_Clip clip, int category, int selection)
 {
  if (clip.Category != category) return false;
  if (category < 2) return selection == 2 || clip.DistanceSet == selection;
  if (category == 2) return selection == 2 || clip.Variant == selection + 1;
  return selection == 0 || clip.Variant == selection;
 }

 protected int Pick(array<ref EAS_Clip> bank, int category, int intensity, float now, int minutes = 0, int range = 1500, int selection = 2, bool second = false)
 {
  EAS_CategorySchedule state = m_Categories[category];
  array<int> eligible = {};
  foreach (int slot, EAS_Clip entry : bank)
   if (Matches(entry, category, selection) && (category != 2 || EAS_Logic.JetFits(RunStarted, minutes, now, entry.Duration, range))) eligible.Insert(slot);
  if (eligible.IsEmpty())
  {
   state.Due = 1000000000;
   if (minutes > 0) state.Due = RunStarted + minutes * 60;
   return -2;
  }
  if (category == 1 && (now - LastExplosion < 1.5 || ActiveVoices(now, 1) >= 2))
  {
   state.Due = Math.Max(now + 0.25, LastExplosion + 1.5);
   return -1;
  }
  for (int b = state.Bag.Count() - 1; b >= 0; b--) if (!eligible.Contains(state.Bag[b])) state.Bag.RemoveOrdered(b);
  int historyCount = EAS_Logic.HistoryLimit(eligible.Count());
  array<int> candidates = {};
  float ready = 1000000000;
  // One refill handles a changed selector or a long recording no longer fitting.
  for (int attempt = 0; attempt < 2; attempt++)
  {
   if (state.Bag.IsEmpty()) state.Bag.Copy(eligible);
   bool unblocked;
   foreach (int index : state.Bag)
   {
    bool recent;
    for (int h = Math.Max(0, state.History.Count() - historyCount); h < state.History.Count(); h++) if (state.History[h] == index) recent = true;
    if (recent) continue;
    unblocked = true;
    float clipReady = m_LastUsed[index] + 60;
    // Long recordings cannot overlap themselves. These at-most-eight reservations
    // clear on interruption; the ordinary sixty-second cooldown still survives.
    foreach (int v, int activeClip : m_VoiceClips)
     if (activeClip == index) clipReady = Math.Max(clipReady, m_VoiceEnds[v]);
    ready = Math.Min(ready, clipReady);
    if (now < clipReady) continue;
    int weight = 1;
    if (category == 0 && bank[index].Activity == Math.Min(intensity, 2)) weight = 3;
    for (int w = 0; w < weight; w++) candidates.Insert(index);
   }
   if (unblocked) break;
   state.Bag.Clear();
  }
  // A singleton selection (Near firefight or pinned jet) needs the same recording
  // at two positions. This exception belongs only to the explicit second source;
  // every new pair still observes the full active-clip and cooldown guards.
  if (candidates.IsEmpty() && second && eligible.Count() == 1 && eligible[0] == state.BurstClip) return state.BurstClip;
  if (candidates.IsEmpty()) { state.Due = Math.Max(now + 0.25, ready); return -1; }
  return candidates[Random.RandInt(0, candidates.Count())];
 }

 protected void UpdateDue(array<int> values)
 {
  NextDue = 1000000000;
  for (int c = 0; c < 4; c++) if (values[8 + c * 2] == 1) NextDue = Math.Min(NextDue, m_Categories[c].Due);
 }

 int ChooseContent(array<ref EAS_Clip> bank, array<int> values, float now)
 {
  if (!values || (values.Count() != 16 && values.Count() != 23) || bank.Count() != m_LastUsed.Count()) return -1;
  // A module may be visited only once per second at full admission. Prioritize
  // its due second source before starting another category's pair. Both scans
  // cover exactly four states; ties and ordinary starts retain round-robin order.
  int cursor = m_Cursor;
  float earliest = now + 0.001;
  for (int pending = 0; pending < 4; pending++)
  {
   int pendingCategory = (m_Cursor + pending) % 4;
   EAS_CategorySchedule pendingState = m_Categories[pendingCategory];
   if (values[8 + pendingCategory * 2] == 1 && pendingState.BurstLeft > 0 && now <= pendingState.BurstUntil && pendingState.Due <= now && pendingState.Due < earliest)
   {
    cursor = pendingCategory;
    earliest = pendingState.Due;
   }
  }
  for (int probe = 0; probe < 4; probe++)
  {
   int category = (cursor + probe) % 4;
   if (values[8 + category * 2] == 0) continue;
   EAS_CategorySchedule state = m_Categories[category];
   if (state.BurstLeft > 0 && now > state.BurstUntil) FinishBurst(category, now);
   if (now < state.Due) continue;
   int frequency = Frequency(values, category);
   state.Due = CapacityDue(now, category, frequency, values.Count() == 23);
   if (state.BurstLeft > 0) state.Due = Math.Min(state.Due, state.BurstUntil + 0.25);
   if (now < state.Due) continue;
   int selection = 2;
   selection = values[9 + category * 2];
   int chosen = Pick(bank, category, frequency, now, values[5], values[3], selection, state.BurstLeft > 0);
   if (chosen >= 0) return chosen;
   if (state.BurstLeft > 0) FinishBurst(category, now);
  }
  UpdateDue(values);
  return -1;
 }

 // A failed ground placement must not starve an eligible airborne category.
 void DeferCategory(int category, array<int> values, float now)
 {
  if (m_Categories[category].BurstLeft > 0) FinishBurst(category, now);
  else m_Categories[category].Due = now + 5;
  m_Cursor = (category + 1) % 4;
  UpdateDue(values);
 }

 void StartedContent(array<ref EAS_Clip> bank, int index, array<int> values, float now)
 {
  Started(bank, index, Frequency(values, bank[index].Category), now, values[3]);
  m_Cursor = (bank[index].Category + 1) % 4;
  UpdateDue(values);
 }

 protected int Frequency(array<int> values, int category)
 {
  if (values.Count() == 23 && category < 3) return values[20 + category];
  return values[1];
 }

 protected void FinishBurst(int category, float now)
 {
  EAS_CategorySchedule state = m_Categories[category];
  state.BurstLeft = 0;
  state.BurstClip = -1;
  float quiet = Random.RandFloatXY(8, 20);
  if (category == 2) quiet = Random.RandFloatXY(15, 30);
  state.Due = Math.Max(now, state.Tail) + quiet;
 }

 // Compatibility entry points for pure selection tests and schema-one fixtures.
 int Choose(array<ref EAS_Clip> bank, int mode, int intensity, float now, int minutes = 0, int range = 1500)
 {
  if (bank.Count() != m_LastUsed.Count()) return -1;
  if (mode == 4)
  {
   int jet = Pick(bank, 2, intensity, now, minutes, range);
   JetExhausted = jet == -2;
   if (JetExhausted) NextDue = m_Categories[2].Due;
   return jet;
  }
  if (mode < 2) return Math.Max(-1, Pick(bank, mode, intensity, now));
  int category;
  if (Random.RandInt(0, 100) < 15) category = 1;
  int chosen = Pick(bank, category, intensity, now);
  if (chosen < 0) chosen = Pick(bank, 1 - category, intensity, now);
  return Math.Max(-1, chosen);
 }

 void Started(array<ref EAS_Clip> bank, int index, int intensity, float now, int range = 1500)
 {
  EAS_Clip clip = bank[index];
  EAS_CategorySchedule state = m_Categories[clip.Category];
  m_LastUsed[index] = now;
  state.Bag.RemoveItem(index);
  state.History.Insert(index);
  while (state.History.Count() > 3) state.History.RemoveOrdered(0);
  if (clip.Category == 1) LastExplosion = now;
  float end = now + EAS_Logic.VoiceLifetime(clip.Duration, range);
  state.Tail = Math.Max(state.Tail, end);
  if (ActiveVoices(now) < 8) { m_VoiceEnds.Insert(end); m_VoiceCategories.Insert(clip.Category); m_VoiceClips.Insert(index); }
  float gap;
  if (clip.Category == 3)
  {
   state.PhraseLeft = 0;
   state.Due = end + 0.5; // One full mix plus propagation tail, then a short transition.
  }
  else if (intensity == 3)
  {
   state.PhraseLeft = 0;
   if (state.BurstLeft > 0) FinishBurst(clip.Category, now);
   else
   {
    state.BurstLeft = 1;
    state.BurstClip = index;
    state.BurstUntil = now + 3;
    state.Due = now + 0.8;
    if (clip.Category == 1) state.Due = now + 1.5;
   }
  }
  else if (clip.Category == 2)
  {
   if (intensity == 0) gap = Random.RandFloatXY(180, 300);
   else if (intensity == 1) gap = Random.RandFloatXY(90, 180);
   else gap = Random.RandFloatXY(45, 90);
   state.PhraseLeft = 0;
   state.Due = now + clip.Duration + gap;
  }
  else
  {
   float quiet;
   if (intensity == 0)
   {
    if (state.PhraseLeft == 0) state.PhraseLeft = 2;
    gap = Random.RandFloatXY(0.4, 0.6);
    quiet = Random.RandFloatXY(60, 150);
   }
   else if (intensity == 1)
   {
    if (state.PhraseLeft == 0) state.PhraseLeft = Random.RandIntInclusive(3, 5);
    gap = Random.RandFloatXY(0.3, 0.5);
    quiet = Random.RandFloatXY(20, 45);
   }
   else
   {
    if (state.PhraseLeft == 0) state.PhraseLeft = Random.RandIntInclusive(6, 10);
    gap = Random.RandFloatXY(0.25, 0.35);
    quiet = Random.RandFloatXY(8, 20);
   }
   state.PhraseLeft--;
   if (clip.Category == 1) gap = Math.Max(gap, 1.5);
   state.Due = EAS_Logic.NextStart(now, state.Tail - now, gap, quiet, state.PhraseLeft == 0);
  }
  PhraseLeft = state.PhraseLeft;
  NextDue = state.Due;
 }

 int NextBand()
 {
  if (m_Bands.IsEmpty()) { m_Bands.Insert(0); m_Bands.Insert(1); m_Bands.Insert(2); }
  int index = Random.RandInt(0, m_Bands.Count());
  if (m_Bands[index] == Band && m_Bands.Count() > 1) index = (index + 1) % m_Bands.Count();
  Band = m_Bands[index];
  m_Bands.Remove(index);
  return Band;
 }

 vector PointInBand(vector centre, float radius, int band)
 {
  if (radius <= 0) return centre;
  return Random.GenerateRandomPointInRadius(radius * band / 3, radius * (band + 1) / 3, centre, true);
 }
}
