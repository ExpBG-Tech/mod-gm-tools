// EXPBG GM Optimizer | M.Pac and K.Edgar
// One public, read-only sample per second. Cache decisions never consume telemetry.
class EBG_CacheDebug
{
 static int Level;
 static bool Closing;
 static int CleanedBodies;
 protected static CanvasWidget s_Panel;
 protected static ref array<ref CanvasWidgetCommand> s_Commands;

 static string One(float value) { return value.ToString(-1, 1); }
 static string Allocations()
 {
  float allocated = System.MemoryAllocationKB();
  if (allocated <= 0) return "Engine allocations unavailable";
  return "Engine allocations " + One(allocated / 1024.0) + " MiB";
 }
 static string OnOff(int value)
 {
  if (value) { return "ON"; }
  return "OFF";
 }
 static string Timing(float fps)
 {
  if (fps <= 0) { return "FPS unavailable"; }
  return string.Format("%1 FPS | %2 ms/frame", One(fps), One(1000 / fps));
 }
 // Server: true while any cache zone has Debug messages on. Routine per-unit and
 // progress log lines of the pack are printed only then; warnings and summaries
 // always print.
 static bool Verbose()
 {
  foreach (EBG_CacheZone zone : EBG_CacheZone.Zones)
  {
   if (zone && zone.DebugMessages > 0) return true;
  }
  return false;
 }
 static string ServerSample()
 {
  Level = 0;
  foreach (EBG_CacheZone zone : EBG_CacheZone.Zones)
  {
   if (zone) { Level = Math.Max(Level, zone.DebugMessages); }
  }
  if (!Level) { return ""; }
  int live, dead, active, simGroups, simUnits, fullGroups, fullUnits, pending, recovery, recoveryUnits;
  EBG_CacheManager manager = EBG_CacheManager.Instance;
  if (manager)
  {
   foreach (EBG_CacheGroup record : manager.Records)
   {
    live += record.Alive; dead += record.Dead;
    EBG_CacheRecordState state = record.CacheState();
    if (state == EBG_CacheRecordState.FULL_CACHED)
    {
     fullGroups++; fullUnits += record.Alive;
    }
    else if (state == EBG_CacheRecordState.SIM_CACHED)
    {
     simGroups++; simUnits += record.Alive;
    }
    else if (state == EBG_CacheRecordState.PENDING) pending++;
    else if (state == EBG_CacheRecordState.RECOVERY) { recovery++; recoveryUnits += record.Alive; }
    else if (state == EBG_CacheRecordState.ACTIVE && record.Alive > 0) active++;
   }
  }
  int seconds = System.GetTickCount() / 1000;
  string text = string.Format("GROUPS  %1 awake | %2 sim cached | %3 full cached", active, simGroups, fullGroups);
  text += string.Format("\nTRACKED  %1 survivors | %2 dead | %3 bodies cleaned", live, dead, CleanedBodies);
  text += string.Format("\nCACHED UNITS  %1 simulation | %2 full", simUnits, fullUnits);
  text += "\nSERVER  " + Timing(System.GetFPS());
  text += string.Format("\n%1 | Players %2 | Sample %3 s", Allocations(), GetGame().GetPlayerManager().GetPlayerCount(), seconds);
  text += string.Format("\nModules %1 | Pending %2 | Recovery %3 (%4 survivors)", EBG_CacheZone.Zones.Count(), pending, recovery, recoveryUnits);
  // ponytail: bound HUD and packet size to four module rows; rotate to cover every module.
  int pages = Math.Max(1, Math.Ceil(EBG_CacheZone.Zones.Count() / 4.0));
  int page = (seconds / 8) % pages;
  text += string.Format("\nMODULE DETAILS  %1/%2 (rotate every 8 s)", page + 1, pages);
  for (int i = page * 4; i < Math.Min(page * 4 + 4, EBG_CacheZone.Zones.Count()); i++)
  {
   EBG_CacheZone zone = EBG_CacheZone.Zones[i];
   if (!zone) { continue; }
   string mode = "SIM"; if (zone.Mode == 1) { mode = "FULL"; }
   string strategy = "ZONE"; if (zone.Strategy == 1) { strategy = "GROUP"; }
   string state = OnOff(zone.Enabled);
   if (zone.Editing) { state = "PAUSED - enable caching to resume"; }
   if (zone.HasPendingSettings()) { state = "SETTINGS QUEUED"; }
   vector origin = zone.GetOrigin();
   text += string.Format("\n#%1 @ %2,%3  %4/%5 | %6 | Cleanup %7", i + 1, Math.Round(origin[0]), Math.Round(origin[2]), mode, strategy, state, OnOff(zone.Cleanup));
   text += string.Format("\n  %1 groups | %2 cached | %3 pending | %4 recovery | %5 blocked", zone.ManagedCount, zone.CachedCount, zone.PendingCount, zone.RecoveryCount, zone.SkippedCount);
   if (zone.SkippedCount > 0 && zone.BlockedReason != "")
   {
    string hold = zone.BlockedReason;
    if (hold.Length() > 76) hold = hold.Substring(0, 73) + "...";
    text += "\n  Hold: " + hold;
   }
   // Why an enabled zone enrolls or caches nothing (full text in the zone status).
   string why;
   if (zone.Enabled && !zone.Editing)
   {
    if (zone.PlayerAwakeCount > 0 && zone.ManagedCount > 0) why = EBG_CacheManager.PlayerAwakeNote(zone);
    else why = zone.EnrollmentNote;
   }
   if (why != "")
   {
    if (why.Length() > 76) why = why.Substring(0, 73) + "...";
    text += "\n  Why: " + why;
   }
  }
  return text;
 }
 static void Reset(bool closing = false)
 {
		EXPBG_LazyStatics_EBG_CacheDebug();
  Closing = closing;
  if (s_Panel) { s_Panel.RemoveFromHierarchy(); }
  s_Panel = null; s_Commands.Clear(); Level = 0;
 }
 protected static void Line(string value, float x, float y, float size, int color = 0xffeeeeee)
 {
		EXPBG_LazyStatics_EBG_CacheDebug();
  TextDrawCommand command = new TextDrawCommand();
  command.m_sText = value; command.m_Position = Vector(x, y, 0);
  command.m_iColor = color; command.m_fSize = size; command.m_iFontPropertiesId = 0;
  s_Commands.Insert(command);
 }
 // Hide only the local widget; the server sample and debug Level are untouched.
 protected static void Hide()
 {
		EXPBG_LazyStatics_EBG_CacheDebug();
  if (s_Panel) { s_Panel.RemoveFromHierarchy(); }
  s_Panel = null; s_Commands.Clear();
 }
 static void Draw(string serverText, int receivedAt)
 {
		EXPBG_LazyStatics_EBG_CacheDebug();
  if (Closing) { return; }
  if (serverText == "" || !GetGame().GetPlayerController())
  {
   Reset(); return;
  }
  // A testing tool for logged-in admins: shown only while this client's editor is
  // open, never in first person. Voted Game Masters keep the zone overlays only.
  SCR_EditorManagerEntity editor = SCR_EditorManagerEntity.GetInstance();
  if (!editor || !editor.IsOpened() || !EBG_CacheVisuals.LocalAdmin())
  {
   Hide(); return;
  }
  if (!s_Panel)
  {
   s_Panel = CanvasWidget.Cast(GetGame().GetWorkspace().CreateWidgets("{20AD8671A9C08CD9}UI/layouts/EXPBG/CacheMapOverlay.layout"));
   if (!s_Panel) { return; }
   s_Panel.SetName("EXPBGDebugPanel");
   s_Panel.SetFlags(WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS);
   s_Panel.SetZOrder(100);
  }
  string network = "Local host / no remote connection";
  if (Replication.IsRunning() && !Replication.IsServer())
  {
   RplConnectionStats stats = Replication.GetConnectionStats(RplIdentity.Local());
   network = "Connection statistics unavailable";
   if (stats)
   {
    network = string.Format("Ping %1 ms | Outgoing packet loss %2 percent", One(stats.GetRoundTripTimeInMs()), One(stats.GetPacketLoss() * 100));
   }
  }
  string localText = "EXPBG CACHE MONITOR - TESTING ONLY";
  localText += "\n" + serverText;
  localText += "\nTHIS CLIENT  " + Timing(System.GetFPS());
  localText += string.Format("\n%1 | %2", Allocations(), network);
  float age = System.GetTickCount(receivedAt) * 0.001;
  if (age > 3) { localText += string.Format("\nSERVER DATA STALE: last received %1 s ago", One(age)); }
  localText += "\nEngine allocations are not total process RAM. CPU/GPU/bandwidth: n/a.";
  localText += "\nTracked survivors include absent cached AI and retained recovery.";
  array<string> lines = {}; localText.Split("\n", lines, false);
  float width, height; s_Panel.GetScreenSize(width, height);
  s_Panel.SetSizeInUnits(Vector(width, height, 0)); s_Panel.SetZoom(1); s_Panel.SetOffsetPx(vector.Zero);
  float size = Math.Clamp(height / 55, 12, 17);
  float panelWidth = Math.Min(width - 32, size * 39);
  float x = 24, y = 70, step = size + 4;
  s_Commands.Clear();
  PolygonDrawCommand background = new PolygonDrawCommand();
  background.m_iColor = 0xdf111b24;
  background.m_Vertices = {x - 10, y - 10, x + panelWidth, y - 10, x + panelWidth, y + lines.Count() * step + 8, x - 10, y + lines.Count() * step + 8};
  s_Commands.Insert(background);
  for (int row = 0; row < lines.Count(); row++)
  {
   int color = 0xffeeeeee;
   if (row == 0) { color = 0xffffce2e; }
   if (lines[row].StartsWith("SERVER DATA STALE")) { color = 0xffffa04a; }
   Line(lines[row], x, y + row * step, size, color);
  }
  s_Panel.SetDrawCommands(s_Commands);
 }

	//------------------------------------------------------------------------------------------------
	//! Creates the collections on first use (not in the global static initializer, which has a
	//! per-function instruction limit that large modsets exceed on Windows).
	protected static void EXPBG_LazyStatics_EBG_CacheDebug()
	{
		if (!s_Commands)
			s_Commands = new array<ref CanvasWidgetCommand>();
	}
}

// The always-replicated game mode carries one aggregate snapshot, including JIP.
// Using streamed module proxies would omit distant towns on ordinary clients.
modded class SCR_BaseGameMode
{
 [RplProp(onRplName: "EBG_OnDebugSample")]
 protected string m_EBGDebugSample;
 protected float m_EBGDebugElapsed;
 protected int m_EBGDebugReceived;
 protected void EBG_OnDebugSample() { m_EBGDebugReceived = System.GetTickCount(); }
 override void EOnInit(IEntity owner)
 {
  super.EOnInit(owner);
  EBG_CacheDebug.Reset();
  EBG_CacheDebug.CleanedBodies = 0;
 }
 override void EOnFrame(IEntity owner, float timeSlice)
 {
  super.EOnFrame(owner, timeSlice);
  m_EBGDebugElapsed += timeSlice;
  if (m_EBGDebugElapsed < 1 || !GetGame().InPlayMode() || EBG_CacheDebug.Closing) { return; }
  m_EBGDebugElapsed = 0;
  if (Replication.IsServer())
  {
   string sample = EBG_CacheDebug.ServerSample();
   if (sample != m_EBGDebugSample)
   {
    m_EBGDebugSample = sample; EBG_OnDebugSample(); Replication.BumpMe();
   }
  }
  if (GetGame().GetPlayerController()) { EBG_CacheDebug.Draw(m_EBGDebugSample, m_EBGDebugReceived); }
 }
}
