// One owned setting per guard. Native setting categories keep stance, weapon and
// look behavior independent. Priority 7000 is above the installed editor's 6000.
class EXPG_PostSpeedSetting : SCR_AICharacterMovementSpeedSettingBase
{
	static EXPG_PostSpeedSetting Create()
	{
		EXPG_PostSpeedSetting setting = new EXPG_PostSpeedSetting();
		setting.Init(SCR_EAISettingOrigin.SCENARIO, SCR_EAIBehaviorCause.ALWAYS);
		setting.m_iPriority = 7000;
		return setting;
	}

	override EMovementType GetSpeed(EMovementType desiredSpeed)
	{
		return EMovementType.IDLE;
	}
}

// Owned by the building record; no timers, spawning, teleporting or regrouping.
// Simulation caching retains this control and its exact native setting on the
// original actor. Release removes only this setting; it is idempotent.
class EXPG_PostControl
{
	protected SCR_ChimeraCharacter m_Actor;
	protected AIAgent m_Agent;
	protected AIGroup m_Group;
	protected SCR_AIUtilityComponent m_Utility;
	protected SCR_AICharacterSettingsComponent m_Settings;
	protected AICharacterMovementComponent m_Movement;
	protected SCR_CharacterControllerComponent m_Controller;
	protected ref EXPG_PostSpeedSetting m_SpeedSetting;
	protected EMovementType m_PreviousMovement;
	protected vector m_Position;
	protected vector m_LookDirection;
	// Knocked off his spot (blast, ragdoll, push, carry, a Game Master move): his
	// post became where he came to rest; the manager copies it once (TakeMoved).
	protected bool m_Moved;
	// Displaced more than 0.5 m: hold where he came to rest. Bind within 1.5 m.
	static const float DRIFT_SQ = 0.25;
	static const float BIND_SQ = 2.25;

	bool Bind(SCR_ChimeraCharacter actor, vector position, vector lookDirection)
	{
		Release();
		if (!Replication.IsServer() || !actor || vector.DistanceSq(position, actor.GetOrigin()) > BIND_SQ)
		{
			return false;
		}

		AIControlComponent control = actor.GetAIControlComponent();
		if (!control || !control.GetAIAgent())
		{
			return false;
		}

		AIAgent agent = control.GetAIAgent();
		SCR_AIUtilityComponent utility = SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent));
		SCR_AICharacterSettingsComponent settings = SCR_AICharacterSettingsComponent.Cast(agent.FindComponent(SCR_AICharacterSettingsComponent));
		AICharacterMovementComponent movement = AICharacterMovementComponent.Cast(actor.FindComponent(AICharacterMovementComponent));
		SCR_CharacterControllerComponent controller = SCR_CharacterControllerComponent.Cast(actor.GetCharacterController());
		if (!utility || !utility.m_CombatMoveState || utility.m_OwnerEntity != actor || !settings || !movement || !controller || controller.EXPG_GetPostControl() || utility.EXPG_GetPostControl() || utility.EXPG_GetPatrolControl())
		{
			return false;
		}

		m_Actor = actor;
		m_Agent = agent;
		m_Group = agent.GetParentGroup();
		m_Utility = utility;
		m_Settings = settings;
		m_Movement = movement;
		m_Controller = controller;
		m_Position = position;
		// Bound off his post (a restore, a re-bind after a carry): he holds where he
		// stands and is never snapped back.
		if (vector.DistanceSq(position, actor.GetOrigin()) > DRIFT_SQ)
		{
			m_Position = actor.GetOrigin();
			m_Moved = true;
		}
		m_LookDirection = lookDirection;
		if (vector.DistanceSq(lookDirection, vector.Zero) > 0.01)
		{
			m_LookDirection.Normalize();
		}
		m_PreviousMovement = movement.GetMovementTypeWanted();
		if (!IsOwnedActor())
		{
			Release();
			return false;
		}

		m_SpeedSetting = EXPG_PostSpeedSetting.Create();
		if (!m_Settings.AddCharacterSetting(m_SpeedSetting, false, false))
		{
			Release();
			return false;
		}
		m_Utility.EXPG_SetPostControl(this);
		m_Controller.EXPG_SetPostControl(this);
		if (!Tick()) { return false; }
		// Native idle/formation motion can bypass desired speed and input values.
		// The character's source-owned minimum cap also gates native locomotion.
		// Other owners' slowdowns remain intact when this post releases its entry.
		m_Actor.SetSpeedLimit(this, 0, true);
		return true;
	}

	bool IsOwnedActor()
	{
		if (!Replication.IsServer() || !m_Actor || !m_Agent || !m_Group || !m_Utility)
		{
			return false;
		}
		CharacterControllerComponent controller = m_Actor.GetCharacterController();
		RplComponent replication = RplComponent.Cast(m_Actor.FindComponent(RplComponent));
		if (!controller || controller.IsDead() || controller.IsPlayerControlled() || m_Actor.IsInVehicle())
		{
			return false;
		}
		if (replication && replication.IsProxy())
		{
			return false;
		}
		AIControlComponent control = m_Actor.GetAIControlComponent();
		return control && control.GetAIAgent() == m_Agent && m_Agent.GetControlledEntity() == m_Actor && m_Agent.GetParentGroup() == m_Group;
	}

	bool Tick()
	{
		if (!m_SpeedSetting || !IsOwnedActor() || !m_Settings || !m_Movement || m_Utility.EXPG_GetPostControl() != this)
		{
			Release();
			return false;
		}
		m_Actor.SetSpeedLimit(this, 0, true);
		m_Movement.SetMovementTypeWanted(EMovementType.IDLE);
		if (m_Controller.IsUnconscious() || Ragdolled())
		{
			return true;
		}
		// Displacement (blast, ragdoll, push, carry, a Game Master move) is never a
		// release and never a snap back: once he is still, his post is where he came
		// to rest. Only a lost ownership ends this control.
		if (vector.DistanceSq(m_Position, m_Actor.GetOrigin()) > DRIFT_SQ && Still())
		{
			m_Position = m_Actor.GetOrigin();
			m_Moved = true;
		}
		SCR_AIBehaviorBase behavior = m_Utility.GetCurrentBehavior();
		if (behavior && behavior.GetCause() == SCR_EAIBehaviorCause.SAFE && vector.DistanceSq(m_LookDirection, vector.Zero) > 0.01)
		{
			m_Utility.LookAt(m_Position + Vector(0, 1.5, 0) + m_LookDirection * 20.0, 2.0);
		}
		return true;
	}

	protected bool Ragdolled()
	{
		CharacterAnimationComponent animator = m_Controller.GetAnimationComponent();
		if (!animator)
		{
			return false;
		}
		return animator.IsRagdollActive();
	}

	// Below 0.2 m/s: he came to rest (not falling, sliding or being carried along).
	protected bool Still()
	{
		return vector.DistanceSq(m_Controller.GetVelocity(), vector.Zero) < 0.04;
	}

	// The manager copies a changed post (where he came to rest) once per change.
	bool TakeMoved(out vector anchor)
	{
		anchor = m_Position;
		bool moved = m_Moved;
		m_Moved = false;
		return moved;
	}

	// The manager refused his resting spot and sends him back (a teleport that lands
	// later): hold this post again. A move still pending shows up as a new
	// displacement, which the manager ignores while he is being sent back.
	void Return(vector position)
	{
		m_Position = position;
		m_Moved = false;
	}

	void Release()
	{
		if (m_Actor) { m_Actor.SetSpeedLimit(this, 1); }
		if (m_Controller && m_Controller.EXPG_GetPostControl() == this) { m_Controller.EXPG_SetPostControl(null); }
		if (m_Utility && m_Utility.EXPG_GetPostControl() == this)
		{
			m_Utility.EXPG_SetPostControl(null);
		}
		if (m_Settings && m_SpeedSetting)
		{
			m_Settings.RemoveSetting(m_SpeedSetting);
			// Undo only our still-active movement value, respecting remaining settings.
			if (m_Movement && m_Movement.GetMovementTypeWanted() == EMovementType.IDLE)
			{
				EMovementType speed = m_PreviousMovement;
				SCR_AICharacterMovementSpeedSettingBase setting = SCR_AICharacterMovementSpeedSettingBase.Cast(m_Settings.GetCurrentSetting(SCR_AICharacterMovementSpeedSettingBase));
				if (setting)
				{
					speed = setting.GetSpeed(speed);
				}
				m_Movement.SetMovementTypeWanted(speed);
			}
		}
		m_SpeedSetting = null;
		m_Settings = null;
		m_Movement = null;
		m_Controller = null;
		m_Utility = null;
		m_Group = null;
		m_Agent = null;
		m_Actor = null;
		m_Moved = false;
	}

	void ~EXPG_PostControl()
	{
		Release();
	}
}

// Bind the request filter to exactly this agent's native combat-move state.
modded class SCR_AIUtilityComponent
{
	protected EXPG_PostControl m_EXPG_PostControl;
	protected EXPG_PatrolControl m_EXPG_PatrolControl;

	EXPG_PatrolControl EXPG_GetPatrolControl() { return m_EXPG_PatrolControl; }
	void EXPG_SetPatrolControl(EXPG_PatrolControl patrol)
	{
		m_EXPG_PatrolControl = patrol;
		if (m_CombatMoveState) { m_CombatMoveState.EXPG_SetPatrolControl(patrol); }
	}

	EXPG_PostControl EXPG_GetPostControl()
	{
		return m_EXPG_PostControl;
	}

	void EXPG_SetPostControl(EXPG_PostControl post)
	{
		m_EXPG_PostControl = post;
		if (m_CombatMoveState)
		{
			m_CombatMoveState.EXPG_SetPostControl(post);
		}
	}
}

// Keep native combat aiming and stance processing alive. A translation request
// becomes its native end stance at the existing position. Never claim that its
// movement completed, copy movement callbacks, or alter another agent's requests.
modded class SCR_AICombatMoveState
{
	protected EXPG_PostControl m_EXPG_PostControl;
	protected EXPG_PatrolControl m_EXPG_PatrolControl;

	void EXPG_SetPatrolControl(EXPG_PatrolControl patrol)
	{
		m_EXPG_PatrolControl = patrol;
		SCR_AICombatMoveRequest_Move move = SCR_AICombatMoveRequest_Move.Cast(GetRequest());
		if (patrol && move) { ApplyNewRequest(move); }
	}

	void EXPG_SetPostControl(EXPG_PostControl post)
	{
		m_EXPG_PostControl = post;
		SCR_AICombatMoveRequest_Move move = SCR_AICombatMoveRequest_Move.Cast(GetRequest());
		if (post && move)
		{
			ApplyNewRequest(move);
		}
	}

	override void ApplyNewRequest(notnull SCR_AICombatMoveRequestBase request)
	{
		if (m_EXPG_PostControl && !m_EXPG_PostControl.IsOwnedActor())
		{
			m_EXPG_PostControl.Release();
		}
		if (m_EXPG_PatrolControl && !m_EXPG_PatrolControl.IsOwnedActor()) { m_EXPG_PatrolControl.Release(); }
		SCR_AICombatMoveRequest_Move move = SCR_AICombatMoveRequest_Move.Cast(request);
		if ((m_EXPG_PostControl || m_EXPG_PatrolControl) && move)
		{
			SCR_AICombatMoveRequest_ChangeStance stance = new SCR_AICombatMoveRequest_ChangeStance();
			stance.m_eStance = move.m_eStanceEnd;
			stance.m_eReason = move.m_eReason;
			stance.m_eUnitType = move.m_eUnitType;
			stance.m_f_UserTimer_s = move.m_f_UserTimer_s;
			stance.m_bAimAtTarget = true;
			stance.m_bAimAtTargetEnd = true;
			move.m_eState = SCR_EAICombatMoveRequestState.CANCELED;
			super.ApplyNewRequest(stance);
			return;
		}
		super.ApplyNewRequest(request);
	}
}
