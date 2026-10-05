// Occasional non-verbal sounds from owned, calm civilians near players.
// Samples and measured audibility: docs/AUDIO-SOURCES.md. The server chooses
// a speaker; clients play the allowlisted clip and follow its source locally.
// Third-party audio notice (included in the distributed addon):
// EAC_Northcom_*.wav: fourteen nonverbal recordings from Northcom Ambient Voices
// 1.0.8 by Your401kPlan, https://reforger.armaplatform.com/workshop/69493AAB5216A58E
// Arma Public License (APL): https://www.bohemia.net/en/licenses/arma-public-license
// Noncommercial, Arma-only reuse; these recordings are not MIT-licensed.
// Modifications by ExpBG Tech: renamed resources, gain-normalized to at most
// -23 dBFS RMS / -3 dBFS peak, and rewritten as mono PCM16 WAV at original 48 kHz.
// Supplied AS IS, without warranties; see APL section 5. No endorsement implied.
// At most four early RPCs wait briefly for their speaker to finish streaming.
class EAC_PendingCivilianVoice
{
 RplId Target;
 string Event;
 vector Origin;
 int Range, DebugLevel;
 float Until;
}

class EAC_LocalCivilianVoice
{
 IEntity Actor;
 AudioHandle Handle;

 bool Update()
 {
  if (!Actor) return false;
  vector transform[4];
  Actor.GetWorldTransform(transform);
  transform[3] = transform[3] + Vector(0, 1.6, 0);
  return AudioSystem.SetSoundTransformation(Handle, transform);
 }
}

class EAC_CivilianVoice
{
 static const string COUGH = "SOUND_VOICE_COUGH";
 static const string CLIMB_LOW = "SOUND_VOICE_CLIMB_LOW";
 static const string LAND_LOW = "SOUND_VOICE_LAND_LOW";
 static const string BREATH_OUT = "SOUND_BREATH_REG_OUT";

 // Vanilla bank that carries the character voice animation events. Referenced by
 // name for verification only; nothing is loaded, shipped or overridden.
 static const string VOICE_BANK = "Sounds/Character/Character_Voice_Animations.acp";

 // Native cough stays at weight zero for historical diagnostics. Normal
 // playback uses only the bundled bank, independent of character sound banks.
 static const ResourceName CIVILIAN_BANK = "{CA1A00000A000000}Sounds/EXPAC/EAC_CivilianSounds.acp";
 static const string CUSTOM_PREFIX = "SOUND_EAC_";
 // Mumbling and drinking are excluded at selection and at the playback boundary.
 protected static ref array<string> s_Events = {"SOUND_VOICE_COUGH", "SOUND_EAC_COUGH", "SOUND_EAC_THROAT", "SOUND_EAC_SNEEZE", "SOUND_EAC_NOSE", "SOUND_EAC_YAWN", "SOUND_EAC_SIGH", "SOUND_EAC_HICCUP", "SOUND_EAC_BURP", "SOUND_EAC_HUM", "SOUND_EAC_WHISTLE", "SOUND_EAC_LAUGH"};
 // Default repeat pause; livelier settings scale it down, never below ten seconds.
 static const float CATEGORY_COOLDOWN = 75;
 protected static ref array<float> s_CategoryUntil = {};
 // Relative chance per event, same order as s_Events. Everyday sounds dominate;
 // the comic ones are rare.
 protected static ref array<int> s_Weights = {0, 16, 12, 5, 5, 9, 12, 3, 2, 10, 10, 6};

 protected static float s_NextGlobal;
 protected static int s_Interval = 15;
 protected static int s_Cursor;
 static const int MAX_CANDIDATES_PER_STEP = 32;
 // Statics reset per world. s_NextGlobal is the one that MATTERS: it holds a
 // world-time value, and a world change restarts that clock near zero, so a value
 // carried over from a previous world silences every vocalisation until the old
 // world's elapsed seconds have passed again. The counters and the speaker
 // blocklist are reset with it so a GM snapshot describes the mission it is
 // taken in; the blocklist is re-learned from the first client refusal, exactly
 // as it was learned the first time.
 protected static BaseWorld s_World;
 static void CheckWorld()
 {
  BaseWorld world;
  if (GetGame()) world = GetGame().GetWorld();
  if (world == s_World) return;
  s_World = world;
  s_NextGlobal = 0;
  s_Interval = 15;
  s_Cursor = 0;
  StopLocalVoices();
  s_BlockedPrefabs.Clear();
  s_RefusalCounts.Clear();
  s_CategoryUntil.Clear();
  s_BankTried = false; s_BankReady = false;
  s_BlockReports = 0; s_GateBlocked = 0; s_BlockRefused = 0;
  s_Broadcast.Clear();
  s_LocalPlayed = 0; s_LocalInvalid = 0; s_LocalNoComponent = 0; s_LocalNoEntity = 0;
  s_LocalFirstRefused = -1;
  s_LocalReported = 0;
  s_LocalOutOfRange = 0;
 }

 static int EventCount()
 {
  return s_Events.Count();
 }

 // Short labels distinguish each event in the mission counters.
 static string ShortName(int index)
 {
  if (index < 0 || index >= s_Events.Count()) return "other";
  if (index == 0) return "native_cough";
  string name = s_Events[index];
  name.Replace(CUSTOM_PREFIX, "");
  name.ToLower();
  return name;
 }

 static int IndexOfEvent(string eventName)
 {
  for (int i = 0; i < s_Events.Count(); i++)
  {
   if (s_Events[i] == eventName) return i;
  }
  return -1;
 }

 // Only server-authored, allowlisted events may ever reach a sound component.
 static bool IsApprovedEvent(string eventName)
 {
  return IndexOfEvent(eventName) >= 0;
 }

 // Weighted pick over the allowlist. Integer arithmetic only; no float modulo.
 static string PickEvent(float now = 0, int interval = 15)
 {
  if (s_CategoryUntil.Count() != s_Events.Count()) { s_CategoryUntil.Clear(); s_CategoryUntil.Resize(s_Events.Count()); }
  int total = 0;
  for (int i = 0; i < s_Weights.Count() && i < s_Events.Count(); i++)
  {
   if (now < s_CategoryUntil[i]) continue;
   total += s_Weights[i];
  }
  if (total <= 0) return "";
  int roll = Math.RandomInt(0, total);
  for (int pick = 0; pick < s_Weights.Count() && pick < s_Events.Count(); pick++)
  {
   if (now < s_CategoryUntil[pick]) continue;
   roll -= s_Weights[pick];
   if (roll < 0)
   {
    s_CategoryUntil[pick] = now + Math.Max(10, CATEGORY_COOLDOWN * interval / 15.0);
    return s_Events[pick];
   }
  }
  return "";
 }

 // Client-side existence and audibility probe. Returns -1 when the audio system
 // cannot resolve or cannot hear the event from this position, otherwise the
 // distance to the listener. This is a read-only query; it plays nothing.
 // A bank played through AudioSystem directly belongs to no sound component, so
 // nothing has loaded it: it must be initialised once per client before PlayEvent
 // or IsAudible can resolve its events (campaign 0919-153 read "never audible" at
 // two metres without this). Tried once per world; a failure is remembered, counted
 // by the caller and never retried every sound.
 protected static bool s_BankTried, s_BankReady;
 static bool PrepareBank()
 {
  CheckWorld();
  if (s_BankTried) return s_BankReady;
  s_BankTried = true;
  s_BankReady = AudioSystem.PlayEventInitialize(CIVILIAN_BANK);
  return s_BankReady;
 }
 static bool BankReady() { return s_BankReady; }

 static float ProbeAudible(string eventName, vector position)
 {
  if (!IsApprovedEvent(eventName)) return -1;
  return AudioSystem.IsAudible(VOICE_BANK, eventName, position);
 }

 // Custom-bank requests never trigger legacy component refusal reports;
 // their actual playback is counted only after a valid native handle exists.
 // A false result on the native-component route can report a speaker refusal.
 static bool PlayLocal(IEntity entity, string eventName)
 {
  // The client entry point. A listener never runs Step, so this is where a
  // client notices a world change.
  CheckWorld();
  if (!IsApprovedEvent(eventName)) return false;
  // A null entity here is the receiver failing to resolve the speaker - the
  // exact fault that stayed invisible for six campaigns. Counted, never silent.
  if (!entity) { s_LocalNoEntity++; return false; }
  if (eventName.StartsWith(CUSTOM_PREFIX))
  {
   // Never evict a playing clip to admit another. One shared callback exists
   // only while these bounded, local audio handles are alive.
   if (System.IsConsoleApp() || s_LocalVoices.Count() >= MAX_LOCAL_VOICES) return true;
   if (!PrepareBank()) { s_LocalInvalid++; return true; }
   // Positional one-shot at head height. The bank's own amplitude curve decides how
   // far it carries; a listener beyond it gets an invalid handle, which is silence,
   // not a fault - so nothing is counted as a refusal on this route.
   vector transform[4];
   entity.GetWorldTransform(transform);
   transform[3] = transform[3] + Vector(0, 1.6, 0);
   AudioHandle voice = AudioSystem.PlayEvent(CIVILIAN_BANK, eventName, transform);
   if (voice != AudioHandle.Invalid)
   {
    s_LocalPlayed++;
    EAC_LocalCivilianVoice playing = new EAC_LocalCivilianVoice();
    playing.Actor = entity; playing.Handle = voice;
    s_LocalVoices.Insert(playing);
    EnsureLocalUpdates();
   }
   return true;
  }
  CommunicationSoundComponent sound = CommunicationSoundComponent.Cast(entity.FindComponent(CommunicationSoundComponent));
  if (!sound) { s_LocalNoComponent++; return false; }
  // An invalid handle is the component refusing the name - the silent miss the
  // header describes. Counted here, on the client, where playback happens.
  AudioHandle handle = sound.SoundEvent(eventName);
  if (handle != AudioHandle.Invalid) { s_LocalPlayed++; return true; }
  // Runs 54-57: some civilian prefabs' components do not carry
  // Character_Voice_Animations.acp and refuse the cough, and no other vanilla
  // route plays a voice-class animation event - AudioSystem.PlayEvent returns
  // Invalid after a successful preload, and the sound manager's handle stays
  // Invalid. So a refusal is counted and named, not retried elsewhere.
  if (s_LocalFirstRefused < 0) s_LocalFirstRefused = IndexOfEvent(eventName);
  s_LocalInvalid++;
  return false;
 }

 // Server-side speaker blocklist, fed by client refusals. Runs 54-58 measured
 // that the refusal is a property of the prefab (its sound component lacks the
 // voice bank), so one report retires every resident spawned from that prefab
 // for the rest of the mission. Nothing is retried on a different route.
 protected static ref array<string> s_BlockedPrefabs = {};
 protected static int s_BlockReports, s_GateBlocked;
 static const int MAX_BLOCKED_PREFABS = 8;
 static const int REFUSALS_TO_BLOCK = 6;
 protected static ref map<string, int> s_RefusalCounts = new map<string, int>();

 protected static string PrefabOf(IEntity entity)
 {
  if (!entity) return "";
  EntityPrefabData data = entity.GetPrefabData();
  if (!data) return "";
  return data.GetPrefabName();
 }

 // Audit S7. Server-side record of the speakers this mission actually broadcast.
 // A client's refusal report names an RplId, and the report handler used to trust
 // it: any client could hand back any replicated entity and retire that prefab
 // for every resident on the server. A refusal is only meaningful for a speaker
 // this server asked that client to voice, and this is the list that proves it.
 // Small, bounded and reset per world; oldest entry drops first.
 protected static ref array<IEntity> s_Broadcast = {};
 static const int MAX_BROADCAST_MEMORY = 16;
 protected static int s_BlockRefused;

 protected static void RememberBroadcast(IEntity speaker)
 {
  if (!speaker) return;
  if (s_Broadcast.Contains(speaker)) return;
  if (s_Broadcast.Count() >= MAX_BROADCAST_MEMORY) s_Broadcast.RemoveOrdered(0);
  s_Broadcast.Insert(speaker);
 }

 // True only for an entity this server recently chose as a speaker.
 static bool WasBroadcast(IEntity speaker)
 {
  if (!speaker) return false;
  return s_Broadcast.Contains(speaker);
 }

 // A refusal report that named an entity this server never asked anyone to voice.
 static void CountRefusedReport()
 {
  if (s_BlockRefused < 100000) s_BlockRefused++;
 }

 static void BlockSpeaker(IEntity entity)
 {
  if (!Replication.IsServer()) return;
  s_BlockReports++;
  string name = PrefabOf(entity);
  if (name == "" || s_BlockedPrefabs.Contains(name)) return;
  // Owner server log 2026-09-19: all three theme prefabs were refused within four
  // minutes and the mission was silent from then on. Every vanilla civilian prefab
  // inherits one sound component with the same voice bank from Character_Base, so a
  // single refusal is a moment (a culled or interrupted event on that client), not
  // a property of the prefab. A prefab is retired only after REFUSALS_TO_BLOCK
  // reports, and at most MAX_BLOCKED_PREFABS of the theme are ever retired.
  int refusals = s_RefusalCounts.Get(name) + 1;
  s_RefusalCounts.Set(name, refusals);
  if (refusals < REFUSALS_TO_BLOCK) return;
  if (s_BlockedPrefabs.Count() >= MAX_BLOCKED_PREFABS) return;
  s_BlockedPrefabs.Insert(name);
  // Mission-long loss of a speaker remains visible at level zero. The insertion
  // above admits each prefab once, at most MAX_BLOCKED_PREFABS per world.
  int blocked = s_BlockedPrefabs.Count();
  PrintFormat("[EAC voice] speaker prefab refused on a client, blocked: %1 (blocked=%2)", name, blocked, level: LogLevel.WARNING);
 }

 static bool IsBlockedSpeaker(IEntity entity)
 {
  if (s_BlockedPrefabs.IsEmpty()) return false;
  string name = PrefabOf(entity);
  if (name == "") return false;
  return s_BlockedPrefabs.Contains(name);
 }

 static int BlockedPrefabCount() { return s_BlockedPrefabs.Count(); }
 static int BlockReports() { return s_BlockReports; }
 static int GateBlocked() { return s_GateBlocked; }

 static string DescribeSpeakers()
 {
  string s = "voice speakers blocked_prefabs=" + s_BlockedPrefabs.Count().ToString();
  s += " reports=" + s_BlockReports.ToString();
  s += " gate_blocked=" + s_GateBlocked.ToString();
  // A non-zero refused_reports is a client naming a speaker this server never
  // broadcast - a bug or an attempt to retire prefabs it does not own.
  s += " refused_reports=" + s_BlockRefused.ToString();
  return s;
 }

 // Client-local counters; each machine keeps its own. The QA fixture reads the
 // owner's copy over an RPC and compares it with the server's issued count.
 protected static int s_LocalPlayed, s_LocalInvalid, s_LocalNoComponent, s_LocalNoEntity;
 protected static int s_LocalFirstRefused = -1;
 static int LocalFirstRefused() { return s_LocalFirstRefused; }
 static int LocalNoEntity() { return s_LocalNoEntity; }
 // Client-side count of refusal reports handed to the player controller, so a
 // campaign can tell "never sent" from "sent and dropped".
 protected static int s_LocalReported;
 // Log hygiene (readiness plan S1). The counter stays unconditional; the line is
 // gated. The level is passed in rather than read from the static mirror because
 // this runs on a CLIENT, where the module's EOnInit and EOnFrame both return
 // before the mirror is ever written - the caller is the module's own broadcast
 // handler and holds the replicated DebugLevel directly.
 static void CountLocalReport(int debugLevel)
 {
  s_LocalReported++;
  if (debugLevel < 1) return;
  int reports = s_LocalReported;
  PrintFormat("[EAC voice] refused speaker reported to server (reports=%1)", reports);
 }
 static int LocalReported() { return s_LocalReported; }
 // Broadcasts whose speaker was beyond VoiceRange of this client's character and
 // could not be resolved locally: out of earshot, not a failure (run 71).
 protected static int s_LocalOutOfRange;
 static void CountLocalOutOfRange() { s_LocalOutOfRange++; }
 static int LocalOutOfRange() { return s_LocalOutOfRange; }
 static int LocalPlayed() { return s_LocalPlayed; }
 static int LocalInvalid() { return s_LocalInvalid; }
 static int LocalNoComponent() { return s_LocalNoComponent; }

 static const int MAX_LOCAL_VOICES = 4;
 protected static ref array<ref EAC_LocalCivilianVoice> s_LocalVoices = {};
 protected static ref array<ref EAC_PendingCivilianVoice> s_PendingVoices = {};
 protected static bool s_LocalUpdating;

 static void ReceiveLocal(RplId target, string eventName, vector origin, int audibleRange, int debugLevel)
 {
  if (System.IsConsoleApp() || !GetGame() || !GetGame().GetWorld() || !IsApprovedEvent(eventName)) return;
  CheckWorld();
  EAC_PendingCivilianVoice pending = new EAC_PendingCivilianVoice();
  pending.Target = target; pending.Event = eventName; pending.Origin = origin;
  pending.Range = audibleRange; pending.DebugLevel = debugLevel;
  pending.Until = GetGame().GetWorld().GetWorldTime() * 0.001 + 2;
  if (TryReceiveLocal(pending)) return;
  if (s_PendingVoices.Count() >= MAX_LOCAL_VOICES) { s_LocalNoEntity++; return; }
  s_PendingVoices.Insert(pending);
  EnsureLocalUpdates();
 }

 // False means only that replication has not supplied this speaker yet.
 protected static bool TryReceiveLocal(EAC_PendingCivilianVoice pending)
 {
  PlayerController local = GetGame().GetPlayerController();
  IEntity listener;
  if (local) listener = local.GetControlledEntity();
  if (!ChimeraCharacter.Cast(listener)) return true;
  RplComponent rpl = RplComponent.Cast(Replication.FindItem(pending.Target));
  IEntity actor;
  if (rpl) actor = rpl.GetEntity();
  vector origin = pending.Origin;
  if (actor) origin = actor.GetOrigin();
  if (vector.DistanceSq(listener.GetOrigin(), origin) > pending.Range * pending.Range)
  {
   CountLocalOutOfRange();
   return true;
  }
  if (!actor) return false;
  CharacterControllerComponent control = CharacterControllerComponent.Cast(actor.FindComponent(CharacterControllerComponent));
  if (!control || control.GetLifeState() != ECharacterLifeState.ALIVE || control.IsUnconscious() || control.IsPlayerControlled() || actor == listener) return true;
  bool played = PlayLocal(actor, pending.Event);
  if (!played && !Replication.IsServer())
  {
   SCR_PlayerController controller = SCR_PlayerController.Cast(local);
   if (controller)
   {
    controller.EAC_ReportVoiceRefused(pending.Target);
    CountLocalReport(pending.DebugLevel);
   }
  }
  return true;
 }

 protected static void EnsureLocalUpdates()
 {
  if (s_LocalUpdating) return;
  s_LocalUpdating = true;
  GetGame().GetCallqueue().CallLater(UpdateLocalVoices, 100, true);
 }

 protected static void StopLocalVoices()
 {
  if (GetGame()) GetGame().GetCallqueue().Remove(UpdateLocalVoices);
  foreach (EAC_LocalCivilianVoice playing : s_LocalVoices)
   AudioSystem.TerminateSound(playing.Handle);
  s_LocalVoices.Clear();
  s_PendingVoices.Clear();
  s_LocalUpdating = false;
 }

 protected static void UpdateLocalVoices()
 {
  CheckWorld();
  if (!s_World) return;
  float now = s_World.GetWorldTime() * 0.001;
  for (int pendingIndex = s_PendingVoices.Count() - 1; pendingIndex >= 0; pendingIndex--)
  {
   EAC_PendingCivilianVoice pending = s_PendingVoices[pendingIndex];
   if (now >= pending.Until)
   {
    s_LocalNoEntity++;
    s_PendingVoices.Remove(pendingIndex);
   }
   else if (TryReceiveLocal(pending)) s_PendingVoices.Remove(pendingIndex);
  }
  for (int i = s_LocalVoices.Count() - 1; i >= 0; i--)
  {
   EAC_LocalCivilianVoice playing = s_LocalVoices[i];
   CharacterControllerComponent controller;
   if (playing.Actor) controller = CharacterControllerComponent.Cast(playing.Actor.FindComponent(CharacterControllerComponent));
   // IsSoundPlayed means FINISHED, as in vanilla SCR_MovingSoundSourceEntity.
   if (!controller || controller.GetLifeState() != ECharacterLifeState.ALIVE || controller.IsUnconscious() || controller.IsPlayerControlled() || AudioSystem.IsSoundPlayed(playing.Handle))
   {
    AudioSystem.TerminateSound(playing.Handle);
    s_LocalVoices.Remove(i);
    continue;
   }
   if (!playing.Update())
   {
    AudioSystem.TerminateSound(playing.Handle);
    s_LocalVoices.Remove(i);
   }
  }
  if (s_LocalVoices.IsEmpty() && s_PendingVoices.IsEmpty()) StopLocalVoices();
 }

 protected static bool NearListener(IEntity actor, array<IEntity> observers, float radius)
 {
  if (!actor || !observers || observers.IsEmpty() || observers.Count() > 64) return false;
  vector position = actor.GetOrigin();
  float radiusSq = radius * radius;
  foreach (IEntity observer : observers)
  {
   // One dropped observer is not "nobody can hear this": skip it and keep asking.
   if (!observer || !ChimeraCharacter.Cast(observer) || observer.GetWorld() != actor.GetWorld()) continue;
   if (vector.DistanceSq(observer.GetOrigin(), position) <= radiusSq) return true;
  }
  return false;
 }

 protected static bool Eligible(EAC_AmbientModule module, EAC_PedestrianActivation activation, array<IEntity> observers, float now)
 {
  // Anchor the scaled duration to the last sound, not the next visit. Otherwise
  // raising the interval could let an old short deadline expire behind the
  // longer shared gate before a cached/unvisited resident gets retuned.
  if (activation && module.VoiceInterval > 0 && activation.VoiceInterval != module.VoiceInterval)
  {
   if (activation.NextVoiceAt > activation.LastVoiceAt)
    activation.NextVoiceAt = activation.LastVoiceAt + (activation.NextVoiceAt - activation.LastVoiceAt) * module.VoiceInterval / activation.VoiceInterval;
   activation.VoiceInterval = module.VoiceInterval;
  }
  if (!activation || activation.ExclusionRemoval || activation.SessionRemoval || activation.PlayerTouched || now < activation.NextVoiceAt) { EAC_RoutineStats.RecordVoiceReject(EAC_RoutineStats.VREJ_COOLDOWN); return false; }
  EAC_ResidentClaim claim = activation.Claim;
  if (!claim || !claim.Committed || claim.Cache || !claim.Resident || claim.Resident.Dead || !claim.Character) { EAC_RoutineStats.RecordVoiceReject(EAC_RoutineStats.VREJ_CLAIM); return false; }
  if (claim.AlarmUntil > now) { EAC_RoutineStats.RecordVoiceReject(EAC_RoutineStats.VREJ_ALARM); return false; }
  // Distance first: do not resolve components/ownership for inaudible residents.
  if (!NearListener(claim.Character, observers, module.VoiceRange)) { EAC_RoutineStats.RecordVoiceReject(EAC_RoutineStats.VREJ_FAR); return false; }
  if (module.GetResidentActivation(claim.Home, claim.Resident) != claim) { EAC_RoutineStats.RecordVoiceReject(EAC_RoutineStats.VREJ_STALE); return false; }
  if (!EAC_PedestrianSpawner.HasCivilianControl(claim.Character, claim.Group)) { EAC_RoutineStats.RecordVoiceReject(EAC_RoutineStats.VREJ_CONTROL); return false; }
  if (!claim.OptimizerMember || claim.OptimizerMember.WasPlayer) { EAC_RoutineStats.RecordVoiceReject(EAC_RoutineStats.VREJ_OPTIMIZER); return false; }
  CharacterControllerComponent controller = CharacterControllerComponent.Cast(claim.Character.FindComponent(CharacterControllerComponent));
  if (!controller || controller.IsPlayerControlled() || controller.IsUnconscious()) { EAC_RoutineStats.RecordVoiceReject(EAC_RoutineStats.VREJ_POSSESSED); return false; }
  if (SCR_PossessingManagerComponent.GetPlayerIdFromControlledEntity(claim.Character) != 0) { EAC_RoutineStats.RecordVoiceReject(EAC_RoutineStats.VREJ_POSSESSED); return false; }
  // A prefab some client already refused is never chosen again this mission.
  // Counted locally (not a VREJ gate) so requested/rejected totals stay comparable
  // with the earlier campaigns.
  if (IsBlockedSpeaker(claim.Character)) { s_GateBlocked++; return false; }
  return true;
 }

 protected static void RetuneInterval(int interval, float now)
 {
  if (interval <= 0 || interval == s_Interval) return;
  s_NextGlobal = now + Math.Max(0, s_NextGlobal - now) * interval / s_Interval;
  float categoryScale = Math.Max(10, CATEGORY_COOLDOWN * interval / 15.0) / Math.Max(10, CATEGORY_COOLDOWN * s_Interval / 15.0);
  for (int i = 0; i < s_CategoryUntil.Count(); i++)
   s_CategoryUntil[i] = now + Math.Max(0, s_CategoryUntil[i] - now) * categoryScale;
  s_Interval = interval;
 }

 // One shared clock and cursor; no work per resident during the global cooldown.
 // If no nearby speaker qualifies, continue the bounded search next scheduler
 // tick. No arrival-edge bookkeeping, world query or extra server timer.
 static void Step(EAC_AmbientModule module, array<ref EAC_PedestrianActivation> tracked, array<IEntity> observers, float now)
 {
  if (!Replication.IsServer() || !module || module != EAC_AmbientModule.GetActive()) return;
  CheckWorld();
  RetuneInterval(module.VoiceInterval, now);
  if (module.SoundsEnabled <= 0 || module.VoiceInterval <= 0 || now < s_NextGlobal || !tracked || tracked.IsEmpty()) return;
  if (!observers || observers.IsEmpty() || observers.Count() > 64) return;
  EAC_RoutineStats.RecordVoiceRequest();
  int count = tracked.Count();
  int visits = Math.Min(count, MAX_CANDIDATES_PER_STEP);
  for (int offset = 0; offset < visits; offset++)
  {
   if (s_Cursor >= count) s_Cursor = 0;
   EAC_PedestrianActivation activation = tracked[s_Cursor++];
   if (!Eligible(module, activation, observers, now)) continue;
   string chosen = PickEvent(now, module.VoiceInterval);
   // All categories cooling down: wait one interval rather than retrying the
   // same catalogue twice a second. A failed candidate consumes no cooldown.
   s_NextGlobal = now + module.VoiceInterval * Math.RandomFloat(0.6, 1.6);
   if (chosen.IsEmpty()) return;
   // A lone walker follows the full setting range: 6-16 s at interval 1,
   // while the default remains 90-240 s. Category and shared gates still apply.
   activation.LastVoiceAt = now;
   activation.NextVoiceAt = now + Math.RandomFloat(6, 16) * module.VoiceInterval;
   // Custom clips cannot be refused by a character component. Never let a
   // client's legacy refusal report retire a custom-bank speaker's prefab.
   if (!chosen.StartsWith(CUSTOM_PREFIX)) RememberBroadcast(activation.Claim.Character);
   module.EAC_BroadcastVocalisation(activation.Claim.Character, chosen);
   EAC_RoutineStats.RecordVoiceIssued(IndexOfEvent(chosen));
   return;
  }
 }
}
