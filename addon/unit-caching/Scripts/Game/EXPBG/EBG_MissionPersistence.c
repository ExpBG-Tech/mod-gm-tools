// Versioned optimizer metadata carried by the native mission save. Native entities
// and inventories remain owned by native persistence; no Full snapshot is stored.
// Only exclusion identities are retained for players; no physical player state.
class EBG_MissionPlayerHistory
{
 protected static World s_World;
 protected static ref array<IEntity> s_Observed;
 protected static ref array<UUID> s_Ids;
 protected static void Ensure()
 {
  if (!GetGame()) return;
  if (s_World == GetGame().GetWorld() && s_Observed && s_Ids) return;
  s_World = GetGame().GetWorld(); s_Observed = {}; s_Ids = {};
 }
 static void Mark(IEntity entity)
 {
  if (!entity || !Replication.IsServer()) return;
  Ensure();
  if (!s_Observed.Contains(entity)) s_Observed.Insert(entity);
  PersistenceSystem system = PersistenceSystem.GetInstance();
  if (!system) return;
  UUID id = system.GetId(entity);
  if (!id.IsNull() && !s_Ids.Contains(id)) s_Ids.Insert(id);
 }
 static bool Contains(IEntity entity)
 {
  if (!entity || !Replication.IsServer()) return false;
  Ensure();
  if (s_Observed.Contains(entity)) return true;
  PersistenceSystem system = PersistenceSystem.GetInstance();
  if (!system) return false;
  UUID id = system.GetId(entity);
  if (id.IsNull()) return false;
  if (s_Ids.Contains(id)) return true;
  EBG_MissionPersistenceState state = EBG_MissionPersistenceState.Get();
  if (!state) return false;
  state.Ensure();
  return state.EverPlayerIds.Contains(id);
 }
 static bool Export(array<UUID> ids)
 {
  Ensure();
  PersistenceSystem system = PersistenceSystem.GetInstance();
  if (!system) return false;
  for (int i = s_Observed.Count() - 1; i >= 0; i--)
  {
   IEntity entity = s_Observed[i];
   if (!entity) { s_Observed.Remove(i); continue; }
   UUID id = system.GetId(entity);
   if (id.IsNull()) return false;
   if (!s_Ids.Contains(id)) s_Ids.Insert(id);
  }
  foreach (UUID observed : s_Ids) if (!ids.Contains(observed)) ids.Insert(observed);
  return ids.Count() <= 65536;
 }
}

class EBG_MissionPersistence
{
 // Only explicit native GM creation stacks authorize a new entity, never load timing.
 static int GMSpawnDepth;
 static bool GMSpawnInProgress() { return Replication.IsServer() && GMSpawnDepth > 0; }
 static bool MayEnroll(SCR_ChimeraCharacter character)
 {
  if (!character || character.EBG_WasPlayerControlled()) return false;
  PersistenceSystem system = PersistenceSystem.GetInstance();
  // Native load does not confer player ownership. Retain recorded exclusions
  // while allowing an otherwise eligible original AI to join a new zone.
  return !system || !HasUnresolvedRelease(system.GetId(character));
 }
 // Validate native membership before committing any loaded scalar/ownership state.
 // Original player-history rows remain exclusion-only and need not be AI agents.
 static bool NativeRosterMatches(EBG_MissionGroupData saved, EBG_CacheGroup record, out string reason)
 {
  PersistenceSystem system = PersistenceSystem.GetInstance();
  if (!system || !saved || !record || record.Members.Count() != saved.Members.Count()) { reason = "Original native roster metadata unavailable"; return false; }
  if (saved.GroupPresent && (!record.Group || system.GetId(record.Group) != saved.GroupId)) { reason = "Original native group UUID unavailable"; return false; }
  array<UUID> actualIds = {};
  if (record.Group)
  {
   array<AIAgent> agents = {}; record.Group.GetAgents(agents);
   foreach (AIAgent agent : agents)
   {
    SCR_ChimeraCharacter character;
    if (agent) character = SCR_ChimeraCharacter.Cast(agent.GetControlledEntity());
    if (!character || agent.GetParentGroup() != record.Group) { reason = "Native group agent or parent association differs"; return false; }
    UUID id = system.GetId(character);
    int index = -1;
    for (int i = 0; i < saved.Members.Count(); i++) if (saved.Members[i].Id == id) index = i;
    if (id.IsNull() || actualIds.Contains(id) || index < 0 || record.Members[index].Entity != character) { reason = "Native group contains an extra, duplicate or mismatched agent"; return false; }
    if (!character.GetCharacterController() || character.GetCharacterController().IsDead() || saved.Members[index].Dead || saved.Members[index].Missing) { reason = "Native group still contains a dead or missing original agent"; return false; }
    actualIds.Insert(id);
   }
  }
  for (int memberIndex = 0; memberIndex < saved.Members.Count(); memberIndex++)
  {
   EBG_MissionMemberData original = saved.Members[memberIndex];
   EBG_CacheMember member = record.Members[memberIndex];
   if (original.Dead || original.Missing || original.WasPlayer || member.WasPlayer || (member.Entity && member.Entity.EBG_WasPlayerControlled())) continue;
   if (!member.Entity || !actualIds.Contains(original.Id)) { reason = "Native group is missing an original living AI agent"; return false; }
   AIAgent actual = SCR_AIUtils.GetAIAgent(member.Entity);
   if (!actual || actual.GetControlledEntity() != member.Entity || actual.GetParentGroup() != record.Group) { reason = "Original living AI agent is detached or belongs to another group"; return false; }
  }
  return true;
 }
 static bool PreserveOriginalGroupDuringSetup(SCR_AIGroup group)
 {
  if (!Replication.IsServer() || !group || group.IsPlayable() || group.GetPlayerCount() > 0 || group.IsSlave() || group.GetMaster() || group.IsCreatedByCommander()) return false;
  PersistenceSystem system = PersistenceSystem.GetInstance();
  if (!system || system.GetState() != EPersistenceSystemState.SETUP || !system.WasDataLoaded()) return false;
  EBG_MissionPersistenceState state = EBG_MissionPersistenceState.Get();
  if (!state || state.LoadedVersion != 2 || state.Bound || !state.Groups || !state.EverPlayerIds) return false;
  UUID id = system.GetId(group);
  if (id.IsNull()) return false;
  foreach (EBG_MissionGroupData saved : state.Groups)
  {
   if (saved.GroupId != id || !saved.GroupPresent) continue;
   if (!saved.Members || saved.Members.IsEmpty()) return false;
   foreach (EBG_MissionMemberData member : saved.Members)
    if (member.WasPlayer || state.EverPlayerIds.Contains(member.Id)) return false;
   return true;
  }
  return false;
 }
 static bool Finite(float value) { return value == value && value > -1000000000 && value < 1000000000; }
 static bool ValidIds(array<UUID> ids)
 {
  if (!ids || ids.Count() > 65536) return false;
  array<UUID> seen = {};
  foreach (UUID id : ids)
  {
   if (id.IsNull() || seen.Contains(id)) return false;
   seen.Insert(id);
  }
  return true;
 }
 static bool HasUnresolvedRelease(UUID id)
 {
  EBG_MissionPersistenceState state = EBG_MissionPersistenceState.Get();
  if (!state) return false;
  state.Ensure();
  return !id.IsNull() && state.ReleasedLineageMembers.Contains(id);
 }
 static bool Reserves(UUID id)
 {
  if (id.IsNull()) return false;
  EBG_MissionPersistenceState state = EBG_MissionPersistenceState.Get();
  if (!state || state.LoadedVersion < 2) return false;
  state.Ensure();
  foreach (EBG_MissionGroupData group : state.Groups) if (group.GroupId == id) return true;
  return false;
 }
 static bool NativeSavingUnused(PersistenceSystem system)
 {
  if (!system || system.GetState() != EPersistenceSystemState.INIT || system.WasDataLoaded() || !EBG_CacheManager.IsPortableWorldReady()) return false;
  SaveGameManager saving = GetGame().GetSaveGameManager();
  return saving && !saving.IsSavingEnabled() && !saving.GetActiveSave();
 }
 static bool Ready(EBG_CacheManager manager)
 {
  PersistenceSystem system = PersistenceSystem.GetInstance();
  if (!system || NativeSavingUnused(system)) return true;
  if (system.GetState() != EPersistenceSystemState.ACTIVE) return false;
  EBG_MissionPersistenceState state = EBG_MissionPersistenceState.Get();
  if (!system.WasDataLoaded()) return true;
  if (!state) return true;
  state.Ensure();
  if (state.Bound) return true;
  if (state.LoadedVersion < 2)
  {
   if (!state.LegacyReported)
   {
    state.LegacyReported = true;
    Print("[EBG MISSION LOAD] Legacy save lacks original ownership/player history; eligible loaded AI may enroll. Current and recorded former players remain excluded; earlier possession absent from the save cannot be reconstructed. Cleanup still requires verified current ownership.", LogLevel.WARNING);
   }
   return true;
  }
  if (!manager) return false;
  float now = manager.Now();
  if (!state.BindingStarted) { state.BindingStarted = true; state.BindingTime = now; }
  EBG_CacheCleanup cleanup = EBG_CacheCleanup.Get();
  cleanup.ImportPersistentNegatives(state.ReleasedIds);
  int pending;
  foreach (EBG_MissionGroupData group : state.Groups)
  {
   if (group.Imported) continue;
   EBG_CacheGroup record = manager.PreparePersistentGroup(group);
   string reason = record.PersistenceIssue;
   if (reason == "" && group.Issue == "" && cleanup.ImportPersistentGroup(group, record, reason))
   {
    record.PersistenceIssue = ""; group.Imported = true;
    if (record.Zone && record.Zone.DebugMessages > 0)
     PrintFormat("[EBG MISSION LOAD] group=%1 members=%2 objects=%3 exactOriginalLedger=1", group.GroupId, group.Members.Count(), group.Objects.Count());
   }
   else if (now - state.BindingTime >= 10 || group.Issue != "" || record.PersistentScalarFailure)
   {
    if (group.Issue != "") reason = group.Issue;
    if (reason == "") reason = "Unresolved original metadata";
    record.PersistenceIssue = reason; group.Imported = true;
    manager.LogPersistentResolution(group, "bind-blocked");
    PrintFormat("[EBG MISSION LOAD] group=%1 retained metadata; group blocked: %2", group.GroupId, reason);
   }
   else pending++;
  }
  if (pending > 0) return false;
  state.Bound = true;
  return true;
 }
 static bool Export(EBG_MissionPersistenceState state, out string reason)
 {
  if (SCR_AIGroupSerializer.EBG_HasPendingMemberCallbacks()) { reason = "Native group callbacks remain pending; use Restore ALL after original ownership is valid"; return false; }
  reason = ""; state.Ensure();
  PersistenceSystem system = PersistenceSystem.GetInstance();
  if (system && system.WasDataLoaded() && state.LoadedVersion >= 2 && !state.Bound) { reason = "Native metadata binding is incomplete"; return false; }
  EBG_CacheManager manager = EBG_CacheManager.Instance;
  if (manager && EBG_FullSaveGate.HasRecoveryState(manager)) { reason = "Restore ALL cache/recovery state before saving"; return false; }
  foreach (EBG_CacheZone zone : EBG_CacheZone.Zones)
   if (zone && zone.Enabled && !zone.Editing) { reason = "Restore ALL zones for editing before saving"; return false; }
  if (!EBG_MissionPlayerHistory.Export(state.EverPlayerIds)) { reason = "Player exclusion identity unavailable or excessive"; return false; }
  EBG_CacheCleanup cleanup = EBG_CacheCleanup.Instance;
  if (cleanup) cleanup.CheckTransfers();
  if (cleanup && !cleanup.ExportPersistentNegatives(state.ReleasedIds)) { reason = "Permanent release identity export failed"; return false; }
  array<ref EBG_MissionGroupData> captured = {};
  if (manager)
   foreach (EBG_CacheGroup record : manager.Records)
   {
    if (record.PersistentScalarRollbackPending) { reason = "Native scalar rollback remains pending"; return false; }
    EBG_MissionGroupData group;
    if (record.PersistentData && (record.PersistenceIssue != "" || !record.PersistentData.Imported)) group = record.PersistentData;
    else
    {
     group = manager.ExportPersistentGroup(record, reason);
     if (!group) return false;
     if (cleanup && !cleanup.ExportPersistentGroup(group, record, reason)) return false;
    }
    captured.Insert(group);
   }
  // Unresolved/orphan originals must not disappear when a later save is made.
  foreach (EBG_MissionGroupData previous : state.Groups)
  {
   bool found;
   foreach (EBG_MissionGroupData current : captured) if (current.GroupId == previous.GroupId) found = true;
   if (!found && !previous.Imported) captured.Insert(previous);
  }
  int objects;
  if (captured.Count() > 256) { reason = "More than 256 managed groups"; return false; }
  foreach (EBG_MissionGroupData bounded : captured) objects += bounded.Objects.Count();
  if (objects > 65536) { reason = "More than 65536 original objects"; return false; }
  state.Groups = captured;
  return true;
 }
}

// Retained only if a native setter fails. This is rollback state, never ownership.
class EBG_MissionScalarBackup
{
 IEntity Entity;
 IEntity OriginalHolder;
 IEntity OriginalParent;
 UUID ResolvedNativeId; // Current validated instance, distinct from saved provenance.
 SCR_ChimeraCharacter Member;
 SCR_WeaponAttachmentsStorageComponent Rails;
 SCR_BayonetComponent Blade;
 ref array<float> PreviousRails = {};
 ref EBG_FullIdentityMaterials PreviousSurface;
 ref EBG_FullIdentityMaterials TargetSurface;
 ref EBG_MissionObjectData Target;
 float PreviousBlood;
 bool Attempted;
 bool Preflight(EBG_CleanupObject object, EBG_MissionObjectData row)
 {
  Entity = object.Entity; OriginalParent = Entity.GetParent(); Member = object.Member.Entity; Target = row;
  if (row.RailsPresent)
  {
   Rails = SCR_WeaponAttachmentsStorageComponent.Cast(Entity.FindComponent(SCR_WeaponAttachmentsStorageComponent));
   if (!Rails || !Rails.EBG_PreflightRHSRails(row.Rails, PreviousRails)) return false;
  }
  if (row.BayonetPresent)
  {
   Blade = SCR_BayonetComponent.Cast(Entity.FindComponent(SCR_BayonetComponent));
   if (!Blade || !Blade.EBG_PreflightMissionBlood(row.Blood) || !row.MaterialValues || row.MaterialValues.Count() != 10) return false;
   PreviousBlood = Blade.EBG_GetBloodStainLevel();
   if (!Blade.EBG_PreflightMissionBlood(PreviousBlood)) return false;
   PreviousSurface = new EBG_FullIdentityMaterials(); PreviousSurface.Capture(Entity);
   if (!PreviousSurface.Present || !PreviousSurface.Values || PreviousSurface.Values.Count() != 10) return false;
   TargetSurface = new EBG_FullIdentityMaterials(); TargetSurface.Present = true;
   TargetSurface.Values = row.MaterialValues; TargetSurface.Wetness = row.Wetness; TargetSurface.Drops = row.Drops;
   if (row.Map.NativeScabbardSlot && (!Member || Member.EBG_FindClothBlade() != Entity)) return false;
  }
  return true;
 }
 bool Apply()
 {
  Attempted = true;
  if (Rails && !Rails.EBG_ApplyMissionRails(Target.Rails)) return false;
  if (Blade && !Blade.EBG_ApplyFullState(Target.Blood, TargetSurface.Values, TargetSurface.Wetness, TargetSurface.Drops)) return false;
  return Matches();
 }
 bool Matches()
 {
  if (!Entity) return false;
  if (Rails && !Rails.EBG_RHSRailsMatch(Target.Rails)) return false;
  if (Blade && (Blade.EBG_GetBloodStainLevel() != Target.Blood || !TargetSurface.Matches(Entity))) return false;
  return !Blade || !Target.Map.NativeScabbardSlot || (Member && Member.EBG_FindClothBlade() == Entity);
 }
 bool Rollback()
 {
  if (!Attempted) return true;
  if (!Entity) return false;
  bool restored = true;
  if (Blade && !Blade.EBG_ApplyFullState(PreviousBlood, PreviousSurface.Values, PreviousSurface.Wetness, PreviousSurface.Drops)) restored = false;
  if (Rails && !Rails.EBG_ApplyMissionRails(PreviousRails)) restored = false;
  if (restored) Attempted = false;
  return restored;
 }
 void Publish()
 {
  if (Rails) Rails.EBG_PublishMissionRails();
  if (Blade)
  {
   Blade.EBG_PublishMissionBlood(Target.Blood, TargetSurface);
   if (Target.Map.NativeScabbardSlot) Member.EBG_PublishClothBlade(Entity, Target.Blood, TargetSurface);
  }
 }
}

// Existing v2 proves the head prefab only, not durable name/body/voice identity.
class EBG_MissionHeadBackup : EBG_MissionScalarBackup
{
 ref Identity IdentityValue;
 ref VisualIdentity Visual;
 CharacterIdentityComponent Component;
 ResourceName PreviousHead;
 float AppliedAt;
 bool RollbackStarted;
 bool CaptureHead(EBG_CleanupObject object, EBG_MissionObjectData row)
 {
  Member = object.Member.Entity; Entity = Member; Target = row;
  if (!Member || Member.EBG_WasPlayerControlled()) return false;
  Component = CharacterIdentityComponent.Cast(Member.FindComponent(CharacterIdentityComponent));
  if (!Component) return false;
  IdentityValue = Component.GetIdentity();
  if (!IdentityValue) return false;
  Visual = IdentityValue.GetVisualIdentity();
  if (!Visual) return false;
  PreviousHead = Visual.GetHead();
  return Safe() && NativeHeadMatches(PreviousHead);
 }
 bool NativeHeadMatches(ResourceName prefab)
 {
  IEntity head = Component.GetHeadEntity();
  if (!head || head.GetParent() != Member || head.GetChildren() || SCR_ResourceNameUtils.GetPrefabName(head) != prefab) return false;
  array<Managed> components = {}; head.FindComponents(GenericComponent, components);
  array<string> types = {}; foreach (Managed component : components) types.Insert(component.Type().ToString());
  types.Sort(); string shape = head.Type().ToString(); foreach (string type : types) shape += "|" + type;
  if (shape != Target.Map.ShapeSignature) return false;
  int heads;
  for (IEntity child = Member.GetChildren(); child; child = child.GetSibling())
   if (SCR_ResourceNameUtils.GetPrefabName(child).Contains("Prefabs/Characters/Heads/")) heads++;
  return heads == 1;
 }
 bool Safe()
 {
  PersistenceSystem system = PersistenceSystem.GetInstance();
  if (!system || !Member || Member.EBG_WasPlayerControlled() || EBG_MissionPlayerHistory.Contains(Member) || system.FindById(Target.Map.MemberId) != Member) return false;
  if (!Member.GetCharacterController() || Member.GetCharacterController().IsDead()) return false;
  if (!Component || Member.FindComponent(CharacterIdentityComponent) != Component || Component.GetIdentity() != IdentityValue || IdentityValue.GetVisualIdentity() != Visual) return false;
  IEntity currentHead = Component.GetHeadEntity();
  if (currentHead && (currentHead.GetParent() != Member || currentHead.GetChildren())) return false;
  ResourceName head = Visual.GetHead();
  return head == PreviousHead || head == Target.Map.Prefab;
 }
 override bool Apply()
 {
  if (!Safe()) return false;
  if (!Attempted)
  {
   Attempted = true; AppliedAt = GetGame().GetWorld().GetWorldTime() * 0.001;
   Visual.SetHead(Target.Map.Prefab); Component.CommitChanges();
#ifdef EBG_ACCEPTANCE_TEST
   PrintFormat("[EBG MISSION HEAD] member=%1 previous='%2' original='%3' nativeCommit=1", Target.Map.MemberId, PreviousHead, Target.Map.Prefab);
#endif
  }
  return true;
 }
 override bool Matches() { return Safe() && Visual.GetHead() == Target.Map.Prefab && NativeHeadMatches(Target.Map.Prefab); }
 override bool Rollback()
 {
  if (!Attempted) return true;
  if (!Safe()) return false;
  if (!RollbackStarted) { Visual.SetHead(PreviousHead); Component.CommitChanges(); RollbackStarted = true; }
  if (!NativeHeadMatches(PreviousHead)) return false;
  Attempted = false; return true;
 }
 override void Publish() {}
}

class EBG_MissionMemberData
{
 UUID Id;
 vector Position;
 bool Dead;
 bool Missing;
 bool WasPlayer;
 float DeathAge;
 bool Write(SaveContext context)
 {
  if (!context.WriteValue("Id", Id)) return false;
  if (!context.WriteValue("Position", Position)) return false;
  if (!context.WriteValue("Dead", Dead)) return false;
  if (!context.WriteValue("Missing", Missing)) return false;
  if (!context.WriteValue("WasPlayer", WasPlayer)) return false;
  if (!context.WriteValue("DeathAge", DeathAge)) return false;
  return true;
 }
 bool Read(LoadContext context)
 {
  if (!context.ReadValue("Id", Id)) return false;
  if (!context.ReadValue("Position", Position)) return false;
  if (!context.ReadValue("Dead", Dead)) return false;
  if (!context.ReadValue("Missing", Missing)) return false;
  if (!context.ReadValue("WasPlayer", WasPlayer)) return false;
  if (!context.ReadValue("DeathAge", DeathAge)) return false;
  return true;
 }
 bool Valid()
 {
  return !Id.IsNull() && EBG_MissionPersistence.Finite(DeathAge) && DeathAge >= 0 && EBG_MissionPersistence.Finite(Position[0]) && EBG_MissionPersistence.Finite(Position[1]) && EBG_MissionPersistence.Finite(Position[2]);
 }
}
class EBG_MissionObjectData
{
 ref EBG_CleanupFullEntry Map = new EBG_CleanupFullEntry();
 ref array<float> Rails = {};
 ref array<int> MaterialValues = {};
 int MemberIndex;
 bool Present;
 bool Corpse;
 bool NativeBelongings;
 bool Allowed;
 bool Held;
 bool Released;
 bool DeathConfirmed;
 float DeathAge;
 bool NativeRequested;
 float NativeRemaining;
 bool RailsPresent;
 float Blood;
 bool BayonetPresent;
 bool Wetness;
 bool Drops;
 bool Write(SaveContext context)
 {
  if (!context.WriteValue("MemberIndex", MemberIndex)) return false;
  if (!context.WriteValue("Present", Present)) return false;
  if (!context.WriteValue("Corpse", Corpse)) return false;
  if (!context.WriteValue("NativeBelongings", NativeBelongings)) return false;
  if (!context.WriteValue("Allowed", Allowed)) return false;
  if (!context.WriteValue("Held", Held)) return false;
  if (!context.WriteValue("Released", Released)) return false;
  if (!context.WriteValue("DeathConfirmed", DeathConfirmed)) return false;
  if (!context.WriteValue("DeathAge", DeathAge)) return false;
  if (!context.WriteValue("NativeRequested", NativeRequested)) return false;
  if (!context.WriteValue("NativeRemaining", NativeRemaining)) return false;
  if (!context.WriteValue("RailsPresent", RailsPresent)) return false;
  if (!context.WriteValue("Blood", Blood)) return false;
  if (!context.WriteValue("BayonetPresent", BayonetPresent)) return false;
  if (!context.WriteValue("Wetness", Wetness)) return false;
  if (!context.WriteValue("Drops", Drops)) return false;
  if (!context.WriteValue("mNativeId", Map.NativeId)) return false;
  if (!context.WriteValue("mMemberId", Map.MemberId)) return false;
  if (!context.WriteValue("mParentId", Map.ParentId)) return false;
  if (!context.WriteValue("mParentEntry", Map.ParentEntry)) return false;
  if (!context.WriteValue("mPrefab", Map.Prefab)) return false;
  if (!context.WriteValue("mShapeSignature", Map.ShapeSignature)) return false;
  if (!context.WriteValue("mWasHeld", Map.WasHeld)) return false;
  if (!context.WriteValue("mLoadedSlotKind", Map.LoadedSlotKind)) return false;
  if (!context.WriteValue("mMuzzleIndex", Map.MuzzleIndex)) return false;
  if (!context.WriteValue("mDirectParentMuzzles", Map.DirectParentMuzzles)) return false;
  if (!context.WriteValue("mBarrelIndex", Map.BarrelIndex)) return false;
  if (!context.WriteValue("mLoadedAmmo", Map.LoadedAmmo)) return false;
  if (!context.WriteValue("mLoadedMaxAmmo", Map.LoadedMaxAmmo)) return false;
  if (!context.WriteValue("mMuzzleType", Map.MuzzleType)) return false;
  if (!context.WriteValue("mParentPrefab", Map.ParentPrefab)) return false;
  if (!context.WriteValue("mLoadedMagazine", Map.LoadedMagazine)) return false;
  if (!context.WriteValue("mMagazineType", Map.MagazineType)) return false;
  if (!context.WriteValue("mMagazineAmmo", Map.MagazineAmmo)) return false;
  if (!context.WriteValue("mMagazineMaxAmmo", Map.MagazineMaxAmmo)) return false;
  if (!context.WriteValue("mMagazineAmmoType", Map.MagazineAmmoType)) return false;
  if (!context.WriteValue("mWeaponInventorySlot", Map.WeaponInventorySlot)) return false;
  if (!context.WriteValue("mStoredInventorySlot", Map.StoredInventorySlot)) return false;
  if (!context.WriteValue("mInventorySlotTemplate", Map.InventorySlotTemplate)) return false;
  if (!context.WriteValue("mCapturedMagazine", Map.CapturedMagazine)) return false;
  if (!context.WriteValue("mPhysicalParentId", Map.PhysicalParentId)) return false;
  if (!context.WriteValue("mStorageType", Map.StorageType)) return false;
  if (!context.WriteValue("mInventorySlotType", Map.InventorySlotType)) return false;
  if (!context.WriteValue("mInventorySlotName", Map.InventorySlotName)) return false;
  if (!context.WriteValue("mInventorySlotId", Map.InventorySlotId)) return false;
  if (!context.WriteValue("mInventorySlotCount", Map.InventorySlotCount)) return false;
  if (!context.WriteValue("mGeneratedDummyBelt", Map.GeneratedDummyBelt)) return false;
  if (!context.WriteValue("mGeneratedWeaponSight", Map.GeneratedWeaponSight)) return false;
  if (!context.WriteValue("mNativeClothSlot", Map.NativeClothSlot)) return false;
  if (!context.WriteValue("mNamedEntitySlot", Map.NamedEntitySlot)) return false;
  if (!context.WriteValue("mNativeScabbardSlot", Map.NativeScabbardSlot)) return false;
  if (!context.WriteValue("mSourceJacketId", Map.SourceJacketId)) return false;
  if (!context.WriteValue("mSourceJacketPrefab", Map.SourceJacketPrefab)) return false;
  return context.WriteValue("Rails", Rails) && context.WriteValue("Material", MaterialValues);
 }
 bool Read(LoadContext context)
 {
  if (!context.ReadValue("MemberIndex", MemberIndex)) return false;
  if (!context.ReadValue("Present", Present)) return false;
  if (!context.ReadValue("Corpse", Corpse)) return false;
  if (!context.ReadValue("NativeBelongings", NativeBelongings)) return false;
  if (!context.ReadValue("Allowed", Allowed)) return false;
  if (!context.ReadValue("Held", Held)) return false;
  if (!context.ReadValue("Released", Released)) return false;
  if (!context.ReadValue("DeathConfirmed", DeathConfirmed)) return false;
  if (!context.ReadValue("DeathAge", DeathAge)) return false;
  if (!context.ReadValue("NativeRequested", NativeRequested)) return false;
  if (!context.ReadValue("NativeRemaining", NativeRemaining)) return false;
  if (!context.ReadValue("RailsPresent", RailsPresent)) return false;
  if (!context.ReadValue("Blood", Blood)) return false;
  if (!context.ReadValue("BayonetPresent", BayonetPresent)) return false;
  if (!context.ReadValue("Wetness", Wetness)) return false;
  if (!context.ReadValue("Drops", Drops)) return false;
  if (!context.ReadValue("mNativeId", Map.NativeId)) return false;
  if (!context.ReadValue("mMemberId", Map.MemberId)) return false;
  if (!context.ReadValue("mParentId", Map.ParentId)) return false;
  if (!context.ReadValue("mParentEntry", Map.ParentEntry)) return false;
  if (!context.ReadValue("mPrefab", Map.Prefab)) return false;
  if (!context.ReadValue("mShapeSignature", Map.ShapeSignature)) return false;
  if (!context.ReadValue("mWasHeld", Map.WasHeld)) return false;
  if (!context.ReadValue("mLoadedSlotKind", Map.LoadedSlotKind)) return false;
  if (!context.ReadValue("mMuzzleIndex", Map.MuzzleIndex)) return false;
  if (!context.ReadValue("mDirectParentMuzzles", Map.DirectParentMuzzles)) return false;
  if (!context.ReadValue("mBarrelIndex", Map.BarrelIndex)) return false;
  if (!context.ReadValue("mLoadedAmmo", Map.LoadedAmmo)) return false;
  if (!context.ReadValue("mLoadedMaxAmmo", Map.LoadedMaxAmmo)) return false;
  if (!context.ReadValue("mMuzzleType", Map.MuzzleType)) return false;
  if (!context.ReadValue("mParentPrefab", Map.ParentPrefab)) return false;
  if (!context.ReadValue("mLoadedMagazine", Map.LoadedMagazine)) return false;
  if (!context.ReadValue("mMagazineType", Map.MagazineType)) return false;
  if (!context.ReadValue("mMagazineAmmo", Map.MagazineAmmo)) return false;
  if (!context.ReadValue("mMagazineMaxAmmo", Map.MagazineMaxAmmo)) return false;
  if (!context.ReadValue("mMagazineAmmoType", Map.MagazineAmmoType)) return false;
  if (!context.ReadValue("mWeaponInventorySlot", Map.WeaponInventorySlot)) return false;
  if (!context.ReadValue("mStoredInventorySlot", Map.StoredInventorySlot)) return false;
  if (!context.ReadValue("mInventorySlotTemplate", Map.InventorySlotTemplate)) return false;
  if (!context.ReadValue("mCapturedMagazine", Map.CapturedMagazine)) return false;
  if (!context.ReadValue("mPhysicalParentId", Map.PhysicalParentId)) return false;
  if (!context.ReadValue("mStorageType", Map.StorageType)) return false;
  if (!context.ReadValue("mInventorySlotType", Map.InventorySlotType)) return false;
  if (!context.ReadValue("mInventorySlotName", Map.InventorySlotName)) return false;
  if (!context.ReadValue("mInventorySlotId", Map.InventorySlotId)) return false;
  if (!context.ReadValue("mInventorySlotCount", Map.InventorySlotCount)) return false;
  if (!context.ReadValue("mGeneratedDummyBelt", Map.GeneratedDummyBelt)) return false;
  if (!context.ReadValue("mGeneratedWeaponSight", Map.GeneratedWeaponSight)) return false;
  if (!context.ReadValue("mNativeClothSlot", Map.NativeClothSlot)) return false;
  if (!context.ReadValue("mNamedEntitySlot", Map.NamedEntitySlot)) return false;
  if (!context.ReadValue("mNativeScabbardSlot", Map.NativeScabbardSlot)) return false;
  if (!context.ReadValue("mSourceJacketId", Map.SourceJacketId)) return false;
  if (!context.ReadValue("mSourceJacketPrefab", Map.SourceJacketPrefab)) return false;
  return context.ReadValue("Rails", Rails) && context.ReadValue("Material", MaterialValues);
 }
#ifdef EBG_ACCEPTANCE_TEST
 bool SameMapping(EBG_CleanupFullEntry other)
 {
  if (!other) return false;
  if (Map.NativeId != other.NativeId) return false;
  if (Map.MemberId != other.MemberId) return false;
  if (Map.ParentId != other.ParentId) return false;
  if (Map.ParentEntry != other.ParentEntry) return false;
  if (Map.Prefab != other.Prefab) return false;
  if (Map.ShapeSignature != other.ShapeSignature) return false;
  if (Map.LoadedSlotKind != other.LoadedSlotKind) return false;
  if (Map.MuzzleIndex != other.MuzzleIndex) return false;
  if (Map.DirectParentMuzzles != other.DirectParentMuzzles) return false;
  if (Map.BarrelIndex != other.BarrelIndex) return false;
  if (Map.LoadedAmmo != other.LoadedAmmo) return false;
  if (Map.LoadedMaxAmmo != other.LoadedMaxAmmo) return false;
  if (Map.MuzzleType != other.MuzzleType) return false;
  if (Map.ParentPrefab != other.ParentPrefab) return false;
  if (Map.LoadedMagazine != other.LoadedMagazine) return false;
  if (Map.MagazineType != other.MagazineType) return false;
  if (Map.MagazineAmmo != other.MagazineAmmo) return false;
  if (Map.MagazineMaxAmmo != other.MagazineMaxAmmo) return false;
  if (Map.MagazineAmmoType != other.MagazineAmmoType) return false;
  if (Map.WeaponInventorySlot != other.WeaponInventorySlot) return false;
  if (Map.StoredInventorySlot != other.StoredInventorySlot) return false;
  if (Map.InventorySlotTemplate != other.InventorySlotTemplate) return false;
  if (Map.CapturedMagazine != other.CapturedMagazine) return false;
  if (Map.PhysicalParentId != other.PhysicalParentId) return false;
  if (Map.StorageType != other.StorageType) return false;
  if (Map.InventorySlotType != other.InventorySlotType) return false;
  if (Map.InventorySlotName != other.InventorySlotName) return false;
  if (Map.InventorySlotId != other.InventorySlotId) return false;
  if (Map.InventorySlotCount != other.InventorySlotCount) return false;
  if (Map.GeneratedDummyBelt != other.GeneratedDummyBelt) return false;
  if (Map.GeneratedWeaponSight != other.GeneratedWeaponSight) return false;
  if (Map.NativeClothSlot != other.NativeClothSlot) return false;
  if (Map.NamedEntitySlot != other.NamedEntitySlot) return false;
  if (Map.NativeScabbardSlot != other.NativeScabbardSlot) return false;
  if (Map.SourceJacketId != other.SourceJacketId) return false;
  if (Map.SourceJacketPrefab != other.SourceJacketPrefab) return false;
  return true;
 }
#endif
 bool Valid(int members, int objects)
 {
  if (!Map || MemberIndex < 0 || MemberIndex >= members || Map.MemberId.IsNull() || Map.ParentEntry < -1 || Map.ParentEntry >= objects || Map.Prefab.Length() > 1024 || Map.ShapeSignature.Length() > 4096) return false;
  if (!EBG_MissionPersistence.Finite(DeathAge) || DeathAge < 0 || !EBG_MissionPersistence.Finite(NativeRemaining) || !EBG_MissionPersistence.Finite(Blood) || Blood < 0 || Blood > 255) return false;
  if (Released && Held) return false;
  if (Rails.Count() > 128 || MaterialValues.Count() > 10 || (BayonetPresent && MaterialValues.Count() != 10) || (RailsPresent && Rails.IsEmpty())) return false;
  foreach (float value : Rails) if (!EBG_MissionPersistence.Finite(value) || (value != 10000 && (value < -1 || value > 1))) return false;
  return Map.LoadedSlotKind >= 0 && Map.LoadedSlotKind <= 2 && Map.InventorySlotCount >= 0 && Map.InventorySlotCount <= 512 && Map.InventorySlotId >= -1 && Map.InventorySlotId < 512 && Map.MuzzleIndex >= -1 && Map.MuzzleIndex < 8 && Map.BarrelIndex >= -1 && Map.BarrelIndex < 8;
 }
}
class EBG_MissionGroupData
{
 UUID ZoneId;
 UUID GroupId;
 bool GroupPresent;
 bool LedgerInitialized;
 string Issue;
 string CallbackIssue; // Session-only ownership of a recoverable native callback error.
 ref array<ref EBG_MissionMemberData> Members = {};
 ref array<ref EBG_MissionObjectData> Objects = {};
 bool Imported;
#ifdef EBG_ACCEPTANCE_TEST
 bool MappingFailureReported;
#endif
 bool Write(SaveContext context)
 {
  if (!context.WriteValue("zone", ZoneId) || !context.WriteValue("group", GroupId) || !context.WriteValue("groupPresent", GroupPresent) || !context.WriteValue("ledger", LedgerInitialized) || !context.WriteValue("issue", Issue) || !context.WriteValue("members", Members.Count()) || !context.WriteValue("objects", Objects.Count())) return false;
  for (int i = 0; i < Members.Count(); i++)
   if (!context.StartObject("member" + i.ToString()) || !Members[i].Write(context) || !context.EndObject()) return false;
  for (int j = 0; j < Objects.Count(); j++)
   if (!context.StartObject("object" + j.ToString()) || !Objects[j].Write(context) || !context.EndObject()) return false;
  return true;
 }
 bool Read(LoadContext context)
 {
  int members, objects;
  if (!context.ReadValue("zone", ZoneId) || !context.ReadValue("group", GroupId) || !context.ReadValue("groupPresent", GroupPresent) || !context.ReadValue("ledger", LedgerInitialized) || !context.ReadValue("issue", Issue) || !context.ReadValue("members", members) || !context.ReadValue("objects", objects) || ZoneId.IsNull() || GroupId.IsNull() || members < 1 || members > 128 || objects < 0 || objects > 2048 || Issue.Length() > 4096) return false;
  array<UUID> ids = {};
  for (int i = 0; i < members; i++)
  {
   EBG_MissionMemberData member = new EBG_MissionMemberData();
   if (!context.StartObject("member" + i.ToString()) || !member.Read(context) || !context.EndObject() || !member.Valid() || ids.Contains(member.Id)) return false;
   ids.Insert(member.Id); Members.Insert(member);
  }
  ids.Clear();
  for (int j = 0; j < objects; j++)
  {
   EBG_MissionObjectData object = new EBG_MissionObjectData();
   if (!context.StartObject("object" + j.ToString()) || !object.Read(context) || !context.EndObject() || !object.Valid(members, objects) || object.Map.ParentEntry == j || object.Map.MemberId != Members[object.MemberIndex].Id) return false;
   if (!object.Map.NativeId.IsNull())
   {
    if (ids.Contains(object.Map.NativeId)) return false;
    ids.Insert(object.Map.NativeId);
   }
   Objects.Insert(object);
  }
  return true;
 }
}

// One native world record. Arrays are initialized lazily because the native
// default deserialize phase can run before the state constructor.
[BaseContainerProps()]
class EBG_MissionPersistenceState : PersistentState
{
 int PilotValue;
 string PilotToken;
 ref array<UUID> EverPlayerIds;
 ref array<UUID> ReleasedIds;
 ref array<UUID> ReleasedLineageMembers;
 ref array<string> ReleasedOrigins;
 ref array<ref EBG_MissionGroupData> Groups;
 int LoadedVersion;
 bool Bound;
 bool BindingStarted;
 float BindingTime;
 bool LegacyReported;
 void Ensure()
 {
  if (!EverPlayerIds) EverPlayerIds = {};
  if (!ReleasedIds) ReleasedIds = {};
  if (!ReleasedLineageMembers) ReleasedLineageMembers = {};
  if (!ReleasedOrigins) ReleasedOrigins = {};
  if (!Groups) Groups = {};
 }
 [NonSerialized()]
 int SerializeCalls;
 [NonSerialized()]
 int DeserializeCalls;

 static EBG_MissionPersistenceState Get()
 {
  PersistenceSystem system = PersistenceSystem.GetInstance();
  if (!system) return null;
  return EBG_MissionPersistenceState.Cast(system.GetPersistentState(EBG_MissionPersistenceState));
 }
}

// Own script state only; no native entity/component serializer is overridden.
class EBG_MissionPersistenceSerializer : ScriptedStateSerializer
{
 override static typename GetTargetType() { return EBG_MissionPersistenceState; }
 override static EDeserializeFailHandling GetDeserializeFailHandling() { return EDeserializeFailHandling.ERROR; }

 override protected ESerializeResult Serialize(notnull Managed instance, notnull SaveContext context)
 {
  // Cover even the legacy branch if metadata changed while native tasks remain.
  if (SCR_AIGroupSerializer.EBG_HasPendingMemberCallbacks())
  {
   Print("[EBG MISSION SAVE] Refused pending native group callbacks; use Restore ALL after original ownership is valid", LogLevel.ERROR);
   return ESerializeResult.ERROR;
  }
  EBG_MissionPersistenceState state = EBG_MissionPersistenceState.Cast(instance);
  if (!state || state.PilotToken.Length() > 64) return ESerializeResult.ERROR;
  PersistenceSystem system = PersistenceSystem.GetInstance();
  // Saving an old mission must not turn unknown history into a trusted empty v2
  // ledger. Preserve its legacy marker without taking over native save permission.
  if (system && system.WasDataLoaded() && state.LoadedVersion < 2)
  {
   if (!context.WriteValue("ebgMissionVersion", 1) || !context.WriteValue("pilotValue", state.PilotValue) || !context.WriteValue("pilotToken", state.PilotToken)) return ESerializeResult.ERROR;
   state.SerializeCalls++;
   return ESerializeResult.OK;
  }
  string reason;
  if (!EBG_MissionPersistence.Export(state, reason))
  {
   Print("[EBG MISSION SAVE] Refused incomplete metadata: " + reason, LogLevel.ERROR);
   return ESerializeResult.ERROR;
  }
  if (!context.WriteValue("ebgMissionVersion", 2) || !context.WriteValue("pilotValue", state.PilotValue) || !context.WriteValue("pilotToken", state.PilotToken) || !context.WriteValue("everPlayers", state.EverPlayerIds) || !context.WriteValue("released", state.ReleasedIds) || !context.WriteValue("releasedLineageMembers", state.ReleasedLineageMembers) || !context.WriteValue("releasedOrigins", state.ReleasedOrigins) || !context.WriteValue("groupCount", state.Groups.Count())) return ESerializeResult.ERROR;
  for (int i = 0; i < state.Groups.Count(); i++)
   if (!context.StartObject("group" + i.ToString()) || !state.Groups[i].Write(context) || !context.EndObject()) return ESerializeResult.ERROR;
  state.SerializeCalls++;
#ifdef EBG_ACCEPTANCE_TEST
  PrintFormat("[EBG MISSION STATE] SERIALIZE version=2 value=%1 token='%2' calls=%3", state.PilotValue, state.PilotToken, state.SerializeCalls);
#endif
  return ESerializeResult.OK;
 }
 override protected bool Deserialize(notnull Managed instance, notnull LoadContext context)
 {
  EBG_MissionPersistenceState state = EBG_MissionPersistenceState.Cast(instance);
  int version, value;
  string token;
  if (!state || !context.ReadValue("ebgMissionVersion", version) || (version != 1 && version != 2) || !context.ReadValue("pilotValue", value) || !context.ReadValue("pilotToken", token) || token.Length() > 64) return false;
  state.Ensure();
  state.Groups.Clear(); state.EverPlayerIds.Clear(); state.ReleasedIds.Clear(); state.ReleasedLineageMembers.Clear(); state.ReleasedOrigins.Clear();
  state.LoadedVersion = version;
  state.Bound = false; state.BindingStarted = false;
  if (version == 2)
  {
   int count;
   if (!context.ReadValue("everPlayers", state.EverPlayerIds) || !context.ReadValue("released", state.ReleasedIds) || !context.ReadValue("releasedLineageMembers", state.ReleasedLineageMembers) || !context.ReadValue("releasedOrigins", state.ReleasedOrigins) || !context.ReadValue("groupCount", count) || count < 0 || count > 256 || !EBG_MissionPersistence.ValidIds(state.EverPlayerIds) || !EBG_MissionPersistence.ValidIds(state.ReleasedIds) || !EBG_MissionPersistence.ValidIds(state.ReleasedLineageMembers) || state.ReleasedOrigins.Count() > 65536) return false;
   foreach (string origin : state.ReleasedOrigins) if (origin.Length() > 32768) return false;
   array<UUID> groups = {}, members = {};
   int objects;
   for (int i = 0; i < count; i++)
   {
    EBG_MissionGroupData row = new EBG_MissionGroupData();
    if (!context.StartObject("group" + i.ToString()) || !row.Read(context) || !context.EndObject() || groups.Contains(row.GroupId)) return false;
    groups.Insert(row.GroupId); objects += row.Objects.Count();
    if (objects > 65536) return false;
    foreach (EBG_MissionMemberData member : row.Members)
    {
     if (members.Contains(member.Id)) return false;
     members.Insert(member.Id);
    }
    state.Groups.Insert(row);
   }
  }
  // No constructor/field initializer overwrites data at the native default phase.
  state.PilotValue = value; state.PilotToken = token;
  state.DeserializeCalls++;
  // Native metadata can load before any cache module exists. The existing
  // manager pump waits for ACTIVE and binds retained ownership once ready.
  if (version >= 2 && GetGame() && GetGame().GetWorld()) EBG_CacheManager.Get();
#ifdef EBG_ACCEPTANCE_TEST
  PrintFormat("[EBG MISSION STATE] DESERIALIZE version=%1 value=%2 token='%3' calls=%4", version, value, token, state.DeserializeCalls);
#endif
  return true;
 }
}
