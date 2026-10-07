// Cross-checks for the performance indexes (0.1.15). Production never sets Enabled; only
// the native test fixtures do. While it is set, a lookup that now uses a precomputed index
// or set also runs its old full scan and reports any difference here. Fixtures print
// Mismatches in their RESULT line and require 0. No cache decision reads this class.
class EBG_DebugChecks
{
 static bool Enabled;
 static int Mismatches;

 static void Mismatch(string what)
 {
  Mismatches++;
  if (Mismatches <= 20) Print("[EBG DEBUGCHECK MISMATCH] " + what, LogLevel.WARNING);
 }
}
