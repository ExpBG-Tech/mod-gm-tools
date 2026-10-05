// Vinny Sounds migration bank; recording IDs and range options are permanent.
// Event names, durations and ranges match Audio/EXPBG/AmbientSounds/EXPBG_Sounds.acp.
class EAS_SoundBank
{
 static const ResourceName PROJECT = "{ADE0062BEE2323CA}Audio/EXPBG/AmbientSounds/EXPBG_Sounds.acp";
 static bool ValidRange(int range)
 {
  switch (range)
  {
   case 30: case 40: case 50: case 100: case 150: case 200: case 250: case 300: case 500: case 600: case 1000: case 1500: return true;
  }
  return false;
 }
 static string Event(int recording, int range = 50)
 {
  if (!ValidRange(range)) return "";
  switch (recording)
  {
   case 0: return "SOUND_EAS_VINNY_RADIO_STATIC" + "_R" + range.ToString();
   case 1: return "SOUND_EAS_VINNY_APACHE_1" + "_R" + range.ToString();
   case 2: return "SOUND_EAS_VINNY_APACHE_2" + "_R" + range.ToString();
   case 3: return "SOUND_EAS_VINNY_RUSSIAN_1" + "_R" + range.ToString();
   case 4: return "SOUND_EAS_VINNY_RUSSIAN_2" + "_R" + range.ToString();
   case 5: return "SOUND_EAS_VINNY_RUSSIAN_3" + "_R" + range.ToString();
   case 6: return "SOUND_EAS_VINNY_RUSSIAN_4" + "_R" + range.ToString();
   case 7: return "SOUND_EAS_VINNY_CHINESE_1" + "_R" + range.ToString();
   case 8: return "SOUND_EAS_VINNY_ARAB_CHATTER" + "_R" + range.ToString();
   case 9: return "SOUND_EAS_VINNY_ARAB_1" + "_R" + range.ToString();
   case 10: return "SOUND_EAS_VINNY_HANOI_HANNAH" + "_R" + range.ToString();
   case 11: return "SOUND_EAS_VINNY_CLOSE_FIREFIGHT" + "_R" + range.ToString();
   case 12: return "SOUND_EAS_VINNY_DISTANT_FIREFIGHT" + "_R" + range.ToString();
   case 13: return "SOUND_EAS_VINNY_DISTANT_SHELLING" + "_R" + range.ToString();
   case 14: return "SOUND_EAS_VINNY_JET_FLYBY_1" + "_R" + range.ToString();
   case 15: return "SOUND_EAS_VINNY_JET_FLYBY_2" + "_R" + range.ToString();
   case 16: return "SOUND_EAS_VINNY_DRONE_OVERHEAD" + "_R" + range.ToString();
   case 17: return "SOUND_EAS_VINNY_MARKET" + "_R" + range.ToString();
   case 18: return "SOUND_EAS_VINNY_MARKET_SELLER" + "_R" + range.ToString();
   case 19: return "SOUND_EAS_VINNY_STREET_SINGING" + "_R" + range.ToString();
   case 20: return "SOUND_EAS_VINNY_MUSLIM_PRAYER" + "_R" + range.ToString();
   case 21: return "SOUND_EAS_VINNY_TRAFFIC_MEDIUM" + "_R" + range.ToString();
   case 22: return "SOUND_EAS_VINNY_CHURCH_BELL" + "_R" + range.ToString();
   case 23: return "SOUND_EAS_VINNY_CAR_ALARM" + "_R" + range.ToString();
   case 24: return "SOUND_EAS_VINNY_POLICE_CAR" + "_R" + range.ToString();
   case 25: return "SOUND_EAS_VINNY_NOKIA_RINGTONE" + "_R" + range.ToString();
   case 26: return "SOUND_EAS_VINNY_DISTANT_BARKING" + "_R" + range.ToString();
   case 27: return "SOUND_EAS_VINNY_DISTANT_SHEEP" + "_R" + range.ToString();
  }
  return "";
 }
 static float Duration(int recording)
 {
  switch (recording)
  {
   case 0: return 18.7181042;
   case 1: return 230.2490625;
   case 2: return 177.7066667;
   case 3: return 241.7893958;
   case 4: return 139.8073542;
   case 5: return 263.4071667;
   case 6: return 240.5819583;
   case 7: return 331.1397708;
   case 8: return 70.2403542;
   case 9: return 75.4880625;
   case 10: return 107.3437642;
   case 11: return 188.0190000;
   case 12: return 195.0080000;
   case 13: return 131.1770833;
   case 14: return 24.2925833;
   case 15: return 63.5083542;
   case 16: return 22.4498333;
   case 17: return 94.8302948;
   case 18: return 101.4712018;
   case 19: return 130.4032653;
   case 20: return 165.7440363;
   case 21: return 87.7574603;
   case 22: return 32.5543764;
   case 23: return 16.4165079;
   case 24: return 22.9180952;
   case 25: return 12.0047166;
   case 26: return 126.0611338;
   case 27: return 11.7028571;
  }
  return 0;
 }
 static bool ValidSelection(int selection) { return !Event(selection).IsEmpty(); }
 static int Resolve(int selection, int previous)
 {
  if (!Event(selection).IsEmpty()) return selection;
  return -1;
 }
}
