// EXPBG GM Optimizer | M.Pac and K.Edgar
// All activation decisions use XZ distance and an independent, signed unit-height band.
class EBG_CacheGeometry
{
 static float DistanceSq(vector a, vector b)
 {
  float x = a[0] - b[0];
  float z = a[2] - b[2];
  return x * x + z * z;
 }
 static bool Contains(vector player, vector center, float radius, float minY, float maxY, bool height, float above, float below, float margin)
 {
  if (radius < 0 || DistanceSq(player, center) > radius * radius) return false;
  return !height || (player[1] >= minY - below - margin && player[1] <= maxY + above + margin);
 }
 static bool AnyPlayer(array<vector> players, vector center, float radius, float minY, float maxY, bool height, float above, float below, float margin)
 {
  foreach (vector player : players)
   if (Contains(player, center, radius, minY, maxY, height, above, below, margin)) return true;
  return false;
 }
 static bool CanSleep(float now, float activeSince, float clearSince, float minimumActive, float delay)
 {
  return clearSince >= 0 && now - activeSince >= minimumActive && now - clearSince >= delay;
 }
}
