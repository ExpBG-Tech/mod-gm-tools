// Informational GM icons for absent Full groups. No world entities or AI agents.
class EBG_FullCacheMarker
{
 vector Position;
 int Survivors;
 ref LineDrawCommand Icon = new LineDrawCommand();
 ref TextDrawCommand Label = new TextDrawCommand();

 void EBG_FullCacheMarker()
 {
  Icon.m_Vertices = {};
  Icon.m_Vertices.Resize(8);
  Icon.m_fWidth = 2;
  Icon.m_bShouldEnclose = true;
  Icon.m_iColor = 0xff8de4ff;
  Label.m_iColor = 0xff8de4ff;
  Label.m_fSize = 15;
  Label.m_iFontPropertiesId = 0;
 }
 void Place(float x, float y)
 {
  Icon.m_Vertices[0] = x; Icon.m_Vertices[1] = y - 7;
  Icon.m_Vertices[2] = x + 7; Icon.m_Vertices[3] = y;
  Icon.m_Vertices[4] = x; Icon.m_Vertices[5] = y + 7;
  Icon.m_Vertices[6] = x - 7; Icon.m_Vertices[7] = y;
  Label.m_Position = Vector(x + 11, y - 8, 0);
 }
}

class EBG_FullCacheMarkers
{
 // ponytail: bounded display, not a management limit; report overflow explicitly.
 static const int MAX_MARKERS = 256;
 protected static ref array<ref EBG_FullCacheMarker> s_Markers = {};
 protected static ref array<ref CanvasWidgetCommand> s_Commands = {};
 protected static CanvasWidget s_Canvas;
 protected static Widget s_Parent;
 protected static bool s_Truncated;
 protected static float s_Received = -1;
 protected static float s_RequestElapsed = 1;
 protected static float s_DrawElapsed;

 static bool Eligible(EBG_CacheGroup record)
 {
  return record && record.Zone && record.Zone.CachedGroupMarkers && record.Alive > 0 && record.CacheState() == EBG_CacheRecordState.FULL_CACHED;
 }
 static void Collect(out array<vector> positions, out array<int> survivors, out bool truncated)
 {
  positions = {}; survivors = {}; truncated = false;
  if (!EBG_CacheManager.Instance) return;
  foreach (EBG_CacheGroup record : EBG_CacheManager.Instance.Records)
  {
   if (!Eligible(record)) continue;
   if (positions.Count() == MAX_MARKERS) { truncated = true; break; }
   positions.Insert(record.Anchor);
   survivors.Insert(record.Alive);
  }
 }
 protected static void ClearCanvas()
 {
  if (s_Canvas) s_Canvas.RemoveFromHierarchy();
  s_Canvas = null; s_Parent = null; s_Commands.Clear();
 }
 static void Reset()
 {
  ClearCanvas(); s_Markers.Clear(); s_Truncated = false;
  s_Received = -1; s_RequestElapsed = 1; s_DrawElapsed = 0;
 }
 static void Receive(array<vector> positions, array<int> survivors, bool truncated)
 {
  if (!EBG_CacheVisuals.Authorized() || !positions || !survivors || positions.Count() != survivors.Count() || positions.Count() > MAX_MARKERS)
  {
   Reset(); return;
  }
  // Reuse draw objects across unchanged snapshots and camera movement.
  for (int i = 0; i < positions.Count(); i++)
  {
   if (i == s_Markers.Count()) s_Markers.Insert(new EBG_FullCacheMarker());
   EBG_FullCacheMarker marker = s_Markers[i];
   marker.Position = positions[i]; marker.Survivors = survivors[i];
   marker.Label.m_sText = string.Format("Full cached | %1 AI", survivors[i]);
  }
  while (s_Markers.Count() > positions.Count()) s_Markers.Remove(s_Markers.Count() - 1);
  s_Truncated = truncated;
  s_Received = GetGame().GetWorld().GetWorldTime() * 0.001;
  s_DrawElapsed = 1;
 }
 static void Update(SCR_EditorManagerEntity editor, float timeSlice)
 {
  if (!editor || editor != SCR_EditorManagerEntity.GetInstance()) return;
  if (!EBG_CacheVisuals.Authorized() || EBG_CacheVisuals.OverlaysDisabled()) { Reset(); return; }
  s_RequestElapsed += timeSlice;
  if (s_RequestElapsed >= 1)
  {
   s_RequestElapsed = 0;
   editor.EBG_RequestFullCacheMarkers(EBG_CacheVisuals.AccessEpoch);
  }
  float now = GetGame().GetWorld().GetWorldTime() * 0.001;
  if (s_Markers.IsEmpty() || s_Received < 0 || now - s_Received > 3) { ClearCanvas(); return; }
  SCR_MenuEditorComponent menu = SCR_MenuEditorComponent.Cast(SCR_MenuEditorComponent.GetInstance(SCR_MenuEditorComponent, false));
  if (!menu || !menu.GetMenu()) { ClearCanvas(); return; }
  Widget parent = menu.GetMenu().GetRootWidget();
  if (!parent) { ClearCanvas(); return; }
  if (s_Parent != parent) ClearCanvas();
  if (!s_Canvas)
  {
   s_Canvas = CanvasWidget.Cast(GetGame().GetWorkspace().CreateWidgets("{20AD8671A9C08CD9}UI/layouts/EXPBG/CacheMapOverlay.layout", parent));
   if (!s_Canvas) return;
   s_Parent = parent;
   s_Canvas.SetFlags(WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS);
   s_Canvas.SetDrawCommands(s_Commands);
   s_DrawElapsed = 1;
  }
  // Only local projection runs at 20 Hz. Server ledger scans/owner RPCs run at 1 Hz.
  s_DrawElapsed += timeSlice;
  if (s_DrawElapsed < 0.05) return;
  s_DrawElapsed = 0;
  Draw();
 }
 protected static void Draw()
 {
  s_Commands.Clear();
  float width, height;
  s_Canvas.GetScreenSize(width, height);
  s_Canvas.SetSizeInUnits(Vector(width, height, 0));
  s_Canvas.SetZoom(1); s_Canvas.SetOffsetPx(vector.Zero);
  SCR_MapEntity mapEntity = SCR_MapEntity.GetMapInstance();
  bool onMap = mapEntity && mapEntity.IsOpen();
  // Never draw this overlay on a player map or an unsupported map configuration.
  if (onMap && (!mapEntity.GetMapConfig() || mapEntity.GetMapConfig().MapEntityMode != EMapEntityMode.EDITOR))
  {
   s_Canvas.SetDrawCommands(s_Commands); return;
  }
  BaseWorld world = GetGame().GetWorld();
  vector camera[4];
  world.GetCamera(world.GetCurrentCameraId(), camera);
  foreach (EBG_FullCacheMarker marker : s_Markers)
  {
   float x, y;
   if (onMap)
   {
    int mapX, mapY;
    mapEntity.WorldToScreen(marker.Position[0], marker.Position[2], mapX, mapY, true);
    x = mapX; y = mapY;
   }
   else
   {
    if (vector.Dot(marker.Position - camera[3], camera[2]) <= 0) continue;
    vector screen = GetGame().GetWorkspace().ProjWorldToScreenNative(marker.Position, world);
    x = screen[0]; y = screen[1];
   }
   if (x < 8 || y < 50 || x > width - 8 || y > height - 24) continue;
   marker.Place(x, y);
   s_Commands.Insert(marker.Icon); s_Commands.Insert(marker.Label);
  }
  if (s_Truncated)
  {
   TextDrawCommand warning = new TextDrawCommand();
   warning.m_sText = "Full cache icons: showing first 256 groups";
   warning.m_Position = "24 245 0";
   warning.m_iColor = 0xffffd35a; warning.m_fSize = 15; warning.m_iFontPropertiesId = 0;
   s_Commands.Insert(warning);
  }
  s_Canvas.SetDrawCommands(s_Commands);
 }
}

modded class SCR_EditorManagerEntity
{
 protected float m_EBGFullMarkerRequestTime = -1;
 // Native InitOwner keeps the local editor frame active even with no zones or a
 // closed map. Preserve permission cleanup without a separate polling callback.
 override void EOnFrame(IEntity owner, float timeSlice)
 {
  super.EOnFrame(owner, timeSlice);
  if (this != SCR_EditorManagerEntity.GetInstance())
  {
   return;
  }
  EBG_CacheVisuals.UpdateAuthorization();
  EBG_FullCacheMarkers.Update(this, timeSlice);
 }
 void ~SCR_EditorManagerEntity()
 {
  if (this == SCR_EditorManagerEntity.GetInstance())
  {
   EBG_FullCacheMarkers.Reset();
  }
 }
 void EBG_RequestFullCacheMarkers(int epoch)
 {
  if (this != SCR_EditorManagerEntity.GetInstance() || !EBG_CacheVisuals.Authorized()) return;
  if (Replication.IsServer()) EBG_FullCacheMarkersServer(epoch);
  else Rpc(EBG_FullCacheMarkersServer, epoch);
 }
 [RplRpc(RplChannel.Reliable, RplRcver.Server)]
 protected void EBG_FullCacheMarkersServer(int epoch)
 {
  if (!Replication.IsServer() || !EBG_CacheVisuals.AuthorizedEditor(this)) return;
  float now = GetGame().GetWorld().GetWorldTime() * 0.001;
  if (now - m_EBGFullMarkerRequestTime < 0.9) return;
  m_EBGFullMarkerRequestTime = now;
  array<vector> positions;
  array<int> survivors;
  bool truncated;
  EBG_FullCacheMarkers.Collect(positions, survivors, truncated);
  if (this == SCR_EditorManagerEntity.GetInstance()) EBG_FullCacheMarkersOwner(epoch, positions, survivors, truncated);
  else Rpc(EBG_FullCacheMarkersOwner, epoch, positions, survivors, truncated);
 }
 [RplRpc(RplChannel.Reliable, RplRcver.Owner)]
 protected void EBG_FullCacheMarkersOwner(int epoch, array<vector> positions, array<int> survivors, bool truncated)
 {
  EBG_CacheVisuals.UpdateAuthorization();
  if (this != SCR_EditorManagerEntity.GetInstance() || epoch != EBG_CacheVisuals.AccessEpoch || !EBG_CacheVisuals.Authorized()) return;
  EBG_FullCacheMarkers.Receive(positions, survivors, truncated);
 }
}
