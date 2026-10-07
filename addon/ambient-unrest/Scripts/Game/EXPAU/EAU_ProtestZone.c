[EntityEditorProps(category: "EXPBG/Ambient", description: "EXPBG Civil Protest Zone: a static crowd of unarmed protesting civilians with angry and rioting crowd audio")]
class EAU_ProtestZoneClass : GenericEntityClass {}

// One protester of the current run. The actor pointer is weak: a deleted body reads null.
class EAU_Protester
{
 SCR_ChimeraCharacter Actor;
 // The actor's AI control (weak), found once at spawn; Observe looks it up again only while unset.
 AIControlComponent Control;
 ResourceName Prefab;
 vector Spot;
 // Spawn facing (entity yaw, 0-360); the protester turns back to it when no player is in sight.
 float HomeYaw;
 float SettleAt;
 float NextGesture;
 int LastGesture = -1;
 bool Settled;
 bool Dead;
 // Facing a player: the chosen player (weak), kept until FaceUntil while still in sight.
 IEntity FaceTarget;
 bool Facing;
 float FaceUntil;
 float NextFace;
 // Last heading request, verified at the next facing check.
 float WantedYaw;
 bool TurnPending;
}

// A protester of a sleeping zone: who stood where, facing which way.
class EAU_CachedProtester
{
 ResourceName Prefab;
 vector Spot;
 float Yaw;
}

// Server-authoritative GM zone. Everything it creates (one civilian group, its
// members and one Ambient Sounds crowd emitter) is plain replicated entities, so
// join-in-progress clients receive them natively. The zone only replicates its
// settings and a short status for the GM dialog and the GM-only radius mesh.
// While no player character is near, the zone sleeps: crowd, group and sound are
// removed and the survivors are remembered; they return when a player comes back.
class EAU_ProtestZone : GenericEntity
{
 static const int KEY_ENABLED = 0;
 static const int KEY_RADIUS = 1;
 static const int KEY_CROWD_MIN = 2;
 static const int KEY_CROWD_MAX = 3;
 static const int KEY_DEBUG = 4;
 static const int KEY_SOUND = 5;
 static const int KEY_WAKE = 6;
 static const int SETTING_COUNT = 7;
 // Saves made before the Crowd sound setting hold only keys 0-4.
 static const int LEGACY_SETTING_COUNT = 5;
 // Saves made before the Wake distance setting hold keys 0-5.
 static const int SOUND_SETTING_COUNT = 6;
 static const int WAKE_MIN = 50;
 static const int WAKE_MAX = 3000;
 static const int WAKE_DEFAULT = 300;
 // A gathered crowd stays until every player is this much farther than the wake distance...
 static const int SLEEP_MARGIN = 50;
 // ...for this long (s). Player distances are checked at most every PRESENCE_SECONDS.
 static const float SLEEP_DELAY_SECONDS = 10;
 static const float PRESENCE_SECONDS = 2;
 // Replicated status for the GM dialog (not saved).
 static const int STATE_OFF = 0;
 static const int STATE_GATHERING = 1;
 static const int STATE_PROTESTING = 2;
 static const int STATE_SLEEPING = 3;
 // Facing players: each protester looks for a player every 3-5 s and keeps a
 // chosen one 8-15 s while still in sight; small corrections are skipped.
 static const float FACE_MIN_SECONDS = 3;
 static const float FACE_MAX_SECONDS = 5;
 static const float FACE_HOLD_MIN = 8;
 static const float FACE_HOLD_MAX = 15;
 static const float TURN_TOLERANCE = 12;
 // A body still this far from its last requested heading at the next check is placed facing it.
 static const float TURN_MISS_DEGREES = 35;
 // Crowd sound values (saved; permanent).
 static const int SOUND_ANGRY = 0;
 static const int SOUND_RIOTING = 1;
 static const int SOUND_ALTERNATE = 2;
 static const int RADIUS_MIN = 3;
 static const int RADIUS_MAX = 30;
 static const int CROWD_MIN = 1;
 static const int CROWD_MAX = 30;
 static const string CIV_FACTION = "CIV";
 static const ResourceName GROUP_PREFAB = "{000CD338713F2B5A}Prefabs/AI/Groups/Group_Base.et";
 static const ResourceName SOUND_PREFAB = "{E1BA8A82AB3C485B}Prefabs/EXPAU/EAU_ProtestCrowdSound.et";
 // Ambient Sounds crowd selections (permanent IDs): recording 0 "Angry crowd",
 // recording 4 "Rioting crowd"; group 101 plays them in turn, switching every loop.
 static const int RECORDING_ANGRY = 0;
 static const int RECORDING_RIOTING = 4;
 static const int SELECTION_ALTERNATE = 101;
 static const int SOUND_VOLUME = 55;
 // A moved module regathers its crowd at the new centre.
 static const float ANCHOR_TOLERANCE = 2;
 // Minimum shoulder-to-shoulder spacing between protesters (m).
 static const float SPACING = 1.1;
 // Consecutive rejected ground probes before a run stops adding protesters.
 static const int MAX_PROBE_FAILURES = 64;
 // Armed or broken prefab candidates tolerated per run before spawning stops.
 static const int MAX_REJECTS = 8;
 static const float DIAGNOSTIC_SECONDS = 10;
 static const int WARN_FACTION = 1;
 static const int WARN_AI_LIMIT = 2;
 static const int WARN_GROUND = 4;
 static const int WARN_PREFAB = 8;
 static const int WARN_CAPACITY = 16;
 static const int WARN_SOUND = 32;

 [Attribute("0", UIWidgets.CheckBox, "On/Off. New zones start Off.", category: "EXPBG Civil Protest Zone"), RplProp()]
 int Enabled;
 [Attribute("8", UIWidgets.EditBox, "Crowd area radius around the centre (m)", "3 30 1", category: "EXPBG Civil Protest Zone"), RplProp(onRplName: "OnRadiusReplicated")]
 int RadiusMeters;
 [Attribute("10", UIWidgets.EditBox, "Crowd size minimum", "1 30 1", category: "EXPBG Civil Protest Zone"), RplProp()]
 int CrowdMin;
 [Attribute("15", UIWidgets.EditBox, "Crowd size maximum", "1 30 1", category: "EXPBG Civil Protest Zone"), RplProp()]
 int CrowdMax;
 [Attribute("0", UIWidgets.CheckBox, "Debug: server diagnostics and the crowd sound's GM rings", category: "EXPBG Civil Protest Zone"), RplProp()]
 int DebugEnabled;
 [Attribute("2", UIWidgets.EditBox, "Crowd sound: 0 angry crowd, 1 rioting crowd, 2 alternate (switches every loop). New zones use 2.", "0 2 1", category: "EXPBG Civil Protest Zone"), RplProp()]
 int CrowdSound;
 [Attribute("300", UIWidgets.EditBox, "Wake distance (m): the crowd and its sound exist only while a player character is this close", "50 3000 25", category: "EXPBG Civil Protest Zone"), RplProp()]
 int WakeDistance;
 [RplProp()]
 protected int m_RunState;
 [RplProp()]
 protected int m_RunCount;

 protected ref array<ref EAU_Protester> m_Members = {};
 // Sleeping zone: the survivors to bring back, the size target, the casualties
 // that stay missing and where the crowd stood. While awake it holds the cached
 // protesters not yet returned.
 protected ref array<ref EAU_CachedProtester> m_Cached = {};
 protected bool m_HasCache;
 protected int m_CacheTarget;
 protected int m_CacheLost;
 protected vector m_CacheAnchor;
 protected bool m_Asleep;
 protected float m_AbsentSince = -1;
 protected float m_NextPresence;
 protected ref EAU_WorldSight m_Sight;
 // Players near the crowd this tick, with their eye positions (facing).
 protected ref array<IEntity> m_FaceTargets = {};
 protected ref array<vector> m_FaceEyes = {};
 // Entities waiting for bounded removal, in order: crowd sound, members, group.
 protected ref array<IEntity> m_Retire = {};
 protected SCR_AIGroup m_Group;
 protected EAS_CrowdModule m_Sound;
 protected int m_SoundRange = -1;
 protected int m_SoundDebug = -1;
 protected int m_SoundSelection = -1;
 protected bool m_Registered;
 protected bool m_Running;
 protected bool m_SizeDirty;
 protected bool m_RadiusDirty;
 protected bool m_Exhausted;
 protected vector m_Anchor;
 protected float m_Yaw;
 protected int m_Run;
 protected int m_Target;
 protected int m_Placed;
 protected int m_Rejects;
 protected int m_ProbeFailures;
 protected int m_Warned;
 protected float m_NextSpawn;
 protected float m_NextDiagnostics;
 // One repeat of the save exclusion after creation, in case tracking began late.
 protected float m_SavesRecheckAt;
 protected float m_NextSound;
 // Bounded counters for Debug and the native fixture.
 protected int m_Spawned, m_Deleted, m_SpawnFailures, m_GesturesStarted, m_GesturesRefused, m_Released;
 protected int m_Sleeps, m_Wakes, m_FacePicks, m_TurnsRequested, m_TurnsSnapped;

 void EAU_ProtestZone(IEntitySource src, IEntity parent)
 {
  SetEventMask(EntityEvent.INIT);
 }

 static bool IsRuntime()
 {
  if (!GetGame() || !GetGame().GetWorld()) return false;
  return GetGame().InPlayMode();
 }

 override void EOnInit(IEntity owner)
 {
  if (!IsRuntime() || SCR_Global.IsEditMode(owner) || !Replication.IsServer()) return;
  Normalize(-1);
  m_Registered = EAU_Director.Register(this);
  Trace("created");
 }

 void ~EAU_ProtestZone()
 {
  if (!m_Registered) return;
  // A deleted module hands everything it still answers for to the director, which
  // removes it a few entities per tick. A world teardown simply discards the list.
  RetireAll();
  EAU_Director.Unregister(this, m_Retire);
  m_Retire.Clear();
 }

 //------------------------------------------------------------------------------------------------
 // Settings
 int GetSetting(int key)
 {
  // Literal cases: keys 0-6 match the KEY_ constants and the Edit.conf m_Key values.
  switch (key)
  {
   case 0: return Enabled;
   case 1: return RadiusMeters;
   case 2: return CrowdMin;
   case 3: return CrowdMax;
   case 4: return DebugEnabled;
   case 5: return CrowdSound;
   case 6: return WakeDistance;
  }
  return 0;
 }

 static bool ValidSetting(int key, int value)
 {
  if (key == KEY_ENABLED || key == KEY_DEBUG) return value == 0 || value == 1;
  if (key == KEY_RADIUS) return value >= RADIUS_MIN && value <= RADIUS_MAX;
  if (key == KEY_CROWD_MIN || key == KEY_CROWD_MAX) return value >= CROWD_MIN && value <= CROWD_MAX;
  if (key == KEY_SOUND) return value >= SOUND_ANGRY && value <= SOUND_ALTERNATE;
  if (key == KEY_WAKE) return value >= WAKE_MIN && value <= WAKE_MAX;
  return false;
 }

 // A raised minimum lifts the maximum; a lowered maximum lowers the minimum.
 protected void Normalize(int changed)
 {
  Enabled = Math.Clamp(Enabled, 0, 1);
  DebugEnabled = Math.Clamp(DebugEnabled, 0, 1);
  CrowdSound = Math.Clamp(CrowdSound, SOUND_ANGRY, SOUND_ALTERNATE);
  WakeDistance = Math.Clamp(WakeDistance, WAKE_MIN, WAKE_MAX);
  RadiusMeters = Math.Clamp(RadiusMeters, RADIUS_MIN, RADIUS_MAX);
  CrowdMin = Math.Clamp(CrowdMin, CROWD_MIN, CROWD_MAX);
  CrowdMax = Math.Clamp(CrowdMax, CROWD_MIN, CROWD_MAX);
  if (CrowdMax >= CrowdMin) return;
  if (changed == KEY_CROWD_MAX) CrowdMin = CrowdMax;
  else CrowdMax = CrowdMin;
 }

 void SetSetting(int key, int value)
 {
  if (!Replication.IsServer() || key < 0 || key >= SETTING_COUNT) return;
  int previousMin = CrowdMin;
  int previousMax = CrowdMax;
  int previous = GetSetting(key);
  switch (key)
  {
   case 0: Enabled = value; break;
   case 1: RadiusMeters = value; break;
   case 2: CrowdMin = value; break;
   case 3: CrowdMax = value; break;
   case 4: DebugEnabled = value; break;
   case 5: CrowdSound = value; break;
   case 6: WakeDistance = value; break;
  }
  Normalize(key);
  if (GetSetting(key) == previous && CrowdMin == previousMin && CrowdMax == previousMax) return;
  Replication.BumpMe();
  if (key == KEY_RADIUS) { m_RadiusDirty = true; OnRadiusReplicated(); }
  if (CrowdMin != previousMin || CrowdMax != previousMax) m_SizeDirty = true;
  // A new wake distance is judged on the next tick, not after the 2 s interval.
  if (key == KEY_WAKE) m_NextPresence = 0;
  Trace("settings");
  EAU_Director.Wake();
 }

 // Complete validated batch from native persistence; never a partial apply.
 // A batch saved before the Crowd sound setting existed restores with Alternate,
 // one saved before the Wake distance setting with the 300 m default.
 bool RestoreSettings(array<int> values)
 {
  if (!Replication.IsServer() || !values) return false;
  array<int> batch = {};
  batch.Copy(values);
  if (batch.Count() == LEGACY_SETTING_COUNT) batch.Insert(SOUND_ALTERNATE);
  if (batch.Count() == SOUND_SETTING_COUNT) batch.Insert(WAKE_DEFAULT);
  if (batch.Count() != SETTING_COUNT) return false;
  for (int key = 0; key < SETTING_COUNT; key++)
  {
   if (!ValidSetting(key, batch[key])) return false;
  }
  if (batch[KEY_CROWD_MAX] < batch[KEY_CROWD_MIN]) return false;
  Enabled = batch[KEY_ENABLED];
  RadiusMeters = batch[KEY_RADIUS];
  CrowdMin = batch[KEY_CROWD_MIN];
  CrowdMax = batch[KEY_CROWD_MAX];
  DebugEnabled = batch[KEY_DEBUG];
  CrowdSound = batch[KEY_SOUND];
  WakeDistance = batch[KEY_WAKE];
  m_SizeDirty = true;
  m_RadiusDirty = true;
  m_NextPresence = 0;
  Replication.BumpMe();
  OnRadiusReplicated();
  Trace("settings-restored");
  EAU_Director.Wake();
  return true;
 }

 // Replication callback; the authority calls it directly because it never
 // receives its own RplProp callback.
 void OnRadiusReplicated()
 {
  EAU_ZoneAreaComponent area = EAU_ZoneAreaComponent.Cast(FindComponent(EAU_ZoneAreaComponent));
  if (area) area.Refresh();
 }

 //------------------------------------------------------------------------------------------------
 // Director step (server only). Returns true while the zone still has work.
 bool Step(float now, EAU_Budget budget)
 {
  if (!Replication.IsServer()) return false;
  Observe(now);
  bool wanted = Enabled == 1 && !EAU_Director.HasEnded();
  if (wanted) Presence(now);
  else Forget();
  bool active = wanted && !m_Asleep;
  if (m_Running && (!active || vector.DistanceXZ(GetOrigin(), m_Anchor) > ANCHOR_TOLERANCE))
  {
   if (active) Trace("moved");
   else Trace("stopped");
   // A moved or stopped crowd starts over: nothing cached comes back.
   Forget();
   RetireAll();
  }
  Drain(budget);
  if (!active)
  {
   Publish();
   Diagnostics(now);
   // A sleeping zone keeps its presence check; an Off zone only finishes removal.
   return wanted || !m_Retire.IsEmpty();
  }
  // A new or resized crowd waits until its previous members are gone.
  if (!m_Retire.IsEmpty()) { Publish(); return true; }
  if (!m_Running) StartRun(now);
  if (m_SizeDirty || m_RadiusDirty) Relayout();
  if (!m_Retire.IsEmpty()) { Publish(); return true; }
  SpawnStep(now, budget);
  SoundStep(now, budget);
  if (m_SavesRecheckAt > 0 && now >= m_SavesRecheckAt)
  {
   m_SavesRecheckAt = 0;
   EAU_Director.KeepOutOfSaves(m_Group);
   EAU_Director.KeepOutOfSaves(m_Sound);
  }
  GestureStep(now, budget);
  FaceStep(now, budget);
  Publish();
  Diagnostics(now);
  return true;
 }

 //------------------------------------------------------------------------------------------------
 // Proximity caching, Full-cache style: no player character near means no crowd,
 // no group and no sound. Survivors are remembered and come back to their spots
 // when a player returns; casualties are never replaced.
 // Wake within the wake distance; a gathered crowd stays until every player is
 // SLEEP_MARGIN farther away. nearest < 0: no player character at all.
 static bool PlayerNear(bool awake, float nearest, int wake)
 {
  if (nearest < 0) return false;
  float reach = wake;
  if (awake) reach += SLEEP_MARGIN;
  return nearest <= reach;
 }

 protected void Presence(float now)
 {
  if (now < m_NextPresence) return;
  m_NextPresence = now + PRESENCE_SECONDS;
  bool awake = m_Running && !m_Asleep;
  float nearest = EAU_Director.NearestPlayerDistance(GetOrigin(), now);
  if (PlayerNear(awake, nearest, WakeDistance))
  {
   m_AbsentSince = -1;
   if (!m_Asleep) return;
   m_Asleep = false;
   m_Wakes++;
   Trace("wake");
   return;
  }
  if (m_Asleep) return;
  if (!awake)
  {
   // Nothing gathered yet: wait asleep instead of gathering for nobody.
   m_Asleep = true;
   m_AbsentSince = -1;
   m_Sleeps++;
   Trace("sleep-idle");
   return;
  }
  if (m_AbsentSince < 0) m_AbsentSince = now;
  if (now - m_AbsentSince < SLEEP_DELAY_SECONDS) return;
  EnterSleep();
 }

 // Remember who still stands, then remove crowd, group and sound through the same
 // bounded path as Off. A civilian a player controls stays and counts as lost.
 protected void EnterSleep()
 {
  int pending = m_Cached.Count();
  int kept;
  foreach (EAU_Protester member : m_Members)
  {
   if (!member.Actor || member.Dead || EAU_Director.IsPlayerCharacter(member.Actor)) continue;
   EAU_CachedProtester cached = new EAU_CachedProtester();
   cached.Prefab = member.Prefab;
   cached.Spot = member.Spot;
   cached.Yaw = member.HomeYaw;
   m_Cached.Insert(cached);
   kept++;
  }
  m_CacheLost = m_Placed - kept;
  if (m_CacheLost < 0) m_CacheLost = 0;
  m_CacheTarget = m_Target;
  // A crowd that ran out of ground keeps its size; nobody is added on waking.
  if (m_Exhausted) m_CacheTarget = m_Placed + pending;
  m_CacheAnchor = m_Anchor;
  m_HasCache = true;
  RetireAll();
  m_Asleep = true;
  m_AbsentSince = -1;
  m_Sleeps++;
  Trace("sleep");
 }

 // Off, moved or game end: a sleeping zone forgets its crowd.
 protected void Forget()
 {
  if (m_Asleep || m_HasCache || !m_Cached.IsEmpty()) Trace("cache-cleared");
  m_Asleep = false;
  m_HasCache = false;
  m_Cached.Clear();
  m_AbsentSince = -1;
  // Re-enabled: judge player distance on the first tick.
  m_NextPresence = 0;
 }

 protected void StartRun(float now)
 {
  m_Running = true;
  m_Run++;
  m_Anchor = GetOrigin();
  vector angles = GetYawPitchRoll();
  m_Yaw = angles[0];
  m_Rejects = 0; m_ProbeFailures = 0; m_Warned = 0;
  m_Exhausted = false;
  m_NextSpawn = now;
  m_NextSound = now;
  if (m_HasCache && vector.DistanceXZ(m_Anchor, m_CacheAnchor) <= ANCHOR_TOLERANCE)
  {
   // Waking: the cached civilians return first; casualties stay counted as placed.
   // Settings changed while asleep still apply through Relayout.
   m_HasCache = false;
   m_Target = m_CacheTarget;
   m_Placed = m_CacheLost;
   Trace("run-restore");
   return;
  }
  m_HasCache = false;
  m_Cached.Clear();
  m_Target = Math.RandomIntInclusive(CrowdMin, CrowdMax);
  m_Placed = 0;
  m_SizeDirty = false; m_RadiusDirty = false;
  Trace("run-start");
 }

 // Keep a running crowd where possible: only members outside a smaller radius or
 // above a smaller size leave, and only the shortfall is spawned again.
 protected void Relayout()
 {
  if (m_SizeDirty)
  {
   m_Target = Math.Clamp(m_Target, CrowdMin, CrowdMax);
   m_SizeDirty = false;
  }
  if (m_RadiusDirty)
  {
   float limit = RadiusMeters + 0.25;
   for (int i = m_Members.Count() - 1; i >= 0; i--)
   {
    EAU_Protester member = m_Members[i];
    if (!member.Dead && vector.DistanceXZ(member.Spot, m_Anchor) > limit) RetireMember(i);
   }
   // Cached civilians outside the new edge are replaced inside it, like the living.
   for (int c = m_Cached.Count() - 1; c >= 0; c--)
   {
    if (vector.DistanceXZ(m_Cached[c].Spot, m_Anchor) > limit) m_Cached.RemoveOrdered(c);
   }
   m_RadiusDirty = false;
  }
  // Cached civilians not yet back make room first, outermost first.
  while (!m_Cached.IsEmpty() && m_Placed + m_Cached.Count() > m_Target)
  {
   int outerCached = 0;
   float outerReach = -1;
   foreach (int slot, EAU_CachedProtester waiting : m_Cached)
   {
    float reach = vector.DistanceXZ(waiting.Spot, m_Anchor);
    if (reach > outerReach) { outerReach = reach; outerCached = slot; }
   }
   m_Cached.RemoveOrdered(outerCached);
  }
  while (m_Placed > m_Target)
  {
   int outermost = -1;
   float farthest = -1;
   foreach (int index, EAU_Protester candidate : m_Members)
   {
    if (candidate.Dead) continue;
    float distance = vector.DistanceXZ(candidate.Spot, m_Anchor);
    if (distance > farthest) { farthest = distance; outermost = index; }
   }
   if (outermost < 0) break;
   RetireMember(outermost);
  }
  m_Exhausted = false;
  m_ProbeFailures = 0;
  Trace("relayout");
 }

 protected void RetireMember(int index)
 {
  EAU_Protester member = m_Members[index];
  if (member.Actor) m_Retire.Insert(member.Actor);
  m_Members.Remove(index);
  m_Placed--;
 }

 // Queue everything for removal: the crowd sound stops first, the group goes last.
 protected void RetireAll()
 {
  if (m_Sound) m_Retire.Insert(m_Sound);
  m_Sound = null; m_SoundRange = -1; m_SoundDebug = -1; m_SoundSelection = -1;
  foreach (EAU_Protester member : m_Members)
  {
   if (member.Actor) m_Retire.Insert(member.Actor);
  }
  m_Members.Clear();
  if (m_Group) m_Retire.Insert(m_Group);
  m_Group = null;
  m_Running = false;
  m_Placed = 0;
  m_Target = 0;
 }

 protected void Drain(EAU_Budget budget)
 {
  while (!m_Retire.IsEmpty() && budget.Deletes > 0)
  {
   IEntity entity = m_Retire[0];
   m_Retire.RemoveOrdered(0);
   if (!EAU_Director.Delete(entity)) continue;
   budget.Deletes--;
   m_Deleted++;
  }
 }

 // Game end: the session is over, remove the whole crowd at once.
 void EndSession()
 {
  Forget();
  RetireAll();
  foreach (IEntity entity : m_Retire) EAU_Director.Delete(entity);
  m_Retire.Clear();
 }

 //------------------------------------------------------------------------------------------------
 // Members
 protected void Observe(float now)
 {
  for (int i = m_Members.Count() - 1; i >= 0; i--)
  {
   EAU_Protester member = m_Members[i];
   SCR_ChimeraCharacter actor = member.Actor;
   // Deleted by a Game Master or by garbage collection: a casualty, never replaced.
   if (!actor) { m_Members.Remove(i); continue; }
   if (member.Dead) continue;
   CharacterControllerComponent controller = actor.GetCharacterController();
   if (!controller) continue;
   if (controller.IsDead() || controller.GetLifeState() == ECharacterLifeState.DEAD) { member.Dead = true; continue; }
   if (EAU_Director.IsPlayerCharacter(actor)) continue;
   // A Game Master moved this civilian into another group: it is theirs now.
   SCR_AIGroup parent = actor.GetCharacterGroup();
   if (m_Group && parent && parent != m_Group)
   {
    ReleaseToGameMaster(actor);
    m_Members.Remove(i);
    continue;
   }
   if (!member.Settled && now >= member.SettleAt)
   {
    member.Settled = true;
    EAU_Director.KeepOutOfSaves(actor);
    if (!EAU_CrowdCast.IsUnarmed(actor))
    {
     m_Rejects++;
     EAU_CrowdCast.Reject(member.Prefab, "carries a weapon or grenade");
     Warn(WARN_PREFAB, "an armed civilian prefab was removed from the crowd");
     RetireMember(i);
     continue;
    }
   }
   // Static by design: no movement, no stance change and no danger reaction.
   AIControlComponent control = member.Control;
   if (!control)
   {
    control = AIControlComponent.Cast(actor.FindComponent(AIControlComponent));
    member.Control = control;
   }
   if (control && control.IsAIActivated()) control.DeactivateAI();
  }
  if (m_Group && m_Group.IsAIActivated()) m_Group.DeactivateAI();
 }

 protected void ReleaseToGameMaster(SCR_ChimeraCharacter actor)
 {
  AIControlComponent control = AIControlComponent.Cast(actor.FindComponent(AIControlComponent));
  if (control && !control.IsAIActivated()) control.ActivateAI();
  EAU_Director.ReturnToSaves(actor);
  m_Released++;
  Trace("released");
 }

 int GetLivingCount()
 {
  int living;
  foreach (EAU_Protester member : m_Members)
  {
   if (member.Actor && !member.Dead) living++;
  }
  return living;
 }

 int GetTrackedCount() { return m_Members.Count(); }

 //------------------------------------------------------------------------------------------------
 // Spawning: at most one native creation per director tick across all zones.
 protected void SpawnStep(float now, EAU_Budget budget)
 {
  if (m_Exhausted || m_Placed >= m_Target || now < m_NextSpawn || budget.Spawns <= 0) return;
  if (m_Rejects >= MAX_REJECTS) { m_Exhausted = true; Trace("rejects-exhausted"); return; }
  Faction civilians = Civilians();
  if (!civilians)
  {
   Warn(WARN_FACTION, "the scenario has no CIV faction; no protesters spawn");
   m_NextSpawn = now + 10;
   return;
  }
  if (!m_Group)
  {
   budget.Spawns--;
   SpawnGroup(now, civilians);
   return;
  }
  if (!EAU_Director.HasCrowdCapacity())
  {
   Warn(WARN_CAPACITY, "the shared protester cap is reached; this crowd waits");
   m_NextSpawn = now + 5;
   return;
  }
  if (!HasAiHeadroom())
  {
   Warn(WARN_AI_LIMIT, "the CIV AI limit is reached; this crowd waits");
   m_NextSpawn = now + 5;
   return;
  }
  vector spot;
  ResourceName prefab;
  float yaw = m_Yaw + Math.RandomFloat(-30, 30);
  bool found;
  if (!m_Cached.IsEmpty())
  {
   // A cached civilian returns to its own spot while that is still clear ground,
   // otherwise to a fresh one; prefab and facing are kept either way.
   EAU_CachedProtester cached = m_Cached[m_Cached.Count() - 1];
   prefab = cached.Prefab;
   yaw = cached.Yaw;
   found = !Crowded(cached.Spot) && EAU_CrowdCast.GroundSpot(GetWorld(), cached.Spot, m_Anchor[1], spot);
   if (!found) found = FindSpot(budget, spot);
   if (found) m_Cached.Remove(m_Cached.Count() - 1);
  }
  else
  {
   found = FindSpot(budget, spot);
   if (found) prefab = EAU_CrowdCast.Pick();
  }
  if (!found)
  {
   if (m_ProbeFailures < MAX_PROBE_FAILURES) return;
   m_Exhausted = true;
   Warn(WARN_GROUND, "not enough clear ground inside the radius; the crowd stays smaller");
   return;
  }
  if (prefab.IsEmpty())
  {
   m_Rejects++;
   m_NextSpawn = now + 1;
   return;
  }
  budget.Spawns--;
  SpawnProtester(now, spot, prefab, yaw);
 }

 protected bool FindSpot(EAU_Budget budget, out vector spot)
 {
  BaseWorld world = GetWorld();
  while (budget.Probes > 0 && m_ProbeFailures < MAX_PROBE_FAILURES)
  {
   budget.Probes--;
   // Denser towards the centre: the crowd gathers rather than spreading evenly.
   float distance = RadiusMeters * Math.Pow(Math.RandomFloat01(), 0.7);
   float angle = Math.RandomFloat(0, Math.PI2);
   vector candidate = m_Anchor + Vector(Math.Cos(angle) * distance, 0, Math.Sin(angle) * distance);
   if (Crowded(candidate) || !EAU_CrowdCast.GroundSpot(world, candidate, m_Anchor[1], spot))
   {
    m_ProbeFailures++;
    continue;
   }
   m_ProbeFailures = 0;
   return true;
  }
  return false;
 }

 protected bool Crowded(vector candidate)
 {
  foreach (EAU_Protester member : m_Members)
  {
   if (vector.DistanceXZ(member.Spot, candidate) < SPACING) return true;
  }
  return false;
 }

 protected void SpawnGroup(float now, Faction civilians)
 {
  Resource resource = Resource.Load(GROUP_PREFAB);
  if (!resource || !resource.IsValid()) { m_SpawnFailures++; m_NextSpawn = now + 5; return; }
  EntitySpawnParams params = new EntitySpawnParams();
  params.TransformMode = ETransformMode.WORLD;
  Math3D.MatrixIdentity4(params.Transform);
  params.Transform[3] = m_Anchor;
  IEntity created = GetGame().SpawnEntityPrefab(resource, GetWorld(), params);
  SCR_AIGroup group = SCR_AIGroup.Cast(created);
  if (!group)
  {
   if (created) SCR_EntityHelper.DeleteEntityAndChildren(created);
   m_SpawnFailures++;
   m_NextSpawn = now + 2;
   return;
  }
  // The zone owns the group's lifetime; an empty group must survive until cleanup.
  group.SetDeleteWhenEmpty(false);
  group.SetFaction(civilians);
  EAU_Director.KeepOutOfSaves(group);
  m_Group = group;
  m_SavesRecheckAt = now + 1;
  m_Spawned++;
  Trace("group");
 }

 // yaw: the module's forward direction give or take 30 degrees (rotate the module
 // to aim the protest), or a returning civilian's own facing.
 protected void SpawnProtester(float now, vector spot, ResourceName prefab, float yaw)
 {
  Resource resource = Resource.Load(prefab);
  if (!resource || !resource.IsValid()) { m_Rejects++; m_NextSpawn = now + 1; return; }
  EntitySpawnParams params = new EntitySpawnParams();
  params.TransformMode = ETransformMode.WORLD;
  Math3D.AnglesToMatrix(Vector(yaw, 0, 0), params.Transform);
  params.Transform[3] = spot;
  IEntity created = GetGame().SpawnEntityPrefab(resource, GetWorld(), params);
  SCR_ChimeraCharacter actor = SCR_ChimeraCharacter.Cast(created);
  if (!actor)
  {
   if (created) SCR_EntityHelper.DeleteEntityAndChildren(created);
   m_SpawnFailures++;
   m_NextSpawn = now + 1;
   return;
  }
  EAU_Director.KeepOutOfSaves(actor);
  EAU_Protester member = new EAU_Protester();
  member.Actor = actor;
  member.Prefab = prefab;
  member.Spot = spot;
  member.HomeYaw = Math.Repeat(yaw, 360);
  member.SettleAt = now + 1;
  member.NextGesture = now + Math.RandomFloat(0.5, 3);
  member.NextFace = now + Math.RandomFloat(1, 3);
  m_Members.Insert(member);
  m_Placed++;
  m_Spawned++;
  FactionAffiliationComponent affiliation = FactionAffiliationComponent.Cast(actor.FindComponent(FactionAffiliationComponent));
  if (affiliation) affiliation.SetAffiliatedFactionByKey(CIV_FACTION);
  AIControlComponent control = AIControlComponent.Cast(actor.FindComponent(AIControlComponent));
  member.Control = control;
  if (control)
  {
   AIAgent agent = control.GetControlAIAgent();
   if (agent)
   {
    AIGroup previous = agent.GetParentGroup();
    if (previous && previous != m_Group) previous.RemoveAgent(agent);
    if (m_Group && agent.GetParentGroup() != m_Group) m_Group.AddAgent(agent);
   }
   control.DeactivateAI();
  }
  if (DebugEnabled == 1) PrintFormat("[EAU] zone=%1 run=%2 protester %3/%4 prefab=%5 spot=%6", GetID(), m_Run, m_Placed, m_Target, prefab, spot);
 }

 static Faction Civilians()
 {
  FactionManager factions = GetGame().GetFactionManager();
  if (!factions) return null;
  return factions.GetFactionByKey(CIV_FACTION);
 }

 static bool HasAiHeadroom()
 {
  ChimeraAIWorld ai = ChimeraAIWorld.Cast(GetGame().GetAIWorld());
  if (!ai) return true;
  return ai.CanLimitedAIBeAddedForFaction(CIV_FACTION);
 }

 //------------------------------------------------------------------------------------------------
 // Crowd audio (angry, rioting or both in turn): one Ambient Sounds crowd emitter
 // while anyone is protesting.
 int AudibleRange()
 {
  // Twice the crowd radius plus a street, in the 10 m steps the crowd bank ships.
  float metres = RadiusMeters * 2 + 50;
  int range = Math.Ceil(metres / 10) * 10;
  range = Math.Clamp(range, 60, 150);
  return range;
 }

 // The Crowd sound setting as an Ambient Sounds crowd selection.
 static int SoundSelection(int choice)
 {
  if (choice == SOUND_ANGRY) return RECORDING_ANGRY;
  if (choice == SOUND_RIOTING) return RECORDING_RIOTING;
  return SELECTION_ALTERNATE;
 }

 protected void SoundStep(float now, EAU_Budget budget)
 {
  int living = GetLivingCount();
  if (living == 0)
  {
   bool spawning = !m_Exhausted && m_Placed < m_Target;
   if (!spawning && m_Sound)
   {
    m_Retire.Insert(m_Sound);
    m_Sound = null; m_SoundRange = -1; m_SoundDebug = -1; m_SoundSelection = -1;
    Trace("sound-silenced");
   }
   return;
  }
  if (!m_Sound)
  {
   if (budget.Spawns <= 0 || now < m_NextSound) return;
   budget.Spawns--;
   SpawnSound();
   // A failed emitter retries slowly instead of taking every spawn allowance.
   if (!m_Sound) { m_NextSound = now + 10; return; }
   m_SavesRecheckAt = now + 1;
  }
  int range = AudibleRange();
  int selection = SoundSelection(CrowdSound);
  if (m_SoundRange == range && m_SoundDebug == DebugEnabled && m_SoundSelection == selection) return;
  // New settings restart the emitter, so a changed Crowd sound is heard at once.
  array<int> settings = {selection, SOUND_VOLUME, 1, 1, 1, range, DebugEnabled};
  if (m_Sound.RestoreSettings(settings))
  {
   m_SoundRange = range;
   m_SoundDebug = DebugEnabled;
   m_SoundSelection = selection;
   return;
  }
  Warn(WARN_SOUND, "the Ambient Sounds crowd emitter refused its settings");
  m_Retire.Insert(m_Sound);
  m_Sound = null; m_SoundRange = -1; m_SoundDebug = -1; m_SoundSelection = -1;
  m_NextSound = now + 10;
 }

 protected void SpawnSound()
 {
  Resource resource = Resource.Load(SOUND_PREFAB);
  if (!resource || !resource.IsValid()) { Warn(WARN_SOUND, "crowd sound prefab missing"); return; }
  EntitySpawnParams params = new EntitySpawnParams();
  params.TransformMode = ETransformMode.WORLD;
  Math3D.MatrixIdentity4(params.Transform);
  // Head height above the gathered crowd.
  params.Transform[3] = m_Anchor + "0 1.6 0";
  IEntity created = GetGame().SpawnEntityPrefab(resource, GetWorld(), params);
  m_Sound = EAS_CrowdModule.Cast(created);
  if (!m_Sound)
  {
   if (created) SCR_EntityHelper.DeleteEntityAndChildren(created);
   m_SpawnFailures++;
   Warn(WARN_SOUND, "crowd sound emitter failed to spawn");
   return;
  }
  // The emitter belongs to this zone; Ambient Sounds' own saves must not keep it.
  EAU_Director.KeepOutOfSaves(m_Sound);
  m_SoundRange = -1; m_SoundDebug = -1; m_SoundSelection = -1;
  m_Spawned++;
  Trace("sound");
 }

 //------------------------------------------------------------------------------------------------
 // Protest animation: vanilla character gestures, server-started and natively replicated.
 protected void GestureStep(float now, EAU_Budget budget)
 {
  foreach (EAU_Protester member : m_Members)
  {
   if (member.Dead || !member.Actor || now < member.NextGesture) continue;
   if (budget.Gestures <= 0) return;
   SCR_CharacterControllerComponent controller = SCR_CharacterControllerComponent.Cast(member.Actor.GetCharacterController());
   if (!controller || controller.GetLifeState() != ECharacterLifeState.ALIVE || EAU_Director.IsPlayerCharacter(member.Actor))
   {
    member.NextGesture = now + 2;
    continue;
   }
   if (controller.IsPlayingGesture())
   {
    member.NextGesture = now + 0.5;
    continue;
   }
   budget.Gestures--;
   int gesture;
   float seconds;
   EAU_Gestures.Pick(member.LastGesture, gesture, seconds);
   int duration = Math.Round(seconds * 1000);
   if (controller.TryStartCharacterGesture(gesture, duration))
   {
    member.LastGesture = gesture;
    member.NextGesture = now + seconds + Math.RandomFloat(0.4, 2.2);
    m_GesturesStarted++;
   }
   else
   {
    member.NextGesture = now + Math.RandomFloat(1, 2);
    m_GesturesRefused++;
   }
  }
 }

 //------------------------------------------------------------------------------------------------
 // Facing players. Each protester looks for a player every 3-5 s: one in sight
 // within EAU_Facing.RANGE, picked at random among those it sees, is kept 8-15 s
 // while still in sight. Nobody in sight turns it back to its protest direction.
 // The turn is a vanilla character heading request on the server-simulated AI
 // body (AI stays deactivated), replicated to clients like any AI turn.
 protected void FaceStep(float now, EAU_Budget budget)
 {
  bool gathered;
  foreach (EAU_Protester member : m_Members)
  {
   if (member.Dead || !member.Actor || now < member.NextFace) continue;
   if (!gathered)
   {
    GatherFaceTargets(now);
    gathered = true;
   }
   member.NextFace = now + Math.RandomFloat(FACE_MIN_SECONDS, FACE_MAX_SECONDS);
   // Nothing to look at, check or undo: costs no allowance.
   if (m_FaceTargets.IsEmpty() && !member.Facing && !member.TurnPending) continue;
   if (budget.Faces <= 0)
   {
    member.NextFace = now + 0.5;
    return;
   }
   budget.Faces--;
   Face(member, now);
  }
 }

 protected void GatherFaceTargets(float now)
 {
  m_FaceTargets.Clear();
  m_FaceEyes.Clear();
  float reach = RadiusMeters + EAU_Facing.RANGE;
  array<IEntity> players = EAU_Director.Players(now);
  foreach (IEntity player : players)
  {
   if (!player || vector.DistanceXZ(player.GetOrigin(), m_Anchor) > reach) continue;
   m_FaceTargets.Insert(player);
   m_FaceEyes.Insert(EAU_Facing.Eye(player));
  }
 }

 protected void Face(EAU_Protester member, float now)
 {
  SCR_ChimeraCharacter actor = member.Actor;
  if (EAU_Director.IsPlayerCharacter(actor)) return;
  VerifyTurn(member);
  if (!m_Sight) m_Sight = new EAU_WorldSight(GetWorld());
  vector eye = actor.EyePosition();
  int chosen = -1;
  if (member.Facing && member.FaceTarget && now < member.FaceUntil)
  {
   int kept = m_FaceTargets.Find(member.FaceTarget);
   if (kept >= 0 && vector.Distance(eye, m_FaceEyes[kept]) <= EAU_Facing.RANGE && m_Sight.Sees(kept, eye, m_FaceEyes[kept])) chosen = kept;
  }
  if (chosen < 0 && !m_FaceTargets.IsEmpty())
  {
   chosen = EAU_Facing.Pick(eye, m_FaceEyes, EAU_Facing.RANGE, m_Sight);
   if (chosen >= 0)
   {
    member.FaceUntil = now + Math.RandomFloat(FACE_HOLD_MIN, FACE_HOLD_MAX);
    m_FacePicks++;
   }
  }
  if (chosen >= 0)
  {
   member.FaceTarget = m_FaceTargets[chosen];
   member.Facing = true;
   Turn(member, EAU_Facing.YawTo(actor.GetOrigin(), m_FaceEyes[chosen]));
   return;
  }
  if (!member.Facing) return;
  member.Facing = false;
  member.FaceTarget = null;
  Turn(member, member.HomeYaw);
 }

 protected void Turn(EAU_Protester member, float yaw)
 {
  if (yaw < 0) return;
  SCR_ChimeraCharacter actor = member.Actor;
  CharacterControllerComponent controller = actor.GetCharacterController();
  if (!controller || controller.GetLifeState() != ECharacterLifeState.ALIVE) return;
  vector angles = actor.GetYawPitchRoll();
  if (EAU_Facing.Gap(angles[0], yaw) < TURN_TOLERANCE) return;
  // Heading in radians around world Y, in the -180..180 range GetYawPitchRoll
  // reports, so the controller never sees a full-turn offset. The aim turns with the body.
  float heading = yaw;
  if (heading > 180) heading -= 360;
  controller.SetHeadingAngle(heading * Math.DEG2RAD, true);
  member.WantedYaw = yaw;
  member.TurnPending = true;
  m_TurnsRequested++;
 }

 // A heading the body never took is applied as a Game Master rotation does: a
 // server teleport in place, replicated natively.
 protected void VerifyTurn(EAU_Protester member)
 {
  if (!member.TurnPending) return;
  member.TurnPending = false;
  SCR_ChimeraCharacter actor = member.Actor;
  // An unconscious or dying body is never placed.
  CharacterControllerComponent controller = actor.GetCharacterController();
  if (!controller || controller.GetLifeState() != ECharacterLifeState.ALIVE) return;
  vector angles = actor.GetYawPitchRoll();
  if (EAU_Facing.Gap(angles[0], member.WantedYaw) <= TURN_MISS_DEGREES) return;
  vector transform[4];
  Math3D.AnglesToMatrix(Vector(member.WantedYaw, 0, 0), transform);
  transform[3] = actor.GetOrigin();
  actor.Teleport(transform);
  Physics body = actor.GetPhysics();
  if (body)
  {
   body.SetVelocity(vector.Zero);
   body.SetAngularVelocity(vector.Zero);
  }
  m_TurnsSnapped++;
  if (DebugEnabled == 1) PrintFormat("[EAU] zone=%1 heading refused by %2; placed facing %3", GetID(), actor, member.WantedYaw);
 }

 //------------------------------------------------------------------------------------------------
 // GM status (replicated; read by EAU_StatusAttribute on the Game Master's machine)
 protected void Publish()
 {
  int state = STATE_OFF;
  int count = GetLivingCount();
  if (Enabled == 1 && !EAU_Director.HasEnded())
  {
   if (m_Asleep)
   {
    state = STATE_SLEEPING;
    count = m_Cached.Count();
   }
   else if (IsSpawnFinished()) state = STATE_PROTESTING;
   else state = STATE_GATHERING;
  }
  if (state == m_RunState && count == m_RunCount) return;
  m_RunState = state;
  m_RunCount = count;
  Replication.BumpMe();
 }

 int GetRunState() { return m_RunState; }

 string DescribeState()
 {
  if (m_RunState == STATE_SLEEPING)
  {
   string text = string.Format("Sleeping: no player within %1 m", WakeDistance);
   if (m_RunCount > 0) text += string.Format(", %1 protesters cached", m_RunCount);
   return text;
  }
  if (m_RunState == STATE_GATHERING) return string.Format("Gathering: %1 protesters", m_RunCount);
  if (m_RunState == STATE_PROTESTING) return string.Format("Protesting: %1 protesters", m_RunCount);
  return "Off";
 }

 static string StateName(int state)
 {
  if (state == STATE_SLEEPING) return "sleeping";
  if (state == STATE_GATHERING) return "gathering";
  if (state == STATE_PROTESTING) return "protesting";
  return "off";
 }

 //------------------------------------------------------------------------------------------------
 // Unit Caching and fixture seams
 bool OwnsGroup(SCR_AIGroup group)
 {
  if (!group) return false;
  return group == m_Group || m_Retire.Contains(group);
 }

 SCR_AIGroup GetGroup() { return m_Group; }
 EAS_CrowdModule GetSound() { return m_Sound; }
 int GetTarget() { return m_Target; }
 int GetPlaced() { return m_Placed; }
 int GetRetiring() { return m_Retire.Count(); }
 int GetGesturesStarted() { return m_GesturesStarted; }
 int GetGesturesRefused() { return m_GesturesRefused; }
 bool IsRunning() { return m_Running; }
 bool IsSpawnFinished() { return m_Running && (m_Exhausted || m_Placed >= m_Target); }
 vector GetAnchor() { return m_Anchor; }
 bool IsAsleep() { return m_Asleep; }
 int GetCachedCount() { return m_Cached.Count(); }
 int GetSleeps() { return m_Sleeps; }
 int GetWakes() { return m_Wakes; }
 int GetFacePicks() { return m_FacePicks; }
 int GetTurnsRequested() { return m_TurnsRequested; }
 int GetTurnsSnapped() { return m_TurnsSnapped; }

 void GetActors(notnull array<IEntity> actors)
 {
  actors.Clear();
  foreach (EAU_Protester member : m_Members)
  {
   if (member.Actor) actors.Insert(member.Actor);
  }
 }

 // Everything this zone answers for, none of which may enter a save. Appends.
 void GetCrowdEntities(notnull array<IEntity> entities)
 {
  if (m_Sound) entities.Insert(m_Sound);
  foreach (EAU_Protester member : m_Members)
  {
   if (member.Actor) entities.Insert(member.Actor);
  }
  if (m_Group) entities.Insert(m_Group);
  foreach (IEntity retiring : m_Retire)
  {
   if (retiring) entities.Insert(retiring);
  }
 }

 //------------------------------------------------------------------------------------------------
 // Diagnostics
 protected void Trace(string reason)
 {
  if (DebugEnabled != 1) return;
  string line = string.Format("[EAU] zone=%1 %2 enabled=%3 run=%4 target=%5 placed=%6 living=%7 retiring=%8", GetID(), reason, Enabled, m_Run, m_Target, m_Placed, GetLivingCount(), m_Retire.Count());
  line += string.Format(" radius=%1 size=%2-%3 sound=%4 crowdSound=%5 wake=%6 asleep=%7 cached=%8", RadiusMeters, CrowdMin, CrowdMax, m_Sound != null, CrowdSound, WakeDistance, m_Asleep, m_Cached.Count());
  Print(line);
 }

 // Operator-relevant problems print once per run even with Debug off.
 protected void Warn(int flag, string message)
 {
  if ((m_Warned & flag) != 0) return;
  m_Warned |= flag;
  PrintFormat("[EAU] zone=%1 %2 (placed %3/%4)", GetID(), message, m_Placed, m_Target, level: LogLevel.WARNING);
 }

 protected void Diagnostics(float now)
 {
  if (DebugEnabled != 1 || now < m_NextDiagnostics) return;
  m_NextDiagnostics = now + DIAGNOSTIC_SECONDS;
  string line = string.Format("[EAU STATUS] zone=%1 run=%2 target=%3 placed=%4 living=%5 tracked=%6 retiring=%7 exhausted=%8", GetID(), m_Run, m_Target, m_Placed, GetLivingCount(), m_Members.Count(), m_Retire.Count(), m_Exhausted);
  line += string.Format(" spawned=%1 deleted=%2 failures=%3 rejects=%4 released=%5", m_Spawned, m_Deleted, m_SpawnFailures, m_Rejects, m_Released);
  line += string.Format(" gestures=%1 refused=%2 sound=%3 range=%4 selection=%5", m_GesturesStarted, m_GesturesRefused, m_Sound != null, m_SoundRange, m_SoundSelection);
  line += string.Format(" state=%1 wake=%2 cached=%3 sleeps=%4 wakes=%5 facePicks=%6 turns=%7 snapped=%8", StateName(m_RunState), WakeDistance, m_Cached.Count(), m_Sleeps, m_Wakes, m_FacePicks, m_TurnsRequested, m_TurnsSnapped);
  Print(line);
 }
}
