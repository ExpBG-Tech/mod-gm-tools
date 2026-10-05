// Client side of one briefing: opens the local fullscreen map, samples the briefer's
// visible frame, own static markers and drawn lines, and streams changes to the server
// through the player's own controller. Closing the map ends streaming and frees the board.
class EBM_BriefingClient
{
 static const int TICK_MS = 200;
 static const int KEEPALIVE_MS = 3000;
 static const int OPEN_TIMEOUT_MS = 6000;
 static const int LOCK_GRACE_MS = 3000;
 protected static ref EBM_BriefingClient s_Active;

 protected RplId m_BoardId;
 protected EBM_BriefingBoardComponent m_Board;
 protected bool m_bSeenOpen;
 protected bool m_bOwnsMenu;
 protected bool m_bFinished;
 protected int m_iStartTick;
 protected int m_iLastSendTick;
 protected string m_sLastSignature;

 static bool IsActive()
 {
  return s_Active && !s_Active.m_bFinished;
 }

 static void Begin(RplId boardId)
 {
  EBM_BriefingBoardComponent board = EBM_BriefingBoardComponent.FromRplId(boardId);
  if (!board) return;
  if (IsActive())
  {
   if (s_Active.m_Board == board) return;
   s_Active.Finish(true);
  }
  s_Active = new EBM_BriefingClient();
  s_Active.Start(boardId, board);
 }

 static void OnBoardDeleted(EBM_BriefingBoardComponent board)
 {
  if (IsActive() && s_Active.m_Board == board) s_Active.Finish(false);
 }

 protected void Start(RplId boardId, EBM_BriefingBoardComponent board)
 {
  m_BoardId = boardId;
  m_Board = board;
  m_iStartTick = System.GetTickCount();
  SCR_MapEntity.GetOnMapClose().Insert(OnMapClose);
  OpenMap();
  GetGame().GetCallqueue().CallLater(Tick, TICK_MS, true);
 }

 // Uses the player's map gadget when carried (vanilla open/close flow); otherwise opens the map menu directly.
 protected void OpenMap()
 {
  SCR_MapEntity mapEntity = SCR_MapEntity.GetMapInstance();
  if (mapEntity && mapEntity.IsOpen())
  {
   m_bSeenOpen = true;
   return;
  }
  IEntity controlled = SCR_PlayerController.GetLocalControlledEntity();
  SCR_GadgetManagerComponent gadgets = SCR_GadgetManagerComponent.GetGadgetManager(controlled);
  IEntity mapGadget;
  if (gadgets) mapGadget = gadgets.GetGadgetByType(EGadgetType.MAP);
  if (mapGadget)
  {
   gadgets.SetGadgetMode(mapGadget, EGadgetMode.IN_HAND);
   return;
  }
  MenuManager menus = GetGame().GetMenuManager();
  if (!menus) return;
  menus.OpenMenu(ChimeraMenuPreset.MapMenu);
  m_bOwnsMenu = true;
  InputManager input = GetGame().GetInputManager();
  if (!input) return;
  input.AddActionListener("MapEscape", EActionTrigger.DOWN, OnCloseInput);
  input.AddActionListener("GadgetMap", EActionTrigger.DOWN, OnCloseInput);
 }

 protected void OnCloseInput(float value, EActionTrigger reason)
 {
  MenuManager menus = GetGame().GetMenuManager();
  if (menus) menus.CloseMenuByPreset(ChimeraMenuPreset.MapMenu);
 }

 protected void OnMapClose(MapConfiguration config)
 {
  if (m_bFinished) return;
  if (m_bSeenOpen) SendState(true);
  Finish(true);
 }

 protected void Tick()
 {
  if (m_bFinished) return;
  if (!m_Board)
  {
   Finish(false);
   return;
  }
  // The server may have released the lock (distance, timeout); stop once replication had time to arrive.
  if (System.GetTickCount() - m_iStartTick > LOCK_GRACE_MS && m_Board.GetBrieferId() != SCR_PlayerController.GetLocalPlayerId())
  {
   Finish(false);
   return;
  }
  SCR_MapEntity mapEntity = SCR_MapEntity.GetMapInstance();
  bool mapOpen = mapEntity && mapEntity.IsOpen();
  if (!mapOpen)
  {
   if (m_bSeenOpen || System.GetTickCount() - m_iStartTick > OPEN_TIMEOUT_MS) Finish(true);
   return;
  }
  MapConfiguration config = mapEntity.GetMapConfig();
  if (!config || config.MapEntityMode != EMapEntityMode.FULLSCREEN) return;
  m_bSeenOpen = true;
  SendState(false);
 }

 protected void SendState(bool force)
 {
  SCR_MapEntity mapEntity = SCR_MapEntity.GetMapInstance();
  SCR_PlayerController controller = SCR_PlayerController.Cast(GetGame().GetPlayerController());
  if (!mapEntity || !controller) return;
  array<float> view = {};
  array<int> markers = {};
  array<string> texts = {};
  array<float> lines = {};
  vector frameMin, frameMax;
  mapEntity.GetMapVisibleFrame(frameMin, frameMax);
  view.Insert(frameMin[0]);
  view.Insert(frameMin[2]);
  view.Insert(frameMax[0]);
  view.Insert(frameMax[2]);
  CollectMarkers(controller.GetPlayerId(), markers, texts);
  SCR_MapDrawingUI drawing = SCR_MapDrawingUI.Cast(mapEntity.GetMapUIComponent(SCR_MapDrawingUI));
  if (drawing) drawing.EBM_CollectLines(lines, EBM_BriefingBoardComponent.MAX_LINES);
  string signature = Signature(view, markers, texts, lines);
  int now = System.GetTickCount();
  if (!force && signature == m_sLastSignature && now - m_iLastSendTick < KEEPALIVE_MS) return;
  m_sLastSignature = signature;
  m_iLastSendTick = now;
  controller.EBM_SendBoardState(m_BoardId, view, markers, texts, lines);
 }

 // Only the briefer's own static markers (local and shared) are streamed.
 protected static void CollectMarkers(int playerId, notnull array<int> markers, notnull array<string> texts)
 {
  SCR_MapMarkerManagerComponent manager = SCR_MapMarkerManagerComponent.GetInstance();
  if (!manager || playerId <= 0) return;
  array<SCR_MapMarkerBase> staticMarkers = manager.GetStaticMarkers();
  if (!staticMarkers) return;
  int position[2];
  foreach (SCR_MapMarkerBase marker : staticMarkers)
  {
   if (!marker || marker.GetMarkerOwnerID() != playerId) continue;
   if (texts.Count() >= EBM_BriefingBoardComponent.MAX_MARKERS) break;
   marker.GetWorldPos(position);
   markers.Insert(marker.GetType());
   markers.Insert(marker.GetMarkerConfigID());
   markers.Insert(marker.GetMarkerOwnerID());
   markers.Insert(marker.GetFlags());
   markers.Insert(position[0]);
   markers.Insert(position[1]);
   markers.Insert(marker.GetRotation());
   markers.Insert(marker.GetColorEntry());
   markers.Insert(marker.GetIconEntry());
   string text = marker.GetCustomText();
   if (text.Length() > EBM_BriefingBoardComponent.TEXT_LIMIT) text = text.Substring(0, EBM_BriefingBoardComponent.TEXT_LIMIT);
   texts.Insert(text);
  }
 }

 // View is compared at 1 m so map jitter does not flood the network.
 protected static string Signature(array<float> view, array<int> markers, array<string> texts, array<float> lines)
 {
  string signature;
  foreach (float viewValue : view)
  {
   signature += Math.Round(viewValue).ToString() + ",";
  }
  signature += "|";
  foreach (int markerValue : markers)
  {
   signature += markerValue.ToString() + ",";
  }
  signature += "|";
  foreach (string textValue : texts)
  {
   signature += textValue + ";";
  }
  signature += "|";
  foreach (float lineValue : lines)
  {
   signature += Math.Round(lineValue).ToString() + ",";
  }
  return signature;
 }

 protected void Finish(bool release)
 {
  if (m_bFinished) return;
  m_bFinished = true;
  GetGame().GetCallqueue().Remove(Tick);
  SCR_MapEntity.GetOnMapClose().Remove(OnMapClose);
  if (m_bOwnsMenu)
  {
   InputManager input = GetGame().GetInputManager();
   if (input)
   {
    input.RemoveActionListener("MapEscape", EActionTrigger.DOWN, OnCloseInput);
    input.RemoveActionListener("GadgetMap", EActionTrigger.DOWN, OnCloseInput);
   }
  }
  if (!release) return;
  SCR_PlayerController controller = SCR_PlayerController.Cast(GetGame().GetPlayerController());
  if (controller) controller.EBM_ReleaseBoard(m_BoardId);
 }
}
