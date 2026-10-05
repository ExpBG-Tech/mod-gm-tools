// Local native-start recovery for one owner's recording; no shared clip blacklist.
class EAS_WarStartRecovery
{
 protected int m_Failures;
 protected float m_NextDue = -1;
 bool CanAttempt(float now) { return m_Failures < 3 && now >= m_NextDue; }
 int Failed(float now)
 {
  if (m_Failures < 3) m_Failures++;
  m_NextDue = now + 5 * m_Failures;
  return m_Failures;
 }
 void Succeeded() { m_Failures = 0; m_NextDue = -1; }
}

[EntityEditorProps(category: "EXPBG/Ambient War Module", description: "Manually placed positional battle area")]
class EAS_AmbientModuleClass : GenericEntityClass {}

class EAS_AmbientModule : GenericEntity
{
 [Attribute("-1", UIWidgets.Hidden, "Legacy sound mode", category: "EXPBG Ambient War Module"), RplProp()]
 int Mode;
 [Attribute("0", UIWidgets.Hidden, "Legacy intensity", "0 2 1", category: "EXPBG Ambient War Module"), RplProp()]
 int Intensity;
 [Attribute("200", UIWidgets.EditBox, "Spread radius (m)", "0 500 25", category: "EXPBG Ambient War Module"), RplProp()]
 int Spread;
 [Attribute("1500", UIWidgets.EditBox, "Audible distance from each source (m)", "100 3000 50", category: "EXPBG Ambient War Module"), RplProp()]
 int Range;
 [Attribute("25", UIWidgets.Hidden, "Legacy volume (%)", "0 100 1", category: "EXPBG Ambient War Module"), RplProp()]
 int Volume;
 [Attribute("0", UIWidgets.EditBox, "Run for (minutes; 0 = Unlimited)", "0 120 1", category: "EXPBG Ambient War Module"), RplProp()]
 int Minutes;
 [Attribute("0", UIWidgets.CheckBox, "Enabled", category: "EXPBG Ambient War Module"), RplProp()]
 int Enabled;
 [Attribute("0", UIWidgets.CheckBox, "Debug logs and GM overlay", category: "EXPBG Ambient War Module"), RplProp(onRplName: "SettingsReceived")]
 int DebugEnabled;
 [Attribute("1", UIWidgets.CheckBox, "Gunshots", category: "EXPBG Ambient War Module"), RplProp()]
 int GunshotsEnabled;
 [Attribute("0", UIWidgets.EditBox, "Gunshot recordings: 0 Distant, 1 Near, 2 Random", "0 2 1", category: "EXPBG Ambient War Module"), RplProp()]
 int GunshotsSet;
 [Attribute("1", UIWidgets.CheckBox, "Explosions", category: "EXPBG Ambient War Module"), RplProp()]
 int ExplosionsEnabled;
 [Attribute("0", UIWidgets.EditBox, "Explosion recordings: 0 Distant, 1 Near, 2 Random", "0 2 1", category: "EXPBG Ambient War Module"), RplProp()]
 int ExplosionsSet;
 [Attribute("0", UIWidgets.CheckBox, "Jet flybys", category: "EXPBG Ambient War Module"), RplProp()]
 int JetsEnabled;
 [Attribute("2", UIWidgets.EditBox, "Jet recording: 0 Flyby 1, 1 Flyby 2, 2 Random", "0 2 1", category: "EXPBG Ambient War Module"), RplProp()]
 int JetSelection;
 [Attribute("0", UIWidgets.CheckBox, "Premixed battle", category: "EXPBG Ambient War Module"), RplProp()]
 int PremixedEnabled;
 [Attribute("0", UIWidgets.EditBox, "War mix: 0 Random, 1 Gunfire 1, 2 Gunfire 2, 3 Outpost, 4 Kent WW2", "0 4 1", category: "EXPBG Ambient War Module"), RplProp()]
 int PremixedSelection;
 // -1 inherits authored legacy overrides; the current prefab supplies 50% / Low through those defaults.
 [Attribute("-1", UIWidgets.EditBox, "Gunshots volume (%; -1 = inherit)", "-1 100 1", category: "EXPBG Ambient War Module"), RplProp()]
 int GunshotsVolume;
 [Attribute("-1", UIWidgets.EditBox, "Explosions volume (%; -1 = inherit)", "-1 100 1", category: "EXPBG Ambient War Module"), RplProp()]
 int ExplosionsVolume;
 [Attribute("-1", UIWidgets.EditBox, "Jets volume (%; -1 = inherit)", "-1 100 1", category: "EXPBG Ambient War Module"), RplProp()]
 int JetsVolume;
 [Attribute("-1", UIWidgets.EditBox, "Premixed volume (%; -1 = inherit)", "-1 100 1", category: "EXPBG Ambient War Module"), RplProp()]
 int PremixedVolume;
 [Attribute("-1", UIWidgets.EditBox, "Gunshots frequency: -1 inherit, 0 Low, 1 Medium, 2 High, 3 Intense", "-1 3 1", category: "EXPBG Ambient War Module"), RplProp()]
 int GunshotsFrequency;
 [Attribute("-1", UIWidgets.EditBox, "Explosions frequency: -1 inherit, 0 Low, 1 Medium, 2 High, 3 Intense", "-1 3 1", category: "EXPBG Ambient War Module"), RplProp()]
 int ExplosionsFrequency;
 [Attribute("-1", UIWidgets.EditBox, "Jets frequency: -1 inherit, 0 Low, 1 Medium, 2 High, 3 Intense", "-1 3 1", category: "EXPBG Ambient War Module"), RplProp()]
 int JetsFrequency;
 // Persistent description of the current long recording for late listeners.
 // Each listener starts it from the beginning; exact audio phase is not synced.
 [RplProp()] protected int m_MixIndex = -1;
 [RplProp()] protected int m_MixRevision = -1;
 [RplProp()] protected int m_MixGeneration;
 [RplProp()] protected vector m_MixPosition;
 [RplProp()] protected float m_MixGain;
 protected int m_PlayedMixGeneration = -1;
 protected float m_NextMixAttempt;
 protected ref array<ref EAS_WarStartRecovery> m_ClipRecovery;
 [RplProp(onRplName: "SettingsReceived")] protected int m_Revision;
 [RplProp()] protected bool m_Ready;
 [RplProp()] protected bool m_Admitted;
 [RplProp(onRplName: "ActivationReceived")] protected bool m_ProximityActive;
 [RplProp()] int AdmissionSlot = -1;
 [RplProp()] int AdmissionGeneration;
 [RplProp()] string Status = "Initializing";
 [RplProp()] int Scheduled;
 [RplProp()] int Skipped;
 // Retain the replication schema; no UI consumes these legacy countdowns.
 // Never dirty the entity periodically just to update diagnostic fields.
 [RplProp()] int NextIn;
 [RplProp()] int Remaining;
 protected int m_Sequence;
 protected int m_LastSequence;
 protected bool m_Initialized;
 protected bool m_Queued;
 protected bool m_InvalidPending;
 protected ref array<int> m_Pending;
 protected ref array<int> m_PendingWritten;
 protected bool m_HasSourcePosition;
 protected vector m_PreviousSourcePosition;
 protected SCR_AttributesManagerEditorComponent m_EditManager;
 protected int m_EditPlayer = -1;
 protected bool m_EditorPending;
 protected bool m_SavedPending;
 protected int m_LocalRevision = -1;
 protected int m_LocalAdmissionGeneration;
 protected RplComponent m_Replication;
 protected ref EAS_Schedule m_Schedule;
 protected vector m_Anchor;
 vector LastPosition;
 string LastSound;
 string LocalIdentity;
 float LocalLastAccepted = -100000;

 // Failed starts retry after 5/10 seconds, then park this local owner/clip.
 // Activation, movement and edits never erase this bound; recreate a parked module.
 bool CanAttemptClip(int index, float now)
 {
  if (index < 0 || index >= EAS_Runtime.Bank().Count()) return false;
  return !m_ClipRecovery || !m_ClipRecovery[index] || m_ClipRecovery[index].CanAttempt(now);
 }

 int FailedClip(int index, float now)
 {
  if (index < 0 || index >= EAS_Runtime.Bank().Count()) return 0;
  if (!m_ClipRecovery)
  {
   m_ClipRecovery = {};
   m_ClipRecovery.Resize(EAS_Runtime.Bank().Count());
  }
  if (!m_ClipRecovery[index]) m_ClipRecovery[index] = new EAS_WarStartRecovery();
  return m_ClipRecovery[index].Failed(now);
 }

 void ClipStarted(int index)
 {
  if (!m_ClipRecovery || index < 0 || index >= m_ClipRecovery.Count()) return;
  if (m_ClipRecovery[index]) m_ClipRecovery[index].Succeeded();
 }

 void TraceSettings(string reason)
 {
  if (!EAS_Diagnostics.Enabled(this)) return;
  string values;
  for (int key = 0; key < 23; key++) { if (key > 0) values += ","; values += Value(key).ToString(); }
  EAS_Diagnostics.Event(reason, this, string.Format("kind=war revision=%1 slot=%2 generation=%3 settings=%4 active=%5 admitted=%6 status=%7", m_Revision, AdmissionSlot, AdmissionGeneration, values, m_ProximityActive, m_Admitted, Status));
 }

 static bool IsRuntime()
 {
  if (!GetGame() || !GetGame().GetWorld()) return false;
#ifdef WORKBENCH
  if (!GetGame().InPlayMode()) return false;
#endif
  return true;
 }

 bool IsAuthority()
 {
  if (m_Replication) return m_Replication.Role() == RplRole.Authority;
  return !Replication.IsRunning();
 }

 void AssignAdmission(int slot, int generation)
 {
  if (!IsAuthority() || (AdmissionSlot == slot && AdmissionGeneration == generation)) return;
  AdmissionSlot = slot; AdmissionGeneration = generation;
  m_Revision++; Replication.BumpMe();
 }

 override protected void EOnInit(IEntity owner)
 {
  if (!IsRuntime() || SCR_Global.IsEditMode(owner)) return;
  m_Initialized = true;
  m_Replication = RplComponent.Cast(FindComponent(RplComponent));
  m_Anchor = GetOrigin();
  if (IsAuthority())
  {
   m_Ready = true;
   array<int> configured = Settings();
   // Old authored worlds retain explicit Mode 0..4. Migrate once on authority;
   // subsequent edits and saves carry the canonical -1 plus category switches.
   if (Mode >= 0) configured.Resize(8);
   else
   {
    for (int volumeKey = 16; volumeKey < 20; volumeKey++) if (configured[volumeKey] == -1) configured[volumeKey] = Volume;
    for (int frequencyKey = 20; frequencyKey < 23; frequencyKey++) if (configured[frequencyKey] == -1) configured[frequencyKey] = Intensity;
   }
   array<int> normalized = EAS_ContentSettings.Normalize(configured);
   bool validSettings = normalized != null;
   if (validSettings) AssignSettings(normalized);
   if (!validSettings) Enabled = 0;
   m_Schedule = new EAS_Schedule(Math.RandomInt(1, 2147483647), EAS_Runtime.Bank().Count());
   m_Schedule.Restart(EAS_Runtime.Now(), true);
   RefreshAdmission();
   if (!validSettings) { Status = "Invalid settings"; EAS_Diagnostics.Error("initial-settings", this, "invalid; playback disabled"); }
   m_Revision++;
   Replication.BumpMe();
  }
  SettingsReceived();
  TraceSettings("initialized");
 }

 bool Valid()
 {
  if (Mode != -1) return false;
  for (int key = 0; key < 23; key++) if (!EAS_ContentSettings.ValidValue(key, Value(key))) return false;
  return true;
 }

 array<int> Settings()
 {
  array<int> values = {};
  for (int key = 0; key < 23; key++) values.Insert(Value(key));
  return values;
 }

 protected void AssignSettings(array<int> values)
 {
  Mode = values[0]; Intensity = values[1]; Spread = values[2]; Range = values[3];
  Volume = values[4]; Minutes = values[5]; Enabled = values[6]; DebugEnabled = values[7];
  GunshotsEnabled = values[8]; GunshotsSet = values[9]; ExplosionsEnabled = values[10]; ExplosionsSet = values[11];
  JetsEnabled = values[12]; JetSelection = values[13]; PremixedEnabled = values[14]; PremixedSelection = values[15];
  GunshotsVolume = values[16]; ExplosionsVolume = values[17]; JetsVolume = values[18]; PremixedVolume = values[19];
  GunshotsFrequency = values[20]; ExplosionsFrequency = values[21]; JetsFrequency = values[22];
 }

 int Value(int key)
 {
  switch (key)
  {
   case 0: return Mode;
   case 1: return Intensity;
   case 2: return Spread;
   case 3: return Range;
   case 4: return Volume;
   case 5: return Minutes;
   case 6: return Enabled;
   case 7: return DebugEnabled;
   case 8: return GunshotsEnabled;
   case 9: return GunshotsSet;
   case 10: return ExplosionsEnabled;
   case 11: return ExplosionsSet;
   case 12: return JetsEnabled;
   case 13: return JetSelection;
   case 14: return PremixedEnabled;
   case 15: return PremixedSelection;
   case 16: return GunshotsVolume;
   case 17: return ExplosionsVolume;
   case 18: return JetsVolume;
   case 19: return PremixedVolume;
   case 20: return GunshotsFrequency;
   case 21: return ExplosionsFrequency;
   case 22: return JetsFrequency;
  }
  return -1;
 }

 // Native editor calls this only after authority/player ownership checks.
 // All writes in ConfirmEditingServer are synchronous; one deferred apply is atomic.
 void QueueSetting(int key, float value)
 {
  if (!IsAuthority() || key < 0 || key > 22 || !m_Initialized) return;
  if (!m_Pending)
  {
   m_Pending = Settings();
   m_PendingWritten = {};
   m_PendingWritten.Resize(23);
  }
  if (!EAS_ContentSettings.ValidValue(key, value)) m_InvalidPending = true;
  else { m_Pending[key] = value; m_PendingWritten[key] = 1; }
  if (m_Queued) return;
  m_Queued = true;
  GetGame().GetCallqueue().CallLater(ApplyPending, 0, false);
 }

 void QueueEditorSetting(int key, float value, SCR_AttributesManagerEditorComponent manager, int playerID)
 {
  if (!IsAuthority() || !EditorAllowed(manager, playerID)) return;
  if (m_SavedPending) { m_InvalidPending = true; return; }
  if (m_EditorPending && (m_EditManager != manager || m_EditPlayer != playerID)) { m_InvalidPending = true; return; }
  m_EditorPending = true; m_EditManager = manager; m_EditPlayer = playerID;
  QueueSetting(key, value);
 }

 // CDF/native session loader calls every field synchronously. Restore only once
 // on the next queue turn, so an Enabled-first document cannot start old audio.
 void QueueSavedSetting(int key, float value)
 {
  if (!Replication.IsServer() || !IsAuthority() || !m_Initialized || key < 0 || key > 22) return;
  if (m_EditorPending) { m_InvalidPending = true; return; }
  m_SavedPending = true;
  QueueSetting(key, value);
 }

 protected bool EditorAllowed(SCR_AttributesManagerEditorComponent manager, int playerID)
 {
  if (!manager) return false;
  SCR_EditorManagerEntity editor = manager.GetManager();
  return editor && editor.GetPlayerID() == playerID && !editor.IsLimited() && editor.GetCurrentMode() == EEditorMode.EDIT;
 }

 // Native saved configuration starts a fresh run; never restores old event/audio state.
 bool RestoreSettings(array<int> values)
 {
  if (!IsAuthority()) return false;
  array<int> normalized = EAS_ContentSettings.Normalize(values);
  if (!normalized) { EAS_Diagnostics.Error("restore", this, "invalid war settings; previous settings retained"); return false; }
  AssignSettings(normalized);
  if (m_Initialized)
  {
   if (EAS_Runtime.Instance) EAS_Runtime.Instance.StopSounds(this, "restore");
   m_Schedule.Restart(EAS_Runtime.Now(), true);
   RefreshAdmission(); m_Revision++; Replication.BumpMe(); SettingsReceived();
  }
  TraceSettings("settings-restored");
  return true;
 }

 protected void ApplyPending()
 {
  m_Queued = false;
  if (!m_Initialized || !IsAuthority() || !m_Pending) return;
  array<int> values = m_Pending;
  m_Pending = null;
  bool saved = m_SavedPending;
  m_SavedPending = false;
  if (m_EditorPending && !EditorAllowed(m_EditManager, m_EditPlayer)) m_InvalidPending = true;
  m_EditorPending = false; m_EditManager = null; m_EditPlayer = -1;
  if (saved)
  {
   // Explicit category writes win over legacy globals regardless of CDF field order.
   for (int volumeKey = 16; volumeKey < 20; volumeKey++) if (m_PendingWritten[4] && !m_PendingWritten[volumeKey]) values[volumeKey] = values[4];
   for (int frequencyKey = 20; frequencyKey < 23; frequencyKey++) if (m_PendingWritten[1] && !m_PendingWritten[frequencyKey]) values[frequencyKey] = values[1];
  }
  if (values[0] >= 0)
  {
   array<int> legacy = {};
   for (int legacyKey = 0; legacyKey < 8; legacyKey++) legacy.Insert(values[legacyKey]);
   legacy = EAS_ContentSettings.Normalize(legacy);
   if (legacy) for (int categoryKey = 8; categoryKey < 23; categoryKey++) if (m_PendingWritten[categoryKey]) legacy[categoryKey] = values[categoryKey];
   values = legacy;
  }
  m_PendingWritten = null;
  values = EAS_ContentSettings.Normalize(values);
  bool valid = !m_InvalidPending && values != null;
  m_InvalidPending = false;
  if (!valid) { Status = "Edit rejected; previous settings retained"; Replication.BumpMe(); Print("[EAS ERROR] Invalid war settings batch; previous settings retained", LogLevel.WARNING); return; }
  if (saved) { RestoreSettings(values); TraceSettings("session-loaded"); return; }
  bool durationChanged = Minutes != values[5];
  bool newRun = !Enabled && values[6] == 1;
  bool playbackChanged;
  for (int key = 0; key < 23; key++) if (key != 5 && key != 7 && Value(key) != values[key]) playbackChanged = true;
  AssignSettings(values);
  if (playbackChanged)
  {
   if (EAS_Runtime.Instance) EAS_Runtime.Instance.StopSounds(this, "settings");
   m_Schedule.Restart(EAS_Runtime.Now(), newRun);
  }
  // Changing the deadline does not stop audio; keep its active reservations.
  if (!playbackChanged && durationChanged) m_Schedule.Restart(EAS_Runtime.Now(), false, true);
  if (Enabled && EAS_Logic.Expired(m_Schedule.RunStarted, Minutes, EAS_Runtime.Now())) { Enabled = 0; playbackChanged = true; }
  RefreshAdmission();
  if (playbackChanged) m_Revision++;
  Replication.BumpMe();
  SettingsReceived();
  TraceSettings("settings-applied");
 }

 protected void RefreshAdmission()
 {
  if (!Enabled)
  {
   m_Admitted = false;
   if (EAS_Runtime.Instance) EAS_Runtime.Instance.Remove(this, "disabled");
   Status = "Stopped";
  }
  else
  {
   EAS_Runtime runtime = EAS_Runtime.Get();
   m_Admitted = runtime && runtime.Add(this);
   if (!m_Admitted) { Enabled = 0; Status = "Capacity hold (32 active modules)"; }
   else Status = "Running";
  }
 }

 protected void SettingsReceived()
 {
  if (!m_Initialized || !m_Ready || !IsRuntime()) return;
  if (m_LocalRevision != m_Revision && EAS_Runtime.Instance) EAS_Runtime.Instance.CancelPending(this);
  if (!IsAuthority())
  {
   if (EAS_Runtime.Instance)
   {
    if (m_LocalAdmissionGeneration != AdmissionGeneration) EAS_Runtime.Instance.Remove(this);
    else if (m_LocalRevision != m_Revision) EAS_Runtime.Instance.StopSounds(this);
   }
   if (Enabled && m_Admitted && Valid()) EAS_Runtime.Get().Add(this);
   else if (EAS_Runtime.Instance) EAS_Runtime.Instance.Remove(this);
  }
  EAS_Runtime runtime = EAS_Runtime.Get();
  if (runtime) runtime.DebugRegistration(this, DebugEnabled == 1);
  m_LocalRevision = m_Revision;
  m_LocalAdmissionGeneration = AdmissionGeneration;
  // Keep the prefab UI info: localized search name, card image and authored labels.
 }

 bool ProximityActive() { return m_ProximityActive; }
 bool LocalPlaybackAllowed()
 {
  return m_ProximityActive && EAS_Activation.LocalNear(GetOrigin(), EAS_Activation.AMBIENT_RADIUS);
 }

 // Called for every admitted module, including sleepers, once per proximity scan.
 void UpdateActivation(float now, EAS_Activation snapshot)
 {
  if (!IsAuthority() || !m_Schedule || !Enabled || !m_Admitted) return;
  if (EAS_Logic.Expired(m_Schedule.RunStarted, Minutes, now)) { QueueSetting(6, 0); return; }
  if (vector.DistanceSq(m_Anchor, GetOrigin()) > 0.01) Moved();
  bool active = snapshot && snapshot.Near(GetOrigin(), EAS_Activation.AMBIENT_RADIUS);
  if (active == m_ProximityActive)
  {
   if (!active && Status != "Sleeping (no player within 3000 m)")
   {
    Status = "Sleeping (no player within 3000 m)";
    Replication.BumpMe();
   }
   return;
  }
  m_ProximityActive = active;
  if (active) m_Schedule.Restart(now, false);
  else if (EAS_Runtime.Instance) EAS_Runtime.Instance.StopSounds(this, "activation");
  Status = "Sleeping (no player within 3000 m)";
  if (active) Status = "Running";
  // Invalidate descriptors queued before sleep, without changing admission IDs.
  m_Revision++;
  Replication.BumpMe();
  ActivationReceived();
 }

 protected void ActivationReceived()
 {
  if (!m_Initialized || !EAS_Runtime.Instance) return;
  TraceSettings("activation");
  if (!m_ProximityActive) EAS_Runtime.Instance.StopSounds(this, "activation");
  EAS_Runtime.Instance.Wake();
 }

 void ServerTick(float now)
 {
  if (!IsAuthority() || !m_Schedule || !Enabled || !m_Admitted) return;
  if (EAS_Logic.Expired(m_Schedule.RunStarted, Minutes, now)) { QueueSetting(6, 0); return; }
  if (vector.DistanceSq(m_Anchor, GetOrigin()) > 0.01) Moved();
  if (!m_ProximityActive || now < m_Schedule.NextDue) return;
  EAS_Runtime runtime = EAS_Runtime.Get();
  if (!runtime) return;
  array<int> values = Settings();
  int index = m_Schedule.ChooseContent(EAS_Runtime.Bank(), values, now);
  if (index < 0) { Status = "Waiting for eligible sound"; return; }
  if (!runtime.ServerBudget.Available(now, 12)) { Skipped++; m_Schedule.NextDue = now + 2; return; }
  EAS_Clip clip = EAS_Runtime.Bank()[index];
  // Charge before native queries: failed positions consume work too. Across all
  // modules, twelve attempts per ten seconds permit at most 36 position probes.
  runtime.ServerBudget.Record(now);
  if (!ChoosePosition(clip.Category == 2)) { Skipped++; Status = "No valid source position"; m_Schedule.DeferCategory(clip.Category, values, now); return; }
  // Playback attenuation preserves the source PCM of unchanged licensed recordings.
  float gain = Math.Clamp(Value(16 + clip.Category) * 0.01 * Math.Pow(10, m_Schedule.Random.RandFloatXY(-1.5, 1.5) / 20), 0, 1) * clip.PlaybackGain;
  ChimeraWorld world = ChimeraWorld.CastFrom(GetWorld());
  if (!world) return;
  WorldTimestamp emitted = world.GetServerTimestamp();
  m_Sequence++;
  m_Schedule.StartedContent(EAS_Runtime.Bank(), index, values, now);
  if (EAS_Diagnostics.Enabled(this)) EAS_Diagnostics.Event("scheduled", this, string.Format("revision=%1 sequence=%2 clip=%3 event=%4 source=%5 spread=%6 distance=%7 range=%8", m_Revision, m_Sequence, index, clip.EventName, m_Schedule.Position, Spread, vector.Distance(GetOrigin(), m_Schedule.Position), Range));
  Scheduled++;
  Status = "Running";
  if (clip.Category == 3)
  {
   m_MixIndex = index; m_MixRevision = m_Revision; m_MixGeneration++;
   m_MixPosition = m_Schedule.Position; m_MixGain = gain;
   Replication.BumpMe();
   return;
  }
  Rpc(RpcPlay, m_Revision, m_Sequence, index, emitted, m_Schedule.Position, gain, Range);
  Receive(m_Revision, m_Sequence, index, emitted, m_Schedule.Position, gain, Range);
 }

 // Called from the existing bounded service; no extra callbacks or emitters.
 void QueueCurrentMix(EAS_Runtime runtime, float now)
 {
  if (System.IsConsoleApp()) return;
  if (!m_Ready || !m_Admitted || !Enabled || !PremixedEnabled || m_MixRevision != m_Revision) return;
  if (!LocalPlaybackAllowed()) { m_PlayedMixGeneration = -1; return; }
  if (m_PlayedMixGeneration == m_MixGeneration || now < m_NextMixAttempt || runtime.HasMixVoice(this)) return;
  m_NextMixAttempt = now + 2;
  if (!Valid() || m_MixIndex < 0 || m_MixIndex >= EAS_Runtime.Bank().Count() || EAS_Runtime.Bank()[m_MixIndex].Category != 3) return;
  float distance = AudioSystem.GetDistance(m_MixPosition);
  if (distance < 0 || distance > Range) return;
  ChimeraWorld world = ChimeraWorld.CastFrom(GetWorld());
  if (world) runtime.Queue(this, m_Revision, m_MixIndex, world.GetServerTimestamp(), m_MixPosition, m_MixGain, Range, -1);
 }

 void ResetMixForLocalExit() { m_PlayedMixGeneration = -1; }

 void MixStarted()
 {
  m_PlayedMixGeneration = m_MixGeneration;
  LastPosition = m_MixPosition;
  LastSound = EAS_Runtime.Bank()[m_MixIndex].EventName;
 }

 protected bool ValidPosition(inout vector position, bool placeOnGround, bool jet)
 {
  vector mins, maxs;
  GetWorld().GetBoundBox(mins, maxs);
  if (position[0] < mins[0] || position[0] > maxs[0] || position[2] < mins[2] || position[2] > maxs[2]) return false;
  float ground = GetWorld().GetSurfaceY(position[0], position[2]);
  EWaterSurfaceType waterType;
  vector water, bounds, transform[4];
  if (ChimeraWorldUtils.TryGetWaterSurface(GetWorld(), position, water, waterType, transform, bounds) && water[1] > ground)
  {
   if (!jet) return false;
   ground = water[1];
  }
  if (jet)
  {
   vector anchor = GetOrigin();
   position[1] = EAS_Logic.JetHeight(anchor[1], ground, Range);
   if (position[1] < mins[1] || position[1] > maxs[1]) return false;
  }
  else if (placeOnGround) position[1] = ground + 1;
  TraceSphere trace = new TraceSphere();
  trace.Start = position;
  trace.End = position;
  trace.Radius = 0.25;
  trace.Flags = TraceFlags.ENTS | TraceFlags.WORLD;
  trace.Exclude = this;
  return GetWorld().TracePosition(trace) >= 0;
 }

 protected bool ChoosePosition(bool jet)
 {
  vector point = EAS_Logic.AnchorPosition(GetOrigin());
  bool valid;
  if (Spread > 0)
  {
   int band = m_Schedule.NextBand();
   float minimumSeparation = Math.Min(10, Spread * 0.1);
   // Three bounded probes; an invalid spread never falls back to a repeated anchor.
   for (int i = 0; i < 3; i++)
   {
    point = m_Schedule.PointInBand(GetOrigin(), Spread, band);
    if (m_HasSourcePosition)
    {
     vector delta = point - m_PreviousSourcePosition;
     if (delta[0] * delta[0] + delta[2] * delta[2] < minimumSeparation * minimumSeparation) continue;
    }
    valid = ValidPosition(point, true, jet);
    if (valid) break;
   }
  }
  else valid = ValidPosition(point, false, jet);
  if (!valid) return false;
  m_Schedule.Position = point;
  m_PreviousSourcePosition = point;
  m_HasSourcePosition = true;
  return true;
 }

 [RplRpc(RplChannel.Unreliable, RplRcver.Broadcast)]
 protected void RpcPlay(int revision, int sequence, int clip, WorldTimestamp emitted, vector position, float gain, int range)
 {
  if (!IsAuthority()) Receive(revision, sequence, clip, emitted, position, gain, range);
 }

 protected void Receive(int revision, int sequence, int clip, WorldTimestamp emitted, vector position, float gain, int range)
 {
  if (!m_Initialized || !m_Ready || !m_Admitted || !Enabled || !Valid() || System.IsConsoleApp()) return;
  ChimeraWorld world = ChimeraWorld.CastFrom(GetWorld());
  if (!world) return;
  float age = world.GetServerTimestamp().DiffSeconds(emitted);
  if (!EAS_Logic.AcceptEvent(m_Revision, revision, sequence, m_LastSequence, 0, age)) return;
  m_LastSequence = sequence; // Also consume culled/denied events; never replay them.
  if (!LocalPlaybackAllowed()) return;
  if (clip < 0 || clip >= EAS_Runtime.Bank().Count() || gain != gain || gain < 0 || gain > 1 || range != Range) return;
  LastPosition = position;
  LastSound = EAS_Runtime.Bank()[clip].EventName;
  EAS_Runtime runtime = EAS_Runtime.Get();
  if (runtime) runtime.Queue(this, revision, clip, emitted, position, gain, range, sequence);
 }

 string PlaybackIdentity()
 {
  if (m_Replication)
  {
   RplId id = m_Replication.Id();
   if (id.IsValid()) return "rpl:" + id.AsString();
  }
  return LocalIdentity;
 }

 bool PendingValid(int revision, WorldTimestamp emitted, int range)
 {
  if (!m_Initialized || !m_Ready || !m_Admitted || !Enabled || !LocalPlaybackAllowed() || revision != m_Revision || range != Range || !Valid() || !IsRuntime()) return false;
  if (GetGame().GetWorld() != GetWorld()) return false;
  ChimeraWorld world = ChimeraWorld.CastFrom(GetWorld());
  if (!world) return false;
  float age = world.GetServerTimestamp().DiffSeconds(emitted);
  return age >= -0.25 && age <= 0.75;
 }

 protected void Moved()
 {
  m_Anchor = GetOrigin();
  if (!m_Initialized || !IsAuthority() || !m_Schedule) return;
  if (EAS_Runtime.Instance) EAS_Runtime.Instance.StopSounds(this, "moved");
  m_Schedule.Restart(EAS_Runtime.Now(), false);
  m_Revision++;
  m_LocalRevision = m_Revision; // This authority already cancelled the prior playback generation.
  Replication.BumpMe();
  TraceSettings("moved");
 }

 override protected void OnTransformResetImpl(TransformResetParams params)
 {
  super.OnTransformResetImpl(params);
  Moved();
 }

 void EAS_AmbientModule(IEntitySource src, IEntity parent) { SetEventMask(EntityEvent.INIT); }
 void ~EAS_AmbientModule()
 {
  TraceSettings("deleted");
  m_Initialized = false;
  if (GetGame()) GetGame().GetCallqueue().Remove(ApplyPending);
  EAS_Runtime runtime = EAS_Runtime.Instance;
  if (runtime) { runtime.Remove(this, "deleted"); runtime.DebugRegistration(this, false); }
 }
}
