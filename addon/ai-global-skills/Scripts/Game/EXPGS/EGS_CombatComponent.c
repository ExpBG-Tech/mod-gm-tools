// Per-soldier EXPBG profile (server). Nothing here writes the vanilla skill or perception
// fields, so vanilla persistence keeps saving the unit's own values and "vanilla" settings
// leave the unit untouched:
// - skill tier and aim accuracy are read by the aiming-error node (EGS_AimErrorOffset.c);
// - spotting speed is folded into the vanilla perception update;
// - magazines of the primary firearm are refilled after inventory changes (no grenades;
//   weapons without a magazine template, such as the RHS M40A5, use the magazine they carry);
// - warning shots use the vanilla suppress behaviour against a point next to the player.
modded class SCR_AICombatComponent
{
	protected static const int EGS_MAX_MAGAZINES = 12;
	protected static const int EGS_AMMO_CHECK_DELAY_MS = 1500;

	protected EAISkill m_eEGS_Skill = EAISkill.NONE;
	protected float m_fEGS_AimErrorScale = 1.0;
	protected float m_fEGS_Perception = 1.0;
	protected bool m_bEGS_Profiled;

	protected int m_iEGS_AmmoMode;
	protected int m_iEGS_RefillSetting;
	protected int m_iEGS_RefillsLeft;
	protected int m_iEGS_MagazineBaseline;
	protected bool m_bEGS_AmmoCheckQueued;
	// Magazine well -> magazine prefab, only for weapons without a magazine template.
	protected ref map<typename, ResourceName> m_mEGS_SeenMagazines;

	protected SCR_AISuppressBehavior m_EGS_WarningBehavior;
	protected EventHandlerManagerComponent m_EGS_WarningEvents;
	protected SCR_AIGroup m_EGS_WarningGroup;
	protected int m_iEGS_WarningToken;
	protected int m_iEGS_WarningShots;

	//------------------------------------------------------------------------------------------------
	//! Server: apply the resolved settings of this soldier.
	void EGS_SetProfile(EAISkill skill, float perception, float aimErrorScale)
	{
		m_eEGS_Skill = skill;
		m_fEGS_Perception = Math.Clamp(perception, 0.1, 4);
		m_fEGS_AimErrorScale = Math.Clamp(aimErrorScale, 0.05, 8);
		m_bEGS_Profiled = skill != EAISkill.NONE || m_fEGS_Perception != 1.0 || m_fEGS_AimErrorScale != 1.0;
	}

	//------------------------------------------------------------------------------------------------
	//! Server: back to vanilla (module removed, player took control or settings cleared).
	void EGS_ResetProfile()
	{
		EGS_SetProfile(EAISkill.NONE, 1.0, 1.0);
		EGS_SetAmmoPolicy(EGS_Settings.AMMO_VANILLA, 0);
		EGS_EndWarningShots(false);
	}

	//------------------------------------------------------------------------------------------------
	bool EGS_HasProfile()
	{
		return m_bEGS_Profiled || m_iEGS_AmmoMode != EGS_Settings.AMMO_VANILLA;
	}

	//------------------------------------------------------------------------------------------------
	//! Skill tier used for aiming: the EXPBG tier when set, otherwise the unit's own.
	EAISkill EGS_ResolveSkill(EAISkill skill)
	{
		if (m_eEGS_Skill != EAISkill.NONE)
			return m_eEGS_Skill;

		return skill;
	}

	//------------------------------------------------------------------------------------------------
	float EGS_GetAimErrorScale()
	{
		return m_fEGS_AimErrorScale;
	}

	//------------------------------------------------------------------------------------------------
	//! Spotting speed: the vanilla factor is temporarily scaled for this update only, so the
	//! value read by vanilla persistence (GetPerceptionFactor) is never changed.
	override void UpdatePerceptionFactor(PerceptionComponent perceptionComp, SCR_AIThreatSystem threatSystem)
	{
		if (m_fEGS_Perception == 1.0)
		{
			super.UpdatePerceptionFactor(perceptionComp, threatSystem);
			return;
		}

		float ownFactor = m_fPerceptionFactor;
		m_fPerceptionFactor = ownFactor * m_fEGS_Perception;
		super.UpdatePerceptionFactor(perceptionComp, threatSystem);
		m_fPerceptionFactor = ownFactor;
	}

	//------------------------------------------------------------------------------------------------
	//! Warning shots: report a newly selected hostile to the group's rules of engagement.
	override void EvaluateWeaponAndTarget(out bool outWeaponEvent, out bool outSelectedTargetChanged,
		out BaseTarget outPrevTarget, out BaseTarget outCurrentTarget,
		out bool outRetreatTargetChanged, out bool outCompartmentChanged)
	{
		super.EvaluateWeaponAndTarget(outWeaponEvent, outSelectedTargetChanged, outPrevTarget, outCurrentTarget, outRetreatTargetChanged, outCompartmentChanged);
		if (!outSelectedTargetChanged || !outCurrentTarget)
			return;

		AIAgent agent = GetAiAgent();
		if (!agent)
			return;

		SCR_AIGroup group = SCR_AIGroup.Cast(agent.GetParentGroup());
		if (group && group.EGS_GetAppliedRoe() == EGS_Settings.ROE_WARNING_SHOTS)
			group.EGS_OnHostileSelected(this, outCurrentTarget.GetTargetEntity());
	}

	//------------------------------------------------------------------------------------------------
	//! Server: fire a short burst at a point beside the target with the vanilla suppress
	//! behaviour (own weapon, sound and animation). Stops after EGS_Manager.WARNING_ROUNDS.
	bool EGS_StartWarningShots(IEntity target, SCR_AIGroup group, int token)
	{
		IEntity owner = GetOwner();
		if (!owner || !target || !m_Utility || m_CurrentVehicle)
			return false;

		EGS_EndWarningShots(false);
		vector point = EGS_Manager.WarningPoint(owner.GetOrigin(), target.GetOrigin());
		// Aim sphere at least 3 m (XZ) from the shooter, its sweep clear of the target
		// (WarningOffset); vanilla aim heights at and beyond its rim are kept finite by
		// EGS_SuppressionVolume.c.
		SCR_AISuppressionVolumeSphere volume = new SCR_AISuppressionVolumeSphere(point, EGS_Manager.WARNING_RADIUS);
		SCR_AISuppressBehavior behavior = new SCR_AISuppressBehavior(m_Utility, null, volume, EGS_Manager.WARNING_WINDOW_S, EGS_Manager.WARNING_FIRE_RATE, SCR_AIActionBase.PRIORITY_LEVEL_PLAYER);
		m_Utility.AddAction(behavior);
		m_EGS_WarningBehavior = behavior;
		m_EGS_WarningGroup = group;
		m_iEGS_WarningToken = token;
		m_iEGS_WarningShots = 0;

		m_EGS_WarningEvents = EventHandlerManagerComponent.Cast(owner.FindComponent(EventHandlerManagerComponent));
		if (m_EGS_WarningEvents)
			m_EGS_WarningEvents.RegisterScriptHandler("OnProjectileShot", this, EGS_OnWarningShot);

		return true;
	}

	//------------------------------------------------------------------------------------------------
	protected void EGS_OnWarningShot(int playerID, BaseWeaponComponent weapon, IEntity entity)
	{
		m_iEGS_WarningShots++;
		if (m_iEGS_WarningShots >= EGS_Manager.WARNING_ROUNDS)
			EGS_EndWarningShots(true);
	}

	//------------------------------------------------------------------------------------------------
	void EGS_EndWarningShots(bool notifyGroup)
	{
		if (m_EGS_WarningEvents)
		{
			m_EGS_WarningEvents.RemoveScriptHandler("OnProjectileShot", this, EGS_OnWarningShot);
			m_EGS_WarningEvents = null;
		}

		if (m_EGS_WarningBehavior)
		{
			m_EGS_WarningBehavior.Complete();
			m_EGS_WarningBehavior = null;
		}

		SCR_AIGroup group = m_EGS_WarningGroup;
		m_EGS_WarningGroup = null;
		if (notifyGroup && group)
			group.EGS_OnWarningShotsDone(m_iEGS_WarningToken);
	}

	//------------------------------------------------------------------------------------------------
	//! Server: ammunition policy. Changing mode or count restarts the refill budget.
	void EGS_SetAmmoPolicy(int mode, int refills)
	{
		if (mode == m_iEGS_AmmoMode && refills == m_iEGS_RefillSetting)
			return;

		m_iEGS_AmmoMode = mode;
		m_iEGS_RefillSetting = refills;
		m_iEGS_RefillsLeft = refills;
		if (mode != EGS_Settings.AMMO_VANILLA)
			EGS_QueueAmmoCheck();
	}

	//------------------------------------------------------------------------------------------------
	//! Inventory changes are noisy: one deferred check per burst, magazines and weapons only.
	override protected void Event_OnInventoryChanged(IEntity item, BaseInventoryStorageComponent storageOwner)
	{
		super.Event_OnInventoryChanged(item, storageOwner);
		if (!item || !Replication.IsServer() || !GetGame().InPlayMode())
			return;

		if (m_iEGS_AmmoMode != EGS_Settings.AMMO_VANILLA && item.FindComponent(BaseMagazineComponent))
			EGS_QueueAmmoCheck();

		// A changed loadout can change the role (for example a picked-up machine gun).
		if (item.FindComponent(BaseWeaponComponent) && EGS_Manager.IsActive())
			EGS_Manager.QueueUnit(GetOwner());
	}

	//------------------------------------------------------------------------------------------------
	protected void EGS_QueueAmmoCheck()
	{
		if (m_bEGS_AmmoCheckQueued)
			return;

		m_bEGS_AmmoCheckQueued = true;
		GetGame().GetCallqueue().CallLater(EGS_CheckAmmo, EGS_AMMO_CHECK_DELAY_MS, false);
	}

	//------------------------------------------------------------------------------------------------
	//! Unlimited: keep the spare magazines at the highest count seen.
	//! Refill: when no spare magazine is left, restore that count, up to N times.
	protected void EGS_CheckAmmo()
	{
		m_bEGS_AmmoCheckQueued = false;
		if (m_iEGS_AmmoMode == EGS_Settings.AMMO_VANILLA || !m_InventoryManager || !m_CharacterController || m_CharacterController.IsDead())
			return;

		if (EGS_Manager.IsPlayerControlled(GetOwner()))
			return;

		BaseMuzzleComponent muzzle;
		ResourceName magazine;
		if (!EGS_FindRefillMuzzle(muzzle, magazine))
			return;

		int spares = m_InventoryManager.GetMagazineCountByMuzzle(null, muzzle);
		if (spares > m_iEGS_MagazineBaseline)
			m_iEGS_MagazineBaseline = Math.MinInt(spares, EGS_MAX_MAGAZINES);

		if (m_iEGS_MagazineBaseline < 1)
			m_iEGS_MagazineBaseline = 1;

		int missing = m_iEGS_MagazineBaseline - spares;
		if (missing <= 0)
			return;

		if (m_iEGS_AmmoMode == EGS_Settings.AMMO_REFILL)
		{
			if (spares > 0 || m_iEGS_RefillsLeft <= 0)
				return;

			m_iEGS_RefillsLeft--;
		}

		map<ResourceName, int> refill = new map<ResourceName, int>();
		refill.Insert(magazine, missing);
		m_InventoryManager.ResupplyMagazines(refill);
	}

	//------------------------------------------------------------------------------------------------
	//! Primary firearm magazine: long gun first, handgun otherwise; base muzzle only, so
	//! underbarrel grenades, launchers and throwables are never refilled. A firearm whose
	//! magazine cannot be told (EGS_ResolveMagazine) gives way to the next one, so a sniper
	//! with an unknown rifle magazine still gets handgun magazines instead of nothing.
	protected bool EGS_FindRefillMuzzle(out BaseMuzzleComponent outMuzzle, out ResourceName outMagazine)
	{
		if (!m_WpnManager)
			return false;

		array<IEntity> weapons = {};
		m_WpnManager.GetWeaponsList(weapons);
		array<BaseWeaponComponent> firearms = {};
		array<int> ranks = {};
		int topRank = 0;
		foreach (IEntity weaponEntity : weapons)
		{
			if (!weaponEntity)
				continue;

			BaseWeaponComponent weapon = BaseWeaponComponent.Cast(weaponEntity.FindComponent(BaseWeaponComponent));
			if (!weapon)
				continue;

			int rank = EGS_Roles.FirearmRank(weapon.GetWeaponType());
			if (rank <= 0)
				continue;

			firearms.Insert(weapon);
			ranks.Insert(rank);
			if (rank > topRank)
				topRank = rank;
		}

		// Highest rank first; equal ranks keep the weapon-slot order, so the firearm picked
		// first is the one picked before whenever its magazine is known.
		for (int wanted = topRank; wanted > 0; wanted--)
		{
			foreach (int position, BaseWeaponComponent firearm : firearms)
			{
				if (ranks[position] == wanted && EGS_FindWeaponMagazine(firearm, outMuzzle, outMagazine))
					return true;
			}
		}

		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected bool EGS_FindWeaponMagazine(BaseWeaponComponent weapon, out BaseMuzzleComponent outMuzzle, out ResourceName outMagazine)
	{
		array<BaseMuzzleComponent> muzzles = {};
		weapon.GetMuzzlesList(muzzles);
		foreach (BaseMuzzleComponent muzzle : muzzles)
		{
			if (!muzzle || muzzle.GetMuzzleType() != EMuzzleType.MT_BaseMuzzle)
				continue;

			SCR_MuzzleInMagComponent inMagazine = SCR_MuzzleInMagComponent.Cast(muzzle);
			if (inMagazine && !inMagazine.CanBeReloaded())
				continue;

			ResourceName magazine = EGS_ResolveMagazine(muzzle);
			if (magazine.IsEmpty())
				continue;

			outMuzzle = muzzle;
			outMagazine = magazine;
			return true;
		}

		return false;
	}

	//------------------------------------------------------------------------------------------------
	//! The muzzle's magazine template when it has one (vanilla, unchanged). Weapons that ship
	//! with an empty template (RHS M40A5: MagazineTemplate "") use the magazine they have
	//! loaded, then the one last seen for their magazine well (a check during a reload has no
	//! loaded magazine), then a matching spare from the inventory.
	protected ResourceName EGS_ResolveMagazine(BaseMuzzleComponent muzzle)
	{
		ResourceName magazine = muzzle.GetDefaultMagazineOrProjectileName();
		if (!magazine.IsEmpty())
			return magazine;

		// A plain BaseMagazineWell would match any magazine (RHS spectrum devices): no key.
		typename wellType = BaseMagazineWell;
		BaseMagazineWell well = muzzle.GetMagazineWell();
		if (well)
			wellType = well.Type();

		bool keyed = wellType != BaseMagazineWell;

		magazine = EGS_MagazinePrefab(muzzle.GetMagazine());
		if (magazine.IsEmpty() && keyed && m_mEGS_SeenMagazines)
			m_mEGS_SeenMagazines.Find(wellType, magazine);

		if (magazine.IsEmpty() && keyed && m_InventoryManager)
		{
			SCR_MagazinePredicate predicate = new SCR_MagazinePredicate();
			predicate.magWellType = wellType;
			IEntity spare = m_InventoryManager.FindItem(predicate);
			if (spare)
				magazine = EGS_MagazinePrefab(BaseMagazineComponent.Cast(spare.FindComponent(BaseMagazineComponent)));
		}

		if (magazine.IsEmpty() || !keyed)
			return magazine;

		if (!m_mEGS_SeenMagazines)
			m_mEGS_SeenMagazines = new map<typename, ResourceName>();

		m_mEGS_SeenMagazines.Set(wellType, magazine);
		return magazine;
	}

	//------------------------------------------------------------------------------------------------
	protected static ResourceName EGS_MagazinePrefab(BaseMagazineComponent magazineComponent)
	{
		if (!magazineComponent)
			return ResourceName.Empty;

		IEntity magazineEntity = magazineComponent.GetOwner();
		if (!magazineEntity)
			return ResourceName.Empty;

		EntityPrefabData prefabData = magazineEntity.GetPrefabData();
		if (!prefabData)
			return ResourceName.Empty;

		return prefabData.GetPrefabName();
	}

	//------------------------------------------------------------------------------------------------
	override void OnDelete(IEntity owner)
	{
		EGS_EndWarningShots(false);
		if (GetGame() && GetGame().GetCallqueue())
			GetGame().GetCallqueue().Remove(EGS_CheckAmmo);

		super.OnDelete(owner);
	}
}
