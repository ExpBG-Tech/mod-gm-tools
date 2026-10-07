// EXPBG Time and Weather: setting keys, defaults, clamps and choice tables of the
// Weather Transition and Time Skip modules. Choice settings store the spinbox index
// (the editor spinbox carries the index); ChoiceValue turns it into the engine value,
// -1 meaning "leave this to the weather".
class ETW_Settings
{
 // Weather Transition, per module. The target weather is kept by state name.
 static const int W_TARGET = 0;
 static const int W_MINUTES = 1;
 static const int W_RAIN = 2;
 static const int W_FOG = 3;
 static const int W_WIND_SPEED = 4;
 static const int W_WIND_DIR = 5;
 static const int W_AFTER = 6;
 static const int W_SMOOTH = 7;
 static const int W_COUNT = 8;
 static const int W_ACTION = 100;
 static const int W_STATUS = 200;

 static const int AFTER_HOLD = 0;
 static const int AFTER_AUTOMATIC = 1;

 static const int ACTION_NONE = 0;
 static const int ACTION_START = 1;
 static const int ACTION_STOP = 2;
 static const int ACTION_AUTOMATIC = 3;

 static const int MINUTES_MIN = 1;
 static const int MINUTES_MAX = 120;
 static const int MINUTES_DEFAULT = 10;

 // Time Skip, per module. The text is kept separately (T_TEXT is its attribute key).
 static const int T_HOURS = 0;
 static const int T_MINUTES = 1;
 static const int T_FADE_OUT = 2;
 static const int T_HOLD = 3;
 static const int T_FADE_IN = 4;
 static const int T_SHOW_TIME = 5;
 static const int T_INCLUDE_GM = 6;
 static const int T_COUNT = 7;
 static const int T_TEXT = 50;
 static const int T_ACTION = 100;
 static const int T_STATUS = 200;

 static const int SKIP_NONE = 0;
 static const int SKIP_NOW = 1;

 static const int HOURS_MAX = 48;
 static const int HOURS_DEFAULT = 6;
 static const float FADE_OUT_DEFAULT = 2;
 static const float HOLD_DEFAULT = 3;
 static const float FADE_IN_DEFAULT = 2;
 static const string TEXT_DEFAULT = "{hours} hours later";
 static const int TEXT_LIMIT = 96;

 //------------------------------------------------------------------------------------------------
 static int WeatherDefault(int key)
 {
  if (key == W_MINUTES)
   return MINUTES_DEFAULT;
  if (key == W_SMOOTH)
   return 1;
  return 0;
 }

 //------------------------------------------------------------------------------------------------
 // Number of spinbox entries of a choice setting, 0 for other keys.
 static int ChoiceCount(int key)
 {
  if (key == W_RAIN || key == W_FOG)
   return 7;
  if (key == W_WIND_SPEED || key == W_WIND_DIR)
   return 9;
  if (key == W_AFTER)
   return 2;
  return 0;
 }

 //------------------------------------------------------------------------------------------------
 static int ClampWeather(int key, float value)
 {
  int rounded = Math.Round(value);
  if (key == W_MINUTES)
   return Math.ClampInt(rounded, MINUTES_MIN, MINUTES_MAX);
  if (key == W_SMOOTH)
  {
   if (rounded != 0)
    return 1;
   return 0;
  }
  int count = ChoiceCount(key);
  if (count > 0)
   return Math.ClampInt(rounded, 0, count - 1);
  return rounded;
 }

 //------------------------------------------------------------------------------------------------
 // Engine value of a choice: rain and fog 0-1, wind speed m/s, wind direction degrees.
 static float ChoiceValue(int key, int choice)
 {
  if (choice <= 0)
   return -1;
  if (key == W_RAIN)
  {
   array<float> rain = {-1, 0, 0.1, 0.25, 0.5, 0.75, 1};
   return Pick(rain, choice);
  }
  if (key == W_FOG)
  {
   array<float> fog = {-1, 0, 0.05, 0.15, 0.3, 0.5, 0.8};
   return Pick(fog, choice);
  }
  if (key == W_WIND_SPEED)
  {
   array<float> speed = {-1, 0, 2, 4, 6, 8, 10, 14, 20};
   return Pick(speed, choice);
  }
  if (key == W_WIND_DIR)
  {
   array<float> direction = {-1, 0, 45, 90, 135, 180, 225, 270, 315};
   return Pick(direction, choice);
  }
  return -1;
 }

 //------------------------------------------------------------------------------------------------
 protected static float Pick(notnull array<float> values, int choice)
 {
  if (!values.IsIndexValid(choice))
   return -1;
  return values[choice];
 }

 //------------------------------------------------------------------------------------------------
 static float SkipDefault(int key)
 {
  if (key == T_HOURS)
   return HOURS_DEFAULT;
  if (key == T_FADE_OUT)
   return FADE_OUT_DEFAULT;
  if (key == T_HOLD)
   return HOLD_DEFAULT;
  if (key == T_FADE_IN)
   return FADE_IN_DEFAULT;
  if (key == T_SHOW_TIME || key == T_INCLUDE_GM)
   return 1;
  return 0;
 }

 //------------------------------------------------------------------------------------------------
 static float ClampSkip(int key, float value)
 {
  if (key == T_HOURS)
   return Math.Clamp(Math.Round(value), 0, HOURS_MAX);
  if (key == T_MINUTES)
   return Math.Clamp(Math.Round(value), 0, 59);
  if (key == T_FADE_OUT || key == T_FADE_IN)
   return Math.Clamp(value, 0.5, 10);
  if (key == T_HOLD)
   return Math.Clamp(value, 1, 15);
  if (key == T_SHOW_TIME || key == T_INCLUDE_GM)
  {
   if (value != 0)
    return 1;
   return 0;
  }
  return value;
 }

 //------------------------------------------------------------------------------------------------
 static bool IsSkipSwitch(int key)
 {
  return key == T_SHOW_TIME || key == T_INCLUDE_GM;
 }
}
