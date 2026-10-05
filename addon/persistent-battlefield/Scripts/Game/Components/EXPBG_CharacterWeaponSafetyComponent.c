class EXPBG_CharacterWeaponSafetyComponentClass : ScriptComponentClass
{
}

//------------------------------------------------------------------------------------------------
//! Places a newly initialized character on weapon safety after weapon/controller initialization.
//! Only the owning peer toggles safety, and only once the controller is registered for
//! replication: SetSafety sends an RPC, and world-placed characters are not registered yet
//! when the first delay elapses during mission load.
//------------------------------------------------------------------------------------------------
class EXPBG_CharacterWeaponSafetyComponent : ScriptComponent
{
	protected static const int EXPBG_SAFETY_DELAY_MS = 200;
	protected static const int EXPBG_SAFETY_RETRY_MS = 500;
	protected static const int EXPBG_SAFETY_MAX_ATTEMPTS = 20;

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);

		if (!owner.FindComponent(CharacterControllerComponent))
			return;

		GetGame().GetCallqueue().CallLater(EXPBG_EnableSafety, EXPBG_SAFETY_DELAY_MS, false, owner, 1);
	}

	//------------------------------------------------------------------------------------------------
	protected void EXPBG_EnableSafety(IEntity owner, int attempt)
	{
		if (!owner)
			return;

		CharacterControllerComponent characterController = CharacterControllerComponent.Cast(owner.FindComponent(CharacterControllerComponent));
		if (!characterController)
			return;

		if (Replication.IsRunning())
		{
			RplComponent replication = RplComponent.Cast(owner.FindComponent(RplComponent));
			if (replication && !replication.IsOwner())
				return;

			if (!Replication.FindItemId(characterController).IsValid())
			{
				if (attempt < EXPBG_SAFETY_MAX_ATTEMPTS)
					GetGame().GetCallqueue().CallLater(EXPBG_EnableSafety, EXPBG_SAFETY_RETRY_MS, false, owner, attempt + 1);

				return;
			}
		}

		characterController.SetSafety(true, false);
	}
}
