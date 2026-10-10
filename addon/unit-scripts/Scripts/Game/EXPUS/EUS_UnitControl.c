// One Game Master unit script on one AI character. Server only.
//
// Hold: native locomotion is capped at zero (the movement speed setting alone
// does not stop formation/cover movement; measured by the Garrison fixture), and
// combat move requests become stance changes at the current spot. Aiming,
// turning, firing and stance stay native. Pushed more than 1.5 m off his spot,
// he is put back.
// Freeze: Hold plus a locked stance, every combat move request becomes a stop
// without aiming, visual perception is zeroed and the head follows the nearest
// player inside a 60 degree forward cone so the body does not have to turn; the
// look stays claimed so vanilla idle glances cannot turn him either. He is put
// back once he is 0.35 m off his spot or his body turned more than about 75
// degrees (beyond the head cone, so it never fights the head tracking).
// Animation: Freeze locks plus one vanilla loiter from EUS_AnimationCatalog. A
// pose whose own sequence ends (Smoke, Stand at ease) plays again inside the same
// loiter command (EUS_PoseLoop.c), so there is no exit, no gap and no locomotion
// between two cycles. If the command still ends (falling, a native interruption),
// he is put back on his spot and heading and the pose is issued again; only an
// entry that never starts counts against LOITER_ATTEMPTS. Pushed out of the pose,
// furniture in the way of a pose that needs room, or damage ends it.
//
// Freeze and Hold hold until the Game Master releases them: damage, unconsciousness
// and ragdoll never end them (corrections pause while he is down or ragdolled and
// resume when he is up). Death, a player taking control, entering any vehicle or
// compartment (also ACE captive and carry helpers) and losing the AI end every
// script, each with its own EUS_EEndReason. While Unit Caching pauses the soldier
// (SetCachePaused or a Simulation-cached actor) the control does nothing at all.
//
// The spot only changes when a Game Master moves the unit (the editor transform,
// SCR_EditableCharacterComponent.SetTransform, see EditorMoved), when he drops
// (the floor went away), when he rides a moving platform, or when he was pushed
// off and an object stands on his spot (a physics fight is never kept up).
// Speed caps alone do not hold a soldier: turn-in-place steps, root motion,
// physics pushes and other systems' teleports bypass them (production
// 2026-10-07: a frozen lone officer drifted 0.78 m in 35 s).
//
// Every native effect is owned by reference and undone by Release, which is
// idempotent. Damage (animations only), death, player possession and leaving AI
// control release through native callbacks; the manager tick is only a bounded
// safety net.

// Why a unit control ended (EUS_UnitControl.GetEnd). Stable values: other modules
// (Unit Caching) and persistence bridges read them; append only.
enum EUS_EEndReason
{
 // Still bound, or never bound.
 NONE = 0,
 // Released by the Game Master (Release action, Normal AI).
 GAME_MASTER = 1,
 // Replaced by another unit script.
 REPLACED = 2,
 // An animation was interrupted by damage (Freeze and Hold never end on damage).
 DAMAGE = 3,
 DEATH = 4,
 // A player possessed or took control of the soldier.
 POSSESSED = 5,
 // He entered a vehicle or a compartment (also helper compartments of other mods).
 VEHICLE = 6,
 // The actor was deleted or the AI agent no longer controls him.
 REMOVED = 7,
 // An animation was pushed off its spot.
 POSE_PUSHED = 8,
 // No room for the pose (Sit on a chair) where it is issued.
 POSE_NO_ROOM = 9,
 // The pose entry never started after LOITER_ATTEMPTS attempts.
 LOITER_FAILED = 10,
 // Silent release: the manager or the world ended.
 SILENT = 11
}

class EUS_UnitControl
{
 static const float FREEZE_LOOK_RANGE = 25;
 static const float FREEZE_LOOK_DOT = 0.5;
 static const int LOITER_ATTEMPTS = 4;
 static const float LOITER_PENDING_SECONDS = 15;
 // Pause after LOITER_ATTEMPTS entries the graph did not take; then a fresh allowance.
 static const float LOITER_BACKOFF_SECONDS = 30;
 // Seconds his AI is kept out of its maximum LOD for one pose entry.
 static const float LOITER_PIN_SECONDS = 15;
 // Horizontal drift put back by Freeze and Hold.
 static const float FREEZE_TOLERANCE = 0.35;
 static const float HOLD_TOLERANCE = 1.5;
 // Cosine of the largest Freeze body turn kept (about 75 degrees, beyond the head cone).
 static const float FREEZE_TURN_DOT = 0.25;
 // A loiter shifts the body a little; farther means he was pushed out of the pose.
 static const float ANIMATION_TOLERANCE = 1.5;
 // A drop of this many metres is a fall, not a push: he keeps the lower spot.
 static const float FALL_HEIGHT = 1;
 // Found again this close to where the previous correction found him, on the very
 // next tick, with an object on his spot: that object pushes him there, so he
 // keeps it.
 static const float FIGHT_RADIUS = 0.25;
 // A platform moving faster than this (squared m/s) carries the spot with it.
 static const float PLATFORM_SPEED_SQ = 0.04;
 // After the bind or a Game Master move the unit comes to rest and the owner
 // teleport lands; Hold and Freeze take the spot he settles on.
 static const float SETTLE_SECONDS = 1.5;
 // Bounded hold log: the first correction at once, then at most one line per unit per interval.
 static const float NOTE_SECONDS = 300;
 // Bounded Game Master move log: the first move at once, then at most one line per unit per interval.
 static const float MOVE_NOTE_SECONDS = 10;
 // Room re-check while a pose that needs room is held (one trace per interval).
 static const float ROOM_SECONDS = 10;
 // Before a pose is issued again he is put back on his spot when he stands farther
 // than this off it (m) or turned more than about 6 degrees (cosine), so repeated
 // cycles never walk or turn him away.
 static const float POSE_SNAP = 0.05;
 static const float POSE_TURN_DOT = 0.995;

 protected SCR_ChimeraCharacter m_Actor;
 protected SCR_AIGroup m_Group;
 protected AIAgent m_Agent;
 protected SCR_AIUtilityComponent m_Utility;
 protected SCR_AICharacterSettingsComponent m_Settings;
 protected AICharacterMovementComponent m_Movement;
 protected SCR_CharacterControllerComponent m_Controller;
 protected SCR_AICombatComponent m_Combat;
 protected SCR_DamageManagerComponent m_Damage;
 protected ref EUS_SpeedSetting m_Speed;
 protected ref EUS_StanceSetting m_Stance;
 protected int m_Code;
 protected vector m_Anchor;
 protected vector m_Forward;
 protected EMovementType m_PreviousMovement;
 protected float m_PreviousPerception = -1;
 // Pose entries issued since the pose was last seen playing.
 protected int m_LoiterAttempts;
 protected bool m_LoiterLogged;
 protected bool m_LoiterBackoffLogged;
 protected float m_NextLoiter;
 protected float m_PendingSince = -1;
 // Unit Caching pause (SetCachePaused), and whether the previous tick was paused
 // (by that call or by a Simulation-cached actor).
 protected bool m_CachePaused;
 protected bool m_WasPaused;
 protected EUS_EEndReason m_End;
 protected float m_SettleUntil = -1;
 // What already stood on the spot when he settled there (an officer at a desk):
 // never a reason by itself to give the spot up.
 protected IEntity m_SpotObstacle;
 // The previous tick put him back, from here.
 protected bool m_JustCorrected;
 protected vector m_PushedTo;
 protected float m_NextRoom;
 protected float m_NextNote;
 protected float m_NextMoveNote;
 protected int m_Moves;
 protected int m_Corrections;
 protected int m_Turns;
 protected int m_TotalCorrections;
 protected float m_Largest;
 protected bool m_Bound;
 protected string m_EndReason;

 bool IsBound() { return m_Bound; }
 int GetCode() { return m_Code; }
 SCR_ChimeraCharacter GetActor() { return m_Actor; }
 SCR_AIGroup GetGroup() { return m_Group; }
 vector GetAnchor() { return m_Anchor; }
 vector GetForward() { return m_Forward; }
 int GetCorrections() { return m_TotalCorrections; }
 string GetEndReason() { return m_EndReason; }
 // Why it ended (NONE while bound). Stable for other modules; see EUS_EEndReason.
 EUS_EEndReason GetEnd() { return m_End; }
 bool IsCachePaused() { return m_CachePaused; }

 // Unit Caching: while paused this control does nothing (no correction, no pose
 // retry, no look); the hidden soldier is never moved and no attempt is spent. On
 // un-pause the retry allowance is fresh and an ended pose starts again at once at
 // his spot. A Simulation-cached actor (EBG_IsSimulationCached) is paused the same
 // way without this call. Idempotent.
 void SetCachePaused(bool paused)
 {
  if (m_CachePaused == paused)
  {
   return;
  }
  m_CachePaused = paused;
  if (!paused && m_Bound && !Paused())
  {
   Unpause(EUS_Codes.WorldSeconds());
  }
 }

 protected bool Paused()
 {
  if (m_CachePaused)
  {
   return true;
  }
  return m_Actor && m_Actor.EBG_IsSimulationCached();
 }

 // Back from a pause: fresh allowance; an ended pose starts again now.
 protected void Unpause(float now)
 {
  m_WasPaused = false;
  m_JustCorrected = false;
  m_LoiterAttempts = 0;
  m_PendingSince = -1;
  m_NextLoiter = now;
  m_NextRoom = now;
  if (!EUS_Codes.IsAnimation(m_Code) || !IsOwnedActor() || Down() || m_Controller.IsLoitering())
  {
   return;
  }
  SCR_ScriptedCharacterInputContext input = m_Controller.GetScrInputContext();
  if (input && input.m_iLoiteringType >= 0)
  {
   return;
  }
  StartLoiter(now);
 }

 // Unconscious or ragdolled: nothing is held, looked at or re-issued until he is up.
 protected bool Down()
 {
  if (m_Controller.GetLifeState() != ECharacterLifeState.ALIVE)
  {
   return true;
  }
  CharacterAnimationComponent animation = m_Controller.GetAnimationComponent();
  return animation && animation.IsRagdollActive();
 }

 // Empty when the character may receive a unit script, otherwise the reason.
 static string Eligibility(SCR_ChimeraCharacter actor)
 {
  if (!actor || actor.IsDeleted()) return "the unit no longer exists";
  CharacterControllerComponent controller = actor.GetCharacterController();
  if (!controller || controller.IsDead() || controller.GetLifeState() != ECharacterLifeState.ALIVE) return "the unit is dead or unconscious";
  if (controller.IsPlayerControlled()) return "the unit is player-controlled";
  if (actor.IsInVehicle()) return "the unit is in a vehicle";
  SCR_DamageManagerComponent damage = actor.GetDamageManager();
  if (damage && damage.IsDestroyed()) return "the unit is dead";
  AIControlComponent control = actor.GetAIControlComponent();
  if (!control || !control.GetAIAgent()) return "the unit has no AI";
  SCR_AIGroup group = SCR_AIGroup.Cast(control.GetAIAgent().GetParentGroup());
  // EXPBG Garrison owns its squads' posts and patrols; never stack controls.
  if (group && group.EXPG_Active) return "the unit belongs to an EXPBG Garrison (use Release Garrison first)";
  return string.Empty;
 }

 // Eligibility plus the native AI components Bind needs: empty when a restored
 // soldier can take his saved script now (right after a load his AI may still be
 // initializing).
 static string Readiness(SCR_ChimeraCharacter actor)
 {
  string reason = Eligibility(actor);
  if (!reason.IsEmpty())
  {
   return reason;
  }
  AIAgent agent = actor.GetAIControlComponent().GetAIAgent();
  SCR_AIUtilityComponent utility = SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent));
  if (!utility || !utility.m_CombatMoveState || utility.m_OwnerEntity != actor || !agent.FindComponent(SCR_AICharacterSettingsComponent))
  {
   return "the unit's AI is not ready";
  }
  return string.Empty;
 }

 // Server: puts a soldier on a spot and heading the way Restore does (the editor's
 // owner teleport). Used before a saved script binds.
 static void PlaceAt(notnull SCR_ChimeraCharacter actor, vector anchor, vector forward)
 {
  vector transform[4];
  actor.GetWorldTransform(transform);
  vector previous = transform[3];
  transform[0] = Vector(forward[2], 0, -forward[0]);
  transform[1] = Vector(0, 1, 0);
  transform[2] = forward;
  transform[3] = anchor;
  actor.Teleport(transform);
  Physics physics = actor.GetPhysics();
  if (physics)
  {
   physics.SetVelocity(vector.Zero);
   physics.SetAngularVelocity(vector.Zero);
  }
  RplComponent rpl = actor.GetRplComponent();
  if (rpl) rpl.ForceNodeMovement(previous);
 }

 bool Bind(SCR_ChimeraCharacter actor, int code, float now, out string reason)
 {
  reason = Eligibility(actor);
  if (!reason.IsEmpty()) return false;
  if (!EUS_Codes.IsScript(code)) { reason = "unknown unit script"; return false; }
  AIAgent agent = actor.GetAIControlComponent().GetAIAgent();
  SCR_AIUtilityComponent utility = SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent));
  SCR_AICharacterSettingsComponent settings = SCR_AICharacterSettingsComponent.Cast(agent.FindComponent(SCR_AICharacterSettingsComponent));
  AICharacterMovementComponent movement = AICharacterMovementComponent.Cast(actor.FindComponent(AICharacterMovementComponent));
  SCR_CharacterControllerComponent controller = SCR_CharacterControllerComponent.Cast(actor.GetCharacterController());
  if (!utility || !utility.m_CombatMoveState || utility.m_OwnerEntity != actor || !settings || !movement || !controller)
  {
   reason = "the unit lacks the native AI components";
   return false;
  }
  if (utility.EXPG_GetPostControl() || utility.EXPG_GetPatrolControl() || controller.EXPG_GetPostControl() || controller.EXPG_GetPatrolControl())
  {
   reason = "the unit is held by an EXPBG Garrison post or patrol";
   return false;
  }
  if (utility.EUS_GetControl() || controller.EUS_GetControl())
  {
   reason = "the unit already runs another unit script";
   return false;
  }

  m_Actor = actor;
  m_Agent = agent;
  m_Group = SCR_AIGroup.Cast(agent.GetParentGroup());
  m_Utility = utility;
  m_Settings = settings;
  m_Movement = movement;
  m_Controller = controller;
  m_Combat = utility.m_CombatComponent;
  m_Damage = actor.GetDamageManager();
  m_Code = code;
  m_Anchor = actor.GetOrigin();
  m_Forward = EUS_Codes.Forward(actor);
  m_PreviousMovement = movement.GetMovementTypeWanted();
  // Refused before any native state changes: a pose without room would fight the
  // furniture's physics every frame. Checked where he stands now, like RoomFor.
  if (EUS_Codes.IsAnimation(code))
  {
   reason = RoomHere(code - EUS_Codes.ANIMATION);
   if (!reason.IsEmpty()) return false;
  }
  m_SettleUntil = now + SETTLE_SECONDS;
  m_NextRoom = now + ROOM_SECONDS;

  m_Speed = EUS_SpeedSetting.Create();
  if (!m_Settings.AddCharacterSetting(m_Speed, false, false))
  {
   m_Speed = null;
   reason = "the native AI settings refused the movement lock";
   return false;
  }
  if (code != EUS_Codes.HOLD)
  {
   ECharacterStance stance = controller.GetStance();
   if (EUS_Codes.IsAnimation(code)) stance = ECharacterStance.STAND;
   m_Stance = EUS_StanceSetting.Create(stance);
   if (!m_Settings.AddCharacterSetting(m_Stance, false, false)) m_Stance = null;
   if (m_Combat)
   {
    m_PreviousPerception = m_Combat.GetPerceptionFactor();
    m_Combat.SetPerceptionFactor(0);
   }
   utility.m_CombatMoveState.EnableAiming(false);
  }

  m_Bound = true;
  // The utility forwards to its combat move state, which re-filters a running request.
  m_Utility.EUS_SetControl(this);
  m_Controller.EUS_SetControl(this);
  m_End = EUS_EEndReason.NONE;
  // Only a pose ends on damage; Freeze and Hold hold until the Game Master releases them.
  if (m_Damage && EUS_Codes.IsAnimation(code)) m_Damage.GetOnDamage().Insert(OnDamage);
  m_Movement.SetMovementTypeWanted(EMovementType.IDLE);
  // Native idle/formation motion can bypass the wanted speed; the character's
  // own speed cap gates locomotion. Other owners' slowdowns stay intact.
  m_Actor.SetSpeedLimit(this, 0, true);
  if (EUS_Codes.IsAnimation(code) && !StartLoiter(now))
  {
   reason = m_EndReason;
   return false;
  }
  m_Actor.EUS_SetScript(code);
  if (EBG_CacheDebug.Verbose()) PrintFormat("[EUS] bound unit=%1 script='%2' anchor=%3", m_Actor, EUS_Codes.Describe(code), m_Anchor);
  return true;
 }

 // True while this control still owns a living AI soldier (he may be unconscious).
 bool IsOwnedActor()
 {
  EUS_EEndReason end;
  return m_Bound && Lost(end).IsEmpty();
 }

 // Empty while he is still a living AI soldier of this agent outside any vehicle;
 // otherwise why not, with its end code. Unconsciousness is not a loss.
 protected string Lost(out EUS_EEndReason end)
 {
  end = EUS_EEndReason.REMOVED;
  if (!m_Actor || m_Actor.IsDeleted() || !m_Agent || !m_Utility || !m_Settings || !m_Controller || !m_Movement)
  {
   return "the unit was removed";
  }
  if (m_Controller.IsDead())
  {
   end = EUS_EEndReason.DEATH;
   return "the unit died";
  }
  if (m_Controller.IsPlayerControlled())
  {
   end = EUS_EEndReason.POSSESSED;
   return "a player took control of the unit";
  }
  if (m_Actor.IsInVehicle())
  {
   end = EUS_EEndReason.VEHICLE;
   return "the unit entered a vehicle or compartment (" + Compartment() + ")";
  }
  AIControlComponent control = m_Actor.GetAIControlComponent();
  if (!control || control.GetAIAgent() != m_Agent || m_Agent.GetControlledEntity() != m_Actor)
  {
   return "the unit left AI control";
  }
  return string.Empty;
 }

 // Name of the vehicle or helper entity whose compartment he occupies.
 protected string Compartment()
 {
  CompartmentAccessComponent access = m_Actor.GetCompartmentAccessComponent();
  if (!access || !access.GetCompartment())
  {
   return "unknown";
  }
  return EUS_Codes.Name(access.GetCompartment().GetOwner());
 }

 // Bounded manager tick. False once released.
 bool Tick(float now, notnull array<IEntity> players)
 {
  if (!m_Bound)
  {
   return false;
  }
  EUS_EEndReason end;
  string lost = Lost(end);
  if (!lost.IsEmpty())
  {
   ReleaseBy(end, lost);
   return false;
  }
  // Unit Caching holds him: nothing moves him and no pose attempt is spent.
  if (Paused())
  {
   m_WasPaused = true;
   return true;
  }
  if (m_WasPaused)
  {
   Unpause(now);
   if (!m_Bound)
   {
    return false;
   }
  }
  if (Down())
  {
   m_JustCorrected = false;
   return true;
  }
  if (m_SettleUntil >= 0)
  {
   // Coming to rest after the bind or a Game Master move: Hold and Freeze take the
   // spot he settles on; an animation keeps the spot it was started or moved to.
   if (!EUS_Codes.IsAnimation(m_Code)) TakeSpot();
   if (now >= m_SettleUntil)
   {
    m_SettleUntil = -1;
    // One trace per bind or move: furniture already on the settled spot.
    if (!EUS_Codes.IsAnimation(m_Code)) m_SpotObstacle = SpotObstacle();
   }
  }
  else if (!HoldSpot(now))
  {
   return false;
  }
  m_Movement.SetMovementTypeWanted(EMovementType.IDLE);
  if (m_Code == EUS_Codes.HOLD) return true;

  SCR_AICombatMoveState state = m_Utility.m_CombatMoveState;
  if (state && state.m_bAimAtTarget) state.EnableAiming(false);
  if (m_Combat && m_Combat.GetPerceptionFactor() > 0) m_Combat.SetPerceptionFactor(0);
  if (m_Code == EUS_Codes.FREEZE)
  {
   TrackHead(players);
   return true;
  }
  return KeepLoiter(now);
 }

 // Hold and Freeze: puts the unit back on his spot when he drifted off it.
 // Animation: see KeepPose. False once released.
 protected bool HoldSpot(float now)
 {
  vector origin = m_Actor.GetOrigin();
  float drift = vector.DistanceXZ(origin, m_Anchor);
  if (EUS_Codes.IsAnimation(m_Code)) return KeepPose(now, drift);
  // Mid-fall or mid-stance change: judge him once he has landed.
  if (m_Controller.IsFalling() || m_Controller.IsChangingStance()) return true;
  float tolerance = HOLD_TOLERANCE;
  bool turned;
  if (m_Code == EUS_Codes.FREEZE)
  {
   tolerance = FREEZE_TOLERANCE;
   turned = vector.Dot(EUS_Codes.Forward(m_Actor), m_Forward) < FREEZE_TURN_DOT;
  }
  bool pushed = drift > tolerance;
  if (!pushed && !turned)
  {
   m_JustCorrected = false;
   return true;
  }
  // Snapping back must never fight the world. He keeps the new spot after a drop
  // (the floor went away) or on a moving platform. Pushed off, he keeps it when an
  // object now stands on his spot, or when an object stands there and he is found
  // again where the previous tick's correction found him (it keeps pushing him
  // there; furniture that was already on the spot only counts this way). Without
  // an object on the spot nothing in the world holds him off it: whatever moves
  // him (his own AI, root motion, a steady push) is put back on every tick, so
  // the spot never walks along with a mover. Physics does not turn a standing
  // character, so a turn is always put back.
  string kept;
  if (m_Anchor[1] - origin[1] > FALL_HEIGHT)
  {
   kept = "dropped from";
  }
  else
  {
   IEntity platform = MovingPlatform();
   if (platform)
   {
    kept = "rode on " + EUS_Codes.Name(platform) + " from";
   }
   else if (pushed)
   {
    IEntity obstacle = SpotObstacle();
    if (obstacle && obstacle != m_SpotObstacle) kept = "was pushed off by " + EUS_Codes.Name(obstacle) + " from";
    else if (obstacle && m_JustCorrected && vector.DistanceXZ(origin, m_PushedTo) < FIGHT_RADIUS) kept = "keeps being pushed off by " + EUS_Codes.Name(obstacle) + " from";
   }
  }
  if (!kept.IsEmpty())
  {
   vector previous = m_Anchor;
   TakeSpot();
   m_SpotObstacle = SpotObstacle();
   m_JustCorrected = false;
   if (now >= m_NextNote)
   {
    m_NextNote = now + NOTE_SECONDS;
    Note(string.Format("%1 %2; holding the new spot %3 (not a Game Master move)", kept, previous, m_Anchor));
   }
   return true;
  }
  Restore(turned);
  m_JustCorrected = true;
  m_PushedTo = origin;
  m_Corrections++;
  if (turned) m_Turns++;
  m_TotalCorrections++;
  m_Largest = Math.Max(m_Largest, drift);
  if (now < m_NextNote) return true;
  m_NextNote = now + NOTE_SECONDS;
  Note(string.Format("held on its spot %1: %2 correction(s) since the last note (%3 turned back), largest drift %4 m (not a Game Master move)", m_Anchor, m_Corrections, m_Turns, m_Largest));
  m_Corrections = 0;
  m_Turns = 0;
  m_Largest = 0;
  return true;
 }

 // Animation: never snapped back (a loiter shifts him a little). Pushed out of the
 // pose, or an obstacle in the way of a pose that needs room, ends it: keeping it
 // would make the physics fight every frame. False once released.
 protected bool KeepPose(float now, float drift)
 {
  if (drift > ANIMATION_TOLERANCE)
  {
   m_End = EUS_EEndReason.POSE_PUSHED;
   Release(string.Format("pushed %1 m off its spot while animating (no room for this pose here)", Math.Round(drift * 10) * 0.1));
   return false;
  }
  if (now < m_NextRoom) return true;
  m_NextRoom = now + ROOM_SECONDS;
  // The held spot, not where the pose's root motion has carried him: the box
  // covers both his spot and the seat in front of it.
  string blocked = PoseRoom(m_Code - EUS_Codes.ANIMATION, m_Anchor, m_Forward);
  if (blocked.IsEmpty()) return true;
  m_End = EUS_EEndReason.POSE_NO_ROOM;
  Release(blocked);
  return false;
 }

 protected void TakeSpot()
 {
  m_Anchor = m_Actor.GetOrigin();
  m_Forward = EUS_Codes.Forward(m_Actor);
 }

 // What a character would bump into on the held spot (knee to head height,
 // 0.4 m square), or null. One position trace. The standing collider is a 0.75 m
 // capsule, so an object that pushes him more than the Freeze tolerance off his
 // spot reaches well inside this box.
 protected IEntity SpotObstacle()
 {
  vector pose[4];
  PoseAt(m_Anchor, m_Forward, pose);
  IEntity obstacle;
  Blocked(pose, Vector(-0.2, 0.4, -0.2), Vector(0.2, 1.6, 0.2), obstacle);
  return obstacle;
 }

 // The moving body he stands on (a vehicle bed, a lift), or null. Vanilla's loiter
 // uses the same physics link to stop on a moving platform.
 protected IEntity MovingPlatform()
 {
  CharacterAnimationComponent animation = m_Controller.GetAnimationComponent();
  if (!animation || !animation.PhysicsIsLinked()) return null;
  IEntity platform = animation.GetLinkedEntity();
  if (!platform || !platform.GetPhysics()) return null;
  if (platform.GetPhysics().GetVelocity().LengthSq() <= PLATFORM_SPEED_SQ) return null;
  return platform;
 }

 // Upright transform at origin facing a horizontal unit forward (right = up x forward).
 protected void PoseAt(vector origin, vector forward, out vector pose[4])
 {
  pose[0] = Vector(forward[2], 0, -forward[0]);
  pose[1] = Vector(0, 1, 0);
  pose[2] = forward;
  pose[3] = origin;
 }

 // Back on the spot the way the editor's owner teleport moves a character
 // (SCR_EditableEntityComponent.SetTransformOwner). The heading is kept unless he
 // turned away.
 protected void Restore(bool turn)
 {
  vector transform[4];
  m_Actor.GetWorldTransform(transform);
  vector previous = transform[3];
  if (turn) PoseAt(m_Anchor, m_Forward, transform);
  transform[3] = m_Anchor;
  m_Actor.Teleport(transform);
  Physics physics = m_Actor.GetPhysics();
  if (physics)
  {
   physics.SetVelocity(vector.Zero);
   physics.SetAngularVelocity(vector.Zero);
  }
  RplComponent rpl = m_Actor.GetRplComponent();
  if (rpl) rpl.ForceNodeMovement(previous);
 }

 // Empty when the script has room where this running unit stands now, otherwise
 // the reason. The manager asks before replacing a running script; it checks
 // exactly what Bind checks next (RoomHere), so a refusal here keeps the running
 // script and a pass here is never refused by the bind.
 string RoomFor(int code)
 {
  if (!m_Bound || !m_Actor || !EUS_Codes.IsAnimation(code)) return string.Empty;
  return RoomHere(code - EUS_Codes.ANIMATION);
 }

 // The pose's room where the unit stands and faces now: where the bind and every
 // loiter attempt issue the pose (StartLoiter takes his current transform). A held
 // Freeze or Hold may stand up to its tolerance off its spot and face elsewhere.
 protected string RoomHere(int index)
 {
  return PoseRoom(index, m_Actor.GetOrigin(), EUS_Codes.Forward(m_Actor));
 }

 // Empty when the pose has room at origin facing forward, otherwise the reason.
 // Only the chair pose needs it (EUS_AnimationCatalog.NeedsRoom): its root motion
 // seats him on a chair without collision 0.65 m in front of him, so furniture
 // there is what his body runs into (production 2026-10-07: with the unit seated
 // next to a GM-placed table the server fell from 240 to 70-145 FPS for about an
 // hour and recovered within 20 s of the release, while Freeze on the same spot
 // cost nothing; the mechanism is inferred, not reproduced). The box covers his
 // spot and the seat: 0.9 m wide, 0.2 m behind to 1.05 m ahead, 0.3 to 1.3 m high.
 protected string PoseRoom(int index, vector origin, vector forward)
 {
  if (!EUS_AnimationCatalog.NeedsRoom(index)) return string.Empty;
  vector pose[4];
  PoseAt(origin, forward, pose);
  IEntity obstacle;
  if (!Blocked(pose, Vector(-0.45, 0.3, -0.2), Vector(0.45, 1.3, 1.05), obstacle)) return string.Empty;
  return string.Format("no room for '%1' here: %2 is in the way (move the unit into the open or use Sit on the ground)", EUS_AnimationCatalog.Name(index), EUS_Codes.Name(obstacle));
 }

 // True when something a character collides with overlaps the box (local to
 // pose). Characters and their gear, the unit included, are ignored. Decided by
 // the hit entity, not by the sign of the result: a touching hit can return 0
 // (vanilla SCR_EntitySpawnerSlotComponent.IsOccupied and
 // SCR_MultiPartDeployableItemComponent.CheckAvailableSpace do the same).
 protected bool Blocked(vector pose[4], vector mins, vector maxs, out IEntity obstacle)
 {
  TraceOBB trace = new TraceOBB();
  trace.Mat[0] = pose[0];
  trace.Mat[1] = pose[1];
  trace.Mat[2] = pose[2];
  trace.Start = pose[3];
  trace.Mins = mins;
  trace.Maxs = maxs;
  trace.Flags = TraceFlags.ENTS;
  trace.LayerMask = EPhysicsLayerPresets.Character;
  trace.Exclude = m_Actor;
  m_Actor.GetWorld().TracePosition(trace, IgnoreCharacters);
  obstacle = trace.TraceEnt;
  return obstacle != null;
 }

 protected bool IgnoreCharacters(notnull IEntity e, vector start = "0 0 0", vector dir = "0 0 0")
 {
  return !ChimeraCharacter.Cast(e.GetRootParent());
 }

 // The one log line of a bound unit besides bind and release; every caller is
 // bounded per unit (at most one Game Master move note per MOVE_NOTE_SECONDS, at
 // most LOITER_ATTEMPTS loiter notes per allowance, at most one hold note per
 // NOTE_SECONDS).
 protected void Note(string text)
 {
  if (!EBG_CacheDebug.Verbose()) return;
  PrintFormat("[EUS] unit=%1 %2", m_Actor, text);
 }

 // The module's line without a bound unit (save and restore summaries). Every
 // caller is bounded: one line per save, per drained restore batch, per refused
 // saved row.
 static void Log(string text)
 {
  Print("[EUS] " + text);
 }

 // Server: a Game Master moved the unit (editor drag, squad move, position
 // attribute). He takes the new spot; the owner teleport may land a frame later,
 // so the hold settles first. A pose gets a fresh retry allowance and room check.
 // Every move updates the state; the log line is rate-limited per unit, because
 // another mod may call the editable's SetTransform on him repeatedly.
 void OnEditorMoved(vector transform[4], float now)
 {
  if (!m_Bound) return;
  m_Anchor = transform[3];
  vector forward = transform[2];
  forward[1] = 0;
  if (forward.LengthSq() > 0.0001)
  {
   forward.Normalize();
   m_Forward = forward;
  }
  m_SettleUntil = now + SETTLE_SECONDS;
  m_JustCorrected = false;
  m_LoiterAttempts = 0;
  m_NextLoiter = now;
  m_NextRoom = now;
  // The chair pose's chair stands in the world, not in his hands: he gets up and
  // sits down again at the new spot (after its room check) instead of sitting in
  // the air next to the chair he left behind.
  if (EUS_Codes.IsAnimation(m_Code) && EUS_AnimationCatalog.NeedsRoom(m_Code - EUS_Codes.ANIMATION) && m_Controller.IsLoitering()) m_Controller.StopLoitering(true);
  m_Moves++;
  if (now < m_NextMoveNote) return;
  m_NextMoveNote = now + MOVE_NOTE_SECONDS;
  Note(string.Format("moved by the Game Master to %1 (%2 move(s) since the last note)", m_Anchor, m_Moves));
  m_Moves = 0;
 }

 // SCR_EditableCharacterComponent.SetTransform succeeded on the server.
 static void EditorMoved(IEntity owner, vector transform[4])
 {
  SCR_ChimeraCharacter actor = SCR_ChimeraCharacter.Cast(owner);
  if (!actor || actor.EUS_Script == EUS_Codes.NONE) return;
  SCR_CharacterControllerComponent controller = SCR_CharacterControllerComponent.Cast(actor.GetCharacterController());
  EUS_Manager manager = EUS_Manager.Current();
  if (!controller || !manager) return;
  EUS_UnitControl control = controller.EUS_GetControl();
  if (control) control.OnEditorMoved(transform, manager.Now());
 }

 // Head tracking for Freeze: the nearest player inside the forward cone,
 // otherwise straight ahead. The look is re-claimed only when it ended (vanilla
 // completes a look after its duration) or another behaviour took it over:
 // vanilla idle glances (Idle_Observe, priority 1, every 10-15 s) would otherwise
 // turn the body.
 protected void TrackHead(notnull array<IEntity> players)
 {
  SCR_AILookAction look = m_Utility.m_LookAction;
  if (!look) return;
  vector eye = m_Actor.EyePosition();
  IEntity target = EUS_Codes.Nearest(eye, players, FREEZE_LOOK_RANGE, m_Forward, FREEZE_LOOK_DOT, m_Actor);
  // Straight ahead at his own eye height (a prone soldier does not look up); the
  // point is fixed to the spot, so breathing never re-sends it.
  vector wanted = m_Anchor + m_Forward * 10;
  wanted[1] = eye[1];
  if (target) wanted = EUS_Codes.Eye(target);
  if (vector.DistanceSq(look.m_vPosition, wanted) < 0.01) return;
  look.LookAt(wanted, EUS_Codes.LOOK_PRIORITY, 1.5);
 }

 // False once released (no room for the pose).
 protected bool StartLoiter(float now)
 {
  int index = m_Code - EUS_Codes.ANIMATION;
  m_NextLoiter = now + 2;
  if (m_Controller.IsChangingStance() || m_Controller.IsFalling())
  {
   return true;
  }
  if (m_Controller.GetStance() != ECharacterStance.STAND)
  {
   SCR_AIStanceHandling.SetStance(m_Controller, ECharacterStance.STAND);
   return true;
  }
  ELoiteringType type = EUS_AnimationCatalog.Type(index);
  if (!m_Controller.CanPlayLoiterAnimation(type)) return true;
  // Every entry starts on the held spot and heading: cycles and re-issues never
  // carry him away (the first entry and a Game Master move start where he stands).
  if (vector.DistanceXZ(m_Actor.GetOrigin(), m_Anchor) > POSE_SNAP || vector.Dot(EUS_Codes.Forward(m_Actor), m_Forward) < POSE_TURN_DOT) Restore(true);
  // Where the loiter below is issued: his current transform.
  string blocked = RoomHere(index);
  if (!blocked.IsEmpty())
  {
   m_End = EUS_EEndReason.POSE_NO_ROOM;
   Release(blocked);
   return false;
  }
  vector transform[4];
  m_Actor.GetWorldTransform(transform);
  m_LoiterAttempts++;
  m_NextLoiter = now + 6;
  // Far from every player the server keeps his AI at its maximum LOD and the entry is
  // never taken (production 2026-10-10): a short pin, as Ambient Civilians does; a
  // cache's permanent LOD pin is left alone.
  if (m_Agent && m_Agent.GetPermanentLOD() < 0)
  {
   m_Agent.PreventMaxLOD(LOITER_PIN_SECONDS);
   m_Agent.SetLOD(0);
   m_Agent.ActivateAI();
  }
  // Owner-side vanilla entry; the command handler replicates it, including JIP.
  m_Controller.StartLoitering(null, type, EUS_AnimationCatalog.Holster(index), true, false, transform, true, EUS_AnimationCatalog.CreateData(index));
  // Log the first loiter and every genuine retry. A routine re-issue after a held
  // loiter (attempts reset to 0, so this is attempt 1 again) stays silent; Release
  // still logs the final failure.
  if (!m_LoiterLogged || m_LoiterAttempts > 1)
  {
   Note(string.Format("loiter=%1 attempt=%2", typename.EnumToString(ELoiteringType, type), m_LoiterAttempts));
   m_LoiterLogged = true;
  }
  return true;
 }

 protected bool KeepLoiter(float now)
 {
  if (m_Controller.IsLoitering())
  {
   m_PendingSince = -1;
   // It plays: a later end is the pose's own end (or an interruption), never a
   // failed entry. Only entries that never start count against LOITER_ATTEMPTS.
   m_LoiterAttempts = 0;
   m_LoiterBackoffLogged = false;
   return true;
  }
  SCR_ScriptedCharacterInputContext input = m_Controller.GetScrInputContext();
  if (input && input.m_iLoiteringType >= 0)
  {
   // Native entry is queued behind holstering/alignment; give it a bounded window.
   if (m_PendingSince < 0) m_PendingSince = now;
   if (now - m_PendingSince < LOITER_PENDING_SECONDS) return true;
   input.m_iLoiteringType = -1;
   input.SetLoiteringEntity(null);
  }
  m_PendingSince = -1;
  if (now < m_NextLoiter) return true;
  if (m_LoiterAttempts >= LOITER_ATTEMPTS)
  {
   // The script is never dropped for an entry the graph did not take: far from every
   // player the server may not run his animation graph (production 2026-10-10: 14
   // poses released after a CDF load with nobody near them). He keeps his spot and
   // the pose is tried again later with a fresh allowance.
   if (!m_LoiterBackoffLogged)
   {
    Note(string.Format("animation entry not taken after %1 attempts; retrying every %2 s", LOITER_ATTEMPTS, LOITER_BACKOFF_SECONDS));
    m_LoiterBackoffLogged = true;
   }
   m_LoiterAttempts = 0;
   m_NextLoiter = now + LOITER_BACKOFF_SECONDS;
   return true;
  }
  return StartLoiter(now);
 }

 // Native damage callback (server), subscribed for animations only: a pose ends
 // when he is hit. Healing, regeneration and bleeding ticks of an existing wound
 // do not count as new damage. Freeze and Hold never end on damage.
 void OnDamage(BaseDamageContext damageContext)
 {
  if (!m_Bound || !EUS_Codes.IsAnimation(m_Code) || !damageContext || damageContext.damageValue <= 0)
  {
   return;
  }
  EDamageType type = damageContext.damageType;
  if (type == EDamageType.HEALING || type == EDamageType.REGENERATION || type == EDamageType.BLEEDING) return;
  m_End = EUS_EEndReason.DAMAGE;
  Release("the unit took damage", true);
 }

 // Combat move requests while bound. Returns the replacement, or null to keep
 // the original. Never claims a cancelled movement succeeded.
 SCR_AICombatMoveRequestBase Filter(notnull SCR_AICombatMoveRequestBase request)
 {
  if (!m_Bound) return null;
  EUS_EEndReason end;
  string lost = Lost(end);
  if (!lost.IsEmpty())
  {
   ReleaseBy(end, lost);
   return null;
  }
  if (m_Code == EUS_Codes.HOLD)
  {
   SCR_AICombatMoveRequest_Move move = SCR_AICombatMoveRequest_Move.Cast(request);
   if (!move) return null;
   SCR_AICombatMoveRequest_ChangeStance stance = new SCR_AICombatMoveRequest_ChangeStance();
   stance.m_eStance = move.m_eStanceEnd;
   stance.m_eReason = move.m_eReason;
   stance.m_eUnitType = move.m_eUnitType;
   stance.m_f_UserTimer_s = move.m_f_UserTimer_s;
   stance.m_bAimAtTarget = true;
   stance.m_bAimAtTargetEnd = true;
   move.m_eState = SCR_EAICombatMoveRequestState.CANCELED;
   return stance;
  }
  if (SCR_AICombatMoveRequest_Stop.Cast(request))
  {
   request.m_bAimAtTarget = false;
   request.m_bAimAtTargetEnd = false;
   return null;
  }
  SCR_AICombatMoveRequest_Stop stop = new SCR_AICombatMoveRequest_Stop();
  stop.m_eReason = request.m_eReason;
  stop.m_eUnitType = request.m_eUnitType;
  stop.m_f_UserTimer_s = request.m_f_UserTimer_s;
  stop.m_bAimAtTarget = false;
  stop.m_bAimAtTargetEnd = false;
  if (request.m_eState == SCR_EAICombatMoveRequestState.EXECUTING || request.m_eState == SCR_EAICombatMoveRequestState.IDLE) request.m_eState = SCR_EAICombatMoveRequestState.CANCELED;
  return stop;
 }

 // Per-frame input callback (server-owned AI). Possession releases at once.
 void OnPrepareControls(CharacterControllerComponent controller, bool player)
 {
  if (!m_Bound) return;
  if (player)
  {
   ReleaseBy(EUS_EEndReason.POSSESSED, "a player took control of the unit");
   return;
  }
  CharacterInputContext input = controller.GetInputContext();
  if (input) input.SetMovement(0, vector.Zero);
 }

 // Release with its end code (the first code set wins; see GetEnd).
 void ReleaseBy(EUS_EEndReason end, string reason, bool fast = false)
 {
  if (!m_Bound)
  {
   return;
  }
  if (m_End == EUS_EEndReason.NONE) m_End = end;
  Release(reason, fast);
 }

 // A plain Release without an end code counts as a Game Master release, or as
 // SILENT without a reason (destructor, manager stop).
 void Release(string reason, bool fast = false)
 {
  if (!m_Bound) return;
  m_Bound = false;
  m_EndReason = reason;
  if (m_End == EUS_EEndReason.NONE)
  {
   if (reason.IsEmpty()) m_End = EUS_EEndReason.SILENT;
   else m_End = EUS_EEndReason.GAME_MASTER;
  }
  if (m_Damage) m_Damage.GetOnDamage().Remove(OnDamage);
  if (m_Actor) m_Actor.SetSpeedLimit(this, 1);
  if (m_Controller && m_Controller.EUS_GetControl() == this) m_Controller.EUS_SetControl(null);
  if (m_Utility && m_Utility.EUS_GetControl() == this) m_Utility.EUS_SetControl(null);
  if (m_Settings)
  {
   if (m_Stance) m_Settings.RemoveSetting(m_Stance);
   if (m_Speed)
   {
    m_Settings.RemoveSetting(m_Speed);
    // Undo only our still-active movement value, respecting remaining settings.
    if (m_Movement && m_Movement.GetMovementTypeWanted() == EMovementType.IDLE)
    {
     EMovementType speed = m_PreviousMovement;
     SCR_AICharacterMovementSpeedSettingBase other = SCR_AICharacterMovementSpeedSettingBase.Cast(m_Settings.GetCurrentSetting(SCR_AICharacterMovementSpeedSettingBase));
     if (other) speed = other.GetSpeed(speed);
     m_Movement.SetMovementTypeWanted(speed);
    }
   }
  }
  m_Stance = null;
  m_Speed = null;
  if (m_Code != EUS_Codes.HOLD)
  {
   // Restore only values that are still ours.
   if (m_Combat && m_PreviousPerception >= 0 && m_Combat.GetPerceptionFactor() == 0) m_Combat.SetPerceptionFactor(m_PreviousPerception);
   if (m_Utility && m_Utility.m_CombatMoveState) m_Utility.m_CombatMoveState.EnableAiming(true);
  }
  if (EUS_Codes.IsAnimation(m_Code) && m_Controller) StopAnimation(fast);
  if (m_Actor) m_Actor.EUS_SetScript(EUS_Codes.NONE);
  if (reason.IsEmpty()) return;
  string held;
  if (m_TotalCorrections > 0) held = string.Format(" (put back on its spot %1 times)", m_TotalCorrections);
  PrintFormat("[EUS] released unit=%1 script='%2' reason='%3'%4", m_Actor, EUS_Codes.Describe(m_Code), reason, held);
 }

 protected void StopAnimation(bool fast)
 {
  if (m_Controller.IsLoitering())
  {
   m_Controller.StopLoitering(fast);
   return;
  }
  // A queued entry (still holstering or aligning) would otherwise start later.
  SCR_ScriptedCharacterInputContext input = m_Controller.GetScrInputContext();
  if (input && input.m_iLoiteringType >= 0)
  {
   input.m_iLoiteringType = -1;
   input.SetLoiteringEntity(null);
  }
 }

 void ~EUS_UnitControl()
 {
  Release(string.Empty);
 }
}

// The utility forwards ownership to exactly this agent's combat move state.
modded class SCR_AIUtilityComponent
{
 protected EUS_UnitControl m_EUS_Control;

 EUS_UnitControl EUS_GetControl()
 {
  return m_EUS_Control;
 }

 void EUS_SetControl(EUS_UnitControl control)
 {
  m_EUS_Control = control;
  if (m_CombatMoveState) m_CombatMoveState.EUS_SetControl(control);
 }
}

// Requests are filtered when applied; a request already running when the script
// binds is filtered once at bind time. Native processing stays untouched.
modded class SCR_AICombatMoveState
{
 protected EUS_UnitControl m_EUS_Control;

 void EUS_SetControl(EUS_UnitControl control)
 {
  m_EUS_Control = control;
  if (!control) return;
  SCR_AICombatMoveRequestBase running = GetRequest();
  if (!running || running.m_eState != SCR_EAICombatMoveRequestState.EXECUTING) return;
  SCR_AICombatMoveRequestBase replacement = control.Filter(running);
  if (replacement) ApplyNewRequest(replacement);
 }

 override void ApplyNewRequest(notnull SCR_AICombatMoveRequestBase request)
 {
  EUS_UnitControl control = m_EUS_Control;
  if (control)
  {
   SCR_AICombatMoveRequestBase replacement = control.Filter(request);
   if (replacement)
   {
    super.ApplyNewRequest(replacement);
    return;
   }
  }
  super.ApplyNewRequest(request);
 }
}

modded class SCR_CharacterControllerComponent
{
 protected EUS_UnitControl m_EUS_Control;

 EUS_UnitControl EUS_GetControl()
 {
  return m_EUS_Control;
 }

 void EUS_SetControl(EUS_UnitControl control)
 {
  m_EUS_Control = control;
 }

 protected override void OnPrepareControls(IEntity owner, ActionManager am, float dt, bool player)
 {
  super.OnPrepareControls(owner, am, dt, player);
  EUS_UnitControl control = m_EUS_Control;
  if (control) control.OnPrepareControls(this, player);
 }

 // Server-side possession signal; releases before native control setup runs.
 override protected void OnControlledByPlayer(IEntity owner, bool controlled)
 {
  EUS_UnitControl possessed = m_EUS_Control;
  if (controlled && possessed) possessed.ReleaseBy(EUS_EEndReason.POSSESSED, "a player took control of the unit");
  super.OnControlledByPlayer(owner, controlled);
 }
}

// Game Master moves of a character (editor drag, squad move, position attribute)
// all end here on the server (vanilla SCR_RefPreviewEntity.ApplyChild and
// SCR_PositionEditorAttribute). A scripted unit takes the new spot; anything
// else that displaces him is not a Game Master move.
modded class SCR_EditableCharacterComponent
{
 override bool SetTransform(vector transform[4], bool changedByUser = false)
 {
  bool moved = EUS_LeaveLooseCompartment(transform, changedByUser);
  if (!moved) moved = super.SetTransform(transform, changedByUser);
  if (moved) EUS_UnitControl.EditorMoved(GetOwner(), transform);
  return moved;
 }

 // Vanilla moves a seated character out of his compartment and, 100 ms later,
 // unregisters the compartment's vehicle from his AI group through GetVehicle(),
 // the vehicle's editable entity. A compartment without one (ACE Captives surrender
 // and tied helpers, other mods' animation helper compartments) made vanilla
 // dereference a null GetVehicle() and later RemoveUsableVehicle(null) (production
 // server 2026-10-08, every Game Master move of such a unit). Same move here, minus
 // the unregister: a helper without an editable vehicle was never one of the
 // group's usable vehicles. True when this handled the move.
 protected bool EUS_LeaveLooseCompartment(vector transform[4], bool changedByUser)
 {
  IEntity owner = GetOwner();
  if (!owner || !IsServer())
  {
   return false;
  }
  CompartmentAccessComponent access = CompartmentAccessComponent.Cast(owner.FindComponent(CompartmentAccessComponent));
  if (!access || !access.IsInCompartment() || GetVehicle())
  {
   return false;
  }
  RplComponent rpl = RplComponent.Cast(owner.FindComponent(RplComponent));
  if (!rpl)
  {
   return false;
  }
  // As vanilla: never below the ground, feedback for a moved player, the owner leaves.
  transform[3][1] = Math.Max(transform[3][1], owner.GetWorld().GetSurfaceY(transform[3][0], transform[3][2]));
  if (changedByUser && IsPlayerOrPossessed()) Rpc(PlayerTeleportedFeedback, false);
  Rpc(GetOutVehicleOwner, rpl.Id(), transform);
  return true;
 }
}
