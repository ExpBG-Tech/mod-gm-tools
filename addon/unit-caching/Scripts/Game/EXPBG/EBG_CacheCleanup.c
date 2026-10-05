// EXPBG GM Optimizer: server-only, positively owned casualty cleanup.
// Add this opt-out to mission/intel entities or their containing corpse/item.
[EntityEditorProps(category: "EXPBG/Optimizer", description: "Never delete this entity through EBG cleanup")]
class EBG_CleanupKeepComponentClass : ScriptComponentClass {}
class EBG_CleanupKeepComponent : ScriptComponent {}

// Exact native birth awaiting controller state propagation, never a late inventory scan.
class EBG_PendingNativeBelongings
{
	IEntity Item;
	SCR_ChimeraCharacter Owner;
	InventoryItemComponent Inventory;
	InventoryStorageSlot Slot;
	float Deadline;
	bool SlotChanged;
	void OnSlotChanged(InventoryStorageSlot oldSlot, InventoryStorageSlot newSlot)
	{
		if (oldSlot != newSlot) SlotChanged = true;
	}
	void Clear()
	{
		if (Inventory) Inventory.m_OnParentSlotChangedInvoker.Remove(OnSlotChanged);
		Item = null; Owner = null; Inventory = null; Slot = null;
	}
}

class EBG_CleanupObject
{
	IEntity Entity;
	EntityID LookupId;
	bool LookupBound;
	ref EBG_CacheGroup Group;
	ref EBG_CacheMember Member;
	InventoryItemComponent Inventory;
	UUID PersistentId;
	ref EBG_CleanupFullEntry PersistentMap;
	bool PermanentlyReleased;
	bool Corpse;
	bool NativeBelongings;
	bool FullDetached;
	bool Allowed;
	bool Held = true;
	bool DeathConfirmed;
	float DeathTime;
	bool NativeRequested;
	float NativeRemaining = -1;
	string ReleaseReason;
	void OnSlotChanged(InventoryStorageSlot oldSlot, InventoryStorageSlot newSlot)
	{
		if (EBG_CacheCleanup.Instance && Held && !FullDetached)
			EBG_CacheCleanup.Instance.CheckGroupTransfers(Group);
	}
}

class EBG_CleanupFullEntry
{
	ref EBG_CleanupObject Object;
	IEntity Original;
	UUID NativeId;
	UUID MemberId;
	UUID ParentId;
	IEntity ParentOriginal;
	int ParentEntry = -1;
	ResourceName Prefab;
	string ShapeSignature;
	bool WasHeld;
	int LoadedSlotKind;
	int MuzzleIndex = -1;
	bool DirectParentMuzzles;
	int BarrelIndex = -1;
	int LoadedAmmo;
	int LoadedMaxAmmo;
	string MuzzleType;
	ResourceName ParentPrefab;
	bool LoadedMagazine;
	string MagazineType;
	int MagazineAmmo;
	int MagazineMaxAmmo;
	ResourceName MagazineAmmoType;
	bool WeaponInventorySlot;
	bool StoredInventorySlot;
	ResourceName InventorySlotTemplate;
	bool CapturedMagazine;
	UUID PhysicalParentId;
	string StorageType;
	string InventorySlotType;
	string InventorySlotName;
	int InventorySlotId = -1;
	int InventorySlotCount;
	bool GeneratedDummyBelt;
	bool GeneratedWeaponSight;
	bool NativeClothSlot;
	bool NamedEntitySlot;
	bool NativeScabbardSlot;
	float BayonetBlood;
	ref EBG_FullIdentityMaterials BayonetMaterial;
	ref EBG_OptionalModState OptionalState;
	UUID SourceJacketId;
	ResourceName SourceJacketPrefab;
}
class EBG_CleanupFullTransfer
{
	// Private read-only inventory snapshot; never joins the cleanup singleton ledger.
	ref EBG_CacheCleanup Validator;
	ref EBG_CleanupFullTransfer Validation;
	ref EBG_CacheGroup Group;
	ref array<ref EBG_CleanupFullEntry> Entries = {};
	bool Detached;
	bool Completed;
}

class EBG_CleanupMappingFailure
{
	string Phase;
	string Reason;
	ResourceName Prefab;
	int Suppressed;
}

class EBG_CacheCleanup
{
	static ref EBG_CacheCleanup Instance;
	protected ref array<ref EBG_CleanupObject> m_Objects = {};
	protected ref map<EntityID, ref array<EBG_CleanupObject>> m_ObjectLookup = new map<EntityID, ref array<EBG_CleanupObject>>();
	protected ref array<IEntity> m_ReleasedForever = {};
	protected ref array<UUID> m_ReleasedIds = {};
	protected ref array<UUID> m_ReleasedLineageMembers = {};
	protected ref array<string> m_ReleasedOrigins = {};
	protected ref array<ref EBG_CacheGroup> m_RegisteredGroups = {};
	protected ref array<ref EBG_PendingNativeBelongings> m_PendingBirths = {};
	protected bool m_BirthRetryQueued;
	protected World m_World;
	protected string m_Reason;
	protected bool m_ValidationOnly;
	protected ref array<ref EBG_CleanupMappingFailure> m_MappingFailures = {};
	protected float m_NextMappingSummary;
	protected bool m_LastFailureReported;
	protected float m_LastDeletionScan = -1;
	// A casualty whose native delete is not confirmed is retried at most
	// CASUALTY_BLOCK_LIMIT times, CASUALTY_RETRY_SECONDS apart, then handed back to native
	// garbage; a keep-protected or intel casualty is parked at once. A rejection
	// deletes nothing of that casualty (EBG_CacheMember.CleanupBlocks/CleanupRetryAt).
	static const int CASUALTY_BLOCK_LIMIT = 3;
	static const float CASUALTY_RETRY_SECONDS = 60.0;
	protected string m_Phase;
	protected string m_Detail;
	// Owner of the tree SafeTree is proving; every node must belong to this member.
	protected EBG_CacheMember m_TreeOwner;
	// When set, SafeTree records every node it proved (one casualty's verified set).
	protected ref array<IEntity> m_TreeNodes;

	static EBG_CacheCleanup Get()
	{
		if (!Instance || Instance.m_World != GetGame().GetWorld())
		{
			if (Instance) ShutdownForWorldCleanup();
			Instance = new EBG_CacheCleanup();
			Instance.m_World = GetGame().GetWorld();
			// Slot changes observe transfers immediately; this is only a safety sweep.
			if (Replication.IsServer()) GetGame().GetCallqueue().CallLater(Instance.CheckTransfers, 60000, true);
		}
		return Instance;
	}
	protected static string Friendly(string reason)
	{
		if (reason.Contains("valuable intel")) { return "Cleanup held: protected intel"; }
		if (reason.Contains("Unowned, released or unapproved hierarchy node")) { return "Cleanup held: unregistered or protected contents"; }
		return reason;
	}
	string GetLastReason()
	{
		return Friendly(m_Reason);
	}
	void ImportPersistentNegatives(array<UUID> ids)
	{
		foreach (UUID id : ids) if (!id.IsNull() && !m_ReleasedIds.Contains(id)) m_ReleasedIds.Insert(id);
		EBG_MissionPersistenceState state = EBG_MissionPersistenceState.Get();
		if (!state) return;
		state.Ensure();
		foreach (UUID member : state.ReleasedLineageMembers) if (!m_ReleasedLineageMembers.Contains(member)) m_ReleasedLineageMembers.Insert(member);
		foreach (string origin : state.ReleasedOrigins) if (!m_ReleasedOrigins.Contains(origin)) m_ReleasedOrigins.Insert(origin);
	}
	bool ExportPersistentNegatives(array<UUID> ids)
	{
		foreach (IEntity entity : m_ReleasedForever) if (entity) RememberReleasedId(entity);
		foreach (UUID id : m_ReleasedIds) if (!ids.Contains(id)) ids.Insert(id);
		EBG_MissionPersistenceState state = EBG_MissionPersistenceState.Get();
		if (!state) return false;
		state.Ensure();
		foreach (UUID member : m_ReleasedLineageMembers) if (!state.ReleasedLineageMembers.Contains(member)) state.ReleasedLineageMembers.Insert(member);
		foreach (string origin : m_ReleasedOrigins) if (!state.ReleasedOrigins.Contains(origin)) state.ReleasedOrigins.Insert(origin);
		return ids.Count() <= 65536 && state.ReleasedOrigins.Count() <= 65536 && state.ReleasedLineageMembers.Count() <= 65536;
	}
	// Capture lineage while ownership is still established. No inventory is adopted.
	protected void RememberPersistentMaps(EBG_CacheGroup record)
	{
		PersistenceSystem system = PersistenceSystem.GetInstance();
		if (!system) return;
		foreach (EBG_CleanupObject object : m_Objects)
		{
			if (object.Group != record || !object.Held || !object.Entity || !object.Member || object.Member.WasPlayer) continue;
			UUID id = system.GetId(object.Entity);
			if (!id.IsNull()) object.PersistentId = id;
			EBG_CleanupFullEntry entry = new EBG_CleanupFullEntry();
			string reason;
			if (CaptureOriginalMapping(object, entry, system, reason, false))
			{
				entry.Object = null;
				object.PersistentMap = entry;
			}
		}
	}
	protected bool IsInertDeathLeafProof(EBG_CleanupObject object)
	{
		if (!object || object.Entity || object.Corpse || object.Held || object.Allowed || !object.PermanentlyReleased || !object.Member || !object.Member.Dead || object.Member.WasPlayer || !object.PersistentMap) return false;
		EBG_CleanupFullEntry entry = object.PersistentMap;
		bool known = object.NativeBelongings && (IsNativeBelongingsResource(entry.Prefab) || BelongingsModels().Contains(entry.Prefab));
		if (!object.NativeBelongings && IsHeadEntry(entry)) known = true;
		return known && object.PersistentId.IsNull() && entry.NativeId.IsNull() && entry.MemberId == object.Member.PersistentId && !entry.WasHeld && entry.ParentEntry == -1 && entry.ParentId.IsNull() && !entry.ParentOriginal;
	}
	protected bool CanExportInertCorpseHead(EBG_CleanupObject object, PersistenceSystem system)
	{
		if (!object || !object.Entity || object.Corpse || object.NativeBelongings || !object.Held || object.PermanentlyReleased || object.FullDetached || !object.PersistentId.IsNull() || (object.PersistentMap && !object.PersistentMap.NativeId.IsNull()) || !system.GetId(object.Entity).IsNull() || Find(object.Entity) != object) return false;
		EBG_CacheMember member = object.Member;
		if (!member || !member.Dead || member.WasPlayer || !member.Entity || member.Entity.EBG_WasPlayerControlled() || IsProtected(member.Entity)) return false;
		CharacterControllerComponent controller = member.Entity.GetCharacterController();
		EBG_CleanupObject corpse = Find(member.Entity);
		if (!controller || !controller.IsDead() || !corpse || !corpse.Corpse || !corpse.Held || !corpse.DeathConfirmed || corpse.Member != member || corpse.Group != object.Group) return false;
		CharacterIdentityComponent identity = CharacterIdentityComponent.Cast(member.Entity.FindComponent(CharacterIdentityComponent));
		return identity && identity.GetHeadEntity() == object.Entity && object.Entity.GetParent() == member.Entity && IsHeadLeaf(object.Entity) && ItemPolicyReason(object.Entity) == "";
	}
	protected bool NeverIdentifiedBelongings(EBG_CleanupObject object, PersistenceSystem system)
	{
		return object && object.Entity && object.NativeBelongings && !object.Corpse && object.Held && !object.PermanentlyReleased && !object.FullDetached && object.PersistentId.IsNull() && (!object.PersistentMap || object.PersistentMap.NativeId.IsNull()) && system.GetId(object.Entity).IsNull();
	}
	protected bool CanExportInertBelongings(EBG_CleanupObject object, PersistenceSystem system)
	{
		if (!NeverIdentifiedBelongings(object, system) || Find(object.Entity) != object) return false;
		EBG_CacheMember member = object.Member;
		if (!member || !member.Dead || member.WasPlayer || !member.Entity || member.Entity.EBG_WasPlayerControlled() || IsProtected(member.Entity)) return false;
		CharacterControllerComponent controller = member.Entity.GetCharacterController();
		EBG_CleanupObject corpse = Find(member.Entity);
		if (!controller || !controller.IsDead() || !corpse || !corpse.Corpse || !corpse.Held || !corpse.DeathConfirmed || corpse.Member != member || corpse.Group != object.Group) return false;
		IEntity root = object.Entity;
		if (!IsNativeBelongingsPrefab(root))
		{
			if (!BelongingsModels().Contains(SCR_ResourceNameUtils.GetPrefabName(root))) return false;
			root = root.GetParent();
		}
		EBG_CleanupObject rootObject = Find(root);
		if (!IsNativeBelongingsPrefab(root) || !NeverIdentifiedBelongings(rootObject, system) || rootObject.Member != member || rootObject.Group != object.Group || Holder(root) != member.Entity || ItemPolicyReason(root) != "") return false;
		bool belongs = object == rootObject;
		int children;
		for (IEntity child = root.GetChildren(); child; child = child.GetSibling())
		{
			if (++children > 8 || !BelongingsModels().Contains(SCR_ResourceNameUtils.GetPrefabName(child))) return false;
			EBG_CleanupObject childObject = Find(child);
			if (!NeverIdentifiedBelongings(childObject, system) || childObject.Member != member || childObject.Group != object.Group || ItemPolicyReason(child) != "") return false;
			if (childObject == object) belongs = true;
		}
		return belongs;
	}
	bool ExportPersistentGroup(EBG_MissionGroupData saved, EBG_CacheGroup record, out string reason)
	{
		PersistenceSystem system = PersistenceSystem.GetInstance();
		if (!system) return false;
		saved.LedgerInitialized = m_RegisteredGroups.Contains(record);
		array<IEntity> originals = {};
		foreach (EBG_CleanupObject object : m_Objects)
		{
			if (object.Group != record || !object.Member) continue;
			if (!object.Entity && !object.Corpse && !object.PermanentlyReleased) continue;
			if (saved.Objects.Count() >= 2048) { reason = "Original object count exceeds 2048"; return false; }
			EBG_MissionObjectData row = new EBG_MissionObjectData();
			row.MemberIndex = record.Members.Find(object.Member);
			if (row.MemberIndex < 0) { reason = "Original object's member is absent from logical roster"; return false; }
			row.Present = object.Entity != null; row.Corpse = object.Corpse;
			row.NativeBelongings = object.NativeBelongings; row.Allowed = object.Allowed;
			row.Held = object.Held; row.Released = object.PermanentlyReleased;
			row.DeathConfirmed = object.DeathConfirmed;
			if (row.DeathConfirmed) row.DeathAge = Math.Max(0, GetGame().GetWorld().GetWorldTime() * 0.001 - object.DeathTime);
			row.NativeRequested = object.NativeRequested; row.NativeRemaining = object.NativeRemaining;
			row.Map.MemberId = saved.Members[row.MemberIndex].Id;
			row.Map.NativeId = object.PersistentId;
			if (object.Entity)
			{
				UUID actualId = system.GetId(object.Entity);
				if (!actualId.IsNull()) { object.PersistentId = actualId; row.Map.NativeId = actualId; }
				row.Map.Prefab = SCR_ResourceNameUtils.GetPrefabName(object.Entity); row.Map.ShapeSignature = FullShape(object.Entity);
			}
			// Never grant next-world cleanup authority to an unaddressable death leaf.
			// Keep a fresh denial descriptor; current-world ownership stays untouched.
			if (IsInertDeathLeafProof(object) || CanExportInertBelongings(object, system) || CanExportInertCorpseHead(object, system))
			{
				if (!object.Entity)
				{
					row.Map.Prefab = object.PersistentMap.Prefab;
					row.Map.ShapeSignature = object.PersistentMap.ShapeSignature;
				}
				row.Present = false; row.Held = false; row.Allowed = false; row.Released = true;
				row.NativeRequested = false; row.NativeRemaining = -1;
			}
			// Released rows retain their ORIGINAL descriptor, never the foreign holder.
			else if (object.PermanentlyReleased)
			{
				if (object.PersistentMap) row.Map = object.PersistentMap;
				row.Map.WasHeld = false;
			}
			else if (object.Entity && object.Held && !saved.Members[row.MemberIndex].WasPlayer)
			{
				if (!CaptureOriginalMapping(object, row.Map, system, reason, false)) saved.Issue = "Original provenance unavailable: " + reason;
				SCR_WeaponAttachmentsStorageComponent rails = SCR_WeaponAttachmentsStorageComponent.Cast(object.Entity.FindComponent(SCR_WeaponAttachmentsStorageComponent));
				if (rails)
				{
					array<float> actualRails = {};
					if (!rails.EBG_ReadRHSRails(actualRails)) saved.Issue = "Original weapon rail state unavailable";
					else foreach (float offset : actualRails) if (offset != 10000) row.RailsPresent = true;
					if (row.RailsPresent) row.Rails = actualRails;
				}
				if (row.Map.BayonetMaterial && row.Map.BayonetMaterial.Present && row.Map.BayonetMaterial.Values && row.Map.BayonetMaterial.Values.Count() == 10)
				{
					row.BayonetPresent = true; row.Blood = row.Map.BayonetBlood;
					row.MaterialValues.Copy(row.Map.BayonetMaterial.Values);
					row.Wetness = row.Map.BayonetMaterial.Wetness; row.Drops = row.Map.BayonetMaterial.Drops;
				}
			}
			row.Map.Object = null;
			saved.Objects.Insert(row); originals.Insert(object.Entity);
		}
		for (int i = 0; i < saved.Objects.Count(); i++)
		{
			EBG_MissionObjectData row = saved.Objects[i];
			row.Map.ParentEntry = -1;
			for (int j = 0; j < saved.Objects.Count(); j++)
			{
				if (i == j) continue;
				if ((!row.Map.ParentId.IsNull() && row.Map.ParentId == saved.Objects[j].Map.NativeId) || (row.Map.ParentOriginal && row.Map.ParentOriginal == originals[j])) { row.Map.ParentEntry = j; break; }
			}
			if (!row.Valid(saved.Members.Count(), saved.Objects.Count())) { reason = "Original disk row is invalid"; return false; }
		}
		return true;
	}
	protected bool CanRollbackPersistentScalar(EBG_MissionScalarBackup backup)
	{
		EBG_MissionHeadBackup headBackup = EBG_MissionHeadBackup.Cast(backup);
		if (headBackup) return headBackup.Safe() && !m_ReleasedLineageMembers.Contains(headBackup.Target.Map.MemberId) && !m_ReleasedIds.Contains(headBackup.Target.Map.MemberId);
		PersistenceSystem system = PersistenceSystem.GetInstance();
		if (!system || !backup.Entity || !backup.Member || backup.Member.EBG_WasPlayerControlled()) return false;
		if (system.GetId(backup.Member) != backup.Target.Map.MemberId || backup.Entity.GetParent() != backup.OriginalParent || Holder(backup.Entity) != backup.OriginalHolder) return false;
		if (system.GetId(backup.Entity) != backup.ResolvedNativeId) return false;
		if (FullShape(backup.Entity) != backup.Target.Map.ShapeSignature || SCR_ResourceNameUtils.GetPrefabName(backup.Entity) != backup.Target.Map.Prefab) return false;
		if (m_ReleasedForever.Contains(backup.Entity) || m_ReleasedIds.Contains(system.GetId(backup.Entity)) || m_ReleasedLineageMembers.Contains(system.GetId(backup.Member))) return false;
		SCR_ChimeraCharacter holder = SCR_ChimeraCharacter.Cast(Holder(backup.Entity));
		if (holder && (holder != backup.Member || holder.EBG_WasPlayerControlled())) return false;
		if (backup.Target.Map.NativeScabbardSlot && backup.Member.EBG_FindClothBlade() != backup.Entity) return false;
		return true;
	}
	// Retry only retained rollback values, never loaded target setters or enrollment.
	bool RecoverPersistentScalars(EBG_CacheGroup record)
	{
		if (!record || !record.PersistentScalarRollbackPending) return true;
		bool restored = true;
		if (!record.PersistentScalarBackups) return false;
		// A later Restore ALL retry is a new ownership boundary. Validate all rows
		// before touching any; a player/transfer veto retains the recovery unchanged.
		foreach (EBG_MissionScalarBackup backup : record.PersistentScalarBackups)
			if (backup.Attempted && !CanRollbackPersistentScalar(backup)) return false;
		for (int i = record.PersistentScalarBackups.Count() - 1; i >= 0; i--)
			if (!record.PersistentScalarBackups[i].Rollback()) restored = false;
		if (restored) { record.PersistentScalarRollbackPending = false; record.PersistentScalarBackups = null; }
		return restored;
	}
	protected void ReportPersistentMappingFailure(EBG_MissionGroupData saved, int index, IEntity parent, IEntity entity, string reason)
	{
#ifdef EBG_ACCEPTANCE_TEST
		if (saved.MappingFailureReported) return;
		saved.MappingFailureReported = true;
		PersistenceSystem system = PersistenceSystem.GetInstance();
		EBG_MissionObjectData row = saved.Objects[index]; EBG_CleanupFullEntry entry = row.Map;
		PrintFormat("[EBG MISSION MAP] group=%1 row=%2 member=%3 original=%4 parentRow=%5 parentId=%6 reason='%7'", saved.GroupId, index, entry.MemberId, entry.NativeId, entry.ParentEntry, entry.ParentId, reason);
		PrintFormat("[EBG MISSION MAP EXPECTED] prefab='%1' shape='%2' present=%3 held=%4 loadedKind=%5 storage='%6' slot=%7 slotName='%8'", entry.Prefab, entry.ShapeSignature, row.Present, row.Held, entry.LoadedSlotKind, entry.StorageType, entry.InventorySlotId, entry.InventorySlotName);
		PrintFormat("[EBG MISSION MAP FLAGS] stored=%1 weapon=%2 cloth=%3 named=%4 scabbard=%5 sight=%6 dummy=%7 muzzle=%8 barrel=%9", entry.StoredInventorySlot, entry.WeaponInventorySlot, entry.NativeClothSlot, entry.NamedEntitySlot, entry.NativeScabbardSlot, entry.GeneratedWeaponSight, entry.GeneratedDummyBelt, entry.MuzzleIndex, entry.BarrelIndex);
		UUID parentId, actualId; ResourceName parentPrefab, actualPrefab; string actualShape;
		if (parent) { parentId = system.GetId(parent); parentPrefab = SCR_ResourceNameUtils.GetPrefabName(parent); }
		if (entity) { actualId = system.GetId(entity); actualPrefab = SCR_ResourceNameUtils.GetPrefabName(entity); actualShape = FullShape(entity); }
		PrintFormat("[EBG MISSION MAP ACTUAL] parentPresent=%1 parentId=%2 parentPrefab='%3' itemPresent=%4 itemId=%5 itemPrefab='%6' itemShape='%7'", parent != null, parentId, parentPrefab, entity != null, actualId, actualPrefab, actualShape);
		if (IsHeadEntry(entry)) ReportHeadIdentity("disk mapping failure", SCR_ChimeraCharacter.Cast(parent), entry.Prefab);
		if (entry.LoadedSlotKind > 0 && parent) ReportLoadedSlots("disk mapping failure", parent, entity);
		int shown;
		if (parent)
			for (IEntity child = parent.GetChildren(); child && shown < 6; child = child.GetSibling())
			{
				PrintFormat("[EBG MISSION MAP CHILD] id=%1 prefab='%2' shape='%3' exactPrefab=%4", system.GetId(child), SCR_ResourceNameUtils.GetPrefabName(child), FullShape(child), SCR_ResourceNameUtils.GetPrefabName(child) == entry.Prefab);
				shown++;
			}
#endif
	}
	bool ImportPersistentGroup(EBG_MissionGroupData saved, EBG_CacheGroup record, out string reason)
	{
		if (ImportPersistentGroupCore(saved, record, reason)) return true;
		if (reason == "Native original head regeneration settling") return false;
		if (record && record.PersistentScalarBackups)
		{
			bool changed;
			foreach (EBG_MissionScalarBackup backup : record.PersistentScalarBackups) if (backup.Attempted) changed = true;
			if (changed)
			{
				record.PersistentScalarFailure = true; record.PersistentScalarRollbackPending = true;
				RecoverPersistentScalars(record);
				if (record.PersistentScalarRollbackPending) reason += "; native identity/scalar rollback pending";
			}
			else { record.PersistentScalarBackups = null; record.PersistentScalarRollbackPending = false; }
		}
		return false;
	}
	protected bool ImportPersistentGroupCore(EBG_MissionGroupData saved, EBG_CacheGroup record, out string reason)
	{
		PersistenceSystem system = PersistenceSystem.GetInstance();
		if (!system || !record || !record.Zone) { reason = "Native world/zone unavailable"; return false; }
		if (record.PersistentScalarFailure) { reason = "Previous scalar failure retained; group excluded"; return false; }
		if (!EBG_MissionPersistence.NativeRosterMatches(saved, record, reason)) return false;
		array<ref EBG_MissionScalarBackup> retainedHeads = {};
		if (record.PersistentScalarBackups)
			foreach (EBG_MissionScalarBackup previous : record.PersistentScalarBackups)
			{
				EBG_MissionHeadBackup pendingHead = EBG_MissionHeadBackup.Cast(previous);
				if (!pendingHead) continue;
				if (!pendingHead.Safe()) { reason = "Original head identity/ownership changed while settling"; return false; }
				if (!pendingHead.Matches())
				{
					if (GetGame().GetWorld().GetWorldTime() * 0.001 - pendingHead.AppliedAt > 3) reason = "Native original head regeneration failed to settle";
					else reason = "Native original head regeneration settling";
					return false;
				}
				retainedHeads.Insert(pendingHead);
			}
		array<ref EBG_MissionHeadBackup> newHeads = {};
		array<ref EBG_CleanupObject> objects = {};
		array<IEntity> resolved = {};
		array<bool> done = {};
		foreach (EBG_MissionObjectData row : saved.Objects)
		{
			EBG_CleanupObject object = new EBG_CleanupObject();
			object.Group = record; object.Member = record.Members[row.MemberIndex];
			object.Corpse = row.Corpse; object.NativeBelongings = row.NativeBelongings;
			object.Allowed = row.Allowed; object.Held = row.Held;
			object.PermanentlyReleased = row.Released; object.PersistentId = row.Map.NativeId;
			object.DeathConfirmed = row.DeathConfirmed; object.DeathTime = GetGame().GetWorld().GetWorldTime() * 0.001 - row.DeathAge;
			object.NativeRequested = row.NativeRequested; object.NativeRemaining = row.NativeRemaining;
			if (object.Member.WasPlayer) { object.Held = false; object.Allowed = false; object.PermanentlyReleased = true; }
			objects.Insert(object); resolved.Insert(null); done.Insert(false);
		}
		int count;
		for (int pass = 0; pass < 32 && count < objects.Count(); pass++)
		{
			bool progress;
			for (int index = 0; index < objects.Count(); index++)
			{
				if (done[index]) continue;
				EBG_MissionObjectData row = saved.Objects[index];
				EBG_CleanupFullEntry entry = row.Map;
				EBG_CleanupObject object = objects[index];
				IEntity parent;
				if (entry.ParentEntry >= 0)
				{
					if (!done[entry.ParentEntry]) continue;
					if (saved.Objects[entry.ParentEntry].Map.MemberId != entry.MemberId) { reason = "Original parent belongs to another member"; return false; }
					parent = resolved[entry.ParentEntry];
				}
				else if (!entry.ParentId.IsNull()) parent = IEntity.Cast(system.FindById(entry.ParentId));
				IEntity entity;
				if (row.Present && !entry.NativeId.IsNull()) entity = IEntity.Cast(system.FindById(entry.NativeId));
				if (row.Present && object.Held)
				{
					entry.Object = object;
					bool mapped = ResolveOriginalMapping(entry, parent, system, entity, reason);
					entry.Object = null;
					bool correctingHead;
					if (mapped && !entity && entry.NativeId.IsNull() && IsHeadEntry(entry) && parent == object.Member.Entity && entry.ParentId == entry.MemberId)
					{
						if (row.Released || !row.Held || row.Corpse || object.Member.WasPlayer || object.Member.Dead || row.RailsPresent || row.BayonetPresent) { reason = "Original head row is not eligible living AI provenance"; return false; }
						foreach (EBG_CacheMember cohortMember : record.Members) if (cohortMember.WasPlayer || (cohortMember.Entity && cohortMember.Entity.EBG_WasPlayerControlled())) { reason = "Head correction requires an entirely nonplayer AI cohort"; return false; }
						int originalHeads;
						foreach (EBG_MissionObjectData headRow : saved.Objects) if (headRow.Map.MemberId == entry.MemberId && IsHeadEntry(headRow.Map)) originalHeads++;
						if (originalHeads != 1) { reason = "Original head provenance is not unique"; return false; }
						EBG_MissionHeadBackup headBackup = new EBG_MissionHeadBackup();
						if (!headBackup.CaptureHead(object, row)) { reason = "Native current head ownership/identity contract unavailable"; return false; }
						foreach (EBG_CacheMember identityMember : record.Members)
						{
							if (!identityMember.Entity || identityMember.Entity == object.Member.Entity) continue;
							CharacterIdentityComponent otherIdentity = CharacterIdentityComponent.Cast(identityMember.Entity.FindComponent(CharacterIdentityComponent));
							if (otherIdentity && otherIdentity.GetIdentity() && (otherIdentity.GetIdentity() == headBackup.IdentityValue || otherIdentity.GetIdentity().GetVisualIdentity() == headBackup.Visual)) { reason = "Native identity objects are shared across original members"; return false; }
						}
						entity = headBackup.Component.GetHeadEntity(); newHeads.Insert(headBackup); correctingHead = true;
					}
					if (!mapped || !entity)
					{
						if (reason == "") { reason = "Original object identity unresolved"; }
						ReportPersistentMappingFailure(saved, index, parent, entity, reason);
						return false;
					}
					if (resolved.Contains(entity) || Find(entity) || FullShape(entity) != entry.ShapeSignature || (!correctingHead && SCR_ResourceNameUtils.GetPrefabName(entity) != entry.Prefab)) { reason = "Original object shape/identity aliases or changed"; return false; }
					IEntity holder = Holder(entity);
					bool ownHolder = holder == object.Member.Entity || holder == entity;
					if (!ownHolder)
						foreach (EBG_CleanupObject ancestor : objects) if (ancestor.Entity == holder && ancestor.Member == object.Member && ancestor.Held) ownHolder = true;
					if (!ownHolder || m_ReleasedIds.Contains(entry.NativeId) || m_ReleasedIds.Contains(system.GetId(entity))) { reason = "Original object transferred or permanently released"; return false; }
					if (entry.StoredInventorySlot)
					{
						entry.Object = object; bool slotMatches = MatchesStoredInventorySlot(entry, parent, entity, system, true); entry.Object = null;
						if (!slotMatches) { reason = "Original stored slot differs"; return false; }
					}
					else if (!entry.NativeClothSlot && (entry.ParentEntry >= 0 || !entry.ParentId.IsNull()) && entity.GetParent() != parent) { reason = "Original physical parent differs"; return false; }
					if (object.Corpse && entity != object.Member.Entity) { reason = "Original corpse/member identity differs"; return false; }
					if (!object.Corpse && object.Allowed && ItemPolicyReason(entity) != "") { reason = "Original equipment policy changed"; return false; }
				}
				// Absent proof-only and released rows do not authorize replacements.
				object.Entity = entity; resolved[index] = entity; done[index] = true; count++; progress = true;
			}
			if (!progress) { reason = "Original lineage cycle or depth exceeded"; return false; }
		}
		if (count != objects.Count()) { reason = "Original lineage exceeds 32 levels"; return false; }
		// Preflight every scalar and capture rollback values before the first setter.
		record.PersistentScalarBackups = retainedHeads;
		foreach (EBG_MissionHeadBackup newHead : newHeads) record.PersistentScalarBackups.Insert(newHead);
		for (int stateIndex = 0; stateIndex < objects.Count(); stateIndex++)
		{
			EBG_CleanupObject object = objects[stateIndex]; EBG_MissionObjectData row = saved.Objects[stateIndex];
			if (!object.Entity || !object.Held || object.Member.WasPlayer || object.PermanentlyReleased || (!row.RailsPresent && !row.BayonetPresent)) continue;
			EBG_MissionScalarBackup backup = new EBG_MissionScalarBackup();
			if (!backup.Preflight(object, row)) { reason = "Saved scalar contract preflight failed"; return false; }
			backup.OriginalHolder = Holder(object.Entity);
			backup.ResolvedNativeId = system.GetId(object.Entity);
			record.PersistentScalarBackups.Insert(backup);
		}
		if (!newHeads.IsEmpty())
		{
			// Every nonhead row and scalar contract passed before native regeneration.
			record.PersistentScalarRollbackPending = true;
			foreach (EBG_MissionHeadBackup applyHead : newHeads)
				if (!applyHead.Apply()) { reason = "Native original head setter precondition changed"; return false; }
			reason = "Native original head regeneration settling";
			return false; // Re-resolve the entire ledger on the next native frame.
		}
		bool scalarFailed;
		foreach (EBG_MissionScalarBackup apply : record.PersistentScalarBackups)
			if (!apply.Apply()) { scalarFailed = true; break; }
		if (!scalarFailed)
			foreach (EBG_MissionScalarBackup verify : record.PersistentScalarBackups)
				if (!verify.Matches()) { scalarFailed = true; break; }
		if (scalarFailed)
		{
			record.PersistentScalarFailure = true;
			record.PersistentScalarRollbackPending = true;
			RecoverPersistentScalars(record);
			reason = "Saved scalar setter failed; group excluded";
			if (record.PersistentScalarRollbackPending) reason = "Saved scalar rollback pending; saving blocked";
			return false;
		}
		// All setters and getters succeeded synchronously. Publish exactly once;
		// unsuccessful attempts never schedule optimizer replication samplers.
		foreach (EBG_MissionScalarBackup publish : record.PersistentScalarBackups) publish.Publish();
		record.PersistentScalarBackups = null; record.PersistentScalarRollbackPending = false;
		if (saved.LedgerInitialized && !m_RegisteredGroups.Contains(record)) m_RegisteredGroups.Insert(record);
		for (int committed = 0; committed < objects.Count(); committed++)
		{
			EBG_CleanupObject object = objects[committed];
			object.PersistentMap = saved.Objects[committed].Map;
			InsertObject(object);
			if (!object.Entity) continue;
			object.PersistentId = system.GetId(object.Entity);
			object.Inventory = InventoryItemComponent.Cast(object.Entity.FindComponent(InventoryItemComponent));
			if (object.Held && object.Inventory) object.Inventory.m_OnParentSlotChangedInvoker.Insert(object.OnSlotChanged);
			if (object.PermanentlyReleased)
			{
				if (!m_ReleasedForever.Contains(object.Entity)) m_ReleasedForever.Insert(object.Entity);
				RememberReleasedId(object.Entity);
			}
			MaintainNativeProtection(object);
		}
		record.CleanupRegistered = saved.LedgerInitialized;
		record.CleanupCasualtiesSettled = false;
		RearmDrained(record);
		record.CleanupClearSince = -1; record.ClearSince = -1;
		return true;
	}
#ifdef EBG_ACCEPTANCE_TEST
	// Independent test read of installed ownership flags and actual equipment getters.
	bool VerifyPersistentGroup(EBG_MissionGroupData expected, EBG_CacheGroup record, out string reason)
	{
		if (!record || record.PersistenceIssue != "" || record.Members.Count() != expected.Members.Count() || m_RegisteredGroups.Contains(record) != expected.LedgerInitialized || record.CleanupClearSince != -1) { reason = "Loaded record/ledger/clearance is not ready"; return false; }
		array<ref EBG_CleanupObject> observed = {};
		foreach (EBG_CleanupObject object : m_Objects) if (object.Group == record) observed.Insert(object);
		if (observed.Count() != expected.Objects.Count()) { reason = "Loaded original ownership row count differs"; return false; }
		for (int i = 0; i < observed.Count(); i++)
		{
			EBG_CleanupObject object = observed[i]; EBG_MissionObjectData row = expected.Objects[i];
			if (object.Member != record.Members[row.MemberIndex] || object.Corpse != row.Corpse || object.Held != row.Held || object.Allowed != row.Allowed || object.PermanentlyReleased != row.Released || object.DeathConfirmed != row.DeathConfirmed || !row.SameMapping(object.PersistentMap)) { reason = "Loaded positive/negative row differs from writer oracle"; return false; }
			if (row.Present && object.Held && !object.Entity) { reason = "Expected original native entity is absent"; return false; }
			if (row.DeathConfirmed && Math.AbsFloat((GetGame().GetWorld().GetWorldTime() * 0.001 - object.DeathTime) - row.DeathAge) > 15) { reason = "Saved corpse age differs beyond bounded load observation"; return false; }
			if (row.NativeRequested && (!object.NativeRequested || object.NativeRemaining != row.NativeRemaining)) { reason = "Saved suspended native lifetime differs"; return false; }
			if (row.RailsPresent)
			{
				SCR_WeaponAttachmentsStorageComponent rails;
				if (object.Entity) rails = SCR_WeaponAttachmentsStorageComponent.Cast(object.Entity.FindComponent(SCR_WeaponAttachmentsStorageComponent));
				if (!rails || !rails.EBG_RHSRailsMatch(row.Rails)) { reason = "Actual loaded rail getters differ"; return false; }
			}
			if (row.BayonetPresent)
			{
				SCR_BayonetComponent bayonet;
				if (object.Entity) bayonet = SCR_BayonetComponent.Cast(object.Entity.FindComponent(SCR_BayonetComponent));
				EBG_FullIdentityMaterials material = new EBG_FullIdentityMaterials(); material.Present = true;
				material.Values = row.MaterialValues; material.Wetness = row.Wetness; material.Drops = row.Drops;
				if (!bayonet || bayonet.EBG_GetBloodStainLevel() != row.Blood || !material.Matches(object.Entity)) { reason = "Actual loaded bayonet getters differ"; return false; }
			}
		}
		return true;
	}
#endif
	// World teardown only: detach listeners before engine-owned entities disappear.
	// Never resume garbage timers while their owning world is being destroyed.
	static void ShutdownForWorldCleanup()
	{
		if (!Instance) return;
		GetGame().GetCallqueue().Remove(Instance.CheckTransfers);
		GetGame().GetCallqueue().Remove(Instance.RetryPendingNativeBirths);
		foreach (EBG_PendingNativeBelongings birth : Instance.m_PendingBirths) birth.Clear();
		Instance.m_PendingBirths.Clear();
		foreach (EBG_CleanupObject object : Instance.m_Objects)
		{
			if (object.Inventory) object.Inventory.m_OnParentSlotChangedInvoker.Remove(object.OnSlotChanged);
			object.Held = false;
		}
		Instance.m_Objects.Clear();
		Instance.m_ObjectLookup.Clear();
		Instance.m_ReleasedForever.Clear();
		Instance.m_ReleasedIds.Clear();
		Instance.m_RegisteredGroups.Clear();
		Instance = null;
	}
	protected EBG_CleanupObject Find(IEntity entity)
	{
		if (!entity) return null;
		array<EBG_CleanupObject> entries = m_ObjectLookup.Get(entity.GetID());
		if (!entries) return null;
		foreach (EBG_CleanupObject object : entries)
			if (object && object.Entity == entity) return object;
		return null;
	}
	// The ordered ledger owns rows; this lookup only accelerates exact live identity.
	// Retained negative rows can alias across saves, so keep canonical first-match order.
	protected void IndexObject(EBG_CleanupObject object)
	{
		if (!object.Entity) return;
		object.LookupId = object.Entity.GetID();
		object.LookupBound = true;
		array<EBG_CleanupObject> entries = m_ObjectLookup.Get(object.LookupId);
		if (!entries)
		{
			entries = {};
			m_ObjectLookup.Set(object.LookupId, entries);
		}
		int at = entries.Count();
		if (at > 0)
		{
			int order = m_Objects.Find(object);
			for (int i = 0; i < entries.Count(); i++)
				if (m_Objects.Find(entries[i]) > order) { at = i; break; }
		}
		entries.InsertAt(object, at);
	}
	protected void UnindexObject(EBG_CleanupObject object)
	{
		if (!object.LookupBound) return;
		array<EBG_CleanupObject> entries = m_ObjectLookup.Get(object.LookupId);
		if (entries)
		{
			entries.RemoveItemOrdered(object);
			if (entries.IsEmpty()) m_ObjectLookup.Remove(object.LookupId);
		}
		object.LookupBound = false;
	}
	protected void InsertObject(EBG_CleanupObject object)
	{
		m_Objects.Insert(object);
		IndexObject(object);
	}
	protected void RemoveObject(int index)
	{
		// Native deletion may already have nulled Entity; use the retained key.
		UnindexObject(m_Objects[index]);
		EBG_CleanupObject moved;
		if (index < m_Objects.Count() - 1)
		{
			moved = m_Objects[m_Objects.Count() - 1];
			UnindexObject(moved);
		}
		m_Objects.Remove(index);
		// Remove swaps in the last row. Its alias priority follows its new position.
		if (moved) IndexObject(moved);
	}
	protected void RebindObject(EBG_CleanupObject object, IEntity entity)
	{
		UnindexObject(object);
		object.Entity = entity;
		IndexObject(object);
	}
	bool IsHeld(IEntity entity)
	{
		EBG_CleanupObject object = Find(entity);
		return object && object.Held;
	}
	protected bool IsDetachedProjectile(IEntity entity)
	{
		return entity && entity.FindComponent(BaseProjectileComponent) && Holder(entity) == entity;
	}
	// Held records establish provenance; disabled cleanup must not veto native garbage.
	bool IsGarbageProtected(IEntity entity)
	{
		return IsObjectGarbageProtected(Find(entity));
	}
	protected bool IsObjectGarbageProtected(EBG_CleanupObject object)
	{
		return object && object.Entity && object.Held && object.Group && object.Group.Zone && object.Group.Zone.Enabled && object.Group.Zone.Cleanup && !IsDetachedProjectile(object.Entity);
	}
	protected void MaintainNativeProtection(EBG_CleanupObject object)
	{
		if (!object || !object.Entity || object.FullDetached) return;
		SCR_GarbageSystem garbage = SCR_GarbageSystem.GetByEntityWorld(object.Entity);
		if (!garbage) return;
		if (IsObjectGarbageProtected(object))
		{
			if (garbage.IsInserted(object.Entity))
			{
				if (!object.NativeRequested) { object.NativeRequested = true; object.NativeRemaining = garbage.GetRemainingLifetime(object.Entity); }
				bool withdrawn = garbage.Withdraw(object.Entity);
#ifdef EBG_ACCEPTANCE_TEST
				EBG_CleanupGroundTimerProof.ObserveNativeHook(object.Entity, "maintenance withdraw", object.NativeRemaining, withdrawn);
#endif
			}
			return;
		}
		if (!object.NativeRequested) return;
		float remaining = object.NativeRemaining;
		object.NativeRequested = false;
		object.NativeRemaining = -1;
		// Native <=0 requests use configured rule lifetimes; never shorten them to one second.
		if (!garbage.IsInserted(object.Entity)) garbage.Insert(object.Entity, remaining);
	}

	// This stock loadout attachment has a real LoadoutSlotInfo with no storage.
	// Verify the authored slot and the currently worn vest before using its wearer.
	protected IEntity NativeVestAccessoryOwner(IEntity item, InventoryStorageSlot slot)
	{
		if (!item || !slot || slot.Type().ToString() != "LoadoutSlotInfo" || slot.GetStorage() || slot.GetAttachedEntity() != item || slot.GetSourceName().IsEmpty()) { return null; }
		ResourceName prefab = SCR_ResourceNameUtils.GetPrefabName(item);
		if (!ApprovedPrefabs().Contains(prefab) || slot.GetSlotTemplate() != prefab) { return null; }
		SCR_ChimeraCharacter wearer = SCR_ChimeraCharacter.Cast(item.GetParent());
		if (!wearer) { return null; }
		SCR_CharacterInventoryStorageComponent inventory = SCR_CharacterInventoryStorageComponent.Cast(wearer.FindComponent(SCR_CharacterInventoryStorageComponent));
		if (!inventory) { return null; }
		IEntity vest = slot.GetOwner();
		if (!vest || !ApprovedPrefabs().Contains(SCR_ResourceNameUtils.GetPrefabName(vest)) || IsProtected(vest)) { return null; }
		// The stock 6B3 carries these authored slots in the armored-vest area.
		if (inventory.GetClothFromArea(LoadoutVestArea) != vest && inventory.GetClothFromArea(LoadoutArmoredVestSlotArea) != vest) { return null; }
		// Native loadout slots belong to the worn vest's cloth component, while
		// their attached accessory is parented directly to the character.
		if (slot.GetOwner() != vest || vest.GetParent() != wearer || slot.GetParentContainer() != vest.FindComponent(BaseLoadoutClothComponent)) { return null; }
		return wearer;
	}
	// An inventory hierarchy is followed through slot/storage ownership, not only
	// IEntity parentage. The depth limit treats pathological hierarchies as unknown.
	protected IEntity Holder(IEntity item)
	{
		IEntity current = item;
		for (int depth = 0; current && depth < 32; depth++)
		{
			if (SCR_ChimeraCharacter.Cast(current)) { return current; }
			InventoryItemComponent component = InventoryItemComponent.Cast(current.FindComponent(InventoryItemComponent));
			if (!component || !component.GetParentSlot())
			{
				if (!current.GetParent()) { return current; }
				current = current.GetParent();
				continue;
			}
			BaseInventoryStorageComponent storage = component.GetParentSlot().GetStorage();
			if (!storage)
			{
				return NativeVestAccessoryOwner(current, component.GetParentSlot());
			}
			if (!storage.GetOwner() || storage.GetOwner() == current) { return null; }
			current = storage.GetOwner();
		}
		return null;
	}
	protected bool IsProtected(IEntity entity)
	{
		return !entity || entity.FindComponent(EBG_CleanupKeepComponent) != null;
	}
	protected bool ProtectedOwnerChain(IEntity entity)
	{
		IEntity current = entity;
		for (int depth = 0; current && depth < 32; depth++)
		{
			if (IsProtected(current)) { return true; }
			InventoryItemComponent inventory = InventoryItemComponent.Cast(current.FindComponent(InventoryItemComponent));
			if (inventory && inventory.GetParentSlot())
			{
				BaseInventoryStorageComponent storage = inventory.GetParentSlot().GetStorage();
				if (!storage)
				{
					current = NativeVestAccessoryOwner(current, inventory.GetParentSlot());
					if (!current)
					{
						return true;
					}
				}
				else
				{
					if (storage.GetOwner() == current)
					{
						return true;
					}
					current = storage.GetOwner();
				}
			}
			else current = current.GetParent();
		}
		return current != null;
	}

	// Exact installed 1.8.0.13 standard US/USSR squad kit and authored attachment resources.
	// Keep this list explicit: unknown prefab variants and unknown runtime components fail closed.
	// Built on first use rather than as static field initializers. The engine compiles every
	// static initializer of every loaded addon into one shared function, and a large mod list
	// overflows it: "Too many instructions per function" at these declarations with 224
	// workshop addons (2026-09-18). A table built inside its own accessor spends that
	// accessor's budget instead. Contents and lookups are unchanged; the table is constant
	// data, so one build per process needs no per-world reset.
	protected static ref array<ResourceName> s_ApprovedPrefabs;
	protected static array<ResourceName> ApprovedPrefabs()
	{
		if (s_ApprovedPrefabs) return s_ApprovedPrefabs;
		array<ResourceName> values = {
			// Stock USSR senior-rifleman kit, inspected in installed 1.8.0.13 data.
			"{4CBDC206FEF9897C}Prefabs/Characters/Vests/Vest_6B3/Vest_6B3.et",
			"{EB404DC9E1BCB750}Prefabs/Weapons/Rifles/AK74/Rifle_AK74N_1P29.et",
			"{ACDF49FACD0701A8}Prefabs/Weapons/Attachments/Optics/Optic_1P29/Optic_1P29.et",
			"{D92368B78263A9E0}Prefabs/Weapons/Attachments/Mounts/Dovetail_AK/Dovetail_AK.et",
			"{47665331979536BC}Prefabs/Weapons/Ammo/Ammo_Flare_40mm_StarParachute_White.et",
			"{3892CBF73CC2A05F}Prefabs/Weapons/Ammo/Ammo_Flare_30mm_RSP30_White.et",
			"{924FB4A21503BBA2}Prefabs/Weapons/Ammo/Ammo_Flare_40mm_StarParachute_Red.et",
			"{896EA6475D6451DF}Prefabs/Weapons/Ammo/Ammo_Flare_40mm_StarParachute_Green.et",
			"{D924100C5EA67F89}Prefabs/Weapons/Ammo/Ammo_Flare_30mm_RSP30_Red.et",
			"{F69A3E81F633C73C}Prefabs/Weapons/Ammo/Ammo_Flare_30mm_RSP30_Green.et",
			"{79FA751EEBE25DDE}Prefabs/Weapons/Ammo/Ammo_Rocket_M72A3.et",
			"{ECCAF9A1B72B4D2B}Prefabs/Weapons/Ammo/Ammo_Rocket_PG22.et",
			"{00E36F41CA310E2A}Prefabs/Items/Medicine/SalineBag_01/SalineBag_US_01.et",
			"{02DF51DB063ABD36}Prefabs/Characters/Vests/Vest_ALICE/Vest_ALICE_firstaid.et",
			"{03C1F3DB6BB9796A}Prefabs/Characters/HeadGear/Helmet_PASGT_01/Helmet_PASGT_01_cover_w_scrim.et",
			"{062E2F1D7F6739D6}Prefabs/Items/Equipment/Accessories/ETool_MPL50/ETool_MPL50_FreeRoamBuilding_Gadget.et",
			"{06D4C36A6D585275}Prefabs/Weapons/Attachments/Muzzle/FlashHider_AKS74u/FlashHider_AKS74u.et",
			"{06D722FC2666EB83}Prefabs/Weapons/Magazines/Box_556x45_M249_200rnd_4Ball_1Tracer.et",
			"{08155E701A949620}Prefabs/Characters/Vests/Vest_SovietHarness/Variants/Vest_SovietHarness_rifleman.et",
			"{0A84AA5A3884176F}Prefabs/Weapons/Magazines/Magazine_545x39_AK_30rnd_Last_5Tracer.et",
			"{0AF10C206CF1A283}Prefabs/Weapons/Ammo/Ammo_Flare_40mm_M661_Green.et",
			"{0B0D67BC8F43A052}Prefabs/Items/Equipment/Accessories/Pouch_ALICE_30rnd_STANAG/Pouch_ALICE_30rnd_STANAG_base.et",
			"{0CF54B9A85D8E0D4}Prefabs/Items/Equipment/Binoculars/Binoculars_M22/Binoculars_M22.et",
			"{0D39750E5695B9D8}Prefabs/Items/Equipment/Backpacks/Backpack_RPG_Gunner.et",
			"{0D9A5DCF89AE7AA9}Prefabs/Items/Medicine/MorphineInjection_01/MorphineInjection_01.et",
			"{1353C6EAD1DCFE43}Prefabs/Weapons/Handguns/M9/Handgun_M9.et",
			"{14C1A0F061D9DDEE}Prefabs/Weapons/Grenades/M18/Smoke_M18_Violet.et",
			"{15067AD09803580D}Prefabs/Characters/Vests/Vest_SovietHarness/Variants/Vest_SovietHarness_MG.et",
			"{156DC7109CEE6F69}Prefabs/Characters/Vests/Vest_ALICE/Variants/Vest_ALICE_AR.et",
			"{1663496AE5B9F10B}Prefabs/Weapons/Ammo/Ammo_Grenade_HEDP_M433.et",
			"{18B8B9316B590643}Prefabs/Characters/Vests/Vest_ALICE/Variants/Vest_ALICE_GL.et",
			"{1ABABE3551512B0A}Prefabs/Weapons/Attachments/Underbarrel/UGL_GP25.et",
			"{21EF98BFC1EB3793}Prefabs/Items/Equipment/Kits/MedicalKit_01/MedicalKit_01_USSR.et",
			"{22963D69CA50EB9E}Prefabs/Characters/HeadGear/Helmet_SSh68_01/Helmet_SSh68_01_net.et",
			"{243948B23D90BECB}Prefabs/Items/Equipment/Binoculars/Binoculars_B8/Binoculars_B8.et",
			"{262F0D09C4130826}Prefabs/Weapons/Ammo/Ammo_Grenade_HE_VOG25.et",
			"{2835A0EA3B79E63E}Prefabs/Characters/Vests/Vest_ALICE/Variants/Vest_ALICE_rifleman.et",
			"{2A63C909016C4C41}Prefabs/Weapons/Ammo/Ammo_Flare_40mm_M662_Red.et",
			"{327103CB218E7CA1}Prefabs/Weapons/Flares/Flare_RSP30_white.et",
			"{32E12D322E107F1C}Prefabs/Weapons/Ammo/Ammo_Rocket_PG7VM.et",
			"{3343A055A83CB30D}Prefabs/Weapons/Grenades/M18/Smoke_M18_Red.et",
			"{36218D5F0C7095E6}Prefabs/Weapons/Flares/Flare_RSP30_red.et",
			"{3A421547BC29F679}Prefabs/Items/Equipment/Flashlights/Flashlight_MX991/Flashlight_MX991.et",
			"{3CCA7A9BB4FD3197}Prefabs/Characters/Uniforms/Jacket_US_BDU_rolledup.et",
			"{3E413771E1834D2F}Prefabs/Weapons/Rifles/M16/Rifle_M16A2.et",
			"{41A9C55B61F375F0}Prefabs/Items/Equipment/Backpacks/Backpack_Kolobok.et",
			"{43FDAF3FA0FF2299}Prefabs/Weapons/Attachments/Underbarrel/UGL_M203_long.et",
			"{4711A4CAF64C4CEE}Prefabs/Characters/Vests/Vest_SovietHarness/Variants/Vest_SovietHarness_AR.et",
			"{477A190AF2A17B8A}Prefabs/Characters/Vests/Vest_ALICE/Variants/Vest_ALICE_MG.et",
			"{4805E67E2AE30F8D}Prefabs/Items/Equipment/Backpacks/Backpack_Medical_M5.et",
			"{489C0EF4C8D47EBC}Prefabs/Items/Equipment/Accessories/ETool_ALICE/ETool_ALICE_carrier_base.et",
			"{4A815EB8B824974A}Prefabs/Weapons/Attachments/Muzzle/FlashHider_AK74/FlashHider_AK74.et",
			"{4B57C11AA5161760}Prefabs/Characters/Vests/Vest_PASGT/Vest_PASGT.et",
			"{4D2C1E8F3A81F894}Prefabs/Weapons/Magazines/Box_762x51_M60_100rnd_4Ball_1Tracer.et",
			"{4F018E3CF74D8636}Prefabs/Items/Equipment/Accessories/Pouch_ALICE_200rnd_M249/Pouch_ALICE_200rnd_M249.et",
			"{50084B2B2B8004FA}Prefabs/Items/Equipment/Accessories/Holster_PM/Holster_PM.et",
			"{51545851688567F1}Prefabs/Characters/Vests/Vest_ALICE/Vest_ALICE_suspenders_1.et",
			"{51D9E3AEC8476BA4}Prefabs/Weapons/Flares/FlareStarParachute_M195_green.et",
			"{54C68E438DD34265}Prefabs/Items/Equipment/Radios/Radio_R107M.et",
			"{558117556F3880A8}Prefabs/Weapons/Attachments/Bayonets/Bayonet_M9.et",
			"{575EA58E67448C2A}Prefabs/Items/Equipment/Flashlights/Flashlight_Soviet_01/Flashlight_Soviet_01.et",
			"{5A987A8A13763769}Prefabs/Weapons/Rifles/M16/Rifle_M16A2_M203.et",
			"{5C5C6EE05EE2FF1A}Prefabs/Items/Equipment/Backpacks/Backpack_ALICE_Medium_assembled.et",
			"{604BB72BE8E023C2}Prefabs/Characters/Uniforms/Pants_US_BDU.et",
			"{61D4F80E49BF9B12}Prefabs/Items/Equipment/Compass/Compass_SY183.et",
			"{6288A1F1A5E3AC37}Prefabs/Weapons/Attachments/Muzzle/FlashHider_M16A2/FlashHider_M16.et",
			"{630FB4AD4A735264}Prefabs/Items/Equipment/Canteens/Canteen_US_01.et",
			"{63E8322E2ADD4AA7}Prefabs/Weapons/Rifles/AK74/Rifle_AK74_GP25.et",
			"{645C73791ECA1698}Prefabs/Weapons/Grenades/Grenade_RGD5.et",
			"{66196D85AB93D2BE}Prefabs/Characters/HeadGear/Helmet_SSh68_01/Helmet_SSh68_01_camo.et",
			"{6726C63F4F56F12F}Prefabs/Characters/Vests/Vest_SovietHarness/Variants/Vest_SovietHarness_medic.et",
			"{6A39B5843B3F36DA}Prefabs/Items/Equipment/Backpacks/Backpack_RPG_Assistant.et",
			"{6B9646EC0757693C}Prefabs/Items/Equipment/Accessories/Holster_M12/Holster_M12.et",
			"{6E35D94130954509}Prefabs/Items/Equipment/Accessories/ETool_ALICE/ETool_ALICE_FreeRoamBuilding_Gadget.et",
			"{6FD6C96121905202}Prefabs/Items/Equipment/Watches/Watch_Vostok.et",
			"{70BC751317551D9B}Prefabs/Items/Equipment/Canteens/Canteen_Soviet_01.et",
			"{722CE6FEC39EE896}Prefabs/Weapons/Launchers/RPG22/Launcher_RPG22.et",
			"{725C5E1C75CADAF4}Prefabs/Characters/Vests/Vest_M69/Vest_M69_M81woodland.et",
			"{73950FBA2D7DB5C5}Prefabs/Items/Equipment/Radios/Radio_ANPRC68.et",
			"{756231FF84F158A4}Prefabs/Weapons/Flares/FlareStarParachute_M126A1_red.et",
			"{77EAE5E07DC4678A}Prefabs/Weapons/Grenades/Smoke_RDG2.et",
			"{78ED4FEF62BBA728}Prefabs/Items/Equipment/Watches/Watch_SandY184A.et",
			"{7AC107CA7AFC9B59}Prefabs/Items/Equipment/Backpacks/Backpack_Medical_Soviet.et",
			"{7CEF68E2BC68CE71}Prefabs/Items/Equipment/Compass/Compass_Adrianov.et",
			"{7DF2186D2BEC4A63}Prefabs/Characters/Vests/Vest_SovietHarness/Vest_SovietHarness_belt_dummy_Inflatable.et",
			"{80E75A71C29190DB}Prefabs/Items/Medicine/Tourniquet_01/Tourniquet_USSR_01.et",
			"{8B853CDD11BA916E}Prefabs/Weapons/Magazines/Magazine_9x18_PM_8rnd_Ball.et",
			"{8C9F39278B899AE6}Prefabs/Items/Equipment/Accessories/Pouch_Soviet_30rnd_AK74/Pouch_Soviet_30rnd_AK74.et",
			"{906F07BD0366E08F}Prefabs/Weapons/Ammo/Ammo_Flare_40mm_VG40OP_White.et",
			"{922F95F91943F69A}Prefabs/Items/Equipment/Maps/Map_Paper_01/PaperMap_01_folded_US.et",
			"{9713FE6DDCC9510D}Prefabs/Characters/Vests/Vest_Lifchik/Vest_Lifchik.et",
			"{98C79F5FAE12F9B6}Prefabs/Weapons/Attachments/Bayonets/Bayonet_6Kh4.et",
			"{98DB57ECEDC81CC2}Prefabs/Weapons/Ammo/Ammo_Flare_40mm_M583A1_White.et",
			"{9945D5435F12C370}Prefabs/Characters/Vests/Vest_M79GrenadeCarrier/Vest_M79GrenadeCarrier.et",
			"{9A21918AC35AC182}Prefabs/Characters/Vests/Vest_ALICE/Vest_ALICE_buttpack.et",
			"{9B6B61BB3FE3DFB0}Prefabs/Items/Equipment/Radios/Radio_ANPRC77.et",
			"{9BBDEE253A16CC66}Prefabs/Weapons/Grenades/M18/Smoke_M18_Yellow.et",
			"{9C05543A503DB80E}Prefabs/Weapons/Magazines/Magazine_9x19_M9_15rnd_Ball.et",
			"{9C5C20FB0E01E64F}Prefabs/Weapons/Launchers/M72/Launcher_M72A3.et",
			"{9DB69176CEF0EE97}Prefabs/Weapons/Grenades/Smoke_ANM8HC.et",
			"{9F546CCA2582D16F}Prefabs/Characters/Uniforms/Jacket_M88.et",
			"{A7AF84C6C58BA3E8}Prefabs/Weapons/MachineGuns/RPK74/MG_RPK74.et",
			"{A7E6D7ECD5F684D7}Prefabs/Characters/HeadGear/Helmet_SSh68_01/Helmet_SSh68_01.et",
			"{A81F501D3EF6F38E}Prefabs/Items/Medicine/FieldDressing_01/FieldDressing_US_01.et",
			"{A89BC9D55FFB4CD8}Prefabs/Weapons/MachineGuns/PKM/MG_PKM.et",
			"{A9A385FE1F7BF4BD}Prefabs/Weapons/Magazines/Magazine_556x45_STANAG_30rnd_M856_Tracer.et",
			"{AA074754692261B1}Prefabs/Characters/Core/VestArmored_Base.et",
			"{AB84E597273FAECA}Prefabs/Characters/Vests/Vest_Lifchik/Vest_Lifchik_GrenadeBelt.et",
			"{ACB6C2B49C1230F9}Prefabs/Characters/Vests/Vest_ALICE/Vest_ALICE_belt_dummy.et",
			"{ADE19B33DCBB9005}Prefabs/Characters/Vests/Vest_6B2/Vest_6B2.et",
			"{AE578EEA4244D41F}Prefabs/Items/Equipment/Kits/MedicalKit_01/MedicalKit_01_US.et",
			"{B1482FB64E3D2D45}Prefabs/Weapons/Rifles/M16/Rifle_M16A2_4x20.et",
			"{B699C9B282352EDE}Prefabs/Characters/Vests/Vest_SovietHarness/Vest_SovietHarness_Suspenders.et",
			"{B6EEF03975F21E4E}Prefabs/Items/Equipment/Accessories/Pouch_Soviet_45rnd_RPK74/Pouch_Soviet_45rnd_RPK74.et",
			"{B95BD66EA8F863C8}Prefabs/Characters/Vests/Vest_ALICE/Vest_ALICE_suspenders_2.et",
			"{BD496EE1B40DC510}Prefabs/Weapons/Attachments/Optics/Optic_4x20/Optic_4x20.et",
			"{BFEA719491610A45}Prefabs/Weapons/Rifles/AKS74U/Rifle_AKS74U.et",
			"{C0F7DD85A86B2900}Prefabs/Weapons/Handguns/PM/Handgun_PM.et",
			"{C3F1FA1E2EC2B345}Prefabs/Items/Medicine/FieldDressing_01/FieldDressing_USSR_01.et",
			"{C5A8B378E38AA4FF}Prefabs/Characters/Vests/Vest_SovietHarness/Vest_SovietHarness_GrenadePouch.et",
			"{C7861F11D5334C0E}Prefabs/Characters/Uniforms/Jacket_US_BDU.et",
			"{C7923961D7235D70}Prefabs/Characters/Footwear/CombatBoots_Soviet_01.et",
			"{C8516078375CBE45}Prefabs/Characters/Vests/Vest_Lifchik/Vest_Lifchik_GL.et",
			"{D11383C5E70AAA89}Prefabs/Characters/Vests/Vest_SovietHarness/Vest_SovietHarness_MedicalPouch.et",
			"{D182DCDD72BF7E34}Prefabs/Weapons/MachineGuns/M60/MG_M60.et",
			"{D262D2102B9931C6}Prefabs/Characters/Uniforms/Jacket_M88_rolledup.et",
			"{D2B48DEBEF38D7D7}Prefabs/Weapons/MachineGuns/M249/MG_M249.et",
			"{D41D22DD1B8E921E}Prefabs/Weapons/Grenades/M18/Smoke_M18_Green.et",
			"{D70216B1B2889129}Prefabs/Items/Medicine/Tourniquet_01/Tourniquet_US_01.et",
			"{D78C667F59829717}Prefabs/Weapons/Magazines/Magazine_545x39_RPK_45rnd_4Ball_1Tracer.et",
			"{D8F2CA92583B23D3}Prefabs/Weapons/Magazines/Magazine_556x45_STANAG_30rnd_M855_M856_Last_5Tracer.et",
			"{DAAFD15478BDE1C3}Prefabs/Characters/Footwear/CombatBoots_US_01.et",
			"{DCF980831E880F6A}Prefabs/Characters/Uniforms/Pants_M88.et",
			"{E1A5D4B878AA8980}Prefabs/Items/Equipment/Radios/Radio_R148.et",
			"{E3154E0EAC48D67B}Prefabs/Items/Equipment/Accessories/Pouch_ALICE_30rnd_STANAG/Pouch_ALICE_30rnd_STANAG.et",
			"{E5912E45754CD421}Prefabs/Weapons/Magazines/Magazine_545x39_AK_30rnd_Tracer.et",
			"{E5E9C5897CF47F44}Prefabs/Weapons/Magazines/Box_762x54_PK_100rnd_4Ball_1Tracer.et",
			"{E5E9DBBF3BFB88C6}Prefabs/Weapons/Attachments/Optics/Optic_PGO7/Optic_PGO7V3.et",
			"{E685A8D337D36204}Prefabs/Characters/HeadGear/Helmet_PASGT_01/Helmet_PASGT_01_cover_w_goggles.et",
			"{E8A55396050E1762}Prefabs/Weapons/Launchers/RPG7/Launcher_RPG7_PGO7.et",
			"{E8F00BF730225B00}Prefabs/Weapons/Grenades/Grenade_M67.et",
			"{EC9BDA3D9DDD8795}Prefabs/Weapons/Flares/FlareStarParachute_M127A1_white.et",
			"{EE7FBAD35CA9D0F2}Prefabs/Items/Equipment/Accessories/Scabbard_Bayonet_M9/Scabbard_Bayonet_M9.et",
			"{F03EAAE51C256CFA}Prefabs/Items/Equipment/Accessories/Pouch_Soviet_100rnd_PKM/Pouch_Soviet_100rnd_PKM.et",
			"{F759F0488730620F}Prefabs/Items/Equipment/Accessories/Scabbard_Bayonet_6Kh4/Scabbard_Bayonet_6Kh4.et",
			"{F849217AB3BA88BE}Prefabs/Items/Equipment/Maps/Map_Paper_01/PaperMap_01_folded_USSR.et",
			"{F928D90B6E4B9166}Prefabs/Characters/Vests/Vest_SovietHarness/Vest_SovietHarness_belt_dummy.et",
			"{FA5C25BF66A53DCF}Prefabs/Weapons/Rifles/AK74/Rifle_AK74.et",
			"{FB1A7F5BC7D935E2}Prefabs/Weapons/Attachments/Handguards/Handguard_M16A2/Handguard_M16.et",
			"{FC79F6BDEB7F1BC2}Prefabs/Weapons/Flares/Flare_RSP30_green.et",
			"{FE5C49069C2499D9}Prefabs/Characters/HeadGear/Helmet_PASGT_01/Helmet_PASGT_01_cover.et"
		};
		s_ApprovedPrefabs = values;
		return s_ApprovedPrefabs;
	}
	// Empty script-component model children observed on the native RHS M4.
	protected static ref array<ResourceName> s_RHSWeaponModels;
	protected static array<ResourceName> RHSWeaponModels()
	{
		if (s_RHSWeaponModels) return s_RHSWeaponModels;
		array<ResourceName> values = {
			"{3515BB3BE4AE2414}Prefabs/Weapons/Attachments/Grips/PistolGrip_AR15_A2/RHS_M4A1_Grip.et",
			"{CDD0D3355AD051DA}Prefabs/Weapons/Rifles/M4A1/base_parts/RHS_M4_BufferTube.et",
			"{BC68AE520F42BF35}Prefabs/Weapons/Attachments/Stocks/Stock_AR15_SOPMOD/RHS_M4_SOPMOD_Black.et",
			"{1BCF0AD736FC3A1B}Prefabs/Weapons/Attachments/Handguards/Handguard_AR15_DD/FDE/RHS_M4A1_DD12_5_Upper_TAN.et",
			"{3F9C076E34E47B72}Prefabs/Weapons/Attachments/Handguards/Handguard_AR15_DD/FDE/RHS_M4A1_DD12_5_Lower_TAN.et",
			"{B4ED47D7BB5448CA}Prefabs/Weapons/Rifles/M4A1/base_parts/RHS_M4_BARREL_14_5_HB.et",
			"{28BC392A46DD235C}Prefabs/Weapons/Rifles/M4A1/base_parts/RHS_M4A1_UpperReceiver.et",
			"{79A64B5F2E13A878}Prefabs/Weapons/Attachments/Misc/M4ChargingHandles/USGI_A2CH.et",
			"{C845839913F97470}Prefabs/Weapons/Rifles/M4A1/base_parts/RHS_Mk12_GasBlock.et"
		};
		s_RHSWeaponModels = values;
		return s_RHSWeaponModels;
	}
	protected static ref array<string> s_ApprovedComponents;
	protected static array<string> ApprovedComponents()
	{
		if (s_ApprovedComponents) return s_ApprovedComponents;
		array<string> values = {
			// Inspected installed EXPBG/GRS/RHS components: authored configuration,
			// native storage specialization, or presentation rebuilt from native state.
			"EXPBG_UniformItemComponent",
			"GRS_ArmorVestStorageComponent",
			"GRS_EquipmentStorageComponent",
			"GRS_PouchStorageComponent",
			"GRS_HelmetStorageComponent",
			"GRS_DevicePriorityComponent",
			"CVON_RadioComponent",
			"BaseLightManagerComponent",
			"GRS_FirstPersonHideComponent",
			"PGS_DetectableSignatureComponent",
			"RHS_ThermalWorkenchHider",
			"RHS_MagazineAnimationComponent",
			"RHS_WristWatchComponent",
			// Native storage subclasses rebuild occupied slots through item callbacks;
			// Full still verifies every original slot, item and reconstructed hierarchy.
			"RHS_HelmetNodeStorageComponent",
			"RHS_ClothNodeStorageComponent",
			"RHS_EquipmentStorageComponent",
			"RHS_RadioSourceActiveComponent",
			"PGS_RadioTransmissionListenerComponent",
			"RHS_ThermalSlotManagerComponent",
			"RHS_SGC_RadialMenuComponent",
			// Stateful components are checked by EBG_OptionalModState before Full deletion.
			"RHS_HeadMountedLightDeviceComponent",
			"RHS_SGC_ANPEQ15Component",
			"RHS_2DPIPSightsComponent",
			"RHS_WeaponRplComponent",
			"GRS_IRIlluminatorComponent",
			"BaconRISAttachments_WeaponAimingEffectAttachmentComponent",
			"BaconRISAttachments_2DPIPScopeIlluminatedTextureComponent",
			"SCR_CollimatorSightsComponent",
			"SCR_CollimatorControllerComponent",
			"SCR_MeleeWeaponProperties",
			"SCR_HealSupportStationComponent",
			"SCR_ResupplyMedicalGadgetSupportStationComponent",
			// Production modset E-tool (2026-10-06): the multi-part deployable derives from
			// SCR_BaseDeployableInventoryItemComponent, so ItemPolicyReason protects it while
			// deployed. The placeable one holds only prefab data and a transient placing-gadget
			// link that exists while a player places the item, never on a casualty.
			"SCR_DeployablePlaceableItemComponent",
			"SCR_MultiPartDeployableItemComponent",
			// ACE Overheating weapon presentation and thermal/jam state: no inventory,
			// ownership or mission data; a removed casualty weapon loses nothing.
			"ACE_Overheating_BarrelComponent",
			"ACE_Overheating_HelperAttachmentComponent",
			"ACE_Overheating_SmokeEffectComponent",
			"ACE_Overheating_BarrelGlowEffectComponent",
			"AICombatPropertiesComponent",
			"ActionsManagerComponent",
			"AttachmentSlotComponent",
			"BaseItemAnimationComponent",
			"BaseLoadoutClothComponent",
			"BaseProjectileComponent",
			"BaseRadioComponent",
			"BaseSightsComponent",
			"BaseSlotComponent",
			"CaseEjectingEffectComponent",
			"ClothNodeStorageComponent",
			"ColliderHistoryComponent",
			"CollisionTriggerComponent",
			"GadgetAnimationComponent",
			"GadgetGamepadEffectsManagerComponent",
			"GrenadeMoveComponent",
			"Hierarchy",
			"InventoryItemComponent",
			"InventoryMagazineComponent",
			"MagazineAnimationComponent",
			"MagazineComponent",
			"MeshObject",
			"MissileMoveComponent",
			"MuzzleComponent",
			"MuzzleEffectComponent",
			"MuzzleInMagComponent",
			"NwkPhysicsMovementComponent",
			"ParametricMaterialInstanceComponent",
			"Persistence",
			"ProcAnimComponent",
			"RigidBody",
			"RocketTraceEffectComponent",
			"RplComponent",
			"SCR_2DOpticsComponent",
			"SCR_2DPIPSightsComponent",
			"SCR_ArmorDamageManagerComponent",
			"SCR_ArsenalComponent",
			"SCR_ArsenalInventoryStorageManagerComponent",
			"SCR_BayonetComponent",
			"SCR_BayonetEffectComponent",
			"SCR_BinocularsComponent",
			"SCR_CampaignBuildingGadgetToolComponent",
			"SCR_CompassComponent",
			"SCR_ConsumableItemComponent",
			"SCR_DeployableInventoryItemInventoryComponent",
			"SCR_EquipmentStorageComponent",
			"SCR_FactionAffiliationComponent",
			"SCR_FlareAnimationComponent",
			"SCR_FlashlightComponent",
			"SCR_HeadgearInventoryItemComponent",
			"SCR_ItemOutfitFactionComponent",
			"SCR_MapGadgetComponent",
			"SCR_MuzzleEffectComponent",
			"SCR_MuzzleInMagComponent",
			"SCR_RadioComponent",
			"SCR_ResourceComponent",
			"SCR_RestrictedDeployableSpawnPointComponent",
			"SCR_ShellSoundComponent",
			"SCR_SoundDataComponent",
			"SCR_SupportStationGadgetComponent",
			"SCR_UniversalInventoryStorageComponent",
			"SCR_WeaponAttachmentsStorageComponent",
			"SCR_WeaponBlastComponent",
			"SCR_WeaponComponent",
			"SCR_WeaponStatsManagerComponent",
			"SCR_WristwatchComponent",
			"ShellMoveComponent",
			"SightsComponent",
			"SignalsManagerComponent",
			"SlotManagerComponent",
			"SoundComponent",
			"TimerTriggerComponent",
			"UGLAnimationComponent",
			"WeaponAnimationComponent",
			"WeaponComponent",
			"WeaponGamepadEffectsManagerComponent",
			"WeaponSoundComponent"
		};
		s_ApprovedComponents = values;
		return s_ApprovedComponents;
	}

	// Exact stock visual identities; these are character model leaves, never loot roots.
	protected static ref array<ResourceName> s_ApprovedHeads;
	protected static array<ResourceName> ApprovedHeads()
	{
		if (s_ApprovedHeads) return s_ApprovedHeads;
		array<ResourceName> values = {
			"{04F4D8CBA36A534B}Prefabs/Characters/Heads/Head_Black_02.et",
			"{0F6C19B0574DCBCA}Prefabs/Characters/Heads/Head_White_03.et",
			"{143245D81F2BAB94}Prefabs/Characters/Heads/Head_White_10.et",
			"{1AFF3351504251EF}Prefabs/Characters/Heads/Head_Asian_02/Head_Asian_02_camo_USSR_02.et",
			"{21363E70FCCBD6F9}Prefabs/Characters/Heads/Head_White_09/Head_White_09_camo_USSR_01.et",
			"{24644218743CDFBD}Prefabs/Characters/Heads/Head_White_09.et",
			"{24D28E910BF9F648}Prefabs/Characters/Heads/Head_Asian_02.et",
			"{31001F48BF88F2B1}Prefabs/Characters/Heads/Head_White_08/Head_White_08_camo_USSR_02.et",
			"{32583C88222142FD}Prefabs/Characters/Heads/Head_White_04.et",
			"{32E93A6AADB4B0DD}Prefabs/Characters/Heads/Head_White_06/Head_White_06_camo_USSR_01.et",
			"{33850F6F8AC1FC61}Prefabs/Characters/Heads/Head_Black_02/Head_Black_02_camo_US_01.et",
			"{36B119F39289239E}Prefabs/Characters/Heads/Head_White_05/Head_White_05_camo_US_01.et",
			"{3966587EF4BE834D}Prefabs/Characters/Heads/Head_Asian_02/Head_Asian_02_camo_US_02.et",
			"{3A4B78A288F46331}Prefabs/Characters/Heads/Head_White_10/Head_White_10_camo_US_02.et",
			"{4678FC43630E2C5B}Prefabs/Characters/Heads/Head_White_09/Head_White_09_camo_US_02.et",
			"{4697EB99D27CDC4F}Prefabs/Characters/Heads/Head_White_04/Head_White_04_camo_US_02.et",
			"{473E20387338226D}Prefabs/Characters/Heads/Head_White_04/Head_White_04_camo_US_01.et",
			"{4D620A90400F59D7}Prefabs/Characters/Heads/Head_White_09/Head_White_09_camo_USSR_02.et",
			"{51636CA07EC51127}Prefabs/Characters/Heads/Head_White_03/Head_White_03_camo_US_01.et",
			"{5D542BA8034C7D9F}Prefabs/Characters/Heads/Head_White_08/Head_White_08_camo_USSR_01.et",
			"{5EBD0E8A11703FF3}Prefabs/Characters/Heads/Head_White_06/Head_White_06_camo_USSR_02.et",
			"{76AB07B1EC86DEC1}Prefabs/Characters/Heads/Head_Asian_02/Head_Asian_02_camo_USSR_01.et",
			"{7E7987E31FB379BF}Prefabs/Characters/Heads/Head_White_10/Head_White_10_camo_US_01.et",
			"{889ACA9FABA0471F}Prefabs/Characters/Heads/Head_White_08/Head_White_08_camo_US_01.et",
			"{91A7ADDAB9030F9E}Prefabs/Characters/Heads/Head_White_03/Head_White_03_camo_USSR_02.et",
			"{9444E2D4C76B6423}Prefabs/Characters/Heads/Head_White_08/Head_White_08_camo_US_02.et",
			"{94ABF50E76199437}Prefabs/Characters/Heads/Head_White_05/Head_White_05_camo_US_02.et",
			"{9F57AC90DD744AD6}Prefabs/Characters/Heads/Head_Black_02/Head_Black_02_camo_US_02.et",
			"{A01F375D335C7A2C}Prefabs/Characters/Heads/Head_White_06/Head_White_06_camo_US_02.et",
			"{A42053AFB05A218B}Prefabs/Characters/Heads/Head_White_06/Head_White_06_camo_US_01.et",
			"{A679253BEE7F693F}Prefabs/Characters/Heads/Head_White_04/Head_White_04_camo_USSR_02.et",
			"{B0575E76CB6D8165}Prefabs/Characters/Heads/Head_White_10/Head_White_10_camo_USSR_01.et",
			"{B64F0403AD3C4D77}Prefabs/Characters/Heads/Head_White_05/Head_White_05_camo_USSR_01.et",
			"{BB90449192005AB8}Prefabs/Characters/Heads/Head_White_06.et",
			"{C1F80EE178D948D6}Prefabs/Characters/Heads/Head_White_08.et",
			"{CA2D11DB52BBE611}Prefabs/Characters/Heads/Head_White_04/Head_White_04_camo_USSR_01.et",
			"{D7C470712EC4D596}Prefabs/Characters/Heads/Head_White_05.et",
			"{DA1B30E311F8C259}Prefabs/Characters/Heads/Head_White_05/Head_White_05_camo_USSR_02.et",
			"{DC036A9677A90E4B}Prefabs/Characters/Heads/Head_White_10/Head_White_10_camo_USSR_02.et",
			"{EAF055495BA85CA1}Prefabs/Characters/Heads/Head_White_02.et",
			"{EDC5B8024684A4F8}Prefabs/Characters/Heads/Head_White_02/Head_White_02_camo_USSR_02.et",
			"{F915F3544A1146EC}Prefabs/Characters/Heads/Head_White_09/Head_White_09_camo_US_01.et",
			"{FDC271A8FC924801}Prefabs/Characters/Heads/Head_White_03/Head_White_03_camo_US_02.et",
			"{FDF3993A05C780B0}Prefabs/Characters/Heads/Head_White_03/Head_White_03_camo_USSR_01.et",
			"{FEAFB8C6203A0B82}Prefabs/Characters/Heads/Head_White_02/Head_White_02_camo_USSR_01.et",
			"{FF94E9F1DAB8496C}Prefabs/Characters/Heads/Head_Asian_02/Head_Asian_02_camo_US_01.et"
		};
		s_ApprovedHeads = values;
		return s_ApprovedHeads;
	}
	// The character's own identity head, resolved through the native identity component,
	// never by GUID. Character mods (ToH ReCharacters, Change Your Face) inherit the stock
	// CharacterHead_Base and only swap mesh/materials: the head is regenerated from the
	// character's visual identity, carries no inventory and is never loot.
	protected static bool IsIdentityHead(IEntity entity)
	{
		if (!entity || entity.GetChildren() || entity.FindComponent(InventoryItemComponent)) return false;
		SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(entity.GetParent());
		if (!character) return false;
		CharacterIdentityComponent identity = CharacterIdentityComponent.Cast(character.FindComponent(CharacterIdentityComponent));
		return identity && identity.GetHeadEntity() == entity;
	}
	protected static bool IsHeadLeaf(IEntity entity)
	{
		return entity && (ApprovedHeads().Contains(SCR_ResourceNameUtils.GetPrefabName(entity)) || IsIdentityHead(entity));
	}
	// Saved rows have no live identity. Only a stock-list or identity-head capture yields
	// this UUID-less stock head shape in the head folder (FindFullLeaf still rebinds it by
	// exact prefab, shape and parent).
	protected static bool IsHeadEntry(EBG_CleanupFullEntry entry)
	{
		if (!entry) return false;
		if (ApprovedHeads().Contains(entry.Prefab)) return true;
		return entry.NativeId.IsNull() && entry.ShapeSignature == "GameEntity|ParametricMaterialInstanceComponent" && entry.Prefab.Contains("Prefabs/Characters/Heads/");
	}
	// ACE Overheating attaches this generated helper to each supported weapon through its
	// own AttachmentSlotComponent. Its prefab is a bare GenericEntity (Hierarchy only): no
	// inventory, actions, replication or script state, so the weapon's slot recreates it
	// like an authored model part. Callers still require the parent weapon's policy.
	protected static bool IsWeaponHelperLeaf(IEntity entity)
	{
		if (!entity || entity.GetChildren() || entity.Type().ToString() != "GenericEntity" || SCR_ResourceNameUtils.GetPrefabName(entity) != "{D4B8C629F3D4A322}Prefabs/Weapons/Attachments/Muzzle/ACE_Overheating_HelperAttachment.et") return false;
		array<Managed> components = {};
		entity.FindComponents(GenericComponent, components);
		foreach (Managed component : components)
			if (component.Type().ToString() != "Hierarchy") return false;
		IEntity weapon = entity.GetParent();
		if (!weapon || !weapon.FindComponent(BaseWeaponComponent)) return false;
		array<Managed> slots = {};
		weapon.FindComponents(AttachmentSlotComponent, slots);
		if (slots.Count() > 32) return false;
		foreach (Managed candidate : slots)
		{
			AttachmentSlotComponent slot = AttachmentSlotComponent.Cast(candidate);
			if (slot && slot.Type().ToString() == "ACE_Overheating_HelperAttachmentComponent" && slot.GetAttachedEntity() == entity) return true;
		}
		return false;
	}

	// These resources are never ordinary kit: only the native death callback may enroll them.
	static bool IsNativeBelongingsPrefab(IEntity entity)
	{
		return entity && IsNativeBelongingsResource(SCR_ResourceNameUtils.GetPrefabName(entity));
	}
	static bool IsNativeBelongingsResource(ResourceName prefab)
	{
		// RHS USAF uses the same native identity item and existing stock model leaves.
		return prefab == "{6676FF9B716402CC}Prefabs/Items/PersonalBelongings/PersonalBelongings_US.et" || prefab == "{E096C6BD45C87B63}Prefabs/Items/PersonalBelongings/PersonalBelongings_USSR.et" || prefab == "{85785F6CDCB922BC}Prefabs/Items/PersonalBelongings/PersonalBelongings_RHS_USAF.et";
	}
	protected static ref array<ResourceName> s_BelongingsModels;
	protected static array<ResourceName> BelongingsModels()
	{
		if (s_BelongingsModels) return s_BelongingsModels;
		array<ResourceName> values = {
			"{2155742A61D1FA0E}Prefabs/Props/PersonalBelongings/Photos/Pictures_Personal_Single_US_02.et",
			"{4D0140CADD157520}Prefabs/Props/PersonalBelongings/Photos/Pictures_Personal_Single_US_01.et",
			"{6B75B63C065F5F6C}Prefabs/Props/PersonalBelongings/Dogtags/Dogtags_US.et",
			"{A735A93A8AD4077A}Prefabs/Props/PersonalBelongings/Papers/Papers_Personal_US.et",
			"{80302DFE6DAC6E3C}Prefabs/Props/PersonalBelongings/Papers/Papers_Personal_USSR.et",
			"{8F6C71B091CBC056}Prefabs/Props/PersonalBelongings/Dogtags/Dogtags_generic.et",
			"{A39644FE92BB6D49}Prefabs/Props/PersonalBelongings/Photos/Pictures_Personal_Single_USSR_01.et",
			"{CFC2701E2E7FE267}Prefabs/Props/PersonalBelongings/Photos/Pictures_Personal_Single_USSR_02.et"
		};
		s_BelongingsModels = values;
		return s_BelongingsModels;
	}
	protected string BelongingsPolicyReason(IEntity item)
	{
		EBG_CleanupObject object = Find(item);
		if (!object || !object.Held || !object.NativeBelongings) { return "Belongings lack native death provenance"; }
		if (IsNativeBelongingsPrefab(item))
		{
			SCR_IdentityInventoryItemComponent identity = SCR_IdentityInventoryItemComponent.Cast(item.FindComponent(SCR_IdentityInventoryItemComponent));
			if (!identity || !identity.GetLinkedExtendedIdentity() || identity.GetLinkedExtendedIdentity().GetOwner() != object.Member.Entity) { return "Belongings identity does not match original casualty"; }
			if (identity.GetValuableIntelFactionID() >= 0) { return "Native belongings contain valuable intel"; }
			array<Managed> components = {};
			item.FindComponents(GenericComponent, components);
			array<string> expected = {"SCR_ArsenalRefundEffectComponent", "SCR_IdentityInventoryItemComponent", "SCR_SoundDataComponent", "ActionsManagerComponent", "RplComponent"};
			if (components.Count() != expected.Count()) { return "Belongings have unverified runtime components"; }
			foreach (Managed component : components)
				if (!expected.Contains(component.Type().ToString())) { return "Belongings have an unverified runtime component"; }
			return "";
		}
		IEntity root = item.GetParent();
		EBG_CleanupObject rootObject = Find(root);
		array<Managed> modelComponents = {};
		item.FindComponents(GenericComponent, modelComponents);
		if (!rootObject || rootObject.Member != object.Member || !IsNativeBelongingsPrefab(root) || item.GetChildren() || !modelComponents.IsEmpty()) { return "Belongings model is not an owned native leaf"; }
		return ItemPolicyReason(root);
	}
	string ItemPolicyReason(IEntity item)
	{
		if (!item || ProtectedOwnerChain(item)) { return "Missing or mission-protected item/ancestor"; }
		ResourceName prefab = SCR_ResourceNameUtils.GetPrefabName(item);
		if (IsNativeBelongingsPrefab(item) || BelongingsModels().Contains(prefab)) { return BelongingsPolicyReason(item); }
		if (ApprovedHeads().Contains(prefab))
		{
			array<Managed> headComponents = {};
			item.FindComponents(GenericComponent, headComponents);
			if (item.Type().ToString() != "GameEntity" || item.GetChildren() || !SCR_ChimeraCharacter.Cast(item.GetParent()) || headComponents.Count() != 1)
				return "Head model is not the inspected native character leaf";
			if (headComponents[0].Type().ToString() != "ParametricMaterialInstanceComponent")
				return "Head model has an unverified runtime component";
			return "";
		}
		// A character-mod identity head: same native leaf rule, every component approved.
		if (IsIdentityHead(item))
		{
			if (item.Type().ToString() != "GameEntity") return "Identity head is not a native character leaf";
			array<Managed> identityHeadComponents = {};
			item.FindComponents(GenericComponent, identityHeadComponents);
			foreach (Managed identityHeadComponent : identityHeadComponents)
				if (!ApprovedComponents().Contains(identityHeadComponent.Type().ToString())) return "Identity head has an unverified runtime component: " + identityHeadComponent.Type().ToString();
			return "";
		}
		if (IsWeaponHelperLeaf(item)) return ItemPolicyReason(item.GetParent());
		// Native unnamed leaves authored inside the inspected US/Soviet flashlights.
		// Both exact parent/empty-component shapes were verified in native sessions.
		if (prefab == "" && item.Type().ToString() == "LightEntity" && !item.GetChildren())
		{
			IEntity flashlight = item.GetParent();
			array<Managed> lightComponents = {};
			item.FindComponents(GenericComponent, lightComponents);
			if (flashlight && lightComponents.IsEmpty())
			{
				ResourceName parentPrefab = SCR_ResourceNameUtils.GetPrefabName(flashlight);
				if (parentPrefab == "{3A421547BC29F679}Prefabs/Items/Equipment/Flashlights/Flashlight_MX991/Flashlight_MX991.et" || parentPrefab == "{575EA58E67448C2A}Prefabs/Items/Equipment/Flashlights/Flashlight_Soviet_01/Flashlight_Soviet_01.et")
				{
					return ItemPolicyReason(flashlight);
				}
			}
		}
		// A content prefab can use the same inspected native component contract.
		// RHS light children are rebuilt by their parent's native light manager.
		// Only the inspected device templates and empty-component leaf shape qualify.
		if (item.Type().ToString() == "RHS_LightEntity" && !item.GetChildren())
		{
			array<ResourceName> lightPrefabs = {
				"{5CE8959505D80468}Prefabs/Items/Equipment/Nightvision/ChargePro/ChargePro_IR_lightEntity.et",
				"{B485641A6178F443}Prefabs/Items/Equipment/Nightvision/ChargePro/ChargePro_lightEntity.et",
				"{04D37D2BFD87A401}Prefabs/Weapons/Attachments/Lasers/anpeq16/ANPEQ16_Laser_IR_HP_LightEntity.et",
				"{2E88D1861AB3F2E2}Prefabs/Weapons/Attachments/Lasers/anpeq16/ANPEQ16_Iluminator_IR_LightEntity.et",
				"{90A256F4E00AFBEF}Prefabs/Weapons/Attachments/Lasers/anpeq16/ANPEQ16_Laser_IR_LightEntity.et",
				"{648E645C23EC2BF3}Prefabs/Weapons/Attachments/Lasers/anpeq16/ANPEQ16_Laser_LightEntity.et",
				"{51BD3CDB9E070694}Prefabs/Weapons/Attachments/Lasers/anpeq16/ANPEQ16_Iluminator_IR_HP_LightEntity.et"
			};
			array<Managed> rhsLightComponents = {}; item.FindComponents(GenericComponent, rhsLightComponents);
			IEntity device = item.GetParent();
			if (lightPrefabs.Contains(prefab) && rhsLightComponents.IsEmpty() && device && device.FindComponent(BaseLightManagerComponent))
				return ItemPolicyReason(device);
		}
		// Ownership, exact shape, storage binding and state checks still apply.
		if (prefab.IsEmpty() || !item.GetPrefabData()) { return "Item has no resolvable prefab"; }
		// Every deployable, including SCR_MultiPartDeployableItemComponent next to another one.
		array<Managed> deployables = {};
		item.FindComponents(SCR_BaseDeployableInventoryItemComponent, deployables);
		foreach (Managed deployableComponent : deployables)
		{
			SCR_BaseDeployableInventoryItemComponent deployable = SCR_BaseDeployableInventoryItemComponent.Cast(deployableComponent);
			if (!deployable || deployable.IsDeployed()) { return "Deployed equipment is protected"; }
		}
		// Never delete a thrown projectile or placed explosive from ground provenance.
		if (item.FindComponent(BaseProjectileComponent) && Holder(item) == item) { return "Ground projectile/explosive is protected"; }
		array<Managed> components = {};
		item.FindComponents(GenericComponent, components);
		if (components.IsEmpty())
		{
			// This inspected M4 grip is authored as a non-inventory model child.
			// Its unique prefab/parent mapping is verified again before Full restore.
			if (RHSWeaponModels().Contains(prefab) && item.Type().ToString() == "GenericEntity" && !item.GetChildren() && item.GetParent() && item.GetParent().FindComponent(BaseWeaponComponent)) return ItemPolicyReason(item.GetParent());
			return "No verifiable native components";
		}
		foreach (Managed component : components)
		{
			string type = component.Type().ToString();
			if (!ApprovedComponents().Contains(type)) { return "Unverified component: " + type; }
		}
		return "";
	}
	protected bool VanillaItemPolicy(IEntity item) { return ItemPolicyReason(item) == ""; }
	bool AuthorizeItem(IEntity item)
	{
		EBG_CleanupObject object = Find(item);
		if (!object || !object.Held || object.Corpse || !VanillaItemPolicy(item)) { return false; }
		object.Allowed = true;
		return true;
	}

	protected void Hold(EBG_CacheGroup group, EBG_CacheMember member, IEntity entity, bool corpse, bool nativeBelongings = false)
	{
		if (!entity || Find(entity) || m_ReleasedForever.Contains(entity)) return;
		PersistenceSystem persistence = PersistenceSystem.GetInstance();
		if (persistence && m_ReleasedIds.Contains(persistence.GetId(entity))) return;
		EBG_CleanupObject object = new EBG_CleanupObject();
		object.Entity = entity;
		if (persistence) object.PersistentId = persistence.GetId(entity);
		object.Group = group;
		object.Member = member;
		object.Corpse = corpse;
		object.NativeBelongings = nativeBelongings;
		object.Allowed = corpse && !IsProtected(entity);
		InsertObject(object);
		// A new owned row may be a casualty remain; re-arm the casualty scan.
		if (group) group.CleanupCasualtiesSettled = false;
		if (member) member.CleanupDrained = false;
		if (!corpse)
		{
			object.Inventory = InventoryItemComponent.Cast(entity.FindComponent(InventoryItemComponent));
			if (object.Inventory) object.Inventory.m_OnParentSlotChangedInvoker.Insert(object.OnSlotChanged);
			AuthorizeItem(entity);
		}
		MaintainNativeProtection(object);
	}

	// Called only for a new item observed inside native identity creation, after
	// native DelayedInit assigns the identity and valuable-intel state.
	protected string NativeBelongingsRejection(IEntity item, SCR_ChimeraCharacter character)
	{
		if (!Replication.IsServer() || EBG_CacheManager.Unloading || !GetGame() || GetGame().GetWorld() != m_World || !character || !IsNativeBelongingsPrefab(item)) { return "Not an authoritative native belongings birth in the owning world"; }
		if (Find(item)) { return "Already registered"; }
		if (Holder(item) != character) { return "Birth item left its original character"; }
		EBG_CleanupObject corpse = Find(character);
		CharacterControllerComponent controller = character.GetCharacterController();
		if (!corpse || !corpse.Held || !corpse.Corpse || !corpse.Member || corpse.Member.WasPlayer || !controller || controller.IsPlayerControlled()) { return "Original AI owner no longer held or became player-controlled"; }
		InventoryItemComponent inventory = InventoryItemComponent.Cast(item.FindComponent(InventoryItemComponent));
		if (!inventory || !inventory.GetParentSlot()) { return "Birth item has no native inventory slot"; }
		BaseInventoryStorageComponent storage = inventory.GetParentSlot().GetStorage();
		EBG_CleanupObject container;
		if (storage) container = Find(storage.GetOwner());
		if (!container || !container.Held || container.Member != corpse.Member || container.Group != corpse.Group) { return "Birth container is not held by the original member"; }
		if (ProtectedOwnerChain(item)) { return "Mission-protected birth item or ancestor"; }
		SCR_IdentityInventoryItemComponent identity = SCR_IdentityInventoryItemComponent.Cast(inventory);
		if (!identity || !identity.GetLinkedExtendedIdentity() || identity.GetLinkedExtendedIdentity().GetOwner() != character) { return "Native linked identity does not match the original owner"; }
		PersistenceSystem persistence = PersistenceSystem.GetInstance();
		if (m_ReleasedForever.Contains(item) || (persistence && m_ReleasedIds.Contains(persistence.GetId(item)))) { return "Permanent player-loot release forbids enrollment"; }
		// Native death callbacks can create/initialize belongings before these flags
		// propagate. Only this final condition may be deferred; all provenance above
		// must already be established and is checked again before registration.
		if (!controller.IsDead() && !controller.IsUnconscious()) { return "Native birth owner is neither dead nor unconscious"; }
		return "";
	}
	protected void DeferNativeBirth(IEntity item, SCR_ChimeraCharacter character)
	{
		foreach (EBG_PendingNativeBelongings existing : m_PendingBirths)
			if (existing.Item == item) return;
		EBG_PendingNativeBelongings birth = new EBG_PendingNativeBelongings();
		birth.Item = item; birth.Owner = character;
		birth.Inventory = InventoryItemComponent.Cast(item.FindComponent(InventoryItemComponent));
		birth.Slot = birth.Inventory.GetParentSlot();
		birth.Deadline = m_World.GetWorldTime() * 0.001 + 5;
		birth.Inventory.m_OnParentSlotChangedInvoker.Insert(birth.OnSlotChanged);
		m_PendingBirths.Insert(birth);
		QueueNativeBirthRetry();
#ifdef EBG_ACCEPTANCE_TEST
		PrintFormat("[EBG CLEANUP NATIVE BIRTH DEFER] owner=%1 item=%2 deadline=%3 exactSlot=1", character.GetID(), item.GetID(), birth.Deadline);
#endif
	}
	protected void QueueNativeBirthRetry()
	{
		if (m_BirthRetryQueued || m_PendingBirths.IsEmpty()) return;
		m_BirthRetryQueued = true;
		GetGame().GetCallqueue().CallLater(RetryPendingNativeBirths, 100, false);
	}
	protected void RetryPendingNativeBirths()
	{
		m_BirthRetryQueued = false;
		if (EBG_CacheManager.Unloading || !GetGame() || GetGame().GetWorld() != m_World) return;
		ProcessPendingNativeBirths();
		QueueNativeBirthRetry();
	}
	protected void ProcessPendingNativeBirths()
	{
		float now = m_World.GetWorldTime() * 0.001;
		for (int i = m_PendingBirths.Count() - 1; i >= 0; i--)
		{
			EBG_PendingNativeBelongings birth = m_PendingBirths[i];
			string rejection;
			if (birth.SlotChanged || !birth.Inventory || birth.Inventory.GetParentSlot() != birth.Slot) rejection = "Exact birth inventory slot changed while pending";
			else if (now > birth.Deadline) rejection = "Native owner state did not confirm within five seconds";
			else rejection = NativeBelongingsRejection(birth.Item, birth.Owner);
			if (rejection == "Native birth owner is neither dead nor unconscious") continue;
			IEntity item = birth.Item;
			SCR_ChimeraCharacter character = birth.Owner;
			birth.Clear(); m_PendingBirths.Remove(i);
			if (rejection == "")
			{
				RegisterNativeBelongings(item, character);
#ifdef EBG_ACCEPTANCE_TEST
				PrintFormat("[EBG CLEANUP NATIVE BIRTH DEFER COMPLETE] owner=%1 item=%2 registered=%3", character, item, IsHeld(item));
#endif
			}
			else PrintFormat("[EBG CLEANUP NATIVE BIRTH DEFER REJECT] owner=%1 item=%2 reason='%3'", character, item, rejection);
		}
	}
	void RegisterNativeBelongings(IEntity item, SCR_ChimeraCharacter character)
	{
		string rejection = NativeBelongingsRejection(item, character);
		if (rejection == "Native birth owner is neither dead nor unconscious")
		{
			DeferNativeBirth(item, character);
			return;
		}
		if (rejection != "")
		{
			PrintFormat("[EBG CLEANUP NATIVE BELONGINGS REJECT] owner=%1 item=%2 reason='%3'", character, item, rejection);
			return;
		}
		EBG_CleanupObject corpse = Find(character);
#ifdef EBG_ACCEPTANCE_TEST
		SCR_IdentityInventoryItemComponent identity = SCR_IdentityInventoryItemComponent.Cast(item.FindComponent(SCR_IdentityInventoryItemComponent));
#endif
		Hold(corpse.Group, corpse.Member, item, false, true);
		for (IEntity child = item.GetChildren(); child; child = child.GetSibling())
			if (BelongingsModels().Contains(SCR_ResourceNameUtils.GetPrefabName(child))) Hold(corpse.Group, corpse.Member, child, false, true);
#ifdef EBG_ACCEPTANCE_TEST
		PrintFormat("[EBG CLEANUP NATIVE BELONGINGS] casualty=%1 item=%2 intelFaction=%3 policy='%4' dead=%5 unconscious=%6", character.GetID(), item.GetID(), identity.GetValuableIntelFactionID(), ItemPolicyReason(item), character.GetCharacterController().IsDead(), character.GetCharacterController().IsUnconscious());
#endif
	}
	void RegisterGroup(EBG_CacheGroup record)
	{
		if (!Replication.IsServer() || !record || !record.Zone || record.PersistenceIssue != "") return;
		// A toggle resumes the original ledger, never adopts the current inventory anew.
		if (m_RegisteredGroups.Contains(record)) { CheckGroupTransfers(record); return; }
		m_RegisteredGroups.Insert(record);
		foreach (EBG_CacheMember member : record.Members) RegisterInitialMember(record, member);
		RememberPersistentMaps(record);
	}
	// Initial enrollment or our fresh prefab birth; never recapture old inventory.
	protected void RegisterInitialMember(EBG_CacheGroup record, EBG_CacheMember member)
	{
		if (!member.Entity || member.WasPlayer) return;
		if (IsProtected(member.Entity))
		{
			// Preserve native death/age proof for the whole original roster only.
			// This body and its kit never enter ownership or native garbage holds.
			if (!Find(member.Entity))
			{
				EBG_CleanupObject proof = new EBG_CleanupObject();
				proof.Entity = member.Entity; proof.Group = record; proof.Member = member;
				proof.Corpse = true; proof.Held = false; proof.Allowed = false;
				InsertObject(proof);
			}
			return;
		}
		Hold(record, member, member.Entity, true);
		HoldChildren(record, member, member.Entity);
		InventoryStorageManagerComponent inventory = InventoryStorageManagerComponent.Cast(member.Entity.FindComponent(InventoryStorageManagerComponent));
		if (!inventory) return;
		array<IEntity> items = {};
		inventory.GetItems(items);
		foreach (IEntity item : items)
			if (item && Holder(item) == member.Entity) Hold(record, member, item, false);
			}
	// Same row-presence rule as ExportPersistentGroup; never inspects new inventory.
	int CountPersistentMemberObjects(EBG_CacheMember member)
	{
		int count;
		foreach (EBG_CleanupObject object : m_Objects)
			if (object.Member == member && (object.Entity || object.Corpse || object.PermanentlyReleased)) count++;
		return count;
	}
	// Conservative initial-enrollment footprint for regroup preflight only.
	// Counting excluded/released entities is safe; this never adopts inventory.
	int CountNewMemberObjects(SCR_ChimeraCharacter character)
	{
		if (!character) return 0;
		array<IEntity> candidates = {character};
		if (!CollectNewMemberChildren(character, candidates)) return 2049;
		InventoryStorageManagerComponent inventory = InventoryStorageManagerComponent.Cast(character.FindComponent(InventoryStorageManagerComponent));
		if (inventory)
		{
			array<IEntity> items = {};
			inventory.GetItems(items);
			foreach (IEntity item : items)
			{
				if (!item || Holder(item) != character || candidates.Contains(item)) continue;
				candidates.Insert(item);
				if (candidates.Count() > 2048) return 2049;
			}
		}
		return candidates.Count();
	}
	protected bool CollectNewMemberChildren(IEntity parent, array<IEntity> candidates, int depth = 0)
	{
		if (depth > 32) return false;
		for (IEntity child = parent.GetChildren(); child; child = child.GetSibling())
		{
			if (!candidates.Contains(child)) candidates.Insert(child);
			if (candidates.Count() > 2048 || !CollectNewMemberChildren(child, candidates, depth + 1)) return false;
		}
		return true;
	}
	// Rebinding moves only existing provenance. Released/looted exclusions,
	// persistent descriptors, casualty age and listener ownership stay intact.
	bool CanRegroup(array<EBG_CacheGroup> records)
	{
		foreach (EBG_CacheGroup record : records)
			if (record.Full || record.FullCleanup || record.Simulation || record.PersistentScalarRollbackPending) return false;
		foreach (EBG_CleanupObject object : m_Objects)
			if (records.Contains(object.Group) && (object.FullDetached || !object.Member || !object.Group.Members.Contains(object.Member))) return false;
		return true;
	}
	void CommitRegroup(array<EBG_CacheGroup> previous, array<EBG_CacheGroup> destinations, array<EBG_CacheMember> newcomers)
	{
		foreach (EBG_CleanupObject object : m_Objects)
		{
			if (!previous.Contains(object.Group)) continue;
			foreach (EBG_CacheGroup destination : destinations)
				if (destination.Members.Contains(object.Member)) { object.Group = destination; break; }
		}
		foreach (EBG_CacheGroup record : destinations)
		{
			record.CleanupCasualtiesSettled = false;
			RearmDrained(record);
			if (record.Members.IsEmpty()) { m_RegisteredGroups.RemoveItem(record); continue; }
			if (!m_RegisteredGroups.Contains(record)) m_RegisteredGroups.Insert(record);
			record.CleanupRegistered = true;
			foreach (EBG_CacheMember member : newcomers)
				if (record.Members.Contains(member)) RegisterInitialMember(record, member);
			RememberPersistentMaps(record);
		}
	}
	// Retire provenance, not world objects. Detached/looted items remain untouched.
	void ReleaseRemovedMember(EBG_CacheGroup record, EBG_CacheMember member)
	{
		for (int pendingIndex = m_PendingBirths.Count() - 1; pendingIndex >= 0; pendingIndex--)
		{
			EBG_PendingNativeBelongings birth = m_PendingBirths[pendingIndex];
			EBG_CleanupObject owner = Find(birth.Owner);
			if (owner && owner.Group == record && owner.Member == member)
			{ birth.Clear(); m_PendingBirths.Remove(pendingIndex); }
		}
		for (int i = m_Objects.Count() - 1; i >= 0; i--)
		{
			EBG_CleanupObject object = m_Objects[i];
			if (object.Group != record || object.Member != member) continue;
			ReleaseObject(object, true);
			RemoveObject(i);
		}
	}
	void ForgetPrefabMember(EBG_CacheGroup record, EBG_CacheMember member)
	{
		for (int i = m_Objects.Count() - 1; i >= 0; i--)
		{
			EBG_CleanupObject object = m_Objects[i];
			if (object.Group != record || object.Member != member || !object.Entity) continue;
			if (object.Entity != member.Entity && !EBG_FullCacheGroup.InventoryBelongsTo(object.Entity, member.Entity)) continue;
			ReleaseObject(object, false);
			RemoveObject(i);
		}
	}
	// Called once at our prefab's birth, never on a later user inventory edit.
	void RegisterPrefabMember(EBG_CacheGroup record, EBG_CacheMember member)
	{
		if (!Replication.IsServer() || !record || !member || !member.Entity || member.WasPlayer || member.Dead) return;
		if (IsProtected(member.Entity)) return;
		RegisterInitialMember(record, member);
	}
	protected void HoldChildren(EBG_CacheGroup record, EBG_CacheMember member, IEntity parent, int depth = 0)
	{
		if (depth > 32) return;
		for (IEntity child = parent.GetChildren(); child; child = child.GetSibling())
		{
			Hold(record, member, child, false);
			HoldChildren(record, member, child, depth + 1);
		}
	}

	void ConfirmDeath(EBG_CacheGroup record, EBG_CacheMember member, float now)
	{
		if (!Replication.IsServer() || !record || !member || !member.Entity) return;
		CharacterControllerComponent controller = member.Entity.GetCharacterController();
		EBG_CleanupObject corpse = Find(member.Entity);
		if (!corpse || corpse.Group != record || !controller || !controller.IsDead() || member.WasPlayer) return;
		corpse.DeathConfirmed = true;
		corpse.DeathTime = now;
		record.CleanupClearSince = -1;
		record.CleanupNextAttempt = 0;
		record.CleanupCasualtiesSettled = false;
		member.CleanupDeathTime = now;
		member.CleanupDrained = false;
		MaintainNativeProtection(corpse);
	}

	// Capture vetoed requests so release can resume native handling. Do not touch
	// blacklists: the installed API has no getter for their previous state.
	bool VetoInsertion(IEntity entity, float lifetime)
	{
		EBG_CleanupObject object = Find(entity);
		if (!object || !IsGarbageProtected(entity)) { return false; }
		if (lifetime > 0)
		{
			object.NativeRequested = true;
			object.NativeRemaining = lifetime;
		}
		return true;
	}
	// Explicit native Insert can bypass OnInsertRequested. OnBeforeDelete ends
	// tracking even when vetoed; retain its timer for a later policy release.
	bool VetoDeletion(IEntity entity)
	{
		if (!IsGarbageProtected(entity)) return false;
		EBG_CleanupObject object = Find(entity);
		if (object && !object.NativeRequested)
		{
			object.NativeRequested = true;
			// Insert(0) selects the configured default, not an expired timer.
			object.NativeRemaining = 0.001;
			SCR_GarbageSystem garbage = SCR_GarbageSystem.GetByEntityWorld(entity);
			if (garbage) object.NativeRemaining = Math.Max(0.001, garbage.GetRemainingLifetime(entity));
		}
		return true;
	}
	protected void ReleaseObject(EBG_CleanupObject object, bool permanently)
	{
		if (!object || !object.Held) return;
		if (permanently)
		{
			object.PermanentlyReleased = true;
			RememberReleasedLineage(object);
		}
		object.Held = false;
		if (object.Inventory) object.Inventory.m_OnParentSlotChangedInvoker.Remove(object.OnSlotChanged);
		if (!object.Entity) return;
		if (permanently && !m_ReleasedForever.Contains(object.Entity)) m_ReleasedForever.Insert(object.Entity);
		if (permanently) RememberReleasedId(object.Entity);
		MaintainNativeProtection(object);
	}
	protected void RememberReleasedLineage(EBG_CleanupObject object)
	{
		PersistenceSystem system = PersistenceSystem.GetInstance();
		if (!system || !object || !object.Member) return;
		UUID id = object.PersistentId;
		if (object.Entity && !system.GetId(object.Entity).IsNull()) id = system.GetId(object.Entity);
		if (!id.IsNull())
		{
			if (!m_ReleasedIds.Contains(id)) m_ReleasedIds.Insert(id);
			return;
		}
		UUID member = object.Member.PersistentId;
		if (object.Member.Entity && !system.GetId(object.Member.Entity).IsNull()) member = system.GetId(object.Member.Entity);
		if (member.IsNull()) return; // The owning record will refuse an identity-less save.
		if (!m_ReleasedLineageMembers.Contains(member)) m_ReleasedLineageMembers.Insert(member);
		// Retain the complete original descriptor as native-save data. An unresolved
		// generated identity protects its original member; it never adopts a new item.
		EBG_MissionObjectData origin = new EBG_MissionObjectData();
		if (object.PersistentMap) origin.Map = object.PersistentMap;
		origin.Map.MemberId = member; origin.Released = true;
		JsonSaveContext context = new JsonSaveContext();
		if (origin.Write(context) && context.IsValid())
		{
			string encoded = context.SaveToString();
			if (encoded.Length() <= 32768 && !m_ReleasedOrigins.Contains(encoded)) m_ReleasedOrigins.Insert(encoded);
		}
	}
	void ReleaseGroup(EBG_CacheGroup record)
	{
		if (record && record.PersistentScalarRollbackPending) return;
		for (int pendingIndex = m_PendingBirths.Count() - 1; pendingIndex >= 0; pendingIndex--)
		{
			EBG_PendingNativeBelongings birth = m_PendingBirths[pendingIndex];
			EBG_CleanupObject owner = Find(birth.Owner);
			if (!owner || owner.Group == record) { birth.Clear(); m_PendingBirths.Remove(pendingIndex); }
		}
		m_RegisteredGroups.RemoveItem(record);
		foreach (EBG_CleanupObject object : m_Objects)
			if (object.Group == record) ReleaseObject(object, false);
		for (int i = m_Objects.Count() - 1; i >= 0; i--)
			if (m_Objects[i].Group == record) RemoveObject(i);
		for (int i = m_ReleasedForever.Count() - 1; i >= 0; i--)
			if (!m_ReleasedForever[i]) m_ReleasedForever.Remove(i);
	}
	void PlayerPossession(IEntity entity)
	{
		foreach (EBG_CleanupObject object : m_Objects)
			if (object.Held && object.Member && object.Member.Entity == entity)
			{
				object.ReleaseReason = "Original member became player-controlled";
				ReleaseObject(object, true);
			}
	}
	void CheckTransfers()
	{
		if (m_World) FlushMappingFailures(m_World.GetWorldTime() * 0.001);
		CheckGroupTransfers(null);
	}
	// Inspect a transferred container's entire original group, including contents
	// whose own slot did not change. Never defer permanent player-loot release.
	void CheckGroupTransfers(EBG_CacheGroup record)
	{
		if (!Replication.IsServer() || EBG_CacheManager.Unloading || !GetGame() || GetGame().GetWorld() != m_World) return;
		if (record) record.CleanupNextAttempt = 0;
		foreach (EBG_CleanupObject object : m_Objects)
		{
			if (record && object.Group != record) continue;
			if (!object.Held || object.FullDetached || object.Corpse || !object.Entity) continue;
			if (IsDetachedProjectile(object.Entity))
			{
				object.ReleaseReason = "Detached projectile/explosive returned to native handling";
				ReleaseObject(object, true);
				continue;
			}
			IEntity holder = Holder(object.Entity);
			// Ordinary worn items and ground drops already have the exact original
			// owner. Avoid searching every group's ledger for these common cases.
			if (holder && (holder == object.Member.Entity || holder == object.Entity)) continue;
			EBG_CleanupObject ownedContainer = Find(holder);
			if (ownedContainer && ownedContainer.Held && ownedContainer.Member == object.Member) continue;
			// Ground items retain their recorded origin. Any transfer to another
			// character/container permanently releases them, even if later returned.
			if (!holder || (holder != object.Entity && holder != object.Member.Entity))
			{
				object.ReleaseReason = string.Format("Ownership transfer: original member=%1, observed holder=%2", object.Member.Entity, holder);
				ReleaseObject(object, true);
			}
		}
		foreach (EBG_CleanupObject object : m_Objects)
			if (!record || object.Group == record) MaintainNativeProtection(object);
		// Preserve corpse death/age proof until every remaining owned item is gone.
		// Native Full absence is temporary and must never discard transfer entries.
		for (int i = m_Objects.Count() - 1; i >= 0; i--)
		{
			EBG_CleanupObject removed = m_Objects[i];
			if (record && removed.Group != record) continue;
			if (removed.Entity || removed.Corpse || removed.FullDetached || (removed.Group && (removed.Group.Full || removed.Group.FullCleanup))) continue;
			// Unresolved denial descriptors survive resaves; ReleaseGroup retires them.
			if (IsInertDeathLeafProof(removed)) continue;
			RemoveObject(i);
		}
	}

	void ReconcileZonePolicy(EBG_CacheZone zone)
	{
		if (!Replication.IsServer() || EBG_CacheManager.Unloading || !zone) return;
		foreach (EBG_CleanupObject object : m_Objects)
		{
			if (!object.Group || object.Group.Zone != zone) continue;
			object.Group.CleanupClearSince = -1;
			object.Group.CleanupNextAttempt = 0;
			object.Group.CleanupCasualtiesSettled = false;
			if (object.Member) object.Member.CleanupDrained = false;
			MaintainNativeProtection(object);
		}
	}
	protected void RememberReleasedId(IEntity entity)
	{
		PersistenceSystem persistence = PersistenceSystem.GetInstance();
		if (!persistence || !entity) return;
		UUID id = persistence.GetId(entity);
		if (!id.IsNull() && !m_ReleasedIds.Contains(id)) m_ReleasedIds.Insert(id);
	}
	protected string FullShape(IEntity entity)
	{
		array<Managed> components = {};
		entity.FindComponents(GenericComponent, components);
		array<string> types = {};
		foreach (Managed component : components) types.Insert(component.Type().ToString());
		types.Sort();
		string shape = entity.Type().ToString();
		foreach (string type : types) shape += "|" + type;
		return shape;
	}
	protected IEntity FindFullLeaf(IEntity parent, EBG_CleanupFullEntry entry)
	{
		if (!parent) { return null; }
		IEntity found;
		for (IEntity child = parent.GetChildren(); child; child = child.GetSibling())
		{
			if (child.GetChildren() || SCR_ResourceNameUtils.GetPrefabName(child) != entry.Prefab || FullShape(child) != entry.ShapeSignature) continue;
			if (found) { return null; } // Ambiguous identical siblings must not exchange provenance.
			found = child;
		}
		return found;
	}
	// Both exact installed BDU resources author Slots/LoadoutSlotInfo DummyBelt.
	// The visual belt has no inventory item/contents and is parented to the wearer.
	protected IEntity GetBDUJacket(SCR_ChimeraCharacter character)
	{
		if (!character) { return null; }
		SCR_CharacterInventoryStorageComponent storage = SCR_CharacterInventoryStorageComponent.Cast(character.FindComponent(SCR_CharacterInventoryStorageComponent));
		if (!storage) { return null; }
		IEntity jacket = storage.GetClothFromArea(LoadoutJacketArea);
		if (!jacket) { return null; }
		ResourceName prefab = SCR_ResourceNameUtils.GetPrefabName(jacket);
		if (prefab != "{C7861F11D5334C0E}Prefabs/Characters/Uniforms/Jacket_US_BDU.et" && prefab != "{3CCA7A9BB4FD3197}Prefabs/Characters/Uniforms/Jacket_US_BDU_rolledup.et") { return null; }
		return jacket;
	}
	protected IEntity ResolveDummyBelt(EBG_CleanupFullEntry entry, SCR_ChimeraCharacter character)
	{
		PersistenceSystem persistence = PersistenceSystem.GetInstance();
		if (!persistence || !character || !IsNativeDummyBelt(entry.Prefab)) { return null; }
		if (entry.ShapeSignature != "GenericEntity|ActionsManagerComponent|BaseLoadoutClothComponent|RplComponent|SCR_SoundDataComponent" || entry.ParentId != entry.MemberId || persistence.GetId(character) != entry.MemberId) { return null; }
		IEntity jacket = GetDummyBeltSource(entry.Prefab, character);
		if (!jacket || entry.SourceJacketId.IsNull() || persistence.GetId(jacket) != entry.SourceJacketId || IEntity.Cast(persistence.FindById(entry.SourceJacketId)) != jacket || SCR_ResourceNameUtils.GetPrefabName(jacket) != entry.SourceJacketPrefab || ItemPolicyReason(jacket) != "") { return null; }
		IEntity belt = FindFullLeaf(character, entry);
		if (!belt || belt.FindComponent(InventoryItemComponent) || ItemPolicyReason(belt) != "") { return null; }
		return belt;
	}
	protected bool IsNativeDummyBelt(ResourceName prefab)
	{
		return prefab == "{ACB6C2B49C1230F9}Prefabs/Characters/Vests/Vest_ALICE/Vest_ALICE_belt_dummy.et" || prefab == "{F928D90B6E4B9166}Prefabs/Characters/Vests/Vest_SovietHarness/Vest_SovietHarness_belt_dummy.et" || prefab == "{7DF2186D2BEC4A63}Prefabs/Characters/Vests/Vest_SovietHarness/Vest_SovietHarness_belt_dummy_Inflatable.et";
	}
	protected IEntity GetDummyBeltSource(ResourceName prefab, SCR_ChimeraCharacter character)
	{
		if (prefab == "{ACB6C2B49C1230F9}Prefabs/Characters/Vests/Vest_ALICE/Vest_ALICE_belt_dummy.et") { return GetBDUJacket(character); }
		if (!character) { return null; }
		SCR_CharacterInventoryStorageComponent storage = SCR_CharacterInventoryStorageComponent.Cast(character.FindComponent(SCR_CharacterInventoryStorageComponent));
		if (!storage) { return null; }
		IEntity garment;
		ResourceName expected;
		if (prefab == "{F928D90B6E4B9166}Prefabs/Characters/Vests/Vest_SovietHarness/Vest_SovietHarness_belt_dummy.et")
		{
			garment = storage.GetClothFromArea(LoadoutJacketArea);
			expected = "{9F546CCA2582D16F}Prefabs/Characters/Uniforms/Jacket_M88.et";
			if (garment && SCR_ResourceNameUtils.GetPrefabName(garment) == "{D262D2102B9931C6}Prefabs/Characters/Uniforms/Jacket_M88_rolledup.et") { return garment; }
		}
		else if (prefab == "{7DF2186D2BEC4A63}Prefabs/Characters/Vests/Vest_SovietHarness/Vest_SovietHarness_belt_dummy_Inflatable.et")
		{
			garment = storage.GetClothFromArea(LoadoutVestArea);
			expected = "{C8516078375CBE45}Prefabs/Characters/Vests/Vest_Lifchik/Vest_Lifchik_GL.et";
			if (garment && SCR_ResourceNameUtils.GetPrefabName(garment) == "{9713FE6DDCC9510D}Prefabs/Characters/Vests/Vest_Lifchik/Vest_Lifchik.et") { return garment; }
		}
		if (!garment || SCR_ResourceNameUtils.GetPrefabName(garment) != expected) { return null; }
		return garment;
	}
	// Native cloth slots regenerate authored accessories; resolve only their exact
	// saved slot on the already validated original garment and wearer.
	protected IEntity ResolveClothSlot(EBG_CleanupFullEntry entry, IEntity garment)
	{
		if (!entry.NativeClothSlot || !garment || SCR_ResourceNameUtils.GetPrefabName(garment) != entry.ParentPrefab || ItemPolicyReason(garment) != "") { return null; }
		SCR_ChimeraCharacter wearer = entry.Object.Member.Entity;
		if (!wearer || garment.GetParent() != wearer) { return null; }
		IEntity found;
		int examined;
		for (IEntity item = wearer.GetChildren(); item; item = item.GetSibling())
		{
			if (++examined > 512)
			{
				return null;
			}
			if (SCR_ResourceNameUtils.GetPrefabName(item) != entry.Prefab) continue;
			InventoryItemComponent inventory = InventoryItemComponent.Cast(item.FindComponent(InventoryItemComponent));
			if (!inventory) continue;
			InventoryStorageSlot slot = inventory.GetParentSlot();
			if (!slot || slot.GetOwner() != garment || slot.GetSourceName() != entry.InventorySlotName) continue;
			if (found || slot.Type().ToString() != entry.InventorySlotType || slot.GetSlotTemplate() != entry.Prefab || slot.GetAttachedEntity() != item || FullShape(item) != entry.ShapeSignature || ItemPolicyReason(item) != "")
			{
				return null;
			}
			BaseInventoryStorageComponent storage = slot.GetStorage();
			if (storage)
			{
				if (storage.GetOwner() != garment || !ClothNodeStorageComponent.Cast(storage) || storage.GetSlot(slot.GetID()) != slot || storage.Get(slot.GetID()) != item || storage.FindItemSlot(item) != slot)
				{
					return null;
				}
			}
			else if (slot.GetParentContainer() != garment.FindComponent(BaseLoadoutClothComponent))
			{
				return null;
			}
			found = item;
		}
		return found;
	}
	protected bool CaptureClothSlot(EBG_CleanupFullEntry entry, PersistenceSystem persistence)
	{
		if (entry.GeneratedDummyBelt) { return false; }
		InventoryItemComponent inventory = InventoryItemComponent.Cast(entry.Original.FindComponent(InventoryItemComponent));
		if (!inventory) { return false; }
		InventoryStorageSlot slot = inventory.GetParentSlot();
		if (!slot || !LoadoutSlotInfo.Cast(slot) || slot.GetSlotTemplate() != entry.Prefab || slot.GetAttachedEntity() != entry.Original) { return false; }
		IEntity garment = slot.GetOwner();
		if (!garment || !garment.FindComponent(BaseLoadoutClothComponent) || garment.GetParent() != entry.Object.Member.Entity) { return false; }
		EBG_CleanupObject owner = Find(garment);
		if (!owner || owner.Member != entry.Object.Member || owner.Group != entry.Object.Group || ItemPolicyReason(garment) != "") { return false; }
		entry.NativeClothSlot = true;
		entry.PhysicalParentId = entry.ParentId;
		entry.ParentId = persistence.GetId(garment);
		entry.ParentOriginal = garment;
		entry.ParentPrefab = SCR_ResourceNameUtils.GetPrefabName(garment);
		entry.InventorySlotName = slot.GetSourceName();
		entry.InventorySlotType = slot.Type().ToString();
		if (entry.Prefab == "{F759F0488730620F}Prefabs/Items/Equipment/Accessories/Scabbard_Bayonet_6Kh4/Scabbard_Bayonet_6Kh4.et")
		{
			// Native cloth loading regenerates the authored scabbard contents.
			// Empty/replaced/extra contents must fail while originals still exist.
			IEntity blade = entry.Original.GetChildren();
			if (!blade || blade.GetSibling() || blade.GetChildren()) { return false; }
			EBG_CleanupFullEntry child = new EBG_CleanupFullEntry();
			child.Object = entry.Object; child.Original = blade;
			child.Prefab = SCR_ResourceNameUtils.GetPrefabName(blade);
			child.ShapeSignature = FullShape(blade); child.NativeScabbardSlot = true;
			if (ResolveScabbardSlot(child, entry.Original) != blade) { return false; }
		}
		return ResolveClothSlot(entry, garment) == entry.Original;
	}
	// Native named entity slots cover authored visual children such as RHS thermal
	// bodies. Their exact owner, source name, template and shape must all match.
	protected IEntity ResolveNamedSlot(EBG_CleanupFullEntry entry, IEntity owner)
	{
		if (!entry.NamedEntitySlot || !owner || SCR_ResourceNameUtils.GetPrefabName(owner) != entry.ParentPrefab) return null;
		array<EntitySlotInfo> slots = {};
		EntitySlotInfo.GetSlotInfos(owner, slots);
		if (slots.Count() > 512) return null;
		IEntity found;
		foreach (EntitySlotInfo slot : slots)
		{
			if (!slot || slot.GetSourceName() != entry.InventorySlotName || slot.Type().ToString() != entry.InventorySlotType) continue;
			IEntity item = slot.GetAttachedEntity();
			if (found || !item || slot.GetOwner() != owner || slot.GetSlotTemplate() != entry.Prefab || EntitySlotInfo.GetSlotInfo(item) != slot || SCR_ResourceNameUtils.GetPrefabName(item) != entry.Prefab || FullShape(item) != entry.ShapeSignature) return null;
			found = item;
		}
		return found;
	}
	protected bool CaptureNamedSlot(EBG_CleanupFullEntry entry, PersistenceSystem persistence)
	{
		EntitySlotInfo slot = EntitySlotInfo.GetSlotInfo(entry.Original);
		if (!slot || slot.GetAttachedEntity() != entry.Original || slot.GetSlotTemplate() != entry.Prefab || slot.GetSourceName().IsEmpty()) return false;
		IEntity owner = slot.GetOwner();
		if (!owner || owner != entry.Original.GetParent() || !Find(owner)) return false;
		entry.NamedEntitySlot = true; entry.ParentOriginal = owner;
		entry.ParentId = persistence.GetId(owner); entry.ParentPrefab = SCR_ResourceNameUtils.GetPrefabName(owner);
		entry.InventorySlotName = slot.GetSourceName(); entry.InventorySlotType = slot.Type().ToString();
		return ResolveNamedSlot(entry, owner) == entry.Original;
	}
	// Only the stock cloth-generated 6Kh4 scabbard regenerates this exact child.
	// Ordinary inventory still requires its saved UUID; never substitute by prefab.
	protected IEntity ResolveScabbardSlot(EBG_CleanupFullEntry entry, IEntity parent)
	{
		if (!entry.NativeScabbardSlot || !parent || entry.Prefab != "{98C79F5FAE12F9B6}Prefabs/Weapons/Attachments/Bayonets/Bayonet_6Kh4.et" || SCR_ResourceNameUtils.GetPrefabName(parent) != "{F759F0488730620F}Prefabs/Items/Equipment/Accessories/Scabbard_Bayonet_6Kh4/Scabbard_Bayonet_6Kh4.et" || ItemPolicyReason(parent) != "") { return null; }
		array<Managed> storages = {};
		parent.FindComponents(BaseInventoryStorageComponent, storages);
		if (storages.Count() != 1) { return null; }
		BaseInventoryStorageComponent storage = BaseInventoryStorageComponent.Cast(storages[0]);
		if (!storage || storage.Type().ToString() != "SCR_EquipmentStorageComponent" || storage.GetOwner() != parent || storage.GetSlotsCount() != 1) { return null; }
		InventoryStorageSlot slot = storage.GetSlot(0);
		if (!slot || slot.Type().ToString() != "SCR_EquipmentStorageSlot" || slot.GetSourceName() != "BayonetSlot" || slot.GetSlotTemplate() != entry.Prefab || slot.GetStorage() != storage || slot.GetID() != 0) { return null; }
		IEntity item = slot.GetAttachedEntity();
		if (!item || item.GetParent() != parent || item.GetChildren() || storage.Get(0) != item || storage.FindItemSlot(item) != slot || SCR_ResourceNameUtils.GetPrefabName(item) != entry.Prefab || FullShape(item) != entry.ShapeSignature || ItemPolicyReason(item) != "") { return null; }
		InventoryItemComponent inventory = InventoryItemComponent.Cast(item.FindComponent(InventoryItemComponent));
		if (!inventory || inventory.GetParentSlot() != slot || Holder(item) != entry.Object.Member.Entity) { return null; }
		return item;
	}
	protected bool CaptureScabbardSlot(EBG_CleanupFullEntry entry, PersistenceSystem persistence)
	{
		IEntity parent = entry.Original.GetParent();
		if (!parent || SCR_ResourceNameUtils.GetPrefabName(parent) != "{F759F0488730620F}Prefabs/Items/Equipment/Accessories/Scabbard_Bayonet_6Kh4/Scabbard_Bayonet_6Kh4.et") { return true; }
		InventoryItemComponent parentInventory = InventoryItemComponent.Cast(parent.FindComponent(InventoryItemComponent));
		if (!parentInventory || NativeVestAccessoryOwner(parent, parentInventory.GetParentSlot()) != entry.Object.Member.Entity) { return true; }
		EBG_CleanupObject parentObject = Find(parent);
		if (!parentObject || parentObject.Member != entry.Object.Member || parentObject.Group != entry.Object.Group || persistence.GetId(parent).IsNull()) { return false; }
		entry.NativeScabbardSlot = true;
		entry.ParentId = persistence.GetId(parent);
		return ResolveScabbardSlot(entry, parent) == entry.Original && entry.Object.Member.Entity.EBG_FindClothBlade() == entry.Original;
	}
	protected void ReportLoadedSlots(string phase, IEntity parent, IEntity expected)
	{
#ifdef EBG_ACCEPTANCE_TEST
		PrintFormat("[EBG CLEANUP LOADED] phase=%1 expected=%2 parent=%3", phase, expected, parent);
		if (!parent) return;
		PersistenceSystem persistence = PersistenceSystem.GetInstance();
		BaseWeaponComponent weapon = BaseWeaponComponent.Cast(parent.FindComponent(BaseWeaponComponent));
		array<BaseMuzzleComponent> muzzles = {};
		GetLoadedMuzzles(parent, weapon == null, muzzles);
		for (int i = 0; i < Math.Min(muzzles.Count(), 8); i++)
		{
			BaseMuzzleComponent muzzle = muzzles[i];
			if (!muzzle) continue;
			IEntity magazineOwner;
			if (muzzle.GetMagazine()) magazineOwner = muzzle.GetMagazine().GetOwner();
			PrintFormat("[EBG CLEANUP LOADED MUZZLE] index=%1 type=%2 ammo=%3 maxAmmo=%4 magazineOwner=%5 matchesOriginal=%6", i, muzzle.Type().ToString(), muzzle.GetAmmoCount(), muzzle.GetMaxAmmoCount(), magazineOwner, magazineOwner && magazineOwner == expected);
			BaseMagazineComponent magazine = muzzle.GetMagazine();
			if (magazine && magazineOwner && persistence) PrintFormat("[EBG CLEANUP LOADED MAGAZINE] nativeId=%1 prefab='%2' type=%3 used=%4 ammo=%5 maxAmmo=%6 ammoType='%7'", persistence.GetId(magazineOwner), SCR_ResourceNameUtils.GetPrefabName(magazineOwner), magazine.Type().ToString(), magazine.IsUsed(), magazine.GetAmmoCount(), magazine.GetMaxAmmoCount(), magazine.GetAmmoType());
			RocketEjectorMuzzleComponent rocket = RocketEjectorMuzzleComponent.Cast(muzzle);
			if (rocket)
				for (int barrel = 0; barrel < Math.Min(rocket.GetBarrelsCount(), 8); barrel++)
					PrintFormat("[EBG CLEANUP LOADED BARREL] index=%1 barrel=%2 projectile=%3 matchesOriginal=%4", i, barrel, rocket.GetBarrelProjectile(barrel), expected && rocket.GetBarrelProjectile(barrel) == expected);
		}
		int shown;
		for (IEntity child = parent.GetChildren(); child && shown < 8; child = child.GetSibling())
		{
			UUID id;
			if (persistence) id = persistence.GetId(child);
			PrintFormat("[EBG CLEANUP LOADED CHILD] prefab='%1' nativeId=%2 shape='%3'", SCR_ResourceNameUtils.GetPrefabName(child), id, FullShape(child));
			shown++;
		}
	#endif
	}
	// A native underbarrel attachment owns its muzzle directly, without a weapon
	// component. Never search descendants or another weapon for a replacement.
	protected void GetLoadedMuzzles(IEntity parent, bool direct, array<BaseMuzzleComponent> muzzles)
	{
		muzzles.Clear();
		if (!parent) return;
		if (!direct)
		{
			BaseWeaponComponent weapon = BaseWeaponComponent.Cast(parent.FindComponent(BaseWeaponComponent));
			if (weapon) weapon.GetMuzzlesList(muzzles);
			return;
		}
		array<Managed> components = {};
		parent.FindComponents(BaseMuzzleComponent, components);
		foreach (Managed component : components)
		{
			BaseMuzzleComponent muzzle = BaseMuzzleComponent.Cast(component);
			if (muzzle && muzzle.GetOwner() == parent) muzzles.Insert(muzzle);
		}
	}
	protected bool CaptureLoadedSlot(EBG_CleanupFullEntry entry, IEntity parent)
	{
		entry.DirectParentMuzzles = !parent.FindComponent(BaseWeaponComponent);
		array<BaseMuzzleComponent> muzzles = {};
		GetLoadedMuzzles(parent, entry.DirectParentMuzzles, muzzles);
		if (muzzles.IsEmpty() || muzzles.Count() > 8) { return false; }
		int matches;
		for (int i = 0; i < muzzles.Count(); i++)
		{
			BaseMuzzleComponent muzzle = muzzles[i];
			if (!muzzle) { return false; }
			RocketEjectorMuzzleComponent rocket = RocketEjectorMuzzleComponent.Cast(muzzle);
			int kind = 0;
			int foundBarrel = -1;
			if (rocket)
			{
				if (rocket.GetBarrelsCount() > 8) { return false; }
				for (int barrel = 0; barrel < rocket.GetBarrelsCount(); barrel++)
					if (rocket.GetBarrelProjectile(barrel) == entry.Original) { kind = 2; foundBarrel = barrel; matches++; }
			}
			else if (muzzle.GetMagazine() && muzzle.GetMagazine().GetOwner() == entry.Original) { kind = 1; matches++; }
			if (kind == 0) continue;
			entry.LoadedSlotKind = kind;
			entry.MuzzleIndex = i; entry.BarrelIndex = foundBarrel;
			entry.LoadedAmmo = muzzle.GetAmmoCount(); entry.LoadedMaxAmmo = muzzle.GetMaxAmmoCount();
			entry.MuzzleType = muzzle.Type().ToString();
			entry.ParentPrefab = SCR_ResourceNameUtils.GetPrefabName(parent);
			if (entry.LoadedMagazine)
			{
				MagazineComponent magazine = MagazineComponent.Cast(entry.Original.FindComponent(MagazineComponent));
				if (kind != 1 || !magazine || muzzle.GetMagazine() != magazine || !magazine.IsUsed() || magazine.GetOwner() != entry.Original) { return false; }
				entry.MagazineType = magazine.Type().ToString();
				entry.MagazineAmmo = magazine.GetAmmoCount(); entry.MagazineMaxAmmo = magazine.GetMaxAmmoCount();
				entry.MagazineAmmoType = magazine.GetAmmoType();
#ifdef EBG_ACCEPTANCE_TEST
				PrintFormat("[EBG CLEANUP MAGAZINE CAPTURE] oldId=%1 weaponId=%2 muzzle=%3 magazineType=%4 ammo=%5 maxAmmo=%6 ammoType='%7' used=1", entry.NativeId, entry.ParentId, i, entry.MagazineType, entry.MagazineAmmo, entry.MagazineMaxAmmo, entry.MagazineAmmoType);
#endif
			}
		}
#ifdef EBG_ACCEPTANCE_TEST
		if (matches == 1) PrintFormat("[EBG CLEANUP LOADED SLOT CAPTURE] original=%1 parent=%2 directParentMuzzles=%3 muzzle=%4 kind=%5", entry.NativeId, entry.ParentId, entry.DirectParentMuzzles, entry.MuzzleIndex, entry.LoadedSlotKind);
#endif
		return matches == 1;
	}
	protected IEntity ResolveLoadedSlot(EBG_CleanupFullEntry entry, IEntity parent)
	{
		if (!parent || SCR_ResourceNameUtils.GetPrefabName(parent) != entry.ParentPrefab || ItemPolicyReason(parent) != "") { return null; }
		array<BaseMuzzleComponent> muzzles = {};
		GetLoadedMuzzles(parent, entry.DirectParentMuzzles, muzzles);
		if (muzzles.Count() > 8) { return null; }
		if (entry.MuzzleIndex < 0 || entry.MuzzleIndex >= muzzles.Count()) { return null; }
		BaseMuzzleComponent muzzle = muzzles[entry.MuzzleIndex];
		if (!muzzle || muzzle.Type().ToString() != entry.MuzzleType || muzzle.GetAmmoCount() != entry.LoadedAmmo || muzzle.GetMaxAmmoCount() != entry.LoadedMaxAmmo) { return null; }
		if (entry.LoadedSlotKind == 1 && muzzle.GetMagazine())
		{
			BaseMagazineComponent magazine = muzzle.GetMagazine();
			IEntity owner = magazine.GetOwner();
			if (entry.LoadedMagazine && (!owner || owner.GetParent() != parent || !owner.FindComponent(InventoryMagazineComponent) || owner.FindComponent(MagazineComponent) != magazine || !magazine.IsUsed() || magazine.Type().ToString() != entry.MagazineType || magazine.GetAmmoCount() != entry.MagazineAmmo || magazine.GetMaxAmmoCount() != entry.MagazineMaxAmmo || magazine.GetAmmoType() != entry.MagazineAmmoType)) { return null; }
			return owner;
		}
		RocketEjectorMuzzleComponent rocket = RocketEjectorMuzzleComponent.Cast(muzzle);
		if (entry.LoadedSlotKind == 2 && rocket && entry.BarrelIndex >= 0 && entry.BarrelIndex < rocket.GetBarrelsCount()) { return rocket.GetBarrelProjectile(entry.BarrelIndex); }
		return null;
	}
	// Native default attachments can be regenerated without retaining their UUID.
	// Capture the exact reciprocal weapon-storage relationship, never a prefab match.
	protected bool CaptureWeaponInventorySlot(EBG_CleanupFullEntry entry, IEntity parent)
	{
		if (!parent || !parent.FindComponent(BaseWeaponComponent) || entry.ParentId.IsNull() || ItemPolicyReason(entry.Original) != "" || ItemPolicyReason(parent) != "") { return false; }
		InventoryItemComponent inventory = InventoryItemComponent.Cast(entry.Original.FindComponent(InventoryItemComponent));
		if (!inventory || !inventory.GetParentSlot()) { return false; }
		InventoryStorageSlot slot = inventory.GetParentSlot();
		BaseInventoryStorageComponent storage = slot.GetStorage();
		if (!storage || !WeaponAttachmentsStorageComponent.Cast(storage) || storage.GetOwner() != parent || slot.GetAttachedEntity() != entry.Original || storage.FindItemSlot(entry.Original) != slot || storage.GetSlot(slot.GetID()) != slot || storage.Get(slot.GetID()) != entry.Original) { return false; }
		int count = storage.GetSlotsCount();
		if (count < 1 || count > 32 || slot.GetID() < 0 || slot.GetID() >= count || slot.GetSourceName().IsEmpty()) { return false; }
		array<Managed> storages = {};
		parent.FindComponents(BaseInventoryStorageComponent, storages);
		int matchingStorages;
		foreach (Managed storageCandidate : storages) if (storageCandidate.Type() == storage.Type()) matchingStorages++;
		if (matchingStorages != 1) { return false; }
		int matchingSlots;
		for (int i = 0; i < count; i++)
		{
			InventoryStorageSlot candidate = storage.GetSlot(i);
			if (candidate && candidate.Type() == slot.Type() && candidate.GetSourceName() == slot.GetSourceName()) matchingSlots++;
		}
		if (matchingSlots != 1) { return false; }
		entry.WeaponInventorySlot = true;
		entry.StorageType = storage.Type().ToString(); entry.InventorySlotType = slot.Type().ToString();
		entry.InventorySlotName = slot.GetSourceName(); entry.InventorySlotId = slot.GetID(); entry.InventorySlotCount = count;
		entry.ParentPrefab = SCR_ResourceNameUtils.GetPrefabName(parent);
#ifdef EBG_ACCEPTANCE_TEST
		PrintFormat("[EBG CLEANUP WEAPON SLOT CAPTURE] oldId=%1 parentId=%2 storage=%3 slot=%4 name=%5 type=%6 count=%7 attachedOriginal=1", entry.NativeId, entry.ParentId, entry.StorageType, entry.InventorySlotId, entry.InventorySlotName, entry.InventorySlotType, count);
#endif
		return true;
	}
	protected IEntity ResolveWeaponInventorySlot(EBG_CleanupFullEntry entry, IEntity parent)
	{
		if (!entry.WeaponInventorySlot || !parent || !parent.FindComponent(BaseWeaponComponent) || SCR_ResourceNameUtils.GetPrefabName(parent) != entry.ParentPrefab || ItemPolicyReason(parent) != "") { return null; }
		array<Managed> storages = {};
		parent.FindComponents(BaseInventoryStorageComponent, storages);
		BaseInventoryStorageComponent storage;
		int matches;
		foreach (Managed storageCandidate : storages)
			if (storageCandidate.Type().ToString() == entry.StorageType) { storage = BaseInventoryStorageComponent.Cast(storageCandidate); matches++; }
		if (matches != 1 || !storage || !WeaponAttachmentsStorageComponent.Cast(storage) || storage.GetOwner() != parent || storage.GetSlotsCount() != entry.InventorySlotCount) { return null; }
		InventoryStorageSlot slot = storage.GetSlot(entry.InventorySlotId);
		if (!slot || slot.GetStorage() != storage || slot.GetID() != entry.InventorySlotId || slot.Type().ToString() != entry.InventorySlotType || slot.GetSourceName() != entry.InventorySlotName) { return null; }
		matches = 0;
		for (int i = 0; i < storage.GetSlotsCount(); i++)
		{
			InventoryStorageSlot candidate = storage.GetSlot(i);
			if (candidate && candidate.Type().ToString() == entry.InventorySlotType && candidate.GetSourceName() == entry.InventorySlotName) matches++;
		}
		if (matches != 1) { return null; }
		IEntity entity = slot.GetAttachedEntity();
		if (!entity || entity.GetParent() != parent || storage.Get(entry.InventorySlotId) != entity || storage.FindItemSlot(entity) != slot || ItemPolicyReason(entity) != "") { return null; }
		InventoryItemComponent inventory = InventoryItemComponent.Cast(entity.FindComponent(InventoryItemComponent));
		if (!inventory || inventory.GetParentSlot() != slot) { return null; }
		return entity;
	}
	// Logical inventory membership survives native gadget hand/slot presentation.
	// Default slots may regenerate their authored item with a new UUID. Only
	// the captured template and reciprocal exact slot can identify a replacement.
	protected bool CaptureStoredInventorySlot(EBG_CleanupFullEntry entry, PersistenceSystem persistence)
	{
		if (entry.NativeId.IsNull() || entry.LoadedSlotKind > 0 || entry.WeaponInventorySlot || entry.GeneratedDummyBelt || ItemPolicyReason(entry.Original) != "") { return false; }
		InventoryItemComponent inventory = InventoryItemComponent.Cast(entry.Original.FindComponent(InventoryItemComponent));
		if (!inventory || !inventory.GetParentSlot()) { return false; }
		InventoryStorageSlot slot = inventory.GetParentSlot();
		BaseInventoryStorageComponent storage = slot.GetStorage();
		if (!storage || !storage.GetOwner()) { return false; }
		IEntity parent = storage.GetOwner();
		EBG_CleanupObject parentObject = Find(parent);
		if (!parentObject || parentObject.Member != entry.Object.Member || parentObject.Group != entry.Object.Group || (parent != entry.Object.Member.Entity && ItemPolicyReason(parent) != "")) { return false; }
		UUID parentId = persistence.GetId(parent);
		int count = storage.GetSlotsCount();
		if (count < 1 || count > 512 || slot.GetID() < 0 || slot.GetID() >= count || storage.GetSlot(slot.GetID()) != slot || storage.Get(slot.GetID()) != entry.Original || storage.FindItemSlot(entry.Original) != slot) { return false; }
		array<Managed> storages = {};
		parent.FindComponents(BaseInventoryStorageComponent, storages);
		int matches;
		foreach (Managed candidate : storages) if (candidate.Type() == storage.Type()) matches++;
		if (matches != 1) { return false; }
		entry.StoredInventorySlot = true;
		entry.InventorySlotTemplate = slot.GetSlotTemplate();
		entry.PhysicalParentId = entry.ParentId;
		entry.ParentId = parentId;
		entry.ParentOriginal = parent;
		entry.ParentPrefab = SCR_ResourceNameUtils.GetPrefabName(parent);
		entry.StorageType = storage.Type().ToString();
		entry.InventorySlotType = slot.Type().ToString();
		entry.InventorySlotName = slot.GetSourceName();
		entry.InventorySlotId = slot.GetID();
		entry.InventorySlotCount = count;
#ifdef EBG_ACCEPTANCE_TEST
		if (entry.PhysicalParentId != entry.ParentId) PrintFormat("[EBG CLEANUP INVENTORY CAPTURE] item=%1 physicalParent=%2 storageParent=%3 storage=%4 slot=%5 slotType=%6 slotName='%7'", entry.NativeId, entry.PhysicalParentId, entry.ParentId, entry.StorageType, entry.InventorySlotId, entry.InventorySlotType, entry.InventorySlotName);
#endif
		return true;
	}
	protected bool MatchesStoredInventorySlot(EBG_CleanupFullEntry entry, IEntity parent, IEntity entity, PersistenceSystem persistence, bool allowDefault = false)
	{
		if (!entry.StoredInventorySlot || !entity || entry.NativeId.IsNull() || !parent || SCR_ResourceNameUtils.GetPrefabName(parent) != entry.ParentPrefab) { return false; }
		bool originalIdentity = persistence.GetId(entity) == entry.NativeId;
		if (!originalIdentity && (!allowDefault || entry.InventorySlotTemplate != entry.Prefab)) return false;
		InventoryItemComponent inventory = InventoryItemComponent.Cast(entity.FindComponent(InventoryItemComponent));
		if (!inventory || !inventory.GetParentSlot()) { return false; }
		InventoryStorageSlot slot = inventory.GetParentSlot();
		BaseInventoryStorageComponent storage = slot.GetStorage();
		if (!storage || storage.GetOwner() != parent || storage.Type().ToString() != entry.StorageType || slot.GetID() != entry.InventorySlotId || slot.Type().ToString() != entry.InventorySlotType || slot.GetSourceName() != entry.InventorySlotName) { return false; }
		int slotCount = storage.GetSlotsCount();
		if (slotCount < 1 || slotCount > 512 || entry.InventorySlotId < 0 || entry.InventorySlotId >= slotCount) return false;
		// Native saves omit empty dynamic slots. The exact original item/slot stays
		// verifiable; a recreated default still requires the full captured capacity.
		if (!originalIdentity && slotCount != entry.InventorySlotCount) return false;
		if (slot.GetSlotTemplate() != entry.InventorySlotTemplate) return false;
		if (storage.GetSlot(entry.InventorySlotId) != slot || storage.Get(entry.InventorySlotId) != entity || storage.FindItemSlot(entity) != slot) { return false; }
		array<Managed> storages = {};
		parent.FindComponents(BaseInventoryStorageComponent, storages);
		int matches;
		foreach (Managed candidate : storages) if (candidate.Type().ToString() == entry.StorageType) matches++;
		if (matches != 1) { return false; }
		// A gadget can be visually held by its character while still assigned to
		// its original inventory container. Arbitrary third-party parents fail.
		IEntity physicalParent = entity.GetParent();
		return physicalParent == parent || physicalParent == entry.Object.Member.Entity;
	}
	protected IEntity ResolveStoredInventorySlot(EBG_CleanupFullEntry entry, IEntity parent, PersistenceSystem persistence)
	{
		if (!parent || entry.InventorySlotTemplate != entry.Prefab || entry.Prefab.IsEmpty()) return null;
		array<Managed> storages = {};
		parent.FindComponents(BaseInventoryStorageComponent, storages);
		IEntity found;
		foreach (Managed candidate : storages)
		{
			if (candidate.Type().ToString() != entry.StorageType) continue;
			BaseInventoryStorageComponent storage = BaseInventoryStorageComponent.Cast(candidate);
			IEntity item = storage.Get(entry.InventorySlotId);
			if (found || !MatchesStoredInventorySlot(entry, parent, item, persistence, true)) return null;
			found = item;
		}
		return found;
	}
	protected void ReportInventoryBinding(EBG_CleanupFullEntry entry, IEntity entity, PersistenceSystem persistence)
	{
#ifdef EBG_ACCEPTANCE_TEST
		InventoryItemComponent inventory;
		if (entity) inventory = InventoryItemComponent.Cast(entity.FindComponent(InventoryItemComponent));
		InventoryStorageSlot slot;
		if (inventory) slot = inventory.GetParentSlot();
		BaseInventoryStorageComponent storage;
		if (slot) storage = slot.GetStorage();
		UUID parentId, storageId;
		if (entity && entity.GetParent()) parentId = persistence.GetId(entity.GetParent());
		if (storage && storage.GetOwner()) storageId = persistence.GetId(storage.GetOwner());
		PrintFormat("[EBG CLEANUP INVENTORY BINDING] item=%1 capturedSlot=%2 expectedParent=%3 capturedPhysical=%4 actualPhysical=%5 actualStorage=%6 expectedStorageType=%7 expectedSlot=%8", entry.NativeId, entry.StoredInventorySlot, entry.ParentId, entry.PhysicalParentId, parentId, storageId, entry.StorageType, entry.InventorySlotId);
		if (slot && storage) PrintFormat("[EBG CLEANUP INVENTORY ACTUAL] storage=%1 slots=%2 slot=%3 slotType=%4 slotName='%5' reciprocal=%6", storage.Type().ToString(), storage.GetSlotsCount(), slot.GetID(), slot.Type().ToString(), slot.GetSourceName(), storage.GetSlot(slot.GetID()) == slot && storage.Get(slot.GetID()) == entity && storage.FindItemSlot(entity) == slot);
	#endif
	}
	protected void ReportMappingSummary(EBG_CleanupMappingFailure failure)
	{
		if (failure.Suppressed == 0) return;
		PrintFormat("[EBG CLEANUP MAPPING SUMMARY] phase=%1 reason='%2' prefab='%3' suppressed=%4", failure.Phase, failure.Reason, failure.Prefab, failure.Suppressed);
		failure.Suppressed = 0;
	}
	protected void FlushMappingFailures(float now)
	{
		// The native 60-second callback can round just below the float deadline.
		if (now + 0.001 < m_NextMappingSummary) return;
		m_NextMappingSummary = now + 60;
		foreach (EBG_CleanupMappingFailure failure : m_MappingFailures) ReportMappingSummary(failure);
	}
	protected bool FirstMappingFailure(string phase, string reason, ResourceName prefab)
	{
		FlushMappingFailures(GetGame().GetWorld().GetWorldTime() * 0.001);
		foreach (EBG_CleanupMappingFailure failure : m_MappingFailures)
		{
			if (failure.Phase != phase || failure.Reason != reason || failure.Prefab != prefab) continue;
			failure.Suppressed++;
			return false;
		}
		// Bound retained diagnostics. Preserve pending counts before eviction;
		// each materially new reason/prefab still receives its first example.
		if (m_MappingFailures.Count() >= 64)
		{
			ReportMappingSummary(m_MappingFailures[0]);
			m_MappingFailures.Remove(0);
		}
		EBG_CleanupMappingFailure added = new EBG_CleanupMappingFailure();
		added.Phase = phase; added.Reason = reason; added.Prefab = prefab;
		m_MappingFailures.Insert(added);
		return true;
	}
	protected bool FullTransferFailure(string phase, string reason, IEntity entity = null, ResourceName expectedPrefab = "")
	{
		m_Reason = reason;
		ResourceName prefab = expectedPrefab;
		if (entity) prefab = SCR_ResourceNameUtils.GetPrefabName(entity);
		// Full validators are short-lived read-only snapshots. Summarize their
		// repeated errors with the world ledger, never in a fresh per-retry bucket.
		EBG_CacheCleanup reporter = this;
		if (Instance) reporter = Instance;
		m_LastFailureReported = reporter.FirstMappingFailure(phase, reason, prefab);
		if (!m_LastFailureReported) return false;
		PersistenceSystem persistence = PersistenceSystem.GetInstance();
		UUID id;
		if (entity)
		{
			if (persistence) id = persistence.GetId(entity);
		}
		string label = "EBG CLEANUP FULL FAILURE";
		if (phase == "provenance") label = "EBG CLEANUP PROVENANCE HOLD";
		PrintFormat("[%1] phase=%2 reason='%3' prefab='%4' nativeId=%5", label, phase, reason, prefab, id);
#ifdef EBG_ACCEPTANCE_TEST
		if (entity)
		{
			UUID parentId;
			if (persistence && entity.GetParent()) parentId = persistence.GetId(entity.GetParent());
			PrintFormat("[%1 DETAIL] type=%2 parentId=%3 shape='%4'", label, entity.Type().ToString(), parentId, FullShape(entity));
		}
#endif
		return false;
	}
	// Capture only existing ledger entries still inside living original members.
	// The native full backend owns serialization validation; this token never claims new items.
	// Reuse the native full-transfer mappings without calling Hold/RegisterGroup,
	// attaching inventory listeners, or requesting/withdrawing native garbage.
	protected bool CaptureValidationNode(EBG_CacheGroup record, EBG_CacheMember member, IEntity entity, out string reason, int depth = 0)
	{
		if (!entity || depth > 32 || Holder(entity) != member.Entity)
		{
			reason = string.Format("Full inventory hierarchy/holder is unavailable depth=%1 node=%2 holder=%3 expected=%4", depth, DescribeNode(entity), DescribeNode(Holder(entity)), DescribeNode(member.Entity));
#ifdef EBG_ACCEPTANCE_TEST
			IEntity traced = entity;
			InventoryItemComponent tracedItem;
			if (traced) tracedItem = InventoryItemComponent.Cast(traced.FindComponent(InventoryItemComponent));
			for (int link = 0; traced && link < 8; link++)
			{
				InventoryStorageSlot tracedSlot;
				BaseInventoryStorageComponent tracedStorage;
				if (tracedItem) tracedSlot = tracedItem.GetParentSlot();
				if (tracedSlot) tracedStorage = tracedSlot.GetStorage();
				PrintFormat("[EBG HOLDER TRACE] link=%1 owner=%2 component=%3 slot=%4 storage=%5", link, traced, tracedItem, tracedSlot, tracedStorage);
				if (tracedSlot)
				{
					PrintFormat("[EBG HOLDER TRACE SLOT] attached=%1 exactOwner=%2 id=%3 name='%4'", tracedSlot.GetAttachedEntity(), tracedSlot.GetAttachedEntity() == traced, tracedSlot.GetID(), tracedSlot.GetSourceName());
					PrintFormat("[EBG HOLDER TRACE LOADOUT] slotOwner=%1 template='%2' container=%3 itemParent=%4", tracedSlot.GetOwner(), tracedSlot.GetSlotTemplate(), tracedSlot.GetParentContainer(), traced.GetParent());
					SCR_ChimeraCharacter tracedWearer = SCR_ChimeraCharacter.Cast(traced.GetParent());
					if (tracedWearer)
					{
						SCR_CharacterInventoryStorageComponent tracedInventory = SCR_CharacterInventoryStorageComponent.Cast(tracedWearer.FindComponent(SCR_CharacterInventoryStorageComponent));
						if (tracedInventory)
						{
							IEntity tracedVest = tracedInventory.GetClothFromArea(LoadoutVestArea);
							PrintFormat("[EBG HOLDER TRACE VEST] vest=%1 protected=%2", DescribeNode(tracedVest), IsProtected(tracedVest));
						}
					}
				}
				if (!tracedStorage)
				{
					traced = traced.GetParent();
					tracedItem = null;
					if (traced) tracedItem = InventoryItemComponent.Cast(traced.FindComponent(InventoryItemComponent));
				}
				else
				{
					PrintFormat("[EBG HOLDER TRACE STORAGE] owner=%1 sameEntity=%2 sameComponent=%3 compartment=%4", tracedStorage.GetOwner(), tracedStorage.GetOwner() == traced, tracedStorage == tracedItem, tracedStorage.IsCompartment());
					traced = tracedStorage.GetOwner();
					tracedItem = tracedStorage;
				}
			}
#endif
			return false;
		}
		EBG_CleanupObject known = Find(entity);
		if (known) { return known.Member == member; }
		if (m_Objects.Count() >= 2048) { reason = "Full inventory hierarchy exceeds 2048 entities"; return false; }
		bool character = entity == member.Entity;
		if (character && IsProtected(entity)) { reason = "Full character has a mission protection opt-out"; return false; }
		if (!character)
		{
			reason = ItemPolicyReason(entity);
			if (reason != "")
			{
				reason = "Unsupported Full inventory: " + reason + " node=" + DescribeNode(entity);
#ifdef EBG_ACCEPTANCE_TEST
				PersistenceSystem persistence = PersistenceSystem.GetInstance();
				UUID nativeId, memberId;
				if (persistence) { nativeId = persistence.GetId(entity); memberId = persistence.GetId(member.Entity); }
				PrintFormat("[EBG FULL INVENTORY REFUSAL] nativeId=%1 memberId=%2 depth=%3 %4", nativeId, memberId, depth, reason);
				InventoryItemComponent inventory = InventoryItemComponent.Cast(entity.FindComponent(InventoryItemComponent));
				InventoryStorageSlot slot;
				if (inventory) slot = inventory.GetParentSlot();
				if (slot)
				{
					BaseInventoryStorageComponent storage = slot.GetStorage();
					PrintFormat("[EBG FULL INVENTORY REFUSAL SLOT] id=%1 name='%2' type=%3 attachedExact=%4 storage=%5", slot.GetID(), slot.GetSourceName(), slot.Type(), slot.GetAttachedEntity() == entity, storage);
					if (storage) PrintFormat("[EBG FULL INVENTORY REFUSAL STORAGE] type=%1 owner=%2", storage.Type(), DescribeNode(storage.GetOwner()));
				}
				else Print("[EBG FULL INVENTORY REFUSAL SLOT] no native inventory parent slot");
				ReportLoadedSlots("Full inventory refusal", entity.GetParent(), entity);
#endif
				return false;
			}
		}
		EBG_CleanupObject object = new EBG_CleanupObject();
		object.Entity = entity; object.Group = record; object.Member = member;
		object.Corpse = character; object.Held = false; object.Allowed = false;
		object.Inventory = InventoryItemComponent.Cast(entity.FindComponent(InventoryItemComponent));
		InsertObject(object);
		array<Managed> storages = {};
		entity.FindComponents(BaseInventoryStorageComponent, storages);
		foreach (Managed storageObject : storages)
		{
			BaseInventoryStorageComponent storage = BaseInventoryStorageComponent.Cast(storageObject);
			array<IEntity> items = {}; storage.GetAll(items);
			if (items.Count() > 256) { reason = "Full inventory storage exceeds bounded item count"; return false; }
			foreach (IEntity item : items)
				if (item != entity && !CaptureValidationNode(record, member, item, reason, depth + 1)) { return false; }
		}
		for (IEntity child = entity.GetChildren(); child; child = child.GetSibling())
			if (!CaptureValidationNode(record, member, child, reason, depth + 1)) { return false; }
		return true;
	}
	protected bool CaptureValidationObjects(EBG_CacheGroup record, out string reason)
	{
		foreach (EBG_CacheMember member : record.Members)
		{
			if (member.Dead) continue;
			if (member.WasPlayer || !member.Entity || !member.Entity.GetCharacterController() || member.Entity.GetCharacterController().IsDead() || member.Entity.GetCharacterController().IsPlayerControlled()) { reason = "Full inventory requires live non-player member bindings"; return false; }
			if (!CaptureValidationNode(record, member, member.Entity, reason)) { return false; }
		}
		return !m_Objects.IsEmpty();
	}
	// Shared exact native provenance capture. Caller determines ownership/life scope.
	protected bool CaptureOriginalMapping(EBG_CleanupObject object, EBG_CleanupFullEntry entry, PersistenceSystem persistence, out string reason, bool includeOptional = true)
	{
		if (CaptureOriginalMappingCore(object, entry, persistence, reason, includeOptional)) return true;
		string phase = "capture";
		if (!includeOptional) phase = "provenance";
		return FullTransferFailure(phase, reason, object.Entity);
	}
	protected bool CaptureOriginalMappingCore(EBG_CleanupObject object, EBG_CleanupFullEntry entry, PersistenceSystem persistence, out string reason, bool includeOptional)
	{
		EBG_CacheGroup record = object.Group;
		SCR_ChimeraCharacter character = object.Member.Entity;
		entry.Object = object;
		entry.Original = object.Entity;
		entry.NativeId = persistence.GetId(object.Entity);
		entry.MemberId = object.Member.PersistentId;
		if (character) entry.MemberId = persistence.GetId(character);
		if (object.Entity.GetParent()) entry.ParentId = persistence.GetId(object.Entity.GetParent());
		entry.ParentOriginal = object.Entity.GetParent();
		entry.Prefab = SCR_ResourceNameUtils.GetPrefabName(object.Entity);
		entry.ShapeSignature = FullShape(object.Entity);
		entry.WasHeld = object.Held;
		if (includeOptional)
		{
			entry.OptionalState = new EBG_OptionalModState();
			if (!entry.OptionalState.Capture(object.Entity)) { reason = entry.OptionalState.Error; return false; }
		}
		MagazineComponent capturedMagazine = MagazineComponent.Cast(object.Entity.FindComponent(MagazineComponent));
		if (capturedMagazine)
		{
			entry.CapturedMagazine = true;
			entry.MagazineType = capturedMagazine.Type().ToString();
			entry.MagazineAmmo = capturedMagazine.GetAmmoCount();
			entry.MagazineMaxAmmo = capturedMagazine.GetMaxAmmoCount();
			entry.MagazineAmmoType = capturedMagazine.GetAmmoType();
		}
		SCR_BayonetComponent bayonet = SCR_BayonetComponent.Cast(object.Entity.FindComponent(SCR_BayonetComponent));
		if (bayonet)
		{
			entry.BayonetBlood = bayonet.EBG_GetBloodStainLevel();
			entry.BayonetMaterial = new EBG_FullIdentityMaterials();
			entry.BayonetMaterial.Capture(object.Entity);
			if (!entry.BayonetMaterial.Present || !entry.BayonetMaterial.Values || entry.BayonetMaterial.Values.Count() != 10 || entry.BayonetBlood < 0 || entry.BayonetBlood > 255) { reason = "Unsupported native bayonet blood/material state"; return false; }
#ifdef EBG_ACCEPTANCE_TEST
			PrintFormat("[EBG BAYONET CAPTURE] item=%1 blood=%2 material1=%3 material2=%4 validation=%5", entry.NativeId, entry.BayonetBlood, entry.BayonetMaterial.Values[3], entry.BayonetMaterial.Values[4], m_ValidationOnly);
#endif
		}
#ifdef EBG_ACCEPTANCE_TEST
		if (IsHeadLeaf(object.Entity)) ReportHeadIdentity("capture", character, entry.Prefab);
#endif
		if (entry.MemberId.IsNull()) { reason = "Cleanup original member has no native identity"; return false; }
		if (IsNativeDummyBelt(entry.Prefab))
		{
			IEntity jacket = GetDummyBeltSource(entry.Prefab, character);
			EBG_CleanupObject jacketObject = Find(jacket);
			if (!jacketObject || jacketObject.Member != object.Member || jacketObject.Group != record || jacketObject.Held != object.Held)
			{ reason = "Cleanup native dummy belt lacks matching original jacket ownership"; return false; }
			entry.SourceJacketId = persistence.GetId(jacket);
			entry.SourceJacketPrefab = SCR_ResourceNameUtils.GetPrefabName(jacket);
			if (ResolveDummyBelt(entry, character) != object.Entity)
			{ reason = "Cleanup native dummy belt lacks unique BDU jacket provenance"; return false; }
			entry.GeneratedDummyBelt = true;
#ifdef EBG_ACCEPTANCE_TEST
			PrintFormat("[EBG CLEANUP DUMMY BELT] capture nativeId=%1 memberId=%2 sourceJacketId=%3 sourceJacketPrefab='%4' held=%5", entry.NativeId, entry.MemberId, entry.SourceJacketId, entry.SourceJacketPrefab, entry.WasHeld);
#endif
		}
		IEntity loadedParent = object.Entity.GetParent();
		// M18's authored sight models disable inventory and actions, so native
		// restore regenerates their IDs outside ordinary inventory membership.
		if (loadedParent && SCR_ResourceNameUtils.GetPrefabName(loadedParent) == "{1F9DA556822C78DA}Prefabs/Weapons/Handguns/M17/Handgun_M18.et" &&
			(entry.Prefab == "{9E24BFE18B64B864}Prefabs/Weapons/Handguns/M17/Details/rearsight.et" || entry.Prefab == "{C103D443C1D3CD8B}Prefabs/Weapons/Handguns/M17/Details/frontsight.et"))
		{
			if (!loadedParent.FindComponent(BaseWeaponComponent) || object.Entity.GetChildren() || ItemPolicyReason(object.Entity) != "" ||
				(entry.ShapeSignature != "GenericEntity|RplComponent" && entry.ShapeSignature != "GenericEntity|PGS_DetectableSignatureComponent|RplComponent") || FindFullLeaf(loadedParent, entry) != object.Entity)
			{ reason = "RHS M18 authored sight model changed"; return false; }
			entry.GeneratedWeaponSight = true; entry.ParentOriginal = loadedParent;
		}
		bool loadedRound = object.Entity.Type().ToString() == "Projectile" && object.Entity.FindComponent(BaseProjectileComponent);
		bool loadedMagazine = object.Entity.Type().ToString() == "GenericEntity" && object.Entity.FindComponent(MagazineComponent) && object.Entity.FindComponent(InventoryMagazineComponent);
		if ((loadedRound || loadedMagazine) && loadedParent && (loadedParent.FindComponent(BaseWeaponComponent) || loadedParent.FindComponent(BaseMuzzleComponent)))
		{
			entry.LoadedMagazine = loadedMagazine;
#ifdef EBG_ACCEPTANCE_TEST
			ReportLoadedSlots("capture", loadedParent, object.Entity);
#endif
			if (entry.ParentId.IsNull() || ItemPolicyReason(object.Entity) != "" || ItemPolicyReason(loadedParent) != "" || !CaptureLoadedSlot(entry, loadedParent))
			{ reason = "Cleanup loaded round/magazine lacks verified native slot provenance"; return false; }
		}
		if (entry.LoadedSlotKind == 0 && loadedParent && loadedParent.FindComponent(BaseWeaponComponent) && object.Entity.FindComponent(InventoryItemComponent))
		{
			// Failure leaves UUID-only mapping intact; no unverified replacement is admitted.
			bool capturedSlot = CaptureWeaponInventorySlot(entry, loadedParent);
#ifdef EBG_ACCEPTANCE_TEST
			PrintFormat("[EBG CLEANUP WEAPON SLOT] captured=%1 prefab='%2' nativeId=%3 parentId=%4", capturedSlot, entry.Prefab, entry.NativeId, entry.ParentId);
#endif
		}
		// RHS AK74N authors this non-inventory mount in EntitySlotInfo Dovetail.
		// Native restore regenerates its UUID; capture the existing exact slot even
		// when it starts tracked. This records provenance, never cleanup ownership.
		if (entry.Prefab == "{D87211B7CE9D89F8}Prefabs/Weapons/Attachments/Mounts/Mount_Dovetail_74N.et")
		{
			if (!loadedParent || !loadedParent.FindComponent(BaseWeaponComponent) || object.Entity.GetChildren() ||
				entry.ShapeSignature != "GameEntity|PGS_DetectableSignatureComponent|RplComponent" || !CaptureNamedSlot(entry, persistence) ||
				entry.InventorySlotName != "Dovetail" || entry.InventorySlotType != "EntitySlotInfo")
			{ reason = "RHS AK74N mount lacks exact original Dovetail slot provenance"; return false; }
		}
		bool capturedCloth = CaptureClothSlot(entry, persistence);
		if (entry.NativeClothSlot && !capturedCloth) { reason = "Original cloth slot could not be verified"; return false; }
		if (entry.NativeId.IsNull() && !entry.NativeClothSlot && !entry.WeaponInventorySlot) CaptureNamedSlot(entry, persistence);
		if (entry.NativeId.IsNull() && !entry.WeaponInventorySlot && !entry.NativeClothSlot && !entry.NamedEntitySlot)
		{
			bool verifiedLeaf = IsHeadLeaf(object.Entity) || IsWeaponHelperLeaf(object.Entity) || BelongingsModels().Contains(entry.Prefab) || object.Entity.Type().ToString() == "LightEntity" || object.Entity.Type().ToString() == "RHS_LightEntity";
			// WeaponPart_Base disables Inventory/Actions. The inspected Pegasus
			// variant adds only prefab-configured, read-only detection properties.
			// Reuse exact inspected resources, never accept arbitrary prefab prefixes.
			bool staticPart = ApprovedPrefabs().Contains(entry.Prefab) && (entry.ShapeSignature == "GenericEntity|RplComponent" || entry.ShapeSignature == "GameEntity|RplComponent" || entry.ShapeSignature == "GenericEntity|PGS_DetectableSignatureComponent|RplComponent");
			verifiedLeaf = verifiedLeaf || staticPart || entry.GeneratedDummyBelt;
			if (RHSWeaponModels().Contains(entry.Prefab) && entry.ShapeSignature == "GenericEntity") verifiedLeaf = true;
			IEntity parent = object.Entity.GetParent();
			if (!verifiedLeaf || !parent || object.Entity.GetChildren() || ItemPolicyReason(object.Entity) != "")
			{ reason = "Cleanup item without native identity is not a verified model leaf"; return false; }
			entry.ParentId = persistence.GetId(parent);
			// RHS light parents can themselves be native inventory-slot children
			// without UUIDs. Resolve the captured parent entry first on restore.
			entry.ParentOriginal = parent;
			if (FindFullLeaf(parent, entry) != object.Entity)
			{ reason = "Cleanup model leaf lacks unique captured parent mapping"; return false; }
		}
		InventoryItemComponent nativeVestItem = InventoryItemComponent.Cast(object.Entity.FindComponent(InventoryItemComponent));
		bool requiresCloth;
		if (nativeVestItem) requiresCloth = NativeVestAccessoryOwner(object.Entity, nativeVestItem.GetParentSlot()) != null;
		if (!capturedCloth && (entry.NativeClothSlot || requiresCloth))
		{
			reason = "Native accessory lacks exact original cloth slot provenance";
			return false;
		}
		if (!CaptureScabbardSlot(entry, persistence)) { reason = "Native scabbard child lacks exact original slot provenance"; return false; }
		if (!entry.NativeClothSlot && !entry.NativeScabbardSlot) CaptureStoredInventorySlot(entry, persistence);
		ReportScabbard("capture", object.Entity.GetParent(), object.Entity);
		return true;
	}
	EBG_CleanupFullTransfer CaptureFull(EBG_CacheGroup record, out string reason, bool includeValidation = true)
	{
		reason = "Cleanup transfer requires authoritative native persistence";
		PersistenceSystem persistence = PersistenceSystem.GetInstance();
		if (!Replication.IsServer() || !record || !persistence || persistence.GetState() != EPersistenceSystemState.ACTIVE)
		{ FullTransferFailure("capture", reason); return null; }
		if (!m_ValidationOnly) CheckTransfers();
		foreach (IEntity released : m_ReleasedForever) RememberReleasedId(released);
		EBG_CleanupFullTransfer token = new EBG_CleanupFullTransfer();
		token.Group = record;
		if (m_ValidationOnly) token.Validator = this;
		if (includeValidation)
		{
			EBG_CacheCleanup validator = new EBG_CacheCleanup();
			validator.m_ValidationOnly = true;
			if (!validator.CaptureValidationObjects(record, reason)) { return null; }
			token.Validation = validator.CaptureFull(record, reason, false);
			if (!token.Validation) { return null; }
#ifdef EBG_ACCEPTANCE_TEST
			PrintFormat("[EBG FULL INVENTORY] captured=%1 cleanupEnabled=%2 validationHeld=0 globalDeletionEnrollment=0", token.Validation.Entries.Count(), record.Zone && record.Zone.Cleanup);
#endif
		}
		foreach (EBG_CleanupObject object : m_Objects)
		{
			if (object.Group != record || !object.Member || object.Member.Dead || !object.Entity) continue;
			SCR_ChimeraCharacter character = object.Member.Entity;
			if (!character || Holder(object.Entity) != character) continue; // Ground/other-owner gear stays materialized.
			if (token.Entries.Count() >= 2048) { reason = "Cleanup full transfer exceeds bounded item limit"; FullTransferFailure("capture", reason, object.Entity); return null; }
			if (object.FullDetached || object.Member.WasPlayer || !character.GetCharacterController() || character.GetCharacterController().IsDead() || character.GetCharacterController().IsPlayerControlled())
			{ reason = "Cleanup living-member transfer state changed"; FullTransferFailure("capture", reason, object.Entity); return null; }
			EBG_CleanupFullEntry entry = new EBG_CleanupFullEntry();
			if (!CaptureOriginalMapping(object, entry, persistence, reason)) return null;
			if (!m_ValidationOnly && !object.Held) RememberReleasedId(object.Entity);
			token.Entries.Insert(entry);
		}
		foreach (EBG_CleanupFullEntry childEntry : token.Entries)
		{
			if (!childEntry.ParentOriginal) continue;
			for (int parentEntryIndex = 0; parentEntryIndex < token.Entries.Count(); parentEntryIndex++)
				if (token.Entries[parentEntryIndex].Original == childEntry.ParentOriginal) { childEntry.ParentEntry = parentEntryIndex; break; }
			if (childEntry.ParentId.IsNull() && childEntry.ParentEntry < 0) { reason = "Structural inventory parent is not in the captured ownership tree"; return null; }
		}
		reason = "Cleanup living ownership captured; dead remains and ground items untouched";
		return token;
	}
	// Call immediately before synchronous native Save/delete. No garbage timers resume.
	bool DetachFull(EBG_CleanupFullTransfer token)
	{
		PersistenceSystem persistence = PersistenceSystem.GetInstance();
		if (!Replication.IsServer() || !persistence || !token || token.Detached || token.Completed) { return FullTransferFailure("detach", "Cleanup transfer token or persistence unavailable"); }
		foreach (EBG_CleanupFullEntry entry : token.Entries)
		{
			EBG_CleanupObject object = entry.Object;
			if (entry.OptionalState && !entry.OptionalState.Apply(entry.Original, false)) { return FullTransferFailure("detach", entry.OptionalState.Error, entry.Original); }
			if (!m_Objects.Contains(object) || !entry.Original || object.Entity != entry.Original || object.Group != token.Group || object.FullDetached || object.Held != entry.WasHeld || object.Member.Dead || object.Member.WasPlayer) { return FullTransferFailure("detach", "Cleanup original ledger state changed", object.Entity); }
			if (!object.Member.Entity || object.Member.Entity.GetCharacterController().IsDead() || object.Member.Entity.GetCharacterController().IsPlayerControlled()) { return FullTransferFailure("detach", "Cleanup original member life/control changed", object.Entity); }
			if (persistence.GetId(object.Member.Entity) != entry.MemberId || Holder(object.Entity) != object.Member.Entity) { return FullTransferFailure("detach", "Cleanup original member identity or item holder changed", object.Entity); }
			if (!entry.NativeId.IsNull() && persistence.GetId(object.Entity) != entry.NativeId) { return FullTransferFailure("detach", "Cleanup original item identity changed", object.Entity); }
			if (entry.LoadedSlotKind > 0 && ResolveLoadedSlot(entry, IEntity.Cast(persistence.FindById(entry.ParentId))) != object.Entity) { return FullTransferFailure("detach", "Cleanup original native loaded slot changed", object.Entity); }
			if (entry.WeaponInventorySlot && ResolveWeaponInventorySlot(entry, IEntity.Cast(persistence.FindById(entry.ParentId))) != object.Entity) { return FullTransferFailure("detach", "Cleanup original weapon inventory slot changed", object.Entity); }
			if (entry.StoredInventorySlot && !MatchesStoredInventorySlot(entry, entry.ParentOriginal, object.Entity, persistence)) { return FullTransferFailure("detach", "Cleanup original logical inventory slot changed", object.Entity); }
			if (entry.GeneratedDummyBelt && ResolveDummyBelt(entry, object.Member.Entity) != object.Entity) { return FullTransferFailure("detach", "Cleanup original BDU dummy belt relationship changed", object.Entity); }
			if (entry.NativeClothSlot && ResolveClothSlot(entry, entry.ParentOriginal) != object.Entity) { return FullTransferFailure("detach", "Cleanup original cloth accessory slot changed", object.Entity); }
			if (entry.NamedEntitySlot && ResolveNamedSlot(entry, entry.ParentOriginal) != object.Entity) { return FullTransferFailure("detach", "Cleanup original named entity slot changed", object.Entity); }
			if (entry.GeneratedWeaponSight && FindFullLeaf(entry.ParentOriginal, entry) != object.Entity) { return FullTransferFailure("detach", "RHS authored sight model changed", object.Entity); }
			if (entry.NativeScabbardSlot && ResolveScabbardSlot(entry, IEntity.Cast(persistence.FindById(entry.ParentId))) != object.Entity) { return FullTransferFailure("detach", "Cleanup original scabbard slot changed", object.Entity); }
			if (entry.BayonetMaterial)
			{
				SCR_BayonetComponent bayonet = SCR_BayonetComponent.Cast(object.Entity.FindComponent(SCR_BayonetComponent));
				if (!bayonet || bayonet.EBG_GetBloodStainLevel() != entry.BayonetBlood || !entry.BayonetMaterial.Matches(object.Entity)) { return FullTransferFailure("detach", "Native bayonet state changed before removal", object.Entity); }
			}
			if (entry.NativeId.IsNull() && !entry.WeaponInventorySlot && !entry.NativeClothSlot && !entry.NamedEntitySlot && FindFullLeaf(entry.ParentOriginal, entry) != object.Entity) { return FullTransferFailure("detach", "Cleanup model leaf parent lookup or unique shape failed", object.Entity); }
		}
		if (token.Validation && !token.Validation.Validator.DetachFull(token.Validation)) { return false; }
		foreach (EBG_CleanupFullEntry entry : token.Entries)
		{
			if (!m_ValidationOnly && entry.Object.Inventory) entry.Object.Inventory.m_OnParentSlotChangedInvoker.Remove(entry.Object.OnSlotChanged);
			entry.Object.FullDetached = true;
		}
		token.Detached = true;
		return true;
	}
	void ReportHeadIdentity(string phase, SCR_ChimeraCharacter character, ResourceName expected)
	{
#ifdef EBG_ACCEPTANCE_TEST
		if (!character) { PrintFormat("[EBG CLEANUP HEAD] phase=%1 expected='%2' character missing", phase, expected); return; }
		CharacterIdentityComponent component = CharacterIdentityComponent.Cast(character.FindComponent(CharacterIdentityComponent));
		IEntity head, headParent;
		ResourceName configuredHead, body, actualPrefab;
		string actualShape;
		if (component)
		{
			head = component.GetHeadEntity();
			Identity identity = component.GetIdentity();
			if (identity && identity.GetVisualIdentity())
			{
				configuredHead = identity.GetVisualIdentity().GetHead();
				body = identity.GetVisualIdentity().GetBody();
			}
		}
		if (head)
		{
			actualPrefab = SCR_ResourceNameUtils.GetPrefabName(head);
			actualShape = FullShape(head);
			headParent = head.GetParent();
		}
		PrintFormat("[EBG CLEANUP HEAD] phase=%1 expected='%2' configuredHead='%3' body='%4' actualPrefab='%5' shape='%6' head=%7 parent=%8", phase, expected, configuredHead, body, actualPrefab, actualShape, head, headParent);
		PersistenceSystem persistence = PersistenceSystem.GetInstance();
		int candidates;
		// Search every direct child: inventory attachments can precede the native head.
		for (IEntity child = character.GetChildren(); child; child = child.GetSibling())
		{
			ResourceName prefab = SCR_ResourceNameUtils.GetPrefabName(child);
			if (!prefab.Contains("Prefabs/Characters/Heads/")) continue;
			UUID id;
			if (persistence) id = persistence.GetId(child);
			PrintFormat("[EBG CLEANUP HEAD CANDIDATE] prefab='%1' nativeId=%2 shape='%3' hasChildren=%4 equalsNativeHead=%5 matchesExpected=%6", prefab, id, FullShape(child), child.GetChildren() != null, child == head, prefab == expected);
			candidates++;
		}
		PrintFormat("[EBG CLEANUP HEAD COUNT] phase=%1 directHeadCandidates=%2 nativeHeadPresent=%3", phase, candidates, head != null);
	#endif
	}
	protected bool FullRebindFailure(string reason, EBG_CleanupFullEntry entry = null, IEntity actual = null, bool duplicate = false)
	{
		ResourceName expectedPrefab;
		if (entry) expectedPrefab = entry.Prefab;
		FullTransferFailure("rebind", reason, actual, expectedPrefab);
		if (!m_LastFailureReported || !entry) { return false; }
		PrintFormat("[EBG CLEANUP REBIND IDENTITY] expectedId=%1 prefab=%2", entry.NativeId, entry.Prefab);
#ifdef EBG_ACCEPTANCE_TEST
		PrintFormat("[EBG CLEANUP REBIND EXPECTED] prefab='%1' nativeId=%2 parentId=%3 memberId=%4 shape='%5' wasHeld=%6", entry.Prefab, entry.NativeId, entry.ParentId, entry.MemberId, entry.ShapeSignature, entry.WasHeld);
		if (entry.LoadedSlotKind > 0) PrintFormat("[EBG CLEANUP REBIND SLOT] kind=%1 muzzle=%2 barrel=%3 muzzleType=%4 ammo=%5 maxAmmo=%6 parentPrefab='%7'", entry.LoadedSlotKind, entry.MuzzleIndex, entry.BarrelIndex, entry.MuzzleType, entry.LoadedAmmo, entry.LoadedMaxAmmo, entry.ParentPrefab);
		if (entry.LoadedMagazine) PrintFormat("[EBG CLEANUP REBIND MAGAZINE] type=%1 ammo=%2 maxAmmo=%3 ammoType='%4'", entry.MagazineType, entry.MagazineAmmo, entry.MagazineMaxAmmo, entry.MagazineAmmoType);
		if (entry.GeneratedDummyBelt) PrintFormat("[EBG CLEANUP REBIND DUMMY] sourceJacketId=%1 sourceJacketPrefab='%2'", entry.SourceJacketId, entry.SourceJacketPrefab);
		PersistenceSystem persistence = PersistenceSystem.GetInstance();
		IEntity member, holder;
		UUID actualMemberId, holderId;
		if (entry.Object && entry.Object.Member) member = entry.Object.Member.Entity;
		if (IsHeadEntry(entry)) ReportHeadIdentity("rebind", SCR_ChimeraCharacter.Cast(member), entry.Prefab);
		if (actual) holder = Holder(actual);
		if (persistence && member) actualMemberId = persistence.GetId(member);
		if (persistence && holder) holderId = persistence.GetId(holder);
		PrintFormat("[EBG CLEANUP REBIND LINKS] isNull=%1 duplicate=%2 member=%3 memberId=%4 holder=%5 holderId=%6 holderMatchesMember=%7", actual == null, duplicate, member, actualMemberId, holder, holderId, holder == member);
		if (entry.Object) PrintFormat("[EBG CLEANUP REBIND LEDGER] held=%1 detached=%2 corpse=%3 registered=%4 originalStillExists=%5", entry.Object.Held, entry.Object.FullDetached, entry.Object.Corpse, m_Objects.Contains(entry.Object), entry.Original != null);
		// A missing UUID-less match otherwise hides whether the parent, prefab or
		// component shape differed. Log a bounded set of actual direct candidates.
		if (entry.NativeId.IsNull() && persistence)
		{
			IEntity parent = IEntity.Cast(persistence.FindById(entry.ParentId));
			PrintFormat("[EBG CLEANUP REBIND LEAF] resolvedParent=%1 expectedParentId=%2", parent, entry.ParentId);
			int shown;
			if (parent)
			{
				for (IEntity child = parent.GetChildren(); child && shown < 8; child = child.GetSibling())
				{
					PrintFormat("[EBG CLEANUP REBIND CANDIDATE] prefab='%1' nativeId=%2 shape='%3' hasChildren=%4", SCR_ResourceNameUtils.GetPrefabName(child), persistence.GetId(child), FullShape(child), child.GetChildren() != null);
					shown++;
				}
			}
		}

#endif
		return false;
	}
	protected void ReportScabbard(string phase, IEntity parent, IEntity expected)
	{
#ifdef EBG_ACCEPTANCE_TEST
		if (!parent || SCR_ResourceNameUtils.GetPrefabName(parent) != "{F759F0488730620F}Prefabs/Items/Equipment/Accessories/Scabbard_Bayonet_6Kh4/Scabbard_Bayonet_6Kh4.et") return;
		array<Managed> components = {};
		parent.FindComponents(BaseInventoryStorageComponent, components);
		PrintFormat("[EBG SCABBARD] phase=%1 parent=%2 expected=%3 storages=%4", phase, parent, expected, components.Count());
		foreach (Managed component : components)
		{
			BaseInventoryStorageComponent storage = BaseInventoryStorageComponent.Cast(component);
			for (int i = 0; i < Math.Min(storage.GetSlotsCount(), 8); i++)
			{
				InventoryStorageSlot slot = storage.GetSlot(i);
				if (slot) PrintFormat("[EBG SCABBARD SLOT] storage=%1 slot=%2 type=%3 name=%4 template=%5 attached=%6", storage.Type(), i, slot.Type(), slot.GetSourceName(), slot.GetSlotTemplate(), slot.GetAttachedEntity());
			}
		}
		BaseSlotComponent baseSlot = BaseSlotComponent.Cast(parent.FindComponent(BaseSlotComponent));
		if (baseSlot) PrintFormat("[EBG SCABBARD BASE] slot=%1 attached=%2", baseSlot.GetSlotInfo(), baseSlot.GetAttachedEntity());
		int shown;
		for (IEntity child = parent.GetChildren(); child && shown < 6; child = child.GetSibling())
		{
			InventoryItemComponent inventory = InventoryItemComponent.Cast(child.FindComponent(InventoryItemComponent));
			InventoryStorageSlot childSlot;
			if (inventory) childSlot = inventory.GetParentSlot();
			PrintFormat("[EBG SCABBARD CHILD] entity=%1 uuid=%2 shape=%3 inventorySlot=%4", child, PersistenceSystem.GetInstance().GetId(child), FullShape(child), childSlot);
			shown++;
		}
#endif
	}
	protected int FullEntryIndex(EBG_CleanupFullTransfer token, UUID id)
	{
		if (id.IsNull()) { return -1; }
		for (int i = 0; i < token.Entries.Count(); i++)
			if (token.Entries[i].NativeId == id) { return i; }
		return -1;
	}
	protected IEntity FullResolvedParent(EBG_CleanupFullTransfer token, EBG_CleanupFullEntry entry, PersistenceSystem persistence, array<IEntity> restored)
	{
		int index = entry.ParentEntry;
		if (index < 0) index = FullEntryIndex(token, entry.ParentId);
		if (index >= 0) { return restored[index]; }
		return IEntity.Cast(persistence.FindById(entry.ParentId));
	}
	// Read-only batch diagnostics on failure use validated parents so one UUID does not
	// conceal the other missing identities in this bounded transfer.
	protected void ReportMissingFullUUIDs(EBG_CleanupFullTransfer token, PersistenceSystem persistence, array<IEntity> restored)
	{
#ifdef EBG_ACCEPTANCE_TEST
		int missing;
		foreach (EBG_CleanupFullEntry entry : token.Entries)
		{
			if (entry.NativeId.IsNull()) continue;
			IEntity existingItem = IEntity.Cast(persistence.FindById(entry.NativeId));
			if (existingItem)
			{
				IEntity expectedParent = FullResolvedParent(token, entry, persistence, restored);
				if ((entry.StoredInventorySlot && !MatchesStoredInventorySlot(entry, expectedParent, existingItem, persistence)) || (!entry.StoredInventorySlot && existingItem.GetParent() != expectedParent)) ReportInventoryBinding(entry, existingItem, persistence);
				continue;
			}
			missing++;
			IEntity parent = FullResolvedParent(token, entry, persistence, restored);
			IEntity slotCandidate;
			if (entry.WeaponInventorySlot) slotCandidate = ResolveWeaponInventorySlot(entry, parent);
			else if (entry.LoadedSlotKind > 0) slotCandidate = ResolveLoadedSlot(entry, parent);
			else if (entry.NativeClothSlot) slotCandidate = ResolveClothSlot(entry, parent);
			else if (entry.StoredInventorySlot && parent)
			{
				array<Managed> candidateStorages = {};
				parent.FindComponents(BaseInventoryStorageComponent, candidateStorages);
				foreach (Managed storageObject : candidateStorages)
				{
					BaseInventoryStorageComponent storage = BaseInventoryStorageComponent.Cast(storageObject);
					if (storage.Type().ToString() != entry.StorageType) continue;
					InventoryStorageSlot slot = storage.GetSlot(entry.InventorySlotId);
					if (!slot) continue;
					slotCandidate = slot.GetAttachedEntity();
					PrintFormat("[EBG MOD RESTORED SLOT] oldId=%1 slot='%2' type=%3 template='%4' candidatePrefab='%5' candidateId=%6", entry.NativeId, slot.GetSourceName(), slot.Type().ToString(), slot.GetSlotTemplate(), SCR_ResourceNameUtils.GetPrefabName(slotCandidate), persistence.GetId(slotCandidate));
				}
			}
			UUID candidateId;
			UUID currentParentId;
			if (parent) currentParentId = persistence.GetId(parent);
			PrintFormat("[EBG CLEANUP MISSING PARENT MAP] originalParent=%1 currentParent=%2 tokenParentIndex=%3", entry.ParentId, currentParentId, FullEntryIndex(token, entry.ParentId));
			if (slotCandidate) candidateId = persistence.GetId(slotCandidate);
			PrintFormat("[EBG CLEANUP ALL MISSING UUID] original=%1 prefab='%2' parentId=%3 parentPresent=%4 weaponSlot=%5 loadedKind=%6 candidateId=%7 shape='%8'", entry.NativeId, entry.Prefab, entry.ParentId, parent != null, entry.WeaponInventorySlot, entry.LoadedSlotKind, candidateId, entry.ShapeSignature);
		}
		PrintFormat("[EBG CLEANUP ALL MISSING UUID] total=%1 capturedEntries=%2 readOnly=1", missing, token.Entries.Count());
	#endif
	}
	// Exact native identity/slot resolution shared by Full and disk metadata.
	protected bool ResolveOriginalMapping(EBG_CleanupFullEntry entry, IEntity parent, PersistenceSystem persistence, out IEntity entity, out string reason)
	{
		EBG_CleanupObject object = entry.Object;
		if (!entry.NativeId.IsNull()) entity = IEntity.Cast(persistence.FindById(entry.NativeId));
		else if (!entry.NativeClothSlot && !entry.WeaponInventorySlot && !entry.NamedEntitySlot) entity = FindFullLeaf(parent, entry);
		if (entry.GeneratedWeaponSight)
		{
			IEntity sight = FindFullLeaf(parent, entry);
			if (!sight || (entity && sight != entity)) { reason = "RHS authored sight model provenance changed"; return FullRebindFailure(reason, entry, sight); }
			entity = sight;
		}
		if (!entity && entry.StoredInventorySlot) entity = ResolveStoredInventorySlot(entry, parent, persistence);
		if (entry.NamedEntitySlot)
		{
			IEntity namedItem = ResolveNamedSlot(entry, parent);
			if (!namedItem || (entity && entity != namedItem)) { reason = "Restored authored entity slot changed"; return FullRebindFailure(reason, entry, namedItem); }
			entity = namedItem;
		}
		if (!entity) ReportScabbard("missing UUID rebind", parent, null);
		if (entry.LoadedSlotKind > 0)
		{
			IEntity loadedParent = parent;
			IEntity loaded = ResolveLoadedSlot(entry, loadedParent);
#ifdef EBG_ACCEPTANCE_TEST
			if (!entity) ReportLoadedSlots("rebind missing original UUID", loadedParent, null);
#endif
			if (entity && entity != loaded) { reason = "Cleanup saved round no longer matches native loaded slot"; return FullRebindFailure(reason, entry, entity); }
#ifdef EBG_ACCEPTANCE_TEST
			if (entry.LoadedMagazine && loaded) PrintFormat("[EBG CLEANUP MAGAZINE REBIND] oldId=%1 newId=%2 weaponId=%3 muzzle=%4 ammo=%5 maxAmmo=%6 nativeSlotMatched=1", entry.NativeId, persistence.GetId(loaded), entry.ParentId, entry.MuzzleIndex, entry.MagazineAmmo, entry.MagazineMaxAmmo);
#endif
			entity = loaded; // Only the getter that matched the exact original may replace its UUID.
		}
		if (entry.WeaponInventorySlot)
		{
			IEntity attachment = ResolveWeaponInventorySlot(entry, parent);
			if (!attachment || (entity && entity != attachment)) { reason = "Cleanup saved attachment no longer matches its exact native inventory slot"; return FullRebindFailure(reason, entry, attachment); }
#ifdef EBG_ACCEPTANCE_TEST
			if (!entity) PrintFormat("[EBG CLEANUP WEAPON SLOT REBIND] oldId=%1 newId=%2 parentId=%3 storage=%4 slot=%5 name=%6 reciprocalNativeSlot=1", entry.NativeId, persistence.GetId(attachment), entry.ParentId, entry.StorageType, entry.InventorySlotId, entry.InventorySlotName);
#endif
			entity = attachment;
		}
		if (entry.NativeClothSlot)
		{
			IEntity accessory = ResolveClothSlot(entry, parent);
			if (!accessory || (entity && entity != accessory))
			{
				reason = "Cleanup restored cloth accessory slot or saved state changed";
				return FullRebindFailure(reason, entry, accessory);
			}
			entity = accessory;
		}
		if (entry.NativeScabbardSlot)
		{
			IEntity blade = ResolveScabbardSlot(entry, parent);
			if (!blade || (entity && entity != blade)) { reason = "Cleanup regenerated scabbard child has changed native provenance"; return FullRebindFailure(reason, entry, blade); }
			entity = blade;
		}
		if (entry.GeneratedDummyBelt)
		{
			IEntity belt = ResolveDummyBelt(entry, object.Member.Entity);
			if (!belt || (entity && entity != belt)) { reason = "Cleanup restored BDU dummy belt provenance changed"; return FullRebindFailure(reason, entry, belt); }
#ifdef EBG_ACCEPTANCE_TEST
			if (!entity) PrintFormat("[EBG CLEANUP DUMMY BELT] restored oldId=%1 newId=%2 sourceJacketId=%3 exactShapeAndOwnership=1", entry.NativeId, persistence.GetId(belt), entry.SourceJacketId);
#endif
			entity = belt;
		}
		return true;
	}
	protected bool ResolveFullEntry(EBG_CleanupFullTransfer token, int entryIndex, PersistenceSystem persistence, array<IEntity> restored, out string reason)
	{
		EBG_CleanupFullEntry entry = token.Entries[entryIndex];
		IEntity parent = FullResolvedParent(token, entry, persistence, restored);
		EBG_CleanupObject object = entry.Object;
		if (!m_Objects.Contains(object) || !object.FullDetached || object.Group != token.Group || object.Member.Dead || object.Member.WasPlayer || !object.Member.Entity || persistence.GetId(object.Member.Entity) != entry.MemberId)
		{ reason = "Cleanup original member binding is unavailable or changed"; return FullRebindFailure(reason, entry, object.Entity); }
		if (object.Held != entry.WasHeld || object.Member.Entity.GetCharacterController().IsDead() || object.Member.Entity.GetCharacterController().IsPlayerControlled())
		{ reason = "Cleanup restored ownership or life state changed"; return FullRebindFailure(reason, entry, object.Entity); }
		IEntity entity;
		if (!ResolveOriginalMapping(entry, parent, persistence, entity, reason)) return false;
		if (!entity || restored.Contains(entity) || Holder(entity) != object.Member.Entity || SCR_ResourceNameUtils.GetPrefabName(entity) != entry.Prefab || FullShape(entity) != entry.ShapeSignature)
		{ reason = "Cleanup restored item identity or native shape does not match"; return FullRebindFailure(reason, entry, entity, restored.Contains(entity)); }
		if (entry.CapturedMagazine)
		{
			MagazineComponent magazine = MagazineComponent.Cast(entity.FindComponent(MagazineComponent));
			if (!magazine || magazine.Type().ToString() != entry.MagazineType || magazine.GetAmmoCount() != entry.MagazineAmmo || magazine.GetMaxAmmoCount() != entry.MagazineMaxAmmo || magazine.GetAmmoType() != entry.MagazineAmmoType)
			{ reason = "Restored magazine contents differ from captured ammunition"; return FullRebindFailure(reason, entry, entity); }
		}
		if (entry.StoredInventorySlot)
		{
			if (!MatchesStoredInventorySlot(entry, parent, entity, persistence, true))
			{
				reason = "Cleanup restored logical inventory slot changed";
#ifdef EBG_ACCEPTANCE_TEST
				ReportInventoryBinding(entry, entity, persistence);
#endif
				return FullRebindFailure(reason, entry, entity);
			}
#ifdef EBG_ACCEPTANCE_TEST
			if (entity.GetParent() != parent || entry.PhysicalParentId != entry.ParentId) ReportInventoryBinding(entry, entity, persistence);
#endif
		}
		else if (!entry.NativeClothSlot && (entry.ParentEntry >= 0 || !entry.ParentId.IsNull()) && (!parent || entity.GetParent() != parent))
		{
			reason = "Cleanup restored item parent identity changed";
#ifdef EBG_ACCEPTANCE_TEST
			ReportInventoryBinding(entry, entity, persistence);
#endif
			return FullRebindFailure(reason, entry, entity);
		}
		EBG_CleanupObject existing = Find(entity);
		if (existing && existing != object) { reason = "Cleanup restored item aliases another ownership record"; return FullRebindFailure(reason, entry, entity, true); }
		if (entry.WasHeld && (m_ReleasedIds.Contains(persistence.GetId(entity)) || (!entry.NativeId.IsNull() && m_ReleasedIds.Contains(entry.NativeId))))
		{ reason = "Cleanup restored held item has permanent loot-release identity"; return FullRebindFailure(reason, entry, entity); }
#ifdef EBG_ACCEPTANCE_TEST
		if (parent && persistence.GetId(parent) != entry.ParentId) PrintFormat("[EBG CLEANUP VALIDATED PARENT MAP] original=%1 current=%2 originalParent=%3 currentParent=%4 loadedKind=%5 directParentMuzzles=%6", entry.NativeId, persistence.GetId(entity), entry.ParentId, persistence.GetId(parent), entry.LoadedSlotKind, entry.DirectParentMuzzles);
#endif
		restored[entryIndex] = entity;
		return true;
	}
	// Root must first rebind the SAME living EBG_CacheMember objects and validate native JSON.
	// Resolve every mapping before replacing any ledger reference or attaching any listener.
	bool RebindFull(EBG_CleanupFullTransfer token, out string reason)
	{
		reason = "Cleanup restore requires retained detached transfer";
		PersistenceSystem persistence = PersistenceSystem.GetInstance();
		if (!Replication.IsServer() || !persistence || !token || !token.Detached || token.Completed) { return FullRebindFailure(reason); }
		if (token.Validation && !token.Validation.Completed && !token.Validation.Validator.RebindFull(token.Validation, reason)) { return false; }
		int count = token.Entries.Count();
		if (count > 2048) { reason = "Cleanup transfer item count outside bounds"; return FullRebindFailure(reason); }
		array<IEntity> restored = {};
		array<UUID> identities = {};
		foreach (EBG_CleanupFullEntry captured : token.Entries)
		{
			restored.Insert(null);
			if (!captured.NativeId.IsNull())
			{
				if (identities.Contains(captured.NativeId)) { reason = "Cleanup transfer has duplicate original identity"; return FullRebindFailure(reason, captured); }
				identities.Insert(captured.NativeId);
			}
		}
		// Parent-first resolution also handles children captured before parents.
		// Every mapped parent has passed the same shape, holder and loot checks.
		int resolved;
		int passes;
		while (resolved < count)
		{
			if (++passes > 32)
			{
				reason = "Cleanup captured parent hierarchy exceeds bounded resolution depth";
#ifdef EBG_ACCEPTANCE_TEST
				ReportMissingFullUUIDs(token, persistence, restored);
#endif
				return FullRebindFailure(reason);
			}
			bool progress = false;
			for (int entryIndex = 0; entryIndex < count; entryIndex++)
			{
				if (restored[entryIndex]) continue;
				EBG_CleanupFullEntry entry = token.Entries[entryIndex];
				int parentIndex = entry.ParentEntry;
				if (parentIndex < 0) parentIndex = FullEntryIndex(token, entry.ParentId);
				if (parentIndex >= 0)
				{
					if (token.Entries[parentIndex].MemberId != entry.MemberId) { reason = "Cleanup captured parent belongs to another member"; return FullRebindFailure(reason, entry); }
					if (!restored[parentIndex]) continue;
				}
				if (!ResolveFullEntry(token, entryIndex, persistence, restored, reason))
				{
#ifdef EBG_ACCEPTANCE_TEST
					ReportMissingFullUUIDs(token, persistence, restored);
#endif
					return false;
				}
				resolved++; progress = true;
			}
			if (!progress)
			{
				reason = "Cleanup captured parent dependency cycle or unresolved identity";
#ifdef EBG_ACCEPTANCE_TEST
				ReportMissingFullUUIDs(token, persistence, restored);
#endif
				return FullRebindFailure(reason);
			}
		}
		if (m_ValidationOnly)
		{
			EBG_CacheCleanup currentTree = new EBG_CacheCleanup();
			currentTree.m_ValidationOnly = true;
			if (!currentTree.CaptureValidationObjects(token.Group, reason)) { return false; }
			if (currentTree.m_Objects.Count() != count) { reason = "Full restored inventory tree has extra or missing entities"; return false; }
			foreach (EBG_CleanupObject currentObject : currentTree.m_Objects)
				if (!restored.Contains(currentObject.Entity)) { reason = "Full restored inventory tree contains an unmatched entity"; return false; }
#ifdef EBG_ACCEPTANCE_TEST
			PrintFormat("[EBG FULL INVENTORY] verified=%1 exactNativeMappings=1 extraEntities=0 cleanupHeld=0", count);
#endif
		}
		// All native parent/slot/tree mappings must validate before restoring state.
		for (int stateIndex = 0; stateIndex < token.Entries.Count(); stateIndex++)
		{
			EBG_CleanupFullEntry stateEntry = token.Entries[stateIndex];
			if (stateEntry.OptionalState && !stateEntry.OptionalState.Apply(restored[stateIndex], true)) { reason = stateEntry.OptionalState.Error; return false; }
			if (!stateEntry.BayonetMaterial) continue;
			SCR_BayonetComponent bayonet = SCR_BayonetComponent.Cast(restored[stateIndex].FindComponent(SCR_BayonetComponent));
			if (!bayonet || !bayonet.EBG_RestoreFullState(stateEntry.BayonetBlood, stateEntry.BayonetMaterial)) { reason = "Native bayonet blood/material state did not restore"; return false; }
			if (stateEntry.NativeScabbardSlot && !stateEntry.Object.Member.Entity.EBG_PublishClothBlade(restored[stateIndex], stateEntry.BayonetBlood, stateEntry.BayonetMaterial)) { reason = "Native cloth bayonet replication binding did not restore"; return false; }
		}
		for (int i = 0; i < token.Entries.Count(); i++)
		{
			EBG_CleanupObject object = token.Entries[i].Object;
			RebindObject(object, restored[i]);
			object.Inventory = InventoryItemComponent.Cast(object.Entity.FindComponent(InventoryItemComponent));
			object.FullDetached = false;
			if (m_ValidationOnly) continue;
			if (object.Held)
			{
				if (object.Inventory) object.Inventory.m_OnParentSlotChangedInvoker.Insert(object.OnSlotChanged);
				MaintainNativeProtection(object);
			}
			else
			{
				if (!m_ReleasedForever.Contains(object.Entity)) m_ReleasedForever.Insert(object.Entity);
				RememberReleasedId(object.Entity);
			}
		}
		token.Detached = false; token.Completed = true;
		if (!m_ValidationOnly) RememberPersistentMaps(token.Group);
		reason = "Original cleanup ownership rebound; excluded items remain excluded";
		return true;
	}
	// Abort is legal only before native deletion: every original must still exist unchanged.
	bool AbortFull(EBG_CleanupFullTransfer token)
	{
		if (!Replication.IsServer() || !token || token.Completed) { return false; }
		foreach (EBG_CleanupFullEntry entry : token.Entries)
			if (!entry.Original || !m_Objects.Contains(entry.Object) || entry.Object.Entity != entry.Original || entry.Object.Member.Dead || Holder(entry.Original) != entry.Object.Member.Entity) { return false; }
		if (token.Validation && !token.Validation.Completed && !token.Validation.Validator.AbortFull(token.Validation)) { return false; }
		if (token.Detached)
		{
			foreach (EBG_CleanupFullEntry entry : token.Entries)
			{
				entry.Object.FullDetached = false;
				if (!m_ValidationOnly && entry.Object.Held && entry.Object.Inventory) entry.Object.Inventory.m_OnParentSlotChangedInvoker.Insert(entry.Object.OnSlotChanged);
			}
		}
		token.Detached = false; token.Completed = true;
		return true;
	}

	// Retire only fully resolved records. Protected or released entities that
	// still exist, pending Full recovery, and changed native rosters retain ownership.
	bool CanRetire(EBG_CacheGroup record)
	{
		if (!record || record.PersistentScalarRollbackPending || !record.CleanupRegistered || record.Full || record.FullCleanup || record.Simulation || record.Recovery != "" || record.Members.IsEmpty()) { return false; }
		foreach (EBG_CacheMember member : record.Members)
		{
			if (!member.Dead || member.Entity) { return false; }
		}
		if (record.Group && record.Group.GetAgentsCount() > 0) { return false; }
		foreach (EBG_CleanupObject object : m_Objects)
		{
			if (object.Group == record && (object.Entity || object.FullDetached)) { return false; }
		}
		return true;
	}
	// Read-only proof hook reuses the production wake/actual-remains geometry.
	bool HasNearbyPlayer(EBG_CacheGroup record, array<vector> players) { return record && record.Zone && PlayerNear(record, players); }
	protected bool PlayerNear(EBG_CacheGroup record, array<vector> players)
	{
		if (!players || players.IsEmpty()) return false;
		EBG_CacheZone zone = record.Zone;
		float low = float.MAX;
		float high = -float.MAX;
		foreach (EBG_CacheMember member : record.Members)
		{
			vector position = member.Position;
			if (member.Entity) position = member.Entity.GetOrigin();
			low = Math.Min(low, position[1]); high = Math.Max(high, position[1]);
		}
		vector center = record.Anchor;
		float radius = zone.GroupWake;
		if (zone.Strategy == 0) { center = zone.GetOrigin(); radius = zone.ZoneWake; }
		if (EBG_CacheGeometry.AnyPlayer(players, center, radius, low, high, zone.Height != 0, zone.Above, zone.Below, 0)) { return true; }
		foreach (EBG_CleanupObject object : m_Objects)
		{
			if (object.Group != record || !object.Held || !object.Entity) continue;
			vector position = object.Entity.GetOrigin();
			if (EBG_CacheGeometry.AnyPlayer(players, position, zone.GroupWake, position[1], position[1], zone.Height != 0, zone.Above, zone.Below, 0)) { return true; }
		}
		return false;
	}

	// All recursively deleted entities must be ours and authorized. Unknown,
	// player-returned or mission-protected contents keep the containing body/item.
	protected string DescribeNode(IEntity entity)
	{
		if (!entity) { return "null"; }
		string description = string.Format("id=%1 type=%2 prefab='%3' flags=%4", entity.GetID(), entity.Type().ToString(), SCR_ResourceNameUtils.GetPrefabName(entity), entity.GetFlags());
		IEntity parent = entity.GetParent();
		if (parent) description += string.Format(" parent=%1:%2:'%3'", parent.GetID(), parent.Type().ToString(), SCR_ResourceNameUtils.GetPrefabName(parent));
		EBG_CleanupObject object = Find(entity);
		if (object) description += string.Format(" registered=true held=%1 allowed=%2 corpse=%3", object.Held, object.Allowed, object.Corpse);
		else description += " registered=false";
		if (object && object.ReleaseReason != "") description += " release=" + object.ReleaseReason;
		array<Managed> components = {};
		entity.FindComponents(GenericComponent, components);
		description += " components=";
		foreach (Managed component : components) description += component.Type().ToString() + ",";
		return description;
	}
	protected bool SafeTree(IEntity entity, EBG_CacheGroup record, int depth = 0)
	{
		// Separate reasons: only a real keep/mission chain may park a casualty root.
		if (!entity || depth > 32) { m_Reason = "Missing or deeply nested hierarchy"; return false; }
		if (ProtectedOwnerChain(entity)) { m_Reason = "Missing, deeply nested or keep-protected hierarchy"; return false; }
		EBG_CleanupObject object = Find(entity);
		// Every node must be this record's held, approved row and, while a casualty root
		// is proven, belong to that same member: a survivor's character or kit can never
		// be deleted together with a casualty.
		if (!object || !object.Held || !object.Allowed || object.Group != record || object.FullDetached || (m_TreeOwner && object.Member != m_TreeOwner))
		{
			m_Reason = "Unowned, released or unapproved hierarchy node: " + DescribeNode(entity);
			if (object && !object.Corpse && !object.Allowed) m_Reason += " policy=" + ItemPolicyReason(entity);
			if (object && m_TreeOwner && object.Member != m_TreeOwner) m_Reason += " owner=another member";
			return false;
		}
		if (!object.Corpse && !VanillaItemPolicy(entity)) { m_Reason = ItemPolicyReason(entity); return false; }
		if (m_TreeNodes)
		{
			// Storage contents are usually hierarchy children too: prove each node once.
			if (m_TreeNodes.Contains(entity)) return true;
			if (m_TreeNodes.Count() >= 4096) { m_Reason = "Casualty tree exceeds 4096 entities: " + DescribeNode(entity); return false; }
			m_TreeNodes.Insert(entity);
		}
		array<Managed> storages = {};
		entity.FindComponents(BaseInventoryStorageComponent, storages);
		foreach (Managed storageObject : storages)
		{
			BaseInventoryStorageComponent storage = BaseInventoryStorageComponent.Cast(storageObject);
			array<IEntity> items = {};
			storage.GetAll(items);
			if (items.Count() > 256) { m_Reason = "Storage holds more than 256 items: " + DescribeNode(entity); return false; }
			foreach (IEntity item : items)
				if (item != entity && !SafeTree(item, record, depth + 1)) { return false; }
		}
		for (IEntity child = entity.GetChildren(); child; child = child.GetSibling())
			if (!SafeTree(child, record, depth + 1)) { return false; }
		return true;
	}
	// Read-only proof hook; deletion also requires all independent Tick guards.
	bool CanDeleteTree(IEntity entity, EBG_CacheGroup record)
	{
		m_Reason = "";
		EBG_CleanupObject root = Find(entity);
		m_TreeOwner = null;
		if (root) m_TreeOwner = root.Member;
		bool safe = SafeTree(entity, record);
		m_TreeOwner = null;
		return safe;
	}
	// Bounded, read-only test diagnostics; preserves the last production Tick reason.
	void ReportRemaining(EBG_CacheGroup record)
	{
#ifdef EBG_ACCEPTANCE_TEST
		if (!record) return;
		string previousReason = m_Reason;
		int held, released, corpses, shown;
		foreach (EBG_CleanupObject object : m_Objects)
		{
			if (object.Group != record || !object.Entity) continue;
			if (object.Held) held++; else released++;
			if (object.Corpse)
			{
				corpses++;
				if (corpses <= 4)
				{
					bool safe = CanDeleteTree(object.Entity, record);
					PrintFormat("[EBG CLEANUP REMAINS] corpse %1 tree=%2 confirmed=%3 deathTime=%4 reason=%5", DescribeNode(object.Entity), safe, object.DeathConfirmed, object.DeathTime, m_Reason);
				}
			}
			else if (shown < 4)
			{
				shown++;
				PrintFormat("[EBG CLEANUP REMAINS] item %1 policy=%2", DescribeNode(object.Entity), ItemPolicyReason(object.Entity));
			}
		}
		PrintFormat("[EBG CLEANUP REMAINS] group=%1 existing held=%2 released=%3 corpses=%4 clearSince=%5 lastTick=%6", record.Id, held, released, corpses, record.CleanupClearSince, previousReason);
		m_Reason = previousReason;
	#endif
	}

	// Bodies must be exact original managed AI casualties, never player bodies.
	bool IsBodyCleanupTarget(IEntity entity, EBG_CacheGroup record)
	{
		EBG_CleanupObject object = Find(entity);
		if (!object || !record || object.Group != record || !object.Corpse || !object.Held || !object.Allowed || !object.DeathConfirmed) { return false; }
		EBG_CacheMember member = object.Member;
		SCR_ChimeraCharacter body = SCR_ChimeraCharacter.Cast(entity);
		if (!member || !body || member.Entity != body || !member.Dead || member.WasPlayer || EntityUtils.IsPlayer(body)) { return false; }
		CharacterControllerComponent controller = body.GetCharacterController();
		return controller && controller.IsDead() && !controller.IsPlayerControlled();
	}
	bool IsOwnedCleanupTarget(IEntity entity, EBG_CacheGroup record)
	{
		if (!entity || !record) { return false; }
		EBG_CleanupObject object = Find(entity);
		if (!object || object.Group != record || !object.Held || !object.Allowed || object.FullDetached || !object.Member || !object.Member.Dead || object.Member.WasPlayer) { return false; }
		if (object.Corpse) { return IsBodyCleanupTarget(entity, record); }
		// The allowlisted original inventory ledger admits equipment only. Visual
		// body parts and muzzle projectiles are removed with their containing root.
		ResourceName prefab = SCR_ResourceNameUtils.GetPrefabName(entity);
		if (IsHeadLeaf(entity) || BelongingsModels().Contains(prefab) || entity.FindComponent(BaseProjectileComponent)) { return false; }
		return entity.FindComponent(InventoryItemComponent) || entity.FindComponent(BaseLoadoutClothComponent);
	}

	// Death time of the member's confirmed corpse row, or -1. A removed body keeps its
	// row, so the member's owned ground equipment still has an age. Cached on the
	// member; only a body-less member ever needs one ledger scan.
	protected float CasualtyDeathTime(EBG_CacheGroup record, EBG_CacheMember member)
	{
		if (member.CleanupDeathTime >= 0) return member.CleanupDeathTime;
		if (member.Entity)
		{
			EBG_CleanupObject present = Find(member.Entity);
			if (present && present.Corpse && present.Member == member && present.Group == record && present.DeathConfirmed) member.CleanupDeathTime = present.DeathTime;
			if (member.CleanupDeathTime < 0) return -1;
			return member.CleanupDeathTime;
		}
		if (member.CleanupDeathTime < -1) return -1;
		member.CleanupDeathTime = -2;
		foreach (EBG_CleanupObject object : m_Objects)
			if (object.Corpse && object.Member == member && object.Group == record && object.DeathConfirmed) member.CleanupDeathTime = object.DeathTime;
		if (member.CleanupDeathTime < 0) return -1;
		return member.CleanupDeathTime;
	}
	// A member that died while Simulation cached is still referenced by the live
	// snapshot. Deleting it would make Restore report a missing member and retain
	// recovery, so it waits until the survivors are restored.
	protected bool InLiveSimulationSnapshot(EBG_CacheGroup record, EBG_CacheMember member)
	{
		if (!record || !record.Simulation || !member || !member.Entity) return false;
		foreach (EBG_SimulationAgent cached : record.Simulation.Members)
			if (cached && cached.Character == member.Entity) return true;
		return false;
	}
	// Per-casualty eligibility. Survivors, possessed members and casualties still in a
	// live Simulation snapshot are never eligible.
	protected bool CasualtyMature(EBG_CacheGroup record, EBG_CacheMember member, float now, float minimumAge)
	{
		if (!record || !member || !member.Dead || member.WasPlayer) return false;
		if (member.Entity)
		{
			CharacterControllerComponent controller = member.Entity.GetCharacterController();
			if (!controller || !controller.IsDead() || controller.IsPlayerControlled() || member.Entity.EBG_WasPlayerControlled() || EntityUtils.IsPlayer(member.Entity)) return false;
			if (InLiveSimulationSnapshot(record, member)) return false;
		}
		float died = CasualtyDeathTime(record, member);
		return died >= 0 && now - died >= minimumAge;
	}
	// O(members) summary. Body-less casualties without a confirmed row count nowhere.
	protected bool SummarizeCasualties(EBG_CacheGroup record, float now, float minimumAge, out float youngestDue, out int snapshotHeld)
	{
		youngestDue = -1;
		snapshotHeld = 0;
		bool mature;
		foreach (EBG_CacheMember member : record.Members)
		{
			if (!member.Dead || member.WasPlayer) continue;
			// A removed body with nothing left to delete never keeps the record mature,
			// so a younger casualty waits on the O(members) corpse-age hold.
			if (!member.Entity && member.CleanupDrained) continue;
			if (CasualtyMature(record, member, now, minimumAge)) { mature = true; continue; }
			if (InLiveSimulationSnapshot(record, member)) { snapshotHeld++; continue; }
			float died = CasualtyDeathTime(record, member);
			if (died < 0) continue;
			float due = died + minimumAge;
			if (due > now && (youngestDue < 0 || due < youngestDue)) youngestDue = due;
		}
		return mature;
	}
	protected static void RearmDrained(EBG_CacheGroup record)
	{
		if (!record) return;
		foreach (EBG_CacheMember member : record.Members)
			if (member) member.CleanupDrained = false;
	}
	// A held but never-approved remain lying on its own (for example a dropped modded
	// weapon). EBG never deletes it; once its casualty is mature, that whole casualty is
	// handed back intact (RemoveCasualty refuses it before deleting anything).
	protected bool StandaloneUnapproved(EBG_CleanupObject object, EBG_CacheGroup record)
	{
		return object.Group == record && object.Held && !object.Allowed && !object.Corpse && object.Entity && !object.FullDetached && object.Member && object.Member.Dead && !object.Member.WasPlayer && Holder(object.Entity) == object.Entity;
	}
	// A row a later scan could still delete or hand back. Parked (mission-protected)
	// casualties and rows that only ride inside another root never keep a record unsettled.
	protected bool PendingCasualtyRow(EBG_CleanupObject object, EBG_CacheGroup record)
	{
		if (object.Group != record || !object.Held || !object.Entity || object.FullDetached || !object.Member || object.Member.CleanupBlocks >= CASUALTY_BLOCK_LIMIT) return false;
		if (IsOwnedCleanupTarget(object.Entity, record)) return true;
		return StandaloneUnapproved(object, record);
	}
	protected int CountCasualtyRows(EBG_CacheGroup record)
	{
		int rows;
		foreach (EBG_CleanupObject object : m_Objects)
		{
			if (!PendingCasualtyRow(object, record)) continue;
			rows++;
			// A pending row means its casualty is not drained after all.
			if (object.Member) object.Member.CleanupDrained = false;
		}
		return rows;
	}
	// 0.1.4 rule (user decision): a casualty is its body, deleted in one native call that
	// takes everything inside it, plus its own items lying loose on the ground (the weapon
	// dropped on death). No per-item policy checks. Rows held by anyone else (a player who
	// picked an item up, a vehicle or box) are never targets. Returns the body, if any.
	protected IEntity CollectCasualtyTargets(EBG_CacheGroup record, EBG_CacheMember member, array<EBG_CleanupObject> rows, array<IEntity> targets)
	{
		IEntity body;
		foreach (EBG_CleanupObject object : m_Objects)
		{
			if (object.Group != record || object.Member != member || !object.Held || !object.Entity || object.FullDetached) continue;
			rows.Insert(object);
			if (object.Corpse) body = object.Entity;
		}
		if (body) targets.Insert(body);
		foreach (EBG_CleanupObject row : rows)
		{
			if (row.Corpse || row.Entity == body || targets.Contains(row.Entity)) continue;
			if (Holder(row.Entity) == row.Entity) targets.Insert(row.Entity);
		}
		return body;
	}
	// Read-only test hooks; deletion also requires all independent Tick guards.
	bool CanDeleteCasualty(EBG_CacheGroup record, EBG_CacheMember member)
	{
		if (!record || !member) return false;
		array<EBG_CleanupObject> rows = {};
		array<IEntity> targets = {};
		CollectCasualtyTargets(record, member, rows, targets);
		return !targets.IsEmpty();
	}
	int CollectHeldMemberEntities(EBG_CacheMember member, array<IEntity> entities)
	{
		foreach (EBG_CleanupObject object : m_Objects)
			if (object.Member == member && object.Held && object.Entity && !entities.Contains(object.Entity)) entities.Insert(object.Entity);
		return entities.Count();
	}
	protected static bool KeepProtected(string reason)
	{
		return reason.Contains("keep-protected") || reason.Contains("mission-protected") || reason.Contains("valuable intel");
	}
	// Order-independent park test: SafeTree reports only the first blocker it meets,
	// so a foreign magazine can hide intel or a keep component in the same tree.
	// A missing or over-deep node proves nothing either way and never parks.
	protected bool TreeHoldsProtected(IEntity entity, int depth = 0)
	{
		if (!entity || depth > 32) return false;
		if (ProtectedOwnerChain(entity)) return true;
		SCR_IdentityInventoryItemComponent identity = SCR_IdentityInventoryItemComponent.Cast(entity.FindComponent(SCR_IdentityInventoryItemComponent));
		if (identity && identity.GetValuableIntelFactionID() >= 0) return true;
		array<Managed> storages = {};
		entity.FindComponents(BaseInventoryStorageComponent, storages);
		foreach (Managed storageObject : storages)
		{
			BaseInventoryStorageComponent storage = BaseInventoryStorageComponent.Cast(storageObject);
			if (!storage) continue;
			array<IEntity> items = {};
			storage.GetAll(items);
			foreach (IEntity item : items)
				if (item != entity && TreeHoldsProtected(item, depth + 1)) return true;
		}
		for (IEntity child = entity.GetChildren(); child; child = child.GetSibling())
			if (TreeHoldsProtected(child, depth + 1)) return true;
		return false;
	}
	// The casualty's body row when its body is still present, otherwise the given row.
	protected EBG_CleanupObject CasualtyReportRow(EBG_CacheGroup record, EBG_CacheMember member, EBG_CleanupObject fallback)
	{
		EBG_CleanupObject body = Find(member.Entity);
		if (body && body.Corpse && body.Group == record && body.Member == member) return body;
		return fallback;
	}
	// H3: bounded per-casualty retry. Nothing of this casualty was deleted. A rejected
	// casualty never puts the record in back-off, so other casualties drain on later scans.
	protected void BlockCasualty(EBG_CacheGroup record, EBG_CacheMember member, array<EBG_CleanupObject> rows, EBG_CleanupObject blocker, float now, string reason, bool immediate)
	{
		member.CleanupBlocks++;
		if (immediate) member.CleanupBlocks = CASUALTY_BLOCK_LIMIT;
		member.CleanupRetryAt = now + CASUALTY_RETRY_SECONDS;
		m_Reason = reason;
		if (member.CleanupBlocks < CASUALTY_BLOCK_LIMIT)
		{
			m_Phase = "blocked casualty retry";
			m_Detail = string.Format("%1 (attempt %2 of %3)", Friendly(reason), member.CleanupBlocks, CASUALTY_BLOCK_LIMIT);
			return;
		}
		EBG_CleanupObject report = CasualtyReportRow(record, member, blocker);
		IEntity reportEntity;
		if (report) reportEntity = report.Entity;
		bool keep = KeepProtected(reason);
		foreach (EBG_CleanupObject row : rows)
			if (!keep && row.Entity && TreeHoldsProtected(row.Entity)) keep = true;
		if (keep)
		{
			// Mission protection keeps the 0.1.31 rule: held, never deleted and never
			// handed to native garbage. The whole casualty is parked: no further retries.
			m_Reason = "Cleanup held: protected casualty remains kept";
			m_Phase = "protected remains kept";
			m_Detail = Friendly(reason);
			PrintFormat("[EBG CLEANUP KEEP] group=%1 member=%2 root=%3 blocker='%4'", record.Id, member.Id, DescribeNode(reportEntity), reason);
			return;
		}
		ReleaseCasualty(record, member, report, reason);
		m_Reason = "Cleanup released an intact blocked casualty to native garbage handling";
		m_Phase = "blocked casualty returned to native garbage";
		m_Detail = m_Phase;
	}
	// Non-permanent release of the WHOLE intact casualty (body, gear and owned ground
	// roots): no lineage or negative IDs, so saves and member identity are unaffected.
	// Native garbage resumes the lifetime it requested (vanilla or Persistent Battlefield);
	// without a captured request the remains behave as they would without EBG.
	protected void ReleaseCasualty(EBG_CacheGroup record, EBG_CacheMember member, EBG_CleanupObject report, string reason)
	{
		string node = "null";
		bool lifetimeRequested;
		if (report)
		{
			node = DescribeNode(report.Entity);
			lifetimeRequested = report.NativeRequested;
		}
		int released;
		foreach (EBG_CleanupObject object : m_Objects)
		{
			if (object.Group != record || object.Member != member || !object.Held || !object.Entity || object.FullDetached) continue;
			object.ReleaseReason = "Cleanup blocked after bounded retries; native garbage handling resumed";
			ReleaseObject(object, false);
			released++;
		}
		PrintFormat("[EBG CLEANUP RELEASE] group=%1 member=%2 attempts=%3 rows=%4 nativeLifetimeRequested=%5 root=%6 blocker='%7'", record.Id, member.Id, member.CleanupBlocks, released, lifetimeRequested, node, reason);
	}
	// The casualty is the unit of work: its body and its loose items go in this same tick,
	// so a body is never left partially stripped across scans.
	protected void RemoveCasualty(EBG_CacheGroup record, EBG_CacheMember member, array<vector> players, float now)
	{
		EBG_CacheZone zone = record.Zone;
		array<EBG_CleanupObject> rows = {};
		array<IEntity> targets = {};
		IEntity body = CollectCasualtyTargets(record, member, rows, targets);
		// Items taken by players or stored elsewhere stop being tracked and are never deleted.
		foreach (EBG_CleanupObject row : rows)
			if (row && row.Entity && !targets.Contains(row.Entity) && Holder(row.Entity) != body) ReleaseObject(row, true);
		if (targets.IsEmpty())
		{
			member.CleanupBlocks = 0;
			m_Phase = "done";
			m_Detail = "casualty items were taken or stored elsewhere; nothing left to remove";
			return;
		}
		// Recheck positions and this casualty's death and age immediately before deletion.
		if (!zone.Enabled || !zone.Cleanup || zone.Editing || PlayerNear(record, players) || !CasualtyMature(record, member, now, zone.CorpseAge))
		{
			record.CleanupClearSince = -1;
			m_Phase = "rechecking";
			m_Detail = "players or casualty state changed at deletion; clear delay restarts";
			return;
		}
		// Explicit protection still wins: a keep component or valuable intel parks the casualty.
		foreach (IEntity kept : targets)
		{
			if (!TreeHoldsProtected(kept)) continue;
			BlockCasualty(record, member, rows, CasualtyReportRow(record, member, null), now, "Cleanup held: keep-protected or valuable intel casualty", true);
			return;
		}
		EntityID reportId = targets[0].GetID();
		bool hadBody = body != null;
		int count = targets.Count();
		foreach (IEntity target : targets)
			if (target) SCR_EntityHelper.DeleteEntityAndChildren(target);
		int left;
		foreach (IEntity remaining : targets)
			if (remaining) left++;
		if (left > 0)
		{
			// A refused or deferred native delete retries a minute later, at most three times.
			BlockCasualty(record, member, rows, CasualtyReportRow(record, member, null), now, string.Format("Native deletion not confirmed for %1 of %2 casualty objects", left, count), false);
			return;
		}
		if (hadBody) EBG_CacheDebug.CleanedBodies++;
		member.CleanupBlocks = 0;
		m_Reason = "One managed AI casualty removed: body and dropped weapon";
		m_Phase = "removed one casualty";
		m_Detail = m_Phase;
		if (zone.DebugMessages > 0)
			PrintFormat("[EBG CLEANUP REMOVED] zone=%1 group=%2 member=%3 root=%4 body=%5 roots=%6 rows=%7 survivors=%8 state='%9'", zone.GetID(), record.Id, member.Id, reportId, hadBody, count, rows.Count(), record.Alive, record.DebugState(false));
	}
	protected void PublishCasualtyStatus(EBG_CacheGroup record)
	{
		record.CleanupPhase = m_Phase;
		record.CleanupStatus = "";
		if (m_Phase == "") return;
		int waiting, gone, handedBack;
		foreach (EBG_CacheMember member : record.Members)
		{
			if (!member.Dead || member.WasPlayer) continue;
			if (!member.Entity) gone++;
			else if (IsHeld(member.Entity)) waiting++;
			else handedBack++;
		}
		string detail = m_Detail;
		if (detail == "") detail = m_Phase;
		record.CleanupStatus = string.Format("Casualty cleanup: %1 bodies waiting, %2 gone, %3 handed back | %4", waiting, gone, handedBack, detail);
	}

	// One casualty (its body and every owned root, proven together) across all records in
	// a main scan. A rejected casualty spends the same allowance as a native deletion.
	// The manager supplies the same timestamp and fresh player list to each record,
	// including Simulation-suspended and Full-cached ones. There, only dead members'
	// rows are candidates; survivors and their snapshots are never touched.
	void Tick(EBG_CacheGroup record, array<vector> players, float now, bool transfersChecked = false)
	{
		if (!Replication.IsServer() || !record) return;
		m_Phase = string.Empty;
		m_Detail = string.Empty;
		TickCasualties(record, players, now, transfersChecked);
		PublishCasualtyStatus(record);
	}
	protected void TickCasualties(EBG_CacheGroup record, array<vector> players, float now, bool transfersChecked)
	{
		if (record.PersistenceIssue != "") return;
		m_Reason = string.Empty;
		EBG_CacheZone zone = record.Zone;
		if (!zone || !zone.Enabled || !zone.Cleanup)
		{
			// Keep transfer listeners, native death provenance and released tombstones.
			// Only explicit ownership release discards this ledger.
			record.CleanupClearSince = -1;
			return;
		}
		if (record.Members.IsEmpty()) { record.CleanupClearSince = -1; return; }
		bool casualty;
		bool eliminated = true;
		foreach (EBG_CacheMember member : record.Members)
		{
			if (!member.Dead) eliminated = false;
			else if (!member.WasPlayer) casualty = true;
		}
		// Living groups, and groups with nothing owned left to remove, need no inventory,
		// roster or deletion scan until a new death, owned row, regroup or policy change.
		if (!casualty || record.CleanupCasualtiesSettled)
		{
			record.CleanupClearSince = -1;
			if (eliminated) m_Reason = "Cleanup: no eligible managed AI remains; unrelated and released objects preserved";
			if (casualty) { m_Phase = "done"; m_Detail = "no owned casualty remains left to remove"; }
			return;
		}
		float youngestDue;
		int snapshotHeld;
		bool mature = SummarizeCasualties(record, now, zone.CorpseAge, youngestDue, snapshotHeld);
		bool playerNear;
		if (!zone.Editing && mature) playerNear = PlayerNear(record, players);
		if (zone.Editing || playerNear || (!mature && (youngestDue > now || snapshotHeld > 0)))
		{
			record.CleanupClearSince = -1;
			record.CleanupNextAttempt = 0;
			m_Reason = "Cleanup held: death/age/player-clear requirements not met";
			if (zone.Editing) { m_Phase = "held while the zone is edited"; m_Detail = m_Phase; }
			else if (playerNear) { m_Phase = "held: player near zone, group or remains"; m_Detail = string.Format("held: player within %1 m of the zone, a group member or remains", zone.ZoneWake); }
			else if (youngestDue > now) { m_Phase = "waiting for corpse age"; m_Detail = string.Format("waiting for corpse age (%1 s)", Math.Ceil(youngestDue - now)); }
			else { m_Phase = "waiting for Simulation restore"; m_Detail = "casualty died while Simulation cached; waits until the survivors are restored"; }
			return;
		}
		if (!mature)
		{
			// No casualty can mature by time alone (body-less imported casualties, refused
			// holds, unconfirmed corpse rows). Verify the ledger at most once a minute and
			// settle when nothing owned is left.
			record.CleanupClearSince = -1;
			m_Reason = "Cleanup held: death/age/player-clear requirements not met";
			m_Phase = "waiting for death confirmation";
			m_Detail = m_Phase;
			if (now < record.CleanupNextAttempt) return;
			if (CountCasualtyRows(record) == 0)
			{
				record.CleanupCasualtiesSettled = true;
				m_Phase = "done";
				m_Detail = "no owned casualty remains left to remove";
				if (eliminated) m_Reason = "Cleanup: no eligible managed AI remains; unrelated and released objects preserved";
				return;
			}
			record.CleanupNextAttempt = now + CASUALTY_RETRY_SECONDS;
			return;
		}
		if (record.CleanupClearSince < 0) record.CleanupClearSince = now;
		if (now - record.CleanupClearSince < zone.CleanupDelay)
		{
			float delayLeft = Math.Ceil(zone.CleanupDelay - (now - record.CleanupClearSince));
			m_Reason = string.Format("Cleanup clear delay: %1 seconds remaining", delayLeft);
			m_Phase = "clear delay";
			m_Detail = string.Format("players clear; deleting in %1 s", delayLeft);
			return;
		}
		if (now < record.CleanupNextAttempt)
		{
			m_Reason = "Cleanup waiting for owned contents to change";
			m_Phase = "waiting to retry";
			m_Detail = string.Format("next check in %1 s", Math.Ceil(record.CleanupNextAttempt - now));
			return;
		}
		if (m_LastDeletionScan == now)
		{
			m_Reason = "Cleanup waiting for shared deletion allowance";
			m_Phase = "waiting for shared deletion allowance";
			m_Detail = m_Phase;
			return;
		}
		if (!transfersChecked) CheckGroupTransfers(record);
		int count = m_Objects.Count();
		if (record.CleanupObjectCursor >= count) record.CleanupObjectCursor = 0;
		int start = record.CleanupObjectCursor;
		int remaining;
		float nextEvent = now + CASUALTY_RETRY_SECONDS;
		if (youngestDue > now) nextEvent = Math.Min(nextEvent, youngestDue);
		EBG_CacheMember checkedMember;
		bool checkedMature;
		array<EBG_CacheMember> pendingMembers = {};
		for (int offset = 0; offset < count; offset++)
		{
			int index = (start + offset) % count;
			EBG_CleanupObject object = m_Objects[index];
			if (object.Group != record || !PendingCasualtyRow(object, record)) continue;
			remaining++;
			if (object.Member && !pendingMembers.Contains(object.Member))
			{
				pendingMembers.Insert(object.Member);
				object.Member.CleanupDrained = false;
			}
			if (object.Member != checkedMember)
			{
				checkedMember = object.Member;
				checkedMature = record.Members.Contains(checkedMember) && CasualtyMature(record, checkedMember, now, zone.CorpseAge);
			}
			// A young casualty, or one still in a live snapshot, never spends the shared allowance.
			if (!checkedMature) continue;
			if (now < checkedMember.CleanupRetryAt) { nextEvent = Math.Min(nextEvent, checkedMember.CleanupRetryAt); continue; }
			// Transfer checks may prune rows. Resume against the current ledger,
			// wrapping next time; no saved identity or ownership relies on this cursor.
			record.CleanupObjectCursor = index + 1;
			m_LastDeletionScan = now;
			RemoveCasualty(record, checkedMember, players, now);
			return;
		}
		// Nothing deletable now. With no pending row left, stay settled until ConfirmDeath,
		// Hold, regroup, import or a zone policy change re-arms the scan.
		// A full scan that deleted nothing proves which casualties are drained.
		foreach (EBG_CacheMember scanned : record.Members)
			if (scanned.Dead && !scanned.WasPlayer && !pendingMembers.Contains(scanned)) scanned.CleanupDrained = true;
		if (remaining == 0) record.CleanupCasualtiesSettled = true;
		record.CleanupNextAttempt = nextEvent;
		m_Reason = "Cleanup: no eligible managed AI remains; unrelated and released objects preserved";
		if (remaining == 0) { m_Phase = "done"; m_Detail = "no owned casualty remains left to remove"; return; }
		m_Phase = "nothing eligible";
		m_Detail = string.Format("nothing eligible now; next check in %1 s", Math.Ceil(nextEvent - now));
	}
}

// Native death and incapacitation share this synchronous creation method.
// Observe its new exact items once; never adopt an item from a later inventory scan.
modded class SCR_IdentityManagerComponent
{
	override protected void SpawnIdentityItemInInventory_S(ChimeraCharacter character, bool checkIfHadSpawnedIdentityItem = true)
	{
		SCR_ChimeraCharacter ownedCharacter = SCR_ChimeraCharacter.Cast(character);
		InventoryStorageManagerComponent inventory;
		array<IEntity> before = {};
		if (Replication.IsServer() && ownedCharacter && EBG_CacheCleanup.Instance && EBG_CacheCleanup.Instance.IsHeld(ownedCharacter))
		{
			inventory = InventoryStorageManagerComponent.Cast(ownedCharacter.FindComponent(InventoryStorageManagerComponent));
			if (inventory) inventory.GetItems(before);
		}
		super.SpawnIdentityItemInInventory_S(character, checkIfHadSpawnedIdentityItem);
		if (!inventory) return;
		array<IEntity> after = {};
		inventory.GetItems(after);
		if (before.Count() > 512 || after.Count() > 512)
		{
			PrintFormat("[EBG CLEANUP NATIVE BIRTH REJECT] ownedCharacter=%1 reason=inventory bound exceeded before=%2 after=%3", ownedCharacter.GetID(), before.Count(), after.Count());
			return;
		}
		foreach (IEntity item : after)
		{
			if (before.Contains(item) || !EBG_CacheCleanup.IsNativeBelongingsPrefab(item)) continue;
			SCR_IdentityInventoryItemComponent identity = SCR_IdentityInventoryItemComponent.Cast(item.FindComponent(SCR_IdentityInventoryItemComponent));
			if (!identity)
			{
				PrintFormat("[EBG CLEANUP NATIVE BIRTH REJECT] ownedCharacter=%1 item=%2 reason=missing native identity component", ownedCharacter.GetID(), item.GetID());
				continue;
			}
			CharacterControllerComponent controller = ownedCharacter.GetCharacterController();
#ifdef EBG_ACCEPTANCE_TEST
			PrintFormat("[EBG CLEANUP NATIVE BIRTH] ownedCharacter=%1 item=%2 dead=%3 unconscious=%4 newInNativeSpawn=1", ownedCharacter.GetID(), item.GetID(), controller && controller.IsDead(), controller && controller.IsUnconscious());
#endif
			identity.EBG_RecordNativeBirthOwner(ownedCharacter);
		}
	}
}

modded class SCR_IdentityInventoryItemComponent
{
	protected SCR_ChimeraCharacter m_EBG_NativeBirthOwner;
	protected bool m_EBG_NativeIdentityInitialized;
	void EBG_RecordNativeBirthOwner(SCR_ChimeraCharacter character)
	{
		m_EBG_NativeBirthOwner = character;
		if (m_EBG_NativeIdentityInitialized) EBG_RegisterNativeBirth();
	}
	protected void EBG_RegisterNativeBirth()
	{
		if (m_EBG_NativeBirthOwner && EBG_CacheCleanup.Instance)
			EBG_CacheCleanup.Instance.RegisterNativeBelongings(GetOwner(), m_EBG_NativeBirthOwner);
		m_EBG_NativeBirthOwner = null;
	}
	override protected void DelayedInit(IEntity owner)
	{
		super.DelayedInit(owner);
		m_EBG_NativeIdentityInitialized = true;
		EBG_RegisterNativeBirth();
	}
}

// Installed GarbageSystem hooks, scoped to this helper's held entities only.
modded class SCR_GarbageSystem
{
	override protected float OnInsertRequested(IEntity entity, float lifetime)
	{
		float adjusted = super.OnInsertRequested(entity, lifetime);
		bool veto = EBG_CacheCleanup.Instance && EBG_CacheCleanup.Instance.VetoInsertion(entity, adjusted);
#ifdef EBG_ACCEPTANCE_TEST
		EBG_CleanupGroundTimerProof.ObserveNativeHook(entity, "insert callback", adjusted, veto);
#endif
		if (veto) { return -1; }
		return adjusted;
	}
	override protected bool OnBeforeDelete(IEntity entity)
	{
		bool protectedItem = EBG_CacheCleanup.Instance && EBG_CacheCleanup.Instance.VetoDeletion(entity);
#ifdef EBG_ACCEPTANCE_TEST
		EBG_CleanupGroundTimerProof.ObserveNativeHook(entity, "delete barrier", -1, protectedItem);
#endif
		if (protectedItem) { return false; }
		return super.OnBeforeDelete(entity);
	}
}
