// Source candidate: native callback timing/containment acceptance is NOT RUN.
// Request one adjacent verified edge. Keep IDLE until the actual native path is
// inspected. The movement-input guard is supplemental; native testing must prove
// it runs before AI translation, including stairs, impulses and low server FPS.
class EXPG_PatrolSpeedSetting : SCR_AICharacterMovementSpeedSettingBase
{
	bool Blocked = true;

	static EXPG_PatrolSpeedSetting Create()
	{
		EXPG_PatrolSpeedSetting setting = new EXPG_PatrolSpeedSetting();
		setting.Init(SCR_EAISettingOrigin.SCENARIO, SCR_EAIBehaviorCause.ALWAYS);
		setting.m_iPriority = 7000;
		return setting;
	}

	override EMovementType GetSpeed(EMovementType desiredSpeed)
	{
		if (Blocked) { return EMovementType.IDLE; }
		return Math.ClampInt(desiredSpeed, EMovementType.IDLE, EMovementType.WALK);
	}
}

class EXPG_PatrolControl
{
	protected SCR_ChimeraCharacter m_Actor;
	protected AIAgent m_Agent;
	protected AIGroup m_Group;
	protected SCR_CharacterControllerComponent m_Controller;
	protected SCR_AIUtilityComponent m_Utility;
	protected SCR_AICharacterSettingsComponent m_Settings;
	protected AICharacterMovementComponent m_Movement;
	protected ref EXPG_PatrolSpeedSetting m_Speed;
	protected ref SCR_AIMoveIndividuallyBehavior m_Move;
	protected ref EXPG_BuildingPlan m_Plan;
	protected int m_Node;
	protected int m_Target = -1;
	protected int m_Choice;
	protected float m_RetryAt;
	protected float m_EdgeDeadline;
	protected vector m_From;
	protected vector m_To;
	protected EMovementType m_PreviousSpeed;
	protected bool m_Reserved;

	bool Start(SCR_ChimeraCharacter actor, EXPG_BuildingPlan plan, int initialNode)
	{
		Release();
		if (!Replication.IsServer() || !actor || !plan || !plan.Done || plan.Error != string.Empty || !plan.Valid()) { return false; }
		if (initialNode < 0 || initialNode >= plan.Nodes.Count() || !plan.Nodes[initialNode].Reachable || plan.Nodes[initialNode].Links.IsEmpty()) { return false; }
		if (vector.DistanceSq(actor.GetOrigin(), plan.Nodes[initialNode].Position) > 0.09) { return false; }
		AIControlComponent control = actor.GetAIControlComponent();
		if (!control || !control.GetAIAgent()) { return false; }
		m_Actor = actor;
		m_Agent = control.GetAIAgent();
		m_Group = m_Agent.GetParentGroup();
		m_Utility = SCR_AIUtilityComponent.Cast(m_Agent.FindComponent(SCR_AIUtilityComponent));
		m_Settings = SCR_AICharacterSettingsComponent.Cast(m_Agent.FindComponent(SCR_AICharacterSettingsComponent));
		m_Movement = AICharacterMovementComponent.Cast(actor.FindComponent(AICharacterMovementComponent));
		m_Controller = SCR_CharacterControllerComponent.Cast(actor.GetCharacterController());
		m_Plan = plan;
		m_Node = initialNode;
		m_Choice = 0;
		m_RetryAt = 0;
		if (!IsOwnedActor() || !m_Utility.m_CombatMoveState || m_Utility.m_OwnerEntity != actor || !m_Settings || !m_Movement || m_Utility.EXPG_GetPostControl() || m_Utility.EXPG_GetPatrolControl() || m_Controller.EXPG_GetPatrolControl() || m_Movement.EXPG_GetPatrolControl())
		{
			Release();
			return false;
		}
		m_PreviousSpeed = m_Movement.GetMovementTypeWanted();
		if (!m_Plan.ReserveNode(actor, initialNode)) { Release(); return false; }
		m_Reserved = true;
		m_Speed = EXPG_PatrolSpeedSetting.Create();
		if (!m_Settings.AddCharacterSetting(m_Speed, false, false)) { Release(); return false; }
		m_Utility.EXPG_SetPatrolControl(this);
		m_Controller.EXPG_SetPatrolControl(this);
		m_Movement.EXPG_SetPatrolControl(this);
		return Tick();
	}

	bool IsOwnedActor()
	{
		if (!Replication.IsServer() || !m_Actor || !m_Agent || !m_Group || !m_Utility || !m_Controller || !m_Plan || !m_Plan.Valid()) { return false; }
		if (m_Controller.IsDead() || m_Controller.IsPlayerControlled() || m_Actor.IsInVehicle()) { return false; }
		RplComponent replication = RplComponent.Cast(m_Actor.FindComponent(RplComponent));
		if (replication && replication.IsProxy()) { return false; }
		AIControlComponent control = m_Actor.GetAIControlComponent();
		return control && control.GetAIAgent() == m_Agent && m_Agent.GetControlledEntity() == m_Actor && m_Agent.GetParentGroup() == m_Group;
	}

	bool Tick()
	{
		if (!m_Speed || !IsOwnedActor()) { Release(); return false; }
		if (m_Controller.IsUnconscious()) { Block(); return true; }
		float now = GetGame().GetWorld().GetWorldTime();
		if (m_Target >= 0 && !InCorridor(m_Actor.GetOrigin())) { Block(); return true; }
		if (m_Move && !m_Speed.Blocked && vector.DistanceSq(m_Actor.GetOrigin(), m_To) <= 0.0225)
		{
			m_Node = m_Target;
			Block();
			m_Target = -1;
		}
		if (m_Move && now > m_EdgeDeadline) { Block(); }
		if (m_Move) { return true; }
		if (now < m_RetryAt) { return true; }
		m_RetryAt = now + 2000;
		array<int> links = m_Plan.Nodes[m_Node].Links;
		if (links.IsEmpty()) { return true; }
		int next = m_Target;
		// A stopped actor mid-edge retains that whole edge and retries only its
		// destination. It cannot cut diagonally to another neighbor of the old node.
		if (next < 0 || AtNode(m_Node))
		{
			next = links[m_Choice % links.Count()];
			m_Choice++;
		}
		vector from = m_Actor.GetOrigin();
		vector to = m_Plan.Nodes[next].Position;
		if (!m_Plan.Nodes[next].Reachable || !m_Plan.Inside(from, 0.25) || !m_Plan.Inside(to, 0.25) || !m_Plan.ClearBody(from, to, m_Actor)) { return true; }
		if (!m_Plan.TryReserveEdge(m_Actor, m_Node, next)) { return true; }
		m_Target = next;
		m_From = from;
		m_To = to;
		m_Speed.Blocked = true;
		m_Movement.SetMovementTypeWanted(EMovementType.IDLE);
		m_EdgeDeadline = now + 10000;
		m_Move = new SCR_AIMoveIndividuallyBehavior(m_Utility, null, m_To, radius: 0.1);
		m_Utility.AddAction(m_Move);
		return true;
	}

	void OnPathSet()
	{
		if (!m_Move || !m_Speed || !IsOwnedActor() || !InCorridor(m_Actor.GetOrigin())) { Block(); return; }
		if (m_Controller.IsUnconscious()) { Block(); return; }
		array<vector> path = {};
		m_Movement.GetCurrentPath(path);
		if (path.IsEmpty() || path.Count() > 64 || vector.DistanceSq(path[path.Count() - 1], m_To) > 0.09) { Block(); return; }
		foreach (vector point : path)
		{
			if (!InCorridor(point)) { Block(); return; }
		}
		m_Speed.Blocked = false;
		m_Movement.SetMovementTypeWanted(EMovementType.WALK);
	}

	protected void Block()
	{
		if (m_Speed) { m_Speed.Blocked = true; }
		if (m_Movement && m_Speed) { m_Movement.SetMovementTypeWanted(EMovementType.IDLE); }
		if (m_Move) { m_Move.Fail(); m_Move = null; }
		if (m_Reserved)
		{
			int node = m_Target;
			if (!AtNode(node)) { node = m_Node; }
			if (AtNode(node) && m_Plan.ReserveNode(m_Actor, node))
			{
				m_Node = node;
				m_Target = -1;
			}
		}
	}

	void InvalidatePath() { Block(); }

	protected bool AtNode(int node)
	{
		return m_Actor && m_Plan && node >= 0 && node < m_Plan.Nodes.Count() && m_Plan.Nodes[node].Reachable && vector.DistanceSq(m_Actor.GetOrigin(), m_Plan.Nodes[node].Position) <= 0.0225;
	}

	// Scalar checks only in the movement callback; no traces or world queries.
	bool AllowInput(float dt, vector localDirection, out float inputScale)
	{
		inputScale = 1;
		if (!m_Speed || !IsOwnedActor()) { Release(); return true; }
		if (m_Controller.IsUnconscious()) { Block(); return false; }
		if (m_Speed.Blocked || m_Target < 0 || dt <= 0 || dt > 0.25) { return false; }
		// Arrival is checked before projection, even between shared scheduler ticks.
		if (AtNode(m_Target)) { Block(); return false; }
		vector pos = m_Actor.GetOrigin();
		vector direction = m_Actor.VectorToParent(localDirection);
		if (vector.DistanceSq(direction, vector.Zero) > 0.01) { direction.Normalize(); }
		// Reduce the actual requested input along with the candidate horizon; simply
		// clipping the checked point would leave unsafe full-speed input unchanged.
		// Verify input scaling, WALK speed and braking in the native fixture.
		float step = 3.0 * dt;
		inputScale = Math.Min(1.0, vector.DistanceXZ(pos, m_To) / step);
		vector projected = pos + direction * (step * inputScale);
		vector inertial = pos + m_Controller.GetVelocity() * dt;
		if (!InCorridor(pos) || !InCorridor(projected) || !InCorridor(inertial)) { Block(); return false; }
		return true;
	}

	bool InCorridor(vector point)
	{
		return m_Target >= 0 && m_Plan && m_Plan.Inside(point, 0.25) && InEdgeCorridor(point, m_From, m_To);
	}

	static bool InEdgeCorridor(vector point, vector from, vector to)
	{
		vector flat = to - from;
		flat[1] = 0;
		float lengthSq = vector.Dot(flat, flat);
		if (lengthSq < 0.0001) { return false; }
		float t = Math.Clamp(vector.Dot(point - from, flat) / lengthSq, 0, 1);
		vector nearest = vector.Lerp(from, to, t);
		return vector.DistanceXZ(point, nearest) <= 0.15 && Math.AbsFloat(point[1] - nearest[1]) <= 0.35;
	}

	void Release()
	{
		Block();
		if (m_Reserved && m_Plan) { m_Plan.ReleaseReservation(m_Actor); }
		m_Reserved = false;
		if (m_Utility && m_Utility.EXPG_GetPatrolControl() == this) { m_Utility.EXPG_SetPatrolControl(null); }
		if (m_Controller && m_Controller.EXPG_GetPatrolControl() == this) { m_Controller.EXPG_SetPatrolControl(null); }
		if (m_Movement && m_Movement.EXPG_GetPatrolControl() == this) { m_Movement.EXPG_SetPatrolControl(null); }
		if (m_Settings && m_Speed) { m_Settings.RemoveSetting(m_Speed); }
		if (m_Movement && m_Speed) { m_Movement.SetMovementTypeWanted(m_PreviousSpeed); }
		m_Speed = null;
		m_Settings = null;
		m_Movement = null;
		m_Controller = null;
		m_Utility = null;
		m_Actor = null;
		m_Agent = null;
		m_Group = null;
		m_Plan = null;
		m_Target = -1;
	}

	void ~EXPG_PatrolControl() { Release(); }
}

modded class AICharacterMovementComponent
{
	protected EXPG_PatrolControl m_EXPG_PatrolControl;
	EXPG_PatrolControl EXPG_GetPatrolControl() { return m_EXPG_PatrolControl; }
	void EXPG_SetPatrolControl(EXPG_PatrolControl patrol) { m_EXPG_PatrolControl = patrol; }
	override void OnPathSet()
	{
		super.OnPathSet();
		if (m_EXPG_PatrolControl) { m_EXPG_PatrolControl.OnPathSet(); }
	}
}

modded class SCR_CharacterControllerComponent
{
	protected EXPG_PatrolControl m_EXPG_PatrolControl;
	EXPG_PatrolControl EXPG_GetPatrolControl() { return m_EXPG_PatrolControl; }
	void EXPG_SetPatrolControl(EXPG_PatrolControl patrol) { m_EXPG_PatrolControl = patrol; }
	protected override void OnPrepareControls(IEntity owner, ActionManager am, float dt, bool player)
	{
		super.OnPrepareControls(owner, am, dt, player);
		if (!player && m_EXPG_PatrolControl)
		{
			CharacterInputContext input = GetInputContext();
			if (!input) { m_EXPG_PatrolControl.InvalidatePath(); return; }
			float speed;
			float inputScale;
			vector direction;
			input.GetMovement(speed, direction);
			if (!m_EXPG_PatrolControl.AllowInput(dt, direction, inputScale)) { input.SetMovement(0, vector.Zero); }
			else if (inputScale < 1) { input.SetMovement(speed * inputScale, direction); }
		}
	}
}
