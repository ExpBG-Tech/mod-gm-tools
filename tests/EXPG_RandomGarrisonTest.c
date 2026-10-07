// Native Enforce contract (tests/Run-Contracts.ps1, Workbench ResourceManager): the pure
// Random Garrison rules (EXPG_RandomGarrisonRules.c) and the setting checks. No world,
// no gameplay; excluded from the addon.
class EXPG_RandomGarrisonTest
{
 // Buckets by member count, size presets, the building target, setting ranges, the
 // stable sort key, the catalog's usable count (sizes, support, faction).
 static bool Rules()
 {
  if (EXPG_RGRules.Bucket(0) != 0 || EXPG_RGRules.Bucket(33) != 0)
  {
   return false;
  }
  if (EXPG_RGRules.Bucket(1) != EXPG_RGRules.BUCKET_TEAM || EXPG_RGRules.Bucket(3) != EXPG_RGRules.BUCKET_TEAM)
  {
   return false;
  }
  if (EXPG_RGRules.Bucket(4) != EXPG_RGRules.BUCKET_FIRETEAM || EXPG_RGRules.Bucket(5) != EXPG_RGRules.BUCKET_FIRETEAM)
  {
   return false;
  }
  if (EXPG_RGRules.Bucket(6) != EXPG_RGRules.BUCKET_SQUAD || EXPG_RGRules.Bucket(9) != EXPG_RGRules.BUCKET_SQUAD)
  {
   return false;
  }
  if (EXPG_RGRules.Bucket(10) != EXPG_RGRules.BUCKET_LARGE || EXPG_RGRules.Bucket(32) != EXPG_RGRules.BUCKET_LARGE)
  {
   return false;
  }
  array<int> presets = {2, 6, 4, 12, 3, 15};
  foreach (int preset : presets)
  {
   if (!EXPG_RGRules.ValidSizes(preset))
   {
    return false;
   }
  }
  array<int> invalid = {0, 1, 5, 7, 8, 16};
  foreach (int mask : invalid)
  {
   if (EXPG_RGRules.ValidSizes(mask))
   {
    return false;
   }
  }
  if (EXPG_RGRules.Target(4, 100, 25) != 4 || EXPG_RGRules.Target(32, 10, 25) != 3 || EXPG_RGRules.Target(10, 50, 7) != 4)
  {
   return false;
  }
  if (EXPG_RGRules.Target(40, 100, 100) != 32 || EXPG_RGRules.Target(4, 100, 0) != 0 || EXPG_RGRules.Target(4, 1, 3) != 1)
  {
   return false;
  }
  if (!EXPG_RandomGarrisonModule.ValidSetting(EXPG_RandomGarrisonModule.KEY_RADIUS, 150) || EXPG_RandomGarrisonModule.ValidSetting(EXPG_RandomGarrisonModule.KEY_RADIUS, 1001))
  {
   return false;
  }
  if (!EXPG_RandomGarrisonModule.ValidSetting(EXPG_RandomGarrisonModule.KEY_SIZES, 6) || EXPG_RandomGarrisonModule.ValidSetting(EXPG_RandomGarrisonModule.KEY_SIZES, 7))
  {
   return false;
  }
  if (EXPG_RandomGarrisonModule.ValidSetting(EXPG_RandomGarrisonModule.KEY_SQUADS_MAX, 5) || EXPG_RandomGarrisonModule.ValidSetting(EXPG_RandomGarrisonModule.INT_SETTINGS, 0))
  {
   return false;
  }
  string low = EXPG_RGRules.SortKey("9 0 1", "b");
  string high = EXPG_RGRules.SortKey("10 0 1", "a");
  if (low != "0050000900 0050000100 b" || low.Compare(high) >= 0)
  {
   return false;
  }
  return Usable();
 }

 // The catalog check before any analysis counts only squads a building can draw: of
 // an enabled size, not support when excluded, of the drawn faction (or of none).
 static bool Usable()
 {
  EXPG_SquadCatalog listed = new EXPG_SquadCatalog();
  EXPG_SquadEntry usTeam = new EXPG_SquadEntry();
  usTeam.Members = 4;
  usTeam.Bucket = EXPG_RGRules.Bucket(4);
  usTeam.FactionId = "US";
  listed.Entries.Insert(usTeam);
  EXPG_SquadEntry medics = new EXPG_SquadEntry();
  medics.Members = 5;
  medics.Bucket = EXPG_RGRules.Bucket(5);
  medics.Support = true;
  listed.Entries.Insert(medics);
  if (listed.CountUsable(EXPG_RGRules.BUCKET_FIRETEAM, true, "USSR") != 0 || listed.CountUsable(EXPG_RGRules.BUCKET_SQUAD, false, "US") != 0)
  {
   return false;
  }
  if (listed.CountUsable(EXPG_RGRules.BUCKET_FIRETEAM, true, "US") != 1 || listed.CountUsable(EXPG_RGRules.BUCKET_FIRETEAM, false, "USSR") != 1)
  {
   return false;
  }
  return listed.CountUsable(EXPG_RGRules.BUCKET_FIRETEAM | EXPG_RGRules.BUCKET_SQUAD, false, "") == 2;
 }

 // The seeded Fisher-Yates shuffle: a permutation, the same for the same seed.
 static bool Shuffle()
 {
  array<int> first = {};
  array<int> second = {};
  array<int> other = {};
  for (int i = 0; i < 20; i++)
  {
   first.Insert(i);
   second.Insert(i);
   other.Insert(i);
  }
  EXPG_RGRules.Shuffle(first, 4242);
  EXPG_RGRules.Shuffle(second, 4242);
  EXPG_RGRules.Shuffle(other, 4243);
  bool differs;
  for (int j = 0; j < 20; j++)
  {
   if (first[j] != second[j])
   {
    return false;
   }
   if (first[j] != other[j]) { differs = true; }
  }
  array<int> sorted = {};
  sorted.Copy(first);
  sorted.Sort();
  for (int k = 0; k < 20; k++)
  {
   if (sorted[k] != k)
   {
    return false;
   }
  }
  if (EXPG_RGRules.SiteSeed(4242, "key") != EXPG_RGRules.SiteSeed(4242, "key") || EXPG_RGRules.SiteSeed(4242, "key") == EXPG_RGRules.SiteSeed(4243, "key"))
  {
   return false;
  }
  return differs;
 }

 // Faction keys saved as (hi16, lo16, 1) floats, the 24-bit token, the box distance.
 static bool Packing()
 {
  vector packed = EXPG_RGRules.PackKey("USSR");
  if (packed[2] != 1 || packed[0] < 0 || packed[0] > 65535 || packed[1] < 0 || packed[1] > 65535)
  {
   return false;
  }
  if (!EXPG_RGRules.SameKey(packed, "USSR") || EXPG_RGRules.SameKey(packed, "US") || EXPG_RGRules.SameKey(vector.Zero, "USSR"))
  {
   return false;
  }
  if (EXPG_RGRules.PackKey("") != vector.Zero)
  {
   return false;
  }
  if (EXPG_RGRules.Token(1, 2) != "rg:1-2" || !EXPG_RGRules.Token(0, 2).IsEmpty() || !EXPG_RGRules.Token(16777216, 1).IsEmpty())
  {
   return false;
  }
  if (EXPG_RGRules.Token(16777215, 16777215) != "rg:16777215-16777215")
  {
   return false;
  }
  if (Math.AbsFloat(EXPG_RGRules.BoxDistance("0 0 0", "1 0 0", "2 1 1") - 1) > 0.001 || EXPG_RGRules.BoxDistance("1.5 0.5 0.5", "1 0 0", "2 1 1") != 0)
  {
   return false;
  }
  return true;
 }
}
