// Native Enforce fixture for addon/ai-global-skills. NOT RUN: no native slot; excluded
// from the addon. Scalar contracts only (tables, role/ROE resolution, warning geometry,
// attribute registry); no world, AI or gameplay is involved.
class EGS_SkillsTest
{
	//------------------------------------------------------------------------------------------------
	static bool SettingsTables()
	{
		if (EGS_Settings.SkillFromIndex(0) != EAISkill.NONE || EGS_Settings.SkillFromIndex(1) != EAISkill.NOOB)
			return false;

		if (EGS_Settings.SkillFromIndex(5) != EAISkill.EXPERT || EGS_Settings.SkillFromIndex(9) != EAISkill.NONE)
			return false;

		if (EGS_Settings.PerceptionFromIndex(0) != 1.0 || EGS_Settings.PerceptionFromIndex(3) != 1.0 || EGS_Settings.PerceptionFromIndex(1) >= 1.0 || EGS_Settings.PerceptionFromIndex(5) <= 1.0)
			return false;

		if (!float.AlmostEqual(EGS_Settings.AimErrorScaleFromIndex(0), 1.0) || !float.AlmostEqual(EGS_Settings.AimErrorScaleFromIndex(4), 1.0))
			return false;

		if (!float.AlmostEqual(EGS_Settings.AimErrorScaleFromIndex(2), 2.0) || !float.AlmostEqual(EGS_Settings.AimErrorScaleFromIndex(6), 0.5) || !float.AlmostEqual(EGS_Settings.AimErrorScaleFromIndex(8), 0.25))
			return false;

		if (EGS_Settings.ClampSlotValue(0, 9) != EGS_Settings.SKILL_MAX || EGS_Settings.ClampSlotValue(EGS_Settings.SLOT_AIM, 99) != EGS_Settings.AIM_MAX || EGS_Settings.ClampSlotValue(3, -4) != 0)
			return false;

		// Session-attribute packing (CDF savers) round-trips every slot exactly through a float.
		array<int> values = {5, 0, 3, 1, 4, 8, 0, 6, 2, 7};
		int skills = EGS_Settings.PackSlots(values, EGS_Settings.SLOT_SKILL, 8);
		int aims = EGS_Settings.PackSlots(values, EGS_Settings.SLOT_AIM, 16);
		vector stored = Vector(2, skills, aims);
		int storedSkills = stored[1];
		int storedAims = stored[2];
		array<int> restored = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
		EGS_Settings.UnpackSlots(storedSkills, restored, EGS_Settings.SLOT_SKILL, 8);
		EGS_Settings.UnpackSlots(storedAims, restored, EGS_Settings.SLOT_AIM, 16);
		for (int i = 0; i < EGS_Settings.SLOT_COUNT; i++)
		{
			if (restored[i] != values[i])
				return false;
		}

		return true;
	}

	//------------------------------------------------------------------------------------------------
	static bool RoleResolution()
	{
		// Untouched faction: vanilla everywhere.
		if (EGS_Settings.ResolveIndex(null, EGS_Settings.SLOT_SKILL, EGS_Settings.ROLE_MARKSMAN, true) != 0)
			return false;

		array<int> values = {};
		for (int i = 0; i < EGS_Settings.SLOT_COUNT; i++)
		{
			values.Insert(0);
		}

		if (EGS_Settings.HasRoleOverrides(values))
			return false;

		values[EGS_Settings.SLOT_SKILL] = 3; // faction: Regular
		values[EGS_Settings.SLOT_SKILL + 1 + EGS_Settings.ROLE_MACHINE_GUNNER] = 5; // MG: Expert
		values[EGS_Settings.SLOT_AIM + 1 + EGS_Settings.ROLE_LEADER] = 6; // leader aim: 200%
		if (!EGS_Settings.HasRoleOverrides(values))
			return false;

		if (EGS_Settings.ResolveIndex(values, EGS_Settings.SLOT_SKILL, EGS_Settings.ROLE_RIFLEMAN, false) != 3)
			return false;

		if (EGS_Settings.ResolveIndex(values, EGS_Settings.SLOT_SKILL, EGS_Settings.ROLE_MACHINE_GUNNER, false) != 5)
			return false;

		// A leader without a leader skill override falls back to his weapon role.
		if (EGS_Settings.ResolveIndex(values, EGS_Settings.SLOT_SKILL, EGS_Settings.ROLE_MACHINE_GUNNER, true) != 5)
			return false;

		if (EGS_Settings.ResolveIndex(values, EGS_Settings.SLOT_AIM, EGS_Settings.ROLE_RIFLEMAN, true) != 6)
			return false;

		// Faction-wide aim untouched: non-leaders stay vanilla.
		if (EGS_Settings.ResolveIndex(values, EGS_Settings.SLOT_AIM, EGS_Settings.ROLE_MARKSMAN, false) != 0)
			return false;

		if (EGS_Roles.FirearmRank(EWeaponType.WT_MACHINEGUN) != 2 || EGS_Roles.FirearmRank(EWeaponType.WT_HANDGUN) != 1)
			return false;

		if (EGS_Roles.FirearmRank(EWeaponType.WT_ROCKETLAUNCHER) != 0 || EGS_Roles.FirearmRank(EWeaponType.WT_FRAGGRENADE) != 0 || EGS_Roles.FirearmRank(EWeaponType.WT_GRENADELAUNCHER) != 0)
			return false;

		return true;
	}

	//------------------------------------------------------------------------------------------------
	static bool RulesOfEngagement()
	{
		// Without a module the module default is vanilla, but a squad's own choice still applies.
		if (EGS_Settings.EffectiveRoe(false, EGS_Settings.GROUP_ROE_DEFAULT, EGS_Settings.ROE_WARNING_SHOTS) != EGS_Settings.ROE_VANILLA)
			return false;

		if (EGS_Settings.EffectiveRoe(false, EGS_Settings.ROE_FIRE_ON_SIGHT, EGS_Settings.ROE_WARNING_SHOTS) != EGS_Settings.ROE_FIRE_ON_SIGHT)
			return false;

		if (EGS_Settings.EffectiveRoe(false, EGS_Settings.ROE_RETURN_FIRE, EGS_Settings.ROE_VANILLA) != EGS_Settings.ROE_RETURN_FIRE)
			return false;

		if (EGS_Settings.EffectiveRoe(false, EGS_Settings.GROUP_ROE_EXEMPT, EGS_Settings.ROE_WARNING_SHOTS) != EGS_Settings.ROE_VANILLA)
			return false;

		// Skill per squad and per soldier: soldier > squad > faction, Novice..Expert only.
		if (EGS_SkillOverrides.Pick(3, 5) != 3 || EGS_SkillOverrides.Pick(0, 5) != 5 || EGS_SkillOverrides.Pick(0, 0) != EGS_SkillOverrides.FOLLOW)
			return false;

		if (EGS_SkillOverrides.Normalize(9) != EGS_Settings.SKILL_MAX || EGS_SkillOverrides.Normalize(-2) != EGS_SkillOverrides.FOLLOW)
			return false;

		if (EGS_Settings.EffectiveRoe(true, EGS_Settings.GROUP_ROE_EXEMPT, EGS_Settings.ROE_WARNING_SHOTS) != EGS_Settings.ROE_VANILLA)
			return false;

		if (EGS_Settings.EffectiveRoe(true, EGS_Settings.GROUP_ROE_DEFAULT, EGS_Settings.ROE_RETURN_FIRE) != EGS_Settings.ROE_RETURN_FIRE)
			return false;

		if (EGS_Settings.EffectiveRoe(true, EGS_Settings.ROE_WARNING_SHOTS, EGS_Settings.ROE_VANILLA) != EGS_Settings.ROE_WARNING_SHOTS)
			return false;

		if (EGS_Settings.EffectiveRoe(true, EGS_Settings.GROUP_ROE_DEFAULT, 17) != EGS_Settings.ROE_WARNING_SHOTS)
			return false;

		return true;
	}

	//------------------------------------------------------------------------------------------------
	static bool WarningGeometry()
	{
		array<float> distances = {0, 20, 150, 600};
		foreach (float distance : distances)
		{
			vector shooter = Vector(100, 10, 100);
			vector target = shooter + Vector(distance * 0.6, 0, distance * 0.8);
			vector left = EGS_Manager.WarningOffset(shooter, target, true);
			vector right = EGS_Manager.WarningOffset(shooter, target, false);
			float gapLeft = vector.DistanceXZ(left, target);
			float gapRight = vector.DistanceXZ(right, target);
			// Beside the target, clear of the vanilla 2-degree minimum sweep from the aim sphere.
			float sweep = Math.Tan(2 * Math.DEG2RAD) * distance;
			float clear = EGS_Manager.WARNING_RADIUS + sweep + EGS_Manager.WARNING_CLEARANCE;
			if (gapLeft < 3.0 || gapLeft < clear || gapLeft > 10.5 + sweep || !float.AlmostEqual(gapLeft, gapRight))
				return false;

			if (left == right || gapLeft != gapLeft)
				return false;

			// Rounds land short of the target, never behind it.
			if (distance > 0 && vector.DistanceXZ(left, shooter) >= vector.DistanceXZ(target, shooter))
				return false;
		}

		return true;
	}

	//------------------------------------------------------------------------------------------------
	static bool AttributeRegistry()
	{
		array<typename> types = {};
		for (int slot = 0; slot < EGS_Settings.SLOT_COUNT; slot++)
		{
			typename type = EGS_Attributes.SlotAttributeType(slot);
			if (types.Contains(type) || !type.IsInherited(EGS_ModuleAttribute))
				return false;

			types.Insert(type);
		}

		EGS_SkillAllAttribute skill = new EGS_SkillAllAttribute();
		EGS_FactionAttribute faction = new EGS_FactionAttribute();
		EGS_GroupRoeAttribute groupRoe = new EGS_GroupRoeAttribute();
		if (skill.IsSerializable() || faction.IsSerializable() || groupRoe.IsSerializable())
			return false;

		// Without a module the module and group attributes stay hidden.
		if (skill.ReadVariable(null, null) || groupRoe.ReadVariable(null, null))
			return false;

		return true;
	}

	//------------------------------------------------------------------------------------------------
	static bool Run()
	{
		bool settings = SettingsTables();
		bool roles = RoleResolution();
		bool roe = RulesOfEngagement();
		bool geometry = WarningGeometry();
		bool attributes = AttributeRegistry();
		PrintFormat("[EGS CONTRACT RESULT] settings=%1 roles=%2 roe=%3 geometry=%4 attributes=%5", settings, roles, roe, geometry, attributes);
		return settings && roles && roe && geometry && attributes;
	}
}
