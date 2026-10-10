// Settings travel through native persistence or the standard GM session attributes.
// Resume only after the complete settings batch and native load binding are ready.
modded class EBG_CacheZone
{
 protected bool m_EBG_RegistrationPending;
 protected bool m_EBG_SessionAttributes;
 protected float m_EBG_SessionSettleUntil;
 protected bool m_EBG_SettingsLoadHold;
 protected bool m_EBG_ResumeAfterLoad;
 protected string m_EBG_SettingsHoldReason;
 // Hold watchdog: one diagnostic line naming the closed gate after 10 s. Only a
 // native registration that never completes is released, after 60 s, the same way
 // as a mission without native saving; tracking starts once persistence is active.
 static const float EBG_HOLD_REPORT_SECONDS = 10;
 static const float EBG_HOLD_RELEASE_SECONDS = 60;
 protected float m_EBG_HoldArmedAt = -1;
 protected bool m_EBG_HoldReported;
 protected bool m_EBG_TrackingDeferred;
 bool EBG_HasSettingsLoadHold() { return m_EBG_SettingsLoadHold;
 }
 string EBG_SettingsLoadReason() { return m_EBG_SettingsHoldReason; }
 void EBG_CompletePortableSettingsLoad()
 {
  // Portable snapshots validate a complete settings batch before import. The
  // global Loading gate remains held until CDF finishes its deferred entities.
  if (!Replication.IsServer() || !EBG_CacheSnapshot.Loading || HasPendingSettings()) return;
  m_EBG_SettingsLoadHold = false;
  m_EBG_ResumeAfterLoad = false;
  m_EBG_SessionAttributes = false;
  Editing = EBG_OptimizerControl.Preparing;
  Replication.BumpMe();
 }

 protected void EBG_ReportPersistenceConfig()
 {
  Resource gm = Resource.Load("{B76E7F1AF7A5D00C}Configs/Systems/Persistence/GameMode/GameMaster.conf");
  Resource zone = Resource.Load("{DBEC4D5170634E86}Configs/Systems/Persistence/Configuration/EXPBG/CacheZone.conf");
  PrintFormat("[EBG ZONE CONFIG] gmValid=%1 zoneValid=%2", gm && gm.IsValid(), zone && zone.IsValid());
  if (gm && gm.IsValid())
  {
   BaseContainer root = gm.GetResource().ToBaseContainer();
   if (root)
   {
    array<string> sources = {};
    root.GetSourceAddons(sources);
    PrintFormat("[EBG ZONE CONFIG] gmSources=%1", sources);
    BaseContainerList groups = root.GetObjectArray("Configurations");
    if (groups)
     for (int i = 0; i < Math.Min(groups.Count(), 32); i++)
     {
      BaseContainer group = groups.Get(i);
      BaseContainerList entries = group.GetObjectArray("Configurations");
      PrintFormat("[EBG ZONE CONFIG] group=%1 class=%2 entries=%3", group.GetName(), group.GetClassName(), entries != null);
      if (group.GetName() == "EXPBG" && entries)
       for (int j = 0; j < Math.Min(entries.Count(), 8); j++) PrintFormat("[EBG ZONE CONFIG] EXPBG entry=%1 class=%2", entries.Get(j).GetName(), entries.Get(j).GetClassName());
     }
   }
  }
  if (zone && zone.IsValid())
  {
   BaseContainer config = zone.GetResource().ToBaseContainer();
   if (!config) return;
   BaseContainer rule = config.GetObject("Rule");
   BaseContainer serializer = config.GetObject("EntitySerializer");
   int priority;
   string target;
   config.Get("Priority", priority);
   if (rule) rule.Get("EntityClass", target);
   string ruleClass, serializerClass;
   if (rule) ruleClass = rule.GetClassName();
   if (serializer) serializerClass = serializer.GetClassName();
   PrintFormat("[EBG ZONE CONFIG] priority=%1 rule=%2 target=%3 serializer=%4 runtimeType=%5", priority, ruleClass, target, serializerClass, Type().ToString());
  }
 }

 override void EBG_PersistenceInitialize()
 {
  // RequestSpawn does not publish tracking/UUID until entity finalization.
  // The authority scheduler registers after finalization, even when this zone
  // receives no entity FRAME events on a dedicated server.
  m_EBG_RegistrationPending = true;
  m_EBG_HoldArmedAt = GetGame().GetWorld().GetWorldTime() * 0.001;
  m_EBG_HoldReported = false;
  EBG_CacheManager.Get();
 }
 protected void EBG_TryPersistentRegistration()
 {
  if (!m_EBG_RegistrationPending || !Replication.IsServer() || EBG_CacheManager.Unloading) return;
  SCR_PersistenceSystem persistence = SCR_PersistenceSystem.GetByEntityWorld(this);
  // Simulation remains available in missions without a native persistence system.
  if (!persistence || EBG_MissionPersistence.NativeSavingUnused(persistence))
  {
   m_EBG_RegistrationPending = false;
   EBG_CacheManager.Get().Register(this);
   Print("[EBG ZONE PERSISTENCE] Native persistence unavailable; zone settings cannot enter native saves.", LogLevel.WARNING);
   return;
  }
  if (persistence.GetState() == EPersistenceSystemState.INIT || persistence.GetState() == EPersistenceSystemState.SETUP) return;
  m_EBG_RegistrationPending = false;
  if (persistence.GetState() != EPersistenceSystemState.ACTIVE)
  {
   m_EBG_ResumeAfterLoad = m_EBG_SessionAttributes;
   Editing = true;
   m_EBG_SettingsLoadHold = true;
   m_EBG_SettingsHoldReason = "Held for editing: native persistence is not active";
   EBG_CacheManager.Get().Register(this);
   PublishStatus();
   return;
  }
  if (!persistence.IsTracked(this)) persistence.StartTracking(this, false);
  SCR_PersistenceJsonSaveContext probe = new SCR_PersistenceJsonSaveContext();
  bool ready = persistence.IsTracked(this) && !persistence.GetId(this).IsNull();
  if (ready) ready = persistence.Serialize(this, probe) == ESerializeResult.OK && probe.IsValid();
  string sample = probe.SaveToString();
  ready = ready && sample.Contains("ebgZoneVersion");
  if (!ready)
  {
   m_EBG_ResumeAfterLoad = m_EBG_SessionAttributes;
   EBG_ReportPersistenceConfig();
   Editing = true;
   m_EBG_SettingsLoadHold = true;
   m_EBG_SettingsHoldReason = "Held for editing: native zone settings persistence unavailable";
  }
  EBG_CacheManager.Get().Register(this);
  PublishStatus();
  if (DebugMessages > 0 || !ready) PrintFormat("[EBG ZONE PERSISTENCE REGISTER] id=%1 tracked=%2 settingsSerializer=%3 settingsLoadHold=%4 json=%5", persistence.GetId(this), persistence.IsTracked(this), ready, m_EBG_SettingsLoadHold, sample);
 }
 void EBG_UpdateSettingsLoad()
 {
  if (m_EBG_TrackingDeferred) EBG_TryDeferredTracking();
  if (!m_EBG_RegistrationPending && !m_EBG_SessionAttributes) return;
  EBG_TryPersistentRegistration();
  if (Replication.IsServer() && m_EBG_SessionAttributes && !EBG_CacheSnapshot.Loading)
  {
   EBG_FlushSavedAttributes();
   ApplyPendingSettings();
  }
  EBG_WatchSettingsHold();
 }
 // Diagnostics only, except the bounded registration release below.
 protected string EBG_SettingsHoldGate(out bool releasable)
 {
  releasable = false;
  if (m_EBG_RegistrationPending)
  {
   string state = "none";
   SCR_PersistenceSystem persistence = SCR_PersistenceSystem.GetByEntityWorld(this);
   if (persistence) state = typename.EnumToString(EPersistenceSystemState, persistence.GetState());
   releasable = EBG_CacheManager.IsPortableWorldReady();
   return "native persistence registration pending (state " + state + ")";
  }
  if (!EBG_CacheManager.IsPortableWorldReady()) return "world not ready: game mode not running or a transition is pending";
  if (HasRestoringRecords()) return "cached groups of this zone are still restoring";
  if (!m_EBG_ResumeAfterLoad) return "hold has no resume request; switch the zone off and on";
  if (!m_EBG_SessionAttributes && !EBG_MissionPersistence.Ready(EBG_CacheManager.Get())) return "native mission metadata binding pending";
  return "saved settings batch still settling";
 }
 protected void EBG_WatchSettingsHold()
 {
  if (!Replication.IsServer() || EBG_CacheSnapshot.Loading || m_EBG_HoldArmedAt < 0) return;
  if (!m_EBG_RegistrationPending && !m_EBG_SettingsLoadHold) { m_EBG_HoldArmedAt = -1; return; }
  float held = GetGame().GetWorld().GetWorldTime() * 0.001 - m_EBG_HoldArmedAt;
  if (held < EBG_HOLD_REPORT_SECONDS) return;
  bool releasable;
  string gate = EBG_SettingsHoldGate(releasable);
  if (!m_EBG_HoldReported)
  {
   m_EBG_HoldReported = true;
   // With CDF in charge native registration never arrives (vanilla persistence off):
   // routine, so only with zone Debug messages on.
   if (EBG_CacheDebug.Verbose()) Print(string.Format("[EBG SETTINGS HOLD] zone=%1 position=%2 held=%3 s gate='%4' reason='%5' pendingKeys=%6 sessionAttributes=%7", GetID(), GetOrigin(), Math.Round(held), gate, m_EBG_SettingsHoldReason, m_PendingKeys.Count(), m_EBG_SessionAttributes), LogLevel.WARNING);
  }
  if (!releasable || held < EBG_HOLD_RELEASE_SECONDS) return;
  // Same outcome as a mission without native saving: settings resume now, the
  // zone joins native saves once persistence becomes active.
  m_EBG_RegistrationPending = false;
  m_EBG_TrackingDeferred = true;
  m_EBG_HoldArmedAt = -1;
  if (EBG_CacheDebug.Verbose()) Print(string.Format("[EBG SETTINGS HOLD] zone=%1 released after %2 s: %3. Settings resume without native tracking until persistence is active.", GetID(), Math.Round(held), gate), LogLevel.WARNING);
  EBG_CacheManager.Get().Register(this);
  if (m_EBG_SessionAttributes)
  {
   EBG_FlushSavedAttributes();
   ApplyPendingSettings();
  }
 }
 protected void EBG_TryDeferredTracking()
 {
  if (!Replication.IsServer() || EBG_CacheManager.Unloading) return;
  SCR_PersistenceSystem persistence = SCR_PersistenceSystem.GetByEntityWorld(this);
  if (!persistence || persistence.GetState() != EPersistenceSystemState.ACTIVE) return;
  m_EBG_TrackingDeferred = false;
  if (!persistence.IsTracked(this)) persistence.StartTracking(this, false);
  PrintFormat("[EBG ZONE PERSISTENCE REGISTER] id=%1 tracked=%2 deferred=1", persistence.GetId(this), persistence.IsTracked(this));
 }
 override void ApplyPendingSettings()
 {
  if (EBG_CacheSnapshot.Loading) return;
  if (m_EBG_ResumeAfterLoad && !m_EBG_RegistrationPending)
  {
   if (m_EBG_SessionAttributes)
   {
    if (!EBG_CacheManager.IsPortableWorldReady() || GetGame().GetWorld().GetWorldTime() * 0.001 < m_EBG_SessionSettleUntil) return;
   }
   else if (!EBG_MissionPersistence.Ready(EBG_CacheManager.Get())) return;
   m_EBG_SettingsLoadHold = false;
   m_EBG_ResumeAfterLoad = false;
   // Pending Enable is applied last by the base method, after geometry and mode.
   super.ApplyPendingSettings();
   if (HasPendingSettings()) { m_EBG_ResumeAfterLoad = true;
   m_EBG_SettingsLoadHold = true;
   return;
   }
   Editing = EBG_OptimizerControl.Preparing;
   SyncCleanupPolicy(true);
   m_EBG_SessionAttributes = false;
   EBG_CacheManager.Get().Register(this);
   Replication.BumpMe();
   PublishStatus();
   if (DebugMessages > 0) PrintFormat("[EBG SETTINGS RESUME] enabled=%1 mode=%2 strategy=%3", Enabled, Mode, Strategy);
   return;
  }
  if (m_EBG_SettingsLoadHold) return;
  super.ApplyPendingSettings();
 }
 void EBG_QueueSavedAttribute(int key, float value)
 {
  if (!Replication.IsServer() || key < 0 || key > 23 || !EBG_MissionPersistence.Finite(value)) return;
  QueueSetting(key, value);
  m_EBG_SessionAttributes = true;
  m_EBG_SessionSettleUntil = GetGame().GetWorld().GetWorldTime() * 0.001 + 1;
  m_EBG_HoldArmedAt = m_EBG_SessionSettleUntil;
  m_EBG_HoldReported = false;
  m_EBG_SettingsLoadHold = true;
  m_EBG_ResumeAfterLoad = true;
  m_EBG_SettingsHoldReason = "Restoring saved module settings";
  Editing = true;
 }
 protected void EBG_FlushSavedAttributes()
 {
  if (!HasPendingSettings() || HasRestoringRecords()) return;
  // CDF invokes the whole attribute batch synchronously. Publish the final values
  // on the next frame, while activation remains held until extraction settles.
  array<int> values = {};
  for (int key = 0; key < 24; key++) values.Insert(GetValue(key));
  for (int i = 0; i < m_PendingKeys.Count(); i++)
  {
   int canonical = EBG_PendingCanonicalKey(m_PendingKeys[i], false, Strategy);
   if (canonical >= 0) values[canonical] = m_PendingValues[i];
  }
  EBG_AssignPersistentValues(values, false);
  m_PendingKeys.Clear(); m_PendingValues.Clear();
  Replication.BumpMe();
 }
 override void PublishStatus()
 {
  if (m_EBG_SettingsLoadHold) Status = m_EBG_SettingsHoldReason;
  super.PublishStatus();
 }
 override void EBG_SetGlobalEnabled(bool enabled)
 {
  if (!Replication.IsServer()) return;
  m_EBG_SettingsLoadHold = false;
  m_EBG_ResumeAfterLoad = false;
  m_EBG_SessionAttributes = false;
  super.EBG_SetGlobalEnabled(enabled);
 }
 override void SetValue(int key, float value)
 {
  // Explicit re-enable is the existing operator action that leaves editing hold.
  if (Replication.IsServer() && key == 0)
  {
   m_EBG_SettingsLoadHold = false;
   m_EBG_ResumeAfterLoad = false;
  }
  super.SetValue(key, value);
 }
 bool EBG_WritePersistentSettings(SaveContext context)
 {
  array<int> values = {};
  for (int key = 0; key < 24; key++) values.Insert(GetValue(key));
  return context.WriteValue("ebgZoneVersion", 5) && context.WriteValue("settings", values) && context.WriteValue("pendingKeys", m_PendingKeys) && context.WriteValue("pendingValues", m_PendingValues);
 }
 protected void EBG_AssignPersistentValues(array<int> values, bool legacy)
 {
  Enabled = values[0];
  Mode = values[1];
  Strategy = values[2];
  Affected = values[3];
  ZoneWake = values[4];
  ZoneSleep = values[5];
  GroupWake = values[6];
  GroupSleep = values[7];
  Height = values[8];
  Above = values[9];
  Below = values[10];
  HeightMargin = values[11];
  SleepDelay = values[12];
  MinimumActive = values[13];
  CaptureNew = values[14];
  Cleanup = values[15];
  CorpseAge = values[16];
  CleanupDelay = values[17];
  Markers = values[18];
  Viewer = values[19];
  Map = values[20];
  DebugMessages = values[21];
  MilitaryOnly = values[22];
  CachedGroupMarkers = values[23];
  NormalizeSettings(legacy);
 }
 protected int EBG_PendingCanonicalKey(int key, bool legacy, int strategy)
 {
  if (key == 8 || key == 11 || key == 14 || key == 19 || key == 20)
  {
   return -1;
  }
  if (key == 13 || key == 17)
  {
   return 12;
  }
  if (key >= 4 && key <= 7)
  {
   if (legacy && ((strategy == 0 && key >= 6) || (strategy == 1 && key <= 5)))
   {
    return -1;
   }
   if (key >= 6)
   {
    return key - 2;
   }
  }
  return key;
 }
 bool EBG_ReadPersistentSettings(LoadContext context)
 {
  int version;
  array<int> values = {}, keys = {};
  array<float> pending = {};
  if (!context.ReadValue("ebgZoneVersion", version) || version < 1 || version > 5 || !context.ReadValue("settings", values) || ((version < 3 && values.Count() != 21) || (version >= 3 && values.Count() != version + 19)) || !context.ReadValue("pendingKeys", keys) || !context.ReadValue("pendingValues", pending) || keys.Count() != pending.Count() || keys.Count() > values.Count())
  {
   return false;
  }
  array<int> seen = {};
  for (int index = 0; index < keys.Count(); index++)
  {
   int key = keys[index];
   float value = pending[index];
   if (key < 0 || key >= values.Count() || seen.Contains(key) || value != value || value > 2147483000 || value < -2147483000)
   {
    return false;
   }
   seen.Insert(key);
  }
  // Earlier schemas have no debug setting: always migrate it to Off.
  if (version < 3) values.Insert(0);
  if (version < 4) values.Insert(1);
  if (version < 5) values.Insert(1);
  // No live SetValue, registration, restore or capture occurs during migration.
  bool legacy = version == 1;
  EBG_AssignPersistentValues(values, legacy);
  array<int> baseline = {}, effective = {}, target = {}, requested = {};
  for (int baseKey = 0; baseKey < 24; baseKey++) baseline.Insert(GetValue(baseKey));
  effective.Copy(values);
  int strategyIndex = keys.Find(2);
  if (strategyIndex >= 0) effective[2] = Math.Clamp(pending[strategyIndex], 0, 1);
  int targetStrategy = Math.Clamp(effective[2], 0, 1);
  for (int queued = 0; queued < keys.Count(); queued++)
  {
   int originalKey = keys[queued];
   int canonical = EBG_PendingCanonicalKey(originalKey, legacy, targetStrategy);
   if (canonical < 0)
   {
    continue;
   }
   if (!requested.Contains(canonical)) requested.Insert(canonical);
   if (legacy) effective[originalKey] = pending[queued];
   else effective[canonical] = pending[queued];
  }
  EBG_AssignPersistentValues(effective, legacy);
  for (int targetKey = 0; targetKey < 24; targetKey++) target.Insert(GetValue(targetKey));
  EBG_AssignPersistentValues(baseline, false);
  m_PendingKeys.Clear();
  m_PendingValues.Clear();
  if (legacy && requested.Contains(12)) target[12] = Math.Max(baseline[12], target[12]);
  // A queued legacy strategy selects its corresponding saved/queued old pair.
  // Also retain the effective pair when a queued extent forces whole-zone wake.
  bool radii = requested.Contains(4) || requested.Contains(5) || requested.Contains(2) || requested.Contains(3);
  array<int> order = {0, 1, 2, 3, 9, 10, 12, 15, 16, 18, 21, 22, 23};
  foreach (int orderedKey : order)
  {
   if (requested.Contains(orderedKey))
   {
    m_PendingKeys.Insert(orderedKey);
    m_PendingValues.Insert(target[orderedKey]);
   }
  }
  if (radii)
  {
   // Expansion first avoids an intermediate wake/sleep clamp losing either end.
   int first = 4, second = 5;
   if (target[4] >= baseline[5]) { first = 5;
   second = 4;
   }
   m_PendingKeys.Insert(first);
   m_PendingValues.Insert(target[first]);
   m_PendingKeys.Insert(second);
   m_PendingValues.Insert(target[second]);
  }
  m_EBG_SettingsLoadHold = true;
  m_EBG_ResumeAfterLoad = true;
  m_EBG_SettingsHoldReason = "Settings restored; waiting for mission loading to finish";
  Editing = true;
  Status = m_EBG_SettingsHoldReason;
  Replication.BumpMe();
  if (DebugMessages > 0) PrintFormat("[EBG ZONE PERSISTENCE LOAD] inputVersion=%1 outputVersion=5 settings=24 originalPending=%2 canonicalPending=%3 resumeAfterLoad=1", version, keys.Count(), m_PendingKeys.Count());
  return true;
 }
}

class EBG_CacheZoneSerializer : GenericEntitySerializer
{
 override static typename GetTargetType()
 {
  return EBG_CacheZone;
 }
 // Match native ScriptedEntitySerializer's default finalization phase. Zone
 // registration is deferred to the authority scheduler so saved settings precede enrollment.
 override static EEntityDeserializeEvent GetDeserializeEvent()
 {
  return EEntityDeserializeEvent.AFTER_FINALIZE;
 }
 override static EDeserializeFailHandling GetDeserializeFailHandling()
 {
  return EDeserializeFailHandling.ERROR;
 }
 override protected ESerializeResult Serialize(notnull IEntity entity, notnull SaveContext context)
 {
  EBG_CacheZone zone = EBG_CacheZone.Cast(entity);
  if (!zone || !context.StartObject("base")) return ESerializeResult.ERROR;
  ESerializeResult baseResult = super.Serialize(entity, context);
  if (!context.EndObject() || baseResult == ESerializeResult.ERROR || !zone.EBG_WritePersistentSettings(context)) return ESerializeResult.ERROR;
  return ESerializeResult.OK;
 }
 override protected bool Deserialize(notnull IEntity entity, notnull LoadContext context)
 {
  EBG_CacheZone zone = EBG_CacheZone.Cast(entity);
  if (!zone) return false;
  if (zone.DebugMessages > 0) PrintFormat("[EBG ZONE DESERIALIZE] enter phase=AFTER_FINALIZE entity=%1 base=%2", entity, context.DoesObjectExist("base"));
  if (context.DoesObjectExist("base"))
  {
   if (!context.StartObject("base")) { Print("[EBG ZONE DESERIALIZE] Failed to enter native base data", LogLevel.ERROR);
   return false;
   }
   bool baseRestored = super.Deserialize(entity, context);
   bool baseEnded = context.EndObject();
   if (!baseRestored || !baseEnded) { PrintFormat("[EBG ZONE DESERIALIZE] Native base failed restored=%1 ended=%2", baseRestored, baseEnded);
   return false;
   }
  }
  bool settingsRestored = zone.EBG_ReadPersistentSettings(context);
  if (zone.DebugMessages > 0 || !settingsRestored) PrintFormat("[EBG ZONE DESERIALIZE] settingsRestored=%1 enabled=%2 editingHold=%3", settingsRestored, zone.Enabled, zone.EBG_HasSettingsLoadHold());
  return settingsRestored;
 }
}
