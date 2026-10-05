// Process-local opt-in. No callbacks, replication, entity queries or playback.
class EAS_Diagnostics
{
 protected static bool s_Checked;
 protected static bool s_Enabled;
 static bool Enabled(IEntity owner = null)
 {
  if (!s_Checked)
  {
   string value;
   s_Enabled = System.GetCLIParam("easDiagnostics", value) && value == "1";
   s_Checked = true;
  }
  if (s_Enabled) return true;
  EAS_AmbientModule war = EAS_AmbientModule.Cast(owner);
  if (war) return war.DebugEnabled == 1;
  EAS_RadioModule finite = EAS_RadioModule.Cast(owner);
  return finite && finite.DebugEnabled == 1;
 }
 // Callers guard Enabled BEFORE formatting their detail string.
 static void Event(string action, IEntity owner, string detail)
 {
  if (!Enabled(owner)) return;
  Write("[EAS DIAG]", action, owner, detail, LogLevel.NORMAL);
 }
 // Validation and playback failures remain visible with diagnostics disabled.
 static void Error(string action, IEntity owner, string detail)
 {
  Write("[EAS ERROR]", action, owner, detail, LogLevel.WARNING);
 }
 protected static void Write(string prefix, string action, IEntity owner, string detail, LogLevel level)
 {
  string identity = "world";
  bool authority = Replication.IsServer() || !Replication.IsRunning();
  if (owner)
  {
   identity = string.Format("%1", owner);
   RplComponent replication = RplComponent.Cast(owner.FindComponent(RplComponent));
   if (replication)
   {
    authority = replication.Role() == RplRole.Authority;
    RplId id = replication.Id();
    if (id.IsValid()) identity = "rpl:" + id.AsString();
   }
  }
  Print(string.Format("%1 action=%2 owner=%3 console=%4 authority=%5 %6", prefix, action, identity, System.IsConsoleApp(), authority, detail), level);
 }
}

// Fixed-size five-second counters, fed by the two existing runtime ticks only.
// Integer-ms zeros are below timer resolution, not proof of zero work.
class EAS_DiagnosticWindow
{
 int Calls, TotalMS, MaxMS, Starts, Stops, Dropped, Scheduled;
 protected float m_NextReport;
 void EAS_DiagnosticWindow(float now = 0) { Reset(now); }
 bool Due(float now) { return now >= m_NextReport || now < m_NextReport - 5; }
 void Tick(int elapsed)
 {
  Calls = Math.Min(Calls + 1, 1000000);
  TotalMS = Math.Min(TotalMS + Math.Clamp(elapsed, 0, 1000000), 1000000000);
  MaxMS = Math.Max(MaxMS, elapsed);
 }
 void Reset(float now)
 {
  Calls = 0; TotalMS = 0; MaxMS = 0; Starts = 0; Stops = 0; Dropped = 0; Scheduled = 0;
  m_NextReport = now + 5;
 }
 void Report(string runtime, float now, int admitted, int voices, int pending, int interval, bool final = false)
 {
  if (!EAS_Diagnostics.Enabled() || (!final && !Due(now))) return;
  string state = string.Format("runtime=%1 admitted=%2 voices=%3 pending=%4 interval_ms=%5 final=%6", runtime, admitted, voices, pending, interval, final);
  string counts = string.Format(" calls=%1 total_ms=%2 max_ms=%3 scheduled=%4 starts=%5 stops=%6 rejected=%7", Calls, TotalMS, MaxMS, Scheduled, Starts, Stops, Dropped);
  EAS_Diagnostics.Event("summary", null, state + counts);
  Reset(now);
 }
}
