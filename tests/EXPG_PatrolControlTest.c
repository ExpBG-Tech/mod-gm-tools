// Native/source fixture authored before implementation. NOT RUN.
// Native acceptance must additionally prove that AI OnPrepareControls runs before
// movement is applied, invalid OnPathSet paths never advance, and stairs work.
class EXPG_PatrolControlTest
{
	// Patrol containment now means keeping out of door zones where patrollers stop
	// (the corridor=1 marker is kept). A 0.9 m leaf hinged at the origin, the
	// doorway along +x: inside the swing disc on both sides, in the passage 1.4 m
	// out, clear 1.6 m out or beyond the leaf's end, and not on another floor.
	static bool CorridorChecks()
	{
		vector hinge = "0 0 0";
		vector along = "1 0 0";
		if (!EXPG_BuildingPlan.InDoorZone("0.5 0.05 1.2", hinge, along, 0.9, 0))
		{
			return false;
		}
		if (!EXPG_BuildingPlan.InDoorZone("0.5 0.05 -1.2", hinge, along, 0.9, 0))
		{
			return false;
		}
		if (!EXPG_BuildingPlan.InDoorZone("1.0 0.05 1.4", hinge, along, 0.9, 0))
		{
			return false;
		}
		if (EXPG_BuildingPlan.InDoorZone("1.0 0.05 1.6", hinge, along, 0.9, 0))
		{
			return false;
		}
		if (EXPG_BuildingPlan.InDoorZone("1.8 0.05 0.5", hinge, along, 0.9, 0))
		{
			return false;
		}
		return !EXPG_BuildingPlan.InDoorZone("0.5 3.05 0.5", hinge, along, 0.9, 0);
	}

	static bool RejectMissingActor()
	{
		ref EXPG_PatrolControl patrol = new EXPG_PatrolControl();
		return !patrol.Start(null, null, 0) && !patrol.Tick();
	}
}
