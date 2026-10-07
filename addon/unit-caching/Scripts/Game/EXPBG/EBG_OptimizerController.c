// One server-owned operation shared by all controller instances. No extra world
// scan or per-frame scheduler: progress is observed by the existing cache tick.
class EBG_OptimizerControl
{
 static bool Preparing;
 static int State;
 static int Pending;
 static int Blocked;
 static int EnabledZones;
 static int DisabledZones;
 static bool Disabling;
 static string Message;
 // Player id of the GM whose controller command is executing (logging only).
 static int ActingPlayer;
 protected static string s_LastReport;
 protected static string s_LastBlocked;
 protected static ref array<EBG_CacheZone> s_PreparedZones;
 static void Reset()
 {
		EXPBG_LazyStatics_EBG_OptimizerControl();
  Preparing = false; Disabling = false; State = 0; Pending = 0; Blocked = 0;
  EnabledZones = 0; DisabledZones = 0;
  Message = "No global operation"; s_LastReport = ""; s_LastBlocked = "";
  s_PreparedZones.Clear();
 }
 static bool ResumePreparedSession()
 {
  if (Disabling)
  {
   Poll(EBG_CacheManager.Instance);
   if (Disabling)
   {
    Print("[EBG GLOBAL] Enable refused: global disable is still restoring AI", LogLevel.WARNING);
    return false;
   }
  }
  if (!Preparing) return true;
  Poll(EBG_CacheManager.Instance);
  if (State != 2)
  {
   Print("[EBG GLOBAL] Enable refused: prepare/recovery is not Ready", LogLevel.WARNING);
   return false;
  }
  Preparing = false; State = 0;
  Message = "Save pause ended explicitly; other zone holds and settings preserved";
  Publish();
  return true;
 }
 static void BeginPreparation()
 {
		EXPBG_LazyStatics_EBG_OptimizerControl();
  Preparing = true; Disabling = false; State = 1;
  s_PreparedZones.Copy(EBG_CacheZone.Zones);
  Message = "Preparing all cache zones; do not save until Ready";
  Publish();
 }
 static void HoldZones()
 {
  if (!Preparing) return;
  foreach (EBG_CacheZone zone : EBG_CacheZone.Zones)
  {
   if (zone && !zone.Editing) { zone.Editing = true; zone.PublishStatus(); }
  }
 }
 static void Execute(int action)
 {
  if (!Replication.IsServer()) return;
  if (!EBG_CacheManager.IsPortableWorldReady() || EBG_CacheSnapshot.Loading)
  {
   if (action >= 1 && action <= 4) Print(string.Format("[EBG GLOBAL] Command %1 dropped: worldReady=%2 snapshotLoading=%3", action, EBG_CacheManager.IsPortableWorldReady(), EBG_CacheSnapshot.Loading), LogLevel.WARNING);
   return;
  }
  EBG_CacheManager manager = EBG_CacheManager.Get();
  if (action == 3)
  {
   manager.RestoreAllForSave();
   Poll(manager);
   return;
  }
  if (action == 4)
  {
   // Explicit operator escape for recovery holds; see EBG_CacheManager.ReleaseBlocked.
   int released = manager.ReleaseBlocked(ActingPlayer);
   Print(string.Format("[EBG GLOBAL] Release blocked groups: %1 released by player %2", released, ActingPlayer), LogLevel.WARNING);
   Poll(manager);
   return;
  }
  if (action != 1 && action != 2) return;
  foreach (EBG_CacheZone loadingZone : EBG_CacheZone.Zones)
  {
   if (loadingZone && loadingZone.EBG_HasSettingsLoadHold())
   {
    Poll(manager);
    Print("[EBG GLOBAL] Command refused: " + loadingZone.EBG_SettingsLoadReason(), LogLevel.WARNING);
    return;
   }
  }
  if (action == 1 && !ResumePreparedSession())
  {
   Message = "Global action held: preparation/recovery must finish first";
   Publish();
   return;
  }
  if (action == 1 || (action == 2 && State == 2)) Preparing = false;
  Disabling = action == 2 && !Preparing;
  // A few switched zones each tell the acting GM what they do after their next
  // pass; with more, the map labels carry it instead of a burst of chat lines.
  bool notify = EBG_CacheZone.Zones.Count() <= 4;
  foreach (EBG_CacheZone zone : EBG_CacheZone.Zones)
  {
   if (!zone) continue;
   zone.EBG_SetGlobalEnabled(action == 1);
   if (notify) zone.EBG_RequestNotice(ActingPlayer);
  }
  if (!Preparing) State = 0;
  Pending = 0; Blocked = 0;
  if (action == 1) Message = "Enabled all existing cache zones; individual settings preserved";
  else Message = "Disabled all existing cache zones; existing caches restore through the normal scheduler";
  if (action == 2) EBG_FullSaveGate.RequestRestoreForSave();
  // Log the accepted command itself before Poll replaces it with zone totals.
  Publish();
  Poll(manager);
 }
 static void Poll(EBG_CacheManager manager)
 {
		EXPBG_LazyStatics_EBG_OptimizerControl();
  if (!Replication.IsServer() || !manager) return;
  EnabledZones = 0; DisabledZones = 0;
  foreach (EBG_CacheZone countedZone : EBG_CacheZone.Zones)
  {
   if (!countedZone) continue;
   if (countedZone.Enabled) EnabledZones++;
   else DisabledZones++;
  }
  if (!Preparing && !Disabling)
  {
   State = 0; Pending = 0; Blocked = 0;
   Message = string.Format("Zone switches: %1 enabled / %2 disabled", EnabledZones, DisabledZones);
   string loadingReason;
   foreach (EBG_CacheZone loadingZone : EBG_CacheZone.Zones)
   {
    if (!loadingZone || (!loadingZone.EBG_HasSettingsLoadHold() && !loadingZone.HasPendingSettings())) continue;
    Pending++;
    if (loadingReason.IsEmpty())
    {
     if (loadingZone.EBG_HasSettingsLoadHold()) loadingReason = loadingZone.EBG_SettingsLoadReason();
     else loadingReason = "Restoring cached groups before applying queued settings";
    }
   }
   if (Pending > 0)
   {
    State = 6;
    Message += string.Format(" | %1 zones waiting: %2", Pending, loadingReason);
   }
   Publish(); return;
  }
  HoldZones();
  Pending = 0; Blocked = 0;
  string firstFailure;
  array<string> blockedLines = {};
  foreach (EBG_CacheGroup record : manager.Records)
  {
   if (record.Recovery != "" || record.PersistenceIssue != "")
   {
    Blocked++;
    // Where to look: group id, saved anchor and owning module position (or its deletion).
    string where = "deleted module";
    if (record.Zone)
    {
     vector moduleOrigin = record.Zone.GetOrigin();
     where = string.Format("module @ %1, %2", Math.Round(moduleOrigin[0]), Math.Round(moduleOrigin[2]));
    }
    string blocked = string.Format("Group %1 @ %2, %3 (%4): %5 %6", record.Id, Math.Round(record.Anchor[0]), Math.Round(record.Anchor[2]), where, record.Recovery, record.PersistenceIssue);
    if (firstFailure.IsEmpty()) firstFailure = blocked;
    if (Blocked <= 16) blockedLines.Insert(blocked);
   }
   if (record.Full || record.Simulation || record.FullCleanup || record.PersistentScalarRollbackPending || !record.FullGroupId.IsNull() || !record.FullMemberIds.IsEmpty() || record.WakeRequested || record.ReleaseRequested) Pending++;
  }
  foreach (EBG_CacheZone zone : EBG_CacheZone.Zones)
  {
   if (zone && (zone.HasPendingSettings() || zone.EBG_HasSettingsLoadHold())) Pending++;
  }
  if (!EBG_CacheManager.IsPortableWorldReady() || !EBG_MissionPersistence.Ready(manager)) Pending++;
  SaveGameManager saving = GetGame().GetSaveGameManager();
  if (saving && saving.IsBusy()) Pending++;
  if (EBG_FullSaveGate.IsFaulted()) { Blocked++; firstFailure = EBG_FullSaveGate.GetStatus(); }
  if (EBG_CacheSnapshot.Loading || EBG_FullCacheGroup.IsNativeOperationBusy() || SCR_AIGroupSerializer.EBG_HasPendingMemberCallbacks() || EBG_FullSaveGate.IsHeld()) Pending++;
  // A clear/import without the optional companion has no callback into core.
  // End the old session pause only after every original zone is gone and every
  // retained cache/callback is restored. Newly imported settings then resume.
  bool originalRemains;
  foreach (EBG_CacheZone original : s_PreparedZones)
  {
   if (original && EBG_CacheZone.Zones.Contains(original)) originalRemains = true;
  }
  if (Preparing && !s_PreparedZones.IsEmpty() && !originalRemains && Blocked == 0 && !EBG_FullSaveGate.HasRecoveryState(manager) && !EBG_FullSaveGate.IsHeld() && !EBG_CacheSnapshot.Loading)
  {
   Reset();
   foreach (EBG_CacheZone replacement : EBG_CacheZone.Zones)
   {
    if (replacement && !replacement.EBG_HasSettingsLoadHold() && !replacement.HasPendingSettings())
    {
     replacement.Editing = false;
     replacement.PublishStatus();
    }
   }
   Message = "Previous save pause ended after original modules and retained caches were cleared";
   Publish(); return;
  }
  State = 1;
  if (Blocked > 0) State = 3;
  else if (Pending == 0) State = 2;
  Message = string.Format("Prepare: zones=%1 pending=%2 blocked=%3", EBG_CacheZone.Zones.Count(), Pending, Blocked);
  if (!firstFailure.IsEmpty()) Message += " | " + firstFailure;
  if (Blocked > 1) Message += string.Format(" (+%1 more blocked)", Blocked - 1);
  ReportBlocked(blockedLines);
  if (!Preparing && State == 2)
  {
   State = 5; Disabling = false;
   Message = "All zones disabled; restoration complete";
  }
  else if (!Preparing && State == 1) State = 4;
  if (State == 2) Message = "Ready to save: all Optimizer AI restored; zones stay paused until explicit Enable or Disable";
  Publish();
 }
 // Server log, once per change: every blocked record (first 16) with where to find it.
 // Diagnostics only; nothing is released or discarded automatically.
 static void ReportBlocked(array<string> lines)
 {
  string key;
  foreach (string line : lines) key += line + ";";
  if (key == s_LastBlocked) return;
  s_LastBlocked = key;
  foreach (string blocked : lines) Print("[EBG GLOBAL BLOCKED] " + blocked, LogLevel.WARNING);
 }
 static void Publish()
 {
  string report = string.Format("state=%1 enabled=%2 disabled=%3 pending=%4 blocked=%5 %6", State, EnabledZones, DisabledZones, Pending, Blocked, Message);
  if (report != s_LastReport)
  {
   Print("[EBG GLOBAL] " + report);
   s_LastReport = report;
  }
  foreach (EBG_OptimizerController controller : EBG_OptimizerController.Controllers)
  {
   if (controller) controller.RefreshStatus();
  }
 }

	//------------------------------------------------------------------------------------------------
	//! Creates the collections on first use (not in the global static initializer, which has a
	//! per-function instruction limit that large modsets exceed on Windows).
	protected static void EXPBG_LazyStatics_EBG_OptimizerControl()
	{
		if (!s_PreparedZones)
			s_PreparedZones = new array<EBG_CacheZone>();
	}
}

[EntityEditorProps(category: "EXPBG/Optimizer", description: "Global controls for Optimizer cache zones only")]
class EBG_OptimizerControllerClass : GenericEntityClass {}
class EBG_OptimizerController : GenericEntity
{
 static ref array<EBG_OptimizerController> Controllers = {};
 [RplProp()] int PreparationState;
 [RplProp()] int PendingCount;
 [RplProp()] int BlockedCount;
 [RplProp()] string Status;
 [RplProp()] int EnabledCount;
 [RplProp()] int DisabledCount;
 void EBG_OptimizerController(IEntitySource src, IEntity parent) { SetEventMask(EntityEvent.INIT); }
 override void EOnInit(IEntity owner)
 {
  super.EOnInit(owner);
  if (!GetGame().InPlayMode()) return;
  Controllers.Insert(this);
  if (Replication.IsServer()) EBG_OptimizerControl.Poll(EBG_CacheManager.Get());
 }
 void RefreshStatus()
 {
  if (!Replication.IsServer()) return;
  if (PreparationState == EBG_OptimizerControl.State && PendingCount == EBG_OptimizerControl.Pending && BlockedCount == EBG_OptimizerControl.Blocked && Status == EBG_OptimizerControl.Message && EnabledCount == EBG_OptimizerControl.EnabledZones && DisabledCount == EBG_OptimizerControl.DisabledZones) return;
  PreparationState = EBG_OptimizerControl.State;
  PendingCount = EBG_OptimizerControl.Pending;
  BlockedCount = EBG_OptimizerControl.Blocked;
  Status = EBG_OptimizerControl.Message;
  EnabledCount = EBG_OptimizerControl.EnabledZones;
  DisabledCount = EBG_OptimizerControl.DisabledZones;
  Replication.BumpMe();
 }
 void ~EBG_OptimizerController() { Controllers.RemoveItem(this); }
}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EBG_GlobalActionAttribute : EBG_CacheAttribute
{
 override bool IsSerializable() { return false; }
 override void Initialize()
 {
  super.Initialize();
  Enable(m_Key == 200);
 }
 override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
 {
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.Cast(item);
  if (!editable) return null;
  EBG_OptimizerController controller = EBG_OptimizerController.Cast(editable.GetOwner());
  if (!controller) return null;
  if (m_Key == 201 && GetUIInfo()) GetUIInfo().SetDescription(controller.Status + ". Reopen settings to refresh.");
  if (m_Key == 204) return SCR_BaseEditorAttributeVar.CreateFloat(controller.EnabledCount);
  if (m_Key == 205) return SCR_BaseEditorAttributeVar.CreateFloat(controller.DisabledCount);
  if (m_Key == 201) return SCR_BaseEditorAttributeVar.CreateFloat(controller.PreparationState);
  if (m_Key == 202) return SCR_BaseEditorAttributeVar.CreateFloat(controller.PendingCount);
  if (m_Key == 203) return SCR_BaseEditorAttributeVar.CreateFloat(controller.BlockedCount);
  return SCR_BaseEditorAttributeVar.CreateFloat(0);
 }
 override void WriteVariable(Managed item, SCR_BaseEditorAttributeVar var, SCR_AttributesManagerEditorComponent manager, int playerID)
 {
  if (m_Key != 200 || !Replication.IsServer() || !manager || !var) return;
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.Cast(item);
  if (!editable || !EBG_OptimizerController.Cast(editable.GetOwner())) return;
  SCR_EditorManagerEntity editor = manager.GetManager();
  if (!editor || editor.GetPlayerID() != playerID || editor.IsLimited() || editor.GetCurrentMode() != EEditorMode.EDIT) return;
  EBG_OptimizerControl.ActingPlayer = playerID;
  EBG_OptimizerControl.Execute(var.GetFloat());
  EBG_OptimizerControl.ActingPlayer = 0;
 }
}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EBG_GlobalStatusAttribute : EBG_GlobalActionAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EBG_GlobalPendingAttribute : EBG_GlobalActionAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EBG_GlobalBlockedAttribute : EBG_GlobalActionAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EBG_GlobalEnabledCountAttribute : EBG_GlobalActionAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EBG_GlobalDisabledCountAttribute : EBG_GlobalActionAttribute {}
