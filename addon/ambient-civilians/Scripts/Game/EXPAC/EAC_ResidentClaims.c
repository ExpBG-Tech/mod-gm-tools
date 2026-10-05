// Internal handle. Mutate through the active module, never edit its fields directly.
class EAC_ResidentClaim
{
 EAC_HouseholdRecord Home;
 EAC_ResidentRecord Resident;
 IEntity Character;
 SCR_AIGroup Group;
 ref EBG_CacheMember OptimizerMember;
 ref EAC_PedestrianCache Cache;
 bool Committed;
 float AlarmUntil;
 protected bool m_EAC_SessionTransferred;

 // Ownership history survives CDF clearing the old actor before loading modules.
 // Observing a handoff changes only this claim, never the actor or its group.
 bool EAC_SessionTransferred()
 {
  if (m_EAC_SessionTransferred) return true;
  if (OptimizerMember && OptimizerMember.WasPlayer) m_EAC_SessionTransferred = true;
  SCR_ChimeraCharacter actor = SCR_ChimeraCharacter.Cast(Character);
  if (!actor) return m_EAC_SessionTransferred;
  if (actor.EBG_WasPlayerControlled() || SCR_PossessingManagerComponent.GetPlayerIdFromControlledEntity(actor) != 0) m_EAC_SessionTransferred = true;
  AIControlComponent control = AIControlComponent.Cast(actor.FindComponent(AIControlComponent));
  if (control && control.GetAIAgent() && control.GetAIAgent().GetParentGroup() && control.GetAIAgent().GetParentGroup() != Group) m_EAC_SessionTransferred = true;
  return m_EAC_SessionTransferred;
 }
}

// Pedestrian claims persist independently of placed configuration modules.
// This ledger neither spawns nor deletes entities; failed cleanup retains capacity.
class EAC_ResidentClaims
{
 protected ref EAC_HouseholdRegistry m_Registry;
 protected ref EAC_PopulationBudget m_Budget;
 protected BaseWorld m_World;
 protected ref map<int, ref EAC_ResidentClaim> m_Claims = new map<int, ref EAC_ResidentClaim>();
 protected int m_CacheCompleted, m_ExclusionCompleted, m_SessionCompleted;

 int GetCacheCompleted() { return m_CacheCompleted; }
 int GetExclusionCompleted() { return m_ExclusionCompleted; }
 int GetSessionCompleted() { return m_SessionCompleted; }

 void EAC_ResidentClaims(EAC_HouseholdRegistry registry, EAC_PopulationBudget budget, BaseWorld world)
 {
  m_Registry = registry; m_Budget = budget; m_World = world;
 }

 EAC_ResidentClaim Find(EAC_HouseholdRecord home, EAC_ResidentRecord resident)
 {
  if (!m_Registry.OwnsResident(home, resident)) return null;
  return m_Claims.Get(resident.Id);
 }

 bool ReservesGroup(SCR_AIGroup group, EAC_ResidentClaim except = null)
 {
  if (!group || !Replication.IsServer() || group.GetWorld() != m_World) return false;
  foreach (int id, EAC_ResidentClaim claim : m_Claims)
  {
   if (claim == except) continue;
   if (claim.Group == group) return true;
   SCR_ChimeraCharacter actor = SCR_ChimeraCharacter.Cast(claim.Character);
   if (!actor && claim.OptimizerMember) actor = claim.OptimizerMember.Entity;
   if (actor && actor.GetCharacterGroup() == group) return true;
  }
  return false;
 }

 EBG_CacheMember FindOptimizerMember(IEntity entity)
 {
  if (!entity || !Replication.IsServer() || entity.GetWorld() != m_World) return null;
  foreach (int id, EAC_ResidentClaim claim : m_Claims)
   if (claim.OptimizerMember && claim.OptimizerMember.Entity == entity) return claim.OptimizerMember;
  return null;
 }

 protected bool Owns(EAC_ResidentClaim claim)
 {
  if (!claim || !Replication.IsServer() || !GetGame() || GetGame().GetWorld() != m_World) return false;
  return m_Registry.OwnsResident(claim.Home, claim.Resident) && m_Claims.Get(claim.Resident.Id) == claim;
 }

 EAC_ResidentClaim Begin(EAC_HouseholdRecord home, EAC_ResidentRecord resident, int limit)
 {
  if (!Replication.IsServer() || !GetGame() || GetGame().GetWorld() != m_World) return null;
  if (!m_Registry.OwnsResident(home, resident) || Find(home, resident) || !home.BuildingEntity || resident.Dead || resident.Removed || !resident.Wanted) return null;
  EAC_ResidentClaim claim = new EAC_ResidentClaim();
  claim.Home = home; claim.Resident = resident;
  if (!m_Budget.TryReserveResident(claim, limit)) return null;
  m_Claims.Insert(resident.Id, claim);
  return claim;
 }

 bool TrackCharacter(EAC_ResidentClaim claim, IEntity character)
 {
  if (!Owns(claim) || claim.Committed || claim.Character || !SCR_ChimeraCharacter.Cast(character) || character.GetWorld() != m_World) return false;
  // At most the shared population cap (200) claims; avoid weak entity map keys.
  foreach (int id, EAC_ResidentClaim other : m_Claims)
   if (other.Character == character || (other.OptimizerMember && other.OptimizerMember.Entity == character)) return false;
  SCR_ChimeraCharacter civilian = SCR_ChimeraCharacter.Cast(character);
  if (ReservesGroup(civilian.GetCharacterGroup(), claim) || !EBG_CacheManager.Get().EAC_CanTrack(null, character)) return false;
  claim.OptimizerMember = new EBG_CacheMember();
  claim.OptimizerMember.Id = claim.Resident.Id;
  claim.OptimizerMember.Entity = civilian;
  claim.OptimizerMember.Position = character.GetOrigin();
  claim.Character = character;
  return true;
 }

 bool TrackGroup(EAC_ResidentClaim claim, SCR_AIGroup group)
 {
  if (!Owns(claim) || claim.Committed || claim.Group || !group || group.GetWorld() != m_World) return false;
  foreach (int id, EAC_ResidentClaim other : m_Claims)
   if (other.Group == group) return false;
  if (ReservesGroup(group, claim) || !EBG_CacheManager.Get().EAC_CanTrack(group, null)) return false;
  claim.Group = group;
  return true;
 }

 // Ownership commit only. The spawn controller must qualify placement/behavior.
 bool Commit(EAC_ResidentClaim claim)
 {
  if (!Owns(claim) || claim.Committed || !claim.Character || !claim.Group || claim.Resident.Dead || claim.Resident.Removed || !claim.Resident.Wanted || !claim.Home.BuildingEntity) return false;
  if (claim.Group.GetAgentsCount() != 1) return false;
  AIControlComponent control = AIControlComponent.Cast(claim.Character.FindComponent(AIControlComponent));
  if (!control || !control.GetAIAgent() || control.GetAIAgent().GetParentGroup() != claim.Group) return false;
  if (!EAC_ExclusionZone.IsPopulationAllowed(claim.Home.BuildingEntity.GetOrigin()) || !EAC_ExclusionZone.IsPopulationAllowed(claim.Character.GetOrigin())) return false;
  claim.Committed = true;
  return true;
 }

 bool Cancel(EAC_ResidentClaim claim)
 {
  if (!Owns(claim) || claim.Committed || claim.Character || claim.Group) return false;
  return Release(claim);
 }

 // Binding only: the caller must qualify state/visibility before destructive sleep.
 EAC_PedestrianCache BeginCache(EAC_ResidentClaim claim)
 {
  if (!Owns(claim) || !claim.Committed || claim.Cache) return null;
  if (!claim.OptimizerMember || claim.OptimizerMember.Entity != claim.Character || claim.OptimizerMember.Dead || claim.OptimizerMember.WasPlayer || claim.Resident.Dead || claim.Resident.Removed) return null;
  if (claim.AlarmUntil > m_World.GetWorldTime() * 0.001 || EAC_CivilianDanger.HasThreat(claim.Character) || !EAC_PedestrianSpawner.IsCivilian(claim.Character, claim.Group)) return null;
  SCR_CharacterControllerComponent controller = SCR_CharacterControllerComponent.Cast(claim.Character.FindComponent(SCR_CharacterControllerComponent));
  if (!controller || controller.IsLoitering() || controller.GetScrInputContext().m_iLoiteringType >= 0) return null;
  claim.Cache = new EAC_PedestrianCache(claim);
  return claim.Cache;
 }

 bool FinishCache(EAC_ResidentClaim claim)
 {
  if (!Owns(claim) || !claim.Cache) return false;
  if (claim.Cache.HasDeletionAttempted())
  {
   if (!claim.Cache.IsComplete() || !Release(claim)) return false;
   if (m_CacheCompleted < EAC_Diagnostics.COUNTER_LIMIT) m_CacheCompleted++;
   ResetActivityState(claim.Resident);
  }
  claim.Cache = null;
  return true;
 }

 bool FinishRemoval(EAC_ResidentClaim claim, bool died)
 {
  if (!Owns(claim) || !claim.Committed || claim.Cache || claim.Character || claim.Group) return false;
  if (!Release(claim)) return false;
  claim.Resident.Dead = claim.Resident.Dead || died;
  return true;
 }

 // Never release a zone's cleanup reservation while ANY owned entity exists.
 bool FinishExclusionRemoval(EAC_ResidentClaim claim, bool sessionRemoval = false)
 {
  if (!Owns(claim) || claim.Character || claim.Group) return false;
  if (claim.OptimizerMember && claim.OptimizerMember.Entity) return false;
  if (!Release(claim)) return false;
  if (sessionRemoval)
  {
   if (m_SessionCompleted < EAC_Diagnostics.COUNTER_LIMIT) m_SessionCompleted++;
  }
  else if (m_ExclusionCompleted < EAC_Diagnostics.COUNTER_LIMIT) m_ExclusionCompleted++;
  claim.Cache = null;
  ResetActivityState(claim.Resident);
  // Deliberate absence is neither death nor a GM-deleted individual. The same
  // household slot can be admitted again once its exclusion is removed.
  return true;
 }

 // Keep only the home slot and terminal outcomes. A replacement starts new
 // routines and receives no prior physical position or activity progress.
 protected void ResetActivityState(EAC_ResidentRecord resident)
 {
  resident.HasActivityPosition = false; resident.LastActivityPosition = vector.Zero;
  resident.ActivityStep = 0; resident.FailedTableAttempts = 0; resident.NextRoutineAt = 0;
  resident.DangerResponse = 0; resident.RoutineSeeded = false;
  resident.AnchorCursor = 0; resident.StationCursor = 0;
  resident.SceneHomeId = 0; resident.SceneHops = 0; resident.SceneSeed = 0;
 }

 protected bool Release(EAC_ResidentClaim claim)
 {
  if (!m_Budget.ReleaseResident(claim)) return false;
  m_Claims.Remove(claim.Resident.Id);
  return true;
 }

 bool EAC_ContainsSessionEntity(SCR_EditableEntityComponent candidate)
 {
  foreach (int id, EAC_ResidentClaim claim : m_Claims)
  {
   if (claim.EAC_SessionTransferred()) continue;
   if (EAC_SessionLifecycle.ContainsOwned(candidate, claim.Character)) return true;
   if (claim.OptimizerMember && EAC_SessionLifecycle.ContainsOwned(candidate, claim.OptimizerMember.Entity)) return true;
   if (EAC_SessionLifecycle.ContainsOwned(candidate, claim.Group) && EAC_SessionGroupExclusive(candidate, claim)) return true;
  }
  return false;
 }

 protected bool EAC_SessionGroupExclusive(SCR_EditableEntityComponent editable, EAC_ResidentClaim claim)
 {
  if (claim.Group.GetAgentsCount() > 1 || claim.Group.GetPlayerCount() > 0 || claim.Group.GetChildren()) return false;
  EAC_PedestrianSpawner spawner = EAC_AmbientModule.EAC_GetSessionSpawner();
  for (int i = 0; i < editable.GetChildrenCount(true); i++)
  {
   SCR_EditableEntityComponent child = editable.GetChild(i);
   if (!child) continue;
   if (spawner && spawner.EAC_ContainsSessionHelper(child, claim)) continue;
   if (child.GetOwner() != claim.Character && (!claim.OptimizerMember || child.GetOwner() != claim.OptimizerMember.Entity)) return false;
  }
  return true;
 }
}
