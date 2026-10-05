// Native numeric reads, with byte-exact comparison of everything outside the
// two direct spawnData transform arrays. Never round a snapshot or component.
class EBG_NativePoseParity
{
 protected static bool Space(string c) { return c == " " || c == "\t" || c == "\r" || c == "\n"; }
 // Locate a direct object member only. Inputs are first validated by native JSON.
 // Quotes/escapes and container depth prevent matching nested or string content.
 protected static bool Member(string json, string key, out int start, out int end)
 {
  return MemberSpan(json, key, start, end) == 1;
 }
 // 0 means absent, -1 means malformed/duplicate, 1 means one valid container.
 // Native spawn data omits implicitly known transforms; absence is not zero.
 protected static int MemberSpan(string json, string key, out int start, out int end)
 {
  int depth, matches;
  bool quoted, escaped;
  int quoteStart;
  for (int i = 0; i < json.Length(); i++)
  {
   string c = json.Substring(i, 1);
   if (quoted)
   {
    if (escaped) { escaped = false; continue; }
    if (c == "\\") { escaped = true; continue; }
    if (c != "\"") continue;
    quoted = false;
    if (depth != 1 || json.Substring(quoteStart + 1, i - quoteStart - 1) != key) continue;
    int colon = i + 1;
    while (colon < json.Length() && Space(json.Substring(colon, 1))) colon++;
    if (colon >= json.Length() || json.Substring(colon, 1) != ":") continue;
    int value = colon + 1;
    while (value < json.Length() && Space(json.Substring(value, 1))) value++;
    if (value >= json.Length()) return -1;
    // Only object/array members are admitted by this helper.
    string opening = json.Substring(value, 1);
    if (opening != "{" && opening != "[") return -1;
    int nesting = 0;
    bool inString, escape;
    int finish = -1;
    for (int j = value; j < json.Length(); j++)
    {
     string token = json.Substring(j, 1);
     if (inString)
     {
      if (escape) escape = false;
      else if (token == "\\") escape = true;
      else if (token == "\"") inString = false;
      continue;
     }
     if (token == "\"") { inString = true; continue; }
     if (token == "{" || token == "[") nesting++;
     else if (token == "}" || token == "]")
     {
      nesting--;
      if (nesting == 0) { finish = j + 1; break; }
     }
    }
    if (finish < 0) return -1;
    start = value; end = finish; matches++;
   }
   else if (c == "\"") { quoted = true; quoteStart = i; }
   else if (c == "{" || c == "[") depth++;
   else if (c == "}" || c == "]") depth--;
  }
  if (matches > 1) return -1;
  return matches;
 }
 protected static bool Pose(string json, out string rest, out vector coords, out vector angles, out int presence)
 {
  SCR_PersistenceJsonLoadContext context = new SCR_PersistenceJsonLoadContext();
  if (!context.LoadFromString(json) || !context.StartObject("spawnData")) return false;
  int spawnStart, spawnEnd;
  if (!Member(json, "spawnData", spawnStart, spawnEnd)) return false;
  string spawn = json.Substring(spawnStart, spawnEnd - spawnStart);
  int coordStart, coordEnd, angleStart, angleEnd;
  int hasCoords = MemberSpan(spawn, "coords", coordStart, coordEnd);
  int hasAngles = MemberSpan(spawn, "angles", angleStart, angleEnd);
  if (hasCoords < 0 || hasAngles < 0) return false;
  presence = hasCoords + 2 * hasAngles;
  array<float> position = {}, rotation = {};
  if (hasCoords)
  {
   if (!context.ReadValue("coords", position) || position.Count() != 3) return false;
   coords = Vector(position[0], position[1], position[2]);
  }
  if (hasAngles)
  {
   if (!context.ReadValue("angles", rotation) || rotation.Count() != 3) return false;
   angles = Vector(rotation[0], rotation[1], rotation[2]);
  }
  if (!context.EndObject() || !context.IsValid()) return false;
  // Replace only these two already-validated array spans in a comparison copy.
  // Every byte of every other field, including nested transforms, stays exact.
  if (hasCoords && hasAngles && coordStart > angleStart)
  {
   spawn = spawn.Substring(0, coordStart) + "[]" + spawn.Substring(coordEnd, spawn.Length() - coordEnd);
   spawn = spawn.Substring(0, angleStart) + "[]" + spawn.Substring(angleEnd, spawn.Length() - angleEnd);
  }
  else if (hasCoords && hasAngles)
  {
   spawn = spawn.Substring(0, angleStart) + "[]" + spawn.Substring(angleEnd, spawn.Length() - angleEnd);
   spawn = spawn.Substring(0, coordStart) + "[]" + spawn.Substring(coordEnd, spawn.Length() - coordEnd);
  }
  else if (hasCoords) spawn = spawn.Substring(0, coordStart) + "[]" + spawn.Substring(coordEnd, spawn.Length() - coordEnd);
  else if (hasAngles) spawn = spawn.Substring(0, angleStart) + "[]" + spawn.Substring(angleEnd, spawn.Length() - angleEnd);
  rest = json.Substring(0, spawnStart) + spawn + json.Substring(spawnEnd, json.Length() - spawnEnd);
  return true;
 }
 static bool Matches(string before, string actual)
 {
  if (before.IsEmpty() || actual.IsEmpty() || before.Length() > 1048576 || actual.Length() > 1048576) return false;
  if (before == actual) return true;
  string beforeRest, actualRest;
  vector beforeCoords, actualCoords, beforeAngles, actualAngles;
  int beforePresence, actualPresence;
  if (!Pose(before, beforeRest, beforeCoords, beforeAngles, beforePresence) || !Pose(actual, actualRest, actualCoords, actualAngles, actualPresence) || beforePresence != actualPresence) return false;
  // Negated <= also rejects non-finite values if a native parser admitted them.
  if (!(vector.Distance(beforeCoords, actualCoords) <= 0.001) || !(vector.Distance(beforeAngles, actualAngles) <= 0.001)) return false;
  return beforeRest == actualRest;
 }
}
