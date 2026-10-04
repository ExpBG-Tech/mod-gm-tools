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
  SCR_PlacingEditorComponent placing = SCR_PlacingEditorComponent.Cast(SCR_PlacingEditorComponent.GetInstance(SCR_PlacingEditorComponent));
  if (building && placing) placing.EXPG_OpenPicker(building, m_Browser);
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

 protected float EXPG_Now() { return GetGame().GetWorld().GetWorldTime() * 0.001; }

 protected bool EXPG_Authorized()
 {
  SCR_EditorManagerEntity editor = GetManager();
  if (!editor || editor.IsLimited() || !editor.IsOpened() || !editor.HasMode(EEditorMode.EDIT) || editor.GetCurrentMode() != EEditorMode.EDIT) return false;
  SCR_EditorManagerCore core = SCR_EditorManagerCore.Cast(SCR_EditorManagerCore.GetInstance(SCR_EditorManagerCore));
  return core && core.GetEditorManager(editor.GetPlayerID()) == editor;
 }

 void EXPG_OpenPicker(IEntity building, SCR_EditorContentBrowserDisplayConfig browser)
 {
  if (!building || !browser || !EXPG_Authorized()) return;
  SetInstantPlacing(null);
  SetSelectedPrefab(ResourceName.Empty);
  SetPlacingFlag(EEditorPlacingFlags.CHARACTER_PLAYER, false);
  m_EXPG_NextNonce++;
  if (m_EXPG_NextNonce <= 0) m_EXPG_NextNonce = 1;
  m_EXPG_ClientNonce = m_EXPG_NextNonce;
  RplId buildingRpl = RplId.Invalid();
  RplComponent rpl = RplComponent.Cast(building.FindComponent(RplComponent));
  if (rpl) buildingRpl = rpl.Id();
  vector transform[4];
  building.GetWorldTransform(transform);
  super.SetInstantPlacing(SCR_EditorPreviewParams.CreateParams(transform));
  if (!SCR_ContentBrowserEditorComponent.OpenBrowserLabelConfigInstance(browser)) SetInstantPlacing(null);
  else
  {
   m_EXPG_Dialog = EditorBrowserDialogUI.Cast(GetGame().GetMenuManager().GetTopMenu());
   Rpc(EXPG_BeginServer, m_EXPG_ClientNonce, buildingRpl, building.GetID(), building.GetOrigin());
  }
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
  if (sourceAction || recipients || !m_EXPG_Dialog || GetGame().GetMenuManager().GetTopMenu() != m_EXPG_Dialog)
  {
   SetInstantPlacing(null);
   return super.SetSelectedPrefab(prefab, onConfirm, showBudgetMaxNotification, recipients, sourceAction);
  }
  if (m_EXPG_Selecting) return false;
  SCR_PlacingEditorComponentClass data = SCR_PlacingEditorComponentClass.Cast(GetEditorComponentData());
  if (!data || !EXPG_Authorized())
  {
   SetInstantPlacing(null);
   return false;
  }
  int prefabID = data.GetPrefabID(prefab);
  if (prefabID < 0) return false;
  m_EXPG_Selecting = true;
  Rpc(EXPG_SelectServer, m_EXPG_ClientNonce, prefabID);
  return false; // Keep native picker open until server accepts, allowing analysis/budget retries.
 }

 [RplRpc(RplChannel.Reliable, RplRcver.Server)]
 protected void EXPG_BeginServer(int nonce, RplId buildingRpl, EntityID buildingStatic, vector expectedOrigin)
 {
  if (Replication.IsClient() || nonce <= m_EXPG_LastNonce) return;
  if (!EXPG_Authorized()) { EXPG_Reject(nonce, "Game Master access is no longer active."); return; }
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
  SCR_PlacingEditorComponent other;
  if (s_EXPG_Pickers.Find(building.GetID(), other) && other && other != this)
  {
   EXPG_Reject(nonce, "Another Game Master is choosing a garrison for this building.");
   return;
  }
  EXPG_GarrisonManager manager = EXPG_GarrisonManager.Get();
  if (!manager) { EXPG_Reject(nonce, "Garrison manager is unavailable."); return; }
  m_EXPG_Building = building;
  m_EXPG_BuildingID = building.GetID();
  m_EXPG_ServerNonce = nonce;
  m_EXPG_Ticket.Start(nonce, now);
  s_EXPG_Pickers.Set(m_EXPG_BuildingID, this);
  manager.Prepare(building);
  GetGame().GetCallqueue().CallLater(EXPG_ExpireServer, 120000, false);
 }

 [RplRpc(RplChannel.Reliable, RplRcver.Server)]
 protected void EXPG_CancelServer(int nonce)
 {
  if (!Replication.IsClient() && nonce == m_EXPG_ServerNonce) EXPG_ClearServer();
 }

 protected void EXPG_ExpireServer()
 {
  Rpc(EXPG_CompleteOwner, m_EXPG_ServerNonce);
  EXPG_ClearServer();
  EXPG_Reply("Garrison selection expired. Open Add Garrison again.");
 }

 protected void EXPG_ClearServer()
 {
  GetGame().GetCallqueue().Remove(EXPG_ExpireServer);
  SCR_PlacingEditorComponent owner;
  if (s_EXPG_Pickers.Find(m_EXPG_BuildingID, owner) && owner == this) s_EXPG_Pickers.Remove(m_EXPG_BuildingID);
  m_EXPG_Ticket.Cancel(m_EXPG_ServerNonce);
  m_EXPG_ServerNonce = 0;
  m_EXPG_Building = null;
 }

 [RplRpc(RplChannel.Reliable, RplRcver.Server)]
 protected void EXPG_SelectServer(int nonce, int prefabID)
 {
  if (Replication.IsClient()) return;
  if (!m_EXPG_Ticket.Matches(nonce, EXPG_Now())) { EXPG_Reject(nonce, "Garrison selection expired or was cancelled. Open Add Garrison again."); return; }
  IEntity building = m_EXPG_Building;
  if (!EXPG_Authorized() || !building || building.IsDeleted())
  {
   EXPG_ClearServer();
   Rpc(EXPG_CompleteOwner, nonce);
   EXPG_Reply("Game Master access or the selected building changed.");
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
  SCR_EditableEntityUIInfo info = SCR_EditableEntityComponentClass.GetInfo(editableSource);
  array<EEditableEntityLabel> labels = {};
  if (info) info.GetEntityLabels(labels);
  if (!labels.Contains(EEditableEntityLabel.GROUPTYPE_INFANTRY)) { EXPG_Reply("Only infantry squads can garrison a building."); return; }
  IEntitySource source = resource.GetResource().ToEntitySource();
  array<ResourceName> members = {};
  if (!source || !source.Get("m_aUnitPrefabSlots", members) || members.IsEmpty() || members.Count() > 32) { EXPG_Reply("Choose a verified infantry roster of 1 to 32 soldiers."); return; }
  EXPG_GarrisonManager manager = EXPG_GarrisonManager.Get();
  string reason;
  if (!manager || !manager.CanFit(building, members.Count(), reason)) { EXPG_Reply("Garrison was not placed. " + reason); return; }
  vector transform[4];
  building.GetWorldTransform(transform);
  SCR_EditorPreviewParams params = SCR_EditorPreviewParams.CreateParams(transform);
  EEditableEntityBudget blockingBudget;
  SetPlacingFlag(EEditorPlacingFlags.CHARACTER_PLAYER, false);
  if (!CanCreateEntity(params: params) || !CanPlaceEntityServer(editableSource, blockingBudget, false, false) || !IsThereEnoughBudgetToSpawn(editableSource))
  {
   EXPG_Reply("The squad exceeds the editor budget or cannot be placed here.");
   return;
  }
  if (!m_EXPG_Ticket.Consume(nonce, EXPG_Now())) return;
  EXPG_ClearServer();
  Rpc(EXPG_CompleteOwner, nonce);
  // Keep the direct reference even if an invalid third-party prefab lacks editable data.
  EntitySpawnParams spawn = new EntitySpawnParams();
  spawn.TransformMode = ETransformMode.WORLD;
  Math3D.MatrixCopy(transform, spawn.Transform);
  OnBeforeEntityCreatedServer(prefab);
  IEntity entity = GetGame().SpawnEntityPrefab(resource, GetGame().GetWorld(), spawn);
  SCR_AIGroup group = SCR_AIGroup.Cast(entity);
  if (!entity) { EXPG_Reply("The engine could not spawn that squad."); return; }
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.GetEditableEntity(entity);
  if (!group || !editable) { EXPG_Reply("The prefab did not create an editable squad; inspect the spawned entity."); return; }
  int playerId = GetManager().GetPlayerID();
  editable.EOnEditorPlace(null, null, 0, false, playerId);
  editable.SetAuthor(playerId);
  editable.OnCreatedServer(this);
  array<SCR_EditableEntityComponent> created = {editable};
  OnEntityCreatedServer(created);
  if (!manager.Adopt(group, building, playerId)) EXPG_Reply("The squad remains under normal AI control because garrison assignment was refused.");
  else EXPG_Reply("Squad placed. Garrison preparation is completing.");
  GetOnPlaceEntityServer().Invoke(prefabID, editable, playerId);
 }

 protected void EXPG_Reply(string message) { Rpc(EXPG_ReplyOwner, message); }

 protected void EXPG_Reject(int nonce, string message)
 {
  Rpc(EXPG_CompleteOwner, nonce);
  EXPG_Reply(message);
 }

 [RplRpc(RplChannel.Reliable, RplRcver.Owner)]
 protected void EXPG_ReplyOwner(string message)
 {
  m_EXPG_Selecting = false;
  SCR_HintManagerComponent.ShowCustomHint(message, "EXPBG Garrison", 8);
 }

 [RplRpc(RplChannel.Reliable, RplRcver.Owner)]
 protected void EXPG_CompleteOwner(int nonce)
 {
  if (nonce != m_EXPG_ClientNonce) return;
  m_EXPG_ClientNonce = 0;
  m_EXPG_Selecting = false;
  super.SetInstantPlacing(null);
  if (m_EXPG_Dialog) m_EXPG_Dialog.Close();
  m_EXPG_Dialog = null;
 }

 override void EOnEditorDeactivate()
 {
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
