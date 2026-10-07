// EXPBG Time and Weather: calendar arithmetic and the Time Skip text. Pure functions,
// shared by the server (date rollover, log lines) and clients (black screen text).
class ETW_TimeMath
{
 //------------------------------------------------------------------------------------------------
 static bool IsLeapYear(int year)
 {
  if (year % 400 == 0)
   return true;
  if (year % 100 == 0)
   return false;
  return year % 4 == 0;
 }

 //------------------------------------------------------------------------------------------------
 static int DaysInMonth(int year, int month)
 {
  if (month == 2)
  {
   if (IsLeapYear(year))
    return 29;
   return 28;
  }
  if (month == 4 || month == 6 || month == 9 || month == 11)
   return 30;
  return 31;
 }

 //------------------------------------------------------------------------------------------------
 // Adds 'add' hours (0 or more) to a date and time of day (hours 0-24). Days roll over
 // into the next month and year; the engine does not roll the date by itself when the
 // time of day is set.
 static void AddHours(int year, int month, int day, float hours24, float add, out int outYear, out int outMonth, out int outDay, out float outHours)
 {
  float total = hours24 + Math.Max(add, 0);
  int days = Math.Floor(total / 24);
  float rest = total - days * 24;
  if (rest >= 24)
  {
   rest -= 24;
   days++;
  }
  if (rest < 0)
   rest = 0;
  outYear = year;
  outMonth = Math.ClampInt(month, 1, 12);
  outDay = Math.ClampInt(day, 1, DaysInMonth(outYear, outMonth));
  // Bounded: a skip is at most 48 h 59 min.
  for (int i = 0; i < days && i < 400; i++)
  {
   outDay++;
   if (outDay <= DaysInMonth(outYear, outMonth))
    continue;
   outDay = 1;
   outMonth++;
   if (outMonth <= 12)
    continue;
   outMonth = 1;
   outYear++;
  }
  outHours = rest;
 }

 //------------------------------------------------------------------------------------------------
 // "HH:MM", rounded to the minute.
 static string FormatClock(float hours24)
 {
  int minutes = Math.Round(hours24 * 60);
  minutes = minutes % 1440;
  if (minutes < 0)
   minutes += 1440;
  int hours = minutes / 60;
  int rest = minutes % 60;
  return hours.ToString(2) + ":" + rest.ToString(2);
 }

 //------------------------------------------------------------------------------------------------
 // "YYYY-MM-DD".
 static string FormatDate(int year, int month, int day)
 {
  return year.ToString() + "-" + month.ToString(2) + "-" + day.ToString(2);
 }

 //------------------------------------------------------------------------------------------------
 // "6 h 30 min" for status and log lines.
 static string FormatSkip(int totalMinutes)
 {
  int hours = totalMinutes / 60;
  int minutes = totalMinutes % 60;
  return hours.ToString() + " h " + minutes.ToString(2) + " min";
 }

 //------------------------------------------------------------------------------------------------
 // "1 hour later", "6 hours later", "2 hours 30 minutes later", "45 minutes later".
 static string AutoText(int hours, int minutes)
 {
  string hourWord = "hours";
  if (hours == 1)
   hourWord = "hour";
  string minuteWord = "minutes";
  if (minutes == 1)
   minuteWord = "minute";
  if (hours > 0 && minutes > 0)
   return string.Format("%1 %2 %3 %4 later", hours, hourWord, minutes, minuteWord);
  if (hours > 0)
   return string.Format("%1 %2 later", hours, hourWord);
  return string.Format("%1 %2 later", minutes, minuteWord);
 }

 //------------------------------------------------------------------------------------------------
 // Black screen text. Empty text and the unchanged default use the automatic phrase
 // (singular, minutes). A text starting with # is a string-table key, translated on the
 // client before the tokens {hours}, {minutes}, {time} and {date} are replaced.
 static string SkipText(string text, int totalMinutes, string clock, string date)
 {
  int hours = totalMinutes / 60;
  int minutes = totalMinutes % 60;
  string result = text.Trim();
  if (result.IsEmpty() || result == ETW_Settings.TEXT_DEFAULT)
   return AutoText(hours, minutes);
  if (result.StartsWith("#"))
   result = WidgetManager.Translate(result);
  result.Replace("{hours}", hours.ToString());
  result.Replace("{minutes}", minutes.ToString());
  result.Replace("{time}", clock);
  result.Replace("{date}", date);
  return result;
 }
}
