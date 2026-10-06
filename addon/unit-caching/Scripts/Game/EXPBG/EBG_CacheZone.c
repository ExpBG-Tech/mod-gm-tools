// EXPBG GM Optimizer | M.Pac and K.Edgar
[EntityEditorProps(category: "EXPBG/Optimizer", description: "GM AI cache zone")]
class EBG_CacheZoneClass : GenericEntityClass {}
class EBG_CacheZone : GenericEntity
{
 static ref array<EBG_CacheZone> Zones = {};
 [Attribute("0", UIWidgets.EditBox, "Enable zone after configuration", "0 1 1", category: "EXPBG GM Optimizer"), RplProp()]
 int Enabled;
 [Attribute("0", UIWidgets.EditBox, "Cache mode: 0 Simulation, 1 Full", "0 1 1", category: "EXPBG GM Optimizer"), RplProp()]
 int Mode;
 [Attribute("0", UIWidgets.EditBox, "Activation: 0 Whole Zone, 1 Per Group", "0 1 1", category: "EXPBG GM Optimizer"), RplProp()]
 int Strategy;
 [Attribute("300", UIWidgets.EditBox, "Affected radius (m)", "1 5000 1", category: "EXPBG GM Optimizer"), RplProp()]
 int Affected;
 [Attribute("700", UIWidgets.EditBox, "Wake radius (m)", "1 9999 1", category: "EXPBG GM Optimizer"), RplProp()]
 int ZoneWake;
 [Attribute("900", UIWidgets.EditBox, "Sleep radius (m)", "2 10000 1", category: "EXPBG GM Optimizer"), RplProp()]
 int ZoneSleep;
 // Legacy fields remain in native saves and replication; normalization mirrors
 // the unified controls into them so old resources retain their field layout.
 [Attribute("700"), RplProp()]
 int GroupWake;
 [Attribute("900"), RplProp()]
 int GroupSleep;
 [Attribute("1", UIWidgets.EditBox, "Height filter", "0 1 1", category: "EXPBG GM Optimizer"), RplProp()]
 int Height;
 [Attribute("200", UIWidgets.EditBox, "Above-unit allowance (m)", "0 5000 1", category: "EXPBG GM Optimizer"), RplProp()]
 int Above;
 [Attribute("200", UIWidgets.EditBox, "Below-unit allowance (m)", "0 5000 1", category: "EXPBG GM Optimizer"), RplProp()]
 int Below;
 [Attribute("50", UIWidgets.EditBox, "Sleep height margin (m)", "0 1000 1", category: "EXPBG GM Optimizer"), RplProp()]
 int HeightMargin;
 [Attribute("30", UIWidgets.EditBox, "Continuous clear delay (s)", "1 600 1", category: "EXPBG GM Optimizer"), RplProp()]
 int SleepDelay;
 [Attribute("30"), RplProp()]
 int MinimumActive;
 [Attribute("1", UIWidgets.EditBox, "Capture newly placed groups", "0 1 1", category: "EXPBG GM Optimizer"), RplProp()]
 int CaptureNew;
 [Attribute("1", UIWidgets.CheckBox, "Clean up managed AI bodies and owned equipment", category: "EXPBG GM Optimizer"), RplProp()]
 int Cleanup;
 [Attribute("120", UIWidgets.EditBox, "Minimum corpse age (s)", "30 3600 1", category: "EXPBG GM Optimizer"), RplProp()]
 int CorpseAge;
 [Attribute("30"), RplProp()]
 int CleanupDelay;
 [Attribute("1", UIWidgets.EditBox, "Markers: 0 Off, 1 Selected, 2 All", "0 2 1", category: "EXPBG GM Optimizer"), RplProp()]
 int Markers;
 [Attribute("1", UIWidgets.EditBox, "GM viewer display", "0 1 1", category: "EXPBG GM Optimizer"), RplProp()]
 int Viewer;
 [Attribute("1", UIWidgets.EditBox, "GM map display", "0 1 1", category: "EXPBG GM Optimizer"), RplProp()]
 int Map;
 [Attribute("0", UIWidgets.EditBox, "Debug: 0 Off, 1 GM panel + logs, 2 Also GM identity detail", "0 2 1", category: "EXPBG GM Optimizer"), RplProp()]
 int DebugMessages;
 // Soldiers only (default). A zone enrols a group only when its faction is military
 // (SCR_AIGroupUtilityComponent.IsMilitary). Civilians are cached by EXPBG Ambient
 // Civilians with its own distances and wake rules; a cache zone placed over a town
 // must not take them. Off admits eligible hand-placed civilian groups. Current
 // settings preserve the choice; older schemas default to On during migration.
 [Attribute("1", UIWidgets.CheckBox, "Soldiers only: cache military factions, leave civilian factions alone", category: "EXPBG GM Optimizer"), RplProp()]
 int MilitaryOnly;
 [Attribute("1", UIWidgets.CheckBox, "Show Full-cached group icons to Game Masters in GM", category: "EXPBG GM Optimizer"), RplProp()]
 int CachedGroupMarkers;
 protected string m_DebugLast;
 protected float m_DebugNext;
 // Coalesce changing counters; diagnostics never drive cache or ownership decisions.
 void DebugStatus()
 {
  if (!Replication.IsServer() || EBG_CacheDebug.Level == 0) { m_DebugLast = ""; m_DebugNext = 0; return; }
  float now = GetGame().GetWorld().GetWorldTime() * 0.001;
  if (Status == m_DebugLast || now < m_DebugNext) return;
  m_DebugLast = Status; m_DebugNext = now + 5;
  PrintFormat("[EBG DEBUG ZONE] id=%1 name='%2' position=%3 mode=%4 strategy=%5 status=%6", GetID(), GetName(), GetOrigin(), Mode, Strategy, Status);
 }
 [RplProp()] int ManagedCount;
 [RplProp()] int AliveCount;
 [RplProp()] int DeadCount;
 [RplProp()] int SkippedCount;
 [RplProp()] int CachedCount;
 [RplProp()] int PendingCount;
 [RplProp()] int RecoveryCount;
 [RplProp()] string BlockedReason;
 [RplProp()] string Status = "Initializing";
 [RplProp()] bool Editing;
 // Server diagnostics only: never replicated on their own, saved or read by a
 // cache, ownership or wake decision. EnrollmentNote says why AI inside the
 // affected radius stayed out of the latest enrollment pass; PlayerAwake* count
 // the enrolled groups a player character keeps awake in the latest tick and the
 // nearest such player (XZ metres). Both are folded into Status.
 string EnrollmentNote;
 int EnrollmentPasses;
 int PlayerAwakeCount;
 float PlayerAwakeDistance = -1;
 protected int m_NoticePlayer;
 protected int m_NoticePass;
 // A Game Master saved this module's settings or used a global switch: tell that
 // GM once what the module does, after its next completed enrollment pass (at once
 // while it cannot enroll: disabled, held for editing or restoring).
 void EBG_RequestNotice(int playerId)
 {
  if (!Replication.IsServer() || playerId <= 0) return;
  m_NoticePlayer = playerId;
  m_NoticePass = EnrollmentPasses;
 }
 bool EBG_NoticePending() { return m_NoticePlayer > 0; }
 int EBG_TakeNotice()
 {
  if (m_NoticePlayer <= 0) return 0;
  if (Enabled && !Editing && !HasPendingSettings() && EnrollmentPasses <= m_NoticePass) return 0;
  int playerId = m_NoticePlayer;
  m_NoticePlayer = 0;
  return playerId;
 }
 protected ref array<int> m_PendingKeys = {};
 protected ref array<float> m_PendingValues = {};
 bool HasPendingSettings() { return !m_PendingKeys.IsEmpty(); }
 protected int m_CleanupPolicy = -1;
 void SyncCleanupPolicy(bool force = false)
 {
  if (!Replication.IsServer()) return;
  int policy;
  if (Enabled && Cleanup) policy = 1;
  if (!force && m_CleanupPolicy == policy) return;
  m_CleanupPolicy = policy;
  if (EBG_CacheCleanup.Instance) EBG_CacheCleanup.Instance.ReconcileZonePolicy(this);
 }
 void NormalizeSettings(bool legacy = false)
 {
  Enabled = Math.Clamp(Enabled, 0, 1);
  Mode = Math.Clamp(Mode, 0, 1);
  Strategy = Math.Clamp(Strategy, 0, 1);
  if (legacy)
  {
   if (Strategy == 1)
   {
    ZoneWake = GroupWake;
    ZoneSleep = GroupSleep;
   }
   SleepDelay = Math.Max(SleepDelay, Math.Max(MinimumActive, CleanupDelay));
   if (!Viewer && !Map) Markers = 0;
  }
  Affected = Math.Clamp(Affected, 1, 5000);
  ZoneWake = Math.Clamp(ZoneWake, 1, 9999);
  if (Strategy == 0) ZoneWake = Math.Max(ZoneWake, Affected);
  ZoneSleep = Math.Max(Math.Clamp(ZoneSleep, 2, 10000), ZoneWake + 1);
  GroupWake = ZoneWake;
  GroupSleep = ZoneSleep;
  Above = Math.Clamp(Above, 0, 5000);
  Below = Math.Clamp(Below, 0, 5000);
  SleepDelay = Math.Clamp(SleepDelay, 1, 600);
  MinimumActive = SleepDelay;
  CleanupDelay = SleepDelay;
  CorpseAge = Math.Clamp(CorpseAge, 30, 3600);
  Markers = Math.Clamp(Markers, 0, 2);
  DebugMessages = Math.Clamp(DebugMessages, 0, 2);
  MilitaryOnly = Math.Clamp(MilitaryOnly, 0, 1);
  CachedGroupMarkers = Math.Clamp(CachedGroupMarkers, 0, 1);
  Height = 1;
  HeightMargin = 50;
  CaptureNew = 1;
  Cleanup = Math.Clamp(Cleanup, 0, 1);
  Viewer = 1;
  Map = 1;
 }
 protected void QueueSetting(int key, float value)
 {
  int index = m_PendingKeys.Find(key);
  if (index >= 0) m_PendingValues[index] = value;
  else { m_PendingKeys.Insert(key); m_PendingValues.Insert(value); }
 }
 protected bool HasRestoringRecords()
 {
  if (!EBG_CacheManager.Instance) return false;
  foreach (EBG_CacheGroup record : EBG_CacheManager.Instance.Records)
   if (record.Zone == this && (record.Full || record.Simulation || record.PersistentScalarRollbackPending)) return true;
  return false;
 }
 void ApplyPendingSettings()
 {
  // Pending settings themselves request wake through the normal scheduler. Do
  // not latch WakeRequested: reverting an edit must cancel unstarted work.
  if (!HasPendingSettings()) return;
  if (HasRestoringRecords()) return;
  array<int> keys = {}; keys.Copy(m_PendingKeys);
  array<float> values = {}; values.Copy(m_PendingValues);
  m_PendingKeys.Clear(); m_PendingValues.Clear();
  // Resume only after queued geometry/mode values are installed, so enrollment
  // cannot use the pre-load extent while migrating a disabled zone.
  int enableIndex = keys.Find(0);
  for (int i = 0; i < keys.Count(); i++)
  {
   if (keys[i] != 0) SetValue(keys[i], values[i]);
  }
  if (enableIndex >= 0)
  {
   if (EBG_OptimizerControl.Preparing)
   {
    Enabled = Math.Clamp(values[enableIndex], 0, 1);
    Editing = true;
    SyncCleanupPolicy(true);
    Replication.BumpMe();
   }
   else SetValue(0, values[enableIndex]);
  }
 }
 protected int NormalizedValue(int key, float value)
 {
  switch (key)
  {
   case 0: case 1: case 2: case 15: case 22: case 23: return Math.Clamp(value, 0, 1);
   case 3: return Math.Clamp(value, 1, 5000);
   case 4:
    int wake = Math.Clamp(value, 1, 9999);
    if (Strategy == 0) wake = Math.Max(wake, Affected);
    return wake;
   case 5: return Math.Max(Math.Clamp(value, 2, 10000), ZoneWake + 1);
   case 9: case 10: return Math.Clamp(value, 0, 5000);
   case 12: return Math.Clamp(value, 1, 600);
   case 16: return Math.Clamp(value, 30, 3600);
   case 18: case 21: return Math.Clamp(value, 0, 2);
  }
  return -1;
 }
 protected void ResetClearTimers()
 {
  if (!EBG_CacheManager.Instance) return;
  EBG_CacheManager.Instance.InvalidateProtection();
  foreach (EBG_CacheGroup record : EBG_CacheManager.Instance.Records)
   if (record.Zone == this)
   {
    record.ClearSince = -1;
    record.CleanupClearSince = -1;
    record.CleanupNextAttempt = 0;
   }
 }
 protected void BumpStatus() { Replication.BumpMe(); }
 protected bool m_StatusPublished, m_PublishedEditing;
 protected int m_PublishedEnabled, m_PublishedManaged, m_PublishedAlive, m_PublishedDead;
 protected int m_PublishedSkipped, m_PublishedCached, m_PublishedPending, m_PublishedRecovery;
 protected string m_PublishedStatus, m_PublishedBlockedReason;
 void PublishStatus()
 {
  DebugStatus();
  if (m_StatusPublished && m_PublishedStatus == Status && m_PublishedBlockedReason == BlockedReason && m_PublishedEditing == Editing && m_PublishedEnabled == Enabled
   && m_PublishedManaged == ManagedCount && m_PublishedAlive == AliveCount && m_PublishedDead == DeadCount
   && m_PublishedSkipped == SkippedCount && m_PublishedCached == CachedCount
   && m_PublishedPending == PendingCount && m_PublishedRecovery == RecoveryCount) return;
  m_StatusPublished = true; m_PublishedStatus = Status; m_PublishedBlockedReason = BlockedReason; m_PublishedEditing = Editing; m_PublishedEnabled = Enabled;
  m_PublishedManaged = ManagedCount; m_PublishedAlive = AliveCount; m_PublishedDead = DeadCount;
  m_PublishedSkipped = SkippedCount; m_PublishedCached = CachedCount;
  m_PublishedPending = PendingCount; m_PublishedRecovery = RecoveryCount;
  BumpStatus();
 }
 void EBG_PersistenceInitialize() { EBG_CacheManager.Get().Register(this); }
 override void EOnInit(IEntity owner)
 {
  super.EOnInit(owner);
  if (!GetGame().InPlayMode()) return;
  EBG_CacheManager.Unloading = false;
  Zones.Insert(this);
  if (Replication.IsServer())
  {
   NormalizeSettings(true);
   EBG_PersistenceInitialize();
  }
 }
 void EBG_CacheZone(IEntitySource src, IEntity parent)
 {
  SetEventMask(EntityEvent.INIT | EntityEvent.FRAME);
 }
 override void EOnFrame(IEntity owner, float timeSlice)
 {
  EBG_CacheVisuals.Update(this, timeSlice);
 }
 void ~EBG_CacheZone()
 {
  EBG_CacheVisuals.RemoveZone(this);
  Zones.RemoveItem(this);
  if (!EBG_CacheManager.Unloading && EBG_CacheManager.Instance)
  {
   Enabled = 0;
   SyncCleanupPolicy(true);
   EBG_CacheManager.Instance.Release(this);
  }
 }
 float GetValue(int key)
 {
  switch (key)
  {
   case 0: return Enabled;
   case 1: return Mode;
   case 2: return Strategy;
   case 3: return Affected;
   case 4: return ZoneWake;
   case 5: return ZoneSleep;
   case 6: return GroupWake;
   case 7: return GroupSleep;
   case 8: return Height;
   case 9: return Above;
   case 10: return Below;
   case 11: return HeightMargin;
   case 12: return SleepDelay;
   case 13: return MinimumActive;
   case 14: return CaptureNew;
   case 15: return Cleanup;
   case 16: return CorpseAge;
   case 17: return CleanupDelay;
   case 18: return Markers;
   case 19: return Viewer;
   case 20: return Map;
   case 21: return DebugMessages;
   case 22: return MilitaryOnly;
   case 23: return CachedGroupMarkers;
  }
  return 0;
 }
 void EBG_SetGlobalEnabled(bool enabled)
 {
  if (!Replication.IsServer()) return;
  int pending = m_PendingKeys.Find(0);
  if (pending >= 0) { m_PendingKeys.Remove(pending); m_PendingValues.Remove(pending); }
  Enabled = enabled;
  Editing = EBG_OptimizerControl.Preparing;
  ResetClearTimers();
  SyncCleanupPolicy(true);
  EBG_CacheManager.Get().Register(this);
  Replication.BumpMe();
 }
 void SetValue(int key, float value)
 {
  if (!Replication.IsServer() || !EBG_MissionPersistence.Finite(value)) return;
  // A one-shot dropdown always reads None. Only its selected action is applied.
  if (key == 104)
  {
   if (value == 1) SetValue(101, 1);
   else if (value == 2) SetValue(102, 1);
   else if (value == 3) SetValue(103, 1);
   else if (value == 4) SetValue(0, 1);
   return;
  }
  // Old script integrations can address the merged controls. Removed toggles
  // cannot turn off the automatic ownership and protection rules.
  if (key == 6) key = 4;
  else if (key == 7) key = 5;
  else if (key == 13 || key == 17) key = 12;
  if (key == 8 || key == 11 || key == 14 || key == 19 || key == 20) return;
  if (key == 100) { EBG_CacheManager.Get().Refresh(this); return; }
  if (key == 103) { EBG_CacheManager.Get().RestoreAllForSave(); return; }
  if (key == 101)
  {
   Editing = true;
   if (EBG_CacheManager.Get().RestoreZone(this, true)) Status = "Held active for editing";
   else Status = "Restoration queued; recovery snapshots retained";
   PublishStatus(); return;
  }
  if (key == 102)
  {
   Enabled = 0;
   SyncCleanupPolicy(true);
   bool released = EBG_CacheManager.Get().RestoreZone(this);
   EBG_CacheManager.Get().Release(this);
   ManagedCount = 0; AliveCount = 0; DeadCount = 0; SkippedCount = 0; CachedCount = 0;
   if (released) Status = "Disabled and released";
   else Status = "Disabled; survivor recovery record retained";
   PublishStatus(); return;
  }
  if (key == 0 && value != 0 && !EBG_OptimizerControl.ResumePreparedSession()) return;
  int target = NormalizedValue(key, value);
  if (target < 0) return;
  int pendingIndex = m_PendingKeys.Find(key);
  bool resume = key == 0 && target == 1 && Editing;
  if (GetValue(key) == target && !resume)
  {
   if (pendingIndex >= 0)
   {
    m_PendingKeys.Remove(pendingIndex); m_PendingValues.Remove(pendingIndex);
    PublishStatus();
   }
   return;
  }
  bool coldChange = key == 1 || (key == 0 && target == 0);
  if (coldChange && HasRestoringRecords())
  {
   QueueSetting(key, target);
   Status = "Settings queued until saved groups are restored"; PublishStatus(); return;
  }
  if (pendingIndex >= 0) { m_PendingKeys.Remove(pendingIndex); m_PendingValues.Remove(pendingIndex); }
  value = target;
  switch (key)
  {
   case 0: Enabled = Math.Clamp(value, 0, 1); Editing = false; break;
   case 1: Mode = Math.Clamp(value, 0, 1); break;
   case 2: Strategy = Math.Clamp(value, 0, 1); break;
   case 3: Affected = Math.Clamp(value, 1, 5000); break;
   case 4: ZoneWake = Math.Clamp(value, 1, 9999); break;
   case 5: ZoneSleep = Math.Clamp(value, 2, 10000); break;
   case 9: Above = Math.Clamp(value, 0, 5000); break;
   case 10: Below = Math.Clamp(value, 0, 5000); break;
   case 12: SleepDelay = Math.Clamp(value, 1, 600); break;
   case 15: Cleanup = Math.Clamp(value, 0, 1); break;
   case 16: CorpseAge = Math.Clamp(value, 30, 3600); break;
   case 18: Markers = Math.Clamp(value, 0, 2); break;
   case 21: DebugMessages = Math.Clamp(value, 0, 2); break;
   case 22: MilitaryOnly = Math.Clamp(value, 0, 1); break;
   case 23: CachedGroupMarkers = Math.Clamp(value, 0, 1); break;
  }
  NormalizeSettings();
  if ((key >= 2 && key <= 12) || key == 15 || key == 16) ResetClearTimers();
  if (key == 0 && Enabled) { Editing = false; EBG_CacheManager.Get().Register(this); }
  if (key == 0 || key == 15) SyncCleanupPolicy(true);
  if (EBG_OptimizerControl.Preparing) Editing = true;
  Replication.BumpMe();
 }
}
