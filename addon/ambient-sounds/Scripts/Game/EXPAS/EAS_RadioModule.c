[EntityEditorProps(category: "EXPBG/Ambient Sounds", description: "Physical GM-controlled radio")]
class EAS_RadioModuleClass : GenericEntityClass {}

class EAS_RadioModule : GenericEntity
{
 [Attribute("0", UIWidgets.EditBox, "Recording: radio 0-13 or 100-103 language random; crowd 0-4, 100 random or 101 angry/rioting alternate; TV 0; placed sound 0-27", "0 103 1", category: "EXPBG Sound Player"), RplProp()]
 int Recording;
 [Attribute("15", UIWidgets.EditBox, "Volume (%)", "0 100 1", category: "EXPBG Sound Player"), RplProp()]
 int Volume;
 [Attribute("0", UIWidgets.CheckBox, "Enabled", category: "EXPBG Sound Player"), RplProp()]
 int Enabled;
 [Attribute("1", UIWidgets.CheckBox, "Loop recording", category: "EXPBG Sound Player"), RplProp()]
 int Loop;
 [Attribute("1", UIWidgets.EditBox, "Pause between complete recordings (seconds)", "0 600 1", category: "EXPBG Sound Player"), RplProp()]
 int PauseSeconds;
 [Attribute("30", UIWidgets.EditBox, "Audible distance (metres); radio/TV 30, crowd 10-150, placed sound 30-1500", "10 1500 10", category: "EXPBG Sound Player"), RplProp()]
 int Range;
 [Attribute("0", UIWidgets.CheckBox, "Debug module (GM only)", category: "EXPBG Sound Player"), RplProp(onRplName: "SettingsReceived")]
 int DebugEnabled;
 [RplProp(onRplName: "SettingsReceived")] protected int m_Revision;
 [RplProp(onRplName: "SettingsReceived")] protected bool m_Ready;
 [RplProp(onRplName: "ActivationReceived")] protected bool m_ProximityActive;
 [RplProp()] int AdmissionSlot = -1;
 [RplProp()] int AdmissionGeneration;
 protected bool m_Initialized;
 protected int m_LocalRevision = -1;
 protected RplComponent m_Replication;
 protected ref array<int> m_Pending;
 protected bool m_InvalidPending;
 protected bool m_SavedPending;
 protected SCR_AttributesManagerEditorComponent m_Manager;
 protected int m_Player = -1;
 ref EAS_RadioState State = new EAS_RadioState();
 AudioHandle Handle = AudioHandle.Invalid;
 float End;
 int StartTick; // System.GetTickCount() at the start: the real clock for the eviction test.

 bool IsAuthority()
 {
  if (m_Replication) return m_Replication.Role() == RplRole.Authority;
  return !Replication.IsRunning();
 }
 void AssignAdmission(int slot, int generation)
 {
  if (!IsAuthority() || (AdmissionSlot == slot && AdmissionGeneration == generation)) return;
  AdmissionSlot = slot; AdmissionGeneration = generation;
  m_Revision++;
  Replication.BumpMe();
 }
 int Value(int key)
 {
  if (key == 0) return Recording;
  if (key == 1) return Volume;
  if (key == 2) return Enabled;
  if (key == 3) return Loop;
  if (key == 4) return PauseSeconds;
  if (key == 5) return Range;
  if (key == 6) return DebugEnabled;
  return -1;
 }
 void TraceSettings(string reason)
 {
  if (!EAS_Diagnostics.Enabled(this)) return;
  string values;
  for (int key = 0; key < 7; key++) { if (key > 0) values += ","; values += Value(key).ToString(); }
  EAS_Diagnostics.Event(reason, this, string.Format("kind=%1 revision=%2 slot=%3 generation=%4 settings=%5 active=%6", AudioKind(), m_Revision, AdmissionSlot, AdmissionGeneration, values, m_ProximityActive));
 }
 // Small finite-source variants share admission, cleanup and playback.
 int AudioKind() { return 0; }
 ResourceName AudioProject() { return EAS_RadioBank.PROJECT; }
 string AudioEvent(int recording) { return EAS_RadioBank.Event(recording); }
 float AudioDuration(int recording) { return EAS_RadioBank.Duration(recording); }
 float AudibleRange() { return Range; }
 float ActivationRadius() { return EAS_Activation.RADIO_RADIUS; }
 int ResolveRecording(int selection, int previous) { return EAS_RadioBank.Resolve(selection, previous); }
 bool ValidSetting(int key, float value)
 {
  if (!EAS_RadioState.ValidValue(key, value)) return false;
  if (key == 0) return EAS_RadioBank.ValidSelection(value);
  if (key == 4) return value == 1;
  if (key == 5) return value == 30;
  return true;
 }
 array<int> Settings() { return {Recording, Volume, Enabled, Loop, PauseSeconds, Range, DebugEnabled}; }
 array<int> NormalizeSettings(array<int> values)
 {
  if (!values || (values.Count() != 3 && values.Count() != 4 && values.Count() != 6 && values.Count() != 7)) return null;
  array<int> result = {}; result.Copy(values);
  if (result.Count() == 3) result.Insert(1);
  if (result.Count() == 4) { result.Insert(1); result.Insert(30); }
  if (result.Count() == 6) result.Insert(0);
  for (int key = 0; key < 7; key++) if (!ValidSetting(key, result[key])) return null;
  return result;
 }
 override protected void EOnInit(IEntity owner)
 {
  if (!EAS_AmbientModule.IsRuntime() || SCR_Global.IsEditMode(owner)) return;
  m_Initialized = true;
  m_Replication = RplComponent.Cast(FindComponent(RplComponent));
  if (IsAuthority())
  {
   m_Ready = true;
   array<int> initial = NormalizeSettings(Settings());
   if (!initial || !State.Apply(initial)) { Enabled = 0; EAS_Diagnostics.Error("initial-settings", this, "invalid; playback disabled"); }
   m_Revision++;
   Replication.BumpMe();
  }
  SettingsReceived();
  TraceSettings("initialized");
 }
 bool Live()
 {
  return m_Initialized && m_Ready && Enabled == 1 && EAS_AmbientModule.IsRuntime() && GetGame().GetWorld() == GetWorld();
 }
 bool ProximityActive() { return m_ProximityActive; }
 void UpdateActivation(EAS_Activation snapshot)
 {
  if (!IsAuthority() || !Live()) return;
  bool active = snapshot && snapshot.Near(GetOrigin(), ActivationRadius());
  if (active == m_ProximityActive) return;
  m_ProximityActive = active;
  Replication.BumpMe();
  ActivationReceived();
 }
 protected void ActivationReceived()
 {
  if (!m_Initialized) return;
  TraceSettings("activation");
  // Proximity must not reset one-shot completion or the bounded failure budget.
  if (m_ProximityActive) State.Resume(EAS_Runtime.Now(), 2 + Math.RandomFloat(0, 3));
  if (!EAS_RadioRuntime.Instance) return;
  if (!m_ProximityActive) EAS_RadioRuntime.Instance.Stop(this, true, "activation");
  EAS_RadioRuntime.Instance.Wake();
 }
 protected bool EditorAllowed(SCR_AttributesManagerEditorComponent manager, int player)
 {
  if (!manager) return false;
  SCR_EditorManagerEntity editor = manager.GetManager();
  return editor && editor.GetPlayerID() == player && !editor.IsLimited() && editor.GetCurrentMode() == EEditorMode.EDIT;
 }
 void QueueEditorSetting(int key, float value, SCR_AttributesManagerEditorComponent manager, int player)
 {
  if (!m_Initialized || !IsAuthority() || !EditorAllowed(manager, player) || key < 0 || key > 6) return;
  QueueSetting(key, value, manager, player, false);
 }
 void QueueSavedSetting(int key, float value)
 {
  if (!Replication.IsServer() || !m_Initialized || !IsAuthority() || key < 0 || key > 6) return;
  QueueSetting(key, value, null, -1, true);
 }
 protected void QueueSetting(int key, float value, SCR_AttributesManagerEditorComponent manager, int player, bool saved)
 {
  if (!m_Pending)
  {
   m_Pending = Settings();
   m_Manager = manager; m_Player = player; m_SavedPending = saved;
   GetGame().GetCallqueue().CallLater(ApplyPending, 0, false);
  }
  if (m_SavedPending != saved || m_Manager != manager || m_Player != player || !ValidSetting(key, value)) m_InvalidPending = true;
  else m_Pending[key] = value;
 }
 protected void ApplyPending()
 {
  array<int> values = m_Pending;
  bool saved = m_SavedPending;
  bool valid = !m_InvalidPending && (m_SavedPending || EditorAllowed(m_Manager, m_Player));
  m_Pending = null; m_InvalidPending = false; m_Manager = null; m_Player = -1; m_SavedPending = false;
  if (!valid) Print("[EAS ERROR] Invalid finite sound settings batch; previous settings retained", LogLevel.WARNING);
  if (!valid || !m_Initialized || !IsAuthority()) return;
  if (!saved && values[0] == Recording && values[1] == Volume && values[2] == Enabled && values[3] == Loop && values[4] == PauseSeconds && values[5] == Range)
  {
   if (values[6] == DebugEnabled) return;
   // Debug is replicated independently; it must not restart a one-shot or loop.
   DebugEnabled = values[6];
   Replication.BumpMe(); SettingsReceived();
   TraceSettings("debug-applied");
   return;
  }
  RestoreSettings(values);
  if (saved) TraceSettings("session-loaded");
 }
 bool RestoreSettings(array<int> values)
 {
  if (!IsAuthority()) return false;
  array<int> normalized = NormalizeSettings(values);
  if (!normalized || !State.Apply(normalized)) { EAS_Diagnostics.Error("restore", this, "invalid finite settings; previous settings retained"); return false; }
  Recording = State.Recording; Volume = State.Volume; Enabled = State.Enabled; Loop = State.Loop; PauseSeconds = State.PauseSeconds; Range = State.Range; DebugEnabled = State.DebugEnabled;
  if (m_Initialized) { m_Revision++; Replication.BumpMe(); SettingsReceived(); }
  TraceSettings("settings-restored");
  return true;
 }
 protected void SettingsReceived()
 {
  if (!m_Initialized || !m_Ready || !EAS_AmbientModule.IsRuntime()) return;
  EAS_Runtime debugRuntime = EAS_Runtime.Get();
  if (debugRuntime) debugRuntime.DebugRegistration(this, DebugEnabled == 1);
  State.DebugEnabled = DebugEnabled;
  if (m_LocalRevision == m_Revision) return;
  if (EAS_RadioRuntime.Instance) EAS_RadioRuntime.Instance.Remove(this, "settings");
  State.Restart(EAS_Runtime.Now());
  array<int> normalized = NormalizeSettings(Settings());
  if (normalized && State.Apply(normalized) && Enabled)
  {
   EAS_RadioRuntime runtime = EAS_RadioRuntime.Get();
   if (runtime && !runtime.Add(this) && IsAuthority())
   {
    Enabled = 0; State.Enabled = 0; m_Revision++; Replication.BumpMe();
    Print("[EAS] Finite audio capacity reached (32); source switched off", LogLevel.WARNING);
   }
  }
  m_LocalRevision = m_Revision;
  TraceSettings("settings-received");
 }
 void EAS_RadioModule(IEntitySource src, IEntity parent) { SetEventMask(EntityEvent.INIT); }
 void ~EAS_RadioModule()
 {
  TraceSettings("deleted");
  m_Initialized = false;
  if (GetGame()) GetGame().GetCallqueue().Remove(ApplyPending);
  if (EAS_RadioRuntime.Instance) EAS_RadioRuntime.Instance.Remove(this, "deleted");
  if (EAS_Runtime.Instance) EAS_Runtime.Instance.DebugRegistration(this, false);
 }
}
