// Opt-in briefing board trace, process-local. Client options (read once):
//  -ebmDiagnostics 1    trace every board: widget creation, render target binding, drawing path
//  -ebmCanvasHandoff 1  use the experimental two render target hand-off on turned screens
//  -ebmDirect 1         skip the upright surface; draw the board turned on the screen itself
// A board's own "Debug trace" attribute also enables its trace. No replication, no callbacks.
class EBM_Diagnostics
{
 protected static bool s_bChecked;
 protected static bool s_bEnabled;
 protected static bool s_bHandoff;
 protected static bool s_bDirect;

 protected static void ReadOptions()
 {
  if (s_bChecked)
   return;
  s_bChecked = true;
  string value;
  s_bEnabled = System.GetCLIParam("ebmDiagnostics", value) && value != "0";
  value = string.Empty;
  s_bHandoff = System.GetCLIParam("ebmCanvasHandoff", value) && value != "0";
  value = string.Empty;
  s_bDirect = System.GetCLIParam("ebmDirect", value) && value != "0";
  if (s_bEnabled || s_bHandoff || s_bDirect)
   Print(string.Format("[EBM DIAG] action=options diagnostics=%1 canvasHandoff=%2 direct=%3", s_bEnabled, s_bHandoff, s_bDirect), LogLevel.NORMAL);
 }

 static bool Enabled(bool boardFlag = false)
 {
  ReadOptions();
  return boardFlag || s_bEnabled;
 }

 static bool HandoffForced()
 {
  ReadOptions();
  return s_bHandoff;
 }

 static bool DirectForced()
 {
  ReadOptions();
  return s_bDirect;
 }

 // Callers check Enabled before building the detail text.
 static void Event(string action, IEntity owner, string detail)
 {
  string identity = "none";
  if (owner)
  {
   identity = string.Format("%1", owner);
   RplComponent replication = RplComponent.Cast(owner.FindComponent(RplComponent));
   if (replication && replication.Id().IsValid())
    identity = "rpl:" + replication.Id().AsString();
  }
  Print(string.Format("[EBM DIAG] action=%1 board=%2 %3", action, identity, detail), LogLevel.NORMAL);
 }
}
