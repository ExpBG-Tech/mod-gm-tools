// Retail UI canvas; no debug-build Shape API, prop component or extra timer.
class EAD_DebugView
{
 static const float NEAR_PLANE = 1;
 static const float SCREEN_MARGIN = 16;
 protected CanvasWidget m_Canvas;
 protected ref array<vector> m_Points = {};
 protected ref array<ref LineDrawCommand> m_Lines = {};
 protected ref array<ref CanvasWidgetCommand> m_Commands = {};
 protected vector m_Origin;
 protected int m_Radius, m_Wake, m_Sleep, m_Width, m_Height;
 void ~EAD_DebugView() { Clear(); }
 protected void Clear()
 {
  if (m_Canvas) m_Canvas.RemoveFromHierarchy();
  m_Canvas = null; m_Commands.Clear(); m_Lines.Clear(); m_Points.Clear();
 }
 void Update(EAD_Zone zone)
 {
  SCR_EditorManagerEntity editor = SCR_EditorManagerEntity.GetInstance();
  if (!zone || (zone.DebugDraw == 0 && zone.DebugLevel == 0) || !GetGame().GetPlayerController() || !editor || editor.IsLimited() || !editor.IsOpened() || editor.GetCurrentMode() != EEditorMode.EDIT)
  {
   Clear(); return;
  }
  WorkspaceWidget workspace = GetGame().GetWorkspace();
  if (!workspace) { Clear(); return; }
  vector origin = zone.GetOrigin();
  if (!m_Canvas || origin != m_Origin || m_Radius != zone.Radius || m_Wake != zone.WakeMargin || m_Sleep != zone.SleepMargin || m_Width != workspace.GetWidth() || m_Height != workspace.GetHeight())
  {
   Clear();
   m_Origin = origin; m_Radius = zone.Radius; m_Wake = zone.WakeMargin; m_Sleep = zone.SleepMargin;
   m_Width = workspace.GetWidth(); m_Height = workspace.GetHeight();
   m_Canvas = CanvasWidget.Cast(workspace.CreateWidgetInWorkspace(WidgetType.CanvasWidgetTypeID, 0, 0,
    workspace.DPIUnscale(m_Width), workspace.DPIUnscale(m_Height), WidgetFlags.VISIBLE | WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS, null, 19));
   if (!m_Canvas) return;
   Ring(zone.GetWorld(), m_Radius, 0xFFFFCC55);
   Ring(zone.GetWorld(), m_Radius + m_Wake, 0xFF66DD88);
   Ring(zone.GetWorld(), m_Radius + m_Sleep, 0xFF6699FF);
  }
  Draw(workspace, zone.GetWorld());
 }
 protected void Ring(BaseWorld world, float radius, int color)
 {
  for (int i = 0; i <= 64; i++)
  {
   float angle = i * Math.PI2 / 64;
   vector point = m_Origin + Vector(Math.Cos(angle) * radius, 0, Math.Sin(angle) * radius);
   point[1] = world.GetSurfaceY(point[0], point[2]) + 0.3;
   m_Points.Insert(point);
   if (i == 64) continue;
   LineDrawCommand line = new LineDrawCommand();
   line.m_Vertices = {0, 0, 0, 0}; line.m_fWidth = 3; line.m_iColor = color;
   m_Lines.Insert(line);
  }
 }
 protected void Draw(WorkspaceWidget workspace, BaseWorld world)
 {
  m_Commands.Clear();
  m_Canvas.SetSizeInUnits(Vector(m_Width, m_Height, 0));
  m_Canvas.SetZoom(1); m_Canvas.SetOffsetPx(vector.Zero);
  SCR_MapEntity mapEntity = SCR_MapEntity.GetMapInstance();
  bool onMap = mapEntity && mapEntity.IsOpen();
  if (onMap && (!mapEntity.GetMapConfig() || mapEntity.GetMapConfig().MapEntityMode != EMapEntityMode.EDITOR))
  {
   m_Canvas.SetDrawCommands(m_Commands); return;
  }
  vector camera[4];
  if (!onMap) world.GetCamera(world.GetCurrentCameraId(), camera);
  for (int i = 0; i < m_Lines.Count(); i++)
  {
   int point = i + i / 64;
   vector a, b;
   vector from = m_Points[point]; vector to = m_Points[point + 1];
   if (onMap)
   {
    int ax, ay, bx, by;
    mapEntity.WorldToScreen(from[0], from[2], ax, ay, true);
    mapEntity.WorldToScreen(to[0], to[2], bx, by, true);
    a = Vector(ax, ay, 1); b = Vector(bx, by, 1);
   }
   else
   {
    // A ring point just in front of the camera projects to enormous coordinates, and
    // such a segment rendered as a screen-filling wedge near the zone. Clip each segment
    // to a near plane before projecting, then to the screen below.
    float depthFrom = vector.Dot(from - camera[3], camera[2]);
    float depthTo = vector.Dot(to - camera[3], camera[2]);
    if (depthFrom < NEAR_PLANE && depthTo < NEAR_PLANE) continue;
    if (depthFrom < NEAR_PLANE) from = from + (to - from) * ((NEAR_PLANE - depthFrom) / (depthTo - depthFrom));
    else if (depthTo < NEAR_PLANE) to = to + (from - to) * ((NEAR_PLANE - depthTo) / (depthFrom - depthTo));
    a = workspace.ProjWorldToScreenNative(from, world);
    b = workspace.ProjWorldToScreenNative(to, world);
    if (a[2] <= 0 || b[2] <= 0) continue;
   }
   if (!ClipToScreen(a, b)) continue;
   LineDrawCommand line = m_Lines[i];
   line.m_Vertices[0] = a[0]; line.m_Vertices[1] = a[1];
   line.m_Vertices[2] = b[0]; line.m_Vertices[3] = b[1];
   m_Commands.Insert(line);
  }
  m_Canvas.SetDrawCommands(m_Commands);
 }
 // Liang-Barsky against the screen plus a small margin: every drawn vertex stays bounded.
 protected bool ClipToScreen(inout vector a, inout vector b)
 {
  if (a[0] != a[0] || a[1] != a[1] || b[0] != b[0] || b[1] != b[1]) return false;
  float dx = b[0] - a[0];
  float dy = b[1] - a[1];
  float t0 = 0;
  float t1 = 1;
  if (!ClipEdge(-dx, a[0] + SCREEN_MARGIN, t0, t1) || !ClipEdge(dx, m_Width + SCREEN_MARGIN - a[0], t0, t1)) return false;
  if (!ClipEdge(-dy, a[1] + SCREEN_MARGIN, t0, t1) || !ClipEdge(dy, m_Height + SCREEN_MARGIN - a[1], t0, t1)) return false;
  vector start = a;
  a = Vector(start[0] + t0 * dx, start[1] + t0 * dy, 0);
  b = Vector(start[0] + t1 * dx, start[1] + t1 * dy, 0);
  return true;
 }
 protected static bool ClipEdge(float p, float q, inout float t0, inout float t1)
 {
  if (p == 0) return q >= 0;
  float r = q / p;
  if (p < 0)
  {
   if (r > t1) return false;
   if (r > t0) t0 = r;
  }
  else
  {
   if (r < t0) return false;
   if (r < t1) t1 = r;
  }
  return true;
 }
}
