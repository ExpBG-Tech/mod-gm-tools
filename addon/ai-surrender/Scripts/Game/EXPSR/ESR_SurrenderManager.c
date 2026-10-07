// EXPBG AI Surrender server logic. Event-driven: a soldier going down (life-state
// change) or a casualty leaving his group queues that squad once; one coalesced call
// evaluates at most GROUPS_PER_PASS squads. No soldier is polled. The only repeating
// timer is a 5-second upkeep over the capped prisoner list, and only while prisoners
// exist; a squad leader who takes a grenade instead of surrendering runs three one-shot
// timers (place, set live, check after the blast), at most MAX_COMMANDERS at a time.

// One squad that has taken a casualty while the module was active.
class ESR_SquadRecord
{
 SCR_AIGroup Group; // weak
 int Peak;
 int LastCasualties;
 float Threshold;
 IEntity RolledCommander; // weak; the leader whose grenade chance was rolled
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
 // His squad's overrides when he surrendered (ESR_Overrides slots, -1 = none); his own
 // stay on him and are read at question time (ESR_Overrides.ForPrisoner).
 ref array<int> SquadOverrides = {};
 ref ESR_Dossier Dossier;
 string SideKey;
 int Attempts;
 int Outcome = -1;
 int RevealCount;
 int RevealDistance;
 int RevealBearing;
 int MarkerId = -1;
 // Intel items he pointed out (ESR_IntelQuery), nearest first: reported distance (25 m
 // steps), compass sector and map marker id per item. Rolled once, with his first answer.
 bool IntelRolled;
 ref array<int> IntelDistances = {};
 ref array<int> IntelBearings = {};
 ref array<int> IntelMarkers = {};
 int PoseTries;
 bool AceMode; // held in ACE Captives' surrender state (ACE loaded)
 bool AceFailed; // ACE did not take him: the vanilla sit from then on
 int AceTries;
 int AceIdle; // upkeep ticks with every ACE state cleared
 float NextQuestion;
}

// A squad leader who sets a grenade live at his own feet instead of surrendering
// (server). Each record ends through its own one-shot timers; Id keys those timers so a
// deleted body never strands a record.
class ESR_Commander
{
 int Id;
 SCR_ChimeraCharacter Character; // weak
 SCR_AIGroup Group; // weak; he stays in it until the blast
 IEntity Grenade; // weak; the grenade at his feet
 ResourceName Prefab;
 bool MustCarry; // the setting when he decided
 bool OwnGrenade; // decided with, then placed from, his own inventory
 bool Live;
 float Fuse;
 float Started;
 string Ending;
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
 // The interrogation point follows the prisoner's head bone (ESR_InterrogationPoint).
 // Only while no bone can be read: rough face heights of the vanilla sit and of ACE's
 // standing surrender pose.
 static const float FACE_HEIGHT_SEATED = 0.9;
 static const float FACE_HEIGHT_ACE = 1.6;
 static const float FACE_FORWARD_FALLBACK = 0.15;
 static const int ACE_RELEASE_TICKS = 2;
 static const float INTERROGATE_RANGE = 4;
 static const float QUESTION_COOLDOWN = 2;
 static const ResourceName POINT_PREFAB = "{B412A163F3014DFE}Prefabs/EXPSR/ESR_InterrogationPoint.et";
 static const string CIVILIAN_FACTION = "CIV";
 static const string INTEL_MARKER_TEXT = "Intel (interrogation)";

 // Commander grenade. He crouches, takes 1-2.5 s, places the grenade at his feet and it
 // goes live one second later, as the vanilla scenario action arms a placed grenade
 // (SCR_ScenarioFrameworkActionSetGrenadeLive: "only after it is truly prepared"). The
 // vanilla frag fuse (TimerTriggerComponent TIMER 4) does the rest.
 static const int MAX_COMMANDERS = 16;
 static const float GRENADE_PREP_MIN = 1.0;
 static const float GRENADE_PREP_MAX = 2.5;
 static const int GRENADE_ARM_MS = 1000;
 static const float GRENADE_FUSE_FALLBACK = 4;
 static const float GRENADE_AFTER_BLAST = 4;
 static const float GRENADE_FORWARD = 0.25;
 static const float GRENADE_LIFT = 0.05;
 // The placed grenade has no physics to settle: the floor or ground under it is traced
 // from this height above his feet, and accepted only within this distance of them.
 static const float GRENADE_SNAP_HEIGHT = 0.5;
 static const float COMMANDER_MAX_AGE = 60;
 static const ResourceName GRENADE_RGD5 = "{645C73791ECA1698}Prefabs/Weapons/Grenades/Grenade_RGD5.et";
 static const ResourceName GRENADE_M67 = "{E8F00BF730225B00}Prefabs/Weapons/Grenades/Grenade_M67.et";

 protected static bool s_bListening;
 protected static bool s_bQueued;
 protected static bool s_bUpkeep;
 protected static int s_iCommanderSerial;
 protected static ref array<SCR_AIGroup> s_aQueue = {};
 protected static ref array<ref ESR_SquadRecord> s_aSquads = {};
 protected static ref array<ref ESR_Prisoner> s_aPrisoners = {};
 protected static ref array<ref ESR_Commander> s_aCommanders = {};

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
  // Commander timers are keyed by record id: a stale one finds no record and ends.
  PruneCommanders();
  if (!s_aQueue.IsEmpty()) ScheduleQueue();
  StartUpkeep();
  // Logs once per world whether ACE Captives' surrender or the vanilla sit is used.
  ESR_AceCaptives.Available();
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
  record.Threshold = Math.Clamp(ESR_Settings.Get(ESR_Settings.THRESHOLD) + ESR_Jitter(spread), 5, 100);
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
   if (!member.IsInVehicle() && !FindPrisoner(member) && !FindCommander(member)) candidates.Insert(member);
  }
  ESR_SquadRecord record = Squad(group, true);
  if (!record) return;
  if (total > record.Peak) record.Peak = total;
  if (record.Peak <= 0) return;
  // A squad whose cached or transitional state is still held (Unit Caching,
  // Garrison) is asleep: its casualties are rolled once it is plainly awake.
  if (EBG_CacheManager.IsCacheHeld(group)) { Trace(string.Format("squad %1 not rolled: its cache state is held", group)); return; }
  int casualties = record.Peak - able;
  // Rolls happen once per new casualty, never twice for the same loss.
  if (casualties <= record.LastCasualties) { Trace(string.Format("squad %1 not rolled: peak=%2 able=%3 casualties=%4 already rolled=%5", group, record.Peak, able, casualties, record.LastCasualties)); return; }
  record.LastCasualties = casualties;
  float ratio = casualties * 100.0 / record.Peak;
  Trace(string.Format("squad %1 peak=%2 able=%3 casualties=%4 ratio=%5 threshold=%6 candidates=%7", group, record.Peak, able, casualties, ratio, record.Threshold, candidates.Count()));
  if (ratio < record.Threshold) return;
  // Whether the squad breaks is decided above (module threshold); each soldier then rolls
  // his own effective chance: his override, else the squad's, else the module setting
  // (ESR_Overrides). An override is exact; the random factor spreads the module chance only.
  int squadSource;
  float chance = ESR_Overrides.Resolve(null, group, ESR_Overrides.SURRENDER, squadSource);
  float spread = ESR_Settings.Get(ESR_Settings.RANDOM);
  // Chosen before anyone leaves the squad.
  SCR_ChimeraCharacter commander = SquadCommander(group, candidates);
  int surrendered;
  int grenades;
  int ownChances;
  foreach (SCR_ChimeraCharacter candidate : candidates)
  {
   if (s_aPrisoners.Count() >= MAX_PRISONERS) break;
   float rolled = chance;
   int own = candidate.ESR_GetOverride(ESR_Overrides.SURRENDER);
   if (own >= 0)
   {
    rolled = own;
    ownChances++;
   }
   else if (squadSource == ESR_Overrides.SOURCE_MODULE) rolled = Math.Clamp(chance + ESR_Jitter(spread), 0, 100);
   if (rolled < 100 && Math.RandomFloat(0, 100) >= rolled) continue;
   // He would surrender; the squad leader may take a grenade instead.
   if (candidate == commander && ChoosesGrenade(candidate, group, record))
   {
    grenades++;
    continue;
   }
   if (Surrender(candidate, group)) surrendered++;
  }
  Trace(string.Format("squad %1 surrender chance=%2 source=%3 soldierOverrides=%4 surrendered=%5 of %6", group, chance, ESR_Overrides.SourceName(squadSource), ownChances, surrendered, candidates.Count()));
  if (grenades > 0) Trace(string.Format("squad %1: leader %2 took a grenade, %3 surrendered", group, commander, surrendered));
  // Prisoners now count as losses; they must not trigger another round by themselves.
  // The leader still counts as able until the blast; his death rolls the rest again.
  record.LastCasualties += surrendered;
 }

 //------------------------------------------------------------------------------------------------
 // Commander grenade (server)
 //------------------------------------------------------------------------------------------------
 // The squad leader: the group's leader agent when he can act (able, on foot, not a
 // prisoner); none when that leader is down or in a vehicle. A group without a leader
 // agent: its highest-ranking candidate, the first on a tie.
 static SCR_ChimeraCharacter SquadCommander(SCR_AIGroup group, notnull array<SCR_ChimeraCharacter> candidates)
 {
  if (!group || candidates.IsEmpty()) return null;
  IEntity leader = group.GetLeaderEntity();
  if (leader)
  {
   SCR_ChimeraCharacter leaderCharacter = SCR_ChimeraCharacter.Cast(leader);
   if (leaderCharacter && candidates.Contains(leaderCharacter)) return leaderCharacter;
   return null;
  }
  SCR_ChimeraCharacter best = null;
  int bestRank = -1;
  foreach (SCR_ChimeraCharacter candidate : candidates)
  {
   int rank = SCR_CharacterRankComponent.GetCharacterRank(candidate);
   if (rank == SCR_ECharacterRank.INVALID) rank = 0;
   if (best && rank <= bestRank) continue;
   best = candidate;
   bestRank = rank;
  }
  return best;
 }

 // Rolled once for a leader, only at the moment he would surrender. False: he surrenders.
 protected static bool ChoosesGrenade(SCR_ChimeraCharacter commander, SCR_AIGroup group, ESR_SquadRecord record)
 {
  int chance = ESR_Settings.Get(ESR_Settings.GRENADE);
  if (chance <= 0 || !record || record.RolledCommander == commander) return false;
  record.RolledCommander = commander;
  float roll = Math.RandomFloat(0, 100);
  Trace(string.Format("squad %1 leader %2 grenade roll=%3 chance=%4", group, commander, roll, chance));
  if (roll >= chance) return false;
  return StartGrenade(commander, group);
 }

 // Server: he stops fighting, crouches and, after a short pause, sets a fragmentation
 // grenade live at his own feet. He stays in his squad (the rest surrender as usual).
 // False leaves him to the normal surrender: player-controlled, prisoner, down, in a
 // vehicle, asleep in a cache, at the record cap, or without a grenade while the
 // "must carry" setting is on.
 static bool StartGrenade(SCR_ChimeraCharacter character, SCR_AIGroup group)
 {
  if (!Replication.IsServer() || !character || FindPrisoner(character) || FindCommander(character)) return false;
  PruneCommanders();
  if (s_aCommanders.Count() >= MAX_COMMANDERS) return false;
  if (!CommanderBlocked(character).IsEmpty()) return false;
  SCR_CharacterControllerComponent controller = SCR_CharacterControllerComponent.Cast(character.GetCharacterController());
  AIControlComponent control = AIControlComponent.Cast(character.FindComponent(AIControlComponent));
  if (!controller || !control) return false;
  bool mustCarry = ESR_Settings.Get(ESR_Settings.GRENADE_CARRY) != 0;
  ResourceName prefab = PrefabOf(FindFragGrenade(character));
  bool own = !prefab.IsEmpty();
  if (!own)
  {
   if (mustCarry)
   {
    Trace(string.Format("leader %1 carries no fragmentation grenade: he surrenders", character));
    return false;
   }
   prefab = SideGrenade(character);
   if (prefab.IsEmpty()) return false;
  }
  ESR_Commander commander = new ESR_Commander();
  s_iCommanderSerial++;
  commander.Id = s_iCommanderSerial;
  commander.Character = character;
  commander.Group = group;
  commander.Prefab = prefab;
  commander.MustCarry = mustCarry;
  commander.OwnGrenade = own;
  commander.Started = Now();
  s_aCommanders.Insert(commander);
  RetireSuppression(control.GetControlAIAgent());
  control.DeactivateAI();
  controller.SetStanceChange(ECharacterStanceChange.STANCECHANGE_TOCROUCH);
  int delay = Math.Round(Math.RandomFloat(GRENADE_PREP_MIN, GRENADE_PREP_MAX) * 1000);
  GetGame().GetCallqueue().CallLater(ESR_SurrenderManager.PlaceGrenade, delay, false, commander.Id);
  PrintFormat("[EXPBG SURRENDER] %1 (squad leader) takes a grenade instead of surrendering at %2 own=%3 prefab=%4 delayMs=%5 commanders=%6", character, character.GetOrigin(), own, prefab, delay, s_aCommanders.Count());
  return true;
 }

 // Why he can no longer act; empty when he can.
 protected static string CommanderBlocked(SCR_ChimeraCharacter character)
 {
  if (!character)
   return "deleted";
  CharacterControllerComponent controller = character.GetCharacterController();
  if (!controller || controller.GetLifeState() != ECharacterLifeState.ALIVE || controller.IsUnconscious())
   return "down";
  if (IsPlayerCharacter(character))
   return "player-controlled";
  if (character.IsInVehicle())
   return "in a vehicle";
  if (FindPrisoner(character))
   return "prisoner";
  AIControlComponent control = AIControlComponent.Cast(character.FindComponent(AIControlComponent));
  AIAgent agent;
  if (control) agent = control.GetControlAIAgent();
  // A cached soldier holds a permanent LOD pin (see Surrender): asleep.
  if (agent && agent.GetPermanentLOD() >= 0)
   return "asleep";
  return string.Empty;
 }

 // Timer 1: the grenade goes to his feet, taken from his inventory when he decided with
 // his own. Vanilla removal (as SCR_ScenarioFrameworkActionRemoveItemFromInventory), then
 // a fresh entity of the same prefab: a placed grenade, exactly where he crouches.
 protected static void PlaceGrenade(int id)
 {
  ESR_Commander commander = FindCommanderById(id);
  if (!commander) return;
  SCR_ChimeraCharacter character = commander.Character;
  string blocked = CommanderBlocked(character);
  if (!blocked.IsEmpty())
  {
   EndCommander(commander, blocked, true);
   return;
  }
  ResourceName prefab = commander.Prefab;
  if (commander.OwnGrenade)
  {
   IEntity carried = FindFragGrenade(character);
   ResourceName carriedPrefab = PrefabOf(carried);
   InventoryStorageManagerComponent inventory = InventoryStorageManagerComponent.Cast(character.FindComponent(InventoryStorageManagerComponent));
   if (!carriedPrefab.IsEmpty() && inventory && inventory.TryDeleteItem(carried))
   {
    prefab = carriedPrefab;
   }
   else if (commander.MustCarry)
   {
    // His grenade is gone: nothing left to use, so he surrenders after all.
    EndCommander(commander, "grenade gone", true);
    Surrender(character, commander.Group);
    return;
   }
   else
   {
    commander.OwnGrenade = false;
    prefab = SideGrenade(character);
   }
  }
  IEntity grenade = SpawnGrenade(prefab, character);
  if (!grenade)
  {
   EndCommander(commander, "grenade not spawned", true);
   return;
  }
  commander.Prefab = prefab;
  commander.Grenade = grenade;
  GetGame().GetCallqueue().CallLater(ESR_SurrenderManager.LiveGrenade, GRENADE_ARM_MS, false, id);
  Trace(string.Format("leader %1 placed %2 at %3 (%4 m) own=%5", character, prefab, grenade.GetOrigin(), vector.Distance(grenade.GetOrigin(), character.GetOrigin()), commander.OwnGrenade));
 }

 // A hand's width in front of his feet, where he crouches. No projectile simulation: a
 // placed grenade.
 protected static IEntity SpawnGrenade(ResourceName prefab, SCR_ChimeraCharacter character)
 {
  if (prefab.IsEmpty() || !character) return null;
  // Keep the loaded resource in a local before spawning.
  Resource resource = Resource.Load(prefab);
  if (!resource || !resource.IsValid()) return null;
  EntitySpawnParams spawn = new EntitySpawnParams();
  spawn.TransformMode = ETransformMode.WORLD;
  Math3D.MatrixIdentity4(spawn.Transform);
  spawn.Transform[3] = GrenadeSpot(character);
  IEntity grenade = GetGame().SpawnEntityPrefab(resource, GetGame().GetWorld(), spawn);
  if (!grenade) return null;
  BaseTriggerComponent trigger = BaseTriggerComponent.Cast(grenade.FindComponent(BaseTriggerComponent));
  if (!trigger)
  {
   SCR_EntityHelper.DeleteEntityAndChildren(grenade);
   return null;
  }
  // His own grenade: the kill is his, never a nearby player's.
  trigger.SetInstigator(Instigator.CreateInstigator(character));
  return grenade;
 }

 // On the floor or ground a hand's width in front of his feet: on a slope that is not his
 // feet's height, and the grenade does not settle by itself. Traced down from knee height
 // with the vanilla item snap (SCR_TerrainHelper.SnapToGeometry: terrain and building
 // floors, him excluded). No usable surface there (a wall, a steep bank): at his feet.
 protected static vector GrenadeSpot(notnull SCR_ChimeraCharacter character)
 {
  vector feet = character.GetOrigin();
  vector forward = character.GetWorldTransformAxis(2);
  forward[1] = 0;
  forward.Normalize();
  vector spot = feet + forward * GRENADE_FORWARD;
  array<IEntity> exclude = {};
  exclude.Insert(character);
  vector snapped;
  SCR_TerrainHelper.SnapToGeometry(snapped, spot + vector.Up * GRENADE_SNAP_HEIGHT, exclude, GetGame().GetWorld());
  if (Math.AbsFloat(snapped[1] - feet[1]) <= GRENADE_SNAP_HEIGHT)
   spot[1] = snapped[1];
  else
   spot = feet;
  return spot + vector.Up * GRENADE_LIFT;
 }

 // Timer 2: vanilla scripted arming of a placed grenade. BaseTriggerComponent.SetLive()
 // starts the prefab's own TimerTriggerComponent fuse; the native trigger then applies
 // the prefab's explosion and warhead (damage to everyone in range, ACE medical too).
 // Armed even if he was shot in the last second: the pin is already out.
 protected static void LiveGrenade(int id)
 {
  ESR_Commander commander = FindCommanderById(id);
  if (!commander) return;
  IEntity grenade = commander.Grenade;
  BaseTriggerComponent trigger;
  if (grenade) trigger = BaseTriggerComponent.Cast(grenade.FindComponent(BaseTriggerComponent));
  if (!trigger)
  {
   EndCommander(commander, "grenade gone", true);
   return;
  }
  // Never armed in someone's hands or inventory (picked up in the last second) or under
  // a player (a Game Master possessed him since): it stays a plain item.
  InventoryItemComponent item = InventoryItemComponent.Cast(grenade.FindComponent(InventoryItemComponent));
  if (grenade.GetParent() || (item && item.GetParentSlot()))
  {
   EndCommander(commander, "grenade picked up", true);
   return;
  }
  if (IsPlayerCharacter(commander.Character))
  {
   EndCommander(commander, "player-controlled", true);
   return;
  }
  trigger.SetLive();
  commander.Live = true;
  float fuse = GRENADE_FUSE_FALLBACK;
  TimerTriggerComponent timer = TimerTriggerComponent.Cast(trigger);
  if (timer && timer.GetTimer() > 0) fuse = timer.GetTimer();
  fuse = Math.Clamp(fuse + trigger.GetArmingTime(), 1, 15);
  commander.Fuse = fuse;
  int wait = Math.Round((fuse + GRENADE_AFTER_BLAST) * 1000);
  GetGame().GetCallqueue().CallLater(ESR_SurrenderManager.AfterBlast, wait, false, id);
  PrintFormat("[EXPBG SURRENDER] %1 (squad leader) set a grenade live at his feet at %2 prefab=%3 own=%4 fuse=%5", commander.Character, grenade.GetOrigin(), commander.Prefab, commander.OwnGrenade, fuse);
 }

 // Timer 3: after the fuse. Normally he is dead or down; a leader still standing (the
 // grenade was moved or deleted by a Game Master) gets his AI back and fights on.
 protected static void AfterBlast(int id)
 {
  ESR_Commander commander = FindCommanderById(id);
  if (!commander) return;
  SCR_ChimeraCharacter character = commander.Character;
  bool down = true;
  if (character)
  {
   CharacterControllerComponent controller = character.GetCharacterController();
   down = !controller || controller.GetLifeState() != ECharacterLifeState.ALIVE || controller.IsUnconscious();
  }
  if (down)
  {
   EndCommander(commander, "blast", false);
   return;
  }
  Print(string.Format("[EXPBG SURRENDER] %1 (squad leader) survived his grenade: AI resumed", character), LogLevel.WARNING);
  EndCommander(commander, "survived", true);
 }

 // Ends a record. wake: a living, conscious leader who is not player-controlled or
 // asleep in a cache gets his AI back.
 protected static void EndCommander(ESR_Commander commander, string reason, bool wake)
 {
  if (!commander) return;
  s_aCommanders.RemoveItem(commander);
  commander.Ending = reason;
  SCR_ChimeraCharacter character = commander.Character;
  if (wake && CommanderBlocked(character).IsEmpty())
  {
   AIControlComponent control = AIControlComponent.Cast(character.FindComponent(AIControlComponent));
   if (control && !control.IsAIActivated()) control.ActivateAI();
  }
  Trace(string.Format("leader %1 grenade ended (%2) commanders=%3", character, reason, s_aCommanders.Count()));
 }

 // Records whose leader is gone or that outlived every timer (a world change).
 protected static void PruneCommanders()
 {
  float now = Now();
  for (int i = s_aCommanders.Count() - 1; i >= 0; i--)
  {
   ESR_Commander commander = s_aCommanders[i];
   if (!commander || !commander.Character || now < commander.Started || now - commander.Started > COMMANDER_MAX_AGE) s_aCommanders.Remove(i);
  }
 }

 static ESR_Commander FindCommander(IEntity entity)
 {
  if (!entity) return null;
  foreach (ESR_Commander commander : s_aCommanders)
  {
   if (commander && commander.Character == entity) return commander;
  }
  return null;
 }

 static ESR_Commander FindCommanderById(int id)
 {
  foreach (ESR_Commander commander : s_aCommanders)
  {
   if (commander && commander.Id == id) return commander;
  }
  return null;
 }

 // Test and diagnostics access.
 static int CommanderCount()
 {
  return s_aCommanders.Count();
 }

 static ESR_Commander GetCommanderAt(int index)
 {
  if (!s_aCommanders.IsIndexValid(index)) return null;
  return s_aCommanders[index];
 }

 // A fragmentation grenade he carries (grenade slots, pouches, backpack): a weapon of
 // type WT_FRAGGRENADE with a trigger. Smoke grenades never count.
 static IEntity FindFragGrenade(IEntity character)
 {
  if (!character) return null;
  InventoryStorageManagerComponent inventory = InventoryStorageManagerComponent.Cast(character.FindComponent(InventoryStorageManagerComponent));
  if (!inventory) return null;
  array<IEntity> items = {};
  array<typename> query = {};
  query.Insert(BaseWeaponComponent);
  inventory.FindItemsWithComponents(items, query, EStoragePurpose.PURPOSE_ANY);
  foreach (IEntity item : items)
  {
   if (IsFragGrenade(item)) return item;
  }
  return null;
 }

 static bool IsFragGrenade(IEntity item)
 {
  if (!item) return false;
  BaseWeaponComponent weapon = BaseWeaponComponent.Cast(item.FindComponent(BaseWeaponComponent));
  if (!weapon || weapon.GetWeaponType() != EWeaponType.WT_FRAGGRENADE) return false;
  return item.FindComponent(BaseTriggerComponent) != null;
 }

 static ResourceName PrefabOf(IEntity entity)
 {
  if (!entity) return ResourceName.Empty;
  EntityPrefabData data = entity.GetPrefabData();
  if (!data) return ResourceName.Empty;
  return data.GetPrefabName();
 }

 // A vanilla fragmentation grenade of his side: M67 for US-like sides, RGD-5 for every
 // other (USSR, FIA, modded); the other one when the first does not load.
 static ResourceName SideGrenade(IEntity character)
 {
  ResourceName first = GRENADE_RGD5;
  ResourceName second = GRENADE_M67;
  SCR_ChimeraCharacter chimera = SCR_ChimeraCharacter.Cast(character);
  Faction side;
  if (chimera) side = chimera.GetFaction();
  if (side && IsUsSide(side.GetFactionKey()))
  {
   first = GRENADE_M67;
   second = GRENADE_RGD5;
  }
  Resource firstResource = Resource.Load(first);
  if (firstResource && firstResource.IsValid()) return first;
  Resource secondResource = Resource.Load(second);
  if (secondResource && secondResource.IsValid()) return second;
  return ResourceName.Empty;
 }

 static bool IsUsSide(string key)
 {
  if (key == "US") return true;
  return key.Contains("USA") || key.Contains("USMC");
 }

 //------------------------------------------------------------------------------------------------
 // Surrender (server)
 //------------------------------------------------------------------------------------------------
 static bool Surrender(SCR_ChimeraCharacter character, SCR_AIGroup group)
 {
  if (!Replication.IsServer() || !character || FindPrisoner(character) || FindCommander(character) || s_aPrisoners.Count() >= MAX_PRISONERS) return false;
  SCR_CharacterControllerComponent controller = SCR_CharacterControllerComponent.Cast(character.GetCharacterController());
  AIControlComponent control = AIControlComponent.Cast(character.FindComponent(AIControlComponent));
  if (!controller || !control || controller.GetLifeState() != ECharacterLifeState.ALIVE || character.IsInVehicle() || IsPlayerCharacter(character)) return false;
  // An AI frozen by a cache holds a permanent LOD pin (vanilla never sets one): asleep.
  AIAgent pinned = control.GetControlAIAgent();
  if (pinned && pinned.GetPermanentLOD() >= 0) return false;
  IEntity leader;
  if (group) leader = group.GetLeaderEntity();
  ESR_Prisoner prisoner = new ESR_Prisoner();
  prisoner.Character = character;
  prisoner.Group = group;
  // Before he leaves the squad: its interrogation overrides go with him.
  ESR_Overrides.CaptureSquad(prisoner, group);
  // Before he leaves the squad: the leader may change once he is gone.
  prisoner.Dossier = ESR_Dossier.Capture(character, leader);
  Faction faction = character.GetFaction();
  if (faction) prisoner.SideKey = faction.GetFactionKey();
  AIAgent agent = control.GetControlAIAgent();
  // Before his weapons, squad and AI go: he stops suppressing (see RetireSuppression).
  int retired = RetireSuppression(agent);
  int dropped = DropWeapons(character);
  if (group && agent && agent.GetParentGroup() == group) group.RemoveAgent(agent);
  // He left his squad for good: Unit Caching and Garrison forget him as a member
  // (never cached, respawned or deleted) and his squad caches and wakes as before.
  character.EBG_MarkLeftSquad();
  control.DeactivateAI();
  bool civilian = SetCivilian(character);
  controller.SetStanceChange(ECharacterStanceChange.STANCECHANGE_TOERECTED);
  s_aPrisoners.Insert(prisoner);
  SpawnPoint(prisoner);
  // Sit down once the weapon drop and stand-up have settled.
  GetGame().GetCallqueue().CallLater(ESR_SurrenderManager.ApplyPose, POSE_DELAY_MS, false, character);
  StartUpkeep();
  ESR_SurrenderModule.PublishPrisoners(s_aPrisoners.Count());
  PrintFormat("[EXPBG SURRENDER] %1 surrendered at %2 faction=%3 weaponsDropped=%4 civilian=%5 prisoners=%6 suppressRetired=%7", character, character.GetOrigin(), prisoner.SideKey, dropped, civilian, s_aPrisoners.Count(), retired);
  return true;
 }

 // A soldier taken mid-fight is usually suppressing: vanilla's group cluster behaviour
 // or EXPBG's warning shots. Leaving his squad fails only behaviours tied to a group
 // activity, and the cluster behaviour has none, so it would stay selected for a
 // disarmed civilian with no squad. Its suppress tree reads the volume through a
 // behaviour-tree variable that the behaviour alone keeps alive, and two vanilla nodes
 // raise a script exception when it is missing ("No suppression volume provided!").
 // Every suppress behaviour is failed through the native utility before his AI goes
 // off, and again whenever upkeep finds his AI back on, so it is never resumed.
 static int RetireSuppression(AIAgent agent)
 {
  if (!agent) return 0;
  SCR_AIUtilityComponent utility = SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent));
  if (!utility) return 0;
  array<ref AIActionBase> actions = {};
  utility.FindActionsOfInheritedType(SCR_AISuppressBehavior, actions);
  int retired;
  foreach (AIActionBase action : actions)
  {
   if (!action) continue;
   EAIActionState state = action.GetActionState();
   if (state == EAIActionState.COMPLETED || state == EAIActionState.FAILED) continue;
   action.Fail();
   retired++;
  }
  return retired;
 }

 // Vanilla turns a casualty's AI back on when he wakes up: a prisoner found with his AI
 // on loses any suppress behaviour first, then his AI goes off again.
 protected static void KeepPassive(AIControlComponent control)
 {
  if (!control || !control.IsAIActivated()) return;
  RetireSuppression(control.GetControlAIAgent());
  control.DeactivateAI();
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
 // replication, so late joiners see the pose too. With ACE Captives loaded he takes
 // ACE's own surrender state and hands-up pose instead (ESR_AceCaptives), never both.
 protected static void ApplyPose(SCR_ChimeraCharacter character)
 {
  ESR_Prisoner prisoner = FindPrisoner(character);
  if (!prisoner || !character) return;
  SCR_CharacterControllerComponent controller = SCR_CharacterControllerComponent.Cast(character.GetCharacterController());
  if (!controller || controller.GetLifeState() != ECharacterLifeState.ALIVE) return;
  if (ApplyAcePose(prisoner, controller)) return;
  if (controller.IsLoitering()) return;
  if (!controller.CanPlayLoiterAnimation(ELoiteringType.SIT)) return;
  prisoner.PoseTries++;
  vector anchor[4];
  Math3D.MatrixIdentity4(anchor);
  controller.StartLoitering(null, ELoiteringType.SIT, true, true, false, anchor, true);
  Trace(string.Format("%1 pose requested try=%2", character, prisoner.PoseTries));
 }

 // ACE Captives loaded: ACE's surrender state and pose. True while ACE holds the pose
 // or is taking it; false leaves the prisoner to the vanilla sit.
 protected static bool ApplyAcePose(ESR_Prisoner prisoner, SCR_CharacterControllerComponent controller)
 {
  SCR_ChimeraCharacter character = prisoner.Character;
  if (prisoner.AceFailed || !ESR_AceCaptives.Available()) return false;
  // Possessed by a Game Master: neither ACE's pose nor the vanilla sit; upkeep resumes later.
  if (IsPlayerCharacter(character)) return true;
  bool surrendered, captive, carried;
  if (!ESR_AceCaptives.ReadState(character, surrendered, captive, carried)) return AceFallback(prisoner, "state unreadable");
  if (captive || carried || (surrendered && ESR_AceCaptives.InHelper(character)))
  {
   // Surrendered in ACE's helper, or tied or carried by a player: ACE owns the pose.
   SetAceMode(prisoner, true);
   prisoner.AceTries = 0;
   return true;
  }
  // Put into a vehicle by a Game Master, or leaving one: wait, upkeep tries again.
  if (character.IsInVehicle() || ESR_AceCaptives.IsGettingOut(character)) return true;
  if (prisoner.AceTries >= MAX_POSE_TRIES) return AceFallback(prisoner, "no helper");
  prisoner.AceTries++;
  // ACE's helper request is pending: one helper at a time.
  if (ESR_AceCaptives.IsGettingIn(character)) return true;
  if (controller.IsLoitering())
  {
   // A loiter ends first; ACE takes over on the next try.
   controller.StopLoitering(false);
   return true;
  }
  // Also repairs ACE's flag without its helper (no helper was spawned): no helper is
  // pending here, so ACE spawns exactly one.
  if (!ESR_AceCaptives.SetSurrender(character, true)) return AceFallback(prisoner, "surrender refused");
  SetAceMode(prisoner, true);
  // ACE's helper sets faction key "CIV"; re-apply ours (missions without "CIV").
  SetCivilian(character);
  Trace(string.Format("%1 ACE surrender requested try=%2 repair=%3", character, prisoner.AceTries, surrendered));
  return true;
 }

 // ACE did not take him: clear a half-set ACE flag and keep the vanilla sit for good.
 protected static bool AceFallback(ESR_Prisoner prisoner, string reason)
 {
  SCR_ChimeraCharacter character = prisoner.Character;
  bool surrendered, captive, carried;
  if (ESR_AceCaptives.ReadState(character, surrendered, captive, carried) && surrendered && !ESR_AceCaptives.InHelper(character)) ESR_AceCaptives.SetSurrender(character, false);
  prisoner.AceFailed = true;
  SetAceMode(prisoner, false);
  Print(string.Format("[EXPBG SURRENDER] %1 ACE surrender unavailable (%2): vanilla sit", character, reason), LogLevel.WARNING);
  return false;
 }

 protected static void SetAceMode(ESR_Prisoner prisoner, bool ace)
 {
  if (prisoner.AceMode == ace) return;
  prisoner.AceMode = ace;
  prisoner.AceIdle = 0;
  // The interrogation point follows his face into the new pose by itself.
 }

 // ACE-held prisoner, alive and awake. ACE owns the pose while he is surrendered in its
 // helper, tied or carried. A prisoner standing free with every ACE state cleared was
 // released through ACE (a Game Master's "Toggle surrender", a player's "Release
 // prisoner"); two ticks in a row, so a wake-up being re-asserted is not misread.
 // False: release him.
 protected static bool AceUpkeep(ESR_Prisoner prisoner)
 {
  SCR_ChimeraCharacter character = prisoner.Character;
  bool surrendered, captive, carried;
  if (!ESR_AceCaptives.ReadState(character, surrendered, captive, carried))
  {
   AceFallback(prisoner, "state unreadable");
   return true;
  }
  if (captive || carried || (surrendered && ESR_AceCaptives.InHelper(character)))
  {
   prisoner.AceIdle = 0;
   prisoner.AceTries = 0;
   return true;
  }
  if (ESR_AceCaptives.IsGettingOut(character))
  {
   // Leaving ACE's helper: judge the outcome on the next tick.
   prisoner.AceIdle = 0;
   return true;
  }
  if (surrendered || character.IsInVehicle() || ESR_AceCaptives.IsGettingIn(character))
  {
   // Moving in, ACE's flag without its helper, or in a vehicle: not a release. Leaving
   // ACE's helper (a Game Master moving him into a vehicle) restored his military faction.
   prisoner.AceIdle = 0;
   KeepCivilian(character);
   ApplyPose(character);
   return true;
  }
  prisoner.AceIdle++;
  return prisoner.AceIdle < ACE_RELEASE_TICKS;
 }

 // ACE restores the default (military) faction whenever its helper ends; an ACE-held
 // prisoner who went down or was moved into a vehicle stays civilian.
 protected static void KeepCivilian(SCR_ChimeraCharacter character)
 {
  SCR_Faction faction = SCR_Faction.Cast(character.GetFaction());
  if (faction && faction.IsMilitary()) SetCivilian(character);
 }

 protected static void Reassert(IEntity entity)
 {
  ESR_Prisoner prisoner = FindPrisoner(entity);
  if (!prisoner || !prisoner.Character || IsPlayerCharacter(prisoner.Character)) return;
  AIControlComponent control = AIControlComponent.Cast(prisoner.Character.FindComponent(AIControlComponent));
  KeepPassive(control);
  // ACE ends its helper on every life-state change: surrender him again (a tied
  // captive is re-tied by ACE itself).
  if (prisoner.AceMode)
  {
   prisoner.AceIdle = 0;
   SetCivilian(prisoner.Character);
  }
  ApplyPose(prisoner.Character);
 }

 // Where the interaction point belongs: just in front of the prisoner's face, clear of
 // the medical contexts on his torso and limbs. From his head bone in any pose; the
 // pose's rough face height only while the bone cannot be read.
 static vector PointPosition(ESR_Prisoner prisoner)
 {
  vector face;
  if (ESR_InterrogationPoint.FacePosition(prisoner.Character, face)) return face;
  vector origin = prisoner.Character.GetOrigin();
  vector forward = prisoner.Character.GetWorldTransformAxis(2);
  forward[1] = 0;
  forward.Normalize();
  float height = FACE_HEIGHT_SEATED;
  if (prisoner.AceMode) height = FACE_HEIGHT_ACE;
  return origin + vector.Up * height + forward * FACE_FORWARD_FALLBACK;
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
  spawn.Transform[3] = PointPosition(prisoner);
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
   if (controller.GetLifeState() != ECharacterLifeState.ALIVE || controller.IsUnconscious() || IsPlayerCharacter(prisoner.Character))
   {
    if (prisoner.AceMode && !IsPlayerCharacter(prisoner.Character)) KeepCivilian(prisoner.Character);
    continue;
   }
   AIControlComponent control = AIControlComponent.Cast(prisoner.Character.FindComponent(AIControlComponent));
   KeepPassive(control);
   SCR_CharacterControllerComponent scripted = SCR_CharacterControllerComponent.Cast(controller);
   if (prisoner.AceMode)
   {
    // Released through ACE: he is no longer our prisoner (AI stays off).
    if (!AceUpkeep(prisoner)) { ReleaseAt(i, "ace-release"); continue; }
   }
   else if (scripted && !scripted.IsLoitering() && prisoner.PoseTries < MAX_POSE_TRIES) ApplyPose(prisoner.Character);
   // The point follows his face on every machine (Game Master moves included); a point
   // that lost him is spawned again.
   vector face = PointPosition(prisoner);
   if (!prisoner.Point) SpawnPoint(prisoner);
   else if (vector.DistanceSq(prisoner.Point.GetOrigin(), face) > 1)
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
    // His own override, else his former squad's (captured at surrender), else the module.
    float reveal = ESR_Overrides.ForPrisoner(prisoner, ESR_Overrides.REVEAL, ESR_Settings.Get(ESR_Settings.REVEAL));
    float identity = ESR_Overrides.ForPrisoner(prisoner, ESR_Overrides.IDENTITY, ESR_Settings.Get(ESR_Settings.IDENTITY));
    float roll = Math.RandomFloat(0, 100);
    if (roll < reveal) outcome = Reveal(prisoner, user, playerId);
    else if (roll < Math.Min(100, reveal + identity)) outcome = OUTCOME_IDENTITY;
    else outcome = OUTCOME_REFUSED;
    // Once he talks the answer is fixed: asking again repeats it.
    if (outcome != OUTCOME_REFUSED)
    {
     prisoner.Outcome = outcome;
     // Intel items: a roll of its own after the answer above, so its odds are unchanged.
     RevealIntel(prisoner, user, playerId);
    }
   }
  }
  int left = attempts - prisoner.Attempts;
  if (left < 0) left = 0;
  point.SendResult(playerId, outcome, prisoner.RevealCount, prisoner.RevealDistance, prisoner.RevealBearing, left, prisoner.IntelDistances, prisoner.IntelBearings);
  Trace(string.Format("interrogation of %1 by player %2: outcome=%3 attempts=%4/%5 marker=%6 intel=%7 chances: %8", prisoner.Character, playerId, outcome, prisoner.Attempts, attempts, prisoner.MarkerId, prisoner.IntelDistances.Count(), ESR_Overrides.DescribePrisoner(prisoner)));
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
  if (!side)
  {
   Trace(string.Format("reveal by %1: no faction for side key '%2'", prisoner.Character, prisoner.SideKey));
   return OUTCOME_NO_SQUAD;
  }
  int radius = ESR_Settings.Get(ESR_Settings.RADIUS);
  ESR_SquadQuery query = new ESR_SquadQuery(side, prisoner.Group);
  query.Collect(origin, radius);
  vector center;
  int count;
  bool found = query.Nearest(origin, center, count);
  Trace(string.Format("reveal by %1 side=%2 radius=%3: agents=%4 groups=%5 squads=%6 found=%7 count=%8 at %9", prisoner.Character, prisoner.SideKey, radius, query.AgentCount(), query.GroupCount(), query.SquadCount(), found, count, center));
  if (!found) return OUTCOME_NO_SQUAD;
  prisoner.RevealDistance = ReportedDistance(origin, center);
  prisoner.RevealBearing = ReportedBearing(origin, center);
  prisoner.RevealCount = count;
  prisoner.MarkerId = PlaceMarker(center, user, playerId, count);
  return OUTCOME_REVEAL;
 }

 // What a prisoner says about a place: its map distance in 25 m steps.
 static int ReportedDistance(vector from, vector to)
 {
  int rounded = Math.Round(vector.DistanceXZ(from, to) / 25);
  return rounded * 25;
 }

 // ...and its compass sector: 0 = north, clockwise in 45-degree steps.
 static int ReportedBearing(vector from, vector to)
 {
  vector offset = to - from;
  float angle = Math.Atan2(offset[0], offset[2]) * Math.RAD2DEG;
  if (angle < 0) angle += 360;
  int sector = Math.Round(angle / 45);
  return sector % 8;
 }

 // Server, once per prisoner, with his first answer other than a refusal: the intel chance
 // is rolled on its own (the squad and identity roll before it is untouched). On a hit he
 // points out the nearest unclaimed intel items within the reveal search radius (one
 // bounded ESR_IntelQuery pass); each gets a map marker like a revealed squad's. The result
 // is kept on the record, so asking again repeats it without new markers.
 protected static void RevealIntel(ESR_Prisoner prisoner, IEntity user, int playerId)
 {
  if (prisoner.IntelRolled || !prisoner.Character) return;
  prisoner.IntelRolled = true;
  // His own override, else his former squad's (captured at surrender), else the module.
  int chance = ESR_Overrides.ForPrisoner(prisoner, ESR_Overrides.INTEL, ESR_Settings.Get(ESR_Settings.INTEL));
  if (chance <= 0) return;
  if (chance < 100 && Math.RandomFloat(0, 100) >= chance)
  {
   Trace(string.Format("intel by %1: chance=%2 missed", prisoner.Character, chance));
   return;
  }
  vector origin = prisoner.Character.GetOrigin();
  int radius = ESR_Settings.Get(ESR_Settings.RADIUS);
  ESR_IntelQuery query = new ESR_IntelQuery();
  query.Collect(origin, radius, prisoner.Character);
  int found = query.Count();
  string placed;
  for (int i = 0; i < found; i++)
  {
   vector position = query.PositionAt(i);
   int marker = PlaceIntelMarker(position, user, playerId);
   prisoner.IntelDistances.Insert(ReportedDistance(origin, position));
   prisoner.IntelBearings.Insert(ReportedBearing(origin, position));
   prisoner.IntelMarkers.Insert(marker);
   placed += string.Format(" %1@%2", marker, position);
  }
  Trace(string.Format("intel by %1 chance=%2 radius=%3: registered=%4 scanned=%5 eligible=%6 pointed=%7 markers:%8", prisoner.Character, chance, radius, query.RegisteredCount(), query.ScannedCount(), query.EligibleCount(), found, placed));
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

 // The revealed squad: an enemy infantry symbol.
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
  return PublishMarker(markers, marker, center, string.Format("Prisoner intel: squad of %1", count), user, playerId);
 }

 // An intel item he points out: a plain placed marker, the kind players place themselves.
 protected static int PlaceIntelMarker(vector position, IEntity user, int playerId)
 {
  SCR_MapMarkerManagerComponent markers = SCR_MapMarkerManagerComponent.GetInstance();
  if (!markers) return -1;
  SCR_MapMarkerBase marker = new SCR_MapMarkerBase();
  marker.SetType(SCR_EMapMarkerType.PLACED_CUSTOM);
  return PublishMarker(markers, marker, position, INTEL_MARKER_TEXT, user, playerId);
 }

 // A player-owned static marker, as if the interrogator had placed it: his faction sees
 // it, he can delete it from the map, and it is part of the marker manager's JIP state.
 // The marker lifetime setting removes it on the server.
 protected static int PublishMarker(notnull SCR_MapMarkerManagerComponent markers, notnull SCR_MapMarkerBase marker, vector center, string text, IEntity user, int playerId)
 {
  int x = center[0];
  int z = center[2];
  marker.SetWorldPos(x, z);
  marker.SetCustomText(text);
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

// Finds the squad a prisoner can give away: the nearest living squad of his side (or of a
// friendly military side), not his own and without players, with a living soldier inside
// the radius; his own remnants only when nothing else is near. One pass per answer that
// reveals. Squads are read from the AI world's agent list (soldiers and groups), each
// group once, so props, wrecks, vehicles, dropped weapons and the gear every soldier and
// civilian carries never count against the bound. 0.1.8 used a sphere query over dynamic
// entities instead; it visits attached gear too, in no particular order, and stopped after
// 2048 entities, so in a busy town it never reached a squad 180 m away ("nobody else").
// Squads in Full cache (Unit Caching or Garrison) have no soldiers in the world while they
// sleep, and neither module offers a read-only API for them (only internal records), so
// they are never revealed.
// Squads cached in Simulation (Unit Caching, Garrison) keep their soldiers and count.
class ESR_SquadQuery
{
 // Agents read per answer: far beyond any AI count a server can run.
 static const int MAX_AGENTS = 16384;
 protected Faction m_Side;
 protected SCR_AIGroup m_Own;
 protected bool m_bFound;
 protected float m_fBest;
 protected vector m_vCenter;
 protected int m_iCount;
 protected int m_iAgents;
 protected int m_iGroups;
 protected int m_iSquads;

 void ESR_SquadQuery(Faction side, SCR_AIGroup own)
 {
  m_Side = side;
  m_Own = own;
 }

 // Diagnostics: agents read, distinct groups seen, qualifying squads.
 int AgentCount()
 {
  return m_iAgents;
 }

 int GroupCount()
 {
  return m_iGroups;
 }

 int SquadCount()
 {
  return m_iSquads;
 }

 void Collect(vector origin, float radius)
 {
  m_bFound = false;
  m_iAgents = 0;
  m_iGroups = 0;
  m_iSquads = 0;
  if (!m_Side || !GetGame()) return;
  AIWorld world = GetGame().GetAIWorld();
  if (!world) return;
  array<AIAgent> agents = {};
  world.GetAIAgents(agents);
  int total = agents.Count();
  if (total > MAX_AGENTS) total = MAX_AGENTS;
  float radiusSq = radius * radius;
  set<SCR_AIGroup> visited = new set<SCR_AIGroup>();
  for (int i = 0; i < total; i++)
  {
   AIAgent agent = agents[i];
   m_iAgents++;
   if (!agent) continue;
   // The list holds groups and soldiers alike; a soldier stands for his group.
   SCR_AIGroup group = SCR_AIGroup.Cast(agent);
   if (!group) group = SCR_AIGroup.Cast(agent.GetParentGroup());
   if (!group || group == m_Own || visited.Contains(group)) continue;
   visited.Insert(group);
   m_iGroups++;
   if (group.GetPlayerCount(true) > 0) continue;
   vector center = vector.Zero;
   float nearestSq = 0;
   Faction memberFaction = null;
   int living = Living(group, origin, center, nearestSq, memberFaction);
   if (living <= 0 || nearestSq > radiusSq) continue;
   // A group without a faction key of its own takes its soldiers' side.
   Faction faction = group.GetFaction();
   if (!faction) faction = memberFaction;
   if (!IsAlly(faction)) continue;
   m_iSquads++;
   float distance = vector.DistanceSq(origin, center);
   if (m_bFound && distance >= m_fBest) continue;
   m_bFound = true;
   m_fBest = distance;
   m_vCenter = center;
   m_iCount = living;
  }
 }

 // His side, or a friendly side; a friendly non-military side (civilians) is no squad.
 protected bool IsAlly(Faction faction)
 {
  if (!faction) return false;
  if (faction == m_Side) return true;
  if (!m_Side.IsFactionFriendly(faction)) return false;
  SCR_Faction scripted = SCR_Faction.Cast(faction);
  if (scripted && !scripted.IsMilitary()) return false;
  return true;
 }

 // Nearest squad other than his own; his own remnants only when nothing else is near.
 bool Nearest(vector origin, out vector center, out int count)
 {
  if (m_bFound)
  {
   center = m_vCenter;
   count = m_iCount;
   return true;
  }
  if (!m_Own) return false;
  vector ownCenter;
  float ownNearestSq;
  Faction ownFaction;
  int ownLiving = Living(m_Own, origin, ownCenter, ownNearestSq, ownFaction);
  if (ownLiving <= 0) return false;
  center = ownCenter;
  count = ownLiving;
  return true;
 }

 protected static int Living(SCR_AIGroup group, vector origin, out vector center, out float nearestSq, out Faction faction)
 {
  array<AIAgent> agents = {};
  group.GetAgents(agents);
  int living = 0;
  vector sum = vector.Zero;
  center = vector.Zero;
  nearestSq = float.MAX;
  faction = null;
  foreach (AIAgent agent : agents)
  {
   if (!agent) continue;
   SCR_ChimeraCharacter member = SCR_ChimeraCharacter.Cast(agent.GetControlledEntity());
   if (!member || !member.GetCharacterController() || member.GetCharacterController().GetLifeState() == ECharacterLifeState.DEAD) continue;
   vector position = member.GetOrigin();
   living++;
   sum = sum + position;
   float distanceSq = vector.DistanceSq(origin, position);
   if (distanceSq < nearestSq) nearestSq = distanceSq;
   if (!faction) faction = member.GetFaction();
  }
  if (living > 0) center = sum * (1.0 / living);
  return living;
 }
}

// Finds the intel items a prisoner can point out: EXPBG Intel Items (laptops, tablets,
// notebooks, manuals) within the reveal search radius (map distance), nearest first, at most
// MAX_ITEMS. One pass per answer that rolls intel, over Intel Items' own registry of the
// items in play (EII_IntelComponent.GetRegistered, capped at MAX_SCAN), so the work grows
// with the number of intel items, never with everything else in the radius.
// Unclaimed, from the Intel Items data model: it keeps no read or picked-up flag for
// notebooks and manuals; its one spent state is a computer item's startup token (laptop,
// tablet), consumed when a player first reads it or picks it up (HasStarted). An item counts
// unless that token is spent or a player carries it: any holder up its hierarchy (vest,
// backpack, the seat of a vehicle) is a player-controlled or possessed character. Items
// lying loose, in a crate or vehicle, or on a body count, at their outermost holder's
// position. Items the prisoner carries himself are left out: searching him finds them.
// EXPBG server racks and USB drives are not intel items: a rack is fixed scenery that
// downloads never use up, and a drive holds only intel a player already downloaded.
class ESR_IntelQuery
{
 static const int MAX_ITEMS = 3;
 // Registered items read per answer: far beyond what a Game Master places.
 static const int MAX_SCAN = 4096;
 // Holder levels walked per item (item, vest, character, vehicle seat, vehicle, ...).
 static const int MAX_DEPTH = 8;
 protected ref array<vector> m_aPositions = {};
 protected ref array<float> m_aDistances = {};
 protected int m_iRegistered;
 protected int m_iScanned;
 protected int m_iEligible;

 // Diagnostics: items registered, items read, unclaimed items inside the radius.
 int RegisteredCount()
 {
  return m_iRegistered;
 }

 int ScannedCount()
 {
  return m_iScanned;
 }

 int EligibleCount()
 {
  return m_iEligible;
 }

 int Count()
 {
  return m_aPositions.Count();
 }

 vector PositionAt(int index)
 {
  if (!m_aPositions.IsIndexValid(index)) return vector.Zero;
  return m_aPositions[index];
 }

 void Collect(vector origin, float radius, IEntity prisoner)
 {
  m_aPositions.Clear();
  m_aDistances.Clear();
  m_iScanned = 0;
  m_iEligible = 0;
  array<IEntity> items = {};
  m_iRegistered = EII_IntelComponent.GetRegistered(items);
  int total = items.Count();
  if (total > MAX_SCAN) total = MAX_SCAN;
  float radiusSq = radius * radius;
  for (int i = 0; i < total; i++)
  {
   IEntity item = items[i];
   m_iScanned++;
   if (!item) continue;
   EII_IntelComponent intel = EII_IntelComponent.Cast(item.FindComponent(EII_IntelComponent));
   if (!intel || intel.HasStarted()) continue;
   IEntity holder = Holder(item, prisoner);
   if (!holder) continue;
   vector position = holder.GetOrigin();
   float distanceSq = vector.DistanceSqXZ(origin, position);
   if (distanceSq > radiusSq) continue;
   m_iEligible++;
   // Keep the MAX_ITEMS nearest, in order.
   int slot = m_aDistances.Count();
   while (slot > 0 && m_aDistances[slot - 1] > distanceSq) slot--;
   if (slot >= MAX_ITEMS) continue;
   m_aDistances.InsertAt(distanceSq, slot);
   m_aPositions.InsertAt(position, slot);
   if (m_aDistances.Count() > MAX_ITEMS)
   {
    m_aDistances.RemoveOrdered(MAX_ITEMS);
    m_aPositions.RemoveOrdered(MAX_ITEMS);
   }
  }
 }

 // The item's outermost holder (the item itself while it lies loose); null while a player
 // or the prisoner carries it, at any depth.
 protected static IEntity Holder(notnull IEntity item, IEntity prisoner)
 {
  IEntity holder = item;
  IEntity parent = item.GetParent();
  int depth = 0;
  while (parent && depth < MAX_DEPTH)
  {
   if (parent == prisoner) return null;
   if (ChimeraCharacter.Cast(parent) && ESR_SurrenderManager.IsPlayerCharacter(parent)) return null;
   holder = parent;
   parent = parent.GetParent();
   depth++;
  }
  return holder;
 }
}

// Math.RandomFloat rejects an empty range; a zero random factor means no jitter.
float ESR_Jitter(float spread)
{
 if (spread <= 0) return 0;
 return Math.RandomFloat(-spread, spread);
}
