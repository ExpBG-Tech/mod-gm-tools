// EXPBG AI Global Skills: server-authoritative settings shared by every module.
// Index 0 always means "vanilla / no change", so an untouched mission behaves exactly
// like the base game. Values are kept per faction KEY (not index), which keeps saves
// valid when a modset adds or reorders factions (RHS and other modded factions are
// detected at runtime; no compatibility addon is needed).
class EGS_Settings
{
	// Per-faction slots: 0 skill (all roles), 1-4 skill per role,
	// 5 aim accuracy (all roles), 6-9 aim accuracy per role.
	static const int SLOT_COUNT = 10;
	static const int SLOT_SKILL = 0;
	static const int SLOT_AIM = 5;

	// Role groups (offset inside a skill/aim block, after the "all roles" slot).
	static const int ROLE_RIFLEMAN = 0;
	static const int ROLE_MACHINE_GUNNER = 1;
	static const int ROLE_MARKSMAN = 2;
	static const int ROLE_LEADER = 3;
	static const int ROLE_COUNT = 4;

	// Attribute keys.
	static const int KEY_FACTION = 1;
	static const int KEY_SKILL = 10; // 10 all roles, 11-14 per role
	static const int KEY_AIM = 20; // 20 all roles, 21-24 per role
	static const int KEY_ROE = 30;
	static const int KEY_AMMO = 31;
	static const int KEY_REFILLS = 32;

	static const int SKILL_MAX = 5;
	static const int AIM_MAX = 8;

	// Rules of engagement. Group overrides use DEFAULT/EXEMPT on top of the same values.
	static const int ROE_VANILLA = 0;
	static const int ROE_RETURN_FIRE = 1;
	static const int ROE_FIRE_ON_SIGHT = 2;
	static const int ROE_WARNING_SHOTS = 3;
	static const int GROUP_ROE_DEFAULT = 0;
	static const int GROUP_ROE_EXEMPT = 4;

	static const int AMMO_VANILLA = 0;
	static const int AMMO_UNLIMITED = 1;
	static const int AMMO_REFILL = 2;

	static const int REFILLS_MIN = 1;
	static const int REFILLS_MAX = 20;
	static const int REFILLS_DEFAULT = 3;

	static const int MAX_FACTIONS = 64;

	protected static BaseWorld s_World;
	protected static bool s_bFactionsBuilt;
	protected static ref array<string> s_aFactionKeys;
	protected static ref map<string, ref array<int>> s_mValues;
	protected static int s_iRoe;
	protected static int s_iAmmo;
	protected static int s_iRefills = REFILLS_DEFAULT;
	protected static int s_iEditFaction;

	//------------------------------------------------------------------------------------------------
	//! Reset to vanilla whenever a new world starts (statics survive Workbench play sessions).
	static void Ensure()
	{
		EXPBG_LazyStatics_EGS_Settings();
		if (!GetGame())
			return;

		BaseWorld world = GetGame().GetWorld();
		if (!world || world == s_World)
			return;

		s_World = world;
		s_bFactionsBuilt = false;
		s_aFactionKeys.Clear();
		s_mValues.Clear();
		s_iRoe = ROE_VANILLA;
		s_iAmmo = AMMO_VANILLA;
		s_iRefills = REFILLS_DEFAULT;
		s_iEditFaction = 0;
	}

	//------------------------------------------------------------------------------------------------
	//! Detect military factions of the running mission (vanilla, RHS or any other mod).
	static void EnsureFactions()
	{
		EXPBG_LazyStatics_EGS_Settings();
		Ensure();
		if (s_bFactionsBuilt || !GetGame())
			return;

		FactionManager factionManager = GetGame().GetFactionManager();
		if (!factionManager)
			return;

		array<Faction> factions = {};
		factionManager.GetFactionsList(factions);
		foreach (Faction faction : factions)
		{
			if (!faction || !IsMilitaryFaction(faction))
				continue;

			string key = faction.GetFactionKey();
			if (key.IsEmpty() || s_aFactionKeys.Contains(key))
				continue;

			s_aFactionKeys.Insert(key);
			if (s_aFactionKeys.Count() >= MAX_FACTIONS)
				break;
		}

		s_bFactionsBuilt = true;
	}

	//------------------------------------------------------------------------------------------------
	static bool IsMilitaryFaction(Faction faction)
	{
		SCR_Faction scripted = SCR_Faction.Cast(faction);
		if (scripted && !scripted.IsMilitary())
			return false;

		return true;
	}

	//------------------------------------------------------------------------------------------------
	static int GetFactionCount()
	{
		EXPBG_LazyStatics_EGS_Settings();
		EnsureFactions();
		return s_aFactionKeys.Count();
	}

	//------------------------------------------------------------------------------------------------
	static string GetFactionKey(int index)
	{
		EXPBG_LazyStatics_EGS_Settings();
		EnsureFactions();
		if (!s_aFactionKeys.IsIndexValid(index))
			return string.Empty;

		return s_aFactionKeys[index];
	}

	//------------------------------------------------------------------------------------------------
	//! \return the ten slot values of a faction, or null when the faction is untouched.
	static array<int> GetFactionValues(string factionKey)
	{
		EXPBG_LazyStatics_EGS_Settings();
		Ensure();
		if (factionKey.IsEmpty())
			return null;

		array<int> values;
		if (!s_mValues.Find(factionKey, values))
			return null;

		return values;
	}

	//------------------------------------------------------------------------------------------------
	static int GetValue(string factionKey, int slot)
	{
		array<int> values = GetFactionValues(factionKey);
		if (!values || !values.IsIndexValid(slot))
			return 0;

		return values[slot];
	}

	//------------------------------------------------------------------------------------------------
	static bool SetValue(string factionKey, int slot, int value)
	{
		EXPBG_LazyStatics_EGS_Settings();
		Ensure();
		if (factionKey.IsEmpty() || slot < 0 || slot >= SLOT_COUNT)
			return false;

		value = ClampSlotValue(slot, value);
		array<int> values;
		if (!s_mValues.Find(factionKey, values))
		{
			if (value == 0)
				return false;

			values = {};
			for (int i = 0; i < SLOT_COUNT; i++)
			{
				values.Insert(0);
			}

			s_mValues.Insert(factionKey, values);
		}

		if (values[slot] == value)
			return false;

		values[slot] = value;
		return true;
	}

	//------------------------------------------------------------------------------------------------
	static int ClampSlotValue(int slot, int value)
	{
		if (slot >= SLOT_AIM)
			return Math.ClampInt(value, 0, AIM_MAX);

		return Math.ClampInt(value, 0, SKILL_MAX);
	}

	//------------------------------------------------------------------------------------------------
	static int GetRoe()
	{
		Ensure();
		return s_iRoe;
	}

	//------------------------------------------------------------------------------------------------
	static int GetAmmoMode()
	{
		Ensure();
		return s_iAmmo;
	}

	//------------------------------------------------------------------------------------------------
	static int GetRefills()
	{
		Ensure();
		return s_iRefills;
	}

	//------------------------------------------------------------------------------------------------
	static int GetEditFaction()
	{
		EXPBG_LazyStatics_EGS_Settings();
		EnsureFactions();
		return Math.ClampInt(s_iEditFaction, 0, Math.MaxInt(0, s_aFactionKeys.Count() - 1));
	}

	//------------------------------------------------------------------------------------------------
	//! Server-side read used by the attributes: x = value, y = faction index it belongs to.
	static vector ReadSetting(int key)
	{
		int faction = GetEditFaction();
		if (key == KEY_FACTION)
			return Vector(faction, 0, 0);

		if (key >= KEY_SKILL && key <= KEY_SKILL + ROLE_COUNT)
			return Vector(GetValue(GetFactionKey(faction), SLOT_SKILL + key - KEY_SKILL), faction, 0);

		if (key >= KEY_AIM && key <= KEY_AIM + ROLE_COUNT)
			return Vector(GetValue(GetFactionKey(faction), SLOT_AIM + key - KEY_AIM), faction, 0);

		if (key == KEY_ROE)
			return Vector(GetRoe(), 0, 0);

		if (key == KEY_AMMO)
			return Vector(GetAmmoMode(), 0, 0);

		if (key == KEY_REFILLS)
			return Vector(GetRefills(), 0, 0);

		return vector.Zero;
	}

	//------------------------------------------------------------------------------------------------
	//! Server only. Per-faction values carry their faction index in y, so the value always lands
	//! on the faction that was shown when the Game Master pressed Save.
	static void WriteSetting(int key, vector setting)
	{
		EXPBG_LazyStatics_EGS_Settings();
		if (!Replication.IsServer())
			return;

		EnsureFactions();
		int value = Math.Round(setting[0]);
		bool changed;
		bool refresh;

		if (key == KEY_FACTION)
		{
			int editFaction = Math.ClampInt(value, 0, Math.MaxInt(0, s_aFactionKeys.Count() - 1));
			changed = editFaction != s_iEditFaction;
			s_iEditFaction = editFaction;
		}
		else if ((key >= KEY_SKILL && key <= KEY_SKILL + ROLE_COUNT) || (key >= KEY_AIM && key <= KEY_AIM + ROLE_COUNT))
		{
			int factionIndex = Math.Round(setting[1]);
			string factionKey = GetFactionKey(factionIndex);
			if (factionKey.IsEmpty())
				return;

			int slot = SLOT_SKILL + key - KEY_SKILL;
			if (key >= KEY_AIM)
				slot = SLOT_AIM + key - KEY_AIM;

			changed = SetValue(factionKey, slot, value);
			refresh = changed;
			if (changed)
				PrintFormat("[EXPBG AI SKILLS] faction=%1 slot=%2 value=%3", factionKey, slot, ClampSlotValue(slot, value));
		}
		else if (key == KEY_ROE)
		{
			value = Math.ClampInt(value, ROE_VANILLA, ROE_WARNING_SHOTS);
			changed = value != s_iRoe;
			s_iRoe = value;
			refresh = changed;
		}
		else if (key == KEY_AMMO)
		{
			value = Math.ClampInt(value, AMMO_VANILLA, AMMO_REFILL);
			changed = value != s_iAmmo;
			s_iAmmo = value;
			refresh = changed;
		}
		else if (key == KEY_REFILLS)
		{
			value = Math.ClampInt(value, REFILLS_MIN, REFILLS_MAX);
			changed = value != s_iRefills;
			s_iRefills = value;
			refresh = changed;
		}

		if (!changed)
			return;

		if (key == KEY_ROE || key == KEY_AMMO || key == KEY_REFILLS)
			PrintFormat("[EXPBG AI SKILLS] roe=%1 ammo=%2 refills=%3", s_iRoe, s_iAmmo, s_iRefills);

		EGS_Manager.RequestPublish();
		if (refresh)
			EGS_Manager.RequestFullRefresh();
	}

	//------------------------------------------------------------------------------------------------
	//! Flatten the detected factions for replication to the module entities.
	static void ExportReplicated(notnull array<string> outKeys, notnull array<int> outValues)
	{
		EXPBG_LazyStatics_EGS_Settings();
		EnsureFactions();
		outKeys.Clear();
		outValues.Clear();
		foreach (string key : s_aFactionKeys)
		{
			outKeys.Insert(key);
			for (int slot = 0; slot < SLOT_COUNT; slot++)
			{
				outValues.Insert(GetValue(key, slot));
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Only touched factions are saved, keyed by faction key.
	static void ExportPersistent(notnull array<string> outKeys, notnull array<int> outValues)
	{
		EXPBG_LazyStatics_EGS_Settings();
		Ensure();
		outKeys.Clear();
		outValues.Clear();
		foreach (string key, array<int> values : s_mValues)
		{
			if (!values || values.Count() != SLOT_COUNT)
				continue;

			bool touched;
			foreach (int value : values)
			{
				if (value != 0)
					touched = true;
			}

			if (!touched)
				continue;

			outKeys.Insert(key);
			outValues.InsertAll(values);
		}
	}

	//------------------------------------------------------------------------------------------------
	static bool IsVanilla()
	{
		EXPBG_LazyStatics_EGS_Settings();
		Ensure();
		if (s_iRoe != ROE_VANILLA || s_iAmmo != AMMO_VANILLA || s_iRefills != REFILLS_DEFAULT)
			return false;

		foreach (string key, array<int> values : s_mValues)
		{
			foreach (int value : values)
			{
				if (value != 0)
					return false;
			}
		}

		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Server only, from the mission save. Unknown factions are kept (harmless) so that a save
	//! made with a faction mod still restores once that mod is loaded again.
	static void ImportPersistent(notnull array<string> keys, notnull array<int> values, int roe, int ammo, int refills)
	{
		EXPBG_LazyStatics_EGS_Settings();
		Ensure();
		s_mValues.Clear();
		int count = Math.MinInt(keys.Count(), MAX_FACTIONS);
		for (int i = 0; i < count; i++)
		{
			string key = keys[i];
			if (key.IsEmpty())
				continue;

			for (int slot = 0; slot < SLOT_COUNT; slot++)
			{
				int index = i * SLOT_COUNT + slot;
				if (values.IsIndexValid(index))
					SetValue(key, slot, values[index]);
			}
		}

		s_iRoe = Math.ClampInt(roe, ROE_VANILLA, ROE_WARNING_SHOTS);
		s_iAmmo = Math.ClampInt(ammo, AMMO_VANILLA, AMMO_REFILL);
		s_iRefills = Math.ClampInt(refills, REFILLS_MIN, REFILLS_MAX);
		PrintFormat("[EXPBG AI SKILLS] loaded factions=%1 roe=%2 ammo=%3 refills=%4", count, s_iRoe, s_iAmmo, s_iRefills);
		EGS_Manager.RequestPublish();
		EGS_Manager.RequestFullRefresh();
	}

	//------------------------------------------------------------------------------------------------
	//! Session-attribute export (CDF and other attribute-based Game Master savers): the n-th
	//! touched faction as (faction index, packed skill slots, packed aim slots). Packed values
	//! stay below 2^24, so they survive float storage exactly.
	static bool ExportSavedFaction(int savedSlot, out vector saved)
	{
		EXPBG_LazyStatics_EGS_Settings();
		EnsureFactions();
		int found;
		foreach (int factionIndex, string key : s_aFactionKeys)
		{
			array<int> values = GetFactionValues(key);
			if (!values || values.Count() != SLOT_COUNT)
				continue;

			int skills = PackSlots(values, SLOT_SKILL, 8);
			int aims = PackSlots(values, SLOT_AIM, 16);
			if (skills == 0 && aims == 0)
				continue;

			if (found == savedSlot)
			{
				saved = Vector(factionIndex, skills, aims);
				return true;
			}

			found++;
		}

		return false;
	}

	//------------------------------------------------------------------------------------------------
	static void ImportSavedFaction(vector saved)
	{
		if (!Replication.IsServer())
			return;

		int factionIndex = Math.Round(saved[0]);
		string key = GetFactionKey(factionIndex);
		if (key.IsEmpty())
			return;

		int skills = Math.Round(saved[1]);
		int aims = Math.Round(saved[2]);
		array<int> values = {};
		for (int i = 0; i < SLOT_COUNT; i++)
		{
			values.Insert(0);
		}

		UnpackSlots(skills, values, SLOT_SKILL, 8);
		UnpackSlots(aims, values, SLOT_AIM, 16);
		for (int slot = 0; slot < SLOT_COUNT; slot++)
		{
			SetValue(key, slot, values[slot]);
		}

		EGS_Manager.RequestPublish();
		EGS_Manager.RequestFullRefresh();
	}

	//------------------------------------------------------------------------------------------------
	//! Five slots from baseSlot as base-radix digits (first slot = least significant digit).
	static int PackSlots(notnull array<int> values, int baseSlot, int radix)
	{
		int packed;
		for (int i = SLOT_AIM - 1; i >= 0; i--)
		{
			packed = packed * radix + Math.ClampInt(values[baseSlot + i], 0, radix - 1);
		}

		return packed;
	}

	//------------------------------------------------------------------------------------------------
	static void UnpackSlots(int packed, notnull array<int> values, int baseSlot, int radix)
	{
		for (int i = 0; i < SLOT_AIM; i++)
		{
			values[baseSlot + i] = ClampSlotValue(baseSlot + i, packed % radix);
			packed = packed / radix;
		}
	}

	//------------------------------------------------------------------------------------------------
	static void ImportSavedGlobal(vector saved)
	{
		if (!Replication.IsServer())
			return;

		Ensure();
		int roe = Math.Round(saved[0]);
		int ammo = Math.Round(saved[1]);
		int refills = Math.Round(saved[2]);
		s_iRoe = Math.ClampInt(roe, ROE_VANILLA, ROE_WARNING_SHOTS);
		s_iAmmo = Math.ClampInt(ammo, AMMO_VANILLA, AMMO_REFILL);
		s_iRefills = Math.ClampInt(refills, REFILLS_MIN, REFILLS_MAX);
		EGS_Manager.RequestPublish();
		EGS_Manager.RequestFullRefresh();
	}

	//------------------------------------------------------------------------------------------------
	static bool HasRoleOverrides(array<int> values)
	{
		if (!values || values.Count() < SLOT_COUNT)
			return false;

		for (int role = 0; role < ROLE_COUNT; role++)
		{
			if (values[SLOT_SKILL + 1 + role] > 0 || values[SLOT_AIM + 1 + role] > 0)
				return true;
		}

		return false;
	}

	//------------------------------------------------------------------------------------------------
	//! Leader override first (when the unit leads), then its weapon role, then the faction-wide value.
	static int ResolveIndex(array<int> values, int baseSlot, int weaponRole, bool leader)
	{
		if (!values || values.Count() < SLOT_COUNT)
			return 0;

		if (leader)
		{
			int leaderValue = values[baseSlot + 1 + ROLE_LEADER];
			if (leaderValue > 0)
				return leaderValue;
		}

		int roleSlot = baseSlot + 1 + Math.ClampInt(weaponRole, ROLE_RIFLEMAN, ROLE_MARKSMAN);
		if (values[roleSlot] > 0)
			return values[roleSlot];

		return values[baseSlot];
	}

	//------------------------------------------------------------------------------------------------
	//! Vanilla skill tier used by the aiming error (EAISkill.NONE = keep the unit's own skill).
	static EAISkill SkillFromIndex(int index)
	{
		switch (index)
		{
			case 1: return EAISkill.NOOB;
			case 2: return EAISkill.ROOKIE;
			case 3: return EAISkill.REGULAR;
			case 4: return EAISkill.VETERAN;
			case 5: return EAISkill.EXPERT;
		}

		return EAISkill.NONE;
	}

	//------------------------------------------------------------------------------------------------
	//! Spotting speed multiplier that goes with a skill tier (1 = vanilla).
	static float PerceptionFromIndex(int index)
	{
		switch (index)
		{
			case 1: return 0.6;
			case 2: return 0.8;
			case 3: return 1.0;
			case 4: return 1.2;
			case 5: return 1.4;
		}

		return 1.0;
	}

	//------------------------------------------------------------------------------------------------
	//! Aim accuracy in percent of vanilla (0 = vanilla).
	static int AccuracyPercentFromIndex(int index)
	{
		switch (index)
		{
			case 1: return 25;
			case 2: return 50;
			case 3: return 75;
			case 4: return 100;
			case 5: return 150;
			case 6: return 200;
			case 7: return 300;
			case 8: return 400;
		}

		return 100;
	}

	//------------------------------------------------------------------------------------------------
	//! Multiplier applied to the vanilla aiming error: higher accuracy means a smaller error.
	static float AimErrorScaleFromIndex(int index)
	{
		return 100.0 / AccuracyPercentFromIndex(index);
	}

	//------------------------------------------------------------------------------------------------
	//! Effective rules of engagement for a group (pure, used by the manager and the fixture).
	static int EffectiveRoe(bool active, int groupOverride, int globalRoe)
	{
		if (!active || groupOverride == GROUP_ROE_EXEMPT)
			return ROE_VANILLA;

		if (groupOverride >= ROE_RETURN_FIRE && groupOverride <= ROE_WARNING_SHOTS)
			return groupOverride;

		return Math.ClampInt(globalRoe, ROE_VANILLA, ROE_WARNING_SHOTS);
	}

	//------------------------------------------------------------------------------------------------
	//! Faction label for the Game Master spinbox (translated name plus key, so mods with similar
	//! display names stay distinguishable).
	static string FactionDisplayName(string factionKey)
	{
		string name;
		if (GetGame() && GetGame().GetFactionManager())
		{
			Faction faction = GetGame().GetFactionManager().GetFactionByKey(factionKey);
			if (faction)
				name = WidgetManager.Translate(faction.GetFactionName());
		}

		if (name.IsEmpty() || name == factionKey)
			return factionKey;

		return name + " [" + factionKey + "]";
	}

	//------------------------------------------------------------------------------------------------
	//! Creates the collections on first use (not in the global static initializer, which has a
	//! per-function instruction limit that large modsets exceed on Windows).
	protected static void EXPBG_LazyStatics_EGS_Settings()
	{
		if (!s_aFactionKeys)
			s_aFactionKeys = new array<string>();
		if (!s_mValues)
			s_mValues = new map<string, ref array<int>>();
	}
}
