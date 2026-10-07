// Interior patrol. Source candidate: native callback timing/containment acceptance
// is NOT RUN. A patroller dwells 10-30 s at a claimed stop, then walks to another
// free stop of the same indoor area; on alarm the garrison sends him to a free
// window or watch point (EXPG_GarrisonManager.ServiceAlert). Keep IDLE until the
// actual native path is inspected: every path point must stay on the indoor floor.
// The movement-input guard is supplemental; native testing must prove it runs
// before AI translation, including stairs, impulses and low server FPS.
class EXPG_PatrolSpeedSetting : SCR_AICharacterMovementSpeedSettingBase
{
	bool Blocked = true;
	// Walk on patrol, run to an alarm post.
	EMovementType MaxSpeed = EMovementType.WALK;

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
		return MaxSpeed;
	}
}

// Logical route only; the Full adapter owns the actor's exact world transform.
// NodeIndex is the claimed stop; Target stays -1 (walks are never resumed).
class EXPG_PatrolState
{
	int NodeIndex;
	int Target = -1;
	int Choice;
	vector From;
	vector To;
}

class EXPG_PatrolControl
{
	static const int STATE_DWELL = 0;
	static const int STATE_LEG = 1;
	static const int STATE_ALERT_MOVE = 2;
	static const int STATE_HOLD = 3;
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
	// The single claim: current stop, or destination while walking.
	protected int m_Node = -1;
	// The stop a patrol leg started from, and whether the walk is already a
	// walk back to it after a failed leg (at most one per leg).
	protected int m_From = -1;
	protected bool m_Backtrack;
	protected int m_State;
	// Holding (or running to) a window during an alarm.
	protected bool m_Window;
	// Cache sleep pending: no new walks (EXPG_GarrisonManager.PatrolsSettled).
	protected bool m_Settle;
	protected bool m_Reserved;
	// World time in milliseconds.
	protected float m_DwellUntil;
	protected float m_MoveDeadline;
	protected float m_RetryAt;
	// Metres per second for the projected step of the input guard.
	protected float m_StepRate = 3.0;
	protected vector m_Dest;
	protected vector m_Look;
	protected EMovementType m_PreviousSpeed;
	// Last inspected native path (count, middle and end point) and its verdict.
	protected int m_PathCount = -1;
	protected vector m_PathEnd;
	protected vector m_PathMid;
	protected bool m_PathOk;
	protected ref array<vector> m_Path = {};
	protected ref array<int> m_Visited = {};
	protected ref array<int> m_Skipped = {};
	protected ref array<float> m_SkipUntil = {};

	bool Start(SCR_ChimeraCharacter actor, EXPG_BuildingPlan plan, int initialNode, EXPG_PatrolState saved = null)
	{
		Release();
		if (!Replication.IsServer() || !actor || !plan || !plan.Done || plan.Error != string.Empty || !plan.Valid()) { return false; }
		if (initialNode < 0 || initialNode >= plan.Nodes.Count() || !plan.Nodes[initialNode].Reachable) { return false; }
		if (saved && saved.NodeIndex != initialNode) { return false; }
		// Placed or restored on his stop. One who stopped short of it claims the
		// nearest free stop where he stands instead; nobody is teleported.
		int claim = initialNode;
		vector origin = actor.GetOrigin();
		vector stop = plan.Nodes[initialNode].Position;
		if (vector.DistanceXZ(origin, stop) > 1.5 || Math.AbsFloat(origin[1] - stop[1]) > 1.0) { claim = plan.NearestNode(origin, 1.0, true); }
		if (claim < 0) { return false; }
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
		m_RetryAt = 0;
		if (!IsOwnedActor() || !m_Utility.m_CombatMoveState || m_Utility.m_OwnerEntity != actor || !m_Settings || !m_Movement || m_Utility.EXPG_GetPostControl() || m_Utility.EXPG_GetPatrolControl() || m_Controller.EXPG_GetPatrolControl())
		{
			Release();
			return false;
		}
		// The checked claim first; a plain reservation keeps a restored survivor
		// whose stop a passer-by briefly overlaps (otherwise the squad is released).
		if (!m_Plan.TryClaimStop(actor, claim) && !m_Plan.ReserveNode(actor, claim)) { Release(); return false; }
		m_Reserved = true;
		m_Node = claim;
		m_State = STATE_DWELL;
		m_Look = m_Plan.Nodes[claim].WatchLook;
		m_DwellUntil = GetGame().GetWorld().GetWorldTime() + Math.RandomFloat(10000, 30000);
		m_PreviousSpeed = m_Movement.GetMovementTypeWanted();
		m_Speed = EXPG_PatrolSpeedSetting.Create();
		if (!m_Settings.AddCharacterSetting(m_Speed, false, false)) { Release(); return false; }
		m_Utility.EXPG_SetPatrolControl(this);
		m_Controller.EXPG_SetPatrolControl(this);
		HoldForPath();
		return Tick();
	}

	// The claimed stop only (Target -1): Full restore wakes him there.
	EXPG_PatrolState CaptureState()
	{
		if (!IsOwnedActor() || m_Node < 0) { return null; }
		EXPG_PatrolState saved = new EXPG_PatrolState();
		saved.NodeIndex = m_Node;
		saved.Target = -1;
		saved.Choice = m_State;
		saved.From = m_Actor.GetOrigin();
		saved.To = m_Plan.Nodes[m_Node].Position;
		return saved;
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
		if (m_Controller.IsUnconscious())
		{
			if (m_Move) { Block(); }
			return true;
		}
		float now = GetGame().GetWorld().GetWorldTime();
		if (m_Move)
		{
			if (AtPoint(m_Dest)) { Arrive(now); }
			else if (now > m_MoveDeadline || !m_Plan.Inside(m_Actor.GetOrigin(), 0.25)) { Fail(now); }
			else { InspectCurrentPath(); }
			return true;
		}
		LookOut();
		if (m_State != STATE_DWELL || m_Settle || now < m_DwellUntil) { return true; }
		StartLeg(now);
		return true;
	}

	// A free stop of the same indoor area (EXPG_BuildingPlan.ClaimRoamStop), not
	// one of the last three visited nor one that failed in the last two minutes.
	protected void StartLeg(float now)
	{
		array<int> avoid = {};
		for (int i = m_Skipped.Count() - 1; i >= 0; i--)
		{
			if (now >= m_SkipUntil[i])
			{
				m_Skipped.RemoveOrdered(i);
				m_SkipUntil.RemoveOrdered(i);
				continue;
			}
			avoid.Insert(m_Skipped[i]);
		}
		array<int> recent = {};
		recent.Copy(avoid);
		recent.InsertAll(m_Visited);
		int destination = m_Plan.ClaimRoamStop(m_Actor, m_Node, recent);
		// A small area: revisiting is better than standing still.
		if (destination < 0) { destination = m_Plan.ClaimRoamStop(m_Actor, m_Node, avoid); }
		if (destination < 0)
		{
			m_DwellUntil = now + Math.RandomFloat(10000, 30000);
			return;
		}
		int hops = m_Plan.Hops(destination);
		m_From = m_Node;
		m_Backtrack = false;
		m_Node = destination;
		BeginMove(m_Plan.Nodes[destination].Position, EMovementType.WALK, SCR_AIActionBase.PRIORITY_LEVEL_NORMAL, Math.Max(15000, hops * 1500));
		m_State = STATE_LEG;
	}

	// Patrol legs walk at normal priority. Alarm moves run at player level, above
	// the native combat behaviours that would otherwise pre-empt them.
	protected void BeginMove(vector destination, EMovementType speed, float level, float duration)
	{
		StopMoving();
		m_Dest = destination;
		m_Speed.MaxSpeed = speed;
		m_StepRate = 3.0;
		if (speed == EMovementType.RUN) { m_StepRate = 5.0; }
		m_MoveDeadline = GetGame().GetWorld().GetWorldTime() + duration;
		m_Move = new SCR_AIMoveIndividuallyBehavior(m_Utility, null, destination, SCR_AIActionBase.PRIORITY_BEHAVIOR_MOVE_INDIVIDUALLY, level, null, 0.25);
		m_Utility.AddAction(m_Move);
	}

	protected void Arrive(float now)
	{
		StopMoving();
		if (m_State == STATE_ALERT_MOVE)
		{
			m_State = STATE_HOLD;
			return;
		}
		m_State = STATE_DWELL;
		m_DwellUntil = now + Math.RandomFloat(10000, 30000);
		m_Visited.Insert(m_Node);
		if (m_Visited.Count() > 3) { m_Visited.RemoveOrdered(0); }
		m_Look = m_Plan.Nodes[m_Node].WatchLook;
	}

	// The walk left the building, lost its path or ran out of time: stop where he
	// is, keep a claim near him and try another destination later.
	protected void Fail(float now)
	{
		int failed = m_Node;
		Block();
		if (m_State == STATE_ALERT_MOVE || m_State == STATE_HOLD)
		{
			m_State = STATE_HOLD;
			m_Window = false;
			m_RetryAt = now;
			return;
		}
		m_Skipped.Insert(failed);
		m_SkipUntil.Insert(now + 120000);
		// Stopped short between stops: he stands on a stop, never in a hallway
		// beside another guard. Once per leg he steps onto the free stop claimed
		// near him, or else walks back to the stop the leg started from.
		if (!m_Backtrack && m_Actor && !AtPoint(m_Plan.Nodes[m_Node].Position))
		{
			int target = m_Node;
			if (m_Node == failed)
			{
				target = -1;
				if (m_From >= 0 && m_From != failed && m_Plan.TryClaimStop(m_Actor, m_From)) { target = m_From; }
			}
			if (target >= 0)
			{
				m_Node = target;
				m_Backtrack = true;
				BeginMove(m_Plan.Nodes[target].Position, EMovementType.WALK, SCR_AIActionBase.PRIORITY_LEVEL_NORMAL, 15000);
				m_State = STATE_LEG;
				return;
			}
		}
		m_State = STATE_DWELL;
		m_DwellUntil = now + Math.RandomFloat(5000, 10000);
		m_Look = m_Plan.Nodes[m_Node].WatchLook;
	}

	// Native looking stays free in combat; at rest he watches his hallway or post.
	protected void LookOut()
	{
		if (vector.DistanceSq(m_Look, vector.Zero) < 0.01) { return; }
		SCR_AIBehaviorBase behavior = m_Utility.GetCurrentBehavior();
		if (behavior && behavior.GetCause() == SCR_EAIBehaviorCause.SAFE) { m_Utility.LookAt(m_Actor.GetOrigin() + Vector(0, 1.5, 0) + m_Look * 20.0, 2.0); }
	}

	// Called from the script character input callback before accepting translation,
	// with shared Tick as secondary polling. Engine movement classes cannot be modded.
	// A new native path (its count, middle or end point changed) is inspected once:
	// every point after the first and the line between points (0.5 m samples, at
	// most 128) must stay on the indoor floor, and no point may pass a parked post.
	// The path's end need not match the stop, so native avoidance and the door
	// step-aside keep working. Until a path is admitted the cap holds him.
	bool InspectCurrentPath()
	{
		if (!m_Move || !m_Speed || !IsOwnedActor()) { StopMoving(); return false; }
		if (m_Controller.IsUnconscious()) { StopMoving(); return false; }
		m_Path.Clear();
		m_Movement.GetCurrentPath(m_Path);
		int count = m_Path.Count();
		if (count == 0)
		{
			HoldForPath();
			return false;
		}
		vector end = m_Path[count - 1];
		vector middle = m_Path[count / 2];
		if (count != m_PathCount || vector.DistanceSq(end, m_PathEnd) > 0.01 || vector.DistanceSq(middle, m_PathMid) > 0.01)
		{
			m_PathCount = count;
			m_PathEnd = end;
			m_PathMid = middle;
			m_PathOk = PathInside();
		}
		if (!m_PathOk)
		{
			// A path to another place can linger until ours is planned: hold and
			// wait for it (the deadline bounds the wait); ours failing ends the walk.
			if (vector.DistanceXZ(end, m_Dest) > 1.0)
			{
				HoldForPath();
				return false;
			}
			Fail(GetGame().GetWorld().GetWorldTime());
			return false;
		}
		if (m_Speed.Blocked)
		{
			m_Speed.Blocked = false;
			m_Movement.SetMovementTypeWanted(m_Speed.MaxSpeed);
			// Admitted indoor path: remove only this patrol's native cap.
			m_Actor.SetSpeedLimit(this, 1);
		}
		return true;
	}

	// Line samples within 1 m of where he stands are not checked: one who drifted
	// off the floor (a door frame) must be able to walk back onto it.
	protected bool PathInside()
	{
		if (m_Path.Count() > 64) { return false; }
		vector here = m_Path[0];
		if (m_Actor) { here = m_Actor.GetOrigin(); }
		int samples = 0;
		for (int i = 1; i < m_Path.Count(); i++)
		{
			vector point = m_Path[i];
			if (!m_Plan.Contained(point) || m_Plan.NearParked(point, 0.6)) { return false; }
			vector previous = m_Path[i - 1];
			int steps = Math.Ceil(vector.DistanceXZ(previous, point) / 0.5);
			for (int s = 1; s < steps && samples < 128; s++)
			{
				samples++;
				vector sample = vector.Lerp(previous, point, s * 1.0 / steps);
				if (vector.DistanceXZ(sample, here) <= 1.0 && m_Plan.Inside(sample, 0.25)) { continue; }
				if (!m_Plan.Contained(sample)) { return false; }
			}
		}
		return true;
	}

	protected void HoldForPath()
	{
		if (m_Speed) { m_Speed.Blocked = true; }
		if (m_Movement && m_Speed) { m_Movement.SetMovementTypeWanted(EMovementType.IDLE); }
		// Desired IDLE and zero input do not stop native idle/formation locomotion
		// (proven on fixed posts). The same source-owned minimum cap holds a blocked
		// patrol; other owners' limits remain intact.
		if (m_Actor && m_Speed) { m_Actor.SetSpeedLimit(this, 0, true); }
	}

	protected void StopMoving()
	{
		HoldForPath();
		// The native action lives in the agent's utility: never touch it once the
		// soldier or his utility is gone (a deleted actor, world teardown).
		if (m_Move && m_Actor && m_Utility && !m_Actor.IsDeleted()) { m_Move.Fail(); }
		m_Move = null;
		m_PathOk = false;
		m_PathCount = -1;
	}

	// Stop now and keep a claim near him: the nearest free stop within 1 m when
	// one is claimable, otherwise the claim he holds.
	protected void Block()
	{
		StopMoving();
		if (!m_Reserved || !m_Actor || !m_Plan) { return; }
		int nearest = m_Plan.NearestNode(m_Actor.GetOrigin(), 1.0, true);
		if (nearest >= 0 && nearest != m_Node && m_Plan.TryClaimStop(m_Actor, nearest)) { m_Node = nearest; }
	}

	void InvalidatePath()
	{
		if (m_Move) { Fail(GetGame().GetWorld().GetWorldTime()); }
	}

	protected bool AtPoint(vector point)
	{
		if (!m_Actor) { return false; }
		vector origin = m_Actor.GetOrigin();
		return vector.DistanceXZ(origin, point) <= 0.35 && Math.AbsFloat(origin[1] - point[1]) <= 0.8;
	}

	// Bounded existing-path inspection and O(1) floor lookups; no traces or world
	// scans. Zero input whenever he is not walking (dwelling or holding).
	bool AllowInput(float dt, vector localDirection, out float inputScale)
	{
		inputScale = 1;
		if (!m_Speed || !IsOwnedActor()) { Release(); return true; }
		if (m_Controller.IsUnconscious()) { StopMoving(); return false; }
		if (!m_Move || dt <= 0 || dt > 0.25) { return false; }
		float now = GetGame().GetWorld().GetWorldTime();
		// Arrival is checked before projection, even between shared scheduler ticks.
		if (AtPoint(m_Dest))
		{
			Arrive(now);
			return false;
		}
		if (!InspectCurrentPath()) { return false; }
		vector pos = m_Actor.GetOrigin();
		vector direction = m_Actor.VectorToParent(localDirection);
		if (vector.DistanceSq(direction, vector.Zero) > 0.01) { direction.Normalize(); }
		// Reduce the actual requested input along with the candidate horizon; simply
		// clipping the checked point would leave unsafe full-speed input unchanged.
		// Verify input scaling, walk/run speed and braking in the native fixture.
		float step = m_StepRate * dt;
		inputScale = Math.Min(1.0, vector.DistanceXZ(pos, m_Dest) / step);
		vector projected = pos + direction * (step * inputScale);
		vector inertial = pos + m_Controller.GetVelocity() * dt;
		// The next step and the momentum must stay on the indoor floor; where he
		// stands only inside the building. One who drifted off the floor (a door
		// frame, native avoidance) may step anywhere inside the building along his
		// admitted path, which leads back onto the floor.
		bool drifted = !m_Plan.Contained(pos);
		bool stepOk = m_Plan.Contained(projected) || (drifted && m_Plan.Inside(projected, 0.25));
		bool driftOk = m_Plan.Contained(inertial) || (drifted && m_Plan.Inside(inertial, 0.25));
		if (!m_Plan.Inside(pos, 0.25) || !stepOk || !driftOk)
		{
			Fail(now);
			return false;
		}
		return true;
	}

	// Alarm: claim a window or watch point and run there (player level), then hold
	// it like a post. False when the stop cannot be claimed.
	bool Alert(int node, vector look, bool window, int hops)
	{
		if (!m_Speed || !IsOwnedActor() || node < 0 || node >= m_Plan.Nodes.Count()) { return false; }
		if (node != m_Node && !m_Plan.TryClaimStop(m_Actor, node)) { return false; }
		StopMoving();
		m_Node = node;
		m_Look = look;
		m_Window = window;
		m_RetryAt = GetGame().GetWorld().GetWorldTime() + 5000;
		m_State = STATE_HOLD;
		if (AtPoint(m_Plan.Nodes[node].Position)) { return true; }
		BeginMove(m_Plan.Nodes[node].Position, EMovementType.RUN, SCR_AIActionBase.PRIORITY_LEVEL_PLAYER, Math.Max(10000, hops * 1000));
		m_State = STATE_ALERT_MOVE;
		return true;
	}

	// Alarm with no free window or watch point: hold the stop he has (spaced from
	// every other claim); a walk under way ends at its claimed stop first.
	void HoldHere()
	{
		if (!m_Speed || !m_Plan || m_Node < 0) { return; }
		m_Window = false;
		m_RetryAt = GetGame().GetWorld().GetWorldTime() + 5000;
		if (m_Move)
		{
			m_State = STATE_ALERT_MOVE;
			return;
		}
		m_State = STATE_HOLD;
		m_Look = m_Plan.Nodes[m_Node].WatchLook;
	}

	// Combat calmed down: back to patrol from the stop he holds.
	void Calm()
	{
		if (!m_Speed || !m_Plan || m_Node < 0) { return; }
		m_Window = false;
		if (m_Move)
		{
			m_State = STATE_LEG;
			return;
		}
		m_State = STATE_DWELL;
		m_DwellUntil = GetGame().GetWorld().GetWorldTime() + Math.RandomFloat(10000, 30000);
		m_Look = m_Plan.Nodes[m_Node].WatchLook;
	}

	// Woken from Simulation caching: the dwell that ran out while he was paused
	// starts again, so a restored patroller first stands at his stop.
	void RestartDwell()
	{
		if (m_State != STATE_DWELL || m_Move) { return; }
		m_DwellUntil = GetGame().GetWorld().GetWorldTime() + Math.RandomFloat(10000, 30000);
	}

	// Cache sleep pending: finish the walk under way, start no new one.
	void SetSettle(bool settle)
	{
		m_Settle = settle;
	}

	// The sleep waited 20 s: stop where he is, at a stop near him.
	void ForceSettle()
	{
		if (m_Move) { Block(); }
		// Stopped half way: the stop he stands at becomes his claim, so he caches (and
		// wakes, or is saved) on a stop rather than metres away from the one he claimed.
		if (m_Actor && m_Plan && m_Node >= 0 && vector.DistanceXZ(m_Actor.GetOrigin(), m_Plan.Nodes[m_Node].Position) > 1.5)
		{
			int nearStop = m_Plan.NearestNode(m_Actor.GetOrigin(), 1.0, true);
			if (nearStop >= 0 && nearStop != m_Node && m_Plan.TryClaimStop(m_Actor, nearStop))
			{
				m_Node = nearStop;
				m_Look = m_Plan.Nodes[nearStop].WatchLook;
			}
		}
		m_State = STATE_DWELL;
		m_Window = false;
		m_DwellUntil = GetGame().GetWorld().GetWorldTime() + 10000;
	}

	bool Settled()
	{
		return m_State == STATE_DWELL && !m_Move;
	}

	bool InAlert()
	{
		return m_State == STATE_ALERT_MOVE || m_State == STATE_HOLD;
	}

	bool HoldsWindow()
	{
		return m_Window && InAlert();
	}

	bool RetryDue()
	{
		return GetGame().GetWorld().GetWorldTime() >= m_RetryAt;
	}

	void DelayRetry(float seconds)
	{
		m_RetryAt = GetGame().GetWorld().GetWorldTime() + seconds * 1000;
	}

	bool IsMoving()
	{
		return m_Move != null;
	}

	int ClaimedNode()
	{
		return m_Node;
	}

	int PatrolPhase()
	{
		return m_State;
	}

	SCR_ChimeraCharacter GetActor()
	{
		return m_Actor;
	}

	void Release()
	{
		StopMoving();
		if (m_Actor) { m_Actor.SetSpeedLimit(this, 1); }
		if (m_Reserved && m_Plan) { m_Plan.ReleaseReservation(m_Actor); }
		m_Reserved = false;
		if (m_Utility && m_Utility.EXPG_GetPatrolControl() == this) { m_Utility.EXPG_SetPatrolControl(null); }
		if (m_Controller && m_Controller.EXPG_GetPatrolControl() == this) { m_Controller.EXPG_SetPatrolControl(null); }
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
		m_Node = -1;
		m_State = STATE_DWELL;
		m_Window = false;
	}

	// A destructor may run during world teardown, after the native agent and its
	// actions are gone (a Fail() there crashed the server): drop the action unseen.
	void ~EXPG_PatrolControl()
	{
		m_Move = null;
		Release();
	}
}

modded class SCR_CharacterControllerComponent
{
	protected EXPG_PostControl m_EXPG_PostControl;
	EXPG_PostControl EXPG_GetPostControl() { return m_EXPG_PostControl; }
	void EXPG_SetPostControl(EXPG_PostControl post) { m_EXPG_PostControl = post; }
	protected EXPG_PatrolControl m_EXPG_PatrolControl;
	EXPG_PatrolControl EXPG_GetPatrolControl() { return m_EXPG_PatrolControl; }
	void EXPG_SetPatrolControl(EXPG_PatrolControl patrol) { m_EXPG_PatrolControl = patrol; }
	protected override void OnPrepareControls(IEntity owner, ActionManager am, float dt, bool player)
	{
		super.OnPrepareControls(owner, am, dt, player);
		if (m_EXPG_PostControl)
		{
			// Possession releases the native speed cap in the first input callback.
			// Aiming, rotation, stance and weapon input stay native.
			if (player || !m_EXPG_PostControl.IsOwnedActor()) { m_EXPG_PostControl.Release(); }
			else
			{
				CharacterInputContext postInput = GetInputContext();
				if (postInput) { postInput.SetMovement(0, vector.Zero); }
			}
		}
		// Possession releases the patrol's native speed cap in the first input callback.
		if (player && m_EXPG_PatrolControl) { m_EXPG_PatrolControl.Release(); }
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
