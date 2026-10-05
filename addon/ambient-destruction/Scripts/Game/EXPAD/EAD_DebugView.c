// Retail UI canvas; no debug-build Shape API, prop component or extra timer.
class EAD_DebugView
{
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
  for (int i = 0; i < m_Lines.Count(); i++)
  {
   int point = i + i / 64;
   vector a, b;
   if (onMap)
   {
    int ax, ay, bx, by;
    vector from = m_Points[point]; vector to = m_Points[point + 1];
    mapEntity.WorldToScreen(from[0], from[2], ax, ay, true);
    mapEntity.WorldToScreen(to[0], to[2], bx, by, true);
    a = Vector(ax, ay, 1); b = Vector(bx, by, 1);
   }
   else
   {
    a = workspace.ProjWorldToScreenNative(m_Points[point], world);
    b = workspace.ProjWorldToScreenNative(m_Points[point + 1], world);
    if (a[2] <= 0 || b[2] <= 0) continue;
   }
   if ((a[0] < 0 && b[0] < 0) || (a[0] > m_Width && b[0] > m_Width) || (a[1] < 0 && b[1] < 0) || (a[1] > m_Height && b[1] > m_Height)) continue;
   LineDrawCommand line = m_Lines[i];
   line.m_Vertices[0] = a[0]; line.m_Vertices[1] = a[1];
   line.m_Vertices[2] = b[0]; line.m_Vertices[3] = b[1];
   m_Commands.Insert(line);
  }
  m_Canvas.SetDrawCommands(m_Commands);
 }
}
