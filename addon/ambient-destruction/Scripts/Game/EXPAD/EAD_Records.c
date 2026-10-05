// Mission records own transforms, never hidden scenery or character entities.
class EAD_PropRecord
{
 int Asset;
 bool Suppressed;
 vector Transform[4];
}
class EAD_Random
{
 protected int m_State;
 void EAD_Random(int seed) { m_State = Math.Clamp(seed, 1, 2147483646); }
 float Next()
 {
  int high = m_State / 127773;
  int low = m_State - high * 127773;
  m_State = 16807 * low - 2836 * high;
  if (m_State <= 0) m_State += 2147483647;
  return m_State / 2147483647.0;
 }
}
class EAD_Policy
{
 static void BodyVariants(int mode, array<int> variants)
 {
  variants.Clear();
  if (mode != 1) { variants.Insert(31); variants.Insert(32); variants.Insert(33); }
  if (mode != 0) { variants.Insert(4); variants.Insert(5); variants.Insert(6); variants.Insert(7); }
 }
 // A completed group always has an undressed member; other members get at most one prop.
 static bool DressBody(int member, int bareMember, float roll)
 {
  return member != bareMember && roll < 0.5;
 }
 static float DistanceSq(vector a, vector b)
 {
  float x = a[0] - b[0];
  float z = a[2] - b[2];
  return x * x + z * z;
 }
 static bool Near(vector point, vector center, float radius, float margin)
 {
  float distance = radius + margin;
  return DistanceSq(point, center) <= distance * distance;
 }
 static int Count(int intensity, int cap) { return Math.Floor(Math.Clamp(intensity, 0, 100) * cap / 100.0); }
}

// No repeated vehicle family in a pass. Four old-car paints occupy one family slot.
class EAD_WreckBag
{
 protected ref array<int> m_Assets = {};
 int Next(EAD_Random random)
 {
  if (m_Assets.IsEmpty())
  {
   m_Assets.Insert(2); m_Assets.Insert(3);
   for (int asset = 8; asset <= 25; asset++) m_Assets.Insert(asset);
   m_Assets.Insert(26 + Math.Min(3, Math.Floor(random.Next() * 4)));
   m_Assets.Insert(30);
  }
  int index = Math.Min(m_Assets.Count() - 1, Math.Floor(random.Next() * m_Assets.Count()));
  int selected = m_Assets[index];
  m_Assets.RemoveOrdered(index);
  return selected;
 }
}
