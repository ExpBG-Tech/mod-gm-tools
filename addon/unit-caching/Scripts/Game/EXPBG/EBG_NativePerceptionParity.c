// Native v1 perception serialization stores ages, which advance after restore.
// Only these five direct numeric values may differ by a measured callback clock.
class EBG_NativePerceptionParity
{
 protected static const string COMPONENT = "PerceptionComponent:54A2CBC75F06064B";
 protected static bool Space(string c) { return c == " " || c == "\t" || c == "\r" || c == "\n"; }
 protected static bool Finite(float value) { return value == value && value - value == 0; }
 // Native JSON validates grammar. This bounded scanner only records direct
 // member/value byte spans; escaped member names and duplicates fail closed.
 protected static int ValueEnd(string json, int start)
 {
  int depth; bool quoted, escaped;
  for (int i = start; i < json.Length(); i++)
  {
   string c = json.Substring(i, 1);
   if (quoted)
   {
    if (escaped) escaped = false;
    else if (c == "\\") escaped = true;
    else if (c == "\"") { quoted = false; if (depth == 0) return i + 1; }
    continue;
   }
   if (c == "\"") quoted = true;
   else if (c == "{" || c == "[") { depth++; if (depth > 64) return -1; }
   else if (c == "}" || c == "]")
   {
    if (depth == 0) { return i; }
    depth--;
    if (depth == 0) { return i + 1; }
   }
   else if (depth == 0 && (c == "," || Space(c))) return i;
  }
  return -1;
 }
 protected static bool Members(string json, out array<string> keys, out array<int> starts, out array<int> ends)
 {
  keys = {}; starts = {}; ends = {};
  int i; while (i < json.Length() && Space(json.Substring(i, 1))) i++;
  if (i >= json.Length() || json.Substring(i, 1) != "{") return false;
  i++;
  while (i < json.Length())
  {
   while (i < json.Length() && Space(json.Substring(i, 1))) i++;
   if (i >= json.Length()) return false;
   if (json.Substring(i, 1) == "}") return true;
   if (json.Substring(i, 1) != "\"") return false;
   i++;
   int keyStart = i;
   int keyEnd = -1;
   for (int keyIndex = keyStart; keyIndex < json.Length(); keyIndex++)
   {
    string keyCharacter = json.Substring(keyIndex, 1);
    if (keyCharacter == "\\") return false;
    if (keyCharacter == "\"") { keyEnd = keyIndex; break; }
   }
   if (keyEnd < 0) return false;
   i = keyEnd;
   string key = json.Substring(keyStart, i - keyStart);
   if (keys.Contains(key) || keys.Count() >= 256) return false;
   i++;
   while (i < json.Length() && Space(json.Substring(i, 1))) i++;
   if (i >= json.Length() || json.Substring(i, 1) != ":") return false;
   i++;
   while (i < json.Length() && Space(json.Substring(i, 1))) i++;
   int end = ValueEnd(json, i);
   if (end <= i) return false;
   keys.Insert(key); starts.Insert(i); ends.Insert(end); i = end;
   while (i < json.Length() && Space(json.Substring(i, 1))) i++;
   if (i >= json.Length()) return false;
   if (json.Substring(i, 1) == "}") return true;
   if (json.Substring(i, 1) != ",") return false;
   i++;
  }
  return false;
 }
 // Bound native float conversion first: ReadValue asserts on overflowing JSON.
 // These limits still cover ages far beyond any supported server uptime.
 protected static bool SafeAgeToken(string token)
 {
  if (token.IsEmpty() || token.Length() > 20 || token.Substring(0, 1) == "-") return false;
  int exponentAt = token.IndexOf("e");
  if (exponentAt < 0) exponentAt = token.IndexOf("E");
  if (exponentAt < 0) return true;
  string exponent = token.Substring(exponentAt + 1, token.Length() - exponentAt - 1);
  if (exponent.IsEmpty() || exponent.Length() > 3) return false;
  int power = exponent.ToInt();
  return power >= -12 && power <= 12;
 }
 protected static bool AgeKey(string key)
 {
  return key == "timeLastDetected" || key == "timeLastSeen" || key == "timeTypeRecognized" || key == "timeFactionRecognized" || key == "timeEndangered";
 }
 protected static bool TargetKey(string key)
 {
  return AgeKey(key) || key == "entityId" || key == "category" || key == "targetDirection" || key == "lastDetectedPosition" || key == "lastSeenPosition" || key == "exposure" || key == "cosTargetDir" || key == "detectAcc" || key == "identifyAcc" || key == "traceFraction";
 }
 // Return the original component and a comparison copy with only age scalars
 // replaced. All surrounding bytes, targets, ordering and unknown outer data stay.
 protected static bool Inspect(string json, out string component, out string masked, out array<float> ages)
 {
  component = ""; masked = json; ages = {};
  if (json.IsEmpty() || json.Length() > 1048576) return false;
  SCR_PersistenceJsonLoadContext context = new SCR_PersistenceJsonLoadContext();
  if (!context.LoadFromString(json)) return false;
  array<string> keys; array<int> starts, ends;
  if (!Members(json, keys, starts, ends)) return false;
  int index = keys.Find("components");
  if (index < 0) return true;
  int componentsStart = starts[index];
  string components = json.Substring(componentsStart, ends[index] - componentsStart);
  if (!Members(components, keys, starts, ends)) return false;
  foreach (string componentKey : keys) if (componentKey.IndexOf("PerceptionComponent:") == 0 && componentKey != COMPONENT) return false;
  index = keys.Find(COMPONENT);
  if (index < 0) return true;
  int componentStart = componentsStart + starts[index];
  component = components.Substring(starts[index], ends[index] - starts[index]);
  if (!Members(component, keys, starts, ends) || keys.Count() != 2 || keys.Find("version") < 0 || keys.Find("targets") < 0) return false;
  index = keys.Find("version");
  if (component.Substring(starts[index], ends[index] - starts[index]) != "1") return false;
  index = keys.Find("targets");
  int targetsStart = starts[index];
  string targets = component.Substring(targetsStart, ends[index] - targetsStart);
  if (targets.Substring(0, 1) != "[") return false;
  if (!context.StartObject("components") || !context.StartObject(COMPONENT)) return false;
  int version, count;
  if (!context.ReadValue("version", version) || version != 1 || !context.StartArray("targets", count) || count < 0 || count > 128) return false;
  array<int> ageStarts = {}, ageEnds = {};
  array<string> targetIds = {};
  int cursor = 1;
  for (int targetIndex = 0; targetIndex < count; targetIndex++)
  {
   while (cursor < targets.Length() && Space(targets.Substring(cursor, 1))) cursor++;
   if (cursor >= targets.Length() || targets.Substring(cursor, 1) != "{") return false;
   int targetEnd = ValueEnd(targets, cursor);
   if (targetEnd <= cursor) return false;
   string target = targets.Substring(cursor, targetEnd - cursor);
   if (!Members(target, keys, starts, ends) || !context.StartObject()) return false;
   string id; int category;
   if (!context.ReadValue("entityId", id) || id.IsEmpty() || targetIds.Contains(id) || !context.ReadValue("category", category)) return false;
   targetIds.Insert(id);
   int ageCount = 0;
   for (int keyIndex = 0; keyIndex < keys.Count(); keyIndex++)
   {
    string key = keys[keyIndex];
    if (!TargetKey(key)) return false;
    if (!AgeKey(key)) continue;
    // Reject strings, containers, null and booleans even if ReadValue coerces.
    string first = target.Substring(starts[keyIndex], 1);
    if ("0123456789-".IndexOf(first) < 0) return false;
    if (!SafeAgeToken(target.Substring(starts[keyIndex], ends[keyIndex] - starts[keyIndex]))) return false;
    float age;
    if (!context.ReadValue(key, age) || !Finite(age) || !(age >= 0)) return false;
    ages.Insert(age); ageCount++;
    ageStarts.Insert(componentStart + targetsStart + cursor + starts[keyIndex]);
    ageEnds.Insert(componentStart + targetsStart + cursor + ends[keyIndex]);
   }
   if (ageCount != 5 || !context.EndObject()) return false;
   cursor = targetEnd;
   while (cursor < targets.Length() && Space(targets.Substring(cursor, 1))) cursor++;
   if (targetIndex < count - 1)
   {
    if (cursor >= targets.Length() || targets.Substring(cursor, 1) != ",") { return false; }
    cursor++;
   }
  }
  while (cursor < targets.Length() && Space(targets.Substring(cursor, 1))) cursor++;
  if (cursor != targets.Length() - 1 || targets.Substring(cursor, 1) != "]") return false;
  if (!context.EndArray() || !context.EndObject() || !context.EndObject() || !context.IsValid()) return false;
  for (int replaceIndex = ageStarts.Count() - 1; replaceIndex >= 0; replaceIndex--)
   masked = masked.Substring(0, ageStarts[replaceIndex]) + "0" + masked.Substring(ageEnds[replaceIndex], masked.Length() - ageEnds[replaceIndex]);
  return true;
 }
 static bool Preflight(string json)
 {
  string component, masked; array<float> ages;
  return Inspect(json, component, masked, ages);
 }
 static bool CallbackExact(string before, string actual)
 {
  string beforeComponent, actualComponent, masked; array<float> ages;
  if (!Inspect(before, beforeComponent, masked, ages) || beforeComponent.IsEmpty() || ages.IsEmpty()) return false;
  if (!Inspect(actual, actualComponent, masked, ages)) return false;
  return beforeComponent == actualComponent;
 }
 static bool Matches(string before, string actual, float elapsed)
 {
  if (!(elapsed >= 0 && elapsed <= 30)) return false;
  string beforeComponent, actualComponent, beforeMasked, actualMasked;
  array<float> beforeAges, actualAges;
  if (!Inspect(before, beforeComponent, beforeMasked, beforeAges) || !Inspect(actual, actualComponent, actualMasked, actualAges)) return false;
  if (beforeComponent.IsEmpty() || actualComponent.IsEmpty() || beforeAges.IsEmpty() || beforeAges.Count() != actualAges.Count()) return false;
  for (int i = 0; i < beforeAges.Count(); i++)
  {
   // Add in the native float domain first: subtracting two large ages before
   // elapsed introduces avoidable cancellation at long manager uptimes.
   float expected = beforeAges[i] + elapsed;
   if (!Finite(expected) || !(Math.AbsFloat(actualAges[i] - expected) <= 0.001)) return false;
  }
  return EBG_NativePoseParity.Matches(beforeMasked, actualMasked);
 }
}

// One restored entity, one first exact callback clock. Repeated deliveries may
// never move this anchor; later state still has to pass measured-age parity.
class EBG_NativePerceptionAnchor
{
 protected IEntity m_Entity;
 protected float m_Clock, m_WorldTime;
 protected bool m_Armed, m_Observed;
 bool IsArmed() { return m_Armed; }
 bool AcceptsEntity(IEntity entity) { return !m_Observed || entity == m_Entity; }
 float GetClock() { return m_Clock; }
 float GetWorldTime() { return m_WorldTime; }
 bool Observe(string before, string actual, IEntity entity, float clock, float worldTime)
 {
  if (m_Observed) return entity == m_Entity;
  m_Observed = true; m_Entity = entity;
  if (!entity || !(clock >= 0) || clock - clock != 0 || !(worldTime >= 0) || worldTime - worldTime != 0) return true;
  if (!EBG_NativePerceptionParity.CallbackExact(before, actual)) return true;
  m_Clock = clock; m_WorldTime = worldTime; m_Armed = true;
  return true;
 }
 bool Matches(string before, string actual, IEntity entity, float clock, float worldTime)
 {
  if (!m_Armed || !entity || entity != m_Entity) return false;
  float worldElapsed = worldTime - m_WorldTime;
  if (!(worldElapsed >= 0 && worldElapsed <= 30000)) return false;
  return EBG_NativePerceptionParity.Matches(before, actual, clock - m_Clock);
 }
 bool MatchesSnapshot(string before, string actual, IEntity entity, float clock, float worldTime, bool clockPhase)
 {
  if (clockPhase && m_Armed) return Matches(before, actual, entity, clock, worldTime);
  return EBG_NativePoseParity.Matches(before, actual);
 }
}
