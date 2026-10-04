// Native fixture: compile into an isolated test addon, then call Run with a live,
// server-owned infantry actor already standing at a verified post. NOT RUN.
// This fixture checks ownership and teardown, not sustained combat/animation.
class EXPG_PostControlTest
{
	static bool Run(SCR_ChimeraCharacter actor)
	{
		if (!actor || !Replication.IsServer())
		{
			return false;
		}

		AIControlComponent control = actor.GetAIControlComponent();
		if (!control || !control.GetAIAgent())
		{
			return false;
		}

		AIAgent agent = control.GetAIAgent();
		AIGroup group = agent.GetParentGroup();
		SCR_AICharacterSettingsComponent settings = SCR_AICharacterSettingsComponent.Cast(agent.FindComponent(SCR_AICharacterSettingsComponent));
		if (!group || !settings)
		{
			return false;
		}

		vector origin = actor.GetOrigin();
		ref EXPG_PostControl post = new EXPG_PostControl();
		bool ok = Check(!post.Bind(null, origin, "0 0 1"), "reject missing actor");
		if (!Check(post.Bind(actor, origin, "0 0 1"), "bind existing actor"))
		{
			return false;
		}
		ok = Check(post.Tick(), "bound actor remains valid") && ok;
		SCR_AIUtilityComponent utility = SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent));
		SCR_AICombatMoveState moveState = utility.m_CombatMoveState;
		SCR_AICombatMoveRequest_Move move = new SCR_AICombatMoveRequest_Move();
		move.m_eStanceEnd = ECharacterStance.CROUCH;
		moveState.ApplyNewRequest(move);
		SCR_AICombatMoveRequest_ChangeStance converted = SCR_AICombatMoveRequest_ChangeStance.Cast(moveState.GetRequest());
		ok = Check(converted && converted.m_eStance == ECharacterStance.CROUCH && converted.m_bAimAtTarget, "move becomes stationary crouch with aim") && ok;
		ok = Check(move.m_eState == SCR_EAICombatMoveRequestState.CANCELED, "movement never reports completed") && ok;
		SCR_AICombatMoveRequest_ChangeStance stance = new SCR_AICombatMoveRequest_ChangeStance();
		moveState.ApplyNewRequest(stance);
		ok = Check(moveState.GetRequest() == stance, "native stance request preserved") && ok;

		// A later independent editor setting must survive release unchanged.
		SCR_AICharacterMovementSpeedSetting foreign = SCR_AICharacterMovementSpeedSetting.Create(SCR_EAISettingOrigin.EDITOR, SCR_EAIBehaviorCause.ALWAYS, EMovementType.WALK);
		settings.AddCharacterSetting(foreign, false, false);
		SCR_AICharacterMovementSpeedSettingBase active = SCR_AICharacterMovementSpeedSettingBase.Cast(settings.GetCurrentSetting(SCR_AICharacterMovementSpeedSettingBase, SCR_EAIBehaviorCause.COMBAT));
		ok = Check(active && active.GetSpeed(EMovementType.RUN) == EMovementType.IDLE, "hold applies during combat") && ok;

		ref EXPG_PostControl duplicate = new EXPG_PostControl();
		ok = Check(!duplicate.Bind(actor, origin, "0 0 1"), "reject duplicate owner") && ok;
		ok = Check(agent.GetParentGroup() == group, "membership unchanged") && ok;
		ok = Check(vector.DistanceSq(origin, actor.GetOrigin()) < 0.0001, "no teleport") && ok;
		post.Release();
		post.Release();
		ok = Check(!post.Tick(), "released controller stays released") && ok;
		SCR_AICombatMoveRequest_Move releasedMove = new SCR_AICombatMoveRequest_Move();
		moveState.ApplyNewRequest(releasedMove);
		ok = Check(moveState.GetRequest() == releasedMove, "movement passes through after release") && ok;
		moveState.CancelRequest();
		array<SCR_AISettingBase> remaining = {};
		settings.GetAllSettings(remaining);
		ok = Check(remaining.Contains(foreign), "foreign setting preserved") && ok;
		foreach (SCR_AISettingBase setting : remaining)
		{
			ok = Check(!EXPG_PostSpeedSetting.Cast(setting), "owned setting removed") && ok;
		}
		settings.RemoveSetting(foreign);
		ok = Check(post.Bind(actor, origin, "0 0 1"), "rebind after pause") && ok;
		post.Release();
		return ok;
	}

	protected static bool Check(bool condition, string description)
	{
		if (!condition)
		{
			Print("EXPG_PostControlTest FAILED: " + description, LogLevel.ERROR);
		}
		return condition;
	}
}
