// One Game Master unit script on one AI character. Server only.
//
// Hold: native locomotion is capped at zero (the movement speed setting alone
// does not stop formation/cover movement; measured by the Garrison fixture), and
// combat move requests become stance changes at the current spot. Aiming,
// turning, firing and stance stay native.
// Freeze: Hold plus a locked stance, every combat move request becomes a stop
// without aiming, visual perception is zeroed and the head follows the nearest
// player inside a 60 degree forward cone so the body does not have to turn.
// Animation: Freeze locks plus one vanilla loiter from EUS_AnimationCatalog,
// re-issued a bounded number of times if the native command ends on its own.
//
// Every native effect is owned by reference and undone by Release, which is
// idempotent. Damage, death, player possession and leaving AI control release
// through native callbacks; the manager tick is only a bounded safety net.
class EUS_UnitControl
{
 static const float FREEZE_LOOK_RANGE = 25;
 static const float FREEZE_LOOK_DOT = 0.5;
 static const int LOITER_ATTEMPTS = 4;
 static const float LOITER_PENDING_SECONDS = 15;

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
 protected int m_LoiterAttempts;
 protected bool m_LoiterLogged;
 protected float m_NextLoiter;
 protected float m_LoiterSince = -1;
 protected float m_PendingSince = -1;
 protected bool m_LookingForward;
 protected bool m_Bound;
 protected string m_EndReason;

 bool IsBound() { return m_Bound; }
 int GetCode() { return m_Code; }
 SCR_ChimeraCharacter GetActor() { return m_Actor; }
 SCR_AIGroup GetGroup() { return m_Group; }
 vector GetAnchor() { return m_Anchor; }
 string GetEndReason() { return m_EndReason; }

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
  if (m_Damage) m_Damage.GetOnDamage().Insert(OnDamage);
  m_Movement.SetMovementTypeWanted(EMovementType.IDLE);
  // Native idle/formation motion can bypass the wanted speed; the character's
  // own speed cap gates locomotion. Other owners' slowdowns stay intact.
  m_Actor.SetSpeedLimit(this, 0, true);
  if (EUS_Codes.IsAnimation(code)) StartLoiter(now);
  m_Actor.EUS_SetScript(code);
  PrintFormat("[EUS] bound unit=%1 script='%2' anchor=%3", m_Actor, EUS_Codes.Describe(code), m_Anchor);
  return true;
 }

 bool IsOwnedActor()
 {
  if (!m_Bound || !m_Actor || m_Actor.IsDeleted() || !m_Agent || !m_Utility || !m_Settings || !m_Controller || !m_Movement) return false;
  if (m_Controller.IsDead() || m_Controller.GetLifeState() != ECharacterLifeState.ALIVE || m_Controller.IsPlayerControlled() || m_Actor.IsInVehicle()) return false;
  AIControlComponent control = m_Actor.GetAIControlComponent();
  return control && control.GetAIAgent() == m_Agent && m_Agent.GetControlledEntity() == m_Actor;
 }

 // Bounded manager tick. False once released.
 bool Tick(float now, notnull array<IEntity> players)
 {
  if (!m_Bound) return false;
  if (!IsOwnedActor())
  {
   Release("the unit died, left AI control or was removed");
   return false;
  }
  // Native locomotion is capped at zero, so a displacement is the Game Master
  // moving the unit with the editor (or physics). Hold the new spot instead of
  // snapping back.
  vector origin = m_Actor.GetOrigin();
  if (vector.DistanceSqXZ(origin, m_Anchor) > 2.25)
  {
   PrintFormat("[EUS] unit=%1 re-anchored from %2 to %3", m_Actor, m_Anchor, origin);
   m_Anchor = origin;
   m_Forward = EUS_Codes.Forward(m_Actor);
   m_LookingForward = false;
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

 // Head tracking for Freeze: nearest player inside the forward cone, otherwise
 // one request to look straight ahead again.
 protected void TrackHead(notnull array<IEntity> players)
 {
  SCR_AILookAction look = m_Utility.m_LookAction;
  if (!look) return;
  vector eye = m_Actor.EyePosition();
  IEntity target = EUS_Codes.Nearest(eye, players, FREEZE_LOOK_RANGE, m_Forward, FREEZE_LOOK_DOT, m_Actor);
  if (target)
  {
   look.LookAt(EUS_Codes.Eye(target), EUS_Codes.LOOK_PRIORITY, 1.5);
   m_LookingForward = false;
   return;
  }
  if (m_LookingForward) return;
  look.LookAt(eye + m_Forward * 10, EUS_Codes.LOOK_PRIORITY, 1.5);
  m_LookingForward = true;
 }

 protected void StartLoiter(float now)
 {
  int index = m_Code - EUS_Codes.ANIMATION;
  m_NextLoiter = now + 2;
  if (m_Controller.IsChangingStance()) return;
  if (m_Controller.GetStance() != ECharacterStance.STAND)
  {
   SCR_AIStanceHandling.SetStance(m_Controller, ECharacterStance.STAND);
   return;
  }
  ELoiteringType type = EUS_AnimationCatalog.Type(index);
  if (!m_Controller.CanPlayLoiterAnimation(type)) return;
  vector transform[4];
  m_Actor.GetWorldTransform(transform);
  m_LoiterAttempts++;
  m_NextLoiter = now + 6;
  // Owner-side vanilla entry; the command handler replicates it, including JIP.
  m_Controller.StartLoitering(null, type, EUS_AnimationCatalog.Holster(index), true, false, transform, true, EUS_AnimationCatalog.CreateData(index));
  // Log the first loiter and every genuine retry. A routine re-issue after a held
  // loiter (attempts reset to 0, so this is attempt 1 again) stays silent; Release
  // still logs the final failure.
  if (!m_LoiterLogged || m_LoiterAttempts > 1)
  {
   PrintFormat("[EUS] unit=%1 loiter=%2 attempt=%3", m_Actor, typename.EnumToString(ELoiteringType, type), m_LoiterAttempts);
   m_LoiterLogged = true;
  }
 }

 protected bool KeepLoiter(float now)
 {
  if (m_Controller.IsLoitering())
  {
   m_PendingSince = -1;
   if (m_LoiterSince < 0) m_LoiterSince = now;
   // A loiter that held for a while earns a fresh retry allowance.
   if (now - m_LoiterSince > 20) m_LoiterAttempts = 0;
   return true;
  }
  m_LoiterSince = -1;
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
   Release(string.Format("the animation could not be kept after %1 attempts", LOITER_ATTEMPTS));
   return false;
  }
  StartLoiter(now);
  return true;
 }

 // Native damage callback (server). Healing, regeneration and bleeding ticks
 // of an existing wound do not count as new damage.
 void OnDamage(BaseDamageContext damageContext)
 {
  if (!m_Bound || !damageContext || damageContext.damageValue <= 0) return;
  EDamageType type = damageContext.damageType;
  if (type == EDamageType.HEALING || type == EDamageType.REGENERATION || type == EDamageType.BLEEDING) return;
  Release("the unit took damage", true);
 }

 // Combat move requests while bound. Returns the replacement, or null to keep
 // the original. Never claims a cancelled movement succeeded.
 SCR_AICombatMoveRequestBase Filter(notnull SCR_AICombatMoveRequestBase request)
 {
  if (!m_Bound) return null;
  if (!IsOwnedActor())
  {
   Release("the unit died, left AI control or was removed");
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
   Release("a player took control of the unit");
   return;
  }
  CharacterInputContext input = controller.GetInputContext();
  if (input) input.SetMovement(0, vector.Zero);
 }

 void Release(string reason, bool fast = false)
 {
  if (!m_Bound) return;
  m_Bound = false;
  m_EndReason = reason;
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
  if (!reason.IsEmpty()) PrintFormat("[EUS] released unit=%1 script='%2' reason='%3'", m_Actor, EUS_Codes.Describe(m_Code), reason);
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
  if (controlled && possessed) possessed.Release("a player took control of the unit");
  super.OnControlledByPlayer(owner, controlled);
 }
}
