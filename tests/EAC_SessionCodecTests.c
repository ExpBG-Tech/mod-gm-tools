// Pure codec tests: safe to execute in a resource-only Workbench fixture.
// Ported unchanged from the standalone repo; EAC_CDFSessionTests calls Run().
class EAC_SessionCodecTests
{
 static bool RoundTrip(string value)
 {
  string restored;
  for (int offset = 0; offset < value.Length(); offset += 9)
  {
   string part;
   if (!EAC_SessionSettings.UnpackText(EAC_SessionSettings.PackText(value, offset), Math.Min(9, value.Length() - offset), part)) return false;
   restored += part;
  }
  return restored == value;
 }

 static bool Run()
 {
  if (!RoundTrip("") || !RoundTrip("A") || !RoundTrip("ABCDE") || !RoundTrip("ABCDEF") || !RoundTrip("ABCDEFGH") || !RoundTrip("ABCDEFGHI") || !RoundTrip("ABCDEFGHIJ")) return false;
  if (EAC_SessionSettings.PackText("ABC", 0) != Vector(4407873, 0, 0)) return false;
  if (EAC_SessionSettings.PackText("ABCDEF", 0) != Vector(4407873, 4605252, 0)) return false;
  if (EAC_SessionSettings.PackText("ABC", 9) != vector.Zero) return false;
  string maximum;
  for (int i = 0; i < 512; i++)
  {
   int code = 32 + i % 95;
   maximum += code.AsciiToString();
  }
  if (!RoundTrip(maximum) || !EAC_SessionSettings.TextSupported(maximum, 512) || EAC_SessionSettings.TextSupported(maximum, 511)) return false;
  if (EAC_SessionSettings.TEXT_FIRST + 56 != EAC_SessionSettings.COMMIT - 1) return false;
  string decoded;
  if (EAC_SessionSettings.UnpackText("65.5 0 0", 1, decoded)) return false;
  if (EAC_SessionSettings.UnpackText("16777216 0 0", 1, decoded)) return false;
  if (EAC_SessionSettings.UnpackText("-1 0 0", 1, decoded)) return false;
  if (EAC_SessionSettings.UnpackText("31 0 0", 1, decoded) || EAC_SessionSettings.UnpackText("127 0 0", 1, decoded)) return false;
  if (EAC_SessionSettings.UnpackText("16706 0 0", 1, decoded)) return false;
  if (!EAC_SessionSettings.UnpackText(vector.Zero, 0, decoded) || !decoded.IsEmpty()) return false;
  if (EAC_SessionSettings.TextSupported("line\nbreak", 128) || EAC_SessionSettings.TextSupported("tab\tvalue", 128)) return false;
  return true;
 }
}
