// Native current-state group transaction. Test entry remains marker-gated;
// the distinct manager entry relies on manager enrollment/reservation policy.
// Client pre-callback quarantine timing remains an acceptance requirement.
enum EBG_FullGroupPhase
{
	NEW,
	CACHED,
	RESTORING,
	READY,
	RELEASED,
	FAILED,
	RELEASING
}

modded class SCR_AIGroup
{
	bool EBG_NativeDeleteWhenEmpty() { return m_bDeleteWhenEmpty; }
	static bool EBG_NativeSpawningIgnored() { return s_bIgnoreSpawning; }
	bool EBG_NativeHasSceneReferences()
	{
		if (m_aSceneGroupUnitInstances && !m_aSceneGroupUnitInstances.IsEmpty()) return true;
		if (m_aSceneWaypointInstances && !m_aSceneWaypointInstances.IsEmpty()) return true;
		if (m_aStaticWaypoints && !m_aStaticWaypoints.IsEmpty()) return true;
		return m_aStaticVehicles && !m_aStaticVehicles.IsEmpty();
	}
}

// Native identity is not covered by Character.conf's persistence serializers.
// Retain its native identity objects and exposed runtime material state separately.
class EBG_FullIdentityMaterials
{
	bool Present;
	ref array<int> Values;
	bool Wetness;
	bool Drops;
	void Capture(IEntity entity)
	{
		ParametricMaterialInstanceComponent material = ParametricMaterialInstanceComponent.Cast(entity.FindComponent(ParametricMaterialInstanceComponent));
		Present = material != null;
		if (!material) return;
		Values = {material.GetColor(), material.GetEmissiveC(), material.GetEmissiveM(), material.GetUserParam1(), material.GetUserParam2(), material.GetUserParam3(), material.GetUserParam4(), material.GetUserAlphaTestParam(), material.GetCustomWetnessParam1(), material.GetCustomWetnessParam2()};
		Wetness = material.GetCustomWetnessEnabled();
		Drops = material.GetSlidingDropsEnabled();
	}
	bool Matches(IEntity entity)
	{
		if (!entity) return false;
		EBG_FullIdentityMaterials actual = new EBG_FullIdentityMaterials();
		actual.Capture(entity);
		if (Present != actual.Present) return false;
		if (!Present) return true;
		if (Wetness != actual.Wetness || Drops != actual.Drops || Values.Count() != actual.Values.Count()) return false;
		for (int i = 0; i < Values.Count(); i++) if (Values[i] != actual.Values[i]) return false;
		return true;
	}
	bool Restore(IEntity entity)
	{
		if (!entity) return false;
		ParametricMaterialInstanceComponent material = ParametricMaterialInstanceComponent.Cast(entity.FindComponent(ParametricMaterialInstanceComponent));
		if (!Present) return material == null;
		if (!material) return false;
		material.SetColor(Values[0]); material.SetEmissiveColor(Values[1]); material.SetEmissiveMultiplier(Values[2]);
		material.SetUserParam1(Values[3]); material.SetUserParam2(Values[4]); material.SetUserParam3(Values[5]); material.SetUserParam4(Values[6]);
		material.SetUserAlphaTestParam(Values[7]); material.SetCustomWetnessParam1(Values[8]); material.SetCustomWetnessParam2(Values[9]);
		material.SetCustomWetnessEnabled(Wetness); material.SetSlidingDropsEnabled(Drops);
		return Matches(entity);
	}
}

class EBG_FullIdentityState
{
 // Session-only native retention. These objects are never mutated by this adapter.
 // The disposable lifetime proof verified reuse after donor entity destruction.
 ref Identity Retained;
 ref VisualIdentity Visual;
 ref SoundIdentity Sound;
 ref MeshConfig Mesh;
 ref EBG_FullIdentityMaterials BodyMaterial = new EBG_FullIdentityMaterials();
 ref EBG_FullIdentityMaterials HeadMaterial = new EBG_FullIdentityMaterials();
 string Name, Alias, Surname, FullName;
 ResourceName ConfiguredHead, ConfiguredBody;
 ResourceName HeadPrefab, HeadMesh, BodyMesh;
 int Voice, IntegerPitch;
 bool MaterialsRestored;
 string Error;

 protected bool SameIdentity(Identity actual)
 {
  if (!Retained || !Visual || !Sound || actual != Retained) { Error = "Actual identity is not the retained native object"; return false; }
  if (Retained.GetVisualIdentity() != Visual || Retained.GetSoundIdentity() != Sound || Visual.GetMeshConfig() != Mesh) { Error = "Retained native nested identity references changed"; return false; }
  if (Retained.GetName() != Name) { Error = "Retained native first name changed"; return false; }
  if (Retained.GetAlias() != Alias) { Error = "Retained native alias changed"; return false; }
  if (Retained.GetSurname() != Surname || Retained.GetFullName() != FullName) { Error = "Retained native surname/full name changed"; return false; }
  if (Visual.GetHead() != ConfiguredHead || Visual.GetBody() != ConfiguredBody) { Error = "Retained native configured head/body changed"; return false; }
  if (Sound.GetVoiceID() != Voice || Sound.GetPitch() != IntegerPitch) { Error = "Retained native voice/pitch getter changed"; return false; }
  // The native integer pitch getter is not a float snapshot. The actual Sound
  // object, including its private float pitch, is retained without conversion.
  return true;
 }
 bool Capture(SCR_ChimeraCharacter member)
 {
  if (!member) { Error = "Character unavailable for identity capture"; return false; }
  CharacterIdentityComponent component = CharacterIdentityComponent.Cast(member.FindComponent(CharacterIdentityComponent));
  if (!component || !component.GetIdentity()) { Error = "Native identity unavailable"; return false; }
  Retained = component.GetIdentity();
  Visual = Retained.GetVisualIdentity(); Sound = Retained.GetSoundIdentity();
  if (!Visual || !Sound) { Error = "Complete native visual/sound identity unavailable"; return false; }
  Mesh = Visual.GetMeshConfig();
  Name = Retained.GetName(); Alias = Retained.GetAlias(); Surname = Retained.GetSurname(); FullName = Retained.GetFullName();
  ConfiguredHead = Visual.GetHead(); ConfiguredBody = Visual.GetBody(); Voice = Sound.GetVoiceID(); IntegerPitch = Sound.GetPitch();
  IEntity head = component.GetHeadEntity();
  if (!head || head.GetParent() != member || head.GetChildren() || !head.GetVObject() || !member.GetVObject()) { Error = "Native head/body visual ownership unavailable"; return false; }
  HeadPrefab = SCR_ResourceNameUtils.GetPrefabName(head);
  if (HeadPrefab != ConfiguredHead) { Error = "Actual head differs from configured head; unsupported identity override"; return false; }
  HeadMesh = head.GetVObject().GetResourceName(); BodyMesh = member.GetVObject().GetResourceName();
  BodyMaterial.Capture(member); HeadMaterial.Capture(head);
#ifdef EBG_ACCEPTANCE_TEST
  PrintFormat("[EBG Full Group IDENTITY CAPTURE] member=%1 head=%2 body=%3 fullName=%4 voice=%5 integerPitch=%6 retainedNativeObjects=1 privateFloatNotConverted=1", member.GetID(), HeadPrefab, ConfiguredBody, FullName, Voice, IntegerPitch);
#endif
  return Matches(member);
 }
 bool Restore(SCR_ChimeraCharacter member)
 {
  if (!member || !SameIdentity(Retained)) return false;
  CharacterIdentityComponent component = CharacterIdentityComponent.Cast(member.FindComponent(CharacterIdentityComponent));
  if (!component) { Error = "Restored identity component unavailable"; return false; }
  // Explicit retry reuses the exact retained object; it never selects an identity.
  component.SetIdentity(Retained);
  component.CommitChanges();
  MaterialsRestored = false;
  return SameIdentity(component.GetIdentity());
 }
 bool Settle(SCR_ChimeraCharacter member)
 {
  if (!member) { Error = "Restored character unavailable during identity settle"; return false; }
  CharacterIdentityComponent component = CharacterIdentityComponent.Cast(member.FindComponent(CharacterIdentityComponent));
  if (!component) { Error = "Identity component missing during settle"; return false; }
  IEntity head = component.GetHeadEntity();
  if (!head || SCR_ResourceNameUtils.GetPrefabName(head) != HeadPrefab || head.GetParent() != member) { Error = "Identity commit has not produced the captured owned head"; return false; }
  if (!MaterialsRestored)
  {
   if (!BodyMaterial.Restore(member) || !HeadMaterial.Restore(head)) { Error = "Captured native material parameters did not restore"; return false; }
   MaterialsRestored = true;
  }
  return Matches(member);
 }
 bool Matches(SCR_ChimeraCharacter member)
 {
  if (!member) { Error = "Character unavailable for identity parity"; return false; }
  CharacterIdentityComponent component = CharacterIdentityComponent.Cast(member.FindComponent(CharacterIdentityComponent));
  if (!component || !SameIdentity(component.GetIdentity())) return false;
  IEntity head = component.GetHeadEntity();
  if (!head || head.GetParent() != member || head.GetChildren() || SCR_ResourceNameUtils.GetPrefabName(head) != HeadPrefab || !head.GetVObject() || head.GetVObject().GetResourceName() != HeadMesh || !member.GetVObject() || member.GetVObject().GetResourceName() != BodyMesh || !BodyMaterial.Matches(member) || !HeadMaterial.Matches(head)) { Error = "Actual head/body mesh or exposed material state differs"; return false; }
  int heads;
  for (IEntity child = member.GetChildren(); child; child = child.GetSibling())
   if (SCR_ResourceNameUtils.GetPrefabName(child).Contains("Prefabs/Characters/Heads/")) heads++;
  if (heads != 1) { Error = "Native identity does not own exactly one head model"; return false; }
  return true;
 }
}
class EBG_FullGroupRoot
{
	IEntity Original;
	IEntity Entity;
	UUID Id;
	PersistenceCollection Collection;
	ResourceName Prefab;
	string Before;
	string OriginalPolicyJson;
	string ConfigurationName;
	string After;
	string CallbackSample;
	string ReleaseSample;
	ref EBG_NativePerceptionAnchor PerceptionAnchor = new EBG_NativePerceptionAnchor();
	bool SelfDelete;
	bool SelfSpawn;
	ESaveGameType SaveMask;
	bool ConfigChanged;
	bool Completed;
	bool SampleMatches;
	bool ActivationObserved;
	ECharacterStance Stance;
	bool StanceWasChanging;
	ref EBG_SimulationAgent AgentState;
	ref EBG_FullIdentityState IdentityState;
	float Radius;
	float YPrecision;
	float Priority;
	EAIWaypointCompletionType Completion;
}

class EBG_FullCacheGroup
{
	static bool InventoryBelongsTo(IEntity item, IEntity member)
	{
		IEntity cursor = item;
		array<IEntity> visited = {};
		for (int depth = 0; cursor && depth < 24; depth++)
		{
			if (cursor == member) return true;
			if (visited.Contains(cursor)) return false;
			visited.Insert(cursor);
			InventoryItemComponent component = InventoryItemComponent.Cast(cursor.FindComponent(InventoryItemComponent));
			InventoryStorageSlot slot;
			if (component) slot = component.GetParentSlot();
			if (slot && slot.GetStorage()) cursor = slot.GetStorage().GetOwner();
			else cursor = cursor.GetParent();
		}
		return false;
	}
	// Exact metadata key observed in installed native JSON, read structurally.
	static string SnapshotConfigurationName(string json)
	{
		SCR_PersistenceJsonLoadContext context = new SCR_PersistenceJsonLoadContext();
		if (!context.LoadFromString(json) || !context.StartObject("configuration")) return string.Empty;
		string name;
		bool found = context.ReadValue("m_rStoreName", name);
		context.EndObject();
		if (!found) return string.Empty;
		return name;
	}


	// Native group deserialization toggles a global IgnoreSpawning flag. Keep
	// this candidate serialized, and retain the owner during uncertain requests.
	protected static ref EBG_FullCacheGroup s_Owner;
#ifdef EBG_ACCEPTANCE_TEST
	protected static SCR_AIGroup s_TestRefuseFirstDetach;
	protected static int s_TestDetachRefusals;
	static bool TestArmFirstDetachRefusal(SCR_AIGroup group)
	{
		if (!GetGame() || !Replication.IsServer() || !EBG_PlayerFixtureProof.TestWorld() || !group || group != GetGame().GetWorld().FindEntityByName("EBG_ProofSquad") || s_Owner || s_TestRefuseFirstDetach) return false;
		s_TestRefuseFirstDetach = group; return true;
	}
	static int TestGetDetachRefusals() { return s_TestDetachRefusals; }
	static void TestClearDetachRefusal() { s_TestRefuseFirstDetach = null; }
#endif
	protected PersistenceSystem m_System;
	protected ref array<ref EBG_FullGroupRoot> m_Roots = {};
	protected ref PersistenceResultCallback m_Callback;
	protected EBG_FullGroupPhase m_Phase;
	protected int m_Members;
	protected int m_Waypoints;
	protected int m_Next;
	protected UUID m_Leader;
	protected bool m_DeleteWhenEmpty;
	protected int m_DormantAlive;
	protected int m_DormantDead;
	protected bool m_DeletedAny;
	protected bool m_Pending;
	protected bool m_RequestFailed;
	protected float m_RequestTime;
	protected float m_ReleaseTime;
	protected float m_SettleTime;
	protected string m_Error;
	protected bool m_WorldClosed;
	protected bool m_ManagedEntry;
	protected bool m_OriginalsRecovered;
	protected bool m_GroupPolicyChanged;
	protected SCR_AIGroup m_SourceGroup;
	protected bool SnapshotMatches(EBG_FullGroupRoot root, string actual, string phase)
	{
		bool clockPhase = phase == "SETTLED" || phase == "RELEASE";
		float clock = -1;
		if (GetGame().GetPerceptionManager()) clock = GetGame().GetPerceptionManager().GetTime();
		bool matches = root.PerceptionAnchor.MatchesSnapshot(root.Before, actual, root.Entity, clock, GetGame().GetWorld().GetWorldTime(), clockPhase);
		bool clockParity = clockPhase && root.PerceptionAnchor.IsArmed();
#ifdef EBG_ACCEPTANCE_TEST
		if (matches && clockParity) PrintFormat("[EBG Full Group PERCEPTION CLOCK] phase=%1 id=%2 stateParity=1 callbackClock=%3 actualClock=%4 directVersion1AgesOnly=1 toleranceSeconds=0.001", phase, root.Id, root.PerceptionAnchor.GetClock(), clock);
		else if (matches && root.Before != actual) PrintFormat("[EBG Full Group POSE QUANTIZATION] phase=%1 id=%2 exactNativeJson=0 stateParity=1 topLevelSpawnOnly=1 maxPositionMetres=0.001 maxAngleDegrees=0.001", phase, root.Id);
#endif
		return matches;
	}

	// Explicit world teardown only: never discard a pending mission recovery.
	void ReleaseForWorldCleanup()
	{
		m_WorldClosed = true;
		m_Pending = false;
		m_Callback = null;
		m_Roots.Clear();
		m_System = null;
		m_SourceGroup = null;
		m_Phase = EBG_FullGroupPhase.FAILED;
		m_Error = "World unloaded; retained test state disposed.";
	}
	static void ShutdownForWorldCleanup()
	{
#ifdef EBG_ACCEPTANCE_TEST
		s_TestRefuseFirstDetach = null; s_TestDetachRefusals = 0;
#endif
		if (s_Owner) s_Owner.ReleaseForWorldCleanup();
		s_Owner = null;
	}

	EBG_FullGroupPhase GetState() { return m_Phase; }
	string GetError() { return m_Error; }
	bool IsPending() { return m_Pending || m_Phase == EBG_FullGroupPhase.RELEASING; }
	int GetRootCount() { return m_Roots.Count(); }
	int GetMemberCount() { return m_Members; }
	int GetWaypointCount() { return m_Waypoints; }
	UUID GetLeaderNativeId() { return m_Leader; }
	UUID GetGroupNativeId() { return GetNativeId(m_Roots.Count() - 1); }
	bool HasDeletionAttempted() { return m_DeletedAny; }
	bool AreOriginalsRecovered() { return m_OriginalsRecovered && !m_DeletedAny && !m_Pending && s_Owner != this; }
	static bool IsNativeOperationBusy() { return s_Owner != null; }
	bool HasReservedUUID(UUID id)
	{
		if (id.IsNull()) return false;
		foreach (EBG_FullGroupRoot root : m_Roots) if (root.Id == id) return true;
		return false;
	}
	string GetSnapshot(int index)
	{
		if (index < 0 || index >= m_Roots.Count()) return string.Empty;
		return m_Roots[index].Before;
	}
	string GetRestoredSnapshot(int index)
	{
		if (index < 0 || index >= m_Roots.Count()) return string.Empty;
		return m_Roots[index].After;
	}
	string GetCallbackSnapshot(int index)
	{
		if (index < 0 || index >= m_Roots.Count()) return string.Empty;
		return m_Roots[index].CallbackSample;
	}
	string GetReleaseSnapshot(int index)
	{
		if (index < 0 || index >= m_Roots.Count()) return string.Empty;
		return m_Roots[index].ReleaseSample;
	}
	string GetOriginalPolicySnapshot(int index)
	{
		if (index < 0 || index >= m_Roots.Count()) return string.Empty;
		return m_Roots[index].OriginalPolicyJson;
	}
	UUID GetNativeId(int index)
	{
		if (index < 0 || index >= m_Roots.Count()) return UUID.NULL_UUID;
		return m_Roots[index].Id;
	}
	SCR_ChimeraCharacter GetRestoredMember(int index)
	{
		if (index < 0 || index >= m_Members || index >= m_Roots.Count()) return null;
		return SCR_ChimeraCharacter.Cast(m_Roots[index].Entity);
	}
	SCR_AIGroup GetRestoredGroup()
	{
		if (m_Roots.IsEmpty()) return null;
		return SCR_AIGroup.Cast(m_Roots[m_Roots.Count() - 1].Entity);
	}
	protected bool Fail(string reason)
	{
		m_Error = reason;
		m_Phase = EBG_FullGroupPhase.FAILED;
		if (!m_DeletedAny && !m_Pending)
		{
			m_OriginalsRecovered = RollBackBeforeDelete();
			m_Error = string.Format("%1 Originals recovered=%2.", reason, m_OriginalsRecovered);
		}
		else ReleaseOperationLockIfSafe();
		Print("[EBG Full Group] " + m_Error, LogLevel.WARNING);
		return false;
	}
	protected void ReleaseOperationLockIfSafe()
	{
		// Pending includes every request without a known final callback. A native
		// serializer which still holds the global spawn flag keeps the lock too.
		if (s_Owner == this && !m_Pending && !SCR_AIGroup.EBG_NativeSpawningIgnored()) s_Owner = null;
	}
	protected bool RuntimeAllowed()
	{
		if (!GetGame() || !GetGame().InPlayMode() || !Replication.IsServer()) return false;
		if (!m_ManagedEntry && !GetGame().GetWorld().FindEntityByName("EBG_TestZone")) return false;
		m_System = PersistenceSystem.GetInstance();
		return m_System && m_System.GetState() == EPersistenceSystemState.ACTIVE;
	}
	protected bool Sample(IEntity entity, out string json)
	{
		if (!entity || !m_System.IsTracked(entity)) return false;
		SCR_PersistenceJsonSaveContext context = new SCR_PersistenceJsonSaveContext();
		if (m_System.Serialize(entity, context) != ESerializeResult.OK || !context.IsValid()) return false;
		json = context.SaveToString();
		return !json.IsEmpty() && json.Length() <= 1048576;
	}
	protected bool AddRoot(IEntity entity, AIAgent agent = null)
	{
		if (!entity || !m_System.IsTracked(entity)) return Fail("Untracked or missing root.");
		EntityPersistenceConfig config = EntityPersistenceConfig.Cast(m_System.GetConfig(entity));
		IEntity parent = m_System.GetTrackedParent(entity);
		if (!config || !config.m_Collection || !config.m_bStorageRoot || config.IsScripted() || (parent && parent != entity)) return Fail("Root is not independently stored under an original native configuration.");
		UUID id = m_System.GetId(entity);
		if (id.IsNull()) return Fail("Root has no native UUID.");
		foreach (EBG_FullGroupRoot previous : m_Roots)
			if (previous.Id == id) return Fail("Duplicate root UUID.");
		EBG_FullGroupRoot root = new EBG_FullGroupRoot();
		root.Original = entity;
		root.Entity = entity;
		root.Id = id;
		root.Collection = config.m_Collection;
		root.Prefab = SCR_ResourceNameUtils.GetPrefabName(entity);
		root.SelfDelete = config.m_bSelfDelete;
		root.SelfSpawn = config.m_bSelfSpawn;
		root.SaveMask = config.m_eSaveMask;
		if (agent)
		{
			root.AgentState = new EBG_SimulationAgent();
			root.AgentState.Capture(agent, SCR_ChimeraCharacter.Cast(entity));
			SCR_ChimeraCharacter stanceMember = SCR_ChimeraCharacter.Cast(entity);
			if (stanceMember)
			{
				root.Stance = stanceMember.GetCharacterController().GetStance();
				root.StanceWasChanging = stanceMember.GetCharacterController().IsChangingStance();
				if (root.Stance != ECharacterStance.STAND && root.Stance != ECharacterStance.CROUCH && root.Stance != ECharacterStance.PRONE)
					return Fail(string.Format("Unsupported current stance before Save: UUID=%1 stance=%2 changing=%3.", root.Id, root.Stance, root.StanceWasChanging));
#ifdef EBG_ACCEPTANCE_TEST
				PrintFormat("[EBG Full Group STANCE CAPTURE] id=%1 current=%2 changing=%3 AIactive=%4 LOD=%5 permanentLOD=%6; animation task progress is not persistent gameplay state", root.Id, root.Stance, root.StanceWasChanging, agent.IsAIActivated(), agent.GetLOD(), agent.GetPermanentLOD());
#endif
				root.IdentityState = new EBG_FullIdentityState();
				if (!root.IdentityState.Capture(stanceMember)) return Fail("Identity preflight refused before Save/deletion: " + root.IdentityState.Error);
			}
		}
		SCR_AIWaypoint waypoint = SCR_AIWaypoint.Cast(entity);
		if (waypoint)
		{
			root.Radius = waypoint.GetCompletionRadius();
			root.YPrecision = waypoint.GetCompletionYPrecision();
			root.Completion = waypoint.GetCompletionType();
			root.Priority = waypoint.GetPriorityLevel();
		}
		m_Roots.Insert(root);
		return true;
	}
	protected bool OriginalStanceUnchanged(EBG_FullGroupRoot root, string phase)
	{
		SCR_ChimeraCharacter member = SCR_ChimeraCharacter.Cast(root.Original);
		if (!member) return true;
		CharacterControllerComponent controller = member.GetCharacterController();
		if (controller && controller.GetStance() == root.Stance) return true;
		int actual = -1;
		bool changing;
		if (controller) { actual = controller.GetStance(); changing = controller.IsChangingStance(); }
		return Fail(string.Format("Current stance changed %1 before deletion: UUID=%2 captured=%3 actual=%4 changing=%5 capturedChanging=%6. Originals retained.", phase, root.Id, root.Stance, actual, changing, root.StanceWasChanging));
	}
	protected bool Configure(EBG_FullGroupRoot root, bool restore)
	{
		if (!root.Entity) return false;
		EntityPersistenceConfig config = EntityPersistenceConfig.Cast(m_System.GetConfig(root.Entity));
		if (!config) return false;
		if (restore)
		{
			// Reapply the named native rule, not an unnamed scripted copy whose
			// values happen to match. Reject any changed rule or policy fields.
			if (!m_System.ReloadConfig(root.Entity)) return false;
			config = EntityPersistenceConfig.Cast(m_System.GetConfig(root.Entity));
			if (!config || config.IsScripted() || config.m_Collection != root.Collection || config.m_bSelfDelete != root.SelfDelete || config.m_bSelfSpawn != root.SelfSpawn || config.m_eSaveMask != root.SaveMask) return false;
			string namedSample;
			if (!Sample(root.Entity, namedSample) || EBG_FullCacheGroup.SnapshotConfigurationName(namedSample) != root.ConfigurationName) return false;
			root.ConfigChanged = false;
			return true;
		}
		else
		{
			config.m_bSelfDelete = false;
			config.m_bSelfSpawn = false;
			config.m_eSaveMask = ESaveGameType.SCRIPTED;
		}
		if (!m_System.SetConfig(root.Entity, config)) return false;
		root.ConfigChanged = !restore;
		return true;
	}
	protected bool RollBackBeforeDelete()
	{
		if (m_DeletedAny || m_Pending) return false;
		bool restored = m_SourceGroup != null;
		foreach (EBG_FullGroupRoot root : m_Roots)
		{
			if (!root.Original || root.Entity != root.Original || !m_System || !m_System.IsTracked(root.Original) || m_System.GetId(root.Original) != root.Id) restored = false;
			if (root.ConfigChanged && !Configure(root, true)) restored = false;
		}
		if (m_GroupPolicyChanged)
		{
			if (m_SourceGroup) { m_SourceGroup.SetDeleteWhenEmpty(m_DeleteWhenEmpty); m_GroupPolicyChanged = false; }
			else restored = false;
		}
		if (restored) ReleaseOperationLockIfSafe();
		return restored;
	}
	// Caller must reserve its existing logical group/member UUID bindings before
	// this synchronous method can remove any entity. No zone or ledger is rebuilt.
	bool BeginManagedSleep(SCR_AIGroup group)
	{
		if (m_Phase != EBG_FullGroupPhase.NEW) { m_Error = "Managed transaction already used."; return false; }
		m_ManagedEntry = true;
		return BeginSleep(group, true);
	}

	// One bounded synchronous transaction: 1-9 healthy current members, 0-8
	// Move/ForcedMove orders. Native Save returns the safe-to-delete boolean; there
	// is no entity Save callback. No originals are deleted until ALL saves pass.
	bool BeginSleep(SCR_AIGroup group, bool isolatedTestAuthorized = false)
	{
		if (m_Phase != EBG_FullGroupPhase.NEW) { m_Error = "Transaction already used."; return false; }
		if (s_Owner) { m_Error = "Another native operation owns the lock; transaction remains NEW."; return false; }
		m_SourceGroup = group;
		if (!isolatedTestAuthorized || !RuntimeAllowed()) return Fail("Authorized server Play entry and ACTIVE native persistence required.");
		if (!group || group.EBG_Exclude || !group.EBG_HasCompletedInitialSpawn() || group.GetPlayerCount() > 0 || group.IsSlave() || group.GetMaster() || group.IsCreatedByCommander()) return Fail("Externally controlled, initializing or missing group.");
		if (group.IsDormant() || group.GetPermanentLOD() >= 0 || group.GetLifecyclePolicy() == SCR_EAIGroupLifecyclePolicy.ProximityDriven || group.EBG_NativeHasSceneReferences() || group.GetChildren()) return Fail("Unsupported lifecycle, hierarchy or scene references.");
		ResourceName prefab = SCR_ResourceNameUtils.GetPrefabName(group);
		if (prefab.IsEmpty() || group.Type() != SCR_AIGroup) return Fail("Requires a prefab using native SCR_AIGroup lifecycle and persistence.");
		string modGroupOperation = EBG_OptionalModState.ActiveGroupOperation(group);
		if (!modGroupOperation.IsEmpty()) return Fail(modGroupOperation);
		array<AIAgent> agents = {};
		group.GetAgents(agents);
		m_Members = agents.Count();
		if (m_Members < 1 || m_Members > 9) return Fail("Requires 1-9 current members.");
		foreach (AIAgent agent : agents)
		{
			if (!agent) return Fail("Missing current agent.");
			SCR_ChimeraCharacter member = SCR_ChimeraCharacter.Cast(agent.GetControlledEntity());
			string unsupported = EBG_SimulationCache.Unsupported(member);
			if (!unsupported.IsEmpty()) return Fail(unsupported);
			// Native far LOD can suspend an animation transition indefinitely. Preserve
			// GetStance's current gameplay enum, not an unavailable target/task cursor.
			EBG_CacheMember managed;
			if (EBG_CacheManager.Instance) managed = EBG_CacheManager.Instance.FindMember(member);
			if (managed && (managed.Dead || managed.WasPlayer)) return Fail("Casualty or previously possessed member.");
			InventoryStorageManagerComponent inventory = InventoryStorageManagerComponent.Cast(member.FindComponent(InventoryStorageManagerComponent));
			if (!inventory) return Fail("Inventory cannot be inspected.");
			array<IEntity> items = {};
			inventory.GetItems(items);
			if (items.Count() > 128) return Fail("Inventory exceeds bounded native dependency limit.");
			foreach (IEntity item : items)
				if (!item || !EBG_FullCacheGroup.InventoryBelongsTo(item, member)) return Fail("Inventory dependency has uncertain actual ownership.");
			if (!AddRoot(member, agent)) return false;
		}
		m_Leader = m_System.GetId(group.GetLeaderEntity());
		if (m_Leader.IsNull()) return Fail("Leader has no persistent identity.");
		array<AIWaypoint> waypoints = {};
		group.GetWaypoints(waypoints);
		m_Waypoints = waypoints.Count();
		if (m_Waypoints > 8 || (m_Waypoints > 0 && group.GetCurrentWaypoint() != waypoints[0])) return Fail("Unsupported waypoint count or current-order cursor.");
		foreach (AIWaypoint order : waypoints)
		{
			SCR_AIWaypoint waypoint = SCR_AIWaypoint.Cast(order);
			if (!waypoint || waypoint.GetChildren()) return Fail("Unsupported waypoint hierarchy.");
			ResourceName orderPrefab = SCR_ResourceNameUtils.GetPrefabName(waypoint);
			if (!orderPrefab.EndsWith("Prefabs/AI/Waypoints/AIWaypoint_Move.et") && !orderPrefab.EndsWith("PrefabsEditable/Auto/AI/Waypoints/E_AIWaypoint_Move.et") && !orderPrefab.EndsWith("Prefabs/AI/Waypoints/AIWaypoint_ForcedMove.et") && !orderPrefab.EndsWith("PrefabsEditable/Auto/AI/Waypoints/E_AIWaypoint_ForcedMove.et")) return Fail("Only native Move or ForcedMove waypoints are supported.");
			array<SCR_AISettingBase> settings = {};
			waypoint.GetSettings(settings);
			if (!settings.IsEmpty()) return Fail("Custom waypoint settings have no audited serializer coverage.");
			if (!AddRoot(waypoint)) return false;
		}
		if (!AddRoot(group, group)) return false;
		m_DeleteWhenEmpty = group.EBG_NativeDeleteWhenEmpty();
		m_DormantAlive = group.GetDormantAliveCount();
		m_DormantDead = group.GetDormantDeadCount();
		s_Owner = this;
		foreach (EBG_FullGroupRoot root : m_Roots)
		{
			if (root.IdentityState && !root.IdentityState.Matches(SCR_ChimeraCharacter.Cast(root.Entity))) return Fail("Original identity changed before Save/deletion: " + root.IdentityState.Error);
			if (!OriginalStanceUnchanged(root, "before Save")) return false;
			if (!Sample(root.Entity, root.Before)) return Fail("Original native snapshot failed; no deletion.");
			if (!EBG_NativePerceptionParity.Preflight(root.Before)) return Fail("Unsupported native perception age schema; originals retained, no deletion.");
			root.OriginalPolicyJson = root.Before;
			root.ConfigurationName = EBG_FullCacheGroup.SnapshotConfigurationName(root.Before);
			if (root.ConfigurationName.IsEmpty()) return Fail("Original native configuration name is empty; no Save/deletion.");
			if (!m_System.Save(root.Entity, ESaveGameType.SCRIPTED))
			{
				bool rolledBack = RollBackBeforeDelete();
				return Fail(string.Format("Save failed before any deletion. Originals retained; config rollback=%1.", rolledBack));
			}
			if (!OriginalStanceUnchanged(root, "after Save")) return false;
#ifdef EBG_ACCEPTANCE_TEST
			PrintFormat("[EBG Full Group SAVE] id=%1 prefab=%2 collection=%3 configuration=%4 nativeJson=%5 save=1 beforeAnyTemporaryConfig=1", root.Id, root.Prefab, root.Collection, root.ConfigurationName, root.Before.Length());
#endif
		}
		// Protect live instances only AFTER all named native records are saved.
		// No further Save occurs before StopTracking(false) and removal.
		foreach (EBG_FullGroupRoot protectedRoot : m_Roots)
		{
			if (!Configure(protectedRoot, false))
			{
				bool policyRestored = RollBackBeforeDelete();
				return Fail(string.Format("Post-save temporary protection failed; no deletion; native-policy rollback=%1.", policyRestored));
			}
#ifdef EBG_ACCEPTANCE_TEST
			string protectedSample;
			if (Sample(protectedRoot.Entity, protectedSample)) PrintFormat("[EBG Full Group POLICY] id=%1 savedNamedConfig=%2 temporaryConfig=%3 temporaryJson=%4 NOT_RESAVED=1", protectedRoot.Id, protectedRoot.ConfigurationName, EBG_FullCacheGroup.SnapshotConfigurationName(protectedSample), protectedSample.Length());
#endif
		}
		// Prevent the group's native OnEmpty 1 ms deletion from racing ownership.
		group.SetDeleteWhenEmpty(false);
		m_GroupPolicyChanged = true;
		foreach (EBG_FullGroupRoot saved : m_Roots)
		{
			// Match SCR_SpawnLogic.OnPlayerEntityCleanup_S: Save, then release
			// tracking with removeData=false before physical entity deletion.
			// SelfDelete=false alone did not yield a demonstrated restore.
			bool detached;
#ifdef EBG_ACCEPTANCE_TEST
			if (!m_DeletedAny && s_TestRefuseFirstDetach == group)
			{
				s_TestRefuseFirstDetach = null;
				s_TestDetachRefusals++;
				Print("[EBG FULL DETACH TEST] Injected first StopTracking refusal; native tracking untouched");
			}
			else detached = m_System.StopTracking(saved.Entity, false);
#else
			detached = m_System.StopTracking(saved.Entity, false);
#endif
			if (!detached)
			{
				// Before any deletion the originals are still normal. Rollback below
				// restores named persistence policies; it must not abandon hidden AI.
				if (m_DeletedAny) QuarantineAvailable();
				return Fail("StopTracking(removeData=false) failed; this root was not deleted. Retained saved roots require inspection.");
			}
#ifdef EBG_ACCEPTANCE_TEST
			PrintFormat("[EBG Full Group DETACH] id=%1 prefab=%2 removeData=0 entityPresent=%3 trackedAfter=%4", saved.Id, saved.Prefab, saved.Entity != null, m_System.IsTracked(saved.Entity));
#endif
			// Once a deletion is attempted, partial child removal is possible even
			// if the root remains. Do not claim the originals are untouched again.
			m_DeletedAny = true;
			SCR_EntityHelper.DeleteEntityAndChildren(saved.Entity);
			if (saved.Entity || m_System.FindById(saved.Id))
			{
				QuarantineAvailable();
				return Fail("Deletion incomplete. Saved roots retained; BeginWake can recover only missing UUIDs after inspection.");
			}
#ifdef EBG_ACCEPTANCE_TEST
			PrintFormat("[EBG Full Group DELETE] id=%1 originalAbsent=1 nativeIdAvailable=0", saved.Id);
#endif
		}
		m_Phase = EBG_FullGroupPhase.CACHED;
		m_Error = "All current roots saved and removal confirmed. Native records and JSON retained.";
		ReleaseOperationLockIfSafe();
		return true;
	}
	protected void Quarantine(EBG_FullGroupRoot root)
	{
		if (!root.Entity || !root.AgentState) return;
		SCR_ChimeraCharacter member = SCR_ChimeraCharacter.Cast(root.Entity);
		if (member && EntityUtils.IsPlayer(member)) return;
		AIAgent agent = SCR_AIGroup.Cast(root.Entity);
		if (member) agent = SCR_AIUtils.GetAIAgent(member);
		if (!agent) return;
		root.AgentState.Agent = agent;
		root.AgentState.Character = member;
		root.AgentState.Suspend();
		if (member && !EntityUtils.IsPlayer(member)) member.EBG_SetSimulationCached(true);
	}
	protected void QuarantineAvailable()
	{
		foreach (EBG_FullGroupRoot root : m_Roots) Quarantine(root);
	}
	// Retry is allowed only after a final callback, never on a timed-out pending
	// request. Already available unconfirmed objects block duplicate recreation.
	bool BeginWake()
	{
		if (m_Pending) { m_Error = "Native request remains pending; no duplicate wake."; return false; }
		if (s_Owner && s_Owner != this) { m_Error = "Another native operation owns the lock; retained state unchanged."; return false; }
		if (m_Phase != EBG_FullGroupPhase.CACHED && m_Phase != EBG_FullGroupPhase.FAILED) { m_Error = "Wake requires cached or finalized failed state."; return false; }
		if (!RuntimeAllowed() || !m_DeletedAny) return Fail("No recoverable deletion or native system unavailable.");
		foreach (EBG_FullGroupRoot root : m_Roots)
		{
			IEntity known = IEntity.Cast(m_System.FindById(root.Id));
			if (root.Completed && !known) return Fail("A completed restored root disappeared. Death/player continuity must be inspected; this retry will not recreate it.");
			if (known && known != root.Entity) return Fail("An unconfirmed instance owns a saved UUID; inspect it before retry. No duplicate spawn.");
			if (known && root.Entity == root.Original) root.Completed = true;
		}
		m_Next = 0;
		m_SettleTime = -1;
		s_Owner = this;
		m_Phase = EBG_FullGroupPhase.RESTORING;
		m_Error = "Restoring saved current members, then waypoints, then group.";
		return true;
	}
	void Poll()
	{
		if (m_WorldClosed) return;
		if (m_Phase == EBG_FullGroupPhase.RELEASING) { PollRelease(); return; }
		if (m_Pending)
		{
			if (GetGame().GetWorld().GetWorldTime() - m_RequestTime > 30000) m_Error = "Native request exceeds 30 seconds; still pending, retries locked and snapshots retained.";
			return;
		}
		if (m_Phase != EBG_FullGroupPhase.RESTORING) return;
		while (m_Next < m_Roots.Count() && m_Roots[m_Next].Completed) m_Next++;
		if (m_Next == m_Roots.Count()) { PollSettledState(); return; }
		EBG_FullGroupRoot root = m_Roots[m_Next];
		if (root.Entity || m_System.FindById(root.Id)) { Fail("Available incomplete root blocks duplicate request."); return; }
		PersistenceSpawnRequest request = new PersistenceSpawnRequest();
		request.Collection = root.Collection;
		request.Include = {root.Id};
		// Match the installed SCR_SpawnLogic single-character Include request.
		// Limit/Offset remain native defaults; Include already bounds this to one.
#ifdef EBG_ACCEPTANCE_TEST
		string expectedCollectionName = "AIGroup";
		if (m_Next < m_Members) expectedCollectionName = "Character";
		else if (m_Next < m_Roots.Count() - 1) expectedCollectionName = "AIWaypoint";
		PersistenceCollection expectedCollection = m_System.FindCollection(expectedCollectionName);
		PrintFormat("[EBG Full Group REQUEST] root=%1 id=%2 prefab=%3 collection=%4 expectedName=%5 expectedCollection=%6 match=%7", m_Next, root.Id, root.Prefab, request.Collection, expectedCollectionName, expectedCollection, request.Collection == expectedCollection);
		PrintFormat("[EBG Full Group REQUEST] includeCount=%1 include0=%2 limit=%3 offset=%4 systemState=%5 availableBefore=%6", request.Include.Count(), request.Include[0], request.Limit, request.Offset, m_System.GetState(), m_System.FindById(root.Id) != null);
#endif
		m_Pending = true;
		m_RequestFailed = false;
		m_RequestTime = GetGame().GetWorld().GetWorldTime();
		m_Callback = new PersistenceResultCallback(OnRestored, this);
		m_System.RequestSpawn(request, m_Callback);
	}
	protected static void OnRestored(EPersistenceStatusCode status, Managed result, bool isLast, Managed context)
	{
		// Local ownership survives clearing the callback's context cycle.
		ref EBG_FullCacheGroup transaction = EBG_FullCacheGroup.Cast(context);
		if (!transaction) return;
		if (isLast) transaction.m_Callback = null;
		if (transaction.m_WorldClosed || !transaction.m_Pending) return;
		EBG_FullGroupRoot root = transaction.m_Roots[transaction.m_Next];
		string resultType = "NULL";
		if (result) resultType = result.Type().ToString();
#ifdef EBG_ACCEPTANCE_TEST
		PrintFormat("[EBG Full Group CALLBACK] status=%1 statusName=%2 result=%3 resultType=%4 isLast=%5 requestedId=%6 prefab=%7", status, typename.EnumToString(EPersistenceStatusCode, status), result, resultType, isLast, root.Id, root.Prefab);
#endif
		IEntity entity = IEntity.Cast(result);
#ifdef EBG_ACCEPTANCE_TEST
		PrintFormat("[EBG Full Group CALLBACK] entity=%1 returnedNativeId=%2 currentlyAvailable=%3 collection=%4", entity, transaction.m_System.GetId(entity), transaction.m_System.FindById(root.Id), root.Collection);
#endif
		if (status != EPersistenceStatusCode.OK || !entity || transaction.m_System.GetId(entity) != root.Id || SCR_ResourceNameUtils.GetPrefabName(entity) != root.Prefab || !root.PerceptionAnchor.AcceptsEntity(entity))
			transaction.m_RequestFailed = true;
		else
		{
			root.Entity = entity;
#ifdef EBG_ACCEPTANCE_TEST
			if (EBG_CacheCleanup.Instance && SCR_ChimeraCharacter.Cast(entity)) EBG_CacheCleanup.Instance.ReportHeadIdentity("restored callback", SCR_ChimeraCharacter.Cast(entity), string.Empty);
#endif
			SCR_AIWaypoint waypoint = SCR_AIWaypoint.Cast(entity);
			if (waypoint)
			{
				waypoint.SetCompletionRadius(root.Radius);
				waypoint.SetCompletionYPrecision(root.YPrecision);
				waypoint.SetCompletionType(root.Completion);
				waypoint.SetPriorityLevel(root.Priority);
			}
			SCR_AIGroup restoredGroup = SCR_AIGroup.Cast(entity);
			if (restoredGroup)
			{
				// Native 1.8 deserialization ignores dormantDead when dormantAlive=-1
				// (a materialized group with casualties). Restore that exact bookkeeping
				// before the next native expansion tick can refill a dead member's slot.
				restoredGroup.SetDormantCounts(transaction.m_DormantAlive, transaction.m_DormantDead);
				if (restoredGroup.GetDormantAliveCount() != transaction.m_DormantAlive || restoredGroup.GetDormantDeadCount() != transaction.m_DormantDead) transaction.m_RequestFailed = true;
#ifdef EBG_ACCEPTANCE_TEST
				PrintFormat("[EBG Full Group CASUALTY COUNTS] id=%1 capturedAlive=%2 capturedDead=%3 restoredAlive=%4 restoredDead=%5 agents=%6", root.Id, transaction.m_DormantAlive, transaction.m_DormantDead, restoredGroup.GetDormantAliveCount(), restoredGroup.GetDormantDeadCount(), restoredGroup.GetAgentsCount());
#endif
				SCR_ChimeraCharacter restoredLeader = SCR_ChimeraCharacter.Cast(transaction.m_System.FindById(transaction.m_Leader));
				if (restoredLeader && restoredLeader.GetCharacterGroup() == restoredGroup) restoredGroup.SetNewLeader(SCR_AIUtils.GetAIAgent(restoredLeader));
			}
			float callbackClock = -1;
			if (GetGame().GetPerceptionManager()) callbackClock = GetGame().GetPerceptionManager().GetTime();
			bool sampled = transaction.Sample(entity, root.After);
			root.SampleMatches = sampled && transaction.SnapshotMatches(root, root.After, "CALLBACK");
			root.CallbackSample = root.After;
			// Never fit an offset to a changed payload. Anchor the actual restored
			// root only after exact callback perception and an unchanged sample clock.
			if (!sampled || !GetGame().GetPerceptionManager() || callbackClock != GetGame().GetPerceptionManager().GetTime()) callbackClock = -1;
			if (!root.PerceptionAnchor.Observe(root.Before, root.After, entity, callbackClock, GetGame().GetWorld().GetWorldTime())) transaction.m_RequestFailed = true;
#ifdef EBG_ACCEPTANCE_TEST
			if (!root.SampleMatches) transaction.ReportSnapshotMismatch(transaction.m_Next, root.After, "CALLBACK");
#endif
			if (EBG_FullCacheGroup.SnapshotConfigurationName(root.After) != root.ConfigurationName) transaction.m_RequestFailed = true;
#ifdef EBG_ACCEPTANCE_TEST
			PrintFormat("[EBG Full Group RESTORED POLICY] expected=%1 actual=%2 stateParity=%3 exactNativeJson=%4", root.ConfigurationName, EBG_FullCacheGroup.SnapshotConfigurationName(root.After), root.SampleMatches, root.Before == root.After);
#endif
			if (!transaction.Configure(root, false)) transaction.m_RequestFailed = true;
			transaction.Quarantine(root);
		}
		if (!isLast) return;
		transaction.m_Pending = false;
		if (transaction.m_RequestFailed) { transaction.Fail(string.Format("Final native restore callback failed: status=%1 (%2), resultType=%3. Keep saved records and inspect partial entities before recovery.", status, typename.EnumToString(EPersistenceStatusCode, status), resultType)); return; }
		root.Completed = true;
		// Native group serializer reactivates every member; re-suppress together.
		transaction.QuarantineAvailable();
	}
	// Native callback finality is not reinterpreted as unfinished deserialization.
	// This is a separate, explicit repair of the captured current stance, followed
	// by native state verification while all available characters stay hidden.
	protected void PollSettledState()
	{
		SCR_AIGroup group = GetRestoredGroup();
		if (!group) { RetainAfterReleaseFailure("Group missing during captured-stance verification."); return; }
		QuarantineAvailable();
		if (m_SettleTime < 0)
		{
			m_SettleTime = GetGame().GetWorld().GetWorldTime();
			for (int memberIndex = 0; memberIndex < m_Members; memberIndex++)
			{
				EBG_FullGroupRoot memberRoot = m_Roots[memberIndex];
				SCR_ChimeraCharacter member = SCR_ChimeraCharacter.Cast(memberRoot.Entity);
				if (!member || !member.GetCharacterController() || EntityUtils.IsPlayer(member) || member.GetCharacterController().IsDead())
				{ RetainAfterReleaseFailure("Captured-stance repair refused for missing, dead or player member."); return; }
				CharacterControllerComponent controller = member.GetCharacterController();
				if (!memberRoot.IdentityState || !memberRoot.IdentityState.Restore(member))
				{
					QuarantineAvailable();
					string identityError = "Captured identity unavailable";
					if (memberRoot.IdentityState) identityError = memberRoot.IdentityState.Error;
					RetainAfterReleaseFailure("Native identity restore failed: " + identityError);
					return;
				}
				// SetIdentity can replace the generated head. Capture and suppress it
				// in this same call before any frame or cleanup rebinding is permitted.
				Quarantine(memberRoot);
#ifdef EBG_ACCEPTANCE_TEST
				if (EBG_CacheCleanup.Instance) EBG_CacheCleanup.Instance.ReportHeadIdentity("restored captured identity", member, memberRoot.IdentityState.HeadPrefab);
#endif
				int beforeStance = controller.GetStance();
				bool requested = beforeStance != memberRoot.Stance;
				if (requested) SCR_AIStanceHandling.SetStance(controller, memberRoot.Stance);
#ifdef EBG_ACCEPTANCE_TEST
				PrintFormat("[EBG Full Group STANCE REQUEST] id=%1 captured=%2 before=%3 after=%4 changing=%5 requested=%6 hidden=%7 capturedChanging=%8", memberRoot.Id, memberRoot.Stance, beforeStance, controller.GetStance(), controller.IsChangingStance(), requested, member.EBG_CheckSimulationLocal() == 0, memberRoot.StanceWasChanging);
#endif
			}
			m_Error = "Native callbacks final; captured stance requested under quarantine. Waiting for later state sample with bounded spawn-pose quantization.";
			return;
		}
		float elapsed = GetGame().GetWorld().GetWorldTime() - m_SettleTime;
		if (elapsed <= 0) return;
		bool exact = true;
#ifdef EBG_ACCEPTANCE_TEST
		bool exactJson = true;
#endif
		string pending;
		group.SetDeleteWhenEmpty(m_DeleteWhenEmpty);
		for (int i = 0; i < m_Roots.Count(); i++)
		{
			EBG_FullGroupRoot root = m_Roots[i];
			if (!Configure(root, true)) { RetainAfterReleaseFailure("Named policy unavailable for settled-state sample."); return; }
			root.SampleMatches = Sample(root.Entity, root.After) && SnapshotMatches(root, root.After, "SETTLED");
#ifdef EBG_ACCEPTANCE_TEST
			if (root.After != root.Before) exactJson = false;
#endif
			if (!Configure(root, false)) { RetainAfterReleaseFailure("Could not re-protect root after settled-state sample."); return; }
			if (!root.SampleMatches) pending = string.Format("id=%1 stateParity=0", root.Id);
			SCR_ChimeraCharacter current = SCR_ChimeraCharacter.Cast(root.Entity);
			if (current)
			{
				CharacterControllerComponent currentController = current.GetCharacterController();
				if (!currentController || EntityUtils.IsPlayer(current) || currentController.IsDead() || current.GetCharacterGroup() != group || current.EBG_CheckSimulationLocal() != 0)
				{ RetainAfterReleaseFailure("Survivor life/ownership/quarantine changed during settled-state verification."); return; }
				// IsChangingStance is animation progress, not a saved target stance.
				// Exact current stance and native serialized gameplay state still match.
				if (currentController.GetStance() != root.Stance) root.SampleMatches = false;
				if (!root.SampleMatches) pending = string.Format("id=%1 capturedStance=%2 actualStance=%3 changing=%4 exactNativeJson=%5", root.Id, root.Stance, currentController.GetStance(), currentController.IsChangingStance(), root.After == root.Before);
				if (!root.IdentityState || !root.IdentityState.Settle(current))
				{
					root.SampleMatches = false;
					pending = string.Format("id=%1 captured identity unavailable", root.Id);
					if (root.IdentityState) pending = string.Format("id=%1 identity=%2", root.Id, root.IdentityState.Error);
				}
			}
			if (!root.SampleMatches)
			{
				exact = false;
				if (elapsed >= 5000) ReportSnapshotMismatch(i, root.After, "SETTLED");
			}
		}
		group.SetDeleteWhenEmpty(false);
		if (!exact)
		{
			m_Error = "Settled state differs under quarantine: " + pending;
			if (elapsed >= 5000) RetainAfterReleaseFailure("Captured-stance/state-parity verification exceeded 5 seconds. " + pending);
			return;
		}
#ifdef EBG_ACCEPTANCE_TEST
		PrintFormat("[EBG Full Group SETTLED] roots=%1 elapsedMs=%2 stateParity=1 exactNativeJson=%3 capturedStance=1 quarantine=1 spawnPositionToleranceMetres=0.001 spawnAngleToleranceDegrees=0.001", m_Roots.Count(), elapsed, exactJson);
#endif
		VerifyRestored();
	}
	protected bool VerifyRestored()
	{
		SCR_AIGroup group = GetRestoredGroup();
		if (!group) return Fail("Restored group missing.");
		if (!m_Roots[m_Roots.Count() - 1].SampleMatches) return Fail("Native group JSON differs from saved current state; no automatic acceptance.");
		array<AIAgent> agents = {};
		group.GetAgents(agents);
		if (agents.Count() != m_Members) return Fail("Final group membership count differs; retain transaction for inspection.");
		for (int i = 0; i < m_Members; i++)
		{
			EBG_FullGroupRoot root = m_Roots[i];
			SCR_ChimeraCharacter member = SCR_ChimeraCharacter.Cast(root.Entity);
			if (!member || !member.GetCharacterController() || EntityUtils.IsPlayer(member) || member.GetCharacterController().IsDead() || member.GetCharacterGroup() != group || !agents.Contains(SCR_AIUtils.GetAIAgent(member))) return Fail("Restored survivor identity/life/group mismatch; no release.");
			if (member.GetCharacterController().GetStance() != root.Stance) return Fail(string.Format("Restored current stance changed before release: UUID=%1 captured=%2 actual=%3 changing=%4.", root.Id, root.Stance, member.GetCharacterController().GetStance(), member.GetCharacterController().IsChangingStance()));
			if (!root.SampleMatches) return Fail("Native member JSON differs from saved current state. Inspect Before/After; no automatic acceptance.");
		}
		SCR_ChimeraCharacter leader = SCR_ChimeraCharacter.Cast(m_System.FindById(m_Leader));
		if (!leader || leader.GetCharacterGroup() != group) return Fail("Saved leader identity missing.");
		group.SetNewLeader(SCR_AIUtils.GetAIAgent(leader));
		array<AIWaypoint> orders = {};
		group.GetWaypoints(orders);
		if (orders.Count() != m_Waypoints) return Fail("Saved order count differs.");
		for (int j = 0; j < m_Waypoints; j++)
		{
			EBG_FullGroupRoot savedOrder = m_Roots[m_Members + j];
			SCR_AIWaypoint restoredOrder = SCR_AIWaypoint.Cast(orders[j]);
			if (!restoredOrder || orders[j] != savedOrder.Entity || !savedOrder.SampleMatches) return Fail("Saved order identity/order/state differs.");
			if (restoredOrder.GetCompletionRadius() != savedOrder.Radius || restoredOrder.GetCompletionYPrecision() != savedOrder.YPrecision || restoredOrder.GetCompletionType() != savedOrder.Completion || restoredOrder.GetPriorityLevel() != savedOrder.Priority) return Fail("Explicit saved waypoint completion/priority parameters differ.");
		}
		if (m_Waypoints > 0 && group.GetCurrentWaypoint() != orders[0]) return Fail("Current order cursor differs.");
		m_Phase = EBG_FullGroupPhase.READY;
		m_Error = "Native callbacks and member/order state parity passed with bounded spawn-pose quantization. Explicit ReleaseRestored required; pre-callback visibility remains unproven.";
		ReleaseOperationLockIfSafe();
		return true;
	}
	protected bool RetainAfterReleaseFailure(string reason)
	{
		bool protectedAll = true;
		foreach (EBG_FullGroupRoot root : m_Roots)
			if (!Configure(root, false)) protectedAll = false;
		SCR_AIGroup group = GetRestoredGroup();
		if (group) group.SetDeleteWhenEmpty(false);
		QuarantineAvailable();
		return Fail(string.Format("%1 Temporary protection reapplied=%2; available roots re-quarantined and snapshots retained.", reason, protectedAll));
	}
	protected void ReportSnapshotMismatch(int index, string actual, string phase)
	{
		EBG_FullGroupRoot root = m_Roots[index];
		PrintFormat("[EBG Full Group MISMATCH] phase=%1 root=%2 id=%3 prefab=%4 beforeChars=%5 actualChars=%6; saved state retained for recovery", phase, index, root.Id, root.Prefab, root.Before.Length(), actual.Length());
#ifdef EBG_ACCEPTANCE_TEST
		int common = Math.Min(root.Before.Length(), actual.Length());
		int offset;
		while (offset < common && root.Before.Substring(offset, 1) == actual.Substring(offset, 1)) offset++;
		int start = Math.Max(0, offset - 64);
		int beforeLength = Math.Min(256, root.Before.Length() - start);
		int afterLength = Math.Min(256, actual.Length() - start);
		PrintFormat("[EBG Full Group %1 MISMATCH] root=%2 id=%3 prefab=%4 beforeChars=%5 actualChars=%6 firstDifferentOffset=%7 windowStart=%8", phase, index, root.Id, root.Prefab, root.Before.Length(), actual.Length(), offset, start);
		PrintFormat("[EBG Full Group %1 BEFORE] %2", phase, root.Before.Substring(start, beforeLength));
		PrintFormat("[EBG Full Group %1 ACTUAL] %2", phase, actual.Substring(start, afterLength));
#ifdef EBG_ACCEPTANCE_TEST
		PrintFormat("[EBG FULL DIFF ORIGINAL] %1", root.Before);
		PrintFormat("[EBG FULL DIFF RESTORED] %1", actual);
#endif
		// Full unmodified strings remain accessible through GetSnapshot and
		// GetReleaseSnapshot; logs show exact bounded windows, not normalized data.
	#endif
	}
	// True means release was dispatched. The owner must retain bindings and this
	// transaction, Poll RELEASING, and commit only after observing RELEASED.
	bool ReleaseRestored()
	{
		if (!RuntimeAllowed() || m_Phase != EBG_FullGroupPhase.READY || m_Pending) return false;
		if (!VerifyRestored()) return false;
		foreach (EBG_FullGroupRoot root : m_Roots)
			if (!Configure(root, true)) return RetainAfterReleaseFailure("Could not restore original named native persistence policy.");
		SCR_AIGroup group = GetRestoredGroup();
		group.SetDeleteWhenEmpty(m_DeleteWhenEmpty);
		// Root callbacks may precede final readiness by multiple frames. Read the
		// actual current state again under the exact original named policies before
		// any member/group activation; earlier callback matches alone are not enough.
		for (int i = 0; i < m_Roots.Count(); i++)
		{
			EBG_FullGroupRoot current = m_Roots[i];
			if (current.IdentityState && !current.IdentityState.Matches(SCR_ChimeraCharacter.Cast(current.Entity))) return RetainAfterReleaseFailure("Fresh release identity differs: " + current.IdentityState.Error);
			SCR_ChimeraCharacter stanceMember = SCR_ChimeraCharacter.Cast(current.Entity);
			if (stanceMember && (!stanceMember.GetCharacterController() || stanceMember.GetCharacterController().GetStance() != current.Stance))
				return RetainAfterReleaseFailure(string.Format("Fresh release current stance differs for UUID=%1 captured=%2.", current.Id, current.Stance));
			current.ReleaseSample = string.Empty;
			bool sampled = Sample(current.Entity, current.ReleaseSample);
			if (!sampled || !SnapshotMatches(current, current.ReleaseSample, "RELEASE"))
			{
				ReportSnapshotMismatch(i, current.ReleaseSample, "RELEASE");
				return RetainAfterReleaseFailure(string.Format("Fresh release-time native state differs for root[%1] UUID=%2 (sampled=%3).", i, current.Id, sampled));
			}
#ifdef EBG_ACCEPTANCE_TEST
			PrintFormat("[EBG Full Group RELEASE CHECK] root=%1 id=%2 stateParity=1 exactNativeJson=%3 chars=%4", i, current.Id, current.ReleaseSample == current.Before, current.ReleaseSample.Length());
#endif
		}
		foreach (EBG_FullGroupRoot restored : m_Roots)
		{
			SCR_ChimeraCharacter member = SCR_ChimeraCharacter.Cast(restored.Entity);
			if (member) member.EBG_SetSimulationCached(false);
			restored.ActivationObserved = false;
			if (restored.AgentState)
			{
				restored.AgentState.Restore();
				if (restored.AgentState.Agent) restored.ActivationObserved = restored.AgentState.Agent.IsAIActivated();
			}
		}
		m_ReleaseTime = GetGame().GetWorld().GetWorldTime();
		m_Phase = EBG_FullGroupPhase.RELEASING;
		m_Error = "Release commands dispatched; waiting for native policy/activation eligibility verification. Snapshots retained.";
		if (s_Owner == this) s_Owner = null;
		return true;
	}
	protected void PollRelease()
	{
		if (!RuntimeAllowed()) { Fail("Native runtime unavailable during release verification; snapshots retained."); return; }
		float elapsed = GetGame().GetWorld().GetWorldTime() - m_ReleaseTime;
		if (elapsed <= 0) return;
		SCR_AIGroup group = GetRestoredGroup();
		if (!group || group.IsDormant() || group.EBG_NativeDeleteWhenEmpty() != m_DeleteWhenEmpty)
		{ RetainAfterReleaseFailure("Released group missing, dormant or deletion policy changed."); return; }
		array<AIAgent> agents = {};
		group.GetAgents(agents);
		if (agents.Count() != m_Members) { RetainAfterReleaseFailure("Released roster count changed before completion."); return; }
		string pending;
		foreach (EBG_FullGroupRoot root : m_Roots)
		{
			if (!root.Entity || !m_System.IsTracked(root.Entity) || m_System.GetId(root.Entity) != root.Id || m_System.FindById(root.Id) != root.Entity)
			{ RetainAfterReleaseFailure(string.Format("Released root UUID binding unavailable: %1.", root.Id)); return; }
			if (!root.AgentState) continue;
			EBG_SimulationAgent saved = root.AgentState;
			AIAgent agent = SCR_AIGroup.Cast(root.Entity);
			SCR_ChimeraCharacter member = SCR_ChimeraCharacter.Cast(root.Entity);
			if (member)
			{
				if (!member.GetCharacterController() || member.GetCharacterController().IsDead() || EntityUtils.IsPlayer(member) || member.GetCharacterGroup() != group)
				{ RetainAfterReleaseFailure(string.Format("Released survivor life/ownership changed: %1.", root.Id)); return; }
				agent = SCR_AIUtils.GetAIAgent(member);
				if (member.EBG_IsSimulationCached() || !agents.Contains(agent))
				{ RetainAfterReleaseFailure(string.Format("Released survivor still cached or absent from roster: %1.", root.Id)); return; }
			}
			if (!agent || agent != saved.Agent)
			{ RetainAfterReleaseFailure(string.Format("Released agent binding changed: %1.", root.Id)); return; }
			bool active = agent.IsAIActivated();
			if (active) root.ActivationObserved = true;
			// Max LOD legitimately disables native agents. Require restoration of
			// the captured pin and an active acknowledgement when originally active;
			// do not fight later native distance scheduling or impose global LOD 0.
			bool eligible = active || agent.GetLOD() == AIAgent.GetMaxLOD();
			if (agent.GetPermanentLOD() != saved.PermanentLOD || (saved.Active && !root.ActivationObserved) || !eligible)
				pending = string.Format("id=%1 permanent=%2 expected=%3 active=%4 savedActive=%5 activationObserved=%6 LOD=%7", root.Id, agent.GetPermanentLOD(), saved.PermanentLOD, active, saved.Active, root.ActivationObserved, agent.GetLOD());
		}
		if (!pending.IsEmpty())
		{
			m_Error = "Waiting for native release eligibility: " + pending;
			if (elapsed >= 5000) RetainAfterReleaseFailure("Release verification exceeded 5 seconds: " + pending);
			return;
		}
		m_Phase = EBG_FullGroupPhase.RELEASED;
		m_Error = "Native UUID/roster bindings and captured activation/permanent-LOD policy verified; native distance scheduling remains in control.";
#ifdef EBG_ACCEPTANCE_TEST
		PrintFormat("[EBG Full Group RELEASE VERIFIED] roots=%1 elapsedMs=%2 capturedPolicy=1 nativeSchedulingEligible=1", m_Roots.Count(), elapsed);
		foreach (EBG_FullGroupRoot verified : m_Roots)
			if (verified.AgentState) PrintFormat("[EBG Full Group RELEASE AGENT] id=%1 permanent=%2 expected=%3 active=%4 savedActive=%5 activationObserved=%6 LOD=%7", verified.Id, verified.AgentState.Agent.GetPermanentLOD(), verified.AgentState.PermanentLOD, verified.AgentState.Agent.IsAIActivated(), verified.AgentState.Active, verified.ActivationObserved, verified.AgentState.Agent.GetLOD());
#endif
		// Restored components now own their identity. Release our strong references
		// only after verified success; every failed/pending recovery retains them.
		foreach (EBG_FullGroupRoot released : m_Roots) released.IdentityState = null;
	}
}
