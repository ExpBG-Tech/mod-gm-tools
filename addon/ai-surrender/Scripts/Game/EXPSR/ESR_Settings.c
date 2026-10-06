// EXPBG AI Surrender: one global configuration on the server. Every placed module is a
// handle to it, so editing any copy edits all of them. Values are whole numbers; the
// two switches are stored as 0/1.
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
 static const int COUNT = 10;
 // Read-only attribute key; never stored or saved.
 static const int PRISONERS = 200;

 protected static ref array<int> s_aValues;

 static bool IsBoolean(int key) { return key == ENABLED || key == DIAGNOSTICS; }
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
  return true;
 }

 static void Copy(notnull array<int> outValues)
 {
  outValues.Clear();
  for (int key = 0; key < COUNT; key++) outValues.Insert(Get(key));
 }
}
