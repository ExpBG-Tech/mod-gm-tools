// Vanilla's suppress tree (SuppressBehavior.bt, Fire_Suppressive.bt) copies its suppression
// volume into a behaviour-tree variable once, when the tree starts; the suppress behaviour
// owns the volume (and the utility's current-behaviour reference keeps that behaviour
// alive until the decision that also swaps the tree). A GM session logged the tree running
// with no volume ("No suppression volume provided!", 3 s after AI Surrender took a
// soldier); which agent and which trigger is not confirmed. Vanilla's own target-data and
// combat-move nodes simply fail on a missing volume, but the centre-position and line
// nodes call NodeError, a script VM exception. Here a missing volume fails the node
// instead, the way NodeError does after its report, and the soldier's next decision moves
// on. A suppress behaviour that itself has no volume can never run and is failed as well;
// one that has a volume is left alone. The capped warning names the executed action so
// the trigger can be traced. A present volume runs the vanilla node unchanged.
//
// Failing the behaviour does not stop its tree at once: Soldier.bt tests the executed
// action (SCR_AIIsValidAction, no abort type) only when it starts the tree, and the suppress
// tree's Parallel restarts its failed centre-position branch on every AI tick, so the same
// tree reads the missing volume once per tick until SCR_AIDecideBehavior next runs and
// swaps it (every 0.55 s at AI LOD 0, 1.3 s at LOD 1, 2 s beyond; a native fixture saw the
// first read plus five more, 83 ms apart). Those reads are vanilla timing, not new
// incidents: each guarded node counts, reports and retires a stale tree once per executed
// action and fails its later reads silently. One behaviour therefore counts at most twice
// (centre position and line), and the ten warnings cover distinct incidents.
class EGS_SuppressionGuard
{
	protected static const int MAX_REPORTS = 10;
	protected static int s_iMissingVolumes;
	protected static int s_iAbsorbedReads;

	//------------------------------------------------------------------------------------------------
	//! Stale trees caught (once per guarded node and executed action) since the game started
	//! (fixtures compare before and after).
	static int MissingVolumeCount()
	{
		return s_iMissingVolumes;
	}

	//------------------------------------------------------------------------------------------------
	//! Later reads of an already counted stale tree, failed without a count or a warning.
	static int AbsorbedReadCount()
	{
		return s_iAbsorbedReads;
	}

	//------------------------------------------------------------------------------------------------
	//! A guarded node read no volume; it returns FAIL. counted is the executed action this node
	//! already counted (weak, may be null); the result is the one it should keep.
	static AIActionBase OnMissingVolume(Node node, AIAgent owner, AIActionBase counted)
	{
		AIActionBase executed;
		IEntity controlled;
		if (owner)
		{
			controlled = owner.GetControlledEntity();
			SCR_AIBaseUtilityComponent utility = SCR_AIBaseUtilityComponent.Cast(owner.FindComponent(SCR_AIBaseUtilityComponent));
			if (utility)
				executed = utility.GetExecutedAction();
		}

		// Same tree read again before the next decision: already counted, reported and retired.
		if (executed && executed == counted)
		{
			s_iAbsorbedReads++;
			return executed;
		}

		s_iMissingVolumes++;
		string stateName = "none";
		if (executed)
			stateName = typename.EnumToString(EAIActionState, executed.GetActionState());

		bool retired;
		SCR_AISuppressBehavior behavior = SCR_AISuppressBehavior.Cast(executed);
		if (behavior && behavior.m_SuppressionVolume && !behavior.m_SuppressionVolume.m_Value)
		{
			EAIActionState actionState = behavior.GetActionState();
			if (actionState != EAIActionState.COMPLETED && actionState != EAIActionState.FAILED)
			{
				behavior.Fail();
				retired = true;
			}
		}

		if (s_iMissingVolumes <= MAX_REPORTS)
			Print(string.Format("[EXPBG AI] suppress tree without a suppression volume: agent=%1 entity=%2 node=%3 executed=%4 state=%5 retired=%6 count=%7", owner, controlled, node, executed, stateName, retired, s_iMissingVolumes), LogLevel.WARNING);

		return executed;
	}
}

modded class SCR_AIGetSuppressionVolumeCenterPosition
{
	// Weak: the executed action this node already counted; the utility owns it.
	protected AIActionBase m_EGS_CountedAction;

	//------------------------------------------------------------------------------------------------
	override ENodeResult EOnTaskSimulate(AIAgent owner, float dt)
	{
		SCR_AISuppressionVolumeBase volume;
		GetVariableIn(SUPPRESSION_VOLUME, volume);
		if (!volume)
		{
			m_EGS_CountedAction = EGS_SuppressionGuard.OnMissingVolume(this, owner, m_EGS_CountedAction);
			return ENodeResult.FAIL;
		}

		return super.EOnTaskSimulate(owner, dt);
	}
}

modded class SCR_AIGetSuppressionVolumeLine
{
	// Weak: the executed action this node already counted; the utility owns it.
	protected AIActionBase m_EGS_CountedAction;

	//------------------------------------------------------------------------------------------------
	override ENodeResult EOnTaskSimulate(AIAgent owner, float dt)
	{
		SCR_AISuppressionVolumeBase volume;
		GetVariableIn(SUPPRESSION_VOLUME_PORT, volume);
		if (!volume)
		{
			m_EGS_CountedAction = EGS_SuppressionGuard.OnMissingVolume(this, owner, m_EGS_CountedAction);
			return ENodeResult.FAIL;
		}

		return super.EOnTaskSimulate(owner, dt);
	}
}
