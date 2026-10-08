// Pure rules of the Random Garrison module, contract-tested natively by
// tests/EXPG_RandomGarrisonTest.c: squad size buckets and presets, the building
// target (and the try limit, modelled by tests/Test-RandomGarrisonFill.ps1), the
// stable building order, the seeded shuffle, the zone token, the
// faction key packing for attribute saves (CDF) and the support squad file names.
// No world access.
class EXPG_RGRules
{
 // Buckets by member count (never by the GROUPSIZE labels, which are often stale).
 static const int BUCKET_TEAM = 1;
 static const int BUCKET_FIRETEAM = 2;
 static const int BUCKET_SQUAD = 4;
 static const int BUCKET_LARGE = 8;
 static const int ALL_BUCKETS = 15;
 // Never more buildings per generation.
 static const int MAX_TARGET = 32;
 // Token halves are 24-bit, so they store exactly in the floats of attribute saves.
 static const int TOKEN_MAX = 16777215;
 static const int KEY_HALF = 65536;

 // Team 1-3, fire team 4-5, squad 6-9, large 10-32 soldiers; 0 outside 1-32.
 static int Bucket(int members)
 {
  if (members < 1 || members > 32)
  {
   return 0;
  }
  if (members <= 3)
  {
   return BUCKET_TEAM;
  }
  if (members <= 5)
  {
   return BUCKET_FIRETEAM;
  }
  if (members <= 9)
  {
   return BUCKET_SQUAD;
  }
  return BUCKET_LARGE;
 }

 // The last support check of a catalog squad (EXPG_SquadCatalog.Step), for squads of
 // other mods that carry no support labels, on the group or on its soldiers (for
 // example REAPER_USSR_HelicopterCrew.et, whose pilots are plain USSR characters).
 // Only the file name counts, never the folders, in lower case. Each word names one
 // kind of support squad: medic (medic, medics, medical), ammo and ammunition (ammo
 // teams), suppl (supply and supplies teams), logistic (logistics), crew (vehicle and
 // helicopter crews), pilot.
 // None of them is part of an infantry squad's name (RifleSquad, FireTeam, Team_AT,
 // Team_Suppress, MachineGunTeam, Sniper, Recon, Spetsnaz, KLMK, Naval_Infantry);
 // "sup" or "supp" would hit Team_Suppress ("suppl" does not). Transport is left to
 // its labels.
 static bool SupportName(string prefabPath)
 {
  string name = prefabPath;
  name.ToLower();
  int cut = name.LastIndexOf("/");
  if (cut < 0) { cut = name.LastIndexOf("}"); }
  if (cut >= 0) { name = name.Substring(cut + 1, name.Length() - cut - 1); }
  return name.Contains("medic") || name.Contains("ammo") || name.Contains("ammunition") || name.Contains("suppl") || name.Contains("logistic") || name.Contains("crew") || name.Contains("pilot");
 }

 // The Squad sizes presets: fire teams (2), fire teams and squads (6), squads (4),
 // squads and large (12), small teams and fire teams (3), any (15).
 static bool ValidSizes(int mask)
 {
  if (mask == 2 || mask == 6 || mask == 4)
  {
   return true;
  }
  return mask == 12 || mask == 3 || mask == 15;
 }

 // Buildings to garrison: min(Buildings, ceil(eligible x Share / 100), 32).
 static int Target(int buildings, int share, int eligible)
 {
  if (eligible <= 0 || buildings <= 0 || share <= 0)
  {
   return 0;
  }
  int byShare = (eligible * share + 99) / 100;
  int target = buildings;
  if (byShare < target)
  {
   target = byShare;
  }
  if (target > MAX_TARGET)
  {
   target = MAX_TARGET;
  }
  return target;
 }

 // Buildings one generation may try (analyse): three times the target, at least the
 // target plus 16, never more than the eligible buildings. A failed building is
 // replaced by the next one until then (0.1.14 stopped at the target plus 8, too few
 // for villages where most buildings are sheds or one-room houses).
 static int AttemptCap(int target, int eligible)
 {
  if (target <= 0 || eligible <= 0)
  {
   return 0;
  }
  int cap = target * 3;
  if (cap < target + 16)
  {
   cap = target + 16;
  }
  if (cap > eligible)
  {
   cap = eligible;
  }
  return cap;
 }

 // Integer position (centimetres) and prefab: the eligible list is sorted by it, so
 // the result never depends on the order a spatial query returned buildings in.
 static string SortKey(vector origin, string prefab)
 {
  int x = Math.Round(origin[0] * 100);
  int z = Math.Round(origin[2] * 100);
  x += 50000000;
  z += 50000000;
  return x.ToString(10) + " " + z.ToString(10) + " " + prefab;
 }

 // Fisher-Yates with the zone's own generator: the same seed gives the same order.
 static void Shuffle(notnull array<int> order, int seed)
 {
  RandomGenerator generator = new RandomGenerator();
  generator.SetSeed(seed);
  for (int i = order.Count() - 1; i > 0; i--)
  {
   int j = generator.RandInt(0, i + 1);
   int held = order[i];
   order[i] = order[j];
   order[j] = held;
  }
 }

 // Each building's own generator seed: squad picks do not depend on the order in
 // which building analyses finish.
 static int SiteSeed(int seed, string key)
 {
  return seed * 31 + key.Hash();
 }

 // One token half in 512..TOKEN_MAX. Math.RandomInt misbehaves on ranges above
 // 32767 (it returned out-of-range halves, so some zones had no token), so the
 // half is built from two narrow draws: 15 bits times 512 plus 9 bits.
 static int RandomTokenPart()
 {
  int high = Math.RandomInt(1, 32767);
  int low = Math.RandomInt(0, 512);
  return high * 512 + low;
 }

 static string Token(int hi, int lo)
 {
  if (hi < 1 || lo < 1 || hi > TOKEN_MAX || lo > TOKEN_MAX)
  {
   return string.Empty;
  }
  return string.Format("rg:%1-%2", hi, lo);
 }

 // A faction key for attribute saves: (hash high 16 bits, low 16 bits, 1); none is 0 0 0.
 static vector PackKey(string key)
 {
  if (key.IsEmpty())
  {
   return vector.Zero;
  }
  int hash = key.Hash();
  int hi = (hash >> 16) & 65535;
  int lo = hash & 65535;
  return Vector(hi, lo, 1);
 }

 static bool SameKey(vector packed, string key)
 {
  if (packed[2] < 0.5 || key.IsEmpty())
  {
   return false;
  }
  vector own = PackKey(key);
  return Math.Round(packed[0]) == Math.Round(own[0]) && Math.Round(packed[1]) == Math.Round(own[1]);
 }

 // Distance from a point to an axis-aligned box (0 inside).
 static float BoxDistance(vector point, vector mins, vector maxs)
 {
  float dx = 0;
  float dy = 0;
  float dz = 0;
  if (point[0] < mins[0]) { dx = mins[0] - point[0]; }
  else if (point[0] > maxs[0]) { dx = point[0] - maxs[0]; }
  if (point[1] < mins[1]) { dy = mins[1] - point[1]; }
  else if (point[1] > maxs[1]) { dy = point[1] - maxs[1]; }
  if (point[2] < mins[2]) { dz = mins[2] - point[2]; }
  else if (point[2] > maxs[2]) { dz = point[2] - maxs[2]; }
  return Math.Sqrt(dx * dx + dy * dy + dz * dz);
 }
}
