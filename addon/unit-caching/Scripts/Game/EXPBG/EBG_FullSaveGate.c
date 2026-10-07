// Native saves cannot capture absent Full groups. The optional CDF companion
// exports portable snapshots separately; other saves still require restoration.
// Native SaveGameManager has one boolean, not owner tokens. Check its actual
// policy before taking/releasing a hold; an installed content addon is not itself
// a conflicting save-policy owner. Unknown simultaneous writers still require
// compatibility testing because the native API exposes no ownership tokens.
class EBG_FullSaveGate
{
 protected static SaveGameManager s_Manager;
 protected static bool s_Held;
 protected static bool s_RestoreRequested;
 protected static bool s_Fault;
 protected static int s_SaveTypes;
 protected static string s_Reason;
 protected static bool s_CDFWarningLogged;
 // Loaded addons cannot change during a session: read them once per world instead of
 // on every Tick and Poll. An empty list is never remembered.
 protected static World s_AddonWorld;
 protected static bool s_AddonsRead;
 protected static bool s_OwnPack;
 protected static bool s_CDFWithoutCompanion;

 static bool IsHeld() { return s_Held; }
 static bool IsFaulted() { return s_Fault; }
 static bool IsRestoreRequested() { return s_RestoreRequested; }
 static string GetStatus()
 {
  if (SCR_AIGroupSerializer.EBG_HasPendingMemberCallbacks()) return "Not ready to save: native group callbacks need recovery. Restore ALL after original member identity and ownership are valid.";
  if (s_Fault) return "Full save protection requires attention: " + s_Reason;
  if (s_Held && s_RestoreRequested) return "Not ready to save: restoring every cache zone. Wait for recovery to finish before any native or CDF save.";
  // Shown only while Full groups are actually absent. The native permission stays
  // withheld while any cache or recovery state remains; Poll returns it once every
  // group is awake again, or through Prepare for save.
  if (s_Held && HasAbsentFullGroups(EBG_CacheManager.Instance)) return "Native saves are paused while Full groups are absent. The matching CDF companion can save stable cached groups. Other saves require restoring ALL caches.";
  if (s_Held) return "";
  return s_Reason;
 }
 // A Full group whose survivors are currently deleted or mid-transition.
 static bool HasAbsentFullGroups(EBG_CacheManager manager)
 {
  if (!manager) return false;
  foreach (EBG_CacheGroup record : manager.Records)
   if (record.Full || record.FullCleanup || !record.FullMemberIds.IsEmpty()) return true;
  return false;
 }

 // The one loaded-addon read: this pack's identity, and CDF loaded without the
 // EXPBG CDF companion. False while the list is empty (nothing is remembered then).
 protected static bool ReadLoadedAddons()
 {
  World world;
  if (GetGame()) world = GetGame().GetWorld();
  if (s_AddonsRead && world == s_AddonWorld)
  {
   return true;
  }
  array<string> addons = {};
  GameProject.GetLoadedAddons(addons);
  if (addons.IsEmpty())
  {
   return false;
  }
  s_OwnPack = false;
  foreach (string addon : addons)
  {
   if (addon == "FC1402F65B2F4A45") s_OwnPack = true; // EXPBG GM Tools pack
  }
  s_CDFWithoutCompanion = addons.Contains("6A1876F37D65AB09") && !addons.Contains("07BC942D90324CD9");
  s_AddonWorld = world;
  s_AddonsRead = true;
  return true;
 }

 protected static bool SupportedRuntime(out string reason)
 {
  reason = "";
  if (!GetGame() || !Replication.IsServer() || EBG_CacheManager.Unloading)
  { reason = "Full save protection requires the running authority"; return false; }
  if (GameStateTransitions.IsTransitionRequestedOrInProgress())
  { reason = "A game transition is pending; saving permission will not be changed"; return false; }
  SCR_GameModeEditor mode = SCR_GameModeEditor.Cast(GetGame().GetGameMode());
  if (!mode || mode.Type() != SCR_GameModeEditor || !mode.IsRunning() || mode.GetState() != SCR_EGameModeState.GAME)
  { reason = "Full save protection supports the active vanilla GM game mode only"; return false; }
  // A vanilla GM mission names GameMasterSystems in its header, hosted locally or on
  // a server. Workbench World Editor play and some scenarios run without it.
  ResourceName systems = GetGame().GetSystemsConfig();
  if (systems != "{8DDC2A311929D52F}Configs/Systems/GameMasterSystems.conf")
  { reason = "Full save protection requires the native GameMasterSystems systems config, which this session does not run (Workbench World Editor play or a scenario that replaces it); start the GM mission from the main menu or a server"; return false; }
  if (!ReadLoadedAddons())
  {
   reason = "Loaded addon ownership could not be verified";
   return false;
  }
  if (!s_OwnPack)
  {
   reason = "EXPBG GM Tools addon identity could not be verified";
   return false;
  }
  return true;
 }

 // CDF does not consult the native save permission. Without its companion,
 // refuse new deletions rather than let CDF silently omit an absent roster.
 // This check is admission-only: restoration and releasing a hold remain valid.
 protected static bool CanCaptureForCDF(out string reason)
 {
  // An unreadable (empty) list never matched CDF before either.
  if (!ReadLoadedAddons() || !s_CDFWithoutCompanion)
  {
   return true;
  }
  reason = "Full Cache blocked: CDF is loaded without the EXPBG GM Tools CDF companion. AI remain present; mode unchanged. Install the companion or choose Simulation. Prepare for save restores existing caches.";
  if (!s_CDFWarningLogged)
  {
   s_CDFWarningLogged = true;
   Print("[EBG FULL SAVE GATE] " + reason, LogLevel.WARNING);
  }
  return false;
 }

 // Status text only: the runtime rules TryAcquire applies, read before any group
 // tries to sleep and without touching the native save permission. A locally
 // hosted vanilla GM mission names GameMasterSystems in its header and passes;
 // Workbench World Editor play has no systems config and fails here.
 static bool Available(out string reason)
 {
  reason = "";
  return SupportedRuntime(reason) && CanCaptureForCDF(reason);
 }

 // Call immediately before reserving the first Full transaction, before Save/delete.
 static bool TryAcquire(EBG_CacheManager manager, out string reason)
 {
  if (!manager) { reason = "Cache manager unavailable"; return false; }
  if (s_Fault || s_RestoreRequested) { reason = GetStatus(); return false; }
  if (!SupportedRuntime(reason) || !CanCaptureForCDF(reason)) { s_Reason = reason; return false; }
  SaveGameManager saving = GetGame().GetSaveGameManager();
  if (s_Held)
  {
   if (saving != s_Manager || !saving || !saving.IsSavingEnabled() || saving.GetEnabledSaveTypes() != s_SaveTypes || saving.IsSavingAllowed())
   { Fault("Native save policy changed while Full snapshots were protected"); reason = GetStatus(); return false; }
   return true;
  }
  // Prefab cache does not use native serialization. Disabled mission saving is valid.
  if (!saving || !saving.IsSavingEnabled()) { s_Reason = ""; return true; }
  if (!saving.IsSavingAllowed() || saving.IsBusy())
  { reason = "Full Cache waits for the current save operation to finish"; s_Reason = reason; return false; }
  s_Manager = saving;
  s_SaveTypes = saving.GetEnabledSaveTypes();
  saving.SetSavingAllowed(false);
  if (saving.IsSavingAllowed())
  { s_Manager = null; reason = "Native saving permission did not acknowledge the Full hold"; s_Reason = reason; return false; }
  s_Held = true;
  s_Reason = "";
  return true;
 }

 // Root's explicit GM action must first hold ALL zones and request normal restore.
 // This only authorizes permission release; it never mutates a population/ledger.
 static void RequestRestoreForSave() { if (s_Held) s_RestoreRequested = true; }

 static bool HasRecoveryState(EBG_CacheManager manager)
 {
  if (SCR_AIGroupSerializer.EBG_HasPendingMemberCallbacks()) return true;
  if (!manager || EBG_FullCacheGroup.IsNativeOperationBusy()) return true;
  foreach (EBG_CacheGroup record : manager.Records)
   if (record.PersistentScalarRollbackPending || record.Full != null || record.Simulation != null || record.FullCleanup != null || !record.FullGroupId.IsNull() || !record.FullMemberIds.IsEmpty() || record.Recovery != "") return true;
  return false;
 }

 // Poll AFTER coordinator/manager finalization. Even RELEASED adapters count until
 // their member/cleanup reservations have been verified and cleared by the owner.
 static void Poll(EBG_CacheManager manager)
 {
  if (!s_Held || s_Fault) return;
  string reason;
  if (!SupportedRuntime(reason)) { Fault(reason); return; }
  if (GetGame().GetSaveGameManager() != s_Manager || !s_Manager || !s_Manager.IsSavingEnabled() || s_Manager.GetEnabledSaveTypes() != s_SaveTypes || s_Manager.IsSavingAllowed())
  { Fault("Native saving policy changed; automatic permission release refused"); return; }
  if (HasRecoveryState(manager)) return;
  // Nothing is cached, pending or in recovery: native saves need no hold. The
  // next Full capture takes the permission again before deleting any AI, and
  // waits while a save is busy (TryAcquire). Prepare for save keeps its path.
  if (!s_RestoreRequested) { ReleasePermission(""); return; }
  // New captures must remain held while native queued requests drain and the GM
  // saves. Root's Restore-for-Editing hold persists until an explicit Enable.
  foreach (EBG_CacheZone zone : EBG_CacheZone.Zones)
   if (zone && zone.Enabled && !zone.Editing) return;
  ReleasePermission("All caches restored. Native saving is available; keep zones held for editing until the save completes.");
 }

 // Only for an entirely aborted pre-deletion attempt, after original policy and
 // cleanup ownership rollback. Never call this for a failed restore transaction.
 static void CancelUncommittedAcquire(EBG_CacheManager manager)
 {
  if (!s_Held || s_Fault || s_RestoreRequested || HasRecoveryState(manager)) return;
  string reason;
  if (!SupportedRuntime(reason)) { Fault(reason); return; }
  if (GetGame().GetSaveGameManager() != s_Manager || !s_Manager || !s_Manager.IsSavingEnabled() || s_Manager.GetEnabledSaveTypes() != s_SaveTypes || s_Manager.IsSavingAllowed())
  { Fault("Native saving policy changed during capture rollback"); return; }
  ReleasePermission("All caches restored. Native saving is available; keep zones held for editing until the save completes.");
 }

 protected static void ReleasePermission(string reason)
 {
  s_Manager.SetSavingAllowed(true);
  if (!s_Manager.IsSavingAllowed()) { Fault("Native saving permission did not acknowledge release"); return; }
  s_Held = false; s_RestoreRequested = false; s_Manager = null;
  s_Reason = reason;
 }

 protected static void Fault(string reason)
 {
  s_Fault = true; s_Reason = reason;
  PrintFormat("[EBG FULL SAVE GATE] RETAINED: %1. No saving permission is enabled by recovery or teardown.", reason);
 }

 static void ShutdownForWorldCleanup()
 {
  // Native exit and game-end paths also set false. Never overwrite those denials.
  s_Manager = null; s_Held = false; s_RestoreRequested = false;
  s_Fault = false; s_SaveTypes = 0; s_Reason = ""; s_CDFWarningLogged = false;
  s_AddonWorld = null; s_AddonsRead = false;
 }
}

modded class SCR_EditableGroupComponent
{
 protected bool m_bEBGSaveWaypointOrder;
 void EBG_UseWaypointOrderForSave() { m_bEBGSaveWaypointOrder = true; }

 override SCR_EditableEntityComponent GetChild(int index)
 {
  SCR_EditableEntityComponent child = super.GetChild(index);
  if (!m_bEBGSaveWaypointOrder || !Replication.IsServer() || !SCR_EditableWaypointComponent.Cast(child)) return child;
  EBG_CacheManager manager = EBG_CacheManager.Instance;
  EBG_CacheGroup record;
  if (manager) record = manager.FindGroup(m_Group);
  if (!record || !record.Zone || !record.Zone.Editing)
  {
   m_bEBGSaveWaypointOrder = false;
   return child;
  }

  // CDF restores in child order. Member removal can shuffle the editor's set;
  // project only waypoint slots into queue order, preserving the native leader first.
  array<AIWaypoint> orders = {};
  GetGroupWaypoints(orders);
  if (!orders.Contains(AIWaypoint.Cast(child.GetOwner()))) return child;
  int ordinal;
  for (int i = 0; i < index; i++)
  {
   SCR_EditableWaypointComponent previous = SCR_EditableWaypointComponent.Cast(super.GetChild(i));
   if (previous && orders.Contains(previous.GetAIWaypoint())) ordinal++;
  }
  foreach (AIWaypoint order : orders)
  {
   if (!order) continue;
   SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.GetEditableEntity(order);
   if (!editable || editable.GetParentEntity() != this) continue;
   if (ordinal == 0) return editable;
   ordinal--;
  }
  return child;
 }
}
