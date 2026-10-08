// EXPBG AI Global Skills: the "fired upon" rule of Return Fire Only and of Warning Shots
// First before it turns lethal (EGS_Group.c, EGS_UnitRoe.c).
// Vanilla RETURN_FIRE opens fire once any enemy is flagged as endangering the group, and only
// the squad leader's gunshot events flag one (SCR_AIDangerReaction_WeaponFired): a shot whose
// line passes within 13 m of the leader (whatever it was aimed at) or an enemy gunshot within
// 15 m of him; an enemy grenade landing near any member also counts. A hit on a member does
// not, so a vanilla squad shot at only through its other members can stay silent. The flag is
// never cleared during the contact. EXPBG squads with these rules of engagement instead hold
// fire until they are provoked, through any member:
// - a member is hit by an enemy (vanilla damage-taken danger event);
// - a member is killed or knocked out and the last instigator of his damage is an enemy;
// - an enemy round passes within NEAR_MISS_RADIUS of a member, measured from the shot's
//   path and only in front of the muzzle;
// - an enemy explosion goes off within EXPLOSION_RADIUS of a member.
// The squad then fires at will until EGS_Manager.REARM_DELAY_S passed since the last
// provocation and no member has a target (checked again every REARM_RETRY_S on the shared
// EGS_Manager tick, SCR_AIGroup.EGS_OnCalmTimer).
// Cost: each hook first reads one flag of the listener's squad (watched: EXPBG Return Fire
// Only or Warning Shots First, or a member with one of these as his own ROE) and returns for
// every other squad; the near-miss test is one dot product. One log line per provocation and
// one when the contact is over. The hooks call super first and never change what vanilla
// (or another addon, such as EXPBG_RO_AI, overriding the same reactions) does with an event.
class EGS_Provocation
{
	static const float NEAR_MISS_RADIUS = 4.0;
	static const float EXPLOSION_RADIUS = 10.0;
	// Rounds are aimed at the body, not at the character origin on the ground.
	static const float BODY_HEIGHT = 1.0;

	//------------------------------------------------------------------------------------------------
	//! Pure: true when a shot fired from muzzle along direction passes within radius of
	//! point, counting only the path in front of the muzzle (also used by the fixture).
	static bool IsNearMiss(vector point, vector muzzle, vector direction, float radius)
	{
		float length = direction.Length();
		if (length < 0.001 || radius <= 0)
			return false;

		vector unit = direction * (1.0 / length);
		float along = vector.Dot(point - muzzle, unit);
		if (along <= 0)
			return false;

		vector closest = muzzle + unit * along;
		return vector.DistanceSq(point, closest) <= radius * radius;
	}

	//------------------------------------------------------------------------------------------------
	//! The listener's squad when it is watched for provocation, else null.
	static SCR_AIGroup WatchingGroup(SCR_AIUtilityComponent utility)
	{
		if (!utility || !utility.m_OwnerEntity)
			return null;

		AIAgent agent = utility.GetOwner();
		if (!agent)
			return null;

		SCR_AIGroup group = SCR_AIGroup.Cast(agent.GetParentGroup());
		if (!group || !group.EGS_WatchesProvocation())
			return null;

		return group;
	}

	//------------------------------------------------------------------------------------------------
	//! Perceived faction of an entity, else of the vehicle or turret root it belongs to.
	static Faction PerceivedFaction(IEntity entity)
	{
		if (!entity)
			return null;

		Faction faction = SCR_AIFactionHandling.GetEntityPerceivedFaction(entity);
		if (faction)
			return faction;

		IEntity root = entity.GetRootParent();
		if (!root || root == entity)
			return null;

		return SCR_AIFactionHandling.GetEntityPerceivedFaction(root);
	}

	//------------------------------------------------------------------------------------------------
	//! True when the listening soldier perceives the entity as an enemy.
	static bool IsEnemyOf(SCR_AIUtilityComponent utility, IEntity entity)
	{
		if (!utility || !entity)
			return false;

		SCR_ChimeraAIAgent agent = SCR_ChimeraAIAgent.Cast(utility.GetOwner());
		if (!agent)
			return false;

		Faction other = PerceivedFaction(entity);
		return agent.IsEnemy(other);
	}

	//------------------------------------------------------------------------------------------------
	//! True when the squad's faction is hostile to the entity's perceived faction.
	static bool IsEnemyOfGroup(SCR_AIGroup group, IEntity entity)
	{
		if (!group || !entity)
			return false;

		Faction own = group.GetFaction();
		Faction other = PerceivedFaction(entity);
		if (!own || !other)
			return false;

		return own.IsFactionEnemy(other);
	}

	//------------------------------------------------------------------------------------------------
	//! A gunshot event reached a member: provoked when an enemy round passes close to him.
	static void OnWeaponFired(SCR_AIUtilityComponent utility, AIDangerEventWeaponFire fire)
	{
		if (!fire)
			return;

		SCR_AIGroup group = WatchingGroup(utility);
		if (!group)
			return;

		IEntity shooter = fire.GetInstigatorEntity();
		if (!shooter || !IsEnemyOf(utility, shooter))
			return;

		vector body = utility.m_OwnerEntity.GetOrigin() + Vector(0, BODY_HEIGHT, 0);
		if (IsNearMiss(body, fire.GetPosition(), fire.GetDirection(), NEAR_MISS_RADIUS))
			group.EGS_Provoke("near miss", shooter);
	}

	//------------------------------------------------------------------------------------------------
	//! A member took damage: provoked when an enemy caused it.
	static void OnDamageTaken(SCR_AIUtilityComponent utility, AIDangerEvent dangerEvent)
	{
		if (!dangerEvent)
			return;

		SCR_AIGroup group = WatchingGroup(utility);
		if (!group || dangerEvent.GetVictim() != utility.m_OwnerEntity)
			return;

		IEntity shooter = dangerEvent.GetObject();
		if (shooter && IsEnemyOf(utility, shooter))
			group.EGS_Provoke("member hit", shooter);
	}

	//------------------------------------------------------------------------------------------------
	//! An explosion event reached a member: provoked when an enemy's goes off close to him.
	static void OnExplosion(SCR_AIUtilityComponent utility, AIDangerEvent dangerEvent)
	{
		if (!dangerEvent)
			return;

		SCR_AIGroup group = WatchingGroup(utility);
		if (!group)
			return;

		if (vector.DistanceSq(utility.m_OwnerEntity.GetOrigin(), dangerEvent.GetPosition()) > EXPLOSION_RADIUS * EXPLOSION_RADIUS)
			return;

		IEntity source = dangerEvent.GetObject();
		if (source && IsEnemyOf(utility, source))
			group.EGS_Provoke("explosion", source);
	}

	//------------------------------------------------------------------------------------------------
	//! A member went down (killed or knocked out): provoked when the last instigator of his
	//! damage is an enemy. A Game Master's kill or neutralize has no enemy instigator.
	static void OnMemberDown(SCR_AIGroup group, AIAgent agent)
	{
		if (!group || !agent || !group.EGS_WatchesProvocation())
			return;

		ChimeraCharacter casualty = ChimeraCharacter.Cast(agent.GetControlledEntity());
		if (!casualty)
			return;

		SCR_DamageManagerComponent damage = casualty.GetDamageManager();
		if (!damage)
			return;

		Instigator instigator = damage.GetInstigator();
		if (!instigator)
			return;

		IEntity killer = instigator.GetInstigatorEntity();
		if (killer && killer != casualty && IsEnemyOfGroup(group, killer))
			group.EGS_Provoke("member down", killer);
	}
}

//------------------------------------------------------------------------------------------------
[BaseContainerProps()]
modded class SCR_AIDangerReaction_WeaponFired
{
	override bool PerformReaction(notnull SCR_AIUtilityComponent utility, notnull SCR_AIThreatSystem threatSystem, AIDangerEvent dangerEvent, int dangerEventCount)
	{
		bool handled = super.PerformReaction(utility, threatSystem, dangerEvent, dangerEventCount);
		EGS_Provocation.OnWeaponFired(utility, AIDangerEventWeaponFire.Cast(dangerEvent));
		return handled;
	}
}

//------------------------------------------------------------------------------------------------
[BaseContainerProps()]
modded class SCR_AIDangerReaction_DamageTaken
{
	override bool PerformReaction(notnull SCR_AIUtilityComponent utility, notnull SCR_AIThreatSystem threatSystem, AIDangerEvent dangerEvent, int dangerEventCount)
	{
		bool handled = super.PerformReaction(utility, threatSystem, dangerEvent, dangerEventCount);
		EGS_Provocation.OnDamageTaken(utility, dangerEvent);
		return handled;
	}
}

//------------------------------------------------------------------------------------------------
[BaseContainerProps()]
modded class SCR_AIDangerReaction_Explosion
{
	override bool PerformReaction(notnull SCR_AIUtilityComponent utility, notnull SCR_AIThreatSystem threatSystem, AIDangerEvent dangerEvent, int dangerEventCount)
	{
		bool handled = super.PerformReaction(utility, threatSystem, dangerEvent, dangerEventCount);
		EGS_Provocation.OnExplosion(utility, dangerEvent);
		return handled;
	}
}

//------------------------------------------------------------------------------------------------
//! A member killed or knocked out by an enemy provokes his squad (EGS_Provocation.OnMemberDown).
modded class SCR_AIGroupUtilityComponent
{
	override protected void OnAgentLifeStateChanged(AIAgent incapacitatedAgent, SCR_AIInfoComponent infoIncap, IEntity vehicle, ECharacterLifeState lifeState)
	{
		super.OnAgentLifeStateChanged(incapacitatedAgent, infoIncap, vehicle, lifeState);
		if (lifeState != ECharacterLifeState.ALIVE && m_Owner && m_Owner.EGS_WatchesProvocation())
			EGS_Provocation.OnMemberDown(m_Owner, incapacitatedAgent);
	}
}
