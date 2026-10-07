// Shared garrison spawn API: EXPBG Add Garrison (EXPG_Editor.c) and the Random
// Garrison module (addon/random-garrison) create squads the same way. One validator
// for squad prefabs, one spawner that creates the squad on its building and makes
// it a garrison in the same call (CanFit, spawn, fresh roster, AdoptFresh, then the
// record's generator id and cache settings). Server only. Nobody is ever trimmed:
// a squad deploys in full or is refused before anything spawns.

// A squad prefab a garrison can take: an editable GROUP whose class is an
// SCR_AIGroup and that spawns its members by itself, with 1 to 32 unit slots that
// are all editable characters and, when a faction is given, of that faction.
class EXPG_SquadPrefab
{
 static const int MAX_MEMBERS = 32;

 static bool Validate(ResourceName prefab, out int members, out string reason, string faction = "")
 {
  members = 0;
  reason = "That squad prefab could not be loaded.";
  if (prefab.IsEmpty())
  {
   return false;
  }
  Resource resource = Resource.Load(prefab);
  if (!resource || !resource.IsValid())
  {
   return false;
  }
  reason = "Choose an infantry squad.";
  IEntityComponentSource editableSource = SCR_EditableEntityComponentClass.GetEditableEntitySource(resource);
  if (!editableSource || SCR_EditableEntityComponentClass.GetEntityType(editableSource) != EEditableEntityType.GROUP)
  {
   return false;
  }
  IEntitySource source = resource.GetResource().ToEntitySource();
  if (!source)
  {
   return false;
  }
  typename squadType = source.GetClassName().ToType();
  if (!squadType || !squadType.IsInherited(SCR_AIGroup))
  {
   return false;
  }
  // A squad that waits for a script to spawn its soldiers would stay empty.
  bool immediate = true;
  source.Get("m_bSpawnImmediately", immediate);
  reason = "That squad does not spawn its soldiers by itself; choose another squad.";
  if (!immediate)
  {
   return false;
  }
  array<ResourceName> slots = {};
  reason = "Choose a verified infantry roster of 1 to 32 soldiers.";
  if (!source.Get("m_aUnitPrefabSlots", slots) || slots.IsEmpty() || slots.Count() > MAX_MEMBERS)
  {
   return false;
  }
  // No vanilla group prefab carries GROUPTYPE_INFANTRY, so verify the roster
  // itself: every unit slot must be an editable character (as vanilla placing does).
  reason = "Only infantry squads can garrison a building.";
  foreach (ResourceName slot : slots)
  {
   Resource memberResource = Resource.Load(slot);
   IEntityComponentSource memberSource;
   if (memberResource && memberResource.IsValid()) { memberSource = SCR_EditableEntityComponentClass.GetEditableEntitySource(memberResource); }
   if (!memberSource || SCR_EditableEntityComponentClass.GetEntityType(memberSource) != EEditableEntityType.CHARACTER)
   {
    return false;
   }
  }
  string squadFaction;
  source.Get("m_faction", squadFaction);
  reason = string.Format("That squad belongs to faction %1, not %2.", squadFaction, faction);
  if (!faction.IsEmpty() && !squadFaction.IsEmpty() && squadFaction != faction)
  {
   return false;
  }
  members = slots.Count();
  reason = "";
  return true;
 }
}

// Cache settings a new garrison starts with (normalized by EXPG_GarrisonSettings).
class EXPG_GarrisonSpawnSettings
{
 int CacheMode = 2;
 float WakeDistance = 300;
 float SleepDistance = 400;
}

// Hooks around the engine spawn. The editor's subclass runs the vanilla placement
// callbacks (budget, author, placement events); a module has none: it never reserves
// a Game Master's budget.
class EXPG_SpawnCallbacks
{
 void BeforeSpawn(ResourceName prefab)
 {
 }

 void AfterSpawn(SCR_AIGroup squad, SCR_EditableEntityComponent editable)
 {
 }
}

// One squad for one building. Results: Squad (also set when the garrison assignment
// was refused after the spawn), Reinforcing (the building already had a garrison)
// and AILimited (refused for the AI limit).
class EXPG_GarrisonSpawnRequest
{
 IEntity Structure;
 ResourceName Prefab;
 int Members;
 int CreatorId;
 string GeneratedBy;
 string FactionId;
 ref EXPG_GarrisonSpawnSettings CacheSettings;
 ref EXPG_SpawnCallbacks Callbacks;
 bool CheckAILimit;
 bool DeleteOnRefusal;
 bool Reinforcing;
 bool AILimited;
 SCR_AIGroup Squad;
}

// Entities a module deletes a few per tick (Random Garrison Clear, Stop, a deleted
// zone). Until each one is deleted it stays out of every save (NON_SERIALIZABLE for
// CDF, native tracking stopped). Only what this list, or the keeper that handed the
// entity over, changed is handed back, and only when DeleteOwned refuses (a player
// took the entity over).
class EXPG_RetireList
{
 protected ref array<IEntity> m_aEntities = {};
 protected ref array<bool> m_aFlagSet = {};
 protected ref array<bool> m_aUntracked = {};

 int Count()
 {
  return m_aEntities.Count();
 }

 bool IsEmpty()
 {
  return m_aEntities.IsEmpty();
 }

 bool Contains(IEntity entity)
 {
  return m_aEntities.Contains(entity);
 }

 void Clear()
 {
  m_aEntities.Clear();
  m_aFlagSet.Clear();
  m_aUntracked.Clear();
 }

 // flagSet/untracked: what the previous keeper changed and now hands over.
 void Add(IEntity entity, bool flagSet = false, bool untracked = false)
 {
  if (!entity) { return; }
  int index = m_aEntities.Find(entity);
  if (index < 0)
  {
   m_aEntities.Insert(entity);
   m_aFlagSet.Insert(false);
   m_aUntracked.Insert(false);
   index = m_aEntities.Count() - 1;
  }
  if (flagSet) { m_aFlagSet[index] = true; }
  if (untracked) { m_aUntracked[index] = true; }
  Hold(index, true);
 }

 // Right before a native save: force (lazy registration may have restarted tracking).
 void KeepOut(bool force)
 {
  for (int i = 0; i < m_aEntities.Count(); i++) { Hold(i, force); }
 }

 protected void Hold(int index, bool force)
 {
  IEntity entity = m_aEntities[index];
  if (!entity || entity.IsDeleted()) { return; }
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.GetEditableEntity(entity);
  if (editable && !editable.HasEntityFlag(EEditableEntityFlag.NON_SERIALIZABLE))
  {
   editable.SetEntityFlag(EEditableEntityFlag.NON_SERIALIZABLE, true);
   m_aFlagSet[index] = true;
  }
  SCR_PersistenceSystem persistence = SCR_PersistenceSystem.GetByEntityWorld(entity);
  if (!persistence) { return; }
  bool tracked = persistence.IsTracked(entity);
  if (tracked) { m_aUntracked[index] = true; }
  if (tracked || force) { persistence.StopTracking(entity); }
 }

 // Moves every entry, with what was changed, to another list (zone -> janitor).
 void MoveTo(notnull EXPG_RetireList target)
 {
  for (int i = 0; i < m_aEntities.Count(); i++) { target.Add(m_aEntities[i], m_aFlagSet[i], m_aUntracked[i]); }
  Clear();
 }

 // Deletes in order (soldiers, then their squad) until budget deletes are spent;
 // returns the number deleted.
 int DeleteSome(int budget)
 {
  int deleted;
  while (!m_aEntities.IsEmpty() && deleted < budget)
  {
   IEntity entity = m_aEntities[0];
   bool flagSet = m_aFlagSet[0];
   bool untracked = m_aUntracked[0];
   m_aEntities.RemoveOrdered(0);
   m_aFlagSet.RemoveOrdered(0);
   m_aUntracked.RemoveOrdered(0);
   if (EXPG_GarrisonSpawner.DeleteOwned(entity))
   {
    deleted++;
    continue;
   }
   HandBack(entity, flagSet, untracked);
  }
  return deleted;
 }

 protected static void HandBack(IEntity entity, bool flagSet, bool untracked)
 {
  if (!entity || entity.IsDeleted()) { return; }
  if (flagSet)
  {
   SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.GetEditableEntity(entity);
   if (editable) { editable.SetEntityFlag(EEditableEntityFlag.NON_SERIALIZABLE, false); }
  }
  if (!untracked) { return; }
  SCR_PersistenceSystem persistence = SCR_PersistenceSystem.GetByEntityWorld(entity);
  if (persistence) { persistence.StartTracking(entity); }
 }
}

class EXPG_GarrisonSpawner
{
 // A save or a load is running, or Unit Caching prepares for a save: module spawns
 // wait (EXPBG Add Garrison is refused by CanFit instead).
 static bool Paused(out string reason)
 {
  reason = "";
  if (EBG_CacheSnapshot.Loading || EXPG_GarrisonPersistence.Importing())
  {
   reason = "garrisons are loading from a save";
   return true;
  }
  if (EBG_OptimizerControl.Preparing)
  {
   reason = "Unit Caching Prepare for Save";
   return true;
  }
  if (EXPG_GarrisonManager.SaveInProgress())
  {
   reason = "saving";
   return true;
  }
  return false;
 }

 // Room for count more AI: the world's active AI limit and the faction's limit.
 static bool AIHeadroom(int count, string faction, out string reason)
 {
  reason = "";
  if (!GetGame())
  {
   return true;
  }
  ChimeraAIWorld aiWorld = ChimeraAIWorld.Cast(GetGame().GetAIWorld());
  if (!aiWorld)
  {
   return true;
  }
  int active = aiWorld.GetCurrentNumOfActiveAIs();
  int limit = aiWorld.GetLimitOfActiveAIs();
  if (limit > 0 && active + count > limit)
  {
   reason = string.Format("AI limit %1/%2", active, limit);
   return false;
  }
  if (!faction.IsEmpty() && !aiWorld.CanLimitedAIBeAddedForFaction(faction))
  {
   reason = string.Format("AI limit of faction %1", faction);
   return false;
  }
  return true;
 }

 // Dry ground 4 m outside the building's bounds, first of the four sides that is not
 // water: a squad added to a garrisoned building spawns there, not at the origin, so
 // new soldiers never shove existing guards off their posts.
 static vector ReinforcementSpawn(IEntity building, vector origin)
 {
  vector mins, maxs;
  building.GetWorldBounds(mins, maxs);
  vector center = (mins + maxs) * 0.5;
  array<vector> sides = {Vector(maxs[0] + 4, 0, center[2]), Vector(mins[0] - 4, 0, center[2]), Vector(center[0], 0, maxs[2] + 4), Vector(center[0], 0, mins[2] - 4)};
  foreach (vector side : sides)
  {
   side[1] = GetGame().GetWorld().GetSurfaceY(side[0], side[2]);
   if (!ChimeraWorldUtils.TryGetWaterSurfaceSimple(GetGame().GetWorld(), side + "0 0.5 0"))
   {
    return side;
   }
  }
  return origin;
 }

 // The squad's replicated settings (the Game Master's edit surface) and its record.
 static void ApplySettings(SCR_AIGroup squad, EXPG_GarrisonSpawnSettings settings)
 {
  if (!squad || !settings || !Replication.IsServer()) { return; }
  vector values = EXPG_GarrisonSettings.Normalize(Vector(settings.WakeDistance, settings.SleepDistance, settings.CacheMode));
  squad.EXPG_WakeDistance = values[0];
  squad.EXPG_SleepDistance = values[1];
  squad.EXPG_CacheMode = values[2];
  squad.EXPG_Changed();
  EXPG_GarrisonManager manager = EXPG_GarrisonManager.Get();
  if (!manager) { return; }
  EXPG_GarrisonRecord record = manager.Find(squad);
  if (record) { record.SyncSettings(); }
 }

 // The same for a garrison that may have no squad in the world (Full cached): the
 // record keeps the settings and hands them to the squad when it is recreated.
 static void ApplyRecordSettings(EXPG_GarrisonRecord record, EXPG_GarrisonSpawnSettings settings)
 {
  if (!record || record.Finished || !settings) { return; }
  if (record.Group)
  {
   ApplySettings(record.Group, settings);
   return;
  }
  vector values = EXPG_GarrisonSettings.Normalize(Vector(settings.WakeDistance, settings.SleepDistance, settings.CacheMode));
  record.WakeDistance = values[0];
  record.SleepDistance = values[1];
  record.CacheMode = values[2];
 }

 // A possessed or player-controlled body is never a module's to delete.
 static bool IsPlayerCharacter(IEntity entity)
 {
  SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(entity);
  if (!character)
  {
   return false;
  }
  CharacterControllerComponent controller = character.GetCharacterController();
  if (controller && controller.IsPlayerControlled())
  {
   return true;
  }
  if (character.EBG_WasPlayerControlled())
  {
   return true;
  }
  return SCR_PossessingManagerComponent.GetPlayerIdFromControlledEntity(character) != 0;
 }

 // A squad that never became a garrison: its living soldiers, then the squad itself.
 static int CollectSquad(SCR_AIGroup squad, notnull array<IEntity> leftovers)
 {
  if (!squad)
  {
   return 0;
  }
  int added;
  array<AIAgent> agents = {};
  squad.GetAgents(agents);
  foreach (AIAgent agent : agents)
  {
   IEntity actor;
   if (agent) { actor = agent.GetControlledEntity(); }
   if (!actor || IsPlayerCharacter(actor) || leftovers.Contains(actor)) { continue; }
   leftovers.Insert(actor);
   added++;
  }
  if (!leftovers.Contains(squad))
  {
   leftovers.Insert(squad);
   added++;
  }
  return added;
 }

 // Random Garrison Clear: the garrison is forgotten at once (EXPG_GarrisonManager.Discard;
 // nobody is woken or respawned) and its living soldiers still in the squad, then the
 // squad, are handed to the caller to delete a few at a time. In the same call they
 // leave EXPG_SaveExclusion without being handed back, so they stay out of every save
 // until deleted. Soldiers who left the squad (surrendered, regrouped) and possessed
 // ones are kept.
 static int Dismiss(EXPG_GarrisonRecord record, notnull EXPG_RetireList leftovers, string why)
 {
  EXPG_GarrisonManager manager = EXPG_GarrisonManager.Get();
  if (!manager || !record || record.Finished)
  {
   return 0;
  }
  if (!manager.Discard(record, why))
  {
   return 0;
  }
  int added;
  foreach (EXPG_GarrisonMember member : record.Members)
  {
   SCR_ChimeraCharacter actor = member.CacheMember.Entity;
   if (!actor || member.CacheMember.Dead || member.CacheMember.WasPlayer || EXPG_GarrisonManager.IsDeadActor(actor) || IsPlayerCharacter(actor)) { continue; }
   if (!record.Group || actor.GetCharacterGroup() != record.Group || leftovers.Contains(actor)) { continue; }
   Retire(leftovers, actor);
   added++;
  }
  if (record.Group && !leftovers.Contains(record.Group))
  {
   Retire(leftovers, record.Group);
   added++;
  }
  return added;
 }

 // The garrison's keeper (EXPG_SaveExclusion) hands the entity over with what it had
 // changed; the list keeps it out of saves from here on.
 protected static void Retire(notnull EXPG_RetireList leftovers, IEntity entity)
 {
  bool flagged, untracked;
  EXPG_SaveExclusion.HandOver(entity, flagged, untracked);
  leftovers.Add(entity, flagged, untracked);
 }

 // Deletes one entity a module handed over; never a player's body or a squad with players.
 static bool DeleteOwned(IEntity entity)
 {
  if (!entity || entity.IsDeleted() || IsPlayerCharacter(entity))
  {
   return false;
  }
  SCR_AIGroup squad = SCR_AIGroup.Cast(entity);
  if (squad && squad.GetPlayerCount() > 0)
  {
   return false;
  }
  SCR_EntityHelper.DeleteEntityAndChildren(entity);
  return true;
 }

 // Creates the squad on its building and makes it a garrison in the same call.
 // Returns the garrison's squad, or null with the reason (request.Squad is still set
 // when the squad spawned but the garrison assignment was refused).
 static SCR_AIGroup Spawn(notnull EXPG_GarrisonSpawnRequest request, out string reason)
 {
  reason = "Garrison manager is unavailable.";
  request.Squad = null;
  request.AILimited = false;
  if (!Replication.IsServer() || !GetGame())
  {
   return null;
  }
  EXPG_GarrisonManager manager = EXPG_GarrisonManager.Get();
  if (!manager)
  {
   return null;
  }
  reason = "The selected building no longer exists.";
  if (!request.Structure || request.Structure.IsDeleted())
  {
   return null;
  }
  if (!manager.CanFit(request.Structure, request.Members, reason))
  {
   return null;
  }
  if (request.CheckAILimit && !AIHeadroom(request.Members, request.FactionId, reason))
  {
   request.AILimited = true;
   return null;
  }
  reason = "That squad prefab could not be loaded.";
  Resource resource = Resource.Load(request.Prefab);
  if (!resource || !resource.IsValid())
  {
   return null;
  }
  vector transform[4];
  request.Structure.GetWorldTransform(transform);
  request.Reinforcing = manager.HasGarrison(request.Structure);
  if (request.Reinforcing) { transform[3] = ReinforcementSpawn(request.Structure, transform[3]); }
  EntitySpawnParams spawn = new EntitySpawnParams();
  spawn.TransformMode = ETransformMode.WORLD;
  Math3D.MatrixCopy(transform, spawn.Transform);
  if (request.Callbacks) { request.Callbacks.BeforeSpawn(request.Prefab); }
  // Keep the direct reference even if an invalid third-party prefab lacks editable data.
  IEntity entity = GetGame().SpawnEntityPrefab(resource, GetGame().GetWorld(), spawn);
  reason = "The engine could not spawn that squad.";
  if (!entity)
  {
   return null;
  }
  SCR_AIGroup squad = SCR_AIGroup.Cast(entity);
  bool fresh = squad && squad.EXPG_BeginFreshRoster(request.Members);
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.GetEditableEntity(entity);
  if (!squad || !editable || !fresh)
  {
   if (squad) { squad.EXPG_EndFreshRoster(); }
   if (request.DeleteOnRefusal) { SCR_EntityHelper.DeleteEntityAndChildren(entity); }
   reason = "The prefab did not create a verified editable squad; inspect the spawned entity.";
   return null;
  }
  request.Squad = squad;
  if (request.Callbacks) { request.Callbacks.AfterSpawn(squad, editable); }
  else if (request.CreatorId > 0) { editable.SetAuthor(request.CreatorId); }
  if (!manager.AdoptFresh(squad, request.Structure, request.CreatorId, request.Members))
  {
   squad.EXPG_EndFreshRoster();
   if (request.DeleteOnRefusal)
   {
    array<IEntity> leftovers = {};
    CollectSquad(squad, leftovers);
    foreach (IEntity leftover : leftovers) { DeleteOwned(leftover); }
    request.Squad = null;
   }
   reason = "The squad remains under normal AI control because garrison assignment was refused.";
   return null;
  }
  EXPG_GarrisonRecord record = manager.Find(squad);
  if (record) { record.GeneratedBy = request.GeneratedBy; }
  if (request.CacheSettings) { ApplySettings(squad, request.CacheSettings); }
  reason = "";
  return squad;
 }
}
