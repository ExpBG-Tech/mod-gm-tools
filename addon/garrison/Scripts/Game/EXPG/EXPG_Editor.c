// Original integration using vanilla editor containers, prefab catalog and budgets.
// No global placement listener: only the browser ticket can enter this RPC path.
class EXPG_EditorTicket
{
 protected int m_Nonce;
 protected float m_Expires;

 void Start(int nonce, float now) { m_Nonce = nonce; m_Expires = now + 120; }
 bool Matches(int nonce, float now) { return nonce > 0 && m_Nonce == nonce && now < m_Expires; }
 void Cancel(int nonce) { if (m_Nonce == nonce) m_Nonce = 0; }
 bool Consume(int nonce, float now)
 {
  if (!Matches(nonce, now)) return false;
  m_Nonce = 0;
  return true;
 }
}

// Local feedback for the acting Game Master only. Profiles can disable hints, so
// every line also enters the local chat history (vanilla system-message style).
class EXPG_Feedback
{
 static void Show(string message)
 {
  Print("[EXPG GARRISON] " + message);
  SCR_HintManagerComponent.ShowCustomHint(message, "EXPBG Garrison", 8);
  SCR_ChatPanelManager chat = SCR_ChatPanelManager.GetInstance();
  if (chat) chat.ShowHelpMessage("EXPBG Garrison: " + message);
 }
}

// Server: one Game Master's EXPBG Add Garrison request waiting for the building's
// plan (EXPG_GarrisonManager.Wait). Its events become owner RPCs of the picker.
class EXPG_PickerWaiter : EXPG_PlanWaiter
{
 SCR_PlacingEditorComponent Picker;
 int Nonce;

 override void OnProgress(int percent, bool queued)
 {
  if (Picker) Picker.EXPG_WaitProgress(Nonce, percent, queued);
 }

 override bool OnReady()
 {
  if (!Picker)
   return true;
  return Picker.EXPG_WaitReady(Nonce);
 }

 override void OnFailed(string reason)
 {
  if (Picker) Picker.EXPG_WaitFailed(Nonce, reason);
 }
}

[BaseContainerProps(), SCR_BaseContainerCustomTitleUIInfo("m_Info")]
class EXPG_AddGarrisonContextAction : SCR_BaseContextAction
{
 [Attribute(desc: "Infantry squad browser")]
 protected ref SCR_EditorContentBrowserDisplayConfig m_Browser;

 protected SCR_DestructibleBuildingEntity FindBuilding(vector position)
 {
  SCR_ManualCamera camera = SCR_CameraEditorComponent.GetCameraInstance();
  if (!camera) return null;
  TraceParam trace = new TraceParam();
  trace.Start = camera.GetOrigin();
  vector direction = position - trace.Start;
  trace.End = position + direction.Normalized() * 0.25;
  trace.Flags = TraceFlags.ENTS;
  GetGame().GetWorld().TraceMove(trace, null);
  IEntity hit = trace.TraceEnt;
  while (hit)
  {
   SCR_DestructibleBuildingEntity building = SCR_DestructibleBuildingEntity.Cast(hit);
   if (building) return building;
   hit = hit.GetParent();
  }
  return null;
 }

 override bool CanBeShown(SCR_EditableEntityComponent hoveredEntity, notnull set<SCR_EditableEntityComponent> selectedEntities, vector cursorWorldPosition, int flags)
 {
  SCR_EditorManagerEntity editor = SCR_EditorManagerEntity.GetInstance();
  return editor && !editor.IsLimited() && editor.GetCurrentMode() == EEditorMode.EDIT && FindBuilding(cursorWorldPosition) != null;
 }

 override bool CanBePerformed(SCR_EditableEntityComponent hoveredEntity, notnull set<SCR_EditableEntityComponent> selectedEntities, vector cursorWorldPosition, int flags)
 {
  return CanBeShown(hoveredEntity, selectedEntities, cursorWorldPosition, flags);
 }

 override void Perform(SCR_EditableEntityComponent hoveredEntity, notnull set<SCR_EditableEntityComponent> selectedEntities, vector cursorWorldPosition, int flags, int param = -1)
 {
  SCR_DestructibleBuildingEntity building = FindBuilding(cursorWorldPosition);
  if (!building) { EXPG_Feedback.Show("Picker not opened: no building under the cursor. Right-click the building again."); return; }
  SCR_PlacingEditorComponent placing = SCR_PlacingEditorComponent.Cast(SCR_PlacingEditorComponent.GetInstance(SCR_PlacingEditorComponent));
  if (!placing) { EXPG_Feedback.Show("Picker not opened: the editor placing component is unavailable in this mode."); return; }
  placing.EXPG_OpenPicker(building, m_Browser);
 }
}

// Every garrison wakes and its squad returns to normal AI control (for a mission that
// will be saved and loaded without EXPBG GM Tools). Offered on a garrison squad.
[BaseContainerProps(), SCR_BaseContainerCustomTitleUIInfo("m_Info")]
class EXPG_ReleaseAllGarrisonsContextAction : SCR_BaseContextAction
{
 protected static bool IsGarrison(SCR_EditableEntityComponent hoveredEntity)
 {
  if (!hoveredEntity) return false;
  SCR_AIGroup group = SCR_AIGroup.Cast(hoveredEntity.GetOwner());
  return group && group.EXPG_Active;
 }

 override bool CanBeShown(SCR_EditableEntityComponent hoveredEntity, notnull set<SCR_EditableEntityComponent> selectedEntities, vector cursorWorldPosition, int flags)
 {
  SCR_EditorManagerEntity editor = SCR_EditorManagerEntity.GetInstance();
  if (!editor || editor.IsLimited() || editor.GetCurrentMode() != EEditorMode.EDIT) return false;
  return IsGarrison(hoveredEntity);
 }

 // Checked again on the server before Perform; the hovered squad is replicated.
 override bool CanBePerformed(SCR_EditableEntityComponent hoveredEntity, notnull set<SCR_EditableEntityComponent> selectedEntities, vector cursorWorldPosition, int flags)
 {
  return IsGarrison(hoveredEntity);
 }

 override void Perform(SCR_EditableEntityComponent hoveredEntity, notnull set<SCR_EditableEntityComponent> selectedEntities, vector cursorWorldPosition, int flags, int param = -1)
 {
  if (!Replication.IsServer()) return;
  int released = EXPG_GarrisonManager.ReleaseAll("Released by the Game Master (Release All Garrisons)");
  PrintFormat("[EXPG GARRISON] Release All Garrisons: %1 garrisons release their squads to normal AI", released);
 }
}

modded class SCR_PlacingEditorComponent
{
 protected static ref map<EntityID, SCR_PlacingEditorComponent> s_EXPG_Pickers = new map<EntityID, SCR_PlacingEditorComponent>();
 protected ref EXPG_EditorTicket m_EXPG_Ticket = new EXPG_EditorTicket();
 protected IEntity m_EXPG_Building;
 protected EntityID m_EXPG_BuildingID;
 protected int m_EXPG_ServerNonce;
 protected int m_EXPG_LastNonce;
 protected int m_EXPG_ClientNonce;
 protected int m_EXPG_NextNonce;
 protected float m_EXPG_NextPrepare;
 protected bool m_EXPG_Selecting;
 protected EditorBrowserDialogUI m_EXPG_Dialog;
 // Server: this request's wait for the building's plan.
 protected ref EXPG_PickerWaiter m_EXPG_Waiter;
 // Client: the request waiting for the analysis (the picker opens when it is ready).
 protected int m_EXPG_WaitNonce;
 protected IEntity m_EXPG_WaitBuilding;
 protected SCR_EditorContentBrowserDisplayConfig m_EXPG_Browser;
 protected ref SCR_HintUIInfo m_EXPG_WaitHint;
 protected float m_EXPG_WaitHeard;
 protected bool m_EXPG_WaitShown;
 protected bool m_EXPG_WaitQueued;

 protected float EXPG_Now() { return GetGame().GetWorld().GetWorldTime() * 0.001; }

 // The content browser is a dialog: MenuManager.GetTopMenu() keeps returning the
 // editor menu while it is open (observed), so find it by its native preset,
 // as vanilla EditorMenuBase.OpenDialog does. Vanilla opens one browser at a
 // time and clears instant placing when it closes without a pick.
 protected EditorBrowserDialogUI EXPG_FindBrowser()
 {
  return EditorBrowserDialogUI.Cast(GetGame().GetMenuManager().FindMenuByPreset(ChimeraMenuPreset.EditorBrowserDialog));
 }

 protected EditorBrowserDialogUI EXPG_PickerDialog()
 {
  if (!m_EXPG_Dialog && m_EXPG_ClientNonce > 0) m_EXPG_Dialog = EXPG_FindBrowser();
  return m_EXPG_Dialog;
 }

 protected bool EXPG_Authorized() { return EXPG_AuthorizationFailure().IsEmpty(); }

 // Empty when authorized, otherwise the reason. The core's per-player editor
 // registry exists only on the server (vanilla CreateEditorManager/OnGameStart
 // return early on RplMode.Client), so a dedicated-server client must compare
 // with its local editor instance; requiring the registry there failed silently.
 protected string EXPG_AuthorizationFailure()
 {
  SCR_EditorManagerEntity editor = GetManager();
  if (!editor) return "no editor manager";
  if (editor.IsLimited()) return "the editor is limited; full Game Master rights are required";
  if (!editor.IsOpened()) return "the editor is closed";
  if (!editor.HasMode(EEditorMode.EDIT) || editor.GetCurrentMode() != EEditorMode.EDIT) return "the editor is not in Edit mode";
  SCR_EditorManagerCore core = SCR_EditorManagerCore.Cast(SCR_EditorManagerCore.GetInstance(SCR_EditorManagerCore));
  if (!core) return "the editor core is unavailable";
  if (RplSession.Mode() == RplMode.Client)
  {
   if (core.GetEditorManager() != editor) return "the editor is not the local Game Master editor";
  }
  else if (core.GetEditorManager(editor.GetPlayerID()) != editor) return string.Format("the editor is not registered for player %1", editor.GetPlayerID());
  return string.Empty;
 }

 void EXPG_OpenPicker(IEntity building, SCR_EditorContentBrowserDisplayConfig browser)
 {
  string failure = EXPG_AuthorizationFailure();
  if (!building) failure = "the building no longer exists";
  else if (!browser) failure = "the squad browser configuration is missing from the action";
  if (!failure.IsEmpty()) { EXPG_Feedback.Show("Picker not opened: " + failure + "."); return; }
  // A second EXPBG Add Garrison while this Game Master waits for an analysis: on
  // the same building it stops waiting (the server keeps analysing, so asking
  // again continues from there); on another building it switches to that one.
  if (m_EXPG_WaitNonce > 0)
  {
   bool sameBuilding = m_EXPG_WaitBuilding == building;
   EXPG_EndWait(true);
   if (sameBuilding) { EXPG_Feedback.Show("Stopped waiting for the structure analysis. It keeps running on the server; EXPBG Add Garrison on this building again continues from there."); return; }
  }
  SetInstantPlacing(null);
  SetSelectedPrefab(ResourceName.Empty);
  SetPlacingFlag(EEditorPlacingFlags.CHARACTER_PLAYER, false);
  m_EXPG_NextNonce++;
  if (m_EXPG_NextNonce <= 0) m_EXPG_NextNonce = 1;
  // The squad picker opens only once the server reports the building's plan
  // ready (EXPG_OpenOwner, at once for an analysed building); until then the
  // server sends the analysis progress for a hint (EXPG_ProgressOwner).
  m_EXPG_WaitNonce = m_EXPG_NextNonce;
  m_EXPG_WaitBuilding = building;
  m_EXPG_Browser = browser;
  m_EXPG_WaitHeard = EXPG_Now();
  m_EXPG_WaitShown = false;
  m_EXPG_WaitQueued = false;
  m_EXPG_WaitHint = null;
  GetGame().GetCallqueue().Remove(EXPG_WatchWait);
  GetGame().GetCallqueue().CallLater(EXPG_WatchWait, 1000, true);
  RplId buildingRpl = RplId.Invalid();
  RplComponent rpl = RplComponent.Cast(building.FindComponent(RplComponent));
  if (rpl) buildingRpl = rpl.Id();
  Rpc(EXPG_BeginServer, m_EXPG_WaitNonce, buildingRpl, building.GetID(), building.GetOrigin());
 }

 // Client: stop waiting for the analysis (the server's analysis goes on).
 protected void EXPG_EndWait(bool cancelServer)
 {
  GetGame().GetCallqueue().Remove(EXPG_WatchWait);
  if (m_EXPG_WaitNonce <= 0) return;
  if (cancelServer) Rpc(EXPG_CancelServer, m_EXPG_WaitNonce);
  m_EXPG_WaitNonce = 0;
  m_EXPG_WaitBuilding = null;
  if (m_EXPG_WaitHint) SCR_HintManagerComponent.HideHint(m_EXPG_WaitHint);
  m_EXPG_WaitHint = null;
 }

 // Client: the server reports progress at least every two seconds while it
 // analyses; after 15 silent seconds stop waiting.
 protected void EXPG_WatchWait()
 {
  if (m_EXPG_WaitNonce <= 0) { GetGame().GetCallqueue().Remove(EXPG_WatchWait); return; }
  if (EXPG_Now() - m_EXPG_WaitHeard < 15) return;
  EXPG_EndWait(true);
  EXPG_Feedback.Show("No answer from the server about the structure analysis. Use EXPBG Add Garrison again.");
 }

 // Client: why an analysis that finished while the Game Master did something else
 // must not open the picker now (it would cancel his placing or cover a window).
 protected string EXPG_BusyReason()
 {
  if (IsPlacing())
   return "you are placing another entity";
  MenuManager menus = GetGame().GetMenuManager();
  if (menus.IsAnyDialogOpen())
   return "another editor window is open";
  SCR_MenuEditorComponent editorMenu = SCR_MenuEditorComponent.Cast(SCR_MenuEditorComponent.GetInstance(SCR_MenuEditorComponent));
  if (editorMenu && editorMenu.GetMenu() && menus.GetTopMenu() != editorMenu.GetMenu())
   return "a menu is open over the editor";
  return string.Empty;
 }

 override void SetInstantPlacing(SCR_EditorPreviewParams param)
 {
  if (!param && m_EXPG_ClientNonce > 0)
  {
   Rpc(EXPG_CancelServer, m_EXPG_ClientNonce);
   m_EXPG_ClientNonce = 0;
   m_EXPG_Selecting = false;
   m_EXPG_Dialog = null;
  }
  super.SetInstantPlacing(param);
 }

 override bool SetSelectedPrefab(ResourceName prefab = "", bool onConfirm = false, bool showBudgetMaxNotification = true, set<SCR_EditableEntityComponent> recipients = null, SCR_BaseEditorAction sourceAction = null)
 {
  if (m_EXPG_ClientNonce <= 0 || prefab.IsEmpty()) return super.SetSelectedPrefab(prefab, onConfirm, showBudgetMaxNotification, recipients, sourceAction);
  EditorBrowserDialogUI dialog = EXPG_PickerDialog();
  if (sourceAction || recipients || !dialog || EXPG_FindBrowser() != dialog)
  {
   Print(string.Format("[EXPG PICKER] native placement fallback: action=%1 recipients=%2 dialog=%3 browser=%4", sourceAction != null, recipients != null, dialog != null, EXPG_FindBrowser()), LogLevel.WARNING);
   SetInstantPlacing(null);
   return super.SetSelectedPrefab(prefab, onConfirm, showBudgetMaxNotification, recipients, sourceAction);
  }
  if (m_EXPG_Selecting)
  {
   Print("[EXPG GARRISON] squad choice ignored: the server has not answered the previous choice yet");
   return false;
  }
  SCR_PlacingEditorComponentClass data = SCR_PlacingEditorComponentClass.Cast(GetEditorComponentData());
  string failure = EXPG_AuthorizationFailure();
  if (!data) failure = "the editor prefab catalog is unavailable";
  if (!failure.IsEmpty())
  {
   SetInstantPlacing(null);
   EXPG_Feedback.Show("Squad choice cancelled: " + failure + ".");
   return false;
  }
  int prefabID = data.GetPrefabID(prefab);
  if (prefabID < 0)
  {
   EXPG_Feedback.Show("That squad is not in the editor catalog; choose another squad.");
   return false;
  }
  m_EXPG_Selecting = true;
  Rpc(EXPG_SelectServer, m_EXPG_ClientNonce, prefabID);
  return false; // Keep native picker open until server accepts, allowing analysis/budget retries.
 }

 [RplRpc(RplChannel.Reliable, RplRcver.Server)]
 protected void EXPG_BeginServer(int nonce, RplId buildingRpl, EntityID buildingStatic, vector expectedOrigin)
 {
  if (Replication.IsClient()) { Print("[EXPG GARRISON] picker request ignored: received on a client"); return; }
  if (nonce <= m_EXPG_LastNonce) { Print(string.Format("[EXPG GARRISON] picker request ignored: stale request %1 (last %2)", nonce, m_EXPG_LastNonce)); return; }
  string failure = EXPG_AuthorizationFailure();
  if (!failure.IsEmpty()) { EXPG_Reject(nonce, "Game Master access check failed on the server: " + failure + "."); return; }
  m_EXPG_LastNonce = nonce;
  EXPG_ClearServer();
  float now = EXPG_Now();
  if (now < m_EXPG_NextPrepare) { EXPG_Reject(nonce, "Please wait before opening another garrison picker."); return; }
  m_EXPG_NextPrepare = now + 1;
  IEntity building;
  if (buildingRpl.IsValid())
  {
   RplComponent rpl = RplComponent.Cast(Replication.FindItem(buildingRpl));
   if (rpl) building = rpl.GetEntity();
  }
  else building = GetGame().GetWorld().FindEntityByID(buildingStatic);
  if (!SCR_DestructibleBuildingEntity.Cast(building) || building.IsDeleted() || vector.DistanceSq(building.GetOrigin(), expectedOrigin) > 0.25)
  {
   EXPG_Reject(nonce, "The selected building is no longer available.");
   return;
  }
  EXPG_GarrisonManager manager = EXPG_GarrisonManager.Get();
  if (!manager) { EXPG_Reject(nonce, "Garrison manager is unavailable."); return; }
  m_EXPG_Building = building;
  m_EXPG_BuildingID = building.GetID();
  m_EXPG_ServerNonce = nonce;
  // Join (or start) the building's single analysis. EXPG_WaitReady opens the
  // picker (at once when the plan is ready); another Game Master's request for
  // the same building joins the same plan and waits while the first one chooses.
  EXPG_PickerWaiter waiter = new EXPG_PickerWaiter();
  waiter.Structure = building;
  waiter.Picker = this;
  waiter.Nonce = nonce;
  m_EXPG_Waiter = waiter;
  string reason;
  if (!manager.Wait(waiter, reason))
  {
   EXPG_ClearServer();
   EXPG_Reject(nonce, "Squad picker not opened: " + reason + ".");
  }
 }

 // Server, from the building's analysis (EXPG_PickerWaiter): progress for the hint.
 void EXPG_WaitProgress(int nonce, int percent, bool queued)
 {
  if (nonce == m_EXPG_ServerNonce) Rpc(EXPG_ProgressOwner, nonce, percent, queued);
 }

 // Server: the plan is ready. Opens the picker for this Game Master unless another
 // one is choosing for the building (false: keep waiting). The 120 s selection
 // ticket starts now, not when the analysis started.
 bool EXPG_WaitReady(int nonce)
 {
  if (nonce != m_EXPG_ServerNonce || !m_EXPG_Building)
   return true;
  SCR_PlacingEditorComponent other;
  if (s_EXPG_Pickers.Find(m_EXPG_BuildingID, other) && other && other != this)
   return false;
  string failure = EXPG_AuthorizationFailure();
  if (!failure.IsEmpty())
  {
   EXPG_ClearServer();
   EXPG_Reject(nonce, "Squad picker not opened: " + failure + ".");
   return true;
  }
  m_EXPG_Waiter = null;
  m_EXPG_Ticket.Start(nonce, EXPG_Now());
  s_EXPG_Pickers.Set(m_EXPG_BuildingID, this);
  GetGame().GetCallqueue().Remove(EXPG_ExpireServer);
  GetGame().GetCallqueue().CallLater(EXPG_ExpireServer, 120000, false);
  Print(string.Format("[EXPG GARRISON] structure analysis ready; opening the squad picker for request %1", nonce));
  Rpc(EXPG_OpenOwner, nonce);
  return true;
 }

 // Server: the analysis failed or the building is gone; tell the Game Master why.
 void EXPG_WaitFailed(int nonce, string reason)
 {
  if (nonce != m_EXPG_ServerNonce) return;
  m_EXPG_Waiter = null;
  EXPG_ClearServer();
  EXPG_Reject(nonce, "Squad picker not opened: the structure analysis failed: " + reason + ".");
 }

 // Client: analysis progress. The first message (and a change to or from waiting
 // for another Game Master) shows a hint and a chat line; later ones only refresh
 // the hint while it is still on screen (the GM may hide it; other hints win).
 [RplRpc(RplChannel.Reliable, RplRcver.Owner)]
 protected void EXPG_ProgressOwner(int nonce, int percent, bool queued)
 {
  if (nonce <= 0 || nonce != m_EXPG_WaitNonce) return;
  m_EXPG_WaitHeard = EXPG_Now();
  string text = "Analysing building structure... " + percent.ToString() + "%. The squad picker opens by itself when it is done; EXPBG Add Garrison on this building again cancels.";
  if (queued) text = "Structure analysis complete. Another Game Master is choosing a garrison for this building; the squad picker opens when they are done. EXPBG Add Garrison on this building again cancels.";
  SCR_HintManagerComponent hints = SCR_HintManagerComponent.GetInstance();
  if (!m_EXPG_WaitShown || queued != m_EXPG_WaitQueued)
  {
   m_EXPG_WaitShown = true;
   m_EXPG_WaitQueued = queued;
   EXPG_Feedback.Show(text);
   if (hints) m_EXPG_WaitHint = hints.GetCurrentHint();
   return;
  }
  if (!hints || !m_EXPG_WaitHint || hints.GetCurrentHint() != m_EXPG_WaitHint) return;
  SCR_HintManagerComponent.ShowCustomHint(text, "EXPBG Garrison", 8, true);
  m_EXPG_WaitHint = hints.GetCurrentHint();
 }

 // Client: the building's plan is ready; open the squad picker.
 [RplRpc(RplChannel.Reliable, RplRcver.Owner)]
 protected void EXPG_OpenOwner(int nonce)
 {
  if (nonce <= 0 || nonce != m_EXPG_WaitNonce)
  {
   Print(string.Format("[EXPG GARRISON] picker open ignored: request %1 is not the waiting request %2", nonce, m_EXPG_WaitNonce));
   return;
  }
  IEntity building = m_EXPG_WaitBuilding;
  SCR_EditorContentBrowserDisplayConfig browser = m_EXPG_Browser;
  bool waited = m_EXPG_WaitShown;
  EXPG_EndWait(false);
  string failure = EXPG_AuthorizationFailure();
  if (!building) failure = "the building no longer exists";
  else if (!browser) failure = "the squad browser configuration is missing from the action";
  if (!failure.IsEmpty())
  {
   Rpc(EXPG_CancelServer, nonce);
   EXPG_Feedback.Show("Picker not opened: " + failure + ".");
   return;
  }
  // After a wait the Game Master may be busy: never cancel his placing or cover
  // another window. The building is analysed, so asking again opens at once.
  string busy;
  if (waited) busy = EXPG_BusyReason();
  if (!busy.IsEmpty())
  {
   Rpc(EXPG_CancelServer, nonce);
   EXPG_Feedback.Show("Structure analysis complete, but the squad picker was not opened because " + busy + ". EXPBG Add Garrison on this building now opens it at once.");
   return;
  }
  m_EXPG_ClientNonce = nonce;
  SetPlacingFlag(EEditorPlacingFlags.CHARACTER_PLAYER, false);
  vector transform[4];
  building.GetWorldTransform(transform);
  super.SetInstantPlacing(SCR_EditorPreviewParams.CreateParams(transform));
  if (!SCR_ContentBrowserEditorComponent.OpenBrowserLabelConfigInstance(browser))
  {
   // The server holds this request's ticket: SetInstantPlacing(null) cancels it.
   SetInstantPlacing(null);
   EXPG_Feedback.Show("Picker not opened: the editor content browser is unavailable.");
   return;
  }
  m_EXPG_Dialog = EXPG_FindBrowser();
  // Not fatal: EXPG_PickerDialog finds the dialog again on selection.
  if (!m_EXPG_Dialog) Print("[EXPG GARRISON] picker dialog not found right after opening; it will be looked up again on selection", LogLevel.WARNING);
  if (waited) EXPG_Feedback.Show("Structure analysis complete. Choose the squad.");
 }

 [RplRpc(RplChannel.Reliable, RplRcver.Server)]
 protected void EXPG_CancelServer(int nonce)
 {
  if (!Replication.IsClient() && nonce == m_EXPG_ServerNonce) { EXPG_ClearServer(); return; }
  Print(string.Format("[EXPG GARRISON] picker cancel ignored: request %1 is not the active request %2", nonce, m_EXPG_ServerNonce));
 }

 protected void EXPG_ExpireServer()
 {
  Rpc(EXPG_CompleteOwner, m_EXPG_ServerNonce);
  EXPG_ClearServer();
  EXPG_Reply("Garrison selection expired. Open EXPBG Add Garrison again.");
 }

 protected void EXPG_ClearServer()
 {
  GetGame().GetCallqueue().Remove(EXPG_ExpireServer);
  // Stop waiting; the building's analysis itself goes on for other requests.
  if (m_EXPG_Waiter)
  {
   EXPG_GarrisonManager manager = EXPG_GarrisonManager.Get();
   if (manager) manager.StopWaiting(m_EXPG_Waiter);
   m_EXPG_Waiter.Finished = true;
   m_EXPG_Waiter = null;
  }
  SCR_PlacingEditorComponent owner;
  if (s_EXPG_Pickers.Find(m_EXPG_BuildingID, owner) && owner == this) s_EXPG_Pickers.Remove(m_EXPG_BuildingID);
  m_EXPG_Ticket.Cancel(m_EXPG_ServerNonce);
  m_EXPG_ServerNonce = 0;
  m_EXPG_Building = null;
 }

 [RplRpc(RplChannel.Reliable, RplRcver.Server)]
 protected void EXPG_SelectServer(int nonce, int prefabID)
 {
  if (Replication.IsClient()) { Print("[EXPG GARRISON] squad choice ignored: received on a client"); return; }
  if (!m_EXPG_Ticket.Matches(nonce, EXPG_Now())) { EXPG_Reject(nonce, "Garrison selection expired or was cancelled. Open EXPBG Add Garrison again."); return; }
  IEntity building = m_EXPG_Building;
  string failure = EXPG_AuthorizationFailure();
  if (!building || building.IsDeleted()) failure = "the selected building no longer exists";
  if (!failure.IsEmpty())
  {
   EXPG_ClearServer();
   Rpc(EXPG_CompleteOwner, nonce);
   EXPG_Reply("Garrison was not placed: " + failure + ".");
   return;
  }
  SCR_PlacingEditorComponentClass data = SCR_PlacingEditorComponentClass.Cast(GetEditorComponentData());
  if (!data || prefabID < 0) { EXPG_Reply("The editor prefab catalog is unavailable."); return; }
  ResourceName prefab = data.GetPrefab(prefabID);
  if (prefab.IsEmpty()) { EXPG_Reply("That squad is not in the server's editor catalog."); return; }
  Resource resource = Resource.Load(prefab);
  if (!resource || !resource.IsValid()) { EXPG_Reply("That squad prefab could not be loaded."); return; }
  IEntityComponentSource editableSource = SCR_EditableEntityComponentClass.GetEditableEntitySource(resource);
  if (!editableSource || SCR_EditableEntityComponentClass.GetEntityType(editableSource) != EEditableEntityType.GROUP) { EXPG_Reply("Choose an infantry squad."); return; }
  IEntitySource source = resource.GetResource().ToEntitySource();
  array<ResourceName> members = {};
  if (!source || !source.Get("m_aUnitPrefabSlots", members) || members.IsEmpty() || members.Count() > 32) { EXPG_Reply("Choose a verified infantry roster of 1 to 32 soldiers."); return; }
  // No vanilla group prefab carries GROUPTYPE_INFANTRY, so verify the roster
  // itself: every unit slot must be an editable character (as vanilla placing does).
  foreach (ResourceName member : members)
  {
   Resource memberResource = Resource.Load(member);
   IEntityComponentSource memberSource;
   if (memberResource && memberResource.IsValid()) memberSource = SCR_EditableEntityComponentClass.GetEditableEntitySource(memberResource);
   if (!memberSource || SCR_EditableEntityComponentClass.GetEntityType(memberSource) != EEditableEntityType.CHARACTER) { EXPG_Reply("Only infantry squads can garrison a building."); return; }
  }
  EXPG_GarrisonManager manager = EXPG_GarrisonManager.Get();
  string reason;
  if (!manager || !manager.CanFit(building, members.Count(), reason)) { EXPG_Reply("Garrison was not placed. " + reason); return; }
  vector transform[4];
  building.GetWorldTransform(transform);
  EEditableEntityBudget blockingBudget;
  SetPlacingFlag(EEditorPlacingFlags.CHARACTER_PLAYER, false);
  // The building plan validates every post, so skip the preview transform: on
  // sloped ground the building origin can fail the terrain-height check.
  bool placeable = CanCreateEntity();
  bool budget = CanPlaceEntityServer(editableSource, blockingBudget, false, false);
  bool spawnBudget = IsThereEnoughBudgetToSpawn(editableSource);
  if (!placeable || !budget || !spawnBudget)
  {
   Print(string.Format("[EXPG GARRISON] placement refused: placing=%1 budget=%2 spawnBudget=%3 blocking=%4", placeable, budget, spawnBudget, typename.EnumToString(EEditableEntityBudget, blockingBudget)), LogLevel.WARNING);
   EXPG_Reply("The squad exceeds the editor budget or cannot be placed here.");
   return;
  }
  if (!m_EXPG_Ticket.Consume(nonce, EXPG_Now())) { EXPG_Reject(nonce, "Garrison selection expired or was cancelled. Open EXPBG Add Garrison again."); return; }
  EXPG_ClearServer();
  Rpc(EXPG_CompleteOwner, nonce);
  // A squad added to a garrisoned building spawns on open ground beside it, not at
  // the origin, so new soldiers never shove existing guards off their posts.
  if (manager.HasGarrison(building)) transform[3] = EXPG_ReinforcementSpawn(building, transform[3]);
  // Keep the direct reference even if an invalid third-party prefab lacks editable data.
  EntitySpawnParams spawn = new EntitySpawnParams();
  spawn.TransformMode = ETransformMode.WORLD;
  Math3D.MatrixCopy(transform, spawn.Transform);
  OnBeforeEntityCreatedServer(prefab);
  IEntity entity = GetGame().SpawnEntityPrefab(resource, GetGame().GetWorld(), spawn);
  SCR_AIGroup group = SCR_AIGroup.Cast(entity);
  if (!entity) { EXPG_Reply("The engine could not spawn that squad."); return; }
  bool fresh = group && group.EXPG_BeginFreshRoster(members.Count());
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.GetEditableEntity(entity);
  if (!group || !editable || !fresh)
  {
   if (group) group.EXPG_EndFreshRoster();
   EXPG_Reply("The prefab did not create a verified editable squad; inspect the spawned entity."); return;
  }
  int playerId = GetManager().GetPlayerID();
  editable.EOnEditorPlace(null, null, 0, false, playerId);
  editable.SetAuthor(playerId);
  editable.OnCreatedServer(this);
  array<SCR_EditableEntityComponent> created = {editable};
  OnEntityCreatedServer(created);
  // Adding to an already garrisoned building always deploys the whole squad.
  bool reinforcing = manager.HasGarrison(building);
  if (!manager.AdoptFresh(group, building, playerId, members.Count()))
  {
   group.EXPG_EndFreshRoster();
   EXPG_Reply("The squad remains under normal AI control because garrison assignment was refused.");
  }
  else if (reinforcing) EXPG_Reply(string.Format("Garrison reinforcement is deploying: all %1 soldiers join this building's garrison, on free posts first, then on extra positions in and around the building.", members.Count()));
  else EXPG_Reply(string.Format("Garrison is deploying: all %1 soldiers take the building's posts first, then patrol inside, then more watch positions and places close around the building. Nobody is removed.", members.Count()));
  GetOnPlaceEntityServer().Invoke(prefabID, editable, playerId);
 }

 // Dry ground 4 m outside the building's bounds, first of the four sides that is not water.
 protected vector EXPG_ReinforcementSpawn(IEntity building, vector origin)
 {
  vector mins, maxs;
  building.GetWorldBounds(mins, maxs);
  vector center = (mins + maxs) * 0.5;
  array<vector> sides = {Vector(maxs[0] + 4, 0, center[2]), Vector(mins[0] - 4, 0, center[2]), Vector(center[0], 0, maxs[2] + 4), Vector(center[0], 0, mins[2] - 4)};
  foreach (vector side : sides)
  {
   side[1] = GetGame().GetWorld().GetSurfaceY(side[0], side[2]);
   if (!ChimeraWorldUtils.TryGetWaterSurfaceSimple(GetGame().GetWorld(), side + "0 0.5 0")) return side;
  }
  return origin;
 }

 // Every server refusal or result is logged once on the server and once on the owner.
 protected void EXPG_Reply(string message)
 {
  SCR_EditorManagerEntity editor = GetManager();
  int playerId;
  if (editor) playerId = editor.GetPlayerID();
  Print(string.Format("[EXPG GARRISON] server reply to player %1: %2", playerId, message));
  Rpc(EXPG_ReplyOwner, message);
 }

 protected void EXPG_Reject(int nonce, string message)
 {
  Rpc(EXPG_CompleteOwner, nonce);
  EXPG_Reply(message);
 }

 [RplRpc(RplChannel.Reliable, RplRcver.Owner)]
 protected void EXPG_ReplyOwner(string message)
 {
  m_EXPG_Selecting = false;
  EXPG_Feedback.Show(message);
 }

 [RplRpc(RplChannel.Reliable, RplRcver.Owner)]
 protected void EXPG_CompleteOwner(int nonce)
 {
  // A refused or failed request that was still waiting for the analysis.
  if (nonce > 0 && nonce == m_EXPG_WaitNonce) { EXPG_EndWait(false); return; }
  if (nonce != m_EXPG_ClientNonce)
  {
   Print(string.Format("[EXPG GARRISON] picker completion ignored: request %1 is not the open request %2", nonce, m_EXPG_ClientNonce));
   return;
  }
  EditorBrowserDialogUI dialog = EXPG_PickerDialog();
  m_EXPG_ClientNonce = 0;
  m_EXPG_Selecting = false;
  super.SetInstantPlacing(null);
  if (dialog) dialog.Close();
  m_EXPG_Dialog = null;
 }

 override void EOnEditorDeactivate()
 {
  EXPG_EndWait(true);
  SetInstantPlacing(null);
  super.EOnEditorDeactivate();
 }

 override void EOnEditorDeactivateServer()
 {
  EXPG_ClearServer();
  super.EOnEditorDeactivateServer();
 }

 override protected void EOnEditorDeleteServer()
 {
  EXPG_ClearServer();
  super.EOnEditorDeleteServer();
 }
}
