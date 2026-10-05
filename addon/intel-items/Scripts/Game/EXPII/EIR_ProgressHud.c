// Client-local download panel, built in code (no layout file, independent of the hint
// system): a full-screen canvas draws the panel and bar, two text widgets carry the
// labels. Only the downloading player's machine creates it. While visible it refreshes
// at 10 Hz, extrapolating between the server's 4 Hz progress updates, hides itself
// under menus and dialogs, and is torn down after the result has been shown.
class EIR_ProgressHud
{
 static const int WIDTH = 460;
 static const int HEIGHT = 96;
 static const int REFRESH_MS = 100;
 static const int RESULT_MS = 4500;
 static const int PANEL_COLOR = 0xD0101010;
 static const int TRACK_COLOR = 0xFF3A3A3A;
 static const int GOLD_COLOR = 0xFFC9A54C;
 static const int FAIL_COLOR = 0xFF9A3B32;
 static const int TEXT_COLOR = 0xFFFFFFFF;
 static const ResourceName FONT = "{EABA4FE9D014CCEF}UI/Fonts/RobotoCondensed/RobotoCondensed_Bold.fnt";
 protected static ref EIR_ProgressHud s_Hud;

 // Rack entity of the shown session: a weak reference, never dereferenced.
 protected IEntity m_Rack;
 protected CanvasWidget m_Canvas;
 protected TextWidget m_Heading;
 protected TextWidget m_Status;
 protected ref array<ref CanvasWidgetCommand> m_Commands = {};
 protected ref PolygonDrawCommand m_Panel;
 protected ref PolygonDrawCommand m_Track;
 protected ref PolygonDrawCommand m_Fill;
 protected bool m_bActive;
 protected int m_iPermille;
 protected int m_iSeconds;
 protected int m_iStamp;
 protected float m_fShown;
 // 0 while downloading; otherwise the tick when the result appeared.
 protected int m_iResultAt;
 protected bool m_bSuccess;
 protected string m_sMessage;

 void EIR_ProgressHud()
 {
  m_Panel = NewRect(PANEL_COLOR);
  m_Track = NewRect(TRACK_COLOR);
  m_Fill = NewRect(GOLD_COLOR);
  m_Commands.Insert(m_Panel);
  m_Commands.Insert(m_Track);
  m_Commands.Insert(m_Fill);
 }

 static void ShowProgress(EIR_RackComponent rack, int permille, int seconds)
 {
  if (!rack || System.IsConsoleApp()) return;
  if (!s_Hud) s_Hud = new EIR_ProgressHud();
  s_Hud.SetProgress(rack, permille, seconds);
 }
 static void ShowResult(EIR_RackComponent rack, string message, bool success)
 {
  if (System.IsConsoleApp()) return;
  if (!s_Hud) s_Hud = new EIR_ProgressHud();
  s_Hud.SetResult(rack, message, success);
 }
 // The session left this player without a result message (yet): hide the bar.
 static void Release(EIR_RackComponent rack)
 {
  if (s_Hud) s_Hud.ReleaseFor(rack);
 }
 // The rack is being deleted locally (GM deletion, streaming out, world end).
 static void Detach(EIR_RackComponent rack)
 {
  if (s_Hud) s_Hud.DetachFor(rack);
 }

 protected static PolygonDrawCommand NewRect(int color)
 {
  PolygonDrawCommand rect = new PolygonDrawCommand();
  rect.m_iColor = color;
  rect.m_Vertices = {0, 0, 0, 0, 0, 0, 0, 0};
  return rect;
 }
 protected static void SetRect(PolygonDrawCommand rect, float x0, float y0, float x1, float y1)
 {
  rect.m_Vertices[0] = x0;
  rect.m_Vertices[1] = y0;
  rect.m_Vertices[2] = x1;
  rect.m_Vertices[3] = y0;
  rect.m_Vertices[4] = x1;
  rect.m_Vertices[5] = y1;
  rect.m_Vertices[6] = x0;
  rect.m_Vertices[7] = y1;
 }
 protected static TextWidget MakeText(WorkspaceWidget workspace, int size, int color)
 {
  TextWidget text = TextWidget.Cast(workspace.CreateWidgetInWorkspace(WidgetType.TextWidgetTypeID, 0, 0, WIDTH, 26,
   WidgetFlags.VISIBLE | WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS | WidgetFlags.NO_LOCALIZATION | WidgetFlags.CENTER, null, 22));
  if (!text) return null;
  text.SetFont(FONT);
  text.SetExactFontSize(size);
  text.SetColorInt(color);
  text.SetOutline(1, 0xEE000000);
  text.SetTextWrapping(false);
  return text;
 }

 void SetProgress(EIR_RackComponent rack, int permille, int seconds)
 {
  // A new session (or the first update after a result) restarts the smoothed bar.
  if (!m_bActive || m_iResultAt != 0 || m_Rack != rack.GetOwner() || permille < m_fShown - 100) m_fShown = permille;
  m_Rack = rack.GetOwner();
  m_iPermille = permille;
  m_iSeconds = seconds;
  m_iStamp = System.GetTickCount();
  m_iResultAt = 0;
  Start();
  Refresh();
 }
 void SetResult(EIR_RackComponent rack, string message, bool success)
 {
  if (rack) m_Rack = rack.GetOwner();
  m_sMessage = message;
  m_bSuccess = success;
  m_iResultAt = System.GetTickCount();
  if (m_iResultAt == 0) m_iResultAt = 1;
  Start();
  Refresh();
 }
 void ReleaseFor(EIR_RackComponent rack)
 {
  if (rack && m_bActive && m_iResultAt == 0 && m_Rack == rack.GetOwner()) Stop();
 }
 void DetachFor(EIR_RackComponent rack)
 {
  if (rack && m_bActive && m_iResultAt == 0 && m_Rack == rack.GetOwner()) SetResult(null, EIR_RackComponent.ResultText(EIR_Result.INTERRUPTED), false);
 }
 protected void Start()
 {
  if (m_bActive) return;
  m_bActive = true;
  GetGame().GetCallqueue().CallLater(Refresh, REFRESH_MS, true);
 }
 protected void Stop()
 {
  m_bActive = false;
  m_iResultAt = 0;
  if (GetGame()) GetGame().GetCallqueue().Remove(Refresh);
  RemoveWidgets();
 }
 protected void RemoveWidgets()
 {
  if (m_Canvas) m_Canvas.RemoveFromHierarchy();
  if (m_Heading) m_Heading.RemoveFromHierarchy();
  if (m_Status) m_Status.RemoveFromHierarchy();
  m_Canvas = null;
  m_Heading = null;
  m_Status = null;
 }
 protected bool EnsureWidgets(WorkspaceWidget workspace)
 {
  if (m_Canvas && m_Heading && m_Status) return true;
  // Recreate all together; never overwrite a reference to a live widget.
  RemoveWidgets();
  float width = workspace.DPIUnscale(workspace.GetWidth());
  float height = workspace.DPIUnscale(workspace.GetHeight());
  m_Canvas = CanvasWidget.Cast(workspace.CreateWidgetInWorkspace(WidgetType.CanvasWidgetTypeID, 0, 0, width, height,
   WidgetFlags.VISIBLE | WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS, null, 21));
  m_Heading = MakeText(workspace, 20, GOLD_COLOR);
  m_Status = MakeText(workspace, 15, TEXT_COLOR);
  if (m_Canvas && m_Heading && m_Status) return true;
  RemoveWidgets();
  return false;
 }
 protected void Refresh()
 {
  if (!m_bActive) return;
  WorkspaceWidget workspace = GetGame().GetWorkspace();
  int now = System.GetTickCount();
  if (!workspace || (m_iResultAt != 0 && now - m_iResultAt >= RESULT_MS) || !EnsureWidgets(workspace))
  {
   Stop();
   return;
  }
  MenuManager menus = GetGame().GetMenuManager();
  bool covered = menus && (menus.IsAnyMenuOpen() || menus.IsAnyDialogOpen());
  m_Canvas.SetVisible(!covered);
  m_Heading.SetVisible(!covered);
  m_Status.SetVisible(!covered);
  if (covered) return;

  float width = workspace.DPIUnscale(workspace.GetWidth());
  float height = workspace.DPIUnscale(workspace.GetHeight());
  if (width <= 0 || height <= 0) return;
  float left = (width - WIDTH) * 0.5;
  float top = height * 0.72;
  FrameSlot.SetPos(m_Canvas, 0, 0);
  FrameSlot.SetSize(m_Canvas, width, height);
  FrameSlot.SetPos(m_Heading, left + 16, top + 10);
  FrameSlot.SetSize(m_Heading, WIDTH - 32, 26);
  FrameSlot.SetPos(m_Status, left + 16, top + 64);
  FrameSlot.SetSize(m_Status, WIDTH - 32, 24);

  int fill = GOLD_COLOR;
  if (m_iResultAt == 0)
  {
   // Server progress plus local time since that update, never moving backwards.
   float estimate = m_iPermille;
   if (m_iSeconds > 0)
   {
    float sinceUpdate = now - m_iStamp;
    float seconds = m_iSeconds;
    estimate = m_iPermille + sinceUpdate / seconds;
   }
   m_fShown = Math.Min(Math.Max(m_fShown, estimate), 999);
   int percent = Math.Floor(m_fShown / 10);
   int remaining = Math.Ceil(m_iSeconds * (1000 - m_fShown) / 1000);
   m_Heading.SetText("DOWNLOADING INTEL");
   m_Status.SetText(percent.ToString() + "%   |   about " + remaining.ToString() + " s left   |   stay within 3 m");
  }
  else
  {
   if (m_bSuccess)
   {
    m_fShown = 1000;
    m_Heading.SetText("INTEL DOWNLOADED");
   }
   else
   {
    fill = FAIL_COLOR;
    m_Heading.SetText("DOWNLOAD STOPPED");
   }
   m_Status.SetText(m_sMessage);
  }

  // Draw commands are in canvas pixels; the canvas covers the screen like the other
  // EXPBG overlays, so reference units scale by the measured pixel width.
  float pixelWidth;
  float pixelHeight;
  m_Canvas.GetScreenSize(pixelWidth, pixelHeight);
  if (pixelWidth <= 0 || pixelHeight <= 0) return;
  m_Canvas.SetSizeInUnits(Vector(pixelWidth, pixelHeight, 0));
  m_Canvas.SetZoom(1);
  m_Canvas.SetOffsetPx(vector.Zero);
  float scale = pixelWidth / width;
  float x0 = left * scale;
  float y0 = top * scale;
  SetRect(m_Panel, x0, y0, x0 + WIDTH * scale, y0 + HEIGHT * scale);
  float barLeft = (left + 16) * scale;
  float barRight = (left + WIDTH - 16) * scale;
  float barTop = (top + 44) * scale;
  float barBottom = (top + 56) * scale;
  SetRect(m_Track, barLeft, barTop, barRight, barBottom);
  SetRect(m_Fill, barLeft, barTop, barLeft + (barRight - barLeft) * m_fShown / 1000, barBottom);
  m_Fill.m_iColor = fill;
  m_Canvas.SetDrawCommands(m_Commands);
 }
}
