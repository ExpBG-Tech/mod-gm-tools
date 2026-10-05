// Native Enforce fixture. NOT RUN: no native slot; excluded from addon.
class EXPG_AttributesTest
{
 static bool Run()
 {
  vector defaults = EXPG_GarrisonSettings.Normalize(Vector(300, 400, 1));
  if (defaults != Vector(300, 400, 1)) return false;
  vector low = EXPG_GarrisonSettings.Normalize(Vector(-100, -200, -1));
  if (low != Vector(50, 75, 0)) return false;
  vector high = EXPG_GarrisonSettings.Normalize(Vector(9000, 10000, 99));
  if (high != Vector(3000, 4000, 2)) return false;
  vector crossed = EXPG_GarrisonSettings.Normalize(Vector(800, 400, 1));
  if (crossed != Vector(800, 825, 1)) return false;
  if (EXPG_GarrisonSettings.Normalize(crossed) != crossed) return false;
  vector fractionalMode = EXPG_GarrisonSettings.Normalize(Vector(300, 400, 1.8));
  if (fractionalMode[2] != 2) return false;
  EXPG_GarrisonStatusAttribute status = new EXPG_GarrisonStatusAttribute();
  if (status.IsSerializable() || status.IsEnabled()) return false;
  EXPG_ReleaseGarrisonAttribute release = new EXPG_ReleaseGarrisonAttribute();
  if (release.IsSerializable()) return false;
  return true;
 }
}
