// Canonical schema four appends category gain/frequency; old field identities stay stable.
class EAS_ContentSettings
{
 static bool ValidValue(int key, float value)
 {
  if (value != value || value < -1 || value > 3000 || value != Math.Floor(value)) return false;
  int number = value;
  if (key == 0) return number >= -1 && number <= 4;
  if (number < 0) return false;
  switch (key)
  {
   case 1: return number <= 2;
   case 2: return number <= 500 && number % 25 == 0;
   case 3: return number >= 100 && number % 50 == 0;
   case 4: case 16: case 17: case 18: case 19: return number <= 100;
   case 5: return number <= 120;
   case 6: case 7: case 8: case 10: case 12: case 14: return number <= 1;
   case 9: case 11: case 13: return number <= 2;
   case 15: return number <= 4;
   case 20: case 21: case 22: return number <= 3;
  }
  return false;
 }

 static array<int> Normalize(array<int> values)
 {
  if (!values || (values.Count() != 8 && values.Count() != 15 && values.Count() != 16 && values.Count() != 23)) return null;
  if ((values.Count() == 8 && values[0] < 0) || (values.Count() >= 15 && values[0] != -1)) return null;
  array<int> result = {};
  foreach (int key, int value : values)
  {
   if (!ValidValue(key, value)) return null;
   result.Insert(value);
  }
  if (result.Count() == 8)
  {
   int mode = result[0];
   result[0] = -1;
   result.Insert(mode == 0 || mode == 2 || mode == 3);
   result.Insert(0);
   result.Insert(mode == 1 || mode == 2 || mode == 3);
   result.Insert(0);
   result.Insert(mode == 4);
   result.Insert(2);
   result.Insert(0);
  }
  if (result.Count() == 15)
  {
   // Schema two's Far recordings now belong to Distant, never to Near.
   if (result[9] == 1) result[9] = 0;
   if (result[11] == 1) result[11] = 0;
   result.Insert(0); // Random mix; explicit tracks are stable values 1..4.
  }
  if (result.Count() == 16)
  {
   for (int category = 0; category < 4; category++) result.Insert(result[4]);
   for (int frequency = 0; frequency < 3; frequency++) result.Insert(result[1]);
  }
  return result;
 }
}
