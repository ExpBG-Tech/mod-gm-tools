// Aiming error of AI soldiers. Vanilla draws a Gaussian error whose spread depends only on
// the soldier's EAISkill; EXPBG replaces the tier when a skill is set and scales the result
// by the aim accuracy setting. Units without an EXPBG profile get the vanilla value.
modded class SCR_AIGetAimErrorOffset
{
	//------------------------------------------------------------------------------------------------
	override float GetRandomFactor(EAISkill skill, float mu)
	{
		if (!m_CombatComponent)
			return super.GetRandomFactor(skill, mu);

		return super.GetRandomFactor(m_CombatComponent.EGS_ResolveSkill(skill), mu) * m_CombatComponent.EGS_GetAimErrorScale();
	}
}
