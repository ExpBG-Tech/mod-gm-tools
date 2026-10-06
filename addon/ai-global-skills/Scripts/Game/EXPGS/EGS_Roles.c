// Role detection from what a soldier actually carries, so it works for modded factions:
// - Leader: leads an AI group of two or more (re-evaluated when the leader changes).
// - MG/LMG: a machine-gun-typed weapon, or a rifle fed from a belt/box/drum (75+ rounds).
// - Marksman: a sniper-typed weapon, or a semi-automatic/bolt rifle with a magnified optic.
// - Rifleman: everything else.
// Authored editor role labels are only a fallback when the weapons give no answer.
class EGS_Roles
{
	static const int BELT_CAPACITY = 75;
	static const float MAGNIFIED_OPTIC = 2.5;

	//------------------------------------------------------------------------------------------------
	static bool IsLeader(AIAgent agent)
	{
		if (!agent)
			return false;

		AIGroup group = agent.GetParentGroup();
		return group && group.GetLeaderAgent() == agent && group.GetAgentsCount() > 1;
	}

	//------------------------------------------------------------------------------------------------
	static int ClassifyWeaponRole(ChimeraCharacter character)
	{
		if (!character)
			return EGS_Settings.ROLE_RIFLEMAN;

		bool machineGun;
		bool marksman;
		BaseWeaponManagerComponent weaponManager = character.GetWeaponManager();
		if (weaponManager)
		{
			array<IEntity> weapons = {};
			weaponManager.GetWeaponsList(weapons);
			foreach (IEntity weaponEntity : weapons)
			{
				if (!weaponEntity)
					continue;

				BaseWeaponComponent weapon = BaseWeaponComponent.Cast(weaponEntity.FindComponent(BaseWeaponComponent));
				if (!weapon)
					continue;

				EWeaponType type = weapon.GetWeaponType();
				if (type == EWeaponType.WT_MACHINEGUN)
				{
					machineGun = true;
				}
				else if (type == EWeaponType.WT_SNIPERRIFLE)
				{
					marksman = true;
				}
				else if (type == EWeaponType.WT_RIFLE)
				{
					if (HasBeltCapacity(weapon))
						machineGun = true;
					else if (HasMagnifiedOptic(weaponEntity, weapon) && !HasAutomaticFire(weapon))
						marksman = true;
				}
			}
		}

		if (machineGun)
			return EGS_Settings.ROLE_MACHINE_GUNNER;

		if (marksman)
			return EGS_Settings.ROLE_MARKSMAN;

		return ClassifyByLabels(character);
	}

	//------------------------------------------------------------------------------------------------
	protected static int ClassifyByLabels(IEntity character)
	{
		SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.GetEditableEntity(character);
		if (!editable)
			return EGS_Settings.ROLE_RIFLEMAN;

		SCR_EditableEntityUIInfo info = SCR_EditableEntityUIInfo.Cast(editable.GetInfo());
		if (!info)
			return EGS_Settings.ROLE_RIFLEMAN;

		if (info.HasEntityLabel(EEditableEntityLabel.ROLE_MACHINEGUNNER))
			return EGS_Settings.ROLE_MACHINE_GUNNER;

		if (info.HasEntityLabel(EEditableEntityLabel.ROLE_SHARPSHOOTER))
			return EGS_Settings.ROLE_MARKSMAN;

		return EGS_Settings.ROLE_RIFLEMAN;
	}

	//------------------------------------------------------------------------------------------------
	static bool HasBeltCapacity(BaseWeaponComponent weapon)
	{
		BaseMagazineComponent magazine = weapon.GetCurrentMagazine();
		return magazine && magazine.GetMaxAmmoCount() >= BELT_CAPACITY;
	}

	//------------------------------------------------------------------------------------------------
	//! Magnified optic on the weapon (attached sights first, then any optic attachment).
	static bool HasMagnifiedOptic(IEntity weaponEntity, BaseWeaponComponent weapon)
	{
		if (IsMagnified(weapon.GetAttachedSights()) || IsMagnified(weapon.GetSights()))
			return true;

		IEntity child = weaponEntity.GetChildren();
		while (child)
		{
			if (IsMagnified(BaseSightsComponent.Cast(child.FindComponent(SCR_2DOpticsComponent))))
				return true;

			child = child.GetSibling();
		}

		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected static bool IsMagnified(BaseSightsComponent sights)
	{
		SCR_2DOpticsComponent optics = SCR_2DOpticsComponent.Cast(sights);
		return optics && optics.GetMagnification() >= MAGNIFIED_OPTIC;
	}

	//------------------------------------------------------------------------------------------------
	static bool HasAutomaticFire(BaseWeaponComponent weapon)
	{
		array<BaseMuzzleComponent> muzzles = {};
		weapon.GetMuzzlesList(muzzles);
		foreach (BaseMuzzleComponent muzzle : muzzles)
		{
			if (!muzzle || muzzle.GetMuzzleType() != EMuzzleType.MT_BaseMuzzle)
				continue;

			array<BaseFireMode> fireModes = {};
			muzzle.GetFireModesList(fireModes);
			foreach (BaseFireMode fireMode : fireModes)
			{
				if (!fireMode)
					continue;

				EWeaponFiremodeType modeType = fireMode.GetFiremodeType();
				if (modeType == EWeaponFiremodeType.Auto || modeType == EWeaponFiremodeType.Burst)
					return true;
			}
		}

		return false;
	}

	//------------------------------------------------------------------------------------------------
	//! Primary firearm rank for ammunition refills: 2 long gun, 1 handgun, 0 anything else
	//! (launchers, grenades and explosives are never refilled).
	static int FirearmRank(EWeaponType type)
	{
		if (type == EWeaponType.WT_RIFLE || type == EWeaponType.WT_MACHINEGUN || type == EWeaponType.WT_SNIPERRIFLE)
			return 2;

		if (type == EWeaponType.WT_HANDGUN)
			return 1;

		return 0;
	}
}
