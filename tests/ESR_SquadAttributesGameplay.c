// TEST ONLY. EXPBG AI Global Skills and AI Surrender squad attributes, runtime effect
// (dedicated server, no players, no GM UI).
// pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -FixturePath tests/ESR_SquadAttributesGameplay.c -TimeoutSeconds 540 -OrchestratorSlotGranted -ExpectResult '\[ESR SQUAD ATTR RESULT\] checks=[1-9]\d* failures=0 surrender100=5 surrender0=0 holdShots=0 farBurst=1 provoked=1 warning=1 lethal=1 rearm=1 leak=0 sync=1 cached=1 reason=complete'
// The runner copies this file to EXPG_GarrisonGameplay.c; the class names are fixed. Judge
// the run by its one [ESR SQUAD ATTR RESULT] line (-ExpectResult also rejects script errors).
// Every EXPBG attribute value is written by the production attribute class taken from the
// merged Game Master attribute list (Configs/Editor/AttributeLists/Edit.conf), on the server,
// as a Game Master save does: WriteVariable with the editing Game Master's player id. The
// only stand-in is the Game Master check itself (EGS_Attributes.IsEditingGameMaster, seam at
// the end: no editor exists without a player). AI Surrender attributes take the session-load
// contract (null manager, player -1): their Game Master check is inline and needs a real
// editor; both branches end in the same ESR_Overrides.SetGroup.
// A second seam makes one standalone US rifleman count as a player for Warning Shots First
// (EGS_Manager.IsPlayerTarget, the entry point production calls, and IsPlayerControlled).
// Enemies that must not shoot are vanilla HOLD_FIRE groups. Fire team E of case R must stay
// whole and unhurt until R's last phase (while case S runs, one E casualty would make the
// rest of E surrender), so no squad near it ever fires at will: S100 and S0 hold fire from
// spawn, V starts in vanilla return fire, B never leaves S0 in fire at will past one frame,
// and Fire on Sight squad F fights at its own site at least SITE_SEPARATION away and holds
// fire once its case ends.
// Fight sites (R/E, W/stand-in, F/E2): the driver point is a town centre (houses between R
// and E), and the first native run's fixed sites gave no fight anywhere (R never targeted E
// in 30 s, E at fire at will never hit R in 60 s, F never fired at E2, W's warning burst
// and lethal fire put no round out, and the stand-in stayed unhurt). The three sites are
// now searched at run time on rings 0.7-4.2 km around the driver point (FindSites): dry,
// level ground with nothing standing where the formations spawn, and every sight and fire
// line from the watcher's wedge to the enemy's 60 m north clear of terrain, buildings,
// trees and rocks at kneeling and standing eye height; the sites are SITE_SEPARATION apart.
// The fights run at noon without fog or rain (Daylight; the world's own hour is printed).
// Shot counters count rounds and thrown grenades alike.
// Cases (run side by side, at separate sites of GM_Eden):
//  S  AI Surrender squad chance: module chance 100%, threshold 10%, random 0. Squad S100 has
//     "Surrender chance (%)" 100%, squad S0 0%. Nobody surrenders before a casualty. One
//     casualty each: the five able soldiers of S100 surrender, nobody of S0 does.
//  B  Vanilla "Set combat mode" and the EXPBG squad ROE agree (S0 after case S, one save per
//     step): Game Master RF over S0's own hold fire, then vanilla return fire -> EXPBG Exempt,
//     return fire kept; one save with vanilla hold fire then EXPBG Return Fire Only -> RF over
//     hold fire; one save with EXPBG Warning Shots First then vanilla fire at will -> WS over
//     fire at will; a mode changed outside EXPBG is put back by a Game Master write of the
//     EXPBG ROE (the same value, written directly: the GM dialog sends only changed values);
//     Exempt restores the squad's own mode. The vanilla attribute comes before the EXPBG one
//     in the list. The first failed step ends B and puts S0 on Exempt and hold fire.
//  R  Return Fire Only (squad R) facing a visible HOLD_FIRE US fire team E at 60 m: no round
//     for 30 s; an E burst (a member without a grenade or rocket launcher) whose path passes
//     about 9 m beside R does not set R off (vanilla flags shots within 13 m of the leader);
//     R stays quiet until E opens fire, is provoked only after E's first round from then on
//     (fresh counters: the far burst does not count) and answers fire.
//  W  Warning Shots First (squad W) facing the stand-in player at 60 m: 1-3 warning rounds,
//     the stand-in unhurt, lethal 5 s later, squad never provoked; once the stand-in is gone,
//     the squad re-arms (RETURN_FIRE, armed) after the contact.
//  F  Soldier ROE leak: a Fire on Sight squad F engages a HOLD_FIRE fire team E2; its holdout
//     with his own Return Fire Only fires no round and never runs the squad's suppressive
//     fire. Then F is put on Exempt and hold fire.
//  V  Unit Caching Full cycle: squad V (vanilla return fire) gets vanilla HOLD_FIRE, then
//     EXPBG Return Fire Only; after Full caching and the wake the recreated squad has Return
//     Fire Only with HOLD_FIRE as its own mode, and Exempt (vanilla) restores HOLD_FIRE.
// Not covered: the GM dialog UI, clients and JIP, a vehicle target for warning shots, RO_AI
// loaded (the runner loads this pack and its dependencies only), native and CDF save/load.
class ESRAttrShots : Managed
{
 IEntity Owner;
 int Count;
 float First = -1;

 // OnProjectileShot and OnGrenadeThrown (same signature).
 void OnShot(int playerID, BaseWeaponComponent weapon, IEntity entity)
 {
  Count++;
  if (First < 0 && GetGame() && GetGame().GetWorld()) First = GetGame().GetWorld().GetWorldTime() * 0.001;
 }
}

class EXPG_GarrisonGameplayClass : GenericEntityClass {}
class EXPG_GarrisonGameplay : GenericEntity
{
 static const float FIXTURE_SECONDS = 360;
 static const int STAND_IN_GM = 4242;
 static const ResourceName ATTRIBUTE_LIST = "{F3D6C6D25642352C}Configs/Editor/AttributeLists/Edit.conf";
 static const ResourceName SURRENDER_MODULE = "{7F2E668385984EA1}PrefabsEditable/EXPSR/ESR_SurrenderModule.et";
 static const ResourceName SKILLS_MODULE = "{48F68574BBAED1D5}PrefabsEditable/EXPBG/EGS_AIGlobalSkills.et";
 static const ResourceName USSR_SQUAD = "{E552DABF3636C2AD}Prefabs/Groups/OPFOR/Group_USSR_RifleSquad.et";
 static const ResourceName US_TEAM = "{84E5BBAB25EA23E5}Prefabs/Groups/BLUFOR/Group_US_FireTeam.et";
 static const ResourceName US_RIFLEMAN = "{26A9756790131354}Prefabs/Characters/Factions/BLUFOR/US_Army/Character_US_Rifleman.et";
 static const ResourceName ZONE = "{7E1080ED8F0633FD}PrefabsEditable/EXPBG/EBG_CacheZone.et";
 static const int SQUAD_SIZE = 6;
 static const int TEAM_SIZE = 4;
 // Vanilla "Set combat mode" entries (vanilla Edit.conf): hold fire, return fire, fire at will.
 static const int ENTRY_HOLD = 0;
 static const int ENTRY_RETURN = 1;
 static const int ENTRY_FIRE = 2;
 // Fight site search (FindSites): rings around the driver point, nearest first, one
 // candidate about every SITE_ARC metres of a ring.
 static const float SITE_RING_FIRST = 700;
 static const float SITE_RING_STEP = 150;
 static const int SITE_RINGS = 24;
 static const float SITE_ARC = 150;
 // No fire-at-will squad of one case ever sees another case's enemy (rifles, daylight).
 static const float SITE_SEPARATION = 1500;
 // Ground within this height of the site centre over the whole fight box.
 static const float SITE_LEVEL = 5;
 static const float SITE_EDGE = 500;
 static const float SITE_FAR_EDGE = 12300;
 static const float SITE_MIN_HEIGHT = 5;
 static ref array<vector> s_Presence;
 static IEntity s_StandIn;

 vector Origin = "4773.46 0 7094.57";
 vector EnemyOffset = "0 0 60";
 vector SiteS = "150 0 0";
 vector SiteS0 = "150 0 50";
 vector SiteV = "150 0 -90";
 // World positions of the fight sites (FindSites): the watcher squad here, its enemy 60 m
 // north (EnemyOffset).
 vector SiteR;
 vector SiteW;
 vector SiteF;
 int SitesFound;
 int SitesTried;
 vector ModuleOffset = "0 0 20";
 vector SkillsOffset = "0 0 25";

 ref SCR_EditorAttributeList Attributes;
 SCR_AIGroupCombatModeAttribute VanillaMode;
 EGS_GroupRoeAttribute SquadRoe;
 EGS_UnitRoeAttribute SoldierRoe;
 ESR_GroupSurrenderAttribute SquadSurrender;
 int VanillaIndex = -1;
 int SquadRoeIndex = -1;

 ESR_SurrenderModule Surrender;
 EGS_Module Skills;
 SCR_AIGroup S100;
 SCR_AIGroup S0;
 SCR_AIGroup R;
 SCR_AIGroup E;
 SCR_AIGroup W;
 SCR_AIGroup F;
 SCR_AIGroup E2;
 SCR_AIGroup V;
 SCR_ChimeraCharacter Holdout;
 EBG_CacheZone FullZone;
 EBG_CacheGroup Record;
 SCR_AISuppressBehavior Burst;
 SCR_ChimeraCharacter BurstShooter;

 ref array<ref ESRAttrShots> RShots = {};
 ref array<ref ESRAttrShots> EShots = {};
 // E's counters from the moment it opens fire (RPhase 4).
 ref array<ref ESRAttrShots> EOpenShots = {};
 ref array<ref ESRAttrShots> WShots = {};
 ref array<ref ESRAttrShots> FShots = {};
 ref ESRAttrShots HoldShotsCounter;
 ref ESRAttrShots BurstShots;

 int SPhase;
 int RPhase;
 int WPhase;
 int FPhase;
 int VPhase;
 float SAt;
 float RAt;
 float WAt;
 float FAt;
 float VAt;
 bool SDone;
 bool RDone;
 bool WDone;
 bool FDone;
 bool VDone;

 int Checks;
 int Failures;
 int Surrender100 = -1;
 int Surrender0 = -1;
 int HoldShots = -1;
 int FarBurst;
 int Provoked;
 int Warning;
 int Lethal;
 int Rearm;
 int Leak = -1;
 int SyncOk;
 int CachedOk;

 bool SawEnemy;
 bool BurstRan;
 float EOpenedAt;
 float RProvokedAt = -1;
 bool RQuietBroken;
 float PendingAt;
 int WShotsAtPending;
 int WShotsAtLethal;
 ref array<float> StandInHealth = {};
 float LethalAt;
 bool WEverProvoked;
 int HoldSuppressSamples;
 int MateSuppressSamples;
 float MatesFiredAt = -1;

 float Started;
 float Next;
 bool Ready;
 bool Finished;

 void EXPG_GarrisonGameplay(IEntitySource src, IEntity parent) { SetEventMask(EntityEvent.INIT | EntityEvent.FRAME); }

 float Now()
 {
  return GetGame().GetWorld().GetWorldTime() * 0.001;
 }

 override void EOnInit(IEntity owner)
 {
  if (!Replication.IsServer()) { ClearEventMask(EntityEvent.FRAME); return; }
  s_Presence = {};
  s_StandIn = null;
  Started = Now(); Next = Started + 10;
  PrintFormat("[ESR SQUAD ATTR BEGIN] origin=%1 deadline=%2", Origin, FIXTURE_SECONDS);
 }

 bool Check(bool ok, string label)
 {
  Checks++;
  if (!ok) Failures++;
  PrintFormat("[ESR SQUAD ATTR CHECK] pass=%1 %2", ok, label);
  return ok;
 }

 void Finish(string reason)
 {
  if (Finished) return;
  Finished = true;
  if (s_Presence) s_Presence.Clear();
  s_StandIn = null;
  ClearEventMask(EntityEvent.FRAME);
  string tail = string.Format(" rearm=%1 leak=%2 sync=%3 cached=%4 reason=%5", Rearm, Leak, SyncOk, CachedOk, reason);
  Print(string.Format("[ESR SQUAD ATTR RESULT] checks=%1 failures=%2 surrender100=%3 surrender0=%4 holdShots=%5 farBurst=%6 provoked=%7 warning=%8 lethal=%9", Checks, Failures, Surrender100, Surrender0, HoldShots, FarBurst, Provoked, Warning, Lethal) + tail);
  GetGame().RequestClose();
 }

 vector Ground(vector p, float lift)
 {
  p[1] = GetGame().GetWorld().GetSurfaceY(p[0], p[2]) + lift;
  return p;
 }

 IEntity Spawn(ResourceName prefab, vector p)
 {
  EntitySpawnParams spawn = new EntitySpawnParams();
  spawn.TransformMode = ETransformMode.WORLD;
  Math3D.MatrixIdentity4(spawn.Transform);
  spawn.Transform[3] = Ground(p, 0.3);
  // Keep the Resource and the spawned entity in locals before casting.
  Resource resource = Resource.Load(prefab);
  IEntity spawned = GetGame().SpawnEntityPrefab(resource, GetGame().GetWorld(), spawn);
  return spawned;
 }

 SCR_AIGroup SpawnGroup(ResourceName prefab, vector p)
 {
  IEntity spawned = Spawn(prefab, p);
  SCR_AIGroup group = SCR_AIGroup.Cast(spawned);
  return group;
 }

 SCR_EditableEntityComponent Editable(IEntity entity)
 {
  if (!entity) return null;
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.GetEditableEntity(entity);
  return editable;
 }

 // The Game Master save path: the attribute's own WriteVariable on the server with the
 // editing Game Master's player id (the stand-in passes EGS_Attributes.IsEditingGameMaster).
 void GmWrite(SCR_BaseEditorAttribute attribute, IEntity entity, int entry)
 {
  if (!attribute || !entity) return;
  attribute.WriteVariable(Editable(entity), SCR_BaseEditorAttributeVar.CreateInt(entry), null, STAND_IN_GM);
 }

 // A write from somebody who is not the editing Game Master (must change nothing).
 void ForeignWrite(SCR_BaseEditorAttribute attribute, IEntity entity, int entry)
 {
  if (!attribute || !entity) return;
  attribute.WriteVariable(Editable(entity), SCR_BaseEditorAttributeVar.CreateInt(entry), null, 77);
 }

 // Session-load contract (CDF restore): null manager, player -1.
 void SessionWrite(SCR_BaseEditorAttribute attribute, IEntity entity, int entry)
 {
  if (!attribute || !entity) return;
  attribute.WriteVariable(Editable(entity), SCR_BaseEditorAttributeVar.CreateInt(entry), null, -1);
 }

 int SessionRead(SCR_BaseEditorAttribute attribute, IEntity entity)
 {
  if (!attribute || !entity) return -1;
  SCR_BaseEditorAttributeVar var = attribute.ReadVariable(Editable(entity), null);
  if (!var) return -1;
  int entry = var.GetInt();
  return entry;
 }

 bool LoadAttributes()
 {
  Resource resource = BaseContainerTools.LoadContainer(ATTRIBUTE_LIST);
  if (!resource || !resource.IsValid()) return false;
  BaseContainer container = resource.GetResource().ToBaseContainer();
  if (!container) return false;
  Attributes = SCR_EditorAttributeList.Cast(BaseContainerTools.CreateInstanceFromContainer(container));
  if (!Attributes) return false;
  for (int i = 0; i < Attributes.GetAttributesCount(); i++)
  {
   SCR_BaseEditorAttribute attribute = Attributes.GetAttribute(i);
   if (!attribute) continue;
   if (!VanillaMode && SCR_AIGroupCombatModeAttribute.Cast(attribute)) { VanillaMode = SCR_AIGroupCombatModeAttribute.Cast(attribute); VanillaIndex = i; }
   if (!SquadRoe && EGS_GroupRoeAttribute.Cast(attribute)) { SquadRoe = EGS_GroupRoeAttribute.Cast(attribute); SquadRoeIndex = i; }
   if (!SoldierRoe && EGS_UnitRoeAttribute.Cast(attribute)) SoldierRoe = EGS_UnitRoeAttribute.Cast(attribute);
   if (!SquadSurrender && ESR_GroupSurrenderAttribute.Cast(attribute)) SquadSurrender = ESR_GroupSurrenderAttribute.Cast(attribute);
  }
  return VanillaMode && SquadRoe && SoldierRoe && SquadSurrender;
 }

 SCR_AIGroupUtilityComponent Utility(SCR_AIGroup group)
 {
  if (!group) return null;
  return group.GetGroupUtilityComponent();
 }

 int External(SCR_AIGroup group)
 {
  SCR_AIGroupUtilityComponent utility = Utility(group);
  if (!utility) return -1;
  int mode = utility.GetCombatModeExternal();
  return mode;
 }

 int Actual(SCR_AIGroup group)
 {
  SCR_AIGroupUtilityComponent utility = Utility(group);
  if (!utility) return -1;
  int mode = utility.GetCombatModeActual();
  return mode;
 }

 int Original(SCR_AIGroup group)
 {
  EAIGroupCombatMode mode;
  if (!group || !group.EGS_GetOriginalMode(mode)) return -1;
  int value = mode;
  return value;
 }

 void SetVanillaMode(SCR_AIGroup group, EAIGroupCombatMode mode)
 {
  SCR_AIGroupUtilityComponent utility = Utility(group);
  if (utility) utility.SetCombatMode(mode);
 }

 string Describe(SCR_AIGroup group)
 {
  if (!group) return "group=NULL";
  return string.Format("override=%1 applied=%2 touched=%3 external=%4 actual=%5 original=%6 provoked=%7 warn=%8", group.EGS_GetRoeOverride(), group.EGS_GetAppliedRoe(), group.EGS_IsRoeTouched(), External(group), Actual(group), Original(group), group.EGS_IsProvoked(), group.EGS_GetWarnState());
 }

 SCR_ChimeraCharacter Member(SCR_AIGroup group, IEntity excluded)
 {
  if (!group) return null;
  array<AIAgent> agents = {};
  group.GetAgents(agents);
  IEntity leader = group.GetLeaderEntity();
  foreach (AIAgent agent : agents)
  {
   if (!agent) continue;
   SCR_ChimeraCharacter member = SCR_ChimeraCharacter.Cast(agent.GetControlledEntity());
   if (!member || member == leader || member == excluded || !member.GetCharacterController() || member.GetCharacterController().IsDead()) continue;
   return member;
  }
  return null;
 }

 int Living(SCR_AIGroup group)
 {
  if (!group) return 0;
  array<AIAgent> agents = {};
  group.GetAgents(agents);
  int living;
  foreach (AIAgent agent : agents)
  {
   if (!agent) continue;
   SCR_ChimeraCharacter member = SCR_ChimeraCharacter.Cast(agent.GetControlledEntity());
   if (member && member.GetCharacterController() && !member.GetCharacterController().IsDead()) living++;
  }
  return living;
 }

 bool Spawned(SCR_AIGroup group, int size)
 {
  return group && group.GetAgentsCount() == size;
 }

 SCR_AIUtilityComponent SoldierUtility(SCR_ChimeraCharacter character)
 {
  if (!character) return null;
  AIControlComponent control = character.GetAIControlComponent();
  if (!control) return null;
  AIAgent agent = control.GetControlAIAgent();
  if (!agent) return null;
  return SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent));
 }

 SCR_AICombatComponent Combat(IEntity entity)
 {
  if (!entity) return null;
  SCR_AICombatComponent combat = SCR_AICombatComponent.Cast(entity.FindComponent(SCR_AICombatComponent));
  return combat;
 }

 bool RunsGroupSuppress(SCR_ChimeraCharacter character)
 {
  SCR_AIUtilityComponent utility = SoldierUtility(character);
  if (!utility) return false;
  return SCR_AISuppressGroupClusterBehavior.Cast(utility.GetExecutedAction()) != null;
 }

 void Kill(IEntity entity)
 {
  SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(entity);
  if (!character) return;
  SCR_CharacterDamageManagerComponent damage = SCR_CharacterDamageManagerComponent.Cast(character.GetDamageManager());
  if (damage) damage.Kill(Instigator.CreateInstigator(null));
 }

 bool Alive(IEntity entity)
 {
  SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(entity);
  return character && character.GetCharacterController() && !character.GetCharacterController().IsDead();
 }

 float Health(IEntity entity)
 {
  SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(entity);
  if (!character || !character.GetDamageManager()) return -1;
  return character.GetDamageManager().GetHealthScaled();
 }

 // Shot counters on every current member (OnProjectileShot, as the warning burst counts,
 // and OnGrenadeThrown: a thrown grenade is fire too, and its blast can provoke).
 void Watch(SCR_AIGroup group, notnull array<ref ESRAttrShots> list, IEntity excluded)
 {
  if (!group) return;
  array<AIAgent> agents = {};
  group.GetAgents(agents);
  foreach (AIAgent agent : agents)
  {
   if (!agent) continue;
   IEntity member = agent.GetControlledEntity();
   if (!member || member == excluded) continue;
   ESRAttrShots counter = WatchOne(member);
   if (counter) list.Insert(counter);
  }
 }

 ESRAttrShots WatchOne(IEntity member)
 {
  if (!member) return null;
  EventHandlerManagerComponent events = EventHandlerManagerComponent.Cast(member.FindComponent(EventHandlerManagerComponent));
  if (!events) return null;
  ESRAttrShots counter = new ESRAttrShots();
  counter.Owner = member;
  events.RegisterScriptHandler("OnProjectileShot", counter, counter.OnShot);
  events.RegisterScriptHandler("OnGrenadeThrown", counter, counter.OnShot);
  return counter;
 }

 int Sum(array<ref ESRAttrShots> list)
 {
  int total;
  foreach (ESRAttrShots counter : list)
  {
   if (counter) total += counter.Count;
  }
  return total;
 }

 float FirstShot(array<ref ESRAttrShots> list)
 {
  float first = -1;
  foreach (ESRAttrShots counter : list)
  {
   if (counter && counter.First >= 0 && (first < 0 || counter.First < first)) first = counter.First;
  }
  return first;
 }

 // A member of R has a member of E as his current target.
 bool SeesGroup(SCR_AIGroup watcher, SCR_AIGroup watched)
 {
  if (!watcher || !watched) return false;
  array<AIAgent> agents = {};
  watcher.GetAgents(agents);
  foreach (AIAgent agent : agents)
  {
   if (!agent) continue;
   SCR_AICombatComponent combat = Combat(agent.GetControlledEntity());
   if (!combat) continue;
   BaseTarget target = combat.GetCurrentTarget();
   if (!target) continue;
   SCR_ChimeraCharacter seen = SCR_ChimeraCharacter.Cast(target.GetTargetEntity());
   if (!seen) continue;
   AIControlComponent control = seen.GetAIControlComponent();
   if (!control || !control.GetControlAIAgent()) continue;
   if (control.GetControlAIAgent().GetParentGroup() == watched) return true;
  }
  return false;
 }

 // AI LOD of the group's first member (diagnostics; 0 is full simulation).
 int Lod(SCR_AIGroup group)
 {
  if (!group) return -1;
  array<AIAgent> agents = {};
  group.GetAgents(agents);
  if (agents.IsEmpty() || !agents[0]) return -1;
  int lod = agents[0].GetLOD();
  return lod;
 }

 //------------------------------------------------------------------------------------------------
 // Fight environment: daylight and open ground.
 // Noon without fog or rain: AI spotting depends on light. The world's own hour is printed.
 bool Daylight()
 {
  ChimeraWorld world = GetGame().GetWorld();
  if (!world) return false;
  TimeAndWeatherManagerEntity weather = world.GetTimeAndWeatherManager();
  if (!weather) return false;
  float hourBefore = weather.GetTimeOfTheDay();
  bool timeSet = weather.SetTimeOfTheDay(12, true);
  weather.SetFogAmountOverride(true, 0);
  weather.SetRainIntensityOverride(true, 0);
  PrintFormat("[ESR SQUAD ATTR WORLD] hourBefore=%1 hour=%2 timeSet=%3", hourBefore, weather.GetTimeOfTheDay(), timeSet);
  return timeSet;
 }

 // R/E, W/stand-in and F/E2 sites in that order: the nearest open sites (OpenSite) on rings
 // around the driver point, each SITE_SEPARATION from the others. A missing site falls back to
 // the first run's position (the site check in Setup then fails).
 void FindSites()
 {
  array<vector> found = {};
  for (int ring = 0; ring < SITE_RINGS && found.Count() < 3; ring++)
  {
   float radius = SITE_RING_FIRST + ring * SITE_RING_STEP;
   int steps = Math.Round(Math.PI2 * radius / SITE_ARC);
   for (int step = 0; step < steps && found.Count() < 3; step++)
   {
    float angle = Math.PI2 * step / steps;
    vector centre = Vector(Origin[0] + radius * Math.Sin(angle), 0, Origin[2] + radius * Math.Cos(angle));
    if (!Separated(centre, found)) continue;
    SitesTried++;
    if (OpenSite(centre)) found.Insert(Ground(centre, 0));
   }
  }
  SitesFound = found.Count();
  SiteR = Origin;
  SiteW = Origin + Vector(-600, 0, -600);
  SiteF = Origin + Vector(-600, 0, 600);
  if (SitesFound > 0) SiteR = found[0];
  if (SitesFound > 1) SiteW = found[1];
  if (SitesFound > 2) SiteF = found[2];
  PrintFormat("[ESR SQUAD ATTR SITES] R=%1 W=%2 F=%3 found=%4 tried=%5 separation=%6", SiteR, SiteW, SiteF, SitesFound, SitesTried, SITE_SEPARATION);
 }

 bool Separated(vector centre, array<vector> sites)
 {
  foreach (vector site : sites)
  {
   if (vector.DistanceXZ(centre, site) < SITE_SEPARATION) return false;
  }
  return true;
 }

 // Open ground for a fight (watcher at centre, enemy 60 m north): on the map and above the
 // sea, dry and level over the fight box (20 m either side, 10 m behind the watcher to 10 m
 // beyond the enemy), nothing standing where the two wedges spawn, and every sight and fire
 // line from the watcher's wedge to the enemy's clear at 1.0 m and 1.7 m.
 bool OpenSite(vector centre)
 {
  if (centre[0] < SITE_EDGE || centre[2] < SITE_EDGE || centre[0] > SITE_FAR_EDGE || centre[2] > SITE_FAR_EDGE) return false;
  BaseWorld world = GetGame().GetWorld();
  float groundY = world.GetSurfaceY(centre[0], centre[2]);
  if (groundY < SITE_MIN_HEIGHT) return false;
  for (int across = -2; across <= 2; across++)
  {
   for (int along = -1; along <= 7; along++)
   {
    vector probe = Vector(centre[0] + across * 10, 0, centre[2] + along * 10);
    probe[1] = world.GetSurfaceY(probe[0], probe[2]);
    if (Math.AbsFloat(probe[1] - groundY) > SITE_LEVEL) return false;
    if (ChimeraWorldUtils.TryGetWaterSurfaceSimple(world, probe + Vector(0, 0.2, 0))) return false;
   }
  }
  for (int watcher = -2; watcher <= 2; watcher++)
  {
   for (int enemy = -1; enemy <= 1; enemy++)
   {
    vector watcherFoot = Vector(centre[0] + watcher * 8, 0, centre[2] - 4);
    vector enemyFoot = Vector(centre[0] + enemy * 8, 0, centre[2] + 57);
    if (!ClearLine(world, watcherFoot, enemyFoot, 1.0) || !ClearLine(world, watcherFoot, enemyFoot, 1.7)) return false;
   }
  }
  for (int column = -2; column <= 2; column++)
  {
   for (int row = 0; row < 3; row++)
   {
    if (!ClearStand(world, centre[0] + column * 5, centre[2] - row * 5)) return false;
    if (!ClearStand(world, centre[0] + column * 5, centre[2] + 60 - row * 5)) return false;
   }
  }
  return true;
 }

 // A line between two eye points: above the terrain everywhere (sampled every 5 m) and no
 // fire or view geometry (buildings, trees, rocks, walls) on it.
 bool ClearLine(BaseWorld world, vector watcherEye, vector enemyEye, float eye)
 {
  watcherEye[1] = world.GetSurfaceY(watcherEye[0], watcherEye[2]) + eye;
  enemyEye[1] = world.GetSurfaceY(enemyEye[0], enemyEye[2]) + eye;
  int samples = Math.Round(vector.Distance(watcherEye, enemyEye) / 5);
  for (int i = 1; i < samples; i++)
  {
   float share = i * 1.0 / samples;
   vector point = vector.Lerp(watcherEye, enemyEye, share);
   if (point[1] < world.GetSurfaceY(point[0], point[2]) + 0.3) return false;
  }
  TraceParam sight = new TraceParam();
  sight.Start = watcherEye;
  sight.End = enemyEye;
  sight.Flags = TraceFlags.WORLD | TraceFlags.ENTS | TraceFlags.OCEAN | TraceFlags.ANY_CONTACT;
  sight.TargetLayers = EPhysicsLayerDefs.FireGeometry | EPhysicsLayerDefs.ViewGeometry;
  float fraction = world.TraceMove(sight, null);
  return fraction >= 1;
 }

 // Nothing standing on the ground at this point (a 25 m drop probe meets only the terrain).
 bool ClearStand(BaseWorld world, float x, float z)
 {
  float y = world.GetSurfaceY(x, z);
  float top = y + 25;
  float bottom = y - 1;
  TraceParam drop = new TraceParam();
  drop.Start = Vector(x, top, z);
  drop.End = Vector(x, bottom, z);
  drop.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
  drop.TargetLayers = EPhysicsLayerDefs.FireGeometry | EPhysicsLayerDefs.ViewGeometry;
  float fraction = world.TraceMove(drop, null);
  float hitY = top + (bottom - top) * fraction;
  return hitY <= y + 0.5;
 }

 // An E member for the far burst: a non-leader without a grenade or rocket launcher (the
 // burst must be rounds, not a blast beside R); any non-leader otherwise.
 SCR_ChimeraCharacter BurstMember(SCR_AIGroup group)
 {
  if (!group) return null;
  array<AIAgent> agents = {};
  group.GetAgents(agents);
  IEntity leader = group.GetLeaderEntity();
  foreach (AIAgent agent : agents)
  {
   if (!agent) continue;
   SCR_ChimeraCharacter member = SCR_ChimeraCharacter.Cast(agent.GetControlledEntity());
   if (!member || member == leader || !Alive(member) || HasLauncher(member)) continue;
   return member;
  }
  return Member(group, null);
 }

 bool HasLauncher(IEntity character)
 {
  BaseWeaponManagerComponent weapons = BaseWeaponManagerComponent.Cast(character.FindComponent(BaseWeaponManagerComponent));
  if (!weapons) return false;
  array<IEntity> carried = {};
  weapons.GetWeaponsList(carried);
  foreach (IEntity weaponEntity : carried)
  {
   if (!weaponEntity) continue;
   BaseWeaponComponent weapon = BaseWeaponComponent.Cast(weaponEntity.FindComponent(BaseWeaponComponent));
   if (!weapon) continue;
   array<BaseMuzzleComponent> muzzles = {};
   weapon.GetMuzzlesList(muzzles);
   foreach (BaseMuzzleComponent muzzle : muzzles)
   {
    if (muzzle && (muzzle.GetMuzzleType() == EMuzzleType.MT_UGLMuzzle || muzzle.GetMuzzleType() == EMuzzleType.MT_RPGMuzzle)) return true;
   }
  }
  return false;
 }

 static bool IsStandIn(IEntity entity)
 {
  return entity && s_StandIn && entity == s_StandIn;
 }

 override void EOnFrame(IEntity owner, float timeSlice)
 {
  if (Finished || Now() < Next) return;
  Next = Now() + 0.25;
  if (Now() - Started > FIXTURE_SECONDS)
  {
   Check(false, string.Format("all cases finished before the fixture deadline (S=%1 R=%2 W=%3 F=%4 V=%5)", SPhase, RPhase, WPhase, FPhase, VPhase));
   Finish("timeout");
   return;
  }
  if (!Ready) { Setup(); return; }
  if (!SDone) StepS();
  if (!RDone) StepR();
  if (!WDone) StepW();
  if (!FDone) StepF();
  if (!VDone) StepV();
  if (SDone && RDone && WDone && FDone && VDone) Finish("complete");
 }

 // A case gave up: one failed check, the case ends, the others go on.
 bool CaseTimeout(float since, float seconds, string label)
 {
  if (Now() - since <= seconds) return false;
  Check(false, label);
  return true;
 }

 void Setup()
 {
  if (!Check(GetGame().GetPlayerManager().GetPlayerCount() == 0 && ESR_SurrenderManager.PrisonerCount() == 0 && EBG_CacheZone.Zones.IsEmpty(), "isolated server: no players, no prisoners, no cache zones")) { Finish("setup"); return; }
  if (!Check(LoadAttributes(), "merged attribute list holds vanilla Set combat mode, EXPBG squad and soldier ROE and the squad surrender chance")) { Finish("setup"); return; }
  PrintFormat("[ESR SQUAD ATTR LIST] vanillaCombatMode=%1 squadRoe=%2 count=%3", VanillaIndex, SquadRoeIndex, Attributes.GetAttributesCount());
  Check(VanillaIndex >= 0 && SquadRoeIndex > VanillaIndex, "one Game Master save writes vanilla Set combat mode before the EXPBG squad ROE");
  PureChecks();
  Check(Daylight(), "clear daylight for the fights: noon, no fog, no rain");
  FindSites();
  Check(SitesFound == 3, string.Format("open fight sites for R, W and F: dry, level, clear sight and fire lines over 60 m, %1 m apart (found %2 of 3, %3 candidates)", SITE_SEPARATION, SitesFound, SitesTried));
  Surrender = ESR_SurrenderModule.Cast(Spawn(SURRENDER_MODULE, Origin + SiteS + ModuleOffset));
  Skills = EGS_Module.Cast(Spawn(SKILLS_MODULE, Origin + SiteS + SkillsOffset));
  if (!Check(Surrender != null && Skills != null && EGS_Module.HasAny(), "AI Surrender and AI Global Skills module prefabs spawned")) { Finish("setup"); return; }
  Surrender.ApplySetting(ESR_Settings.ENABLED, ESR_Settings.FromBool(true));
  Surrender.ApplySetting(ESR_Settings.CHANCE, 0);
  Surrender.ApplySetting(ESR_Settings.THRESHOLD, 10);
  Surrender.ApplySetting(ESR_Settings.RANDOM, 0);
  Surrender.ApplySetting(ESR_Settings.GRENADE, 0);
  Surrender.ApplySetting(ESR_Settings.DIAGNOSTICS, 1);
  EGS_Settings.WriteSetting(EGS_Settings.KEY_ROE, Vector(EGS_Settings.ROE_VANILLA, 0, 0));
  Check(EGS_Settings.GetRoe() == EGS_Settings.ROE_VANILLA, "module default ROE vanilla: only the squad attributes steer");
  // Modes are set the moment the groups exist, before any member can see an enemy. S100,
  // S0 and V stay by the driver point (at least 450 m from E) and never fire at will.
  S100 = SpawnGroup(USSR_SQUAD, Origin + SiteS);
  Check(SessionRead(VanillaMode, S100) == ENTRY_FIRE, "vanilla Set combat mode reads fire at will on a fresh squad (entry mapping)");
  SetVanillaMode(S100, EAIGroupCombatMode.HOLD_FIRE);
  Check(SessionRead(VanillaMode, S100) == ENTRY_HOLD, "vanilla Set combat mode reads hold fire after SetCombatMode (entry mapping)");
  S0 = SpawnGroup(USSR_SQUAD, Origin + SiteS0);
  SetVanillaMode(S0, EAIGroupCombatMode.HOLD_FIRE);
  V = SpawnGroup(USSR_SQUAD, Origin + SiteV);
  SetVanillaMode(V, EAIGroupCombatMode.RETURN_FIRE);
  R = SpawnGroup(USSR_SQUAD, SiteR);
  GmWrite(SquadRoe, R, EGS_Settings.ROE_RETURN_FIRE);
  E = SpawnGroup(US_TEAM, SiteR + EnemyOffset);
  SetVanillaMode(E, EAIGroupCombatMode.HOLD_FIRE);
  W = SpawnGroup(USSR_SQUAD, SiteW);
  GmWrite(SquadRoe, W, EGS_Settings.ROE_WARNING_SHOTS);
  F = SpawnGroup(USSR_SQUAD, SiteF);
  GmWrite(SquadRoe, F, EGS_Settings.ROE_FIRE_ON_SIGHT);
  if (!Check(S100 && S0 && V && R && E && W && F, "squads spawned: S100, S0, V, R, W, F (USSR) and fire team E (US)")) { Finish("setup"); return; }
  Check(R.EGS_GetRoeOverride() == EGS_Settings.ROE_RETURN_FIRE && R.EGS_GetAppliedRoe() == EGS_Settings.ROE_RETURN_FIRE && External(R) == EAIGroupCombatMode.RETURN_FIRE && R.EGS_UsesStrictReturnFire(), "Game Master save: R Return Fire Only, RETURN_FIRE with the strict rule; " + Describe(R));
  Check(W.EGS_GetRoeOverride() == EGS_Settings.ROE_WARNING_SHOTS && External(W) == EAIGroupCombatMode.RETURN_FIRE && W.EGS_UsesStrictReturnFire() && W.EGS_WatchesProvocation(), "Game Master save: W Warning Shots First, armed, strict and watched; " + Describe(W));
  Check(F.EGS_GetRoeOverride() == EGS_Settings.ROE_FIRE_ON_SIGHT && External(F) == EAIGroupCombatMode.FIRE_AT_WILL && !F.EGS_UsesStrictReturnFire(), "Game Master save: F Fire on Sight; " + Describe(F));
  ForeignWrite(SquadRoe, R, EGS_Settings.ROE_FIRE_ON_SIGHT);
  Check(R.EGS_GetRoeOverride() == EGS_Settings.ROE_RETURN_FIRE, "a write from somebody who is not the editing Game Master changes nothing");
  Ready = true;
  SAt = Now(); RAt = Now(); WAt = Now(); FAt = Now(); VAt = Now();
 }

 // Pure rules: the strict near-miss test against vanilla's 13 m flyby cylinder, and the
 // vehicle clearance of a warning burst.
 void PureChecks()
 {
  vector muzzle = "0 1.5 0";
  vector aim = "0 0 100";
  vector close = "3 1 50";
  vector wide = "10 1 50";
  vector behind = "1 1 -20";
  Check(EGS_Provocation.IsNearMiss(close, muzzle, aim, EGS_Provocation.NEAR_MISS_RADIUS), "near miss: a round 3 m from a soldier provokes");
  Check(!EGS_Provocation.IsNearMiss(wide, muzzle, aim, EGS_Provocation.NEAR_MISS_RADIUS) && EGS_Provocation.IsNearMiss(wide, muzzle, aim, 13), "a round 10 m away does not provoke (vanilla's 13 m flyby would)");
  Check(!EGS_Provocation.IsNearMiss(behind, muzzle, aim, EGS_Provocation.NEAR_MISS_RADIUS), "only the path in front of the muzzle counts");
  vector shooter = "0 0 0";
  vector target = "0 0 60";
  float soldier = vector.Distance(EGS_Manager.WarningOffset(shooter, target, true), target);
  float vehicle = vector.Distance(EGS_Manager.WarningOffset(shooter, target, true, 4.0), target);
  Check(vehicle >= soldier + 3.0, string.Format("warning burst at a vehicle keeps its size clear (%1 m vs %2 m)", vehicle, soldier));
  Check(!EGS_Manager.IsPlayerTarget(null), "no target is no player target");
 }

 //------------------------------------------------------------------------------------------------
 // S: surrender chance 100% / 0% per squad; B: vanilla Set combat mode and the EXPBG ROE agree.
 void StepS()
 {
  if (SPhase == 0)
  {
   if (!Spawned(S100, SQUAD_SIZE) || !Spawned(S0, SQUAD_SIZE)) { if (CaseTimeout(SAt, 60, "S: both squads finished their initial spawn")) SDone = true; return; }
   SessionWrite(SquadSurrender, S100, ESR_Overrides.ToEntry(100));
   SessionWrite(SquadSurrender, S0, ESR_Overrides.ToEntry(0));
   Check(S100.ESR_GetOverride(ESR_Overrides.SURRENDER) == 100 && S0.ESR_GetOverride(ESR_Overrides.SURRENDER) == 0 && SessionRead(SquadSurrender, S100) == 21 && SessionRead(SquadSurrender, S0) == 1, "squad surrender chance stored: S100 100%, S0 0%");
   // The module chance is 100%: S0's 0% must win, S100's 100% is its own.
   Surrender.ApplySetting(ESR_Settings.CHANCE, 100);
   Check(ESR_SurrenderManager.PrisonerCount() == 0, "100% does not surrender before the squad breaks");
   Kill(Member(S100, null));
   Kill(Member(S0, null));
   SPhase = 1; SAt = Now(); return;
  }
  if (SPhase == 1)
  {
   int fromHundred = PrisonersOf(S100);
   if (fromHundred < SQUAD_SIZE - 1) { if (CaseTimeout(SAt, 15, string.Format("S: the five able soldiers of S100 surrendered (got %1)", fromHundred))) { Surrender100 = fromHundred; Surrender0 = PrisonersOf(S0); EndS(); } return; }
   // Weapon drops and sit-downs are scheduled; give them time.
   if (Now() - SAt < 4) return;
   Surrender100 = PrisonersOf(S100);
   Surrender0 = PrisonersOf(S0);
   PrintFormat("[ESR SQUAD ATTR SURRENDER] s100=%1 s0=%2 s0Living=%3 prisoners=%4", Surrender100, Surrender0, Living(S0), ESR_SurrenderManager.PrisonerCount());
   Check(Surrender100 == SQUAD_SIZE - 1, "S100 (100%): every able soldier surrendered after one casualty");
   Check(Surrender0 == 0 && Living(S0) == SQUAD_SIZE - 1, "S0 (0%): nobody surrendered although the module chance is 100%");
   EndS();
   return;
  }
  StepB();
 }

 int PrisonersOf(SCR_AIGroup group)
 {
  int count;
  for (int i = 0; i < ESR_SurrenderManager.PrisonerCount(); i++)
  {
   ESR_Prisoner prisoner = ESR_SurrenderManager.GetPrisonerAt(i);
   if (prisoner && prisoner.Group == group) count++;
  }
  return count;
 }

 void EndS()
 {
  Surrender.ApplySetting(ESR_Settings.CHANCE, 0);
  SPhase = 10; SAt = Now();
 }

 // One Game Master save per step (a save is one call; steps are 0.25 s apart). S0 is 150 m
 // from E: every step leaves it holding fire or in return fire, never in fire at will.
 void StepB()
 {
  if (!S0 || Living(S0) == 0) { Check(false, "B: squad S0 still exists"); SDone = true; return; }
  bool ok;
  if (SPhase == 10)
  {
   GmWrite(SquadRoe, S0, EGS_Settings.ROE_RETURN_FIRE);
   ok = Check(S0.EGS_IsRoeTouched() && External(S0) == EAIGroupCombatMode.RETURN_FIRE && Original(S0) == EAIGroupCombatMode.HOLD_FIRE, "B1: EXPBG Return Fire Only over the squad's own hold fire; " + Describe(S0));
   NextB(ok); return;
  }
  if (SPhase == 11)
  {
   GmWrite(VanillaMode, S0, ENTRY_RETURN);
   ok = Check(S0.EGS_GetRoeOverride() == EGS_Settings.GROUP_ROE_EXEMPT && S0.EGS_GetAppliedRoe() == EGS_Settings.ROE_VANILLA && !S0.EGS_IsRoeTouched() && External(S0) == EAIGroupCombatMode.RETURN_FIRE, "B2: vanilla Set combat mode return fire switches the EXPBG ROE to Exempt and keeps return fire (not the old hold fire); " + Describe(S0));
   NextB(ok); return;
  }
  if (SPhase == 12)
  {
   // One save: vanilla hold fire (written first, as in the list), then EXPBG Return Fire Only.
   GmWrite(VanillaMode, S0, ENTRY_HOLD);
   GmWrite(SquadRoe, S0, EGS_Settings.ROE_RETURN_FIRE);
   ok = Check(S0.EGS_GetRoeOverride() == EGS_Settings.ROE_RETURN_FIRE && External(S0) == EAIGroupCombatMode.RETURN_FIRE && Original(S0) == EAIGroupCombatMode.HOLD_FIRE, "B3: one save with both: Return Fire Only over the new hold fire; " + Describe(S0));
   NextB(ok); return;
  }
  if (SPhase == 13)
  {
   // One save in the other order: EXPBG Warning Shots First, then vanilla fire at will
   // (taken as the squad's own mode in the same call: RETURN_FIRE again before any AI update).
   GmWrite(SquadRoe, S0, EGS_Settings.ROE_WARNING_SHOTS);
   GmWrite(VanillaMode, S0, ENTRY_FIRE);
   ok = Check(S0.EGS_GetRoeOverride() == EGS_Settings.ROE_WARNING_SHOTS && External(S0) == EAIGroupCombatMode.RETURN_FIRE && Original(S0) == EAIGroupCombatMode.FIRE_AT_WILL, "B4: one save, EXPBG first: Warning Shots First stays, fire at will becomes the squad's own mode; " + Describe(S0));
   NextB(ok); return;
  }
  if (SPhase == 14)
  {
   // Another addon (or a waypoint) changes the mode; a Game Master write of the EXPBG ROE
   // (here the same value, written directly) puts it back and keeps the changed mode as the
   // squad's own.
   SetVanillaMode(S0, EAIGroupCombatMode.HOLD_FIRE);
   GmWrite(SquadRoe, S0, EGS_Settings.ROE_WARNING_SHOTS);
   ok = Check(External(S0) == EAIGroupCombatMode.RETURN_FIRE && Original(S0) == EAIGroupCombatMode.HOLD_FIRE, "B5: a Game Master write puts the EXPBG mode back over a mode changed outside EXPBG; " + Describe(S0));
   NextB(ok); return;
  }
  if (SPhase == 15)
  {
   GmWrite(SquadRoe, S0, EGS_Settings.GROUP_ROE_EXEMPT);
   ok = Check(!S0.EGS_IsRoeTouched() && External(S0) == EAIGroupCombatMode.HOLD_FIRE, "B6: Exempt (vanilla) restores the squad's own hold fire; " + Describe(S0));
   NextB(ok);
  }
 }

 // B goes on while its steps pass; the first failure ends it with S0 on Exempt and hold fire.
 void NextB(bool ok)
 {
  if (ok && SPhase < 15) { SPhase++; return; }
  if (ok) SyncOk = 1;
  else
  {
   EGS_Manager.SetGroupRoe(S0, EGS_Settings.GROUP_ROE_EXEMPT);
   SetVanillaMode(S0, EAIGroupCombatMode.HOLD_FIRE);
  }
  SDone = true;
 }

 //------------------------------------------------------------------------------------------------
 // R: Return Fire Only holds fire at a visible enemy until fired upon.
 void StepR()
 {
  if (!R || !E) { Check(false, "R: squads R and E still exist"); RDone = true; return; }
  if (RPhase == 0)
  {
   if (!Spawned(R, SQUAD_SIZE) || !Spawned(E, TEAM_SIZE)) { if (CaseTimeout(RAt, 60, "R: R and E finished their initial spawn")) RDone = true; return; }
   Watch(R, RShots, null);
   Watch(E, EShots, null);
   Check(RShots.Count() == SQUAD_SIZE && EShots.Count() == TEAM_SIZE, "R: shot counters on every member of R and E");
   RPhase = 1; RAt = Now(); return;
  }
  if (RPhase == 1)
  {
   if (SeesGroup(R, E)) SawEnemy = true;
   if (Sum(RShots) > 0 || R.EGS_IsProvoked()) { HoldShots = Sum(RShots); Check(false, "R: no round and no provocation while E holds fire; " + Describe(R)); RDone = true; return; }
   if (Now() - RAt < 30) return;
   HoldShots = Sum(RShots);
   PrintFormat("[ESR SQUAD ATTR R HOLD] shots=%1 sawEnemy=%2 rLod=%3 eLod=%4 eLiving=%5 %6", HoldShots, SawEnemy, Lod(R), Lod(E), Living(E), Describe(R));
   Check(SawEnemy, "R: a member of R had a member of E as his target (visible enemy)");
   Check(HoldShots == 0 && Actual(R) == EAIGroupCombatMode.HOLD_FIRE, "R: 30 s at a visible enemy, no round fired, actual hold fire");
   if (!StartFarBurst()) { RDone = true; return; }
   RPhase = 2; RAt = Now(); return;
  }
  if (RPhase == 2)
  {
   int burstSoFar = 0;
   if (BurstShots) burstSoFar = BurstShots.Count;
   SCR_AIUtilityComponent burstUtility = SoldierUtility(BurstShooter);
   if (Burst && burstUtility && burstUtility.GetExecutedAction() == Burst) BurstRan = true;
   if (burstSoFar < 3 && Now() - RAt < 12) return;
   if (Burst) Burst.Complete();
   RPhase = 3; RAt = Now(); return;
  }
  if (RPhase == 3)
  {
   if (Now() - RAt < 5) return;
   int burstRounds = 0;
   if (BurstShots) burstRounds = BurstShots.Count;
   bool launcher;
   if (BurstShooter) launcher = HasLauncher(BurstShooter);
   PrintFormat("[ESR SQUAD ATTR R FAR BURST] rounds=%1 rShots=%2 ran=%3 shooter=%4 launcher=%5 %6", burstRounds, Sum(RShots), BurstRan, BurstShooter, launcher, Describe(R));
   bool burstFired = Check(burstRounds >= 1, "R: E fired the far burst");
   bool held = Check(!R.EGS_IsProvoked() && Sum(RShots) == 0 && Actual(R) == EAIGroupCombatMode.HOLD_FIRE, "R: rounds passing about 9 m beside R do not set it off");
   if (burstFired && held) FarBurst = 1;
   RPhase = 4; RAt = Now(); return;
  }
  if (RPhase == 4)
  {
   if (R.EGS_IsProvoked() || Sum(RShots) > 0) RQuietBroken = true;
   // E opens fire only after the cache case, so its rounds never reach the waking squad V.
   if (!VDone && Now() - RAt < 150) return;
   // Fresh counters: E's first round from now on (the far burst of RPhase 2 does not count).
   Watch(E, EOpenShots, null);
   SetVanillaMode(E, EAIGroupCombatMode.FIRE_AT_WILL);
   EOpenedAt = Now();
   RPhase = 5; RAt = Now(); return;
  }
  if (RPhase == 5)
  {
   if (RProvokedAt < 0 && R.EGS_IsProvoked()) RProvokedAt = Now();
   bool answered = RProvokedAt >= 0 && Sum(RShots) > 0;
   if (!answered) { if (CaseTimeout(RAt, 60, string.Format("R: provoked by E's fire and answered it; eOpenShots=%1 rShots=%2 rLiving=%3 ", Sum(EOpenShots), Sum(RShots), Living(R)) + Describe(R))) RDone = true; return; }
   float eFirst = FirstShot(EOpenShots);
   float rFirst = FirstShot(RShots);
   PrintFormat("[ESR SQUAD ATTR R PROVOKED] eOpened=%1 eFirst=%2 provoked=%3 rFirst=%4 eOpenShots=%5 rShots=%6 quietBroken=%7 eCounters=%8 %9", EOpenedAt, eFirst, RProvokedAt, rFirst, Sum(EOpenShots), Sum(RShots), RQuietBroken, EOpenShots.Count(), Describe(R));
   // RProvokedAt is polled (up to 0.25 s late), eFirst is the shot's own time.
   bool provokedOk = Check(!RQuietBroken && eFirst >= EOpenedAt && RProvokedAt >= eFirst - 0.05, "R: quiet until E opened fire, provoked only after E's first round from then on");
   provokedOk = Check(rFirst >= RProvokedAt - 1.0 && rFirst - RProvokedAt <= 15, "R: answered fire after it was provoked") && provokedOk;
   if (provokedOk) Provoked = 1;
   RPhase = 6; RAt = Now(); return;
  }
  if (RPhase == 6)
  {
   if (Now() - RAt < 2) return;
   Check(Actual(R) == EAIGroupCombatMode.FIRE_AT_WILL || !R.EGS_IsProvoked(), "R: a provoked squad fires at will");
   RDone = true;
  }
 }

 // An E member suppresses a point beside R: the path passes about 9 m beside R's nearest member.
 bool StartFarBurst()
 {
  BurstShooter = BurstMember(E);
  if (!Check(BurstShooter != null, "R: an E member to fire the far burst")) return false;
  array<AIAgent> agents = {};
  R.GetAgents(agents);
  vector centre;
  int count;
  foreach (AIAgent agent : agents)
  {
   if (!agent || !agent.GetControlledEntity()) continue;
   centre = centre + agent.GetControlledEntity().GetOrigin();
   count++;
  }
  if (!Check(count > 0, "R: R has members for the far burst")) return false;
  centre = centre * (1.0 / count);
  vector muzzle = BurstShooter.GetOrigin() + Vector(0, 1.5, 0);
  vector forward = centre - BurstShooter.GetOrigin();
  forward[1] = 0;
  float distance = forward.Length();
  if (distance < 1) distance = 1;
  forward = forward * (1.0 / distance);
  vector side = Vector(forward[2], 0, -forward[0]);
  float widest;
  foreach (AIAgent spread : agents)
  {
   if (!spread || !spread.GetControlledEntity()) continue;
   float lateral = vector.Dot(spread.GetControlledEntity().GetOrigin() - centre, side);
   if (lateral > widest) widest = lateral;
  }
  vector aim = Ground(centre + side * (widest + 9.0), 1.0);
  int near;
  int flyby;
  foreach (AIAgent member : agents)
  {
   if (!member || !member.GetControlledEntity()) continue;
   vector body = member.GetControlledEntity().GetOrigin() + Vector(0, EGS_Provocation.BODY_HEIGHT, 0);
   if (EGS_Provocation.IsNearMiss(body, muzzle, aim - muzzle, EGS_Provocation.NEAR_MISS_RADIUS)) near++;
   if (EGS_Provocation.IsNearMiss(body, muzzle, aim - muzzle, 13)) flyby++;
  }
  PrintFormat("[ESR SQUAD ATTR R FAR AIM] widest=%1 near=%2 vanillaFlyby=%3 aim=%4", widest, near, flyby, aim);
  if (!Check(near == 0, "R: the far burst's aim line passes no R member within 4 m")) return false;
  SCR_AIUtilityComponent utility = SoldierUtility(BurstShooter);
  if (!Check(utility != null, "R: the burst shooter has an AI utility")) return false;
  BurstShots = WatchOne(BurstShooter);
  // Locals hold the new objects until the utility owns the behaviour (as the warning burst).
  SCR_AISuppressionVolumeSphere volume = new SCR_AISuppressionVolumeSphere(aim, 1.0);
  SCR_AISuppressBehavior behavior = new SCR_AISuppressBehavior(utility, null, volume, 10.0, 2.0, SCR_AIActionBase.PRIORITY_LEVEL_PLAYER);
  utility.AddAction(behavior);
  Burst = behavior;
  return true;
 }

 //------------------------------------------------------------------------------------------------
 // W: Warning Shots First at the stand-in player.
 void StepW()
 {
  if (!W) { Check(false, "W: squad W still exists"); WDone = true; return; }
  if (WPhase == 0)
  {
   if (!Spawned(W, SQUAD_SIZE)) { if (CaseTimeout(WAt, 60, "W: W finished its initial spawn")) WDone = true; return; }
   Watch(W, WShots, null);
   // The stand-in player: a standalone rifleman without AI (he cannot shoot back).
   s_StandIn = Spawn(US_RIFLEMAN, SiteW + EnemyOffset);
   SCR_ChimeraCharacter standIn = SCR_ChimeraCharacter.Cast(s_StandIn);
   if (!Check(standIn != null && EGS_Manager.IsPlayerTarget(standIn), "W: the stand-in counts as a player target")) { WDone = true; return; }
   AIControlComponent control = standIn.GetAIControlComponent();
   if (control && control.IsAIActivated()) control.DeactivateAI();
   WPhase = 1; WAt = Now(); return;
  }
  if (W.EGS_IsProvoked()) WEverProvoked = true;
  if (WPhase == 1)
  {
   if (W.EGS_GetWarnState() != 1) { if (CaseTimeout(WAt, 60, "W: a member spotted the stand-in and the warning started; " + Describe(W))) WDone = true; return; }
   PendingAt = Now();
   WShotsAtPending = Sum(WShots);
   StandInHealth.Clear();
   StandInHealth.Insert(Health(s_StandIn));
   Check(WShotsAtPending == 0, "W: no round before the warning");
   WPhase = 2; WAt = Now(); return;
  }
  if (WPhase == 2)
  {
   if (W.EGS_GetWarnState() != 2) { if (CaseTimeout(WAt, EGS_Manager.WARNING_WINDOW_S + EGS_Manager.WARNING_PAUSE_S + 6, "W: the squad turned lethal after the warning; " + Describe(W))) WDone = true; return; }
   LethalAt = Now();
   WShotsAtLethal = Sum(WShots);
   int rounds = WShotsAtLethal - WShotsAtPending;
   float pause = LethalAt - PendingAt;
   float health = Health(s_StandIn);
   PrintFormat("[ESR SQUAD ATTR W WARNING] rounds=%1 pause=%2 healthBefore=%3 healthAfter=%4 %5", rounds, pause, StandInHealth[0], health, Describe(W));
   bool ok = Check(rounds >= 1 && rounds <= EGS_Manager.WARNING_ROUNDS + 1, "W: 1-3 warning rounds before lethal");
   ok = Check(Alive(s_StandIn) && health >= StandInHealth[0], "W: the warning rounds left the stand-in unhurt") && ok;
   ok = Check(pause >= EGS_Manager.WARNING_PAUSE_S - 0.5 && pause <= EGS_Manager.WARNING_WINDOW_S + EGS_Manager.WARNING_PAUSE_S + 2, "W: lethal about 5 s after the warning") && ok;
   ok = Check(External(W) == EAIGroupCombatMode.FIRE_AT_WILL && !WEverProvoked, "W: lethal (fire at will) without ever being fired upon") && ok;
   if (ok) Warning = 1;
   WPhase = 3; WAt = Now(); return;
  }
  if (WPhase == 3)
  {
   if (Sum(WShots) > WShotsAtLethal && Lethal == 0)
   {
    Lethal = 1;
    Check(true, "W: lethal fire after the pause");
   }
   if (Alive(s_StandIn) && Now() - WAt < 25) return;
   PrintFormat("[ESR SQUAD ATTR W LETHAL] rounds=%1 standInAlive=%2 standInHealth=%3 wLiving=%4 wLod=%5 %6", Sum(WShots) - WShotsAtLethal, Alive(s_StandIn), Health(s_StandIn), Living(W), Lod(W), Describe(W));
   if (Lethal == 0) Check(false, "W: lethal fire after the pause");
   // End the contact for good: the stand-in is gone.
   if (Alive(s_StandIn)) Kill(s_StandIn);
   WPhase = 4; WAt = Now(); return;
  }
  if (WPhase == 4)
  {
   bool rearmed = W.EGS_GetWarnState() == 0 && External(W) == EAIGroupCombatMode.RETURN_FIRE;
   if (!rearmed) { if (CaseTimeout(WAt, EGS_Manager.REARM_DELAY_S + 2 * EGS_Manager.REARM_RETRY_S + 15, "W: re-armed after the contact; " + Describe(W))) WDone = true; return; }
   PrintFormat("[ESR SQUAD ATTR W REARM] after=%1 %2", Now() - LethalAt, Describe(W));
   if (Check(!WEverProvoked && W.EGS_UsesStrictReturnFire(), "W: re-armed (RETURN_FIRE, strict, never provoked)")) Rearm = 1;
   WDone = true;
  }
 }

 //------------------------------------------------------------------------------------------------
 // F: a soldier with his own Return Fire Only stays out of his Fire on Sight squad's fire.
 void StepF()
 {
  if (!F) { Check(false, "F: squad F still exists"); FDone = true; return; }
  if (FPhase == 0)
  {
   if (!Spawned(F, SQUAD_SIZE)) { if (CaseTimeout(FAt, 60, "F: F finished its initial spawn")) EndF(); return; }
   Holdout = Member(F, null);
   if (!Check(Holdout != null, "F: a holdout chosen in F")) { EndF(); return; }
   GmWrite(SoldierRoe, Holdout, EGS_Settings.ROE_RETURN_FIRE);
   SCR_AICombatComponent holdCombat = Combat(Holdout);
   bool applied = Check(Holdout.EGS_GetRoeOverride() == EGS_Settings.ROE_RETURN_FIRE && holdCombat && holdCombat.EGS_GetUnitRoe() == EGS_Settings.ROE_RETURN_FIRE && F.EGS_WatchesProvocation(), "F: Game Master save: the holdout's own Return Fire Only, applied at once; F watched");
   if (!applied) { EndF(); return; }
   HoldShotsCounter = WatchOne(Holdout);
   Watch(F, FShots, Holdout);
   // The target appears only now, after the holdout's ROE is in place.
   E2 = SpawnGroup(US_TEAM, SiteF + EnemyOffset);
   SetVanillaMode(E2, EAIGroupCombatMode.HOLD_FIRE);
   if (!Check(E2 != null, "F: HOLD_FIRE fire team E2 spawned in front of F")) { EndF(); return; }
   FPhase = 1; FAt = Now(); return;
  }
  if (FPhase == 1)
  {
   if (RunsGroupSuppress(Holdout)) HoldSuppressSamples++;
   array<AIAgent> agents = {};
   F.GetAgents(agents);
   foreach (AIAgent agent : agents)
   {
    if (!agent) continue;
    SCR_ChimeraCharacter mate = SCR_ChimeraCharacter.Cast(agent.GetControlledEntity());
    if (mate && mate != Holdout && RunsGroupSuppress(mate)) MateSuppressSamples++;
   }
   if (MatesFiredAt < 0 && Sum(FShots) > 0) MatesFiredAt = Now();
   bool settled = MatesFiredAt >= 0 && (Now() - MatesFiredAt >= 15 || Living(E2) == 0);
   if (!settled) { if (CaseTimeout(FAt, 60, string.Format("F: F's other soldiers engaged E2 (mateRounds=%1 e2Living=%2 fLod=%3)", Sum(FShots), Living(E2), Lod(F)))) EndF(); return; }
   int holdoutRounds = -1;
   if (HoldShotsCounter) holdoutRounds = HoldShotsCounter.Count;
   SCR_AICombatComponent holdoutCombat = Combat(Holdout);
   int mode = -1;
   if (holdoutCombat) mode = holdoutCombat.GetCombatMode();
   Leak = holdoutRounds + HoldSuppressSamples;
   PrintFormat("[ESR SQUAD ATTR F LEAK] holdoutRounds=%1 holdoutSuppressSamples=%2 mateRounds=%3 mateSuppressSamples=%4 holdoutMode=%5 e2Living=%6 %7", holdoutRounds, HoldSuppressSamples, Sum(FShots), MateSuppressSamples, mode, Living(E2), Describe(F));
   Check(!F.EGS_IsProvoked(), "F: never fired upon (E2 holds fire)");
   Check(holdoutRounds == 0 && HoldSuppressSamples == 0 && (!Alive(Holdout) || mode == EAIGroupCombatMode.HOLD_FIRE), "F: the holdout fired no round and never ran the squad's suppressive fire");
   EndF();
  }
 }

 // F's case is over (or gave up): Exempt (vanilla) and hold fire, so F fires at nobody for
 // the rest of the run.
 void EndF()
 {
  if (F)
  {
   GmWrite(SquadRoe, F, EGS_Settings.GROUP_ROE_EXEMPT);
   SetVanillaMode(F, EAIGroupCombatMode.HOLD_FIRE);
  }
  FDone = true;
 }

 //------------------------------------------------------------------------------------------------
 // V: the squad's own combat mode survives a Unit Caching Full cycle under an EXPBG ROE.
 EBG_CacheZone SpawnZone(vector point)
 {
  EBG_CacheZone zone = EBG_CacheZone.Cast(Spawn(ZONE, point));
  if (!zone) return null;
  // Full mode, 30 m affected radius, wake 100 m, sleep 300 m, 5 s sleep delay, no cleanup,
  // no markers, debug messages on (as the override fixture, smaller radius).
  zone.SetValue(1, 1); zone.SetValue(2, 0); zone.SetValue(3, 30);
  zone.SetValue(4, 100); zone.SetValue(5, 300); zone.SetValue(12, 5);
  zone.SetValue(15, 0); zone.SetValue(18, 0); zone.SetValue(21, 1);
  zone.SetValue(0, 1);
  return zone;
 }

 void StepV()
 {
  if (VPhase == 0)
  {
   if (!Spawned(V, SQUAD_SIZE)) { if (CaseTimeout(VAt, 60, "V: V finished its initial spawn")) VDone = true; return; }
   GmWrite(VanillaMode, V, ENTRY_HOLD);
   Check(External(V) == EAIGroupCombatMode.HOLD_FIRE && !V.EGS_IsRoeTouched() && V.EGS_GetRoeOverride() == EGS_Settings.GROUP_ROE_DEFAULT, "V: vanilla Set combat mode hold fire on a squad EXPBG does not steer is plain vanilla");
   VPhase = 1; VAt = Now(); return;
  }
  if (VPhase == 1)
  {
   GmWrite(SquadRoe, V, EGS_Settings.ROE_RETURN_FIRE);
   if (!Check(External(V) == EAIGroupCombatMode.RETURN_FIRE && Original(V) == EAIGroupCombatMode.HOLD_FIRE, "V: Return Fire Only over its own hold fire; " + Describe(V))) { VDone = true; return; }
   FullZone = SpawnZone(Origin + SiteV);
   if (!Check(FullZone != null, "V: Full cache zone spawned over V")) { VDone = true; return; }
   VPhase = 2; VAt = Now(); return;
  }
  if (VPhase == 2)
  {
   if (!V) { Check(false, "V: enrolled before it was cached"); VDone = true; return; }
   Record = EBG_CacheManager.Get().FindGroup(V);
   if (!Record || Record.Members.Count() != SQUAD_SIZE) { if (CaseTimeout(VAt, 60, "V: enrolled by the Full zone")) VDone = true; return; }
   VPhase = 3; VAt = Now(); return;
  }
  if (!Record) { Check(false, "V: logical cache record retained across the cycle"); VDone = true; return; }
  if (VPhase == 3)
  {
   if (!Record.Full || Record.Full.GetState() != EBG_FullGroupPhase.CACHED) { if (CaseTimeout(VAt, 90, "V: Full cached; reason='" + Record.Reason + "' recovery='" + Record.Recovery + "'")) VDone = true; return; }
   EBG_PrefabFullCache full = EBG_PrefabFullCache.Cast(Record.Full);
   Check(!V && full && full.EGS_GetSnapshotSquadRoe() == EGS_Settings.ROE_RETURN_FIRE, "V: deleted while cached; the snapshot holds Return Fire Only");
   VPhase = 4; VAt = Now(); return;
  }
  if (VPhase == 4)
  {
   if (Now() - VAt < 3) return;
   s_Presence.Insert(Ground(Origin + SiteV, 1.8));
   VPhase = 5; VAt = Now(); return;
  }
  if (VPhase == 5)
  {
   if (Record.Full || !Record.Group || Record.Group.GetAgentsCount() != SQUAD_SIZE || Record.Recovery != "") { if (CaseTimeout(VAt, 60, "V: woke from Full with six members; reason='" + Record.Reason + "' recovery='" + Record.Recovery + "'")) VDone = true; return; }
   V = Record.Group;
   VPhase = 6; VAt = Now(); return;
  }
  if (VPhase == 6)
  {
   // The wake applies the squad's EXPBG ROE at once; a later AI Global Skills tick must not
   // change it.
   if (Now() - VAt < 3) return;
   PrintFormat("[ESR SQUAD ATTR V WOKEN] %1", Describe(V));
   bool ok = Check(V.EGS_GetRoeOverride() == EGS_Settings.ROE_RETURN_FIRE && External(V) == EAIGroupCombatMode.RETURN_FIRE && Original(V) == EAIGroupCombatMode.HOLD_FIRE, "V: woken squad has Return Fire Only over its own hold fire (not the EXPBG mode)");
   GmWrite(SquadRoe, V, EGS_Settings.GROUP_ROE_EXEMPT);
   ok = Check(!V.EGS_IsRoeTouched() && External(V) == EAIGroupCombatMode.HOLD_FIRE, "V: Exempt (vanilla) after the wake restores hold fire; " + Describe(V)) && ok;
   if (ok) CachedOk = 1;
   s_Presence.Clear();
   VDone = true;
  }
 }
}

// Stand-in Game Master: the editing Game Master check passes for the fixture's player id
// with no editor (the attribute code after it is production).
modded class EGS_Attributes
{
 override static bool IsEditingGameMaster(SCR_AttributesManagerEditorComponent manager, int playerID)
 {
  if (!manager && playerID == EXPG_GarrisonGameplay.STAND_IN_GM) return true;
  return super.IsEditingGameMaster(manager, playerID);
 }
}

// Stand-in player for Warning Shots First. Production reaches the player check through
// EGS_Manager.IsPlayerTarget (qualified calls from EGS_Group and EGS_CombatComponent), which
// calls IsPlayerControlled from inside the original class: both are overridden, so the seam
// holds however Enforce binds that inner call.
modded class EGS_Manager
{
 override static bool IsPlayerControlled(IEntity entity)
 {
  if (EXPG_GarrisonGameplay.IsStandIn(entity)) return true;
  return super.IsPlayerControlled(entity);
 }

 override static bool IsPlayerTarget(IEntity target)
 {
  if (EXPG_GarrisonGameplay.IsStandIn(target)) return true;
  return super.IsPlayerTarget(target);
 }
}

// Presence seam, as in the override, dialog cache and unit-cleanup fixtures.
modded class EBG_CacheManager
{
 override protected void UpdatePlayers()
 {
  super.UpdatePlayers();
  if (EXPG_GarrisonGameplay.s_Presence)
  {
   foreach (vector presence : EXPG_GarrisonGameplay.s_Presence) Players.Insert(presence);
  }
 }
}
