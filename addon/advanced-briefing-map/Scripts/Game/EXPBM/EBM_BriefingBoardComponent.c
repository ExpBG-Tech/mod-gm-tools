[ComponentEditorProps(category: "EXPBG/Briefing", description: "EXPBG live briefing board: one player at a time streams their map view, own markers and drawn lines onto this board")]
class EBM_BriefingBoardComponentClass : ScriptComponentClass {}

// Server-authoritative board. One briefer holds the lock; their client streams the
// visible map frame, their own static markers and their drawn lines. Every client
// with a UI renders that state into the board mesh's $rendertarget material.
class EBM_BriefingBoardComponent : ScriptComponent
{
 static const ResourceName LAYOUT = "{636C657BF826EDAA}UI/layouts/EXPBM/EBM_BoardRT.layout";
 static const ResourceName LINE_LAYOUT = "{E8850FCD9219C411}UI/layouts/Map/MapDrawLine.layout";
 // Per marker: type, config id, owner id, flags, world x, world y, rotation, color entry, icon entry.
 static const int MARKER_FIELDS = 9;
 static const int MAX_MARKERS = 48;
 static const int MAX_LINES = 24;
 static const int TEXT_LIMIT = 64;
 static const int STALE_MS = 12000;
 // Height of the vanilla line image (the texture carries its own transparent margin).
 static const float LINE_THICKNESS = 50;

 [Attribute("2", UIWidgets.Slider, "Scale applied when the board entity is unscaled (1 = keep model size)", "1 4 0.1")]
 protected float m_fBoardScale;
 [Attribute("1024", UIWidgets.EditBox, "Render target width in layout units")]
 protected int m_iRtWidth;
 [Attribute("720", UIWidgets.EditBox, "Render target height in layout units")]
 protected int m_iRtHeight;
 [Attribute("", UIWidgets.ResourcePickerThumbnail, "Map image override; empty uses the world map entity's satellite background image", "edds")]
 protected ResourceName m_sMapImage;
 [Attribute("60", UIWidgets.EditBox, "Clients render the board within this camera distance (m)")]
 protected float m_fRenderDistance;
 [Attribute("30", UIWidgets.EditBox, "The lock is released when the briefer is further away than this (m)")]
 protected float m_fMaxBriefDistance;

 [RplProp(onRplName: "EBM_OnStateRpl")]
 protected int m_iBriefer;
 [RplProp(onRplName: "EBM_OnStateRpl")]
 protected string m_sBrieferName;
 [RplProp(onRplName: "EBM_OnStateRpl")]
 protected ref array<float> m_aView = {};
 [RplProp(onRplName: "EBM_OnStateRpl")]
 protected ref array<int> m_aMarkers = {};
 [RplProp(onRplName: "EBM_OnStateRpl")]
 protected ref array<string> m_aTexts = {};
 [RplProp(onRplName: "EBM_OnStateRpl")]
 protected ref array<float> m_aLines = {};

 // Server only.
 protected float m_fLastUpdateMs;
 protected bool m_bWatchdog;

 // Client rendering.
 protected Widget m_wRoot;
 protected RTTextureWidget m_wRT;
 protected Widget m_wMarkerLayer;
 protected Widget m_wLineLayer;
 protected ImageWidget m_wMap;
 protected TextWidget m_wTitle;
 protected ref array<Widget> m_aMarkerWidgets = {};
 protected ref array<ref SCR_MapMarkerBase> m_aMarkerObjects = {};
 protected ref array<int> m_aMarkerSlots = {};
 protected ref array<Widget> m_aLineWidgets = {};
 protected string m_sMarkerSignature;
 protected bool m_bMapImageLoaded;
 protected bool m_bRedrawQueued;
 protected float m_fCenterX;
 protected float m_fCenterY;
 protected float m_fScale = 1;

 static EBM_BriefingBoardComponent Find(IEntity entity)
 {
  if (!entity) return null;
  return EBM_BriefingBoardComponent.Cast(entity.FindComponent(EBM_BriefingBoardComponent));
 }
 static EBM_BriefingBoardComponent FromRplId(RplId id)
 {
  if (!id.IsValid()) return null;
  RplComponent rpl = RplComponent.Cast(Replication.FindItem(id));
  if (!rpl) return null;
  return Find(rpl.GetEntity());
 }
 RplId GetRplId()
 {
  RplComponent rpl = RplComponent.Cast(GetOwner().FindComponent(RplComponent));
  if (!rpl) return RplId.Invalid();
  return rpl.Id();
 }
 bool IsAuthority()
 {
  RplComponent rpl = RplComponent.Cast(GetOwner().FindComponent(RplComponent));
  return !rpl || !rpl.IsProxy();
 }
 int GetBrieferId() { return m_iBriefer; }
 string GetBrieferName() { return m_sBrieferName; }
 float GetMaxBriefDistance() { return m_fMaxBriefDistance; }

 protected static float NowMs()
 {
  BaseWorld world = GetGame().GetWorld();
  if (!world) return 0;
  return world.GetWorldTime();
 }

 //------------------------------------------------------------------------------------------------
 // Server
 //------------------------------------------------------------------------------------------------
 // Grants the lock unless another valid briefer holds it, then tells the briefer's client to open its map.
 bool ServerRequestBriefing(int playerId)
 {
  if (!IsAuthority() || playerId <= 0) return false;
  if (m_iBriefer > 0 && m_iBriefer != playerId && ServerBrieferValid()) return false;
  if (m_iBriefer != playerId)
  {
   m_iBriefer = playerId;
   m_sBrieferName = GetGame().GetPlayerManager().GetPlayerName(playerId);
   m_aView.Clear();
   m_aMarkers.Clear();
   m_aTexts.Clear();
   m_aLines.Clear();
   Replication.BumpMe();
   EBM_OnStateRpl();
  }
  m_fLastUpdateMs = NowMs();
  if (!m_bWatchdog)
  {
   m_bWatchdog = true;
   GetGame().GetCallqueue().CallLater(ServerWatchdog, 1000, true);
  }
  SCR_PlayerController controller = SCR_PlayerController.Cast(GetGame().GetPlayerManager().GetPlayerController(playerId));
  if (controller) controller.EBM_BeginBriefing(GetRplId());
  return true;
 }

 // Only the lock holder may write; sizes are bounded before anything is replicated.
 void ServerApplyState(int playerId, array<float> view, array<int> markers, array<string> texts, array<float> lines)
 {
  if (!IsAuthority() || playerId <= 0 || playerId != m_iBriefer) return;
  if (!view || !markers || !texts || !lines) return;
  if (view.Count() != 4 || texts.Count() > MAX_MARKERS || markers.Count() != texts.Count() * MARKER_FIELDS) return;
  if (lines.Count() % 4 != 0 || lines.Count() > MAX_LINES * 4) return;
  m_fLastUpdateMs = NowMs();
  m_aView.Copy(view);
  m_aMarkers.Copy(markers);
  m_aTexts.Clear();
  foreach (string text : texts)
  {
   string clipped = text;
   if (clipped.Length() > TEXT_LIMIT) clipped = text.Substring(0, TEXT_LIMIT);
   m_aTexts.Insert(clipped);
  }
  m_aLines.Copy(lines);
  Replication.BumpMe();
  EBM_OnStateRpl();
 }

 // playerId 0 forces the release (watchdog). The last picture stays on the board.
 void ServerRelease(int playerId)
 {
  if (!IsAuthority() || m_iBriefer <= 0) return;
  if (playerId > 0 && playerId != m_iBriefer) return;
  m_iBriefer = 0;
  Replication.BumpMe();
  StopWatchdog();
  EBM_OnStateRpl();
 }

 protected void StopWatchdog()
 {
  if (!m_bWatchdog) return;
  m_bWatchdog = false;
  if (GetGame()) GetGame().GetCallqueue().Remove(ServerWatchdog);
 }

 protected void ServerWatchdog()
 {
  if (m_iBriefer <= 0)
  {
   StopWatchdog();
   return;
  }
  if (!ServerBrieferValid() || NowMs() - m_fLastUpdateMs > STALE_MS) ServerRelease(0);
 }

 protected bool ServerBrieferValid()
 {
  PlayerController controller = GetGame().GetPlayerManager().GetPlayerController(m_iBriefer);
  if (!controller) return false;
  IEntity controlled = controller.GetControlledEntity();
  if (!controlled) return false;
  ChimeraCharacter character = ChimeraCharacter.Cast(controlled);
  if (character)
  {
   CharacterControllerComponent characterController = character.GetCharacterController();
   if (characterController && characterController.IsDead()) return false;
  }
  return vector.DistanceSq(controlled.GetOrigin(), GetOwner().GetOrigin()) <= m_fMaxBriefDistance * m_fMaxBriefDistance;
 }

 //------------------------------------------------------------------------------------------------
 // Lifecycle
 //------------------------------------------------------------------------------------------------
 override void OnPostInit(IEntity owner)
 {
  super.OnPostInit(owner);
  if (!GetGame().InPlayMode()) return;
  // Editor spawns may arrive unscaled; apply the board scale once on every machine.
  if (m_fBoardScale > 0 && Math.AbsFloat(owner.GetScale() - 1) < 0.001 && Math.AbsFloat(m_fBoardScale - 1) > 0.001)
  {
   owner.SetScale(m_fBoardScale);
   owner.Update();
  }
  if (System.IsConsoleApp()) return;
  GetGame().GetCallqueue().CallLater(ClientProximityTick, 1000, true);
 }

 override void OnDelete(IEntity owner)
 {
  if (GetGame())
  {
   GetGame().GetCallqueue().Remove(ClientProximityTick);
   GetGame().GetCallqueue().Remove(ServerWatchdog);
   GetGame().GetCallqueue().Remove(RedrawQueued);
  }
  m_bWatchdog = false;
  DestroyBoardWidgets();
  EBM_BriefingClient.OnBoardDeleted(this);
  super.OnDelete(owner);
 }

 //------------------------------------------------------------------------------------------------
 // Client rendering
 //------------------------------------------------------------------------------------------------
 // Replication callback; also called on the authority after local writes.
 protected void EBM_OnStateRpl()
 {
  if (m_bRedrawQueued || System.IsConsoleApp() || !GetGame()) return;
  m_bRedrawQueued = true;
  GetGame().GetCallqueue().CallLater(RedrawQueued, 0);
 }

 protected void RedrawQueued()
 {
  m_bRedrawQueued = false;
  Redraw();
 }

 protected void ClientProximityTick()
 {
  IEntity owner = GetOwner();
  if (!owner) return;
  BaseWorld world = owner.GetWorld();
  if (!world) return;
  vector camera[4];
  world.GetCurrentCamera(camera);
  float distance = vector.Distance(camera[3], owner.GetOrigin());
  if (!m_wRoot && distance <= m_fRenderDistance)
  {
   CreateBoardWidgets();
   Redraw();
  }
  else if (m_wRoot && distance > m_fRenderDistance + 15)
  {
   DestroyBoardWidgets();
  }
 }

 protected ResourceName ResolveMapImage()
 {
  if (!m_sMapImage.IsEmpty()) return m_sMapImage;
  SCR_MapEntity mapEntity = SCR_MapEntity.GetMapInstance();
  if (!mapEntity) return ResourceName.Empty;
  EntityPrefabData prefabData = mapEntity.GetPrefabData();
  if (!prefabData) return ResourceName.Empty;
  BaseContainer prefab = prefabData.GetPrefab();
  if (!prefab) return ResourceName.Empty;
  ResourceName image;
  prefab.Get("Satellite background image", image);
  return image;
 }

 protected void CreateBoardWidgets()
 {
  WorkspaceWidget workspace = GetGame().GetWorkspace();
  if (!workspace) return;
  m_wRoot = workspace.CreateWidgets(LAYOUT);
  if (!m_wRoot) return;
  m_wRT = RTTextureWidget.Cast(m_wRoot.FindAnyWidget("RTTexture0"));
  m_wMarkerLayer = m_wRoot.FindAnyWidget("EBM_Markers");
  m_wLineLayer = m_wRoot.FindAnyWidget("EBM_Lines");
  m_wMap = ImageWidget.Cast(m_wRoot.FindAnyWidget("EBM_Map"));
  m_wTitle = TextWidget.Cast(m_wRoot.FindAnyWidget("EBM_Title"));
  if (!m_wRT || !m_wMarkerLayer || !m_wLineLayer || !m_wMap)
  {
   DestroyBoardWidgets();
   return;
  }
  FrameSlot.SetSize(m_wRoot, m_iRtWidth, m_iRtHeight);
  FrameSlot.SetSize(m_wRT, m_iRtWidth, m_iRtHeight);
  ResourceName image = ResolveMapImage();
  m_bMapImageLoaded = false;
  if (!image.IsEmpty()) m_bMapImageLoaded = m_wMap.LoadImageTexture(0, image);
  m_wMap.SetVisible(m_bMapImageLoaded);
  m_sMarkerSignature = string.Empty;
  m_wRT.SetRenderTarget(GetOwner());
 }

 protected void DestroyBoardWidgets()
 {
  IEntity owner = GetOwner();
  if (m_wRT && owner && !owner.IsDeleted()) m_wRT.RemoveRenderTarget(owner);
  if (m_wRoot) m_wRoot.RemoveFromHierarchy();
  m_wRoot = null;
  m_wRT = null;
  m_wMarkerLayer = null;
  m_wLineLayer = null;
  m_wMap = null;
  m_wTitle = null;
  m_aMarkerWidgets.Clear();
  m_aMarkerObjects.Clear();
  m_aMarkerSlots.Clear();
  m_aLineWidgets.Clear();
  m_sMarkerSignature = string.Empty;
 }

 // World map coordinates (x east, y north) to board layout units.
 protected void ToBoard(float worldX, float worldY, out float boardX, out float boardY)
 {
  boardX = m_iRtWidth * 0.5 + (worldX - m_fCenterX) * m_fScale;
  boardY = m_iRtHeight * 0.5 - (worldY - m_fCenterY) * m_fScale;
 }

 protected void Redraw()
 {
  if (!m_wRoot) return;
  UpdateTitle();
  float mapX, mapY;
  float mapWidth = 1024;
  float mapHeight = 1024;
  SCR_MapEntity mapEntity = SCR_MapEntity.GetMapInstance();
  if (mapEntity && mapEntity.GetMapSizeX() > 0 && mapEntity.GetMapSizeY() > 0)
  {
   vector offset = mapEntity.Offset();
   mapX = offset[0];
   mapY = offset[2];
   mapWidth = mapEntity.GetMapSizeX();
   mapHeight = mapEntity.GetMapSizeY();
  }
  // Fit the briefer's visible frame (or the whole map when idle) inside the board.
  float minX = mapX;
  float minY = mapY;
  float maxX = mapX + mapWidth;
  float maxY = mapY + mapHeight;
  if (m_aView.Count() == 4 && m_aView[2] - m_aView[0] > 1 && m_aView[3] - m_aView[1] > 1)
  {
   minX = m_aView[0];
   minY = m_aView[1];
   maxX = m_aView[2];
   maxY = m_aView[3];
  }
  m_fScale = Math.Min(m_iRtWidth / (maxX - minX), m_iRtHeight / (maxY - minY));
  m_fCenterX = (minX + maxX) * 0.5;
  m_fCenterY = (minY + maxY) * 0.5;
  if (m_bMapImageLoaded && m_wMap)
  {
   float left, top;
   ToBoard(mapX, mapY + mapHeight, left, top);
   FrameSlot.SetPos(m_wMap, left, top);
   m_wMap.SetSize(mapWidth * m_fScale, mapHeight * m_fScale);
  }
  RedrawLines();
  RedrawMarkers();
 }

 protected void UpdateTitle()
 {
  if (!m_wTitle) return;
  string title = "EXPBG BRIEFING";
  if (m_iBriefer > 0) title = title + "  -  LIVE: " + m_sBrieferName;
  else if (!m_sBrieferName.IsEmpty()) title = title + "  -  " + m_sBrieferName;
  m_wTitle.SetText(title);
 }

 protected void RedrawLines()
 {
  foreach (Widget oldLine : m_aLineWidgets)
  {
   if (oldLine) oldLine.RemoveFromHierarchy();
  }
  m_aLineWidgets.Clear();
  WorkspaceWidget workspace = GetGame().GetWorkspace();
  if (!workspace || !m_wLineLayer) return;
  int lineCount = m_aLines.Count() / 4;
  for (int i = 0; i < lineCount; i++)
  {
   float startX, startY, endX, endY;
   ToBoard(m_aLines[i * 4], m_aLines[i * 4 + 1], startX, startY);
   ToBoard(m_aLines[i * 4 + 2], m_aLines[i * 4 + 3], endX, endY);
   float deltaX = endX - startX;
   float deltaY = endY - startY;
   float length = Math.Sqrt(deltaX * deltaX + deltaY * deltaY);
   if (length < 1) continue;
   // The vanilla drawn-line layout (root aligned left-centre, image pivot left-centre), placed the way
   // the map places it: root at the start point, image rotated in screen space (y down) and stretched.
   Widget lineRoot = workspace.CreateWidgets(LINE_LAYOUT, m_wLineLayer);
   if (!lineRoot) continue;
   ImageWidget lineImage = ImageWidget.Cast(lineRoot.FindAnyWidget("DrawLineImage"));
   if (lineImage)
   {
    lineImage.SetRotation(Math.Atan2(deltaY, deltaX) * Math.RAD2DEG);
    lineImage.SetSize(length, LINE_THICKNESS);
   }
   FrameSlot.SetPos(lineRoot, startX, startY);
   DisableCursor(lineRoot);
   m_aLineWidgets.Insert(lineRoot);
  }
 }

 // Rebuilds marker widgets only when something other than their position changed.
 protected string BuildMarkerSignature()
 {
  int markerCount = m_aTexts.Count();
  string signature = markerCount.ToString();
  if (m_aMarkers.Count() != markerCount * MARKER_FIELDS) return signature;
  for (int i = 0; i < markerCount; i++)
  {
   int field = i * MARKER_FIELDS;
   signature += "|" + m_aMarkers[field].ToString() + "," + m_aMarkers[field + 1].ToString() + "," + m_aMarkers[field + 2].ToString() + "," + m_aMarkers[field + 3].ToString();
   signature += "," + m_aMarkers[field + 6].ToString() + "," + m_aMarkers[field + 7].ToString() + "," + m_aMarkers[field + 8].ToString() + "," + m_aTexts[i];
  }
  return signature;
 }

 protected void RedrawMarkers()
 {
  if (!m_wMarkerLayer) return;
  string signature = BuildMarkerSignature();
  if (signature != m_sMarkerSignature)
  {
   RebuildMarkers();
   m_sMarkerSignature = signature;
  }
  int widgetCount = m_aMarkerWidgets.Count();
  for (int i = 0; i < widgetCount; i++)
  {
   Widget markerWidget = m_aMarkerWidgets[i];
   if (!markerWidget) continue;
   int field = m_aMarkerSlots[i] * MARKER_FIELDS;
   if (field + MARKER_FIELDS > m_aMarkers.Count()) continue;
   float boardX, boardY;
   ToBoard(m_aMarkers[field + 4], m_aMarkers[field + 5], boardX, boardY);
   FrameSlot.SetPos(markerWidget, boardX, boardY);
   markerWidget.SetVisible(boardX > -64 && boardX < m_iRtWidth + 64 && boardY > -64 && boardY < m_iRtHeight + 64);
  }
 }

 // Uses the vanilla marker layout and entry config so viewers see the briefer's markers as drawn on the map.
 protected void RebuildMarkers()
 {
  foreach (Widget oldMarker : m_aMarkerWidgets)
  {
   if (oldMarker) oldMarker.RemoveFromHierarchy();
  }
  m_aMarkerWidgets.Clear();
  m_aMarkerObjects.Clear();
  m_aMarkerSlots.Clear();
  WorkspaceWidget workspace = GetGame().GetWorkspace();
  SCR_MapMarkerManagerComponent manager = SCR_MapMarkerManagerComponent.GetInstance();
  if (!workspace || !manager || !GetGame().GetPlayerController()) return;
  SCR_MapMarkerConfig config = manager.GetMarkerConfig();
  if (!config) return;
  int markerCount = m_aTexts.Count();
  if (m_aMarkers.Count() != markerCount * MARKER_FIELDS) return;
  for (int i = 0; i < markerCount; i++)
  {
   int field = i * MARKER_FIELDS;
   SCR_MapMarkerEntryConfig entry = config.GetMarkerEntryConfigByType(m_aMarkers[field]);
   if (!entry) continue;
   SCR_MapMarkerBase marker = new SCR_MapMarkerBase();
   marker.SetType(m_aMarkers[field]);
   marker.SetMarkerConfigID(m_aMarkers[field + 1]);
   marker.SetMarkerOwnerID(m_aMarkers[field + 2]);
   marker.SetFlags(m_aMarkers[field + 3]);
   marker.SetWorldPos(m_aMarkers[field + 4], m_aMarkers[field + 5]);
   marker.SetRotation(m_aMarkers[field + 6]);
   marker.SetColorEntry(m_aMarkers[field + 7]);
   marker.SetIconEntry(m_aMarkers[field + 8]);
   marker.SetCustomText(m_aTexts[i]);
   Widget markerWidget = workspace.CreateWidgets(entry.GetMarkerLayout(), m_wMarkerLayer);
   if (!markerWidget) continue;
   SCR_MapMarkerWidgetComponent widgetComponent = SCR_MapMarkerWidgetComponent.Cast(markerWidget.FindHandler(SCR_MapMarkerWidgetComponent));
   if (widgetComponent)
   {
    widgetComponent.SetMarkerObject(marker);
    entry.InitClientSettings(marker, widgetComponent);
    widgetComponent.SetRotation(m_aMarkers[field + 6]);
    widgetComponent.SetEventListening(false);
   }
   DisableCursor(markerWidget);
   m_aMarkerWidgets.Insert(markerWidget);
   m_aMarkerObjects.Insert(marker);
   m_aMarkerSlots.Insert(i);
  }
 }

 // Board widgets live in the main workspace; they must never take clicks or focus.
 protected static void DisableCursor(Widget widget)
 {
  if (!widget) return;
  widget.SetFlags(WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS);
  Widget child = widget.GetChildren();
  while (child)
  {
   DisableCursor(child);
   child = child.GetSibling();
  }
 }
}
