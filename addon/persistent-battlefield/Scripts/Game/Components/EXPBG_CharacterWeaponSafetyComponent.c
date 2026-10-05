class EXPBG_CharacterWeaponSafetyComponentClass : ScriptComponentClass
{
}

//------------------------------------------------------------------------------------------------
//! Places a newly initialized character on weapon safety after weapon/controller initialization.
//------------------------------------------------------------------------------------------------
class EXPBG_CharacterWeaponSafetyComponent : ScriptComponent
{
	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);

		CharacterControllerComponent characterController = CharacterControllerComponent.Cast(owner.FindComponent(CharacterControllerComponent));
		if (!characterController)
			return;

		GetGame().GetCallqueue().CallLater(EXPBG_EnableSafety, 200, false, characterController);
	}

	//------------------------------------------------------------------------------------------------
	protected void EXPBG_EnableSafety(CharacterControllerComponent characterController)
	{
		if (characterController)
			characterController.SetSafety(true, false);
	}
}
