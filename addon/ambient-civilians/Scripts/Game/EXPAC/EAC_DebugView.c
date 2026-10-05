// Release UI only. The local editor owns one view; the server supplies bounded snapshots.
enum EAC_DebugKind
{
 HOME = 0,
 ACTIVE = 1,
 CACHED = 2,
 PENDING = 3,
 REJECTED = 4,
 NO_GO = 5
}

// Retail UI canvas, not debug-build Shape drawing. Terrain is sampled once per
// snapshot; only bounded camera/map projection runs at 10 Hz on the viewing GM.
class EAC_DebugRanges
{
 static const int SEGMENTS = 48;
 static const int MAX_PLAYERS = 1;
 protected CanvasWidget m_Canvas;
 protected ref array<vector> m_Points = {};
 protected ref array<ref LineDrawCommand> m_Lines = {};
 protected ref array<ref CanvasWidgetCommand> m_Commands = {};
 protected float m_NextDraw;

 void ~EAC_DebugRanges()
 {
  if (m_Canvas) m_Canvas.RemoveFromHierarchy();
 }

 void Build(array<vector> centers, vector radii)
 {
  WorkspaceWidget workspace = GetGame().GetWorkspace();
  BaseWorld world = GetGame().GetWorld();
  if (!workspace || !world || !centers || centers.IsEmpty()) return;
  float width = workspace.DPIUnscale(workspace.GetWidth());
  float height = workspace.DPIUnscale(workspace.GetHeight());
  m_Canvas = CanvasWidget.Cast(workspace.CreateWidgetInWorkspace(WidgetType.CanvasWidgetTypeID, 0, 0, width, height,
   WidgetFlags.VISIBLE | WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS, null, 19));
  if (!m_Canvas) return;
  m_Canvas.SetDrawCommands(m_Commands);
  vector camera[4];
  world.GetCamera(world.GetCurrentCameraId(), camera);
  array<int> chosen = {};
  // The cap limits drawing only; it never limits actual player admission.
  for (int player = 0; player < Math.Min(MAX_PLAYERS, centers.Count()); player++)
  {
   int nearest = -1;
   float distance;
   for (int candidate = 0; candidate < centers.Count(); candidate++)
   {
    if (chosen.Contains(candidate)) continue;
    float next = vector.DistanceSq(camera[3], centers[candidate]);
    if (nearest < 0 || next < distance) { nearest = candidate; distance = next; }
   }
   if (nearest < 0) break;
   chosen.Insert(nearest);
   for (int kind = 0; kind < 3; kind++)
   {
    float radius = Math.Clamp(radii[kind], 0, 6000);
    if (radius <= 0) continue;
    int color = 0xFF66EE88;
    if (kind == 1) color = 0xFFFF6666;
    if (kind == 2) color = 0xFF55DDDD;
    for (int point = 0; point <= SEGMENTS; point++)
    {
     float angle = point * Math.PI2 / SEGMENTS;
     vector position = centers[nearest] + Vector(Math.Cos(angle) * radius, 0, Math.Sin(angle) * radius);
     position[1] = world.GetSurfaceY(position[0], position[2]) + 0.25;
     m_Points.Insert(position);
     if (point == SEGMENTS) continue;
     LineDrawCommand line = new LineDrawCommand();
     line.m_Vertices = {0, 0, 0, 0};
     line.m_fWidth = 2; line.m_iColor = color;
     m_Lines.Insert(line);
    }
   }
  }
 }

 void Draw(float now)
 {
  if (!m_Canvas || now < m_NextDraw) return;
  m_NextDraw = now + 0.1;
  m_Commands.Clear();
  WorkspaceWidget workspace = GetGame().GetWorkspace();
  float width = workspace.GetWidth();
  float height = workspace.GetHeight();
  // FrameSlot uses DPI layout units; canvas virtual units are a separate space
  // (native SCR_MapEntity sets those to map metres). Our projections use pixels.
  // Size from the workspace, never feed the canvas's rendered size back to layout.
  FrameSlot.SetSize(m_Canvas, workspace.DPIUnscale(width), workspace.DPIUnscale(height));
  m_Canvas.SetSizeInUnits(Vector(width, height, 0));
  m_Canvas.SetZoom(1); m_Canvas.SetOffsetPx(vector.Zero);
  SCR_MapEntity mapEntity = SCR_MapEntity.GetMapInstance();
  bool onMap = mapEntity && mapEntity.IsOpen();
  if (onMap && (!mapEntity.GetMapConfig() || mapEntity.GetMapConfig().MapEntityMode != EMapEntityMode.EDITOR))
  {
   m_Canvas.SetDrawCommands(m_Commands);
   return;
  }
  for (int i = 0; i < m_Lines.Count(); i++)
  {
   int point = i + i / SEGMENTS;
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
    a = GetGame().GetWorkspace().ProjWorldToScreenNative(m_Points[point], GetGame().GetWorld());
    b = GetGame().GetWorkspace().ProjWorldToScreenNative(m_Points[point + 1], GetGame().GetWorld());
    if (a[2] <= 0 || b[2] <= 0) continue;
   }
   if ((a[0] < 0 && b[0] < 0) || (a[0] > width && b[0] > width) || (a[1] < 0 && b[1] < 0) || (a[1] > height && b[1] > height)) continue;
   LineDrawCommand line = m_Lines[i];
   line.m_Vertices[0] = a[0]; line.m_Vertices[1] = a[1];
   line.m_Vertices[2] = b[0]; line.m_Vertices[3] = b[1];
   m_Commands.Insert(line);
  }
  m_Canvas.SetDrawCommands(m_Commands);
 }
}

class EAC_DebugView
{
 static const int MAX_POINTS = 64;
 static const float SNAPSHOT_TTL = 3;
 protected static const ResourceName FONT = "{EABA4FE9D014CCEF}UI/Fonts/RobotoCondensed/RobotoCondensed_Bold.fnt";
 // Screen layout, in DPI-unscaled workspace units. The live run on 2026-10-06 had
 // the old full-width legend rows (height - 94 and height - 60) and the top-left
 // summary overlapping the Unit Caching monitor (top-left), the GM scenario panel
 // and the old Ambient Sounds legend (bottom-left). Regions now in use by others:
 // top-left (Unit Caching), bottom-left (GM panel), bottom-right corner (GM entity
 // panel) and the right edge between 25 % and 50 % of the height (Ambient Sounds
 // legend). The vanilla edit mode (UI/layouts/Editor/Modes/Mode_Edit.layout)
 // draws a full-width 42-unit toolbar and, centred under it, the 96 x 96 compass
 // dial (CompassSlot, top padding 42, so 42-138); its N/E/S/W labels stay inside
 // the dial. The 0.1.4 client re-test saw the summary, then at top centre from 64
 // down, drawn over that dial (issue #21). This view uses two free regions:
 //  - the summary panel centred just below the dial (SUMMARY_TOP), so it clears
 //    the toolbar and the compass at any aspect ratio. Beside the dial is not
 //    free on 16:9: its left is the Unit Caching monitor's region again;
 //  - the legend on the right edge directly above the GM entity panel, its
 //    bottom at 72 % of the height and its top never above 50 %.
 static const int PANEL_WIDTH = 340;
 static const int PANEL_MARGIN = 24;
 // Compass dial bottom (42 + 96) plus a margin.
 static const int SUMMARY_TOP = 152;
 static const int PANEL_HEIGHT = 180;
 static const float LEGEND_BOTTOM_SHARE = 0.72;
 static const float LEGEND_TOP_MIN_SHARE = 0.5;
 static const int LEGEND_LINE = 24;
 static const int LEGEND_HEADER = 44;
 // One row per drawn marker kind: HOME, ACTIVE, PENDING, REJECTED, NO_GO
 // (CACHED is never drawn).
 static const int LEGEND_ROWS = 5;
 // Workspace widgets outlive the world that created them. Every live view is
 // listed here (weak entries, removed by the destructor) so world cleanup can
 // remove its widgets even when the owning editor entity is torn down late.
 protected static ref array<EAC_DebugView> s_Live = {};
 protected float m_NextRequest;
 protected float m_Expires;
 protected TextWidget m_Summary;
 protected ref array<TextWidget> m_Legend = {};
 protected ref array<TextWidget> m_Markers = {};
 protected ref array<vector> m_Positions = {};
 protected ref EAC_DebugRanges m_Ranges;
 protected EAC_AmbientModule m_Module;
 protected bool m_Drawing;
 protected ref array<vector> m_ScreenPoints = {};

 static bool IsDrawingEnabled(int level, int draw)
 {
  return draw == 1;
 }

 static bool HasPermission(bool admin, bool limited, bool editMode)
 {
  return admin || (!limited && editMode);
 }

 static bool CanViewLocal(SCR_EditorManagerEntity editor)
 {
  if (!GetGame() || !GetGame().GetPlayerController() || !GetGame().GetPlayerManager()) return false;
  if (!editor || editor != SCR_EditorManagerEntity.GetInstance()) return false;
  if (!editor.IsOpened() || editor.GetCurrentMode() != EEditorMode.EDIT) return false;
  // GetEditorModes reads the authority's access map; the owner receives actual
  // mode entities through CreateEditorModeOwner/AddMode instead of that map.
  return HasPermission(SCR_Global.IsAdmin(), editor.IsLimited(), editor.FindModeEntity(EEditorMode.EDIT) != null);
 }

 static int MarkerColor(int kind)
 {
  switch (kind)
  {
   case EAC_DebugKind.HOME: return 0xFF55DDDD;
   case EAC_DebugKind.ACTIVE: return 0xFF66EE88;
   case EAC_DebugKind.CACHED: return 0xFF6699FF;
   case EAC_DebugKind.PENDING: return 0xFFFFDD55;
   case EAC_DebugKind.REJECTED: return 0xFFFF6666;
   case EAC_DebugKind.NO_GO: return 0xFFFF9900;
  }
  return 0xFFFFFFFF;
 }

 static string MarkerLabel(int kind)
 {
  switch (kind)
  {
   case EAC_DebugKind.HOME: return "H home";
   case EAC_DebugKind.ACTIVE: return "A active";
   case EAC_DebugKind.CACHED: return "C cached";
   case EAC_DebugKind.PENDING: return "P pending/recovery";
   case EAC_DebugKind.REJECTED: return "X rejected";
   case EAC_DebugKind.NO_GO: return "Z no-go boundary";
  }
  return "?";
 }

 protected TextWidget MakeText(int x, int y, int width, int height, int size, int color)
 {
  WorkspaceWidget workspace = GetGame().GetWorkspace();
  if (!workspace) return null;
  TextWidget widget = TextWidget.Cast(workspace.CreateWidgetInWorkspace(WidgetType.TextWidgetTypeID, x, y, width, height,
   WidgetFlags.VISIBLE | WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS | WidgetFlags.NO_LOCALIZATION, null, 20));
  if (!widget) return null;
  widget.SetFont(FONT);
  widget.SetExactFontSize(size);
  widget.SetColorInt(color);
  widget.SetOutline(2, 0xEE000000);
  widget.SetTextWrapping(true);
  return widget;
 }

 void Clear()
 {
  if (m_Summary) m_Summary.RemoveFromHierarchy();
  m_Summary = null;
  foreach (TextWidget legend : m_Legend) if (legend) legend.RemoveFromHierarchy();
  foreach (TextWidget marker : m_Markers) if (marker) marker.RemoveFromHierarchy();
  m_Legend.Clear();
  m_Markers.Clear();
  m_Positions.Clear();
  m_Ranges = null;
  m_Module = null;
  m_Drawing = false;
  m_ScreenPoints.Clear();
  m_Expires = 0;
 }

 void EAC_DebugView() { s_Live.Insert(this); }
 void ~EAC_DebugView() { Clear(); s_Live.RemoveItem(this); }

 // Disconnect, mission end or world change: no widget of this view may survive
 // into the next world (the Ambient Sounds legend was seen doing exactly that).
 static void ShutdownForWorldCleanup()
 {
  foreach (EAC_DebugView view : s_Live) if (view) view.Clear();
 }

 // Top of the legend block on the right edge: its bottom sits at 72 % of the
 // height, just above the GM entity panel, and it never rises into the Ambient
 // Sounds legend's band (right edge, 25-50 %), even on a short screen.
 static float LegendTop(float height)
 {
  float top = height * LEGEND_BOTTOM_SHARE - LEGEND_HEADER - LEGEND_LINE * LEGEND_ROWS;
  return Math.Max(top, height * LEGEND_TOP_MIN_SHARE + 4);
 }

 static bool EditingAttributes()
 {
  return GetGame() && GetGame().GetMenuManager() && GetGame().GetMenuManager().IsAnyDialogOpen();
 }

 void Receive(EAC_AmbientModule module, int level, bool draw, string summary, array<vector> positions, array<int> kinds, array<vector> centers, vector radii)
 {
  Clear(); // Each snapshot replaces all old points, including empty/off responses.
  if (!module) return;
  if (EditingAttributes()) return; // Keep the controller's own settings readable.
  // A late snapshot must not restore drawing after the replicated switch is off.
  draw = draw && IsDrawingEnabled(module.DebugLevel, module.DebugDraw);
  if (module.DebugLevel <= 0) level = 0;
  if (level <= 0 && !draw) return;
  if (!positions || !kinds || positions.Count() != kinds.Count() || positions.Count() > MAX_POINTS) return;
  if (summary.Length() > 4096) return;
  if (!centers || centers.Count() > MAX_POINTS) return;
  m_Module = module;
  m_Drawing = draw;
  m_Expires = GetGame().GetWorld().GetWorldTime() * 0.001 + SNAPSHOT_TTL;
  WorkspaceWidget workspace = GetGame().GetWorkspace();
  if (!workspace) return;
  float width = workspace.DPIUnscale(workspace.GetWidth());
  float height = workspace.DPIUnscale(workspace.GetHeight());
  // See PANEL_WIDTH: summary centred below the GM compass, legend on the right
  // edge above the GM entity panel.
  float columnWidth = Math.Min(PANEL_WIDTH, width - 2 * PANEL_MARGIN);
  float columnLeft = width - PANEL_MARGIN - columnWidth;
  float legendTop = LegendTop(height);
  if (level > 0)
  {
   // A small panel of numbers, not a log: see EAC_AmbientModule.BuildOverlayStats.
   m_Summary = MakeText((width - columnWidth) * 0.5, SUMMARY_TOP, columnWidth, PANEL_HEIGHT, 15, 0xFFFFFFFF);
   if (m_Summary) m_Summary.SetText("EXPBG Ambient Civilians\n" + summary);
  }
  if (!draw) return;
  m_Ranges = new EAC_DebugRanges();
  m_Ranges.Build(centers, radii);
  TextWidget rangeLegend = MakeText(columnLeft, legendTop, columnWidth, LEGEND_HEADER, 16, 0xFFFFFFFF);
  if (rangeLegend)
  {
   if (!centers.IsEmpty()) rangeLegend.SetText(string.Format("Module population boundary: %1 m. Players activate civilians only inside this area.", radii[0]));
   m_Legend.Insert(rangeLegend);
  }
  int row;
  for (int k = 0; k <= EAC_DebugKind.NO_GO; k++)
  {
   if (k == EAC_DebugKind.CACHED) continue;
   TextWidget legend = MakeText(columnLeft, legendTop + LEGEND_HEADER + row * LEGEND_LINE, columnWidth, LEGEND_LINE, 18, MarkerColor(k));
   row++;
   if (legend) legend.SetText(MarkerLabel(k));
   m_Legend.Insert(legend);
  }
  for (int i = 0; i < positions.Count(); i++)
  {
   if (kinds[i] == EAC_DebugKind.CACHED || kinds[i] < EAC_DebugKind.HOME || kinds[i] > EAC_DebugKind.NO_GO) continue;
   TextWidget marker = MakeText(0, 0, 170, 26, 16, MarkerColor(kinds[i]));
   if (!marker) continue;
   marker.SetText("+ " + MarkerLabel(kinds[i]));
   marker.SetVisible(false); // First projection decides visibility, never flash at the screen origin.
   m_Markers.Insert(marker);
   m_Positions.Insert(positions[i] + "0 1 0");
  }
 }

 void Tick(SCR_EditorManagerEntity editor)
 {
  if (!GetGame() || !GetGame().GetWorld()) { Clear(); return; }
  SCR_PlayerController player = SCR_PlayerController.Cast(GetGame().GetPlayerController());
  if (!CanViewLocal(editor) || !player || EditingAttributes()) { Clear(); return; }
  // Check the real replicated controller, not only the last 1 Hz snapshot.
  if (m_Expires > 0 && (!m_Module || (m_Drawing && m_Module.DebugDraw != 1) || (!m_Drawing && m_Module.DebugLevel <= 0))) Clear();
  float now = GetGame().GetWorld().GetWorldTime() * 0.001;
  if (now >= m_NextRequest)
  {
   m_NextRequest = now + 1;
   player.EAC_RequestDebug();
  }
  if (m_Expires <= now) { Clear(); return; }
  WorkspaceWidget workspace = GetGame().GetWorkspace();
  if (!workspace) { Clear(); return; }
  float width = workspace.DPIUnscale(workspace.GetWidth());
  float height = workspace.DPIUnscale(workspace.GetHeight());
  if (m_Ranges) m_Ranges.Draw(now);
  m_ScreenPoints.Clear();
  // Only camera projection runs per frame; positions come from the shared 1 Hz snapshot.
  for (int i = 0; i < m_Markers.Count(); i++)
  {
   vector screen = workspace.ProjWorldToScreen(m_Positions[i], GetGame().GetWorld());
   bool visible = screen[2] > 0 && screen[0] >= 0 && screen[1] >= 0 && screen[0] < width && screen[1] < height;
   if (m_ScreenPoints.Count() >= 16) visible = false;
   foreach (vector used : m_ScreenPoints)
    if (Math.AbsFloat(screen[0] - used[0]) < 170 && Math.AbsFloat(screen[1] - used[1]) < 28) visible = false;
   m_Markers[i].SetVisible(visible);
   if (visible) { FrameSlot.SetPos(m_Markers[i], screen[0], screen[1]); m_ScreenPoints.Insert(screen); }
  }
 }
}

// The editor entity's own teardown normally clears its view first; this covers a
// world that ends (disconnect, mission end) before that destructor runs.
modded class ArmaReforgerScripted
{
 override protected void OnBeforeWorldCleanup()
 {
  EAC_DebugView.ShutdownForWorldCleanup();
  super.OnBeforeWorldCleanup();
 }
}

// Existing local editor callback: no extra entity, world scan or resident timer.
modded class SCR_EditorManagerEntity
{
 protected ref EAC_DebugView m_EAC_DebugView;
 protected bool m_EAC_DebugClosing;

 // Native IsOpened() can remain true while the asynchronous close is in flight.
 // Clear at its lifecycle boundary and reject late snapshots until a new open.
 override protected void StartEvents(EEditorEventOperation type = EEditorEventOperation.NONE)
 {
  if (type == EEditorEventOperation.OPEN) m_EAC_DebugClosing = false;
  if (type == EEditorEventOperation.REQUEST_CLOSE || type == EEditorEventOperation.CLOSE || type == EEditorEventOperation.DELETE)
   m_EAC_DebugClosing = true;
  if (m_EAC_DebugClosing || type == EEditorEventOperation.MODE_CHANGE)
   if (m_EAC_DebugView) m_EAC_DebugView.Clear();
  super.StartEvents(type);
 }

 override void EOnFrame(IEntity owner, float timeSlice)
 {
  super.EOnFrame(owner, timeSlice);
  if (SCR_EditorManagerEntity.GetInstance() != this || m_EAC_DebugClosing)
  {
   if (m_EAC_DebugView) m_EAC_DebugView.Clear();
   return;
  }
  if (!m_EAC_DebugView && EAC_DebugView.CanViewLocal(this)) m_EAC_DebugView = new EAC_DebugView();
  if (m_EAC_DebugView) m_EAC_DebugView.Tick(this);
 }

 void EAC_ReceiveDebug(EAC_AmbientModule module, int level, bool draw, string summary, array<vector> positions, array<int> kinds, array<vector> centers, vector radii)
 {
  if (m_EAC_DebugClosing || !EAC_DebugView.CanViewLocal(this))
  {
   if (m_EAC_DebugView) m_EAC_DebugView.Clear();
   return;
  }
  if (!m_EAC_DebugView) m_EAC_DebugView = new EAC_DebugView();
  m_EAC_DebugView.Receive(module, level, draw, summary, positions, kinds, centers, radii);
 }

 void ~SCR_EditorManagerEntity()
 {
  if (m_EAC_DebugView) m_EAC_DebugView.Clear();
 }
}

// RPC authority follows the requesting controller's owned native node.
modded class SCR_PlayerController
{
 protected float m_EAC_NextDebugRequest;
 protected float m_EAC_NextVoiceReport;

 void EAC_RequestDebug() { Rpc(RPC_EAC_RequestDebug); }

 // Run 68: the client called a server RPC on the ambient module and the server
 // never saw it (voice client_refused=1, server reports=0) - the module is
 // server-owned, and a client may only send server RPCs through an item it
 // owns. This controller is that item, exactly like the debug request above.
 void EAC_ReportVoiceRefused(RplId target) { Rpc(RPC_EAC_VoiceRefused, target); }

 [RplRpc(RplChannel.Reliable, RplRcver.Server)]
 protected void RPC_EAC_VoiceRefused(RplId target)
 {
  if (!Replication.IsServer() || !GetGame() || !GetGame().GetWorld()) return;
  float now = GetGame().GetWorld().GetWorldTime() * 0.001;
  if (now < m_EAC_NextVoiceReport) return;
  m_EAC_NextVoiceReport = now + 1;
  // Audit S7. This RPC used to accept ANY RplId from any client and hand the
  // resolved entity to BlockSpeaker, which retires that prefab for every resident
  // on the server for the rest of the mission. Two gates now stand in front of
  // it: this mod must actually be running, and the entity must be one this server
  // recently chose as a speaker. A report naming anything else is counted and
  // dropped, so a client can only ever refuse a speaker it was genuinely asked to
  // voice - and never retire a prefab this mod does not own.
  if (!EAC_AmbientModule.GetActive()) return;
  RplComponent rpl = RplComponent.Cast(Replication.FindItem(target));
  if (!rpl) return;
  IEntity actor = rpl.GetEntity();
  if (!actor) return;
  if (!EAC_CivilianVoice.WasBroadcast(actor)) { EAC_CivilianVoice.CountRefusedReport(); return; }
  EAC_CivilianVoice.BlockSpeaker(actor);
 }

 [RplRpc(RplChannel.Reliable, RplRcver.Server)]
 protected void RPC_EAC_RequestDebug()
 {
  if (!Replication.IsServer() || !GetGame() || !GetGame().GetWorld()) return;
  float now = GetGame().GetWorld().GetWorldTime() * 0.001;
  if (now < m_EAC_NextDebugRequest) return;
  m_EAC_NextDebugRequest = now + 1;
  int playerId = GetPlayerId();
  SCR_EditorManagerCore core = SCR_EditorManagerCore.Cast(SCR_EditorManagerCore.GetInstance(SCR_EditorManagerCore));
  SCR_EditorManagerEntity editor;
  if (core) editor = core.GetEditorManager(playerId);
  bool authorized;
  if (playerId > 0 && editor && editor.GetPlayerID() == playerId)
   authorized = EAC_DebugView.HasPermission(SCR_Global.IsAdmin(playerId), editor.IsLimited(), (editor.GetEditorModes() & EEditorMode.EDIT) != 0);
  array<vector> positions = {};
  array<int> kinds = {};
  array<vector> centers = {};
  vector radii;
  EAC_AmbientModule module = EAC_AmbientModule.GetActive();
  RplId moduleId = RplId.Invalid();
  int level;
  bool draw;
  string summary;
  if (authorized && module)
  {
   RplComponent moduleRpl = RplComponent.Cast(module.FindComponent(RplComponent));
   if (moduleRpl) moduleId = moduleRpl.Id();
   level = Math.Clamp(module.DebugLevel, 0, 3);
   draw = EAC_DebugView.IsDrawingEnabled(level, module.DebugDraw);
   if (level > 0) summary = module.BuildOverlayStats();
   if (summary.Length() > 4096) summary = summary.Substring(0, 4096);
   if (draw)
   {
    module.GetDebugDrawData(positions, kinds);
    module.GetDebugRangeCenters(centers);
    radii = Vector(module.SettlementRadius, 0, 0);
   }
   if (positions.Count() != kinds.Count()) { positions.Clear(); kinds.Clear(); }
   while (positions.Count() > EAC_DebugView.MAX_POINTS)
   {
    positions.Remove(positions.Count() - 1);
    kinds.Remove(kinds.Count() - 1);
   }
  }
  Rpc(RPC_EAC_DebugSnapshot, moduleId, level, draw, summary, positions, kinds, centers, radii);
 }

 [RplRpc(RplChannel.Reliable, RplRcver.Owner)]
 protected void RPC_EAC_DebugSnapshot(RplId moduleId, int level, bool draw, string summary, array<vector> positions, array<int> kinds, array<vector> centers, vector radii)
 {
  if (GetGame().GetPlayerController() != this) return;
  SCR_EditorManagerEntity editor = SCR_EditorManagerEntity.GetInstance();
  RplComponent moduleRpl = RplComponent.Cast(Replication.FindItem(moduleId));
  EAC_AmbientModule module;
  if (moduleRpl) module = EAC_AmbientModule.Cast(moduleRpl.GetEntity());
  if (editor) editor.EAC_ReceiveDebug(module, level, draw, summary, positions, kinds, centers, radii);
 }
}
