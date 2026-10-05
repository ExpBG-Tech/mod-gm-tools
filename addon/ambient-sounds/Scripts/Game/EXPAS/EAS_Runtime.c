class EAS_Voice
{
 EAS_AmbientModule Owner;
 AudioHandle Handle;
 int Category;
 vector Position;
 int Range;
 float End;
 bool Fading;
}

// One bounded service per local world. Dedicated processes execute only scheduling.
class EAS_Runtime
{
 static ref EAS_Runtime Instance;
 protected static ref array<ref EAS_Clip> s_Bank;
 // Shared placement-attempt budget, including failed native probes.
 ref EAS_StartBudget ServerBudget = new EAS_StartBudget();
 protected ref EAS_StartBudget m_ClientBudget = new EAS_StartBudget();
 protected ref EAS_AdmissionSlots m_Slots = new EAS_AdmissionSlots();
 protected bool m_Ticking;
 protected int m_TickInterval;
 protected float m_NextActivation;
 protected bool m_HasActive;
 protected ref array<EAS_AmbientModule> m_Modules = {};
 protected ref array<IEntity> m_DebugModules = {};
 protected ref array<ref EAS_Voice> m_Voices = {};
 protected ref EAS_PlaybackCohort m_Cohort = new EAS_PlaybackCohort();
 protected BaseWorld m_World;
 protected int m_LocalIdentity;
 protected int m_Cursor;
 protected float m_LastExplosion = -100000;
 protected float m_NextDebug;
 protected SCR_EditorManagerEntity m_DebugEditor;
 protected static const string DEBUG_CANVAS_NAME = "EAS_DebugRanges";
 protected static const string DEBUG_LEGEND_NAME = "EAS_DebugLegend";
 protected static const int LEGEND_WIDTH = 360;
 protected static const int LEGEND_HEIGHT = 100;
 protected CanvasWidget m_DebugCanvas;
 protected TextWidget m_DebugLegend;
 protected ref array<ref LineDrawCommand> m_DebugLines = {};
 protected ref array<ref CanvasWidgetCommand> m_DebugCommands = {};
 protected float m_DebugWidth;
 protected float m_DebugHeight;
 protected ref EAS_DiagnosticWindow m_Diagnostics;
 int LocalStarted;
 int LocalSkipped;

 static array<ref EAS_Clip> Bank()
 {
  if (!s_Bank) s_Bank = EAS_Bank.Create();
  return s_Bank;
 }

 static float Now()
 {
  if (!GetGame() || !GetGame().GetWorld()) return 0;
  return GetGame().GetWorld().GetWorldTime() * 0.001;
 }
 static EAS_Runtime Get()
 {
  BaseWorld world;
  if (GetGame()) world = GetGame().GetWorld();
  if (Instance && (!world || Instance.m_World != world)) Instance.Shutdown();
  if (!world) return null;
  if (!Instance)
  {
   Instance = new EAS_Runtime();
   Instance.m_World = world;
   if (EAS_Diagnostics.Enabled()) Instance.m_Diagnostics = new EAS_DiagnosticWindow(Now());
  }
  return Instance;
 }

 protected bool WorldLive() { return m_World && GetGame() && GetGame().GetWorld() == m_World; }

 bool Add(EAS_AmbientModule module)
 {
  if (!WorldLive() || !module || module.GetWorld() != m_World) return false;
  if (module.IsAuthority())
  {
   int slot = m_Slots.Claim(module);
   if (slot < 0) return false;
   module.AssignAdmission(slot, m_Slots.Generation(slot));
  }
  else
  {
   EAS_AmbientModule previous = EAS_AmbientModule.Cast(m_Slots.Get(module.AdmissionSlot));
   if (!m_Slots.Accept(module, module.AdmissionSlot, module.AdmissionGeneration)) return false;
   if (previous && previous != module) { m_Modules.RemoveItem(previous); StopSounds(previous, "admission-replaced"); }
  }
  if (!m_Modules.Contains(module))
  {
   m_Modules.Insert(module);
   m_LocalIdentity++; module.LocalIdentity = "local:" + m_LocalIdentity.ToString();
   if (EAS_Diagnostics.Enabled(module)) EAS_Diagnostics.Event("admit", module, string.Format("runtime=war slot=%1 generation=%2 admitted=%3", module.AdmissionSlot, module.AdmissionGeneration, m_Modules.Count()));
  }
  Wake();
  return true;
 }

 void Remove(EAS_AmbientModule module, string reason = "removed")
 {
  bool removed = m_Slots.Remove(module);
  m_Modules.RemoveItem(module);
  StopSounds(module, reason);
  if (removed && EAS_Diagnostics.Enabled(module)) EAS_Diagnostics.Event("remove", module, string.Format("runtime=war reason=%1 admitted=%2 pending=%3", reason, m_Modules.Count(), m_Cohort.Items().Count()));
  FinishIfIdle();
 }

 void DebugRegistration(IEntity module, bool enabled)
 {
  if (System.IsConsoleApp()) return;
  // Cached screen-space commands must not outlive a toggle or deleted owner.
  ClearDebug();
  m_NextDebug = 0;
  m_DebugModules.RemoveItem(module);
  // A deleting owner may already read as null in the weak list; drop such
  // entries so the last disable can still release the overlay and the tick.
  for (int i = m_DebugModules.Count() - 1; i >= 0; i--) if (!m_DebugModules[i]) m_DebugModules.Remove(i);
  if (module && enabled && m_DebugModules.Count() < 32) { m_DebugModules.Insert(module); Wake(); }
  if (!enabled) FinishIfIdle();
 }

 // World cleanup (disconnect, mission change) can end before the next tick
 // notices the world change: release the instance and any named overlay widget.
 static void ShutdownForWorldCleanup()
 {
  if (Instance) Instance.Shutdown();
  RemoveDebugWidgets();
 }

 protected static bool DebugWanted(IEntity entity)
 {
  EAS_AmbientModule module = EAS_AmbientModule.Cast(entity);
  if (module) return module.DebugEnabled == 1;
  EAS_RadioModule finite = EAS_RadioModule.Cast(entity);
  return finite && finite.DebugEnabled == 1;
 }

 // Bounded sweep by name: a widget that lost its script reference is still removed.
 protected static void RemoveDebugWidgets()
 {
  if (!GetGame()) return;
  WorkspaceWidget workspace = GetGame().GetWorkspace();
  if (!workspace) return;
  for (int i = 0; i < 8; i++)
  {
   Widget stale = workspace.FindAnyWidget(DEBUG_LEGEND_NAME);
   if (!stale) stale = workspace.FindAnyWidget(DEBUG_CANVAS_NAME);
   if (!stale) return;
   stale.RemoveFromHierarchy();
  }
 }

 void CancelPending(EAS_AmbientModule module)
 {
  for (int i = m_Cohort.Items().Count() - 1; i >= 0; i--)
  {
   EAS_Pending pending = m_Cohort.Items()[i];
   if (pending.Owner == module)
   {
    m_Cohort.Items().Remove(i);
    if (m_Diagnostics) m_Diagnostics.Dropped++;
   }
  }
 }

 void StopSounds(EAS_AmbientModule module, string reason = "module-state")
 {
  CancelPending(module);
  if (!WorldLive()) { Shutdown(); return; }
  if (System.IsConsoleApp()) return;
  foreach (EAS_Voice voice : m_Voices)
  {
   if (voice.Owner != module || voice.Fading) continue;
   AudioSystem.TerminateSoundFadeOut(voice.Handle, true, 1);
   voice.Fading = true;
   voice.End = Math.Min(voice.End, Now() + 1);
   if (EAS_Diagnostics.Enabled(module)) EAS_Diagnostics.Event("fade", module, string.Format("runtime=war handle=%1 reason=%2 seconds=1", voice.Handle, reason));
  }
  if (m_Modules.IsEmpty() && !m_Voices.IsEmpty())
  {
   GetGame().GetCallqueue().Remove(FinishFades);
   GetGame().GetCallqueue().CallLater(FinishFades, 1000, false);
  }
 }

 void Queue(EAS_AmbientModule module, int revision, int index, WorldTimestamp emitted, vector position, float gain, int range, int sequence = 0)
 {
  if (System.IsConsoleApp() || !WorldLive() || !m_Modules.Contains(module) || !module.LocalPlaybackAllowed() || gain <= 0) return;
  EAS_Pending pending = new EAS_Pending();
  pending.Owner = module; pending.StableKey = module.PlaybackIdentity(); pending.ConfigRevision = revision;
  pending.Clip = index; pending.Emitted = emitted; pending.Position = position; pending.Gain = gain; pending.Range = range; pending.EventSequence = sequence;
  if (!m_Cohort.Add(pending)) { LocalSkipped++; if (m_Diagnostics) m_Diagnostics.Dropped++; }
  else SetTickInterval(250); // A newly audible event must not wait on a sleeping client's two-second timer.
 }

 protected void Flush(float now)
 {
  if (System.IsConsoleApp() || !WorldLive()) { m_Cohort.Clear(); return; }
  foreach (EAS_Pending pending : m_Cohort.Items())
  {
   pending.Score = EAS_Logic.EstimatedAudibility(pending.Gain, AudioSystem.GetDistance(pending.Position), pending.Range);
   if (pending.Owner) pending.LastAccepted = pending.Owner.LocalLastAccepted;
  }
  while (!m_Cohort.Empty())
  {
   EAS_Pending selected = m_Cohort.TakeBest();
   EAS_AmbientModule owner = selected.Owner;
   if (!owner || !m_Modules.Contains(owner) || !owner.PendingValid(selected.ConfigRevision, selected.Emitted, selected.Range)) { LocalSkipped++; if (m_Diagnostics) m_Diagnostics.Dropped++; continue; }
   Play(owner, selected.Clip, selected.Position, selected.Gain, selected.Range, now);
  }
 }

 bool HasMixVoice(EAS_AmbientModule module)
 {
  foreach (EAS_Voice voice : m_Voices) if (voice.Owner == module && voice.Category == 3) return true;
  return false;
 }

 protected void Play(EAS_AmbientModule module, int index, vector position, float gain, int range, float now)
 {
  if (!WorldLive() || System.IsConsoleApp() || !module.LocalPlaybackAllowed() || gain <= 0 || index < 0 || index >= Bank().Count() || !module.CanAttemptClip(index, now)) return;
  float distance = AudioSystem.GetDistance(position);
  if (distance < 0 || distance > range) { LocalSkipped++; if (m_Diagnostics) m_Diagnostics.Dropped++; return; }
  EAS_Clip clip = Bank()[index];
  int explosions;
  int jets;
  int ownerVoices;
  foreach (EAS_Voice active : m_Voices)
  {
   if (active.Owner == module && active.Category == clip.Category) ownerVoices++;
   if (active.Category == 1) explosions++;
   if (active.Category == 2) jets++;
  }
  int frequency;
  if (clip.Category < 3) frequency = module.Value(20 + clip.Category);
  bool ownerFull = ownerVoices >= EAS_Logic.OwnerVoiceCap(clip.Category, frequency);
  if (!EAS_Logic.CanStartVoice(clip.Category, m_Voices.Count(), explosions, jets, ownerFull, m_ClientBudget.Available(now, 12), now - m_LastExplosion)) { LocalSkipped++; if (m_Diagnostics) m_Diagnostics.Dropped++; return; }
  vector transform[4];
  Math3D.MatrixIdentity4(transform);
  transform[3] = position;
  array<string> names = {"EAS_Gain"};
  array<float> values = {gain};
  AudioHandle handle = AudioSystem.PlayEvent(EAS_Bank.PROJECT, clip.EventName + "_R" + range.ToString(), transform, names, values);
  if (handle == AudioHandle.Invalid)
  {
   int attempts = module.FailedClip(index, now);
   EAS_Diagnostics.Error("war-start-failed", module, string.Format("event=%1_R%2 position=%3 distance=%4 gain=%5 attempt=%6/3 parked=%7", clip.EventName, range, position, distance, gain, attempts, attempts == 3));
   LocalSkipped++;
   if (m_Diagnostics) m_Diagnostics.Dropped++;
   return;
  }
  EAS_Voice voice = new EAS_Voice();
  module.ClipStarted(index);
  voice.Owner = module; voice.Handle = handle; voice.Category = clip.Category;
  voice.Position = position; voice.Range = range;
  // Native propagation can defer arrival. Keep the owned handle through its tail.
  voice.End = now + EAS_Logic.VoiceLifetime(clip.Duration, range);
  m_Voices.Insert(voice);
  m_ClientBudget.Record(now);
  module.LocalLastAccepted = now;
  if (clip.Category == 1) m_LastExplosion = now;
  LocalStarted++;
  if (m_Diagnostics) m_Diagnostics.Starts++;
  if (EAS_Diagnostics.Enabled(module))
  {
   EAS_Diagnostics.Event("play", module, string.Format("runtime=war clip=%1 event=%2_R%3 handle=%4 position=%5 range=%3 gain=%6 voices=%7", index, clip.EventName, range, handle, position, gain, m_Voices.Count()));
  }
  if (clip.Category == 3) module.MixStarted();
 }

 protected void CleanVoices(float now)
 {
  if (!WorldLive() || System.IsConsoleApp()) return;
  for (int i = m_Voices.Count() - 1; i >= 0; i--)
  {
   EAS_Voice voice = m_Voices[i];
   if (voice.Owner && !voice.Fading && !voice.Owner.LocalPlaybackAllowed())
   {
    // Re-arm at the actual local exit, even if this owner is outside this tick's
    // eight-module scheduling budget and the listener returns before its turn.
    voice.Owner.ResetMixForLocalExit();
    StopSounds(voice.Owner, "activation-exit");
   }
   if (voice.Owner && !voice.Fading)
   {
    float distance = AudioSystem.GetDistance(voice.Position);
    if (distance < 0 || distance > voice.Range)
    {
     // Release any inaudible source without stopping this owner's other effects.
     // Only a persistent mix rearms its current generation for local reentry.
     if (voice.Category == 3) voice.Owner.ResetMixForLocalExit();
     AudioSystem.TerminateSoundFadeOut(voice.Handle, true, 1);
     voice.Fading = true;
     voice.End = Math.Min(voice.End, now + 1);
     if (EAS_Diagnostics.Enabled(voice.Owner)) EAS_Diagnostics.Event("fade", voice.Owner, string.Format("runtime=war handle=%1 reason=inaudible position=%2 range=%3 seconds=1", voice.Handle, voice.Position, voice.Range));
    }
   }
   // Deleting an entity clears weak Owner references; preserve its requested fade.
   // Native completion is also true immediately on requesting a fade. Retain
   // that capacity through the fade deadline even after native handle release.
   if (now < voice.End && (voice.Fading || !AudioSystem.IsSoundPlayed(voice.Handle))) continue;
   AudioSystem.TerminateSoundFadeOut(voice.Handle, false, 0);
   if (m_Diagnostics) m_Diagnostics.Stops++;
   if (EAS_Diagnostics.Enabled(voice.Owner))
   {
    string reason = "finished";
    if (voice.Fading) reason = "fade-complete";
    EAS_Diagnostics.Event("release", voice.Owner, string.Format("runtime=war handle=%1 reason=%2 voices_remaining=%3", voice.Handle, reason, m_Voices.Count() - 1));
   }
   m_Voices.Remove(i);
  }
 }

 protected void Tick()
 {
  if (Instance != this || !WorldLive() || !EAS_AmbientModule.IsRuntime()) { Shutdown(); return; }
  int tickStarted;
  if (m_Diagnostics) tickStarted = System.GetTickCount();
  float now = Now();
  if (now >= m_NextActivation || now < m_NextActivation - 2)
  {
   m_HasActive = false;
   EAS_Activation snapshot;
   // Inspect all 32 before the eight-module scheduling budget: even the last
   // sleeping module can wake at the next two-second poll, not eight seconds.
   foreach (EAS_AmbientModule candidate : m_Modules)
   {
    if (!candidate) continue;
    if (candidate.IsAuthority())
    {
     if (!snapshot) snapshot = EAS_Activation.Get(now);
     candidate.UpdateActivation(now, snapshot);
    }
    if (candidate.ProximityActive() && (candidate.IsAuthority() || candidate.LocalPlaybackAllowed())) m_HasActive = true;
   }
   m_NextActivation = now + 2;
  }
  CleanVoices(now);
  int inspected = m_Modules.Count();
  int work;
  for (int n = 0; n < inspected && work < 8 && !m_Modules.IsEmpty(); n++)
  {
   m_Cursor = m_Cursor % m_Modules.Count();
   EAS_AmbientModule module = m_Modules[m_Cursor];
   m_Cursor++;
   if (!module) m_Modules.RemoveItem(module);
   else
   {
    if (module.ProximityActive())
    {
     int scheduled;
     if (m_Diagnostics) scheduled = module.Scheduled;
     module.ServerTick(now); work++;
     if (m_Diagnostics) m_Diagnostics.Scheduled += Math.Max(0, module.Scheduled - scheduled);
    }
    module.QueueCurrentMix(this, now);
   }
  }
  Flush(now);
  if (!System.IsConsoleApp() && now >= m_NextDebug) { DrawDebug(); m_NextDebug = now + 0.5; }
  int interval = 2000;
  if (m_HasActive || !m_Voices.IsEmpty() || !m_DebugModules.IsEmpty()) interval = 250;
  SetTickInterval(interval);
  if (m_Diagnostics)
  {
   m_Diagnostics.Tick(System.GetTickCount(tickStarted));
   m_Diagnostics.Report("war", now, m_Modules.Count(), m_Voices.Count(), m_Cohort.Items().Count(), m_TickInterval);
  }
  FinishIfIdle();
 }

 protected void FinishFades()
 {
  if (!WorldLive()) { Shutdown(); return; }
  CleanVoices(Now());
  FinishIfIdle();
 }

 void Wake()
 {
  m_NextActivation = 0;
  SetTickInterval(250);
 }
 protected void SetTickInterval(int milliseconds)
 {
  if (!WorldLive() || (m_Ticking && m_TickInterval == milliseconds)) return;
  GetGame().GetCallqueue().Remove(Tick);
  GetGame().GetCallqueue().CallLater(Tick, milliseconds, true);
  m_TickInterval = milliseconds;
  m_Ticking = true;
 }
 protected void FinishIfIdle()
 {
  if (!WorldLive()) { Shutdown(); return; }
  if (!m_Modules.IsEmpty() || !m_Voices.IsEmpty() || !m_DebugModules.IsEmpty()) return;
  bool wasTicking = m_Ticking;
  GetGame().GetCallqueue().Remove(Tick); GetGame().GetCallqueue().Remove(FinishFades);
  m_Ticking = false;
  ClearDebug();
  if (m_Diagnostics && wasTicking) m_Diagnostics.Report("war", Now(), 0, 0, m_Cohort.Items().Count(), 0, true);
  // Keep authority generations and proxy tombstones across same-world idle.
 }

 protected void DrawDebug()
 {
  SCR_EditorManagerEntity editor = SCR_EditorManagerEntity.GetInstance();
  SCR_MapEntity mapEntity = SCR_MapEntity.GetMapInstance();
  WorkspaceWidget workspace = GetGame().GetWorkspace();
  CameraManager cameras = GetGame().GetCameraManager();
  SCR_ManualCamera camera = SCR_CameraEditorComponent.GetCameraInstance();
  // Deleted owners and switched-off modules no longer keep the overlay alive.
  for (int i = m_DebugModules.Count() - 1; i >= 0; i--) if (!DebugWanted(m_DebugModules[i])) m_DebugModules.Remove(i);
  // IsOpened retains the previous state while the native editor closes.
  // Require its actual camera too, so an owned GM role cannot draw in player view.
  if (!workspace || m_DebugModules.IsEmpty() || !editor || editor.IsLimited() || !editor.IsOpened() || editor.IsInTransition() || editor.IsModeChangeRequested() || editor.GetCurrentMode() != EEditorMode.EDIT || !camera || !cameras || cameras.CurrentCamera() != camera || (mapEntity && mapEntity.IsOpen())) { ClearDebug(); return; }
  float width = workspace.DPIUnscale(workspace.GetWidth());
  float height = workspace.DPIUnscale(workspace.GetHeight());
  if (!m_DebugCanvas || !m_DebugLegend)
  {
   // Recreate both together; never overwrite a reference to a live widget.
   ClearDebug();
   RemoveDebugWidgets();
   m_DebugCanvas = CanvasWidget.Cast(workspace.CreateWidgetInWorkspace(WidgetType.CanvasWidgetTypeID, 0, 0, width, height,
    WidgetFlags.VISIBLE | WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS, null, 19));
   if (!m_DebugCanvas) return;
   m_DebugCanvas.SetName(DEBUG_CANVAS_NAME);
   m_DebugEditor = editor;
   m_DebugEditor.GetOnDeactivate().Insert(ClearDebug);
   m_DebugEditor.GetOnClosed().Insert(ClearDebug);
   m_DebugLegend = TextWidget.Cast(workspace.CreateWidgetInWorkspace(WidgetType.TextWidgetTypeID, 0, 0, LEGEND_WIDTH, LEGEND_HEIGHT,
    WidgetFlags.VISIBLE | WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS | WidgetFlags.NO_LOCALIZATION, null, 20));
   if (!m_DebugLegend) { ClearDebug(); return; }
   m_DebugLegend.SetName(DEBUG_LEGEND_NAME);
   m_DebugLegend.SetFont("{EABA4FE9D014CCEF}UI/Fonts/RobotoCondensed/RobotoCondensed_Bold.fnt");
   m_DebugLegend.SetExactFontSize(16);
   m_DebugLegend.SetColorInt(0xFFFFFFFF);
   m_DebugLegend.SetOutline(2, 0xEE000000);
   m_DebugLegend.SetTextWrapping(true);
   m_DebugLegend.SetText("EXPBG sound ranges\nGreen: activation\nBlue: spread\nYellow: audible range and latest source");
  }
  FrameSlot.SetSize(m_DebugCanvas, width, height);
  m_DebugCanvas.GetScreenSize(m_DebugWidth, m_DebugHeight);
  m_DebugCanvas.SetSizeInUnits(Vector(m_DebugWidth, m_DebugHeight, 0));
  m_DebugCanvas.SetZoom(1); m_DebugCanvas.SetOffsetPx(vector.Zero);
  // Right edge, upper middle: clear of the GM top bar, the bottom-left scenario
  // buttons, the bottom-right entity panel, the civilians legends (bottom) and the
  // top-left civilians/cache panels.
  float legendWidth = Math.Min(LEGEND_WIDTH, width - 48);
  FrameSlot.SetPos(m_DebugLegend, width - legendWidth - 24, Math.Max(120, height * 0.38));
  FrameSlot.SetSize(m_DebugLegend, legendWidth, LEGEND_HEIGHT);
  m_DebugCommands.Clear();
  foreach (IEntity entity : m_DebugModules)
  {
   if (!entity) continue;
   vector centre = entity.GetOrigin();
   EAS_AmbientModule module = EAS_AmbientModule.Cast(entity);
   EAS_RadioModule finite = EAS_RadioModule.Cast(entity);
   if (module && module.DebugEnabled)
   {
    DebugRing(centre, module.Spread, 0xFF60CCFF);
    DebugRing(centre, EAS_Activation.AMBIENT_RADIUS, 0xFF70FF80);
    vector soundPosition = centre;
    if (!module.LastSound.IsEmpty()) soundPosition = module.LastPosition;
    DebugRing(soundPosition, module.Range, 0xFFFFCC40);
    if (!module.LastSound.IsEmpty()) DebugRing(soundPosition, 3, 0xFFFFCC40);
   }
   else if (finite && finite.DebugEnabled)
   {
    DebugRing(centre, finite.ActivationRadius(), 0xFF70FF80);
    DebugRing(centre, finite.AudibleRange(), 0xFFFFCC40);
   }
  }
  m_DebugCanvas.SetDrawCommands(m_DebugCommands);
 }

 // Retail UI, fixed 48-segment rings: no terrain scans or extra entities.
 protected void DebugRing(vector centre, float radius, int color)
 {
  if (radius <= 0) return;
  WorkspaceWidget workspace = GetGame().GetWorkspace();
  vector first = centre + Vector(radius, 1, 0);
  vector a = workspace.ProjWorldToScreenNative(first, m_World);
  for (int i = 0; i < 48; i++)
  {
   float angle = (i + 1) * Math.PI2 / 48;
   vector point = centre + Vector(Math.Cos(angle) * radius, 1, Math.Sin(angle) * radius);
   vector b = workspace.ProjWorldToScreenNative(point, m_World);
   if (a[2] > 0 && b[2] > 0 && !((a[0] < 0 && b[0] < 0) || (a[0] > m_DebugWidth && b[0] > m_DebugWidth) || (a[1] < 0 && b[1] < 0) || (a[1] > m_DebugHeight && b[1] > m_DebugHeight)))
   {
    int index = m_DebugCommands.Count();
    if (index == m_DebugLines.Count())
    {
     LineDrawCommand created = new LineDrawCommand();
     created.m_Vertices = {0, 0, 0, 0}; created.m_fWidth = 2;
     m_DebugLines.Insert(created);
    }
    LineDrawCommand line = m_DebugLines[index];
    line.m_iColor = color;
    line.m_Vertices[0] = a[0]; line.m_Vertices[1] = a[1];
    line.m_Vertices[2] = b[0]; line.m_Vertices[3] = b[1];
    m_DebugCommands.Insert(line);
   }
   a = b;
  }
 }

 protected void ClearDebug()
 {
  if (m_DebugEditor)
  {
   m_DebugEditor.GetOnDeactivate().Remove(ClearDebug);
   m_DebugEditor.GetOnClosed().Remove(ClearDebug);
  }
  m_DebugEditor = null;
  if (m_DebugCanvas) m_DebugCanvas.RemoveFromHierarchy();
  if (m_DebugLegend) m_DebugLegend.RemoveFromHierarchy();
  m_DebugCanvas = null; m_DebugLegend = null;
  m_DebugCommands.Clear(); m_DebugLines.Clear();
 }

 protected void Shutdown()
 {
  if (GetGame()) { GetGame().GetCallqueue().Remove(Tick); GetGame().GetCallqueue().Remove(FinishFades); }
  if (WorldLive() && !System.IsConsoleApp()) foreach (EAS_Voice voice : m_Voices)
  {
   AudioSystem.TerminateSoundFadeOut(voice.Handle, false, 0);
   if (m_Diagnostics) m_Diagnostics.Stops++;
   if (EAS_Diagnostics.Enabled(voice.Owner))
   {
    EAS_Diagnostics.Event("release", voice.Owner, string.Format("runtime=war handle=%1 reason=shutdown", voice.Handle));
   }
  }
  m_Voices.Clear(); m_Modules.Clear(); m_DebugModules.Clear();
  m_Cohort.Clear(); EAS_Activation.ForgetWorld(m_World); m_World = null;
  m_Slots = new EAS_AdmissionSlots(); m_Ticking = false;
  ClearDebug();
  if (m_Diagnostics) m_Diagnostics.Report("war", Now(), 0, 0, 0, 0, true);
  if (Instance == this) Instance = null;
 }
}

modded class ArmaReforgerScripted
{
 // Screen-space debug widgets live in the global workspace, not in the world.
 override protected void OnBeforeWorldCleanup()
 {
  EAS_Runtime.ShutdownForWorldCleanup();
  super.OnBeforeWorldCleanup();
 }
}
