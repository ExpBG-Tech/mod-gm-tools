//------------------------------------------------------------------------------------------------
//! Keeps vehicle wrecks registered with the garbage system so that the Game Master
//! cleanup button can still remove them, but gives them an effectively permanent
//! automatic lifetime.
//!
//! Corpses and loose inventory items retain their configured cleanup lifetimes.
//------------------------------------------------------------------------------------------------
modded class SCR_GarbageSystem
{
	//! 24 hours in seconds. A server or scenario restart will normally occur long
	//! before this lifetime expires.
	protected static const float EXPBG_PERSISTENT_BATTLEFIELD_LIFETIME = 86400;

	//------------------------------------------------------------------------------------------------
	override protected float OnInsertRequested(IEntity entity, float lifetime)
	{
		// Preserve vanilla SCR_GarbageSystem processing, including the extended
		// lifetime applied to vehicles carrying substantial supplies.
		lifetime = super.OnInsertRequested(entity, lifetime);

		// A previous override may have explicitly refused this insertion.
		if (lifetime <= 0)
			return lifetime;

		// Corpse persistence is intentionally disabled so corpse lifetime and
		// player-distance protection come from ChimeraSystemsConfig.conf.
		// if (ChimeraCharacter.Cast(entity))
		// 	return EXPBG_PERSISTENT_BATTLEFIELD_LIFETIME;

		// The config rule's "Only Destroyed" setting restricts this to wrecks.
		if (Vehicle.Cast(entity))
			return EXPBG_PERSISTENT_BATTLEFIELD_LIFETIME;

		// Weapons, magazines, deployables and other garbage retain their normal
		// configured lifetimes.
		return lifetime;
	}
}
