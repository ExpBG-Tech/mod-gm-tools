// Vanilla 1.8 never constructs SCR_AISuppressionVolumeSphere (its suppress trees only get
// boxes); EXPBG does: AI Global Skills warning shots aim at a 1 m sphere beside the target
// (EGS_StartWarningShots), and the suppress fixture uses one too. The base
// GetRandomPosition (SCR_AISuppressionVolume.c) keeps every suppression line at least
// 2 degrees wide "no matter what", so beyond about 28.6 m from a 1 m sphere a line drawn
// from its centre (how every burst starts) always ends outside the sphere's footprint
// (beyond 57.3 m every line does; at any range, so does a line that the line node
// continues outward from near the rim), and a start point outside the volume is moved
// onto the rim (float rounding can put it just beyond). Vanilla GetYRange then takes
// Math.Sqrt(r*r - d*d) of a negative number: NaN aim heights on retail servers and the
// assertion "!BadFloat(val)" (run blocked) on diagnostic servers.
// Clamped here: the sphere's half-height at a point is 0 on and beyond its rim, so the
// vertical range collapses to the centre height (the base still lifts it above the
// ground). Inside the rim the vanilla arithmetic and result are unchanged.
modded class SCR_AISuppressionVolumeSphere
{
	//------------------------------------------------------------------------------------------------
	override protected void GetYRange(vector position, out float minY, out float maxY)
	{
		vector centerPos = GetCenterPosition();
		float distToCenter2D = vector.DistanceXZ(centerPos, position);
		float halfHeightSq = (m_fRadius * m_fRadius) - (distToCenter2D * distToCenter2D);
		float halfHeight = 0;
		if (halfHeightSq > 0)
			halfHeight = Math.Sqrt(halfHeightSq);

		maxY = centerPos[1] + halfHeight;
		minY = centerPos[1] - halfHeight;
	}
}
