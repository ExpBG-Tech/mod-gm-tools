[ComponentEditorProps(category: "EXPBG/Briefing", description: "EXPBG live briefing board: one player at a time streams their map view, own markers and drawn lines onto this board")]
class EBM_BriefingBoardComponentClass : ScriptComponentClass {}

// Server-authoritative board. One briefer holds the lock; their client streams the
// visible map frame, their own static markers and their drawn lines. Every client
// with a UI renders that state into the board mesh's $rendertarget material.
// Both supported meshes map the texture a quarter turn round (WallMap_01 and the Heine
// projector screen), and the game's own map view (roads, buildings, names; EBM_MapEntity.c)
// cannot be turned. So by default each client spawns a local flat quad with upright UVs (a
// vanilla model, every material slot remapped to the render target material) just in
// front of the screen, and draws the board content (EBM_BoardContent.layout) unturned into
// its own render target (EBM_SurfaceRT): game map view over the world map image, lines,
// markers and title. The screen itself (RTTexture0) then shows only the paper backdrop.
// Fallbacks: if the game map view cannot draw, the map image under it still shows; if the
// surface model cannot be loaded or spawned, the content is drawn straight onto the screen
// (RTTexture0, over its UV rectangle) with every element turned, the single render target
// path 0.1.9 proved live. The 0.1.10 hand-off (content in EBM_Canvas shown turned through
// the image EBM_Screen) drew a plain white screen live and is only an opt-in experiment.
class EBM_BriefingBoardComponent : ScriptComponent
{
 static const ResourceName LAYOUT = "{636C657BF826EDAA}UI/layouts/EXPBM/EBM_BoardRT.layout";
 // The render target material, on the screen slot (prefab) and on the upright surface (remap).
 static const ResourceName SURFACE_MATERIAL = "{AA3CD43539C7EF56}Assets/EXPBM/EBM_BoardRT.emat";
 static const ResourceName CONTENT_LAYOUT = "{5DBA61BA423517E6}UI/layouts/EXPBM/EBM_BoardContent.layout";
 static const ResourceName ENGINE_MAP_LAYOUT = "{F4540CBAD6301389}UI/layouts/EXPBM/EBM_EngineMap.layout";
 static const ResourceName LINE_LAYOUT = "{E8850FCD9219C411}UI/layouts/Map/MapDrawLine.layout";
 static const ResourceName DEFAULT_MAP_CONFIG = "{1B8AC767E06A0ACD}Configs/Map/MapFullscreen.conf";
 // A new map widget is laid out a frame later (vanilla waits SCR_MapEntity.FRAME_DELAY); give up after this many frames.
 static const int ENGINE_MAP_RETRIES = 10;
 // Per marker: type, config id, owner id, flags, world x, world y, rotation, color entry, icon entry.
 static const int MARKER_FIELDS = 9;
 static const int MAX_MARKERS = 48;
 static const int MAX_LINES = 24;
 static const int TEXT_LIMIT = 64;
 static const int STALE_MS = 12000;
 // Height of the vanilla line image (the texture carries its own transparent margin).
 static const float LINE_THICKNESS = 50;
 // Title box in board units (top-left corner and height; the width follows the board).
 static const float TITLE_LEFT = 16;
 static const float TITLE_TOP = 8;
 static const float TITLE_HEIGHT = 40;
 // Render target renewals per board build when the mesh object changes.
 static const int MAX_REBINDS = 5;

 [Attribute("2", UIWidgets.Slider, "Scale applied when the board entity is unscaled (1 = keep model size)", "1 4 0.1")]
 protected float m_fBoardScale;
 [Attribute("1024", UIWidgets.EditBox, "Square render target size in layout units")]
 protected int m_iRtSize;
 // The screen surface's UV rectangle in the render target (defaults: Heine projector screen, slot tela1).
 [Attribute("0.0505", UIWidgets.EditBox, "Screen surface: lowest texture U (render target x) of its UVs")]
 protected float m_fScreenUMin;
 [Attribute("0.6223", UIWidgets.EditBox, "Screen surface: highest texture U (render target x) of its UVs")]
 protected float m_fScreenUMax;
 [Attribute("0.0087", UIWidgets.EditBox, "Screen surface: lowest texture V (render target y) of its UVs")]
 protected float m_fScreenVMin;
 [Attribute("0.9912", UIWidgets.EditBox, "Screen surface: highest texture V (render target y) of its UVs")]
 protected float m_fScreenVMax;
 [Attribute("-90", UIWidgets.EditBox, "Turn (degrees, clockwise) of the board picture inside the render target so it reads upright on the mesh: -90 projector screen, 90 WallMap_01, 0 unturned UVs")]
 protected float m_fScreenRotation;
 // Upright surface: a local flat quad with standard upright UVs placed just in front of the
 // screen, so the board (and the game's own map view, which cannot be turned) is drawn
 // unturned. Defaults: the Heine projector screen (tela1) and the vanilla barracks door pane.
 [Attribute("0.0059 1.2312 -0.0118", UIWidgets.EditBox, "Screen centre in the board model's own space (m, unscaled)")]
 protected vector m_vScreenCenter;
 [Attribute("1 0 0", UIWidgets.EditBox, "Screen front normal in the board model's own space")]
 protected vector m_vScreenNormal;
 [Attribute("0 1 0", UIWidgets.EditBox, "Screen up direction in the board model's own space")]
 protected vector m_vScreenUp;
 [Attribute("3.9564 2.3026 0", UIWidgets.EditBox, "Screen width and height (m, unscaled)")]
 protected vector m_vScreenSize;
 [Attribute("{01F85A3B7D7C5EB0}Assets/Structures/BuildingsParts/Doors/Door_Barracks_01/Glass_Door_Barracks_92x56.xob", UIWidgets.ResourcePickerThumbnail, "Upright surface model: one flat quad, front +Z, upright UVs (u to the right, v down seen from the front); empty draws on the screen itself", "xob")]
 protected ResourceName m_sSurfaceModel;
 [Attribute("0 0 0", UIWidgets.EditBox, "Upright surface model: quad centre in its own space")]
 protected vector m_vSurfaceModelCenter;
 [Attribute("0.92 0.56 0", UIWidgets.EditBox, "Upright surface model: quad width and height in its own space")]
 protected vector m_vSurfaceModelSize;
 [Attribute("0.28439", UIWidgets.EditBox, "Upright surface model: lowest U (left edge)")]
 protected float m_fSurfaceUMin;
 [Attribute("0.74439", UIWidgets.EditBox, "Upright surface model: highest U (right edge)")]
 protected float m_fSurfaceUMax;
 [Attribute("0.35912", UIWidgets.EditBox, "Upright surface model: lowest V (top edge)")]
 protected float m_fSurfaceVMin;
 [Attribute("0.63912", UIWidgets.EditBox, "Upright surface model: highest V (bottom edge)")]
 protected float m_fSurfaceVMax;
 [Attribute("0.01", UIWidgets.EditBox, "Upright surface distance in front of the screen (m)")]
 protected float m_fSurfaceLift;
 [Attribute("2048", UIWidgets.EditBox, "Upright surface render target size (square, layout units)")]
 protected int m_iSurfaceRtSize;
 [Attribute("0", UIWidgets.CheckBox, "Experimental, instead of the upright surface: draw the board in a second render target and hand it to the screen's render target turned (drew a white screen in the 0.1.10 live test). The client option -ebmCanvasHandoff 1 switches it on for a test")]
 protected bool m_bCanvasHandoff;
 [Attribute("15", UIWidgets.EditBox, "Hand-off canvas frame rate limit (0 = unlimited)")]
 protected int m_iCanvasMaxFps;
 [Attribute("1", UIWidgets.CheckBox, "Draw roads, buildings, names and contours with the game's own map renderer where it can be shown (upright surface, hand-off canvas or an unturned screen; one board per client at a time, only while the player's own map is closed). The plain map image is always drawn underneath")]
 protected bool m_bUseEngineMap;
 [Attribute("0", UIWidgets.CheckBox, "Trace board widget creation, render target binding and the drawing path in the client log (EBM DIAG lines); the client option -ebmDiagnostics 1 does the same for every board")]
 protected bool m_bDebugTrace;
 [Attribute("1080", UIWidgets.EditBox, "Map detail layer: the one a briefer screen this many pixels high would show for the same view")]
 protected float m_fLayerReferenceHeight;
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

 // Client rendering. Board size in layout units (the screen's UV rectangle, upright).
 protected int m_iRtWidth = 1006;
 protected int m_iRtHeight = 586;
 // Quarter turns of the mesh UVs (-1 projector, +1 wall map, 0 none) and of the drawing
 // (the same on the direct path, 0 on the hand-off canvas).
 protected int m_iScreenTurn;
 protected int m_iDrawTurn;
 protected bool m_bHandoffActive;
 protected bool m_bSurfaceActive;
 // Local (never replicated) upright surface entity and its render target.
 protected IEntity m_Surface;
 protected RTTextureWidget m_wSurfaceRT;
 protected vector m_vSurfacePlacedOrigin;
 protected vector m_vSurfacePlacedAngles;
 protected float m_fSurfacePlacedScale;
 protected VObject m_BoundObject;
 protected int m_iRebinds;
 protected string m_sLastPath;
 protected Widget m_wRoot;
 protected RTTextureWidget m_wRT;
 protected RTTextureWidget m_wCanvas;
 protected ImageWidget m_wScreen;
 protected Widget m_wContent;
 protected Widget m_wEngineMapSlot;
 protected CanvasWidget m_wEngineMap;
 protected ref MapConfiguration m_EngineMapConfig;
 protected bool m_bEngineMapLayersReady;
 protected bool m_bEngineMapFailed;
 protected int m_iEngineMapRetries;
 protected bool m_bMapHooks;
 // The native map entity has one zoom, pan and layer: only one board per client drives it.
 protected static EBM_BriefingBoardComponent s_EngineMapOwner;
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
   return;
  }
  if (m_Surface)
   PlaceSurface(false);
  if (!m_wRoot)
   return;
  if (m_wRT && m_iRebinds < MAX_REBINDS && owner.GetVObject() != m_BoundObject)
  {
   // The render target belongs to the mesh object it was set on; a new object needs it again
   // (bounded, in case the engine hands out a new object handle each time).
   m_iRebinds++;
   BindRenderTarget("rebind");
  }
  else if (!m_wEngineMap && !s_EngineMapOwner && EngineMapAllowed())
  {
   // Another board gave the native map back (deleted, out of range): take it over.
   EBM_OnStateRpl();
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

 // Quarter turn of the screen UVs from m_fScreenRotation: -1 (anticlockwise), +1 or 0.
 protected int ScreenTurn()
 {
  float turn = m_fScreenRotation;
  if (Math.AbsFloat(Math.AbsFloat(turn) - 90) < 1)
  {
   if (turn < 0)
    return -1;
   return 1;
  }
  if (Math.AbsFloat(turn) >= 1)
   Print("EXPBG Briefing: screen rotation " + turn.ToString() + " is not 0 or a quarter turn; drawing unturned", LogLevel.WARNING);
  return 0;
 }

 protected void CreateBoardWidgets()
 {
  WorkspaceWidget workspace = GetGame().GetWorkspace();
  if (!workspace)
  {
   Trace("create-failed", "reason=no-workspace");
   return;
  }
  m_wRoot = workspace.CreateWidgets(LAYOUT);
  if (!m_wRoot)
  {
   Print("EXPBG Briefing: the board layout could not be created", LogLevel.WARNING);
   return;
  }
  m_wRT = RTTextureWidget.Cast(m_wRoot.FindAnyWidget("RTTexture0"));
  m_wCanvas = RTTextureWidget.Cast(m_wRoot.FindAnyWidget("EBM_Canvas"));
  m_wScreen = ImageWidget.Cast(m_wRoot.FindAnyWidget("EBM_Screen"));
  m_wSurfaceRT = RTTextureWidget.Cast(m_wRoot.FindAnyWidget("EBM_SurfaceRT"));
  if (!m_wRT || !m_wCanvas || !m_wScreen || !m_wSurfaceRT)
  {
   Print("EXPBG Briefing: the board layout lacks its render target widgets", LogLevel.WARNING);
   DestroyBoardWidgets();
   return;
  }
  // Path: experimental hand-off when asked for; otherwise the upright surface; otherwise the
  // board drawn straight onto the screen itself, turned (also the fallback when the surface
  // model cannot be loaded or spawned).
  m_iScreenTurn = ScreenTurn();
  m_bHandoffActive = m_iScreenTurn != 0 && (m_bCanvasHandoff || EBM_Diagnostics.HandoffForced());
  m_bSurfaceActive = false;
  if (!m_bHandoffActive && !m_sSurfaceModel.IsEmpty() && !EBM_Diagnostics.DirectForced())
   m_bSurfaceActive = SpawnSurface();
  Widget surface = m_wRT;
  if (m_bHandoffActive)
   surface = m_wCanvas;
  else if (m_bSurfaceActive)
   surface = m_wSurfaceRT;
  // Unused render targets and the hand-off image leave the hierarchy and draw nothing.
  if (!m_bHandoffActive)
  {
   m_wCanvas.RemoveFromHierarchy();
   m_wCanvas = null;
   m_wScreen.RemoveFromHierarchy();
   m_wScreen = null;
  }
  if (!m_bSurfaceActive)
  {
   m_wSurfaceRT.RemoveFromHierarchy();
   m_wSurfaceRT = null;
  }
  m_wContent = workspace.CreateWidgets(CONTENT_LAYOUT, surface);
  if (m_wContent)
  {
   m_wEngineMapSlot = m_wContent.FindAnyWidget("EBM_EngineMapSlot");
   m_wMarkerLayer = m_wContent.FindAnyWidget("EBM_Markers");
   m_wLineLayer = m_wContent.FindAnyWidget("EBM_Lines");
   m_wMap = ImageWidget.Cast(m_wContent.FindAnyWidget("EBM_Map"));
   m_wTitle = TextWidget.Cast(m_wContent.FindAnyWidget("EBM_Title"));
  }
  if (!m_wContent || !m_wMarkerLayer || !m_wLineLayer || !m_wMap)
  {
   Print("EXPBG Briefing: the board content layout could not be created", LogLevel.WARNING);
   DestroyBoardWidgets();
   return;
  }
  LayoutScreen();
  ResourceName image = ResolveMapImage();
  m_bMapImageLoaded = false;
  if (!image.IsEmpty()) m_bMapImageLoaded = m_wMap.LoadImageTexture(0, image);
  m_wMap.SetVisible(m_bMapImageLoaded);
  m_sMarkerSignature = string.Empty;
  m_sLastPath = string.Empty;
  if (TraceEnabled())
  {
   string uv = string.Format("%1..%2,%3..%4", m_fScreenUMin, m_fScreenUMax, m_fScreenVMin, m_fScreenVMax);
   if (m_bSurfaceActive)
    uv = string.Format("%1..%2,%3..%4", m_fSurfaceUMin, m_fSurfaceUMax, m_fSurfaceVMin, m_fSurfaceVMax);
   Trace("create", string.Format("mode=%1 screenTurn=%2 drawTurn=%3 board=%4x%5 uv=%6", DrawMode(), m_iScreenTurn, m_iDrawTurn, m_iRtWidth, m_iRtHeight, uv));
   Trace("raster", string.Format("image=%1 loaded=%2", image, m_bMapImageLoaded));
  }
  BindRenderTarget("bind");
  BindSurfaceTarget();
  SCR_MapEntity.GetOnMapInit().Insert(EBM_OnLocalMapInit);
  SCR_MapEntity.GetOnMapClose().Insert(EBM_OnLocalMapClose);
  m_bMapHooks = true;
 }

 protected string DrawMode()
 {
  if (m_bHandoffActive)
   return "handoff";
  if (m_bSurfaceActive)
   return "surface";
  return "direct";
 }

 // Hands RTTexture0 to the owner's mesh ($rendertarget in EBM_BoardRT.emat). The binding
 // belongs to the current mesh object, so it is remembered and renewed if the object changes.
 // With the upright surface the screen itself shows only the paper backdrop.
 protected void BindRenderTarget(string action)
 {
  IEntity owner = GetOwner();
  if (!m_wRT || !owner || owner.IsDeleted())
   return;
  m_wRT.SetRenderTarget(owner);
  m_BoundObject = owner.GetVObject();
  if (TraceEnabled())
   Trace(action, string.Format("entity=%1 object=%2 materials=%3", owner, DescribeObject(m_BoundObject), DescribeMaterials(m_BoundObject)));
 }

 protected void BindSurfaceTarget()
 {
  if (!m_wSurfaceRT || !m_Surface)
   return;
  m_wSurfaceRT.SetRenderTarget(m_Surface);
  if (TraceEnabled())
   Trace("bind-surface", string.Format("entity=%1 object=%2 materials=%3 scale=%4", m_Surface, DescribeObject(m_Surface.GetVObject()), DescribeMaterials(m_Surface.GetVObject()), m_Surface.GetScale()));
 }

 protected static string DescribeObject(VObject visual)
 {
  if (!visual)
   return "none";
  return visual.GetResourceName();
 }

 protected static string DescribeMaterials(VObject visual)
 {
  string slots;
  if (!visual)
   return slots;
  string materials[64];
  int count = visual.GetMaterials(materials);
  for (int i = 0; i < count; i++)
  {
   if (i > 0)
    slots += ",";
   slots += materials[i];
  }
  return slots;
 }

 //------------------------------------------------------------------------------------------------
 // Upright surface (local entity, never replicated)
 //------------------------------------------------------------------------------------------------
 // Spawns the surface quad with every material slot remapped to the render target material.
 protected bool SpawnSurface()
 {
  IEntity owner = GetOwner();
  if (!owner || m_vSurfaceModelSize[0] <= 0 || m_vSurfaceModelSize[1] <= 0)
   return false;
  Resource resource = Resource.Load(m_sSurfaceModel);
  BaseResourceObject resourceObject;
  if (resource && resource.IsValid())
   resourceObject = resource.GetResource();
  VObject visual;
  if (resourceObject)
   visual = resourceObject.ToVObject();
  if (!visual)
  {
   Print("EXPBG Briefing: the upright surface model could not be loaded; drawing on the screen itself", LogLevel.WARNING);
   Trace("surface-failed", "reason=model " + m_sSurfaceModel);
   return false;
  }
  vector transform[4];
  float scale;
  ComputeSurfaceTransform(transform, scale);
  EntitySpawnParams params = new EntitySpawnParams();
  params.TransformMode = ETransformMode.WORLD;
  params.Transform[0] = transform[0];
  params.Transform[1] = transform[1];
  params.Transform[2] = transform[2];
  params.Transform[3] = transform[3];
  params.Scale = scale;
  m_Surface = GetGame().SpawnEntity(GenericEntity, owner.GetWorld(), params);
  if (!m_Surface)
  {
   Print("EXPBG Briefing: the upright surface could not be spawned; drawing on the screen itself", LogLevel.WARNING);
   Trace("surface-failed", "reason=spawn");
   return false;
  }
  string remap;
  string materials[64];
  int count = visual.GetMaterials(materials);
  for (int i = 0; i < count; i++)
  {
   remap += string.Format("$remap '%1' '%2';", materials[i], SURFACE_MATERIAL);
  }
  m_Surface.SetObject(visual, remap);
  m_Surface.ClearFlags(EntityFlags.TRACEABLE, false);
  PlaceSurface(true);
  Trace("surface", string.Format("model=%1 slots=%2 scale=%3 origin=%4", m_sSurfaceModel, count, scale, transform[3]));
  return true;
 }

 // World transform of the surface: front +Z, right -X, up +Y of the quad onto the screen's
 // front, right and up, centred on the screen and as large as fits (uniform scale).
 protected void ComputeSurfaceTransform(out vector transform[4], out float scale)
 {
  IEntity owner = GetOwner();
  vector ownerTransform[4];
  owner.GetWorldTransform(ownerTransform);
  vector axisX = ownerTransform[0].Normalized();
  vector axisY = ownerTransform[1].Normalized();
  vector axisZ = ownerTransform[2].Normalized();
  float ownerScale = owner.GetScale();
  if (ownerScale <= 0)
   ownerScale = 1;
  vector normal = axisX * m_vScreenNormal[0] + axisY * m_vScreenNormal[1] + axisZ * m_vScreenNormal[2];
  normal.Normalize();
  vector up = axisX * m_vScreenUp[0] + axisY * m_vScreenUp[1] + axisZ * m_vScreenUp[2];
  up.Normalize();
  // Seen from the front, right = normal x up (checked on the decoded models: front +X has +Z on its right).
  vector right = CrossProduct(normal, up);
  vector center = ownerTransform[3] + (axisX * m_vScreenCenter[0] + axisY * m_vScreenCenter[1] + axisZ * m_vScreenCenter[2]) * ownerScale + normal * m_fSurfaceLift;
  scale = ownerScale * Math.Min(m_vScreenSize[0] / m_vSurfaceModelSize[0], m_vScreenSize[1] / m_vSurfaceModelSize[1]);
  transform[0] = -right;
  transform[1] = up;
  transform[2] = normal;
  transform[3] = center - (transform[0] * m_vSurfaceModelCenter[0] + transform[1] * m_vSurfaceModelCenter[1] + transform[2] * m_vSurfaceModelCenter[2]) * scale;
 }

 protected static vector CrossProduct(vector first, vector second)
 {
  return Vector(first[1] * second[2] - first[2] * second[1], first[2] * second[0] - first[0] * second[2], first[0] * second[1] - first[1] * second[0]);
 }

 // Keeps the surface on the screen when the board is moved, turned or scaled (Game Master).
 protected void PlaceSurface(bool force)
 {
  IEntity owner = GetOwner();
  if (!m_Surface || !owner)
   return;
  vector origin = owner.GetOrigin();
  vector angles = owner.GetAngles();
  float ownerScale = owner.GetScale();
  if (!force && origin == m_vSurfacePlacedOrigin && angles == m_vSurfacePlacedAngles && ownerScale == m_fSurfacePlacedScale)
   return;
  vector transform[4];
  float scale;
  ComputeSurfaceTransform(transform, scale);
  m_Surface.SetTransform(transform);
  m_Surface.SetScale(scale);
  m_Surface.Update();
  m_vSurfacePlacedOrigin = origin;
  m_vSurfacePlacedAngles = angles;
  m_fSurfacePlacedScale = ownerScale;
  if (!force)
   Trace("surface-move", string.Format("origin=%1 scale=%2", transform[3], scale));
 }

 protected void DeleteSurface()
 {
  if (!m_Surface)
   return;
  if (m_wSurfaceRT && !m_Surface.IsDeleted())
   m_wSurfaceRT.RemoveRenderTarget(m_Surface);
  delete m_Surface;
  m_Surface = null;
 }

 // The mesh samples the render target through the screen's UV rectangle, turned a quarter
 // (projector: shown clockwise; WallMap_01: anticlockwise). Surface path: the content frame
 // covers the surface quad's upright UV rectangle, unturned. Direct path: the content frame
 // covers the screen's rectangle and every element is drawn pre-turned the other way
 // (ToSurface). Hand-off path: the content fills the upright canvas, shown pre-turned by EBM_Screen.
 protected void LayoutScreen()
 {
  int size = m_iRtSize;
  if (size < 64)
   size = 64;
  float spanU = Math.AbsFloat(m_fScreenUMax - m_fScreenUMin) * size;
  float spanV = Math.AbsFloat(m_fScreenVMax - m_fScreenVMin) * size;
  float centerX = (m_fScreenUMin + m_fScreenUMax) * 0.5 * size;
  float centerY = (m_fScreenVMin + m_fScreenVMax) * 0.5 * size;
  if (m_iScreenTurn != 0)
  {
   m_iRtWidth = Math.Round(spanV);
   m_iRtHeight = Math.Round(spanU);
  }
  else
  {
   m_iRtWidth = Math.Round(spanU);
   m_iRtHeight = Math.Round(spanV);
  }
  // The layout root lives in the workspace and is stretched there; only render targets are sized.
  FrameSlot.SetSize(m_wRT, size, size);
  if (m_bSurfaceActive)
  {
   int surfaceSize = m_iSurfaceRtSize;
   if (surfaceSize < 64)
    surfaceSize = 64;
   m_iDrawTurn = 0;
   m_iRtWidth = Math.Round(Math.AbsFloat(m_fSurfaceUMax - m_fSurfaceUMin) * surfaceSize);
   m_iRtHeight = Math.Round(Math.AbsFloat(m_fSurfaceVMax - m_fSurfaceVMin) * surfaceSize);
   if (m_iRtWidth < 16)
    m_iRtWidth = 16;
   if (m_iRtHeight < 16)
    m_iRtHeight = 16;
   FrameSlot.SetSize(m_wSurfaceRT, surfaceSize, surfaceSize);
   PlaceFrame(m_wContent, Math.Min(m_fSurfaceUMin, m_fSurfaceUMax) * surfaceSize, Math.Min(m_fSurfaceVMin, m_fSurfaceVMax) * surfaceSize, m_iRtWidth, m_iRtHeight);
   LayoutTitle();
   return;
  }
  if (m_iRtWidth < 16)
   m_iRtWidth = 16;
  if (m_iRtHeight < 16)
   m_iRtHeight = 16;
  if (m_bHandoffActive)
  {
   m_iDrawTurn = 0;
   FrameSlot.SetSize(m_wCanvas, m_iRtWidth, m_iRtHeight);
   PlaceFrame(m_wContent, 0, 0, m_iRtWidth, m_iRtHeight);
   if (m_iCanvasMaxFps > 0)
    m_wCanvas.SetMaxFPS(m_iCanvasMaxFps);
   m_wScreen.SetImageTexture(0, m_wCanvas);
   m_wScreen.SetImage(0);
   m_wScreen.SetSize(m_iRtWidth, m_iRtHeight);
   m_wScreen.SetPivot(0.5, 0.5);
   m_wScreen.SetRotation(m_iScreenTurn * 90);
   FrameSlot.SetPos(m_wScreen, centerX - m_iRtWidth * 0.5, centerY - m_iRtHeight * 0.5);
   if (TraceEnabled())
   {
    int textureWidth, textureHeight;
    m_wScreen.GetImageSize(0, textureWidth, textureHeight);
    Trace("handoff", string.Format("canvasTexture=%1x%2 (0x0: the image got no canvas texture and draws its plain colour)", textureWidth, textureHeight));
   }
  }
  else
  {
   m_iDrawTurn = m_iScreenTurn;
   float frameWidth = m_iRtWidth;
   float frameHeight = m_iRtHeight;
   if (m_iDrawTurn != 0)
   {
    frameWidth = m_iRtHeight;
    frameHeight = m_iRtWidth;
   }
   PlaceFrame(m_wContent, centerX - frameWidth * 0.5, centerY - frameHeight * 0.5, frameWidth, frameHeight);
  }
  LayoutTitle();
 }

 // Fixed top-left anchoring, then position and size (parent: a render target).
 protected static void PlaceFrame(Widget widget, float left, float top, float width, float height)
 {
  FrameSlot.SetAnchorMin(widget, 0, 0);
  FrameSlot.SetAnchorMax(widget, 0, 0);
  FrameSlot.SetAlignment(widget, 0, 0);
  FrameSlot.SetPos(widget, left, top);
  FrameSlot.SetSize(widget, width, height);
 }

 // Title box along the board's top edge, turned with the drawing about its own centre.
 protected void LayoutTitle()
 {
  if (!m_wTitle)
   return;
  float width = m_iRtWidth - 2 * TITLE_LEFT;
  float centerX, centerY;
  ToSurface(TITLE_LEFT + width * 0.5, TITLE_TOP + TITLE_HEIGHT * 0.5, centerX, centerY);
  FrameSlot.SetSize(m_wTitle, width, TITLE_HEIGHT);
  FrameSlot.SetPos(m_wTitle, centerX - width * 0.5, centerY - TITLE_HEIGHT * 0.5);
  m_wTitle.SetPivot(0.5, 0.5);
  m_wTitle.SetRotation(DrawDegrees());
 }

 protected void DestroyBoardWidgets()
 {
  ReleaseEngineMap();
  if (m_bMapHooks)
  {
   SCR_MapEntity.GetOnMapInit().Remove(EBM_OnLocalMapInit);
   SCR_MapEntity.GetOnMapClose().Remove(EBM_OnLocalMapClose);
   m_bMapHooks = false;
  }
  IEntity owner = GetOwner();
  if (m_wRT && owner && !owner.IsDeleted()) m_wRT.RemoveRenderTarget(owner);
  DeleteSurface();
  if (m_wRoot)
  {
   m_wRoot.RemoveFromHierarchy();
   Trace("destroy", string.Empty);
  }
  m_wRoot = null;
  m_wRT = null;
  m_wCanvas = null;
  m_wScreen = null;
  m_wSurfaceRT = null;
  m_wContent = null;
  m_wEngineMapSlot = null;
  m_EngineMapConfig = null;
  m_bEngineMapFailed = false;
  m_bHandoffActive = false;
  m_bSurfaceActive = false;
  m_BoundObject = null;
  m_iRebinds = 0;
  m_sLastPath = string.Empty;
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

 // World map coordinates (x east, y north) to board layout units (upright board, y down).
 protected void ToBoard(float worldX, float worldY, out float boardX, out float boardY)
 {
  boardX = m_iRtWidth * 0.5 + (worldX - m_fCenterX) * m_fScale;
  boardY = m_iRtHeight * 0.5 - (worldY - m_fCenterY) * m_fScale;
 }

 // Board units to the content frame's own units: the board turned a quarter against the
 // mesh (-1: anticlockwise, the projector; +1: clockwise, the wall map), so it reads upright.
 protected void ToSurface(float boardX, float boardY, out float surfaceX, out float surfaceY)
 {
  if (m_iDrawTurn < 0)
  {
   surfaceX = boardY;
   surfaceY = m_iRtWidth - boardX;
  }
  else if (m_iDrawTurn > 0)
  {
   surfaceX = m_iRtHeight - boardY;
   surfaceY = boardX;
  }
  else
  {
   surfaceX = boardX;
   surfaceY = boardY;
  }
 }

 // Widget rotation (degrees, clockwise) that goes with ToSurface.
 protected float DrawDegrees()
 {
  return m_iDrawTurn * 90.0;
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
   // The map image turns about its centre, so its unturned and turned boxes share it.
   float left, top;
   ToBoard(mapX, mapY + mapHeight, left, top);
   float width = mapWidth * m_fScale;
   float height = mapHeight * m_fScale;
   float centerX, centerY;
   ToSurface(left + width * 0.5, top + height * 0.5, centerX, centerY);
   m_wMap.SetSize(width, height);
   m_wMap.SetPivot(0.5, 0.5);
   m_wMap.SetRotation(DrawDegrees());
   FrameSlot.SetPos(m_wMap, centerX - width * 0.5, centerY - height * 0.5);
  }
  UpdateEngineMap();
  RedrawLines();
  RedrawMarkers();
  TracePath();
 }

 //------------------------------------------------------------------------------------------------
 // Diagnostics (opt-in: m_bDebugTrace or -ebmDiagnostics 1)
 //------------------------------------------------------------------------------------------------
 protected bool TraceEnabled()
 {
  return EBM_Diagnostics.Enabled(m_bDebugTrace);
 }

 protected void Trace(string action, string detail)
 {
  if (TraceEnabled())
   EBM_Diagnostics.Event(action, GetOwner(), detail);
 }

 // Logs the drawing path when it changes (not every redraw).
 protected void TracePath()
 {
  if (!TraceEnabled())
   return;
  // path=engine: the game's map view (roads, buildings, names) over the map image; path=raster: the map image only.
  string path = "raster";
  if (m_wEngineMap)
   path = "engine";
  string blocker = EngineMapBlocker();
  string detail = string.Format("path=%1 mode=%2 rasterLoaded=%3 engineBlocker=%4 briefer=%5 view=%6 markers=%7 lines=%8", path, DrawMode(), m_bMapImageLoaded, blocker, m_iBriefer, m_aView.Count(), m_aMarkerWidgets.Count(), m_aLineWidgets.Count());
  if (path + blocker == m_sLastPath)
   return;
  m_sLastPath = path + blocker;
  Trace("path", detail);
 }

 //------------------------------------------------------------------------------------------------
 // Game map renderer (roads, buildings, names, contours)
 //------------------------------------------------------------------------------------------------
 // Why the game map renderer cannot draw on this board now; empty when it can.
 protected string EngineMapBlocker()
 {
  if (!m_bUseEngineMap)
   return "disabled";
  if (m_bEngineMapFailed)
   return "failed";
  if (!m_wEngineMapSlot)
   return "no-slot";
  // A map widget cannot be turned: only the hand-off canvas or an unturned screen shows it upright.
  if (m_iDrawTurn != 0)
   return "turned-screen";
  SCR_MapEntity mapEntity = SCR_MapEntity.GetMapInstance();
  if (!mapEntity)
   return "no-map-entity";
  if (mapEntity.IsOpen())
   return "player-map-open";
  if (mapEntity.GetMapSizeX() <= 0 || mapEntity.GetMapSizeY() <= 0)
   return "no-map-size";
  if (s_EngineMapOwner && s_EngineMapOwner != this)
   return "other-board";
  return string.Empty;
 }

 protected bool EngineMapAllowed()
 {
  if (!m_bUseEngineMap || m_bEngineMapFailed || !m_wEngineMapSlot || m_iDrawTurn != 0)
   return false;
  SCR_MapEntity mapEntity = SCR_MapEntity.GetMapInstance();
  if (!mapEntity || mapEntity.IsOpen())
   return false;
  return mapEntity.GetMapSizeX() > 0 && mapEntity.GetMapSizeY() > 0;
 }

 protected static ResourceName ResolveMapConfig()
 {
  BaseGameMode gameMode = GetGame().GetGameMode();
  if (gameMode)
  {
   SCR_MapConfigComponent configComponent = SCR_MapConfigComponent.Cast(gameMode.FindComponent(SCR_MapConfigComponent));
   if (configComponent && !configComponent.GetGadgetMapConfig().IsEmpty())
    return configComponent.GetGadgetMapConfig();
  }
  return DEFAULT_MAP_CONFIG;
 }

 // Draws the native map into this board's map widget with the same transform as ToBoard
 // (scale and centre), styled by the scenario's gadget map config. The map widget only
 // exists while this board drives the native map, so it never competes with a map the
 // player opens; the plain map image under it stays as the fallback.
 protected void UpdateEngineMap()
 {
  if (!EngineMapAllowed() || (s_EngineMapOwner && s_EngineMapOwner != this))
  {
   ReleaseEngineMap();
   return;
  }
  SCR_MapEntity mapEntity = SCR_MapEntity.GetMapInstance();
  if (!m_EngineMapConfig)
   m_EngineMapConfig = mapEntity.EBM_CreateBoardConfig(ResolveMapConfig(), m_wRoot);
  if (!m_wEngineMap && m_EngineMapConfig)
  {
   WorkspaceWidget workspace = GetGame().GetWorkspace();
   Widget created;
   if (workspace)
    created = workspace.CreateWidgets(ENGINE_MAP_LAYOUT, m_wEngineMapSlot);
   m_wEngineMap = CanvasWidget.Cast(created);
   if (created && !m_wEngineMap)
    created.RemoveFromHierarchy();
   DisableCursor(m_wEngineMap);
  }
  if (!m_wEngineMap || !m_EngineMapConfig)
  {
   Print("EXPBG Briefing: the board cannot create its map view; showing the plain map image", LogLevel.WARNING);
   m_bEngineMapFailed = true;
   ReleaseEngineMap();
   return;
  }
  s_EngineMapOwner = this;
  if (!m_bEngineMapLayersReady)
  {
   mapEntity.EBM_InitBoardLayers(m_EngineMapConfig);
   m_bEngineMapLayersReady = true;
  }
  m_wEngineMap.SetSizeInUnits(Vector(mapEntity.GetMapSizeX(), mapEntity.GetMapSizeY(), 0));
  float screenWidth, screenHeight;
  m_wEngineMap.GetScreenSize(screenWidth, screenHeight);
  float widgetPixelPerUnit = m_wEngineMap.PixelPerUnit();
  if (screenWidth <= 0 || screenHeight <= 0 || widgetPixelPerUnit <= 0)
  {
   m_iEngineMapRetries++;
   if (m_iEngineMapRetries <= ENGINE_MAP_RETRIES)
   {
    EBM_OnStateRpl();
    return;
   }
   Print("EXPBG Briefing: the board's map view never got a size; showing the plain map image", LogLevel.WARNING);
   m_bEngineMapFailed = true;
   ReleaseEngineMap();
   return;
  }
  m_iEngineMapRetries = 0;
  // ToBoard in the map widget's own pixels: screen = k * board units, k = widget width / canvas width.
  float pixelsPerMeter = m_fScale * screenWidth / m_iRtWidth;
  vector offset = mapEntity.Offset();
  // As SCR_MapEntity.SetZoom, and the inverse of SCR_MapEntity.WorldToScreen for the pan.
  mapEntity.ZoomChange(pixelsPerMeter / widgetPixelPerUnit);
  float panX = screenWidth * 0.5 - (m_fCenterX - offset[0]) * pixelsPerMeter;
  float panY = screenHeight * 0.5 - (mapEntity.GetMapSizeY() - m_fCenterY + offset[2]) * pixelsPerMeter;
  mapEntity.PosChange(panX, panY);
  float halfWidth = m_iRtWidth * 0.5 / m_fScale;
  float halfHeight = m_iRtHeight * 0.5 / m_fScale;
  mapEntity.SetFrame(Vector(m_fCenterX - halfWidth, 0, m_fCenterY - halfHeight), Vector(m_fCenterX + halfWidth, 0, m_fCenterY + halfHeight));
  SelectEngineMapLayer(mapEntity, m_fLayerReferenceHeight / (halfHeight * 2));
  mapEntity.EnableGrid(true);
  mapEntity.EnableVisualisation(true);
  mapEntity.EBM_MarkNativeDirty();
 }

 // The detail layer a briefer screen would get for this frame (vanilla AssignViewLayer rule:
 // the first layer whose ceiling the zoom reaches).
 protected static void SelectEngineMapLayer(SCR_MapEntity mapEntity, float pixelsPerMeter)
 {
  int count = mapEntity.LayerCount();
  if (count <= 0)
   return;
  int chosen = count - 1;
  for (int i = 0; i < count; i++)
  {
   MapLayer mapLayer = mapEntity.GetLayer(i);
   if (mapLayer && pixelsPerMeter >= mapLayer.GetCeiling())
   {
    chosen = i;
    break;
   }
  }
  if (mapEntity.GetLayerIndex() != chosen)
   mapEntity.SetLayer(chosen);
 }

 // Removes this board's map widget; the board that drove the native map switches its
 // drawing off again unless a map of the player is open (that map owns it then).
 protected void ReleaseEngineMap()
 {
  if (m_wEngineMap)
   m_wEngineMap.RemoveFromHierarchy();
  m_wEngineMap = null;
  m_iEngineMapRetries = 0;
  m_bEngineMapLayersReady = false;
  if (s_EngineMapOwner != this)
   return;
  s_EngineMapOwner = null;
  SCR_MapEntity mapEntity = SCR_MapEntity.GetMapInstance();
  if (mapEntity && !mapEntity.IsOpen())
   mapEntity.EnableVisualisation(false);
 }

 // A map of the player opens (gadget, editor, deploy screen): give the native map back at
 // once. Vanilla re-initialises the layers and switches drawing on once that map is ready.
 protected void EBM_OnLocalMapInit(MapConfiguration config)
 {
  bool wasDriving = s_EngineMapOwner == this;
  ReleaseEngineMap();
  if (!wasDriving)
   return;
  SCR_MapEntity mapEntity = SCR_MapEntity.GetMapInstance();
  if (mapEntity)
   mapEntity.EnableVisualisation(false);
 }

 // Queued, so the board takes the native map back after CloseMap has finished.
 protected void EBM_OnLocalMapClose(MapConfiguration config)
 {
  EBM_OnStateRpl();
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
   // the map places it: root at the start point, image rotated in screen space (y down) and stretched,
   // plus the drawing turn on a turned screen.
   Widget lineRoot = workspace.CreateWidgets(LINE_LAYOUT, m_wLineLayer);
   if (!lineRoot) continue;
   ImageWidget lineImage = ImageWidget.Cast(lineRoot.FindAnyWidget("DrawLineImage"));
   if (lineImage)
   {
    lineImage.SetRotation(Math.Atan2(deltaY, deltaX) * Math.RAD2DEG + DrawDegrees());
    lineImage.SetSize(length, LINE_THICKNESS);
   }
   float surfaceX, surfaceY;
   ToSurface(startX, startY, surfaceX, surfaceY);
   FrameSlot.SetPos(lineRoot, surfaceX, surfaceY);
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
   float surfaceX, surfaceY;
   ToSurface(boardX, boardY, surfaceX, surfaceY);
   FrameSlot.SetPos(markerWidget, surfaceX, surfaceY);
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
   TurnLeaves(markerWidget, DrawDegrees());
   m_aMarkerWidgets.Insert(markerWidget);
   m_aMarkerObjects.Insert(marker);
   m_aMarkerSlots.Insert(i);
  }
 }

 // Frames cannot be turned, so a marker is turned piece by piece: every image and text about
 // its own centre (icons and labels read upright; a label keeps its unturned offset).
 protected static void TurnLeaves(Widget widget, float degrees)
 {
  if (!widget || degrees == 0)
   return;
  ImageWidget image = ImageWidget.Cast(widget);
  if (image)
  {
   image.SetPivot(0.5, 0.5);
   image.SetRotation(image.GetRotation() + degrees);
  }
  TextWidget text = TextWidget.Cast(widget);
  if (text)
  {
   text.SetPivot(0.5, 0.5);
   text.SetRotation(text.GetRotation() + degrees);
  }
  Widget child = widget.GetChildren();
  while (child)
  {
   TurnLeaves(child, degrees);
   child = child.GetSibling();
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
