// EXPBG GM Optimizer | M.Pac and K.Edgar
// Local GM overlays. Group geometry is never a public replicated property.
#ifdef EBG_ACCEPTANCE_TEST
// Counts are process-lifetime totals so late allocations remain visible after Reset.
class EBG_VisualLifetimeProof
{
 static int MapOpens, MapCloses, BlockedOpens;
 static int CanvasCreated, CanvasRemoved, Commands;
 static int Subscriptions, MeshCreated, MeshDeleted, ResourceAcquired, ResourceReleased;
 static void Report(string phase)
 {
  PrintFormat("[EBG VISUAL LIFETIME] phase=%1 unloading=%2 overlayOff=%3 opens=%4 closes=%5 blocked=%6 subscriptions=%7", phase, EBG_CacheManager.Unloading, EBG_CacheVisuals.OverlaysDisabled(), MapOpens, MapCloses, BlockedOpens, Subscriptions);
  PrintFormat("[EBG VISUAL OWNERS] canvasCreated=%1 canvasRemoved=%2 commands=%3 meshCreated=%4 meshDeleted=%5 resourcesAcquired=%6 resourcesReleased=%7", CanvasCreated, CanvasRemoved, Commands, MeshCreated, MeshDeleted, ResourceAcquired, ResourceReleased);
 }
}
#endif

class EBG_CacheVisualGroup
{
 EBG_CacheZone Zone;
 vector Anchor;
 vector Bounds; // lowest member Y, highest member Y, stable manager group ID
 string Label;
 float Received;
}

class EBG_CacheVisualCircle
{
 EBG_CacheZone Zone;
 vector Center;
 float Radius;
 float Bottom;
 float Top;
 int Kind; // enrollment, wake, sleep
 bool Terrain;
}

class EBG_CacheVisualMesh
{
 IEntity Entity;
 ref Resource Mesh;
 vector Center;
 float Radius;
 float Bottom;
 float Top;
 int Kind = -1;
 bool Terrain;

 void Clear()
 {
#ifdef EBG_ACCEPTANCE_TEST
  if (Entity) EBG_VisualLifetimeProof.MeshDeleted++;
  if (Mesh) EBG_VisualLifetimeProof.ResourceReleased++;
#endif
  if (Entity) delete Entity;
  Entity = null;
  Mesh = null;
 }

 void Apply(EBG_CacheVisualCircle circle)
 {
  if (!circle || !EBG_CacheVisuals.Authorized() || EBG_CacheVisuals.OverlaysDisabled()) { Clear(); return; }
  if (Entity && Center == circle.Center && Radius == circle.Radius && Bottom == circle.Bottom && Top == circle.Top && Kind == circle.Kind && Terrain == circle.Terrain) return;
  Center = circle.Center; Radius = circle.Radius; Bottom = circle.Bottom; Top = circle.Top; Kind = circle.Kind; Terrain = circle.Terrain;
  if (EBG_CacheVisuals.OverlaysDisabled() || EBG_CacheManager.Unloading) { Clear(); return; }
  if (!Entity)
  {
   Entity = GetGame().SpawnEntity(GenericEntity, GetGame().GetWorld());
#ifdef EBG_ACCEPTANCE_TEST
   if (Entity) EBG_VisualLifetimeProof.MeshCreated++;
#endif
  }
  if (!Entity) return;
  Entity.SetOrigin(Center);
  array<vector> points = {};
  for (int i = 0; i < 48; i++)
  {
   float angle = Math.PI2 * i / 48;
   vector point = Vector(Math.Sin(angle) * Radius, Bottom - Center[1], Math.Cos(angle) * Radius);
   if (Terrain) point[1] = GetGame().GetWorld().GetSurfaceY(Center[0] + point[0], Center[2] + point[2]) - Center[1] + 0.15;
   points.Insert(point);
  }
  // Cyan enrollment inherits the native area shader; warning wake and danger sleep.
  ResourceName material = "{2D561A6C91F9C551}Assets/EXPBG/CacheAffected.emat";
  if (Kind == 1) material = "Assets/Editor/VirtualArea/VirtualArea_01_Warning.emat";
  if (Kind == 2) material = "Assets/Editor/VirtualArea/VirtualArea_01_Danger.emat";
#ifdef EBG_ACCEPTANCE_TEST
  if (Mesh) EBG_VisualLifetimeProof.ResourceReleased++;
#endif
  Mesh = SCR_Shape.CreateAreaMesh(points, Math.Max(Top - Bottom, 2), material, true);
#ifdef EBG_ACCEPTANCE_TEST
  if (Mesh) EBG_VisualLifetimeProof.ResourceAcquired++;
#endif
  if (!Mesh || !Mesh.GetResource()) { Clear(); return; }
  MeshObject meshObject = Mesh.GetResource().ToMeshObject();
  if (!meshObject) { Clear(); return; }
  Entity.SetObject(meshObject, "");
  Entity.SetFlags(EntityFlags.VISIBLE);
  // The mesh is assigned after spawning an inactive entity; commit its scene link now.
  Entity.Update();
 }
}

class EBG_CacheVisuals
{
 static ref array<ref EBG_CacheVisualGroup> Groups = {};
 static ref array<ref EBG_CacheVisualGroup> Orphans = {};
 static ref array<ref EBG_CacheVisualCircle> Circles = {};
 static ref array<ref EBG_CacheVisualMesh> Meshes;
 static float Elapsed = 1;
 static bool DisplayTruncated;
 // ponytail: cap display cost; management itself is not capped by these display limits.
 static const int MAX_ZONES = 32;
 static const int REQUEST_BATCH = 8;
 protected static int RequestCursor;
 static const int MAX_GROUPS = 32;
 static const int MAX_CIRCLES = 256;

 static bool OverlaysDisabled()
 {
  bool disabled;
#ifdef EBG_ACCEPTANCE_TEST
#ifdef EBG_ACCEPTANCE_NO_OVERLAYS
  disabled = true;
#endif
#endif
  return disabled;
 }

 static int AccessEpoch;
 protected static bool HadAccess;
 // Native ADMINISTRATOR means currently logged in (BanCommands permission).
 // Listed admin identities, elected SESSION_ADMINISTRATOR and GAME_MASTER do
 // not satisfy this gate. Only the testing monitor and GM identity detail use it.
 static bool LoggedInAdmin(int playerId)
 {
  if (!GetGame() || playerId <= 0) return false;
  PlayerManager players = GetGame().GetPlayerManager();
  return players && players.IsPlayerConnected(playerId) && players.HasPlayerRole(playerId, EPlayerRole.ADMINISTRATOR);
 }
 static bool LocalAdmin()
 {
  if (!GetGame() || !GetGame().GetPlayerController()) return false;
  return LoggedInAdmin(GetGame().GetPlayerController().GetPlayerId());
 }
 // Zone icons and overlays: any Game Master, voted or admin, with the full editor
 // open in Edit mode. The editor owner checks below are the permission.
 static bool AuthorizedEditor(SCR_EditorManagerEntity editor)
 {
  if (EBG_CacheManager.Unloading || !GetGame() || !GetGame().InPlayMode() || !editor) return false;
  PlayerManager players = GetGame().GetPlayerManager();
  if (editor.GetPlayerID() <= 0 || !players || !players.IsPlayerConnected(editor.GetPlayerID())) return false;
  if (!editor.IsOpened() || editor.IsLimited() || !editor.HasMode(EEditorMode.EDIT) || editor.GetCurrentMode() != EEditorMode.EDIT) return false;
  if (Replication.IsServer())
  {
   SCR_EditorManagerCore core = SCR_EditorManagerCore.Cast(SCR_EditorManagerCore.GetInstance(SCR_EditorManagerCore));
   return core && core.GetEditorManager(editor.GetPlayerID()) == editor;
  }
  return true;
 }
 static bool Authorized()
 {
  if (!GetGame()) return false;
  PlayerController player = GetGame().GetPlayerController();
  SCR_EditorManagerEntity editor = SCR_EditorManagerEntity.GetInstance();
  return player && editor && editor.GetPlayerID() == player.GetPlayerId() && AuthorizedEditor(editor);
 }
 static void UpdateAuthorization()
 {
  bool allowed = Authorized();
  if (allowed == HadAccess) return;
  HadAccess = allowed; AccessEpoch++;
  EBG_FullCacheMarkers.Reset();
  ClearMeshes(); Circles.Clear(); Groups.Clear(); Orphans.Clear();
  DisplayTruncated = false; Elapsed = 1;
  if (!allowed)
  {
   SCR_MapEntity mapEntity = SCR_MapEntity.GetMapInstance();
   EBG_CacheMapModule module;
   if (mapEntity) module = EBG_CacheMapModule.Cast(mapEntity.GetMapModule(EBG_CacheMapModule));
   if (module) module.ClearForPermissionLoss();
  }
 }

 static bool Visible(EBG_CacheZone zone)
 {
  if (!zone || zone.Markers == 0) return false;
  if (zone.Markers == 2) return true;
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.Cast(zone.FindComponent(SCR_EditableEntityComponent));
  return editable && editable.HasEntityState(EEditableEntityState.SELECTED);
 }

 static void ClearMeshes()
 {
		EXPBG_LazyStatics_EBG_CacheVisuals();
  foreach (EBG_CacheVisualMesh mesh : Meshes) mesh.Clear();
  Meshes.Clear();
 }

 static void Reset()
 {
  HadAccess = false; AccessEpoch++;
  RequestCursor = 0;
  EBG_FullCacheMarkers.Reset();
  ClearMeshes();
  Circles.Clear();
  Groups.Clear();
  Orphans.Clear();
  Elapsed = 1;
  DisplayTruncated = false;
  SCR_MapEntity mapEntity = SCR_MapEntity.GetMapInstance();
  if (!mapEntity) return;
  EBG_CacheMapModule module = EBG_CacheMapModule.Cast(mapEntity.GetMapModule(EBG_CacheMapModule));
  if (module) module.EBG_Shutdown();
 }

 static void RemoveZone(EBG_CacheZone zone)
 {
  for (int i = Groups.Count() - 1; i >= 0; i--)
   if (!Groups[i].Zone || Groups[i].Zone == zone) Groups.Remove(i);
  Circles.Clear();
  ClearMeshes();
  Elapsed = 1;
 }

 static void Collect(EBG_CacheZone zone, out array<vector> anchors, out array<vector> bounds, out array<string> labels)
 {
  anchors = {}; bounds = {}; labels = {};
  if (!EBG_CacheManager.Instance) return;
  foreach (EBG_CacheGroup record : EBG_CacheManager.Instance.Records)
  {
   if (record.Zone != zone || record.MinY > record.MaxY) continue;
   if (!zone && (!record.ReleaseRequested || (!record.Full && !record.Simulation))) continue;
   if (anchors.Count() >= MAX_GROUPS)
   {
    labels[labels.Count() - 1] = labels[labels.Count() - 1] + " | Display truncated: 32 groups";
    break;
   }
   anchors.Insert(record.Anchor);
   bounds.Insert(Vector(record.MinY, record.MaxY, record.Id));
   string state = record.DebugState();
   if (!zone) state = "Module deleted; recovery retained | " + state;
   labels.Insert(string.Format("%1 alive / %2 dead | %3", record.Alive, record.Dead, state));
  }
 }

 static void ReceiveRecovery(array<vector> anchors, array<vector> bounds, array<string> labels)
 {
  Orphans.Clear();
  if (!Authorized() || anchors.Count() != bounds.Count() || anchors.Count() != labels.Count()) return;
  float now = GetGame().GetWorld().GetWorldTime() * 0.001;
  for (int i = 0; i < Math.Min(anchors.Count(), MAX_GROUPS); i++)
  {
   EBG_CacheVisualGroup group = new EBG_CacheVisualGroup();
   group.Anchor = anchors[i]; group.Bounds = bounds[i]; group.Label = labels[i]; group.Received = now;
   Orphans.Insert(group);
  }
 }

 static void Receive(EBG_CacheZone zone, array<vector> anchors, array<vector> bounds, array<string> labels)
 {
  if (!Authorized() || !zone || anchors.Count() != bounds.Count() || anchors.Count() != labels.Count()) return;
  for (int i = Groups.Count() - 1; i >= 0; i--)
   if (!Groups[i].Zone || Groups[i].Zone == zone) Groups.Remove(i);
  float now = GetGame().GetWorld().GetWorldTime() * 0.001;
  for (int i = 0; i < Math.Min(anchors.Count(), MAX_GROUPS); i++)
  {
   EBG_CacheVisualGroup group = new EBG_CacheVisualGroup();
   group.Zone = zone; group.Anchor = anchors[i]; group.Bounds = bounds[i]; group.Label = labels[i]; group.Received = now;
   Groups.Insert(group);
  }
 }

 static void AddCircle(EBG_CacheZone zone, vector center, float radius, float bottom, float top, int kind, bool terrain)
 {
  if (Circles.Count() >= MAX_CIRCLES) { DisplayTruncated = true; return; }
  EBG_CacheVisualCircle circle = new EBG_CacheVisualCircle();
  circle.Zone = zone; circle.Center = center; circle.Radius = radius; circle.Bottom = bottom; circle.Top = top; circle.Kind = kind; circle.Terrain = terrain;
  Circles.Insert(circle);
 }

 static vector HeightBand(float minimumY, float maximumY, float below, float above, float margin = 0)
 {
  return Vector(minimumY - below - margin, maximumY + above + margin, 0);
 }

 // Run from Workbench script console: EBG_CacheVisuals.RunGeometryChecks();
 static int RunGeometryChecks()
 {
  int failed;
  vector band = HeightBand(10, 90, 200, 200);
  if (band != "-190 290 0") failed++;
  if (!EBG_CacheGeometry.Contains(Vector(400, band[1], 0), "0 999 0", 400, 10, 90, true, 200, 200, 0)) failed++;
  if (EBG_CacheGeometry.Contains(Vector(400, band[1] + 1, 0), "0 999 0", 400, 10, 90, true, 200, 200, 0)) failed++;
  if (!EBG_CacheGeometry.Contains(Vector(400, band[0], 0), "0 999 0", 400, 10, 90, true, 200, 200, 0)) failed++;
  band = HeightBand(10, 90, 200, 200, 50);
  if (band != "-240 340 0") failed++;
  if (!EBG_CacheGeometry.Contains(Vector(600, band[1], 0), "0 -999 0", 600, 10, 90, true, 200, 200, 50)) failed++;
  PrintFormat("[EBG TEST] Overlay bands versus activation geometry: 6 checks, %1 failures", failed);
  return failed;
 }

 static void Build()
 {
  Circles.Clear();
  DisplayTruncated = false;
  array<EBG_CacheZone> visible = {};
  foreach (EBG_CacheZone zone : EBG_CacheZone.Zones)
  {
   if (!Visible(zone) || (!zone.Map && !zone.Viewer)) continue;
   if (visible.Count() >= MAX_ZONES) { DisplayTruncated = true; break; }
   visible.Insert(zone);
   vector origin = zone.GetOrigin();
   // Reserve every zone's enrollment ring before any per-group geometry.
   AddCircle(zone, origin, zone.Affected, origin[1], origin[1] + 3, 0, true);
  }
  // Whole-zone protection uses the union of managed member height bands. One
  // pair replaces overlapping pairs that previously exhausted the display cap.
  foreach (EBG_CacheZone wholeZone : visible)
  {
   if (wholeZone.Strategy != 0) continue;
   vector center = wholeZone.GetOrigin();
   float low = float.MAX, high = -float.MAX;
   foreach (EBG_CacheVisualGroup memberGroup : Groups)
   {
    if (memberGroup.Zone != wholeZone) continue;
    low = Math.Min(low, memberGroup.Bounds[0]);
    high = Math.Max(high, memberGroup.Bounds[1]);
   }
   bool terrain = low > high || wholeZone.Height == 0;
   vector band = HeightBand(low, high, wholeZone.Below, wholeZone.Above);
   if (terrain) band = "0 3 0";
   AddCircle(wholeZone, center, wholeZone.ZoneWake, band[0], band[1], 1, terrain);
   if (!terrain) { band[0] = band[0] - wholeZone.HeightMargin; band[1] = band[1] + wholeZone.HeightMargin; }
   AddCircle(wholeZone, center, wholeZone.ZoneSleep, band[0], band[1], 2, terrain);
  }
  foreach (EBG_CacheVisualGroup group : Groups)
  {
   EBG_CacheZone owner = group.Zone;
   if (!owner || owner.Strategy != 1 || !visible.Contains(owner)) continue;
   vector groupBand = HeightBand(group.Bounds[0], group.Bounds[1], owner.Below, owner.Above);
   AddCircle(owner, group.Anchor, owner.GroupWake, groupBand[0], groupBand[1], 1, false);
   AddCircle(owner, group.Anchor, owner.GroupSleep, groupBand[0] - owner.HeightMargin, groupBand[1] + owner.HeightMargin, 2, false);
  }
 }

 static void Update(EBG_CacheZone caller, float timeSlice)
 {
		EXPBG_LazyStatics_EBG_CacheVisuals();
  if (EBG_CacheZone.Zones.IsEmpty() || caller != EBG_CacheZone.Zones[0]) return;
  UpdateAuthorization();
  if (OverlaysDisabled()) return;
  if (!Authorized()) { ClearMeshes(); Circles.Clear(); Groups.Clear(); Orphans.Clear(); Elapsed = 1; return; }
  // Permission and display-channel checks happen each frame, even between data refreshes.
  Elapsed += timeSlice;
  if (Elapsed >= 0.5)
  {
   Elapsed = 0;
   float now = GetGame().GetWorld().GetWorldTime() * 0.001;
   for (int i = Groups.Count() - 1; i >= 0; i--)
    if (!Groups[i].Zone || now - Groups[i].Received > 3) Groups.Remove(i);
   array<EBG_CacheZone> visible = {};
   foreach (EBG_CacheZone candidate : EBG_CacheZone.Zones)
   {
    if (Visible(candidate) && (candidate.Map || candidate.Viewer)) visible.Insert(candidate);
    if (visible.Count() >= MAX_ZONES) break;
   }
   // Keep the existing eight-request burst budget. Thirty-two displayed zones
   // refresh within two seconds rather than starving every zone after eight.
   int requests = Math.Min(REQUEST_BATCH, visible.Count());
   for (int offset = 0; offset < requests; offset++)
   {
    EBG_CacheZone zone = visible[(RequestCursor + offset) % visible.Count()];
    if (Replication.IsServer())
    {
     array<vector> anchors, bounds;
     array<string> labels;
     Collect(zone, anchors, bounds, labels);
     Receive(zone, anchors, bounds, labels);
    }
    else
    {
     SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.Cast(zone.FindComponent(SCR_EditableEntityComponent));
     if (editable) SCR_EditorManagerEntity.GetInstance().EBG_RequestVisuals(Replication.FindItemId(editable));
    }
   }
   if (!visible.IsEmpty()) RequestCursor = (RequestCursor + requests) % visible.Count();
   Build();
  }
  // Apply owns visibility; excluded mesh slots are destroyed below.
  int index;
  foreach (EBG_CacheVisualCircle circle : Circles)
  {
   if (!Visible(circle.Zone) || !circle.Zone.Viewer) continue;
   if (index >= Meshes.Count()) Meshes.Insert(new EBG_CacheVisualMesh());
   EBG_CacheVisualMesh mesh = Meshes[index++];
   mesh.Apply(circle);
  }
  while (Meshes.Count() > index)
  {
   Meshes[Meshes.Count() - 1].Clear();
   Meshes.Remove(Meshes.Count() - 1);
  }
 }

	//------------------------------------------------------------------------------------------------
	//! Creates the collections on first use (not in the global static initializer, which has a
	//! per-function instruction limit that large modsets exceed on Windows).
	protected static void EXPBG_LazyStatics_EBG_CacheVisuals()
	{
		if (!Meshes)
			Meshes = new array<ref EBG_CacheVisualMesh>();
	}
}

// Native 1.8 clears these maps on editor close while owner RPCs may still be
// queued. Match its guarded disconnect path; native reopening rebuilds both maps.
modded class SCR_PlayersManagerEditorComponent
{
 [RplRpc(RplChannel.Reliable, RplRcver.Owner)]
 override protected void OnConnectedOwner(int playerID)
 {
  if (!m_MainEntities)
  {
#ifdef EBG_ACCEPTANCE_TEST
   PrintFormat("[EBG EDITOR LIFECYCLE] Native owner-connect ignored with closed player map; player=%1", playerID);
#endif
   return;
  }
  super.OnConnectedOwner(playerID);
 }

 [RplRpc(RplChannel.Reliable, RplRcver.Owner)]
 override protected void OnPossessedOwner(int playerID, bool isPossessing, int entityID)
 {
  if (!m_PossessedEntities)
  {
#ifdef EBG_ACCEPTANCE_TEST
   PrintFormat("[EBG EDITOR LIFECYCLE] Native owner-possession ignored with closed player map; player=%1 possessing=%2", playerID, isPossessing);
#endif
   return;
  }
  super.OnPossessedOwner(playerID, isPossessing, entityID);
 }
}

// Existing editor entity already has owner-scoped replication and server permission state.
modded class SCR_EditorManagerEntity
{
 protected float m_EBGRequestWindow;
 protected int m_EBGRequests;
 protected float m_EBGRecoveryRequestTime = -1;

 void EBG_RequestRecoveryVisuals()
 {
  if (this != SCR_EditorManagerEntity.GetInstance()) return;
  EBG_CacheVisuals.UpdateAuthorization();
  Rpc(EBG_RecoveryVisualsServer, EBG_CacheVisuals.AccessEpoch);
 }

 [RplRpc(RplChannel.Reliable, RplRcver.Server)]
 protected void EBG_RecoveryVisualsServer(int epoch)
 {
  if (!Replication.IsServer() || !EBG_CacheVisuals.AuthorizedEditor(this)) return;
  float now = GetGame().GetWorld().GetWorldTime() * 0.001;
  if (now - m_EBGRecoveryRequestTime < 0.4) return;
  m_EBGRecoveryRequestTime = now;
  array<vector> anchors, bounds;
  array<string> labels;
  EBG_CacheVisuals.Collect(null, anchors, bounds, labels);
  Rpc(EBG_RecoveryVisualsOwner, epoch, anchors, bounds, labels);
 }

 [RplRpc(RplChannel.Reliable, RplRcver.Owner)]
 protected void EBG_RecoveryVisualsOwner(int epoch, array<vector> anchors, array<vector> bounds, array<string> labels)
 {
  EBG_CacheVisuals.UpdateAuthorization();
  if (this != SCR_EditorManagerEntity.GetInstance() || epoch != EBG_CacheVisuals.AccessEpoch || !EBG_CacheVisuals.Authorized()) return;
  EBG_CacheVisuals.ReceiveRecovery(anchors, bounds, labels);
 }
 void EBG_RequestVisuals(RplId zoneId)
 {
  if (this != SCR_EditorManagerEntity.GetInstance()) return;
  EBG_CacheVisuals.UpdateAuthorization();
  Rpc(EBG_VisualsServer, zoneId, EBG_CacheVisuals.AccessEpoch);
 }

 [RplRpc(RplChannel.Reliable, RplRcver.Server)]
 protected void EBG_VisualsServer(RplId zoneId, int epoch)
 {
  if (!Replication.IsServer() || !EBG_CacheVisuals.AuthorizedEditor(this)) return;
  float now = GetGame().GetWorld().GetWorldTime() * 0.001;
  if (now - m_EBGRequestWindow >= 0.4) { m_EBGRequestWindow = now; m_EBGRequests = 0; }
  if (++m_EBGRequests > EBG_CacheVisuals.REQUEST_BATCH) return;
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.Cast(Replication.FindItem(zoneId));
  if (!editable) return;
  EBG_CacheZone zone = EBG_CacheZone.Cast(editable.GetOwner());
  if (!zone || !zone.Markers || (!zone.Map && !zone.Viewer)) return;
  array<vector> anchors, bounds;
  array<string> labels;
  EBG_CacheVisuals.Collect(zone, anchors, bounds, labels);
  Rpc(EBG_VisualsOwner, zoneId, epoch, anchors, bounds, labels);
 }

 [RplRpc(RplChannel.Reliable, RplRcver.Owner)]
 protected void EBG_VisualsOwner(RplId zoneId, int epoch, array<vector> anchors, array<vector> bounds, array<string> labels)
 {
  EBG_CacheVisuals.UpdateAuthorization();
  if (this != SCR_EditorManagerEntity.GetInstance() || epoch != EBG_CacheVisuals.AccessEpoch || !EBG_CacheVisuals.Authorized()) return;
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.Cast(Replication.FindItem(zoneId));
  if (editable) EBG_CacheVisuals.Receive(EBG_CacheZone.Cast(editable.GetOwner()), anchors, bounds, labels);
 }
}

[BaseContainerProps()]
class EBG_CacheMapModule : SCR_MapModuleBase
{
 protected CanvasWidget m_Canvas;
 protected ref array<ref CanvasWidgetCommand> m_Commands = {};
 protected float m_RecoveryElapsed = 1;
#ifdef EBG_ACCEPTANCE_TEST
 protected bool m_EBG_DiagnosticActive;
#endif

 override void SetActive(bool active, bool isCleanup = false)
 {
  // Native deactivation can remove a module without dispatching OnMapClose.
  // Release our own widget for either lifecycle path before unsubscribing.
  if (!active) EBG_ClearCanvas();
  super.SetActive(active, isCleanup);
#ifdef EBG_ACCEPTANCE_TEST
  if (active != m_EBG_DiagnosticActive)
  {
   if (active) EBG_VisualLifetimeProof.Subscriptions++;
   else EBG_VisualLifetimeProof.Subscriptions--;
   m_EBG_DiagnosticActive = active;
  }
  EBG_VisualLifetimeProof.Report("module SetActive");
#endif
 }

 void EBG_Shutdown()
 {
  EBG_ClearCanvas();
  SetActive(false);
 }

 override protected void OnMapOpen(MapConfiguration config)
 {
#ifdef EBG_ACCEPTANCE_TEST
  EBG_VisualLifetimeProof.MapOpens++;
#endif
  EBG_ClearCanvas();
  if (config.MapEntityMode != EMapEntityMode.EDITOR || EBG_CacheVisuals.OverlaysDisabled() || !EBG_CacheVisuals.Authorized())
  {
#ifdef EBG_ACCEPTANCE_TEST
   EBG_VisualLifetimeProof.BlockedOpens++;
   EBG_VisualLifetimeProof.Report("map open denied");
#endif
   return;
  }
  CreateAuthorizedCanvas(config);
 }
 protected void CreateAuthorizedCanvas(MapConfiguration config)
 {
  if (m_Canvas || !config || config.MapEntityMode != EMapEntityMode.EDITOR || !config.RootWidgetRef || EBG_CacheVisuals.OverlaysDisabled() || !EBG_CacheVisuals.Authorized()) return;
  m_Canvas = CanvasWidget.Cast(GetGame().GetWorkspace().CreateWidgets("{20AD8671A9C08CD9}UI/layouts/EXPBG/CacheMapOverlay.layout", config.RootWidgetRef));
  m_RecoveryElapsed = 1;
  if (m_Canvas)
  {
   m_Canvas.SetFlags(WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS);
   m_Canvas.SetDrawCommands(m_Commands);
  }
#ifdef EBG_ACCEPTANCE_TEST
  if (m_Canvas) EBG_VisualLifetimeProof.CanvasCreated++;
  EBG_VisualLifetimeProof.Report("map open allocated");
#endif
 }

 override protected void OnMapClose(MapConfiguration config)
 {
#ifdef EBG_ACCEPTANCE_TEST
  EBG_VisualLifetimeProof.MapCloses++;
#endif
  EBG_ClearCanvas();
 }

 void EBG_ClearCanvas()
 {
  m_Commands.Clear();
#ifdef EBG_ACCEPTANCE_TEST
  EBG_VisualLifetimeProof.Commands = 0;
  if (m_Canvas) EBG_VisualLifetimeProof.CanvasRemoved++;
#endif
  if (m_Canvas) m_Canvas.RemoveFromHierarchy();
  m_Canvas = null;
  EBG_CacheVisuals.Orphans.Clear();
#ifdef EBG_ACCEPTANCE_TEST
  EBG_VisualLifetimeProof.Report("canvas clear");
#endif
 }

 protected void Text(string value, vector pos, int color = 0xffffffff)
 {
  TextDrawCommand command = new TextDrawCommand();
  command.m_sText = value; command.m_Position = pos; command.m_iColor = color; command.m_fSize = 15; command.m_iFontPropertiesId = 0;
  m_Commands.Insert(command);
 }
 protected int WrappedText(string value, float x, float y, int columns)
 {
  array<string> words = {};
  value.Split(" ", words, true);
  string line;
  int rows;
  foreach (string word : words)
  {
   if (line != "" && line.Length() + word.Length() + 1 > columns)
   { Text(line, Vector(x, y + rows * 17, 0)); rows++; line = ""; }
   if (line != "") line += " ";
   line += word;
  }
  if (line != "") { Text(line, Vector(x, y + rows * 17, 0)); rows++; }
  return rows;
 }

 void ClearForPermissionLoss()
 {
  // Drop only our canvas; native map/UI entries remain owned by their modules.
  // A later valid login can recreate it from the currently open map config.
  EBG_ClearCanvas();
 }
#ifdef EBG_ACCEPTANCE_TEST
 bool EBG_HasCanvas() { return m_Canvas != null; }
#endif
 override void Update(float timeSlice)
 {
  EBG_CacheVisuals.UpdateAuthorization();
  // Login while an editor map is already open must not require a map reopen.
  if (!m_Canvas && m_MapEntity && m_MapEntity.IsOpen()) CreateAuthorizedCanvas(m_MapEntity.GetMapConfig());
  if (!m_Canvas) return;
  m_Commands.Clear();
#ifdef EBG_ACCEPTANCE_TEST
  EBG_VisualLifetimeProof.Commands = 0;
#endif
  bool allowed = EBG_CacheVisuals.Authorized();
  m_Canvas.SetVisible(allowed);
  if (!allowed) { EBG_CacheVisuals.Orphans.Clear(); return; }
  // The map updates even after the last cache module was deleted.
  m_RecoveryElapsed += timeSlice;
  if (m_RecoveryElapsed >= 0.5)
  {
   m_RecoveryElapsed = 0;
   if (Replication.IsServer())
   {
    array<vector> anchors, bounds;
    array<string> labels;
    EBG_CacheVisuals.Collect(null, anchors, bounds, labels);
    EBG_CacheVisuals.ReceiveRecovery(anchors, bounds, labels);
   }
   else SCR_EditorManagerEntity.GetInstance().EBG_RequestRecoveryVisuals();
  }
  float width, height;
  m_Canvas.GetScreenSize(width, height);
  m_Canvas.SetSizeInUnits(Vector(width, height, 0));
  m_Canvas.SetZoom(1);
  m_Canvas.SetOffsetPx(vector.Zero);
  array<EBG_CacheZone> wholeWake = {};
  array<EBG_CacheZone> wholeSleep = {};
  bool drawn;
  foreach (EBG_CacheVisualCircle circle : EBG_CacheVisuals.Circles)
  {
   if (!EBG_CacheVisuals.Visible(circle.Zone) || !circle.Zone.Map) continue;
   // Whole-zone groups may have different vertical bands, but share a horizontal map circle.
   if (circle.Zone.Strategy == 0 && circle.Kind == 1)
   {
    if (wholeWake.Contains(circle.Zone)) continue;
    wholeWake.Insert(circle.Zone);
   }
   if (circle.Zone.Strategy == 0 && circle.Kind == 2)
   {
    if (wholeSleep.Contains(circle.Zone)) continue;
    wholeSleep.Insert(circle.Zone);
   }
   int x, y;
   m_MapEntity.WorldToScreen(circle.Center[0], circle.Center[2], x, y, true);
   LineDrawCommand command = new LineDrawCommand();
   command.m_Vertices = {};
   command.m_fWidth = 2;
   command.m_bShouldEnclose = true;
   command.m_iColor = 0xff55dfff;
   if (circle.Kind == 1) command.m_iColor = 0xffffd35a;
   if (circle.Kind == 2) command.m_iColor = 0xffff6565;
   m_Canvas.TessellateCircle(Vector(x, y, 0), circle.Radius * m_MapEntity.GetCurrentZoom(), 48, command.m_Vertices);
   m_Commands.Insert(command);
   drawn = true;
  }
  float now = GetGame().GetWorld().GetWorldTime() * 0.001;
  for (int i = EBG_CacheVisuals.Orphans.Count() - 1; i >= 0; i--)
   if (now - EBG_CacheVisuals.Orphans[i].Received > 2) EBG_CacheVisuals.Orphans.Remove(i);
  foreach (EBG_CacheVisualGroup orphan : EBG_CacheVisuals.Orphans)
  {
   int x, y;
   m_MapEntity.WorldToScreen(orphan.Anchor[0], orphan.Anchor[2], x, y, true);
   LineDrawCommand marker = new LineDrawCommand();
   marker.m_Vertices = {}; marker.m_fWidth = 3; marker.m_bShouldEnclose = true; marker.m_iColor = 0xffffa451;
   m_Canvas.TessellateCircle(Vector(x, y, 0), 8, 16, marker.m_Vertices);
   m_Commands.Insert(marker);
   int columns = Math.Min(64, Math.Max(24, Math.Floor((width - 48) / 10)));
   float labelX = Math.Max(24, Math.Min(x + 12, width - columns * 10 - 24));
   float labelY = Math.Max(190, Math.Min(y, height - 220));
   int rows = WrappedText(orphan.Label, labelX, labelY, columns);
   WrappedText(string.Format("Saved position @ %1, %2. Recovery data retained. To retry, place or select any cache module, set Before saving mission to Restore all AI and pause caching, then Save and close.", Math.Round(orphan.Anchor[0]), Math.Round(orphan.Anchor[2])), labelX, labelY + rows * 17, columns);
   drawn = true;
  }
  if (!drawn) { m_Canvas.SetDrawCommands(m_Commands); return; }
  // Leave the native notification area above the map legend clear.
  Text("EXPBG | Affected: cyan | Wake: yellow | Sleep: red", "24 186 0");
  Text("Height is relative to units", "24 207 0");
  if (!EBG_CacheVisuals.Orphans.IsEmpty()) Text("Orange: deleted-module recovery. Retry from any cache module.", "24 167 0", 0xffffa451);
  if (EBG_CacheVisuals.DisplayTruncated)
   Text("Display truncated: maximum 8 zones / 96 rings", "24 146 0", 0xffffd35a);
  foreach (EBG_CacheVisualGroup group : EBG_CacheVisuals.Groups)
  {
   if (!EBG_CacheVisuals.Visible(group.Zone) || !group.Zone.Map) continue;
   int x, y;
   m_MapEntity.WorldToScreen(group.Anchor[0], group.Anchor[2], x, y, true);
   int columns = Math.Min(64, Math.Max(24, Math.Floor((width - 48) / 10)));
   float labelX = Math.Max(24, Math.Min(x + 6, width - columns * 10 - 24));
   float labelY = Math.Max(160, Math.Min(y, height - 170));
   string label = group.Label;
   if (group.Zone.DebugMessages == 2 && EBG_CacheVisuals.LocalAdmin()) label = string.Format("DEBUG group=%1 zone=%2 | %3", Math.Round(group.Bounds[2]), group.Zone.GetID(), label);
   int rows = WrappedText(label, labelX, labelY, columns);
   vector origin = group.Zone.GetOrigin();
   WrappedText(string.Format("Owner zone @ %1, %2 | units Y %3 to %4", Math.Round(origin[0]), Math.Round(origin[2]), Math.Round(group.Bounds[0]), Math.Round(group.Bounds[1])), labelX, labelY + rows * 17, columns);
  }
  // A selected module's replicated status under its centre: counts plus why it
  // enrolls or caches nothing (no group nearby, held by another module, a player
  // character inside the sleep radius, Full unavailable). Text only.
  int statusLabels;
  foreach (EBG_CacheZone statusZone : EBG_CacheZone.Zones)
  {
   if (statusLabels >= EBG_CacheVisuals.MAX_ZONES || !EBG_CacheVisuals.Visible(statusZone) || !statusZone.Map) continue;
   SCR_EditableEntityComponent statusEditable = SCR_EditableEntityComponent.Cast(statusZone.FindComponent(SCR_EditableEntityComponent));
   if (!statusEditable || !statusEditable.HasEntityState(EEditableEntityState.SELECTED)) continue;
   statusLabels++;
   int x, y;
   vector statusOrigin = statusZone.GetOrigin();
   m_MapEntity.WorldToScreen(statusOrigin[0], statusOrigin[2], x, y, true);
   int columns = Math.Min(64, Math.Max(24, Math.Floor((width - 48) / 10)));
   float labelX = Math.Max(24, Math.Min(x + 6, width - columns * 10 - 24));
   float labelY = Math.Max(160, Math.Min(y + 24, height - 170));
   WrappedText("Cache zone: " + statusZone.Status, labelX, labelY, columns);
  }
  m_Canvas.SetDrawCommands(m_Commands);
#ifdef EBG_ACCEPTANCE_TEST
  EBG_VisualLifetimeProof.Commands = m_Commands.Count();
#endif
 }
}

modded class SCR_MapEntity
{
 override protected void OnMapOpen(MapConfiguration config)
 {
  // Native ActivateModules only creates new module types on the first map-mode
  // load. Register before a zone exists too: Unloading can still reflect the
  // previous world until the first zone initializes. Canvas creation and drawing
  // remain guarded by Authorized(), including its world-cleanup check.
  if (config.MapEntityMode == EMapEntityMode.EDITOR && !EBG_CacheVisuals.OverlaysDisabled())
  {
   bool exists;
   foreach (SCR_MapModuleBase module : config.Modules)
    if (module.IsInherited(EBG_CacheMapModule)) exists = true;
   if (!exists) config.Modules.Insert(new EBG_CacheMapModule());
  }
  super.OnMapOpen(config);
 }
}

modded class ArmaReforgerScripted
{
 override protected void OnBeforeWorldCleanup()
 {
#ifdef EBG_ACCEPTANCE_TEST
  EBG_VisualLifetimeProof.Report("before world cleanup");
#endif
  EBG_FullCacheGroup.ShutdownForWorldCleanup();
  SCR_ChimeraCharacter.EBG_ClearSimulationOwnersForWorldCleanup();
#ifdef EBG_ACCEPTANCE_TEST
  EBG_ZonePersistenceProof.Shutdown();
#endif
  EBG_CacheManager.ShutdownForWorldCleanup();
  EBG_CacheDebug.Reset(true);
  EBG_CacheVisuals.Reset();
  super.OnBeforeWorldCleanup();
#ifdef EBG_ACCEPTANCE_TEST
  EBG_VisualLifetimeProof.Report("after native before-world-cleanup");
#endif
 }
}
