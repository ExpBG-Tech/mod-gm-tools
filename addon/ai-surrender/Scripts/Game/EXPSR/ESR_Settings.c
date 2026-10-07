// EXPBG AI Surrender: one global configuration on the server. Every placed module is a
// handle to it, so editing any copy edits all of them. Values are whole numbers; the
// three switches are stored as 0/1.
class ESR_Settings
{
 static const int ENABLED = 0;
 static const int CHANCE = 1;
 static const int THRESHOLD = 2;
 static const int RANDOM = 3;
 static const int REVEAL = 4;
 static const int IDENTITY = 5;
 static const int RADIUS = 6;
 static const int ATTEMPTS = 7;
 static const int LIFETIME = 8;
 static const int DIAGNOSTICS = 9;
 // Commander: grenade instead of surrender (%), and whether he must carry one.
 static const int GRENADE = 10;
 static const int GRENADE_CARRY = 11;
 // Interrogation: the prisoner also points out unclaimed intel items (%). Appended so
 // every earlier key keeps its index; it searches within RADIUS.
 static const int INTEL = 12;
 static const int COUNT = 13;
 // Saves before the commander settings (esrVersion 1) hold the first ten values.
 static const int COUNT_V1 = 10;
 // Saves before the intel setting (esrVersion 2) hold the first twelve.
 static const int COUNT_V2 = 12;
 // Read-only attribute key; never stored or saved.
 static const int PRISONERS = 200;

 protected static ref array<int> s_aValues;
 // Bumped whenever a stored value changes; per-squad caches of effective values
 // (ESR_Overrides) compare against it.
 protected static int s_iRevision;

 static bool IsBoolean(int key) { return key == ENABLED || key == DIAGNOSTICS || key == GRENADE_CARRY; }
 static int FromBool(bool value)
 {
  if (value) return 1;
  return 0;
 }

 static int Minimum(int key)
 {
  if (key == THRESHOLD) return 10;
  if (key == RADIUS) return 100;
  if (key == ATTEMPTS) return 1;
  return 0;
 }

 static int Maximum(int key)
 {
  if (IsBoolean(key)) return 1;
  if (key == RANDOM) return 50;
  if (key == RADIUS) return 3000;
  if (key == ATTEMPTS) return 5;
  if (key == LIFETIME) return 120;
  return 100;
 }

 static int Default(int key)
 {
  if (key == ENABLED) return 1;
  if (key == CHANCE) return 30;
  if (key == THRESHOLD) return 50;
  if (key == RANDOM) return 15;
  if (key == REVEAL) return 40;
  if (key == IDENTITY) return 40;
  if (key == RADIUS) return 1000;
  if (key == ATTEMPTS) return 3;
  // Off unless the Game Master opts in; a leader then needs his own grenade.
  if (key == GRENADE) return 0;
  if (key == GRENADE_CARRY) return 1;
  if (key == INTEL) return 30;
  return 0;
 }

 static int Normalize(int key, float value)
 {
  if (key < 0 || key >= COUNT) return 0;
  // NaN from a damaged save falls back to the default.
  if (value != value) return Default(key);
  int rounded = Math.Round(value);
  return Math.ClampInt(rounded, Minimum(key), Maximum(key));
 }

 static bool IsSet() { return s_aValues != null; }

 static int Revision()
 {
  return s_iRevision;
 }

 static int Get(int key)
 {
  if (key < 0 || key >= COUNT) return 0;
  if (!s_aValues) return Default(key);
  return s_aValues[key];
 }

 // Server only. Adopts a complete set (first module of a session, or a loaded save).
 static void Adopt(notnull array<int> values)
 {
  s_aValues = {};
  for (int key = 0; key < COUNT; key++)
  {
   float value = Default(key);
   if (values.IsIndexValid(key)) value = values[key];
   s_aValues.Insert(Normalize(key, value));
  }
  s_iRevision++;
 }

 // Server only. Returns true when the stored value changed.
 static bool Set(int key, float value)
 {
  if (key < 0 || key >= COUNT) return false;
  if (!s_aValues)
  {
   array<int> defaults = {};
   Adopt(defaults);
  }
  int normalized = Normalize(key, value);
  if (s_aValues[key] == normalized) return false;
  s_aValues[key] = normalized;
  s_iRevision++;
  return true;
 }

 static void Copy(notnull array<int> outValues)
 {
  outValues.Clear();
  for (int key = 0; key < COUNT; key++) outValues.Insert(Get(key));
 }
}
