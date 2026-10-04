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
	protected ref EXPG_PostSpeedSetting m_SpeedSetting;
	protected EMovementType m_PreviousMovement;
	protected vector m_Position;
	protected vector m_LookDirection;

	bool Bind(SCR_ChimeraCharacter actor, vector position, vector lookDirection)
	{
		Release();
		if (!Replication.IsServer() || !actor || vector.DistanceSq(position, actor.GetOrigin()) > 1.0)
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
		if (!utility || !utility.m_CombatMoveState || utility.m_OwnerEntity != actor || !settings || !movement || utility.EXPG_GetPostControl() || utility.EXPG_GetPatrolControl())
		{
			return false;
		}

		m_Actor = actor;
		m_Agent = agent;
		m_Group = agent.GetParentGroup();
		m_Utility = utility;
		m_Settings = settings;
		m_Movement = movement;
		m_Position = position;
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
		return Tick();
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
		// Displacement is a failed post, never permission to snap a guard back.
		if (vector.DistanceSq(m_Position, m_Actor.GetOrigin()) > 2.25)
		{
			Release();
			return false;
		}
		if (m_Actor.GetCharacterController().IsUnconscious()) { return true; }
		m_Movement.SetMovementTypeWanted(EMovementType.IDLE);
		SCR_AIBehaviorBase behavior = m_Utility.GetCurrentBehavior();
		if (behavior && behavior.GetCause() == SCR_EAIBehaviorCause.SAFE && vector.DistanceSq(m_LookDirection, vector.Zero) > 0.01)
		{
			m_Utility.LookAt(m_Position + Vector(0, 1.5, 0) + m_LookDirection * 20.0, 2.0);
		}
		return true;
	}

	void Release()
	{
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
		m_Utility = null;
		m_Group = null;
		m_Agent = null;
		m_Actor = null;
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
