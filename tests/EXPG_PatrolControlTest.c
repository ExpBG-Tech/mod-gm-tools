// Native/source fixture authored before implementation. NOT RUN.
// Native acceptance must additionally prove that AI OnPrepareControls runs before
// movement is applied, invalid OnPathSet paths never advance, and stairs work.
class EXPG_PatrolControlTest
{
	static bool CorridorChecks()
	{
		vector start = "0 0 0";
		vector end = "0.75 0.3 0";
		if (!EXPG_PatrolControl.InEdgeCorridor("0.375 0.15 0", start, end))
		{
			return false;
		}
		if (EXPG_PatrolControl.InEdgeCorridor("0.375 0.15 0.5", start, end))
		{
			return false;
		}
		if (EXPG_PatrolControl.InEdgeCorridor("0.375 3 0", start, end))
		{
			return false;
		}
		return !EXPG_PatrolControl.InEdgeCorridor("2 0.3 0", start, end);
	}

	static bool RejectMissingActor()
	{
		ref EXPG_PatrolControl patrol = new EXPG_PatrolControl();
		return !patrol.Start(null, null, 0) && !patrol.Tick();
	}
}
