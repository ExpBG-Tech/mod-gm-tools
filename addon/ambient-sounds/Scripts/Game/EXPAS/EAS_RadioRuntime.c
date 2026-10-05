// Shared finite radio, crowd and TV playback; no war scheduler events or catch-up queue.
class EAS_RadioRuntime
{
 static ref EAS_RadioRuntime Instance;
 protected ref EAS_AdmissionSlots m_Slots = new EAS_AdmissionSlots();
 protected bool m_Ticking;
 protected int m_TickInterval;
 protected float m_NextActivation;
 protected BaseWorld m_World;
 protected int m_Cursor;
 protected ref EAS_DiagnosticWindow m_Diagnostics;
 static EAS_RadioRuntime Get()
 {
  if (!GetGame() || !GetGame().GetWorld()) return null;
  if (Instance && Instance.m_World != GetGame().GetWorld()) Instance.Shutdown();
  if (!Instance)
  {
   Instance = new EAS_RadioRuntime();
   Instance.m_World = GetGame().GetWorld();
   if (EAS_Diagnostics.Enabled()) Instance.m_Diagnostics = new EAS_DiagnosticWindow(EAS_Runtime.Now());
  }
  return Instance;
 }
 bool Add(EAS_RadioModule module)
 {
  if (!module || module.GetWorld() != m_World) return false;
  if (module.IsAuthority())
  {
   int slot = m_Slots.Claim(module);
   if (slot < 0) return false;
   module.AssignAdmission(slot, m_Slots.Generation(slot));
  }
  else
  {
   EAS_RadioModule previous = EAS_RadioModule.Cast(m_Slots.Get(module.AdmissionSlot));
   if (!m_Slots.Accept(module, module.AdmissionSlot, module.AdmissionGeneration)) return false;
   if (previous != module) Stop(previous, false, "admission-replaced");
  }
  if (EAS_Diagnostics.Enabled(module)) EAS_Diagnostics.Event("admit", module, string.Format("runtime=finite kind=%1 slot=%2 generation=%3", module.AudioKind(), module.AdmissionSlot, module.AdmissionGeneration));
  Wake();
  return true;
 }

 void Stop(EAS_RadioModule module, bool interrupted = false, string reason = "module-state")
 {
  if (!module || module.Handle == AudioHandle.Invalid) return;
  if (interrupted) module.State.Interrupted(EAS_Runtime.Now());
  if (!System.IsConsoleApp() && GetGame() && GetGame().GetWorld() == m_World)
   AudioSystem.TerminateSoundFadeOut(module.Handle, false, 0);
  if (m_Diagnostics) m_Diagnostics.Stops++;
  if (EAS_Diagnostics.Enabled(module))
  {
   EAS_Diagnostics.Event("release", module, string.Format("runtime=finite kind=%1 handle=%2 recording=%3 reason=%4 interrupted=%5", module.AudioKind(), module.Handle, module.State.LastRecording, reason, interrupted));
  }
  module.Handle = AudioHandle.Invalid;
 }
 void Remove(EAS_RadioModule module, string reason = "removed")
 {
  Stop(module, false, reason);
  bool removed = m_Slots.Remove(module);
  if (removed && EAS_Diagnostics.Enabled(module)) EAS_Diagnostics.Event("remove", module, "runtime=finite reason=" + reason);
  SleepIfIdle();
 }
 protected void Tick()
 {
  if (Instance != this || !GetGame() || GetGame().GetWorld() != m_World || !EAS_AmbientModule.IsRuntime()) { Shutdown(); return; }
  int tickStarted;
  if (m_Diagnostics) tickStarted = System.GetTickCount();
  float now = EAS_Runtime.Now();
  if (now >= m_NextActivation || now < m_NextActivation - 2)
  {
   EAS_Activation snapshot;
   for (int slot = 0; slot < 32; slot++)
   {
    EAS_RadioModule candidate = EAS_RadioModule.Cast(m_Slots.Get(slot));
    if (candidate && candidate.IsAuthority())
    {
     if (!snapshot) snapshot = EAS_Activation.Get(now);
     candidate.UpdateActivation(snapshot);
    }
   }
   m_NextActivation = now + 2;
  }
  int voices;
  int admitted;
  bool nearby;
  for (int i = 31; i >= 0; i--)
  {
   EAS_RadioModule module = EAS_RadioModule.Cast(m_Slots.Get(i));
   if (!module || !module.Live()) { Stop(module, false, "inactive"); m_Slots.Remove(module); continue; }
   if (m_Diagnostics) admitted++;
   if (!module.ProximityActive()) { Stop(module, true, "activation-exit"); continue; }
   if (System.IsConsoleApp()) continue;
   float distance = AudioSystem.GetDistance(module.GetOrigin());
   bool outside = distance < 0 || distance > module.AudibleRange();
   // Keep fast updates only where a source can actually be heard. The shared
   // activation snapshot still uses 1000 m and distant devices poll every 2 s.
   if (!outside) nearby = true;
   if (module.Handle == AudioHandle.Invalid)
   {
    module.State.RecoverAfterApproach(distance, module.AudibleRange());
    continue;
   }
   // Native IsSoundPlayed is true when playback has finished.
   if (now >= module.End || outside || AudioSystem.IsSoundPlayed(module.Handle))
   {
    string reason = "finished";
    if (outside) reason = "inaudible";
    Stop(module, outside, reason);
    module.State.NextDue = Math.Max(module.State.NextDue, now);
   }
   else
   {
    vector moving[4]; module.GetWorldTransform(moving);
    AudioSystem.SetSoundTransformation(module.Handle, moving);
    voices++;
   }
  }
  if (m_Slots.Empty()) { DiagnosticTick(tickStarted, 0, 0); SleepIfIdle(); return; }
  int interval = 2000;
  if (nearby || voices > 0) interval = 250;
  SetTickInterval(interval);
  if (System.IsConsoleApp()) { DiagnosticTick(tickStarted, admitted, 0); return; }
  // Oldest due time wins; rotation only breaks ties. A completed loop cannot
  // repeatedly take the voice ahead of radios that have waited since startup.
  int count = 32;
  m_Cursor = m_Cursor % count;
  for (int attempt = 0; attempt < 4 && voices < 4; attempt++)
  {
   EAS_RadioModule radio;
   for (int n = 0; n < count; n++)
   {
    EAS_RadioModule candidate = EAS_RadioModule.Cast(m_Slots.Get((m_Cursor + n) % count));
    if (!candidate || !candidate.ProximityActive() || candidate.Handle != AudioHandle.Invalid || !candidate.State.CanStart(now, voices)) continue;
    float range = AudioSystem.GetDistance(candidate.GetOrigin());
    if (range < 0 || range > candidate.AudibleRange()) continue;
    if (!radio || candidate.State.NextDue < radio.State.NextDue) radio = candidate;
   }
   if (!radio) break;
   vector transform[4]; radio.GetWorldTransform(transform);
   array<string> names = {"EAS_Gain"}; array<float> values = {radio.Volume * 0.01};
   int recording = radio.ResolveRecording(radio.Recording, radio.State.LastRecording);
   string eventName = radio.AudioEvent(recording);
   float duration = radio.AudioDuration(recording);
   if (eventName.IsEmpty() || duration != duration || duration <= 0 || duration > 3600)
   {
    int invalidAttempts = radio.State.FailedStart(now);
    if (m_Diagnostics) m_Diagnostics.Dropped++;
    if (invalidAttempts == 1 || invalidAttempts == 3)
     EAS_Diagnostics.Error("finite-event-invalid", radio, string.Format("kind=%1 selection=%2 resolved=%3 event=%4 duration=%5 attempt=%6/3", radio.AudioKind(), radio.Recording, recording, eventName, duration, invalidAttempts));
    continue;
   }
   radio.Handle = AudioSystem.PlayEvent(radio.AudioProject(), eventName, transform, names, values);
   if (radio.Handle == AudioHandle.Invalid)
   {
    float failedDistance = AudioSystem.GetDistance(radio.GetOrigin());
    int failures = radio.State.FailedStart(now, failedDistance);
    if (m_Diagnostics) m_Diagnostics.Dropped++;
    if (failures == 1 || failures == 3)
     PrintFormat("[EAS] Radio start failed: event=%1 distance=%2 attempt=%3/3 parked=%4", eventName, failedDistance, failures, failures == 3);
    continue;
   }
   radio.End = now + duration + 0.25;
   radio.State.Started(now, duration, recording);
   voices++;
   if (m_Diagnostics) m_Diagnostics.Starts++;
   if (EAS_Diagnostics.Enabled(radio))
   {
    EAS_Diagnostics.Event("play", radio, string.Format("runtime=finite kind=%1 selection=%2 resolved=%3 event=%4 handle=%5 position=%6 range=%7 duration=%8 voices=%9", radio.AudioKind(), radio.Recording, recording, eventName, radio.Handle, radio.GetOrigin(), radio.AudibleRange(), duration, voices));
   }
  }
  m_Cursor = (m_Cursor + 1) % count;
  DiagnosticTick(tickStarted, admitted, voices);
 }
 protected void DiagnosticTick(int started, int admitted, int voices)
 {
  if (!m_Diagnostics) return;
  m_Diagnostics.Tick(System.GetTickCount(started));
  m_Diagnostics.Report("finite", EAS_Runtime.Now(), admitted, voices, 0, m_TickInterval);
 }
 void Wake()
 {
  m_NextActivation = 0;
  if (System.IsConsoleApp()) SetTickInterval(2000);
  else SetTickInterval(250);
 }
 protected void SetTickInterval(int milliseconds)
 {
  if (!GetGame() || GetGame().GetWorld() != m_World || (m_Ticking && m_TickInterval == milliseconds)) return;
  GetGame().GetCallqueue().Remove(Tick);
  GetGame().GetCallqueue().CallLater(Tick, milliseconds, true);
  m_TickInterval = milliseconds;
  m_Ticking = true;
 }
 protected void SleepIfIdle()
 {
  if (!m_Slots.Empty()) return;
  bool wasTicking = m_Ticking;
  if (GetGame()) GetGame().GetCallqueue().Remove(Tick);
  m_Ticking = false;
  if (m_Diagnostics && wasTicking) m_Diagnostics.Report("finite", EAS_Runtime.Now(), 0, 0, 0, 0, true);
  // Retain slot tombstones and generation counter until the world changes.
 }
 protected void Shutdown()
 {
  if (GetGame()) GetGame().GetCallqueue().Remove(Tick);
  for (int i = 0; i < 32; i++) Stop(EAS_RadioModule.Cast(m_Slots.Get(i)), false, "shutdown");
  m_Slots = new EAS_AdmissionSlots(); EAS_Activation.ForgetWorld(m_World); m_World = null; m_Ticking = false;
  if (m_Diagnostics) m_Diagnostics.Report("finite", EAS_Runtime.Now(), 0, 0, 0, 0, true);
  if (Instance == this) Instance = null;
 }
}
