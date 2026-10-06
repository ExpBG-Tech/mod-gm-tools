// EXPBG AI Surrender server logic. Event-driven: a soldier going down (life-state
// change) or a casualty leaving his group queues that squad once; one coalesced call
// evaluates at most GROUPS_PER_PASS squads. No soldier is polled. The only timer is a
// 5-second upkeep over the capped prisoner list, and only while prisoners exist.

// One squad that has taken a casualty while the module was active.
class ESR_SquadRecord
{
 SCR_AIGroup Group; // weak
 int Peak;
 int LastCasualties;
 float Threshold;
}

// Identity captured on the server at the moment of surrender. Strings may be
// localisation keys; clients translate them when they show the result.
class ESR_Dossier
{
 string NameFormat;
 string Name;
 string Alias;
 string Surname;
 string Bio;
 string Origin;
 int Age = -1;
 bool Leader;
 string LeaderFormat;
 string LeaderName;
 string LeaderAlias;
 string LeaderSurname;

 static void ReadName(IEntity entity, out string format, out string name, out string alias, out string surname)
 {
  if (!entity) return;
  SCR_CharacterIdentityComponent scripted = SCR_CharacterIdentityComponent.Cast(entity.FindComponent(SCR_CharacterIdentityComponent));
  if (scripted && scripted.GetIdentity())
  {
   scripted.GetFormattedFullName(format, name, alias, surname);
   return;
  }
  CharacterIdentityComponent identity = CharacterIdentityComponent.Cast(entity.FindComponent(CharacterIdentityComponent));
  if (!identity || !identity.GetIdentity()) return;
  name = identity.GetIdentity().GetName();
  alias = identity.GetIdentity().GetAlias();
  surname = identity.GetIdentity().GetSurname();
 }

 static ESR_Dossier Capture(notnull IEntity character, IEntity leader)
 {
  ESR_Dossier dossier = new ESR_Dossier();
  string format, name, alias, surname;
  ReadName(character, format, name, alias, surname);
  dossier.NameFormat = format; dossier.Name = name; dossier.Alias = alias; dossier.Surname = surname;
  SCR_ExtendedIdentityComponent extended = SCR_ExtendedIdentityComponent.Cast(character.FindComponent(SCR_ExtendedIdentityComponent));
  if (extended)
  {
   SCR_IdentityBio bio = extended.GetIdentityBio();
   if (bio) dossier.Bio = bio.GetBioText();
   SCR_ExtendedIdentity data = extended.GetExtendedIdentity();
   if (data)
   {
    dossier.Age = data.GetAge();
    SCR_UIInfo place = data.GetPlaceOfOriginUIInfo();
    if (place) dossier.Origin = place.GetName();
   }
  }
  dossier.Leader = leader == character;
  if (leader && !dossier.Leader)
  {
   string leaderFormat, leaderName, leaderAlias, leaderSurname;
   ReadName(leader, leaderFormat, leaderName, leaderAlias, leaderSurname);
   dossier.LeaderFormat = leaderFormat; dossier.LeaderName = leaderName; dossier.LeaderAlias = leaderAlias; dossier.LeaderSurname = leaderSurname;
  }
  return dossier;
 }
}

class ESR_Prisoner
{
 SCR_ChimeraCharacter Character; // weak
 ESR_InterrogationPoint Point; // weak
 SCR_AIGroup Group; // weak; the squad he left
 ref ESR_Dossier Dossier;
 string SideKey;
 int Attempts;
 int Outcome = -1;
 int RevealCount;
 int RevealDistance;
 int RevealBearing;
 int MarkerId = -1;
 int PoseTries;
 float NextQuestion;
}

class ESR_SurrenderManager
{
 static const int OUTCOME_BUSY = -1;
 static const int OUTCOME_REFUSED = 0;
 static const int OUTCOME_REVEAL = 1;
 static const int OUTCOME_IDENTITY = 2;
 static const int OUTCOME_SILENT = 3;
 static const int OUTCOME_NO_SQUAD = 4;

 static const int MAX_PRISONERS = 64;
 static const int MAX_SQUADS = 256;
 static const int GROUPS_PER_PASS = 8;
 static const int QUEUE_DELAY_MS = 400;
 static const int POSE_DELAY_MS = 1500;
 static const int UPKEEP_MS = 5000;
 static const int MAX_POSE_TRIES = 6;
 static const float CHEST_HEIGHT = 0.75;
 static const float INTERROGATE_RANGE = 4;
 static const float QUESTION_COOLDOWN = 2;
 static const ResourceName POINT_PREFAB = "{B412A163F3014DFE}Prefabs/EXPSR/ESR_InterrogationPoint.et";
 static const string CIVILIAN_FACTION = "CIV";

 protected static bool s_bListening;
 protected static bool s_bQueued;
 protected static bool s_bUpkeep;
 protected static ref array<SCR_AIGroup> s_aQueue = {};
 protected static ref array<ref ESR_SquadRecord> s_aSquads = {};
 protected static ref array<ref ESR_Prisoner> s_aPrisoners = {};

 //------------------------------------------------------------------------------------------------
 // State and switches
 //------------------------------------------------------------------------------------------------
 static bool IsListening() { return s_bListening; }
 // Server-side hooks run while new surrenders are possible or prisoners need upkeep.
 static bool IsWatching() { return s_bListening || !s_aPrisoners.IsEmpty(); }
 static int PrisonerCount() { return s_aPrisoners.Count(); }

 static void Trace(string text)
 {
  if (ESR_Settings.Get(ESR_Settings.DIAGNOSTICS) != 0) Print("[EXPBG SURRENDER] " + text);
 }

 static float Now()
 {
  if (!GetGame() || !GetGame().GetWorld()) return 0;
  return GetGame().GetWorld().GetWorldTime() * 0.001;
 }

 static void Refresh()
 {
  if (!GetGame() || !Replication.IsServer())
  {
   s_bListening = false;
   return;
  }
  s_bListening = ESR_SurrenderModule.ActiveCount() > 0 && ESR_Settings.Get(ESR_Settings.ENABLED) != 0;
 }

 // First module of a session (or after every module was deleted). Statics survive a
 // world change, so stale timers are dropped and dead references pruned; living
 // prisoners of this session are kept.
 static void OnFirstModule()
 {
  if (!GetGame()) return;
  ScriptCallQueue queue = GetGame().GetCallqueue();
  queue.Remove(ESR_SurrenderManager.ProcessQueue);
  queue.Remove(ESR_SurrenderManager.Upkeep);
  s_bQueued = false;
  s_bUpkeep = false;
  for (int i = s_aQueue.Count() - 1; i >= 0; i--) { if (!s_aQueue[i]) s_aQueue.Remove(i); }
  for (int j = s_aSquads.Count() - 1; j >= 0; j--) { if (!s_aSquads[j] || !s_aSquads[j].Group) s_aSquads.Remove(j); }
  for (int k = s_aPrisoners.Count() - 1; k >= 0; k--) { if (!s_aPrisoners[k] || !s_aPrisoners[k].Character) ReleaseAt(k, "stale"); }
  if (!s_aQueue.IsEmpty()) ScheduleQueue();
  StartUpkeep();
 }

 static SCR_AIGroup GroupOf(IEntity entity)
 {
  if (!entity) return null;
  AIControlComponent control = AIControlComponent.Cast(entity.FindComponent(AIControlComponent));
  if (!control) return null;
  AIAgent agent = control.GetControlAIAgent();
  if (!agent) return null;
  return SCR_AIGroup.Cast(agent.GetParentGroup());
 }

 static bool IsPlayerCharacter(IEntity entity)
 {
  if (!entity) return false;
  if (EntityUtils.IsPlayer(entity)) return true;
  if (GetGame().GetPlayerManager().GetPlayerIdFromControlledEntity(entity) > 0) return true;
  return SCR_PossessingManagerComponent.GetPlayerIdFromControlledEntity(entity) > 0;
 }

 static ESR_Prisoner FindPrisoner(IEntity entity)
 {
  if (!entity) return null;
  foreach (ESR_Prisoner prisoner : s_aPrisoners)
  {
   if (prisoner && prisoner.Character == entity) return prisoner;
  }
  return null;
 }

 static ESR_Prisoner FindByPoint(ESR_InterrogationPoint point)
 {
  if (!point) return null;
  foreach (ESR_Prisoner prisoner : s_aPrisoners)
  {
   if (prisoner && prisoner.Point == point) return prisoner;
  }
  return null;
 }

 //------------------------------------------------------------------------------------------------
 // Casualty events (server)
 //------------------------------------------------------------------------------------------------
 static void OnLifeState(IEntity entity, SCR_AIGroup group, ECharacterLifeState previous, ECharacterLifeState current)
 {
  if (!Replication.IsServer() || !entity) return;
  ESR_Prisoner prisoner = FindPrisoner(entity);
  if (prisoner)
  {
   if (current == ECharacterLifeState.DEAD) Release(prisoner, "died");
   // Vanilla reactivates AI when a casualty wakes up; a prisoner stays passive.
   else if (current == ECharacterLifeState.ALIVE) GetGame().GetCallqueue().CallLater(ESR_SurrenderManager.Reassert, 750, false, entity);
   return;
  }
  if (!s_bListening || !group || previous != ECharacterLifeState.ALIVE || current == ECharacterLifeState.ALIVE) return;
  if (!IsCandidateSquad(group)) return;
  // The casualty is still a member here; record the strength before he is removed.
  ESR_SquadRecord record = Squad(group, true);
  if (record && group.GetAgentsCount() > record.Peak) record.Peak = group.GetAgentsCount();
  Queue(group);
 }

 static void OnAgentRemoved(SCR_AIGroup group, AIAgent agent)
 {
  if (!s_bListening || !group || !agent) return;
  SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(agent.GetControlledEntity());
  if (!character) return;
  CharacterControllerComponent controller = character.GetCharacterController();
  // Only casualties count; living soldiers leave groups for many other reasons.
  if (!controller || controller.GetLifeState() == ECharacterLifeState.ALIVE) return;
  if (!IsCandidateSquad(group)) return;
  ESR_SquadRecord record = Squad(group, true);
  if (record && group.GetAgentsCount() + 1 > record.Peak) record.Peak = group.GetAgentsCount() + 1;
  Queue(group);
 }

 static bool IsCandidateSquad(SCR_AIGroup group)
 {
  if (!group || group.IsSlave() || group.GetPlayerCount(true) > 0) return false;
  SCR_Faction faction = SCR_Faction.Cast(group.GetFaction());
  return faction && faction.IsMilitary();
 }

 protected static ESR_SquadRecord Squad(SCR_AIGroup group, bool create)
 {
  for (int i = s_aSquads.Count() - 1; i >= 0; i--)
  {
   ESR_SquadRecord existing = s_aSquads[i];
   if (!existing || !existing.Group) { s_aSquads.Remove(i); continue; }
   if (existing.Group == group) return existing;
  }
  if (!create) return null;
  if (s_aSquads.Count() >= MAX_SQUADS) s_aSquads.RemoveOrdered(0);
  ESR_SquadRecord record = new ESR_SquadRecord();
  record.Group = group;
  record.Peak = group.GetNumberOfMembersToSpawn();
  float spread = ESR_Settings.Get(ESR_Settings.RANDOM);
  record.Threshold = Math.Clamp(ESR_Settings.Get(ESR_Settings.THRESHOLD) + Math.RandomFloat(-spread, spread), 5, 100);
  s_aSquads.Insert(record);
  return record;
 }

 protected static void Queue(SCR_AIGroup group)
 {
  if (!s_aQueue.Contains(group)) s_aQueue.Insert(group);
  ScheduleQueue();
 }

 protected static void ScheduleQueue()
 {
  if (s_bQueued || !GetGame()) return;
  s_bQueued = true;
  GetGame().GetCallqueue().CallLater(ESR_SurrenderManager.ProcessQueue, QUEUE_DELAY_MS, false);
 }

 protected static void ProcessQueue()
 {
  s_bQueued = false;
  int processed;
  while (!s_aQueue.IsEmpty() && processed < GROUPS_PER_PASS)
  {
   SCR_AIGroup group = s_aQueue[0];
   s_aQueue.RemoveOrdered(0);
   processed++;
   Evaluate(group);
  }
  if (!s_aQueue.IsEmpty()) ScheduleQueue();
 }

 protected static void Evaluate(SCR_AIGroup group)
 {
  if (!s_bListening || !IsCandidateSquad(group)) return;
  array<AIAgent> agents = {};
  int total = group.GetAgents(agents);
  int able;
  array<SCR_ChimeraCharacter> candidates = {};
  foreach (AIAgent agent : agents)
  {
   if (!agent) continue;
   SCR_ChimeraCharacter member = SCR_ChimeraCharacter.Cast(agent.GetControlledEntity());
   if (!member) continue;
   // A squad with a player-controlled or possessed member is left alone.
   if (IsPlayerCharacter(member)) return;
   CharacterControllerComponent controller = member.GetCharacterController();
   if (!controller || controller.GetLifeState() != ECharacterLifeState.ALIVE || controller.IsUnconscious()) continue;
   able++;
   if (!member.IsInVehicle() && !FindPrisoner(member)) candidates.Insert(member);
  }
  ESR_SquadRecord record = Squad(group, true);
  if (!record) return;
  if (total > record.Peak) record.Peak = total;
  if (record.Peak <= 0) return;
  int casualties = record.Peak - able;
  // Rolls happen once per new casualty, never twice for the same loss.
  if (casualties <= record.LastCasualties) return;
  record.LastCasualties = casualties;
  float ratio = casualties * 100.0 / record.Peak;
  Trace(string.Format("squad %1 peak=%2 able=%3 casualties=%4 ratio=%5 threshold=%6 candidates=%7", group, record.Peak, able, casualties, ratio, record.Threshold, candidates.Count()));
  if (ratio < record.Threshold) return;
  float chance = ESR_Settings.Get(ESR_Settings.CHANCE);
  float spread = ESR_Settings.Get(ESR_Settings.RANDOM);
  int surrendered;
  foreach (SCR_ChimeraCharacter candidate : candidates)
  {
   if (s_aPrisoners.Count() >= MAX_PRISONERS) break;
   float rolled = Math.Clamp(chance + Math.RandomFloat(-spread, spread), 0, 100);
   if (Math.RandomFloat(0, 100) >= rolled) continue;
   if (Surrender(candidate, group)) surrendered++;
  }
  // Prisoners now count as losses; they must not trigger another round by themselves.
  record.LastCasualties += surrendered;
 }

 //------------------------------------------------------------------------------------------------
 // Surrender (server)
 //------------------------------------------------------------------------------------------------
 static bool Surrender(SCR_ChimeraCharacter character, SCR_AIGroup group)
 {
  if (!Replication.IsServer() || !character || FindPrisoner(character) || s_aPrisoners.Count() >= MAX_PRISONERS) return false;
  SCR_CharacterControllerComponent controller = SCR_CharacterControllerComponent.Cast(character.GetCharacterController());
  AIControlComponent control = AIControlComponent.Cast(character.FindComponent(AIControlComponent));
  if (!controller || !control || controller.GetLifeState() != ECharacterLifeState.ALIVE || character.IsInVehicle() || IsPlayerCharacter(character)) return false;
  IEntity leader;
  if (group) leader = group.GetLeaderEntity();
  ESR_Prisoner prisoner = new ESR_Prisoner();
  prisoner.Character = character;
  prisoner.Group = group;
  // Before he leaves the squad: the leader may change once he is gone.
  prisoner.Dossier = ESR_Dossier.Capture(character, leader);
  Faction faction = character.GetFaction();
  if (faction) prisoner.SideKey = faction.GetFactionKey();
  int dropped = DropWeapons(character);
  AIAgent agent = control.GetControlAIAgent();
  if (group && agent && agent.GetParentGroup() == group) group.RemoveAgent(agent);
  control.DeactivateAI();
  bool civilian = SetCivilian(character);
  controller.SetStanceChange(ECharacterStanceChange.STANCECHANGE_TOERECTED);
  s_aPrisoners.Insert(prisoner);
  SpawnPoint(prisoner);
  // Sit down once the weapon drop and stand-up have settled.
  GetGame().GetCallqueue().CallLater(ESR_SurrenderManager.ApplyPose, POSE_DELAY_MS, false, character);
  StartUpkeep();
  ESR_SurrenderModule.PublishPrisoners(s_aPrisoners.Count());
  PrintFormat("[EXPBG SURRENDER] %1 surrendered at %2 faction=%3 weaponsDropped=%4 civilian=%5 prisoners=%6", character, character.GetOrigin(), prisoner.SideKey, dropped, civilian, s_aPrisoners.Count());
  return true;
 }

 // Vanilla removal from weapon storage drops the item to the ground, as the AI drop node does.
 protected static int DropWeapons(SCR_ChimeraCharacter character)
 {
  BaseWeaponManagerComponent weapons = BaseWeaponManagerComponent.Cast(character.FindComponent(BaseWeaponManagerComponent));
  SCR_InventoryStorageManagerComponent inventory = SCR_InventoryStorageManagerComponent.Cast(character.FindComponent(SCR_InventoryStorageManagerComponent));
  if (!weapons || !inventory) return 0;
  array<IEntity> carried = {};
  weapons.GetWeaponsList(carried);
  int dropped;
  foreach (IEntity weapon : carried)
  {
   if (!weapon) continue;
   InventoryItemComponent item = InventoryItemComponent.Cast(weapon.FindComponent(InventoryItemComponent));
   if (!item) continue;
   InventoryStorageSlot slot = item.GetParentSlot();
   if (!slot || !slot.GetStorage()) continue;
   if (inventory.TryRemoveItemFromStorage(weapon, slot.GetStorage())) dropped++;
  }
  return dropped;
 }

 // Civilians are neither shot at by either side nor fought for. Without a civilian
 // faction the prisoner keeps his own (passive, but still a valid target).
 protected static bool SetCivilian(SCR_ChimeraCharacter character)
 {
  FactionManager factions = GetGame().GetFactionManager();
  FactionAffiliationComponent affiliation = FactionAffiliationComponent.Cast(character.FindComponent(FactionAffiliationComponent));
  if (!factions || !affiliation) return false;
  Faction civilian = factions.GetFactionByKey(CIVILIAN_FACTION);
  SCR_Faction scripted = SCR_Faction.Cast(civilian);
  if (scripted && scripted.IsMilitary()) civilian = null;
  if (!civilian)
  {
   array<Faction> all = {};
   factions.GetFactionsList(all);
   foreach (Faction candidate : all)
   {
    SCR_Faction candidateScripted = SCR_Faction.Cast(candidate);
    if (candidateScripted && !candidateScripted.IsMilitary())
    {
     civilian = candidate;
     break;
    }
   }
  }
  if (!civilian) return false;
  affiliation.SetAffiliatedFaction(civilian);
  return true;
 }

 // Vanilla 1.8 has no hands-up animation; the prisoner uses the vanilla sit-on-ground
 // loiter (the "Sit on ground" emote). Loiter state is part of the character's native
 // replication, so late joiners see the pose too.
 protected static void ApplyPose(SCR_ChimeraCharacter character)
 {
  ESR_Prisoner prisoner = FindPrisoner(character);
  if (!prisoner || !character) return;
  SCR_CharacterControllerComponent controller = SCR_CharacterControllerComponent.Cast(character.GetCharacterController());
  if (!controller || controller.GetLifeState() != ECharacterLifeState.ALIVE || controller.IsLoitering()) return;
  if (!controller.CanPlayLoiterAnimation(ELoiteringType.SIT)) return;
  prisoner.PoseTries++;
  vector anchor[4];
  Math3D.MatrixIdentity4(anchor);
  controller.StartLoitering(null, ELoiteringType.SIT, true, true, false, anchor, true);
  Trace(string.Format("%1 pose requested try=%2", character, prisoner.PoseTries));
 }

 protected static void Reassert(IEntity entity)
 {
  ESR_Prisoner prisoner = FindPrisoner(entity);
  if (!prisoner || !prisoner.Character || IsPlayerCharacter(prisoner.Character)) return;
  AIControlComponent control = AIControlComponent.Cast(prisoner.Character.FindComponent(AIControlComponent));
  if (control && control.IsAIActivated()) control.DeactivateAI();
  ApplyPose(prisoner.Character);
 }

 protected static bool SpawnPoint(ESR_Prisoner prisoner)
 {
  if (!prisoner || !prisoner.Character) return false;
  RplComponent rpl = prisoner.Character.GetRplComponent();
  if (!rpl) return false;
  // Keep the loaded resource in a local before spawning.
  Resource resource = Resource.Load(POINT_PREFAB);
  if (!resource || !resource.IsValid()) return false;
  EntitySpawnParams spawn = new EntitySpawnParams();
  spawn.TransformMode = ETransformMode.WORLD;
  Math3D.MatrixIdentity4(spawn.Transform);
  spawn.Transform[3] = prisoner.Character.GetOrigin() + vector.Up * CHEST_HEIGHT;
  IEntity spawned = GetGame().SpawnEntityPrefab(resource, GetGame().GetWorld(), spawn);
  ESR_InterrogationPoint point = ESR_InterrogationPoint.Cast(spawned);
  if (!point)
  {
   if (spawned) SCR_EntityHelper.DeleteEntityAndChildren(spawned);
   Print("[EXPBG SURRENDER] Interrogation point could not be spawned", LogLevel.WARNING);
   return false;
  }
  point.Setup(rpl.Id(), prisoner.Dossier);
  prisoner.Point = point;
  return true;
 }

 protected static void DeletePoint(ESR_Prisoner prisoner)
 {
  if (!prisoner || !prisoner.Point) return;
  RplComponent.DeleteRplEntity(prisoner.Point, false);
  prisoner.Point = null;
 }

 static void Release(ESR_Prisoner prisoner, string reason)
 {
  int index = s_aPrisoners.Find(prisoner);
  if (index >= 0) ReleaseAt(index, reason);
 }

 protected static void ReleaseAt(int index, string reason)
 {
  if (!s_aPrisoners.IsIndexValid(index)) return;
  ESR_Prisoner prisoner = s_aPrisoners[index];
  DeletePoint(prisoner);
  s_aPrisoners.Remove(index);
  ESR_SurrenderModule.PublishPrisoners(s_aPrisoners.Count());
  Trace(string.Format("prisoner released (%1) prisoners=%2", reason, s_aPrisoners.Count()));
 }

 protected static void StartUpkeep()
 {
  if (s_bUpkeep || s_aPrisoners.IsEmpty() || !GetGame()) return;
  s_bUpkeep = true;
  GetGame().GetCallqueue().CallLater(ESR_SurrenderManager.Upkeep, UPKEEP_MS, false);
 }

 // Bounded: at most MAX_PRISONERS entries, constant work each.
 protected static void Upkeep()
 {
  s_bUpkeep = false;
  for (int i = s_aPrisoners.Count() - 1; i >= 0; i--)
  {
   ESR_Prisoner prisoner = s_aPrisoners[i];
   if (!prisoner || !prisoner.Character) { ReleaseAt(i, "deleted"); continue; }
   CharacterControllerComponent controller = prisoner.Character.GetCharacterController();
   if (!controller || controller.GetLifeState() == ECharacterLifeState.DEAD) { ReleaseAt(i, "died"); continue; }
   // Unconscious, or possessed by a Game Master: leave him alone until he is back.
   if (controller.GetLifeState() != ECharacterLifeState.ALIVE || controller.IsUnconscious() || IsPlayerCharacter(prisoner.Character)) continue;
   AIControlComponent control = AIControlComponent.Cast(prisoner.Character.FindComponent(AIControlComponent));
   if (control && control.IsAIActivated()) control.DeactivateAI();
   SCR_CharacterControllerComponent scripted = SCR_CharacterControllerComponent.Cast(controller);
   if (scripted && !scripted.IsLoitering() && prisoner.PoseTries < MAX_POSE_TRIES) ApplyPose(prisoner.Character);
   // A Game Master may move the prisoner; the interaction point follows.
   vector chest = prisoner.Character.GetOrigin() + vector.Up * CHEST_HEIGHT;
   if (!prisoner.Point) SpawnPoint(prisoner);
   else if (vector.DistanceSq(prisoner.Point.GetOrigin(), chest) > 1)
   {
    DeletePoint(prisoner);
    SpawnPoint(prisoner);
   }
  }
  StartUpkeep();
 }

 //------------------------------------------------------------------------------------------------
 // Interrogation (server)
 //------------------------------------------------------------------------------------------------
 static int Interrogate(ESR_InterrogationPoint point, IEntity user, int playerId)
 {
  if (!Replication.IsServer() || !point || !user) return OUTCOME_BUSY;
  ESR_Prisoner prisoner = FindByPoint(point);
  if (!prisoner || !prisoner.Character) return OUTCOME_BUSY;
  CharacterControllerComponent controller = prisoner.Character.GetCharacterController();
  if (!controller || controller.GetLifeState() != ECharacterLifeState.ALIVE || controller.IsUnconscious()) return OUTCOME_BUSY;
  if (vector.DistanceSq(user.GetOrigin(), prisoner.Character.GetOrigin()) > INTERROGATE_RANGE * INTERROGATE_RANGE) return OUTCOME_BUSY;
  float now = Now();
  if (now < prisoner.NextQuestion) return OUTCOME_BUSY;
  prisoner.NextQuestion = now + QUESTION_COOLDOWN;
  int attempts = ESR_Settings.Get(ESR_Settings.ATTEMPTS);
  int outcome = prisoner.Outcome;
  if (outcome < 0)
  {
   if (prisoner.Attempts >= attempts) outcome = OUTCOME_SILENT;
   else
   {
    prisoner.Attempts++;
    float reveal = ESR_Settings.Get(ESR_Settings.REVEAL);
    float identity = ESR_Settings.Get(ESR_Settings.IDENTITY);
    float roll = Math.RandomFloat(0, 100);
    if (roll < reveal) outcome = Reveal(prisoner, user, playerId);
    else if (roll < Math.Min(100, reveal + identity)) outcome = OUTCOME_IDENTITY;
    else outcome = OUTCOME_REFUSED;
    // Once he talks the answer is fixed: asking again repeats it.
    if (outcome != OUTCOME_REFUSED) prisoner.Outcome = outcome;
   }
  }
  int left = attempts - prisoner.Attempts;
  if (left < 0) left = 0;
  point.SendResult(playerId, outcome, prisoner.RevealCount, prisoner.RevealDistance, prisoner.RevealBearing, left);
  Trace(string.Format("interrogation of %1 by player %2: outcome=%3 attempts=%4/%5 marker=%6", prisoner.Character, playerId, outcome, prisoner.Attempts, attempts, prisoner.MarkerId));
  return outcome;
 }

 // Test and diagnostics access to the last interrogation state of a prisoner.
 static ESR_Prisoner GetPrisonerAt(int index)
 {
  if (!s_aPrisoners.IsIndexValid(index)) return null;
  return s_aPrisoners[index];
 }

 protected static int Reveal(ESR_Prisoner prisoner, IEntity user, int playerId)
 {
  vector origin = prisoner.Character.GetOrigin();
  Faction side;
  FactionManager factions = GetGame().GetFactionManager();
  if (factions && !prisoner.SideKey.IsEmpty()) side = factions.GetFactionByKey(prisoner.SideKey);
  if (!side) return OUTCOME_NO_SQUAD;
  ESR_SquadQuery query = new ESR_SquadQuery(side, prisoner.Group);
  GetGame().GetWorld().QueryEntitiesBySphere(origin, ESR_Settings.Get(ESR_Settings.RADIUS), query.Add, null, EQueryEntitiesFlags.DYNAMIC);
  vector center;
  int count;
  if (!query.Nearest(origin, center, count)) return OUTCOME_NO_SQUAD;
  vector offset = center - origin;
  float distance = vector.DistanceXZ(origin, center);
  int rounded = Math.Round(distance / 25);
  prisoner.RevealDistance = rounded * 25;
  float angle = Math.Atan2(offset[0], offset[2]) * Math.RAD2DEG;
  if (angle < 0) angle += 360;
  int sector = Math.Round(angle / 45);
  prisoner.RevealBearing = sector % 8;
  prisoner.RevealCount = count;
  prisoner.MarkerId = PlaceMarker(center, user, playerId, count);
  return OUTCOME_REVEAL;
 }

 protected static Faction ViewerFaction(IEntity user, int playerId)
 {
  if (playerId > 0)
  {
   Faction playerFaction = SCR_FactionManager.SGetPlayerFaction(playerId);
   if (playerFaction) return playerFaction;
  }
  SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(user);
  if (character) return character.GetFaction();
  return null;
 }

 // A player-owned static marker, as if the interrogator had placed it: his faction sees
 // it, he can delete it from the map, and it is part of the marker manager's JIP state.
 protected static int PlaceMarker(vector center, IEntity user, int playerId, int count)
 {
  SCR_MapMarkerManagerComponent markers = SCR_MapMarkerManagerComponent.GetInstance();
  if (!markers) return -1;
  SCR_MapMarkerBase marker = markers.PrepareMilitaryMarker(EMilitarySymbolIdentity.OPFOR, EMilitarySymbolDimension.LAND, EMilitarySymbolIcon.INFANTRY);
  if (!marker)
  {
   marker = new SCR_MapMarkerBase();
   marker.SetType(SCR_EMapMarkerType.PLACED_CUSTOM);
  }
  int x = center[0];
  int z = center[2];
  marker.SetWorldPos(x, z);
  marker.SetCustomText(string.Format("Prisoner intel: squad of %1", count));
  marker.SetTimestampVisibility(true);
  ChimeraWorld world = GetGame().GetWorld();
  if (world) marker.SetTimestamp(world.GetServerTimestamp());
  FactionManager factions = GetGame().GetFactionManager();
  Faction viewer = ViewerFaction(user, playerId);
  if (factions && viewer) marker.AddMarkerFactionFlags(factions.GetFactionIndex(viewer));
  markers.AssignMarkerUID(marker);
  int ownerId = -1;
  if (playerId > 0) ownerId = playerId;
  marker.SetMarkerOwnerID(ownerId);
  markers.OnAddSynchedMarker(marker);
  markers.OnAskAddStaticMarker(marker);
  int lifetime = ESR_Settings.Get(ESR_Settings.LIFETIME);
  if (lifetime > 0)
  {
   float due = Now() + lifetime * 60;
   GetGame().GetCallqueue().CallLater(ESR_SurrenderManager.RemoveMarker, lifetime * 60000, false, marker.GetMarkerID(), due);
  }
  return marker.GetMarkerID();
 }

 protected static void RemoveMarker(int markerId, float due)
 {
  // A timer from an earlier world (world time restarted) must not remove a new marker.
  if (Now() + 1 < due || markerId < 0) return;
  SCR_MapMarkerManagerComponent markers = SCR_MapMarkerManagerComponent.GetInstance();
  if (!markers) return;
  if (!markers.GetStaticMarkerByID(markerId) && !markers.GetDisabledMarkerByID(markerId)) return;
  markers.OnRemoveSynchedMarker(markerId);
  markers.OnAskRemoveStaticMarker(markerId);
  Trace(string.Format("intel marker %1 expired", markerId));
 }
}

// Collects squads of the prisoner's side around him (dynamic entities only, bounded).
class ESR_SquadQuery
{
 static const int MAX_VISITED = 2048;
 static const int MAX_SQUADS = 32;
 protected Faction m_Side;
 protected SCR_AIGroup m_Own;
 protected ref array<SCR_AIGroup> m_aSquads = {};
 protected int m_iVisited;

 void ESR_SquadQuery(Faction side, SCR_AIGroup own)
 {
  m_Side = side;
  m_Own = own;
 }

 bool Add(IEntity entity)
 {
  m_iVisited++;
  if (m_iVisited > MAX_VISITED || m_aSquads.Count() >= MAX_SQUADS) return false;
  SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(entity);
  if (!character) return true;
  CharacterControllerComponent controller = character.GetCharacterController();
  if (!controller || controller.GetLifeState() != ECharacterLifeState.ALIVE) return true;
  SCR_AIGroup group = ESR_SurrenderManager.GroupOf(character);
  if (!group || m_aSquads.Contains(group) || group.GetPlayerCount(true) > 0) return true;
  Faction faction = group.GetFaction();
  if (!faction || (faction != m_Side && !m_Side.IsFactionFriendly(faction))) return true;
  m_aSquads.Insert(group);
  return true;
 }

 // Nearest squad other than his own; his own remnants only when nothing else is near.
 bool Nearest(vector origin, out vector center, out int count)
 {
  bool found;
  float best;
  foreach (SCR_AIGroup group : m_aSquads)
  {
   if (!group || group == m_Own) continue;
   vector groupCenter;
   int living = Living(group, groupCenter);
   if (living <= 0) continue;
   float distance = vector.DistanceSq(origin, groupCenter);
   if (found && distance >= best) continue;
   found = true;
   best = distance;
   center = groupCenter;
   count = living;
  }
  if (found || !m_Own) return found;
  vector ownCenter;
  int ownLiving = Living(m_Own, ownCenter);
  if (ownLiving <= 0) return false;
  center = ownCenter;
  count = ownLiving;
  return true;
 }

 protected static int Living(SCR_AIGroup group, out vector center)
 {
  array<AIAgent> agents = {};
  group.GetAgents(agents);
  int living;
  vector sum;
  foreach (AIAgent agent : agents)
  {
   if (!agent) continue;
   SCR_ChimeraCharacter member = SCR_ChimeraCharacter.Cast(agent.GetControlledEntity());
   if (!member || !member.GetCharacterController() || member.GetCharacterController().GetLifeState() == ECharacterLifeState.DEAD) continue;
   living++;
   sum = sum + member.GetOrigin();
  }
  if (living > 0) center = sum * (1.0 / living);
  return living;
 }
}
