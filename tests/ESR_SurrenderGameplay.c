// TEST ONLY. EXPBG AI Surrender server fixture (dedicated server, no players, no GM UI).
// pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -FixturePath tests/ESR_SurrenderGameplay.c -TimeoutSeconds 300 -OrchestratorSlotGranted
// The runner copies this file to EXPG_GarrisonGameplay.c; the class names are fixed. Its
// built-in evidence matcher is Garrison's, so judge this run by the [ESR TEST ...] lines
// in build/gameplay-*/ (see the module report): RESULT must say failures=0 reason=complete.
// Real module prefab and public settings, real squads, native Kill, production surrender,
// interrogation (server API; the interrogation point stands in for the player) and upkeep.
class EXPG_GarrisonGameplayClass : GenericEntityClass {}
class EXPG_GarrisonGameplay : GenericEntity
{
 static const float FIXTURE_SECONDS = 180;
 static const ResourceName MODULE = "{7F2E668385984EA1}PrefabsEditable/EXPSR/ESR_SurrenderModule.et";
 static const ResourceName SQUAD = "{E552DABF3636C2AD}Prefabs/Groups/OPFOR/Group_USSR_RifleSquad.et";
 static const int SIZE = 6;
 vector Origin = "4773.46 0 7094.57";
 vector WitnessOffset = "300 0 300";
 ESR_SurrenderModule Surrender;
 SCR_AIGroup Broken;
 SCR_AIGroup Witness;
 ref array<SCR_ChimeraCharacter> Victims = {};
 // Strong references: the records must stay readable after the manager releases them.
 ref ESR_Prisoner Revealer;
 ref ESR_Prisoner Talker;
 ref ESR_Prisoner Refuser;
 ref ESR_Prisoner Doomed;
 int Phase;
 int Checks;
 int Failures;
 int MarkersBefore = -1;
 int FirstMarker = -1;
 float Started;
 float PhaseAt;
 float Next;
 bool Finished;

 void EXPG_GarrisonGameplay(IEntitySource src, IEntity parent) { SetEventMask(EntityEvent.INIT | EntityEvent.FRAME); }
 float Now() { return GetGame().GetWorld().GetWorldTime() * 0.001; }

 override void EOnInit(IEntity owner)
 {
  if (!Replication.IsServer()) { ClearEventMask(EntityEvent.FRAME); return; }
  Started = Now(); Next = Started + 10; PhaseAt = Started;
  PrintFormat("[ESR TEST BEGIN] origin=%1 witnessOffset=%2 deadline=%3", Origin, WitnessOffset, FIXTURE_SECONDS);
 }

 bool Check(bool ok, string label)
 {
  Checks++;
  if (!ok) Failures++;
  PrintFormat("[ESR TEST CHECK] pass=%1 %2", ok, label);
  return ok;
 }

 void Finish(string reason)
 {
  if (Finished) return;
  Finished = true;
  ClearEventMask(EntityEvent.FRAME);
  PrintFormat("[ESR TEST RESULT] checks=%1 failures=%2 prisoners=%3 reason=%4", Checks, Failures, ESR_SurrenderManager.PrisonerCount(), reason);
  GetGame().RequestClose();
 }

 vector Ground(vector p, float lift) { p[1] = GetGame().GetWorld().GetSurfaceY(p[0], p[2]) + lift; return p; }

 EntitySpawnParams Params(vector p)
 {
  EntitySpawnParams spawn = new EntitySpawnParams();
  spawn.TransformMode = ETransformMode.WORLD;
  Math3D.AnglesToMatrix(vector.Zero, spawn.Transform);
  spawn.Transform[3] = Ground(p, 0.3);
  return spawn;
 }

 SCR_AIGroup SpawnSquad(vector p)
 {
  // Keep the Resource and the spawned entity in locals before casting.
  Resource squad = Resource.Load(SQUAD);
  IEntity spawned = GetGame().SpawnEntityPrefab(squad, GetGame().GetWorld(), Params(p));
  return SCR_AIGroup.Cast(spawned);
 }

 void Configure(int chance, int threshold, int reveal, int identity, int attempts, bool enabled)
 {
  Surrender.ApplySetting(ESR_Settings.ENABLED, ESR_Settings.FromBool(enabled));
  Surrender.ApplySetting(ESR_Settings.CHANCE, chance);
  Surrender.ApplySetting(ESR_Settings.THRESHOLD, threshold);
  Surrender.ApplySetting(ESR_Settings.RANDOM, 0);
  Surrender.ApplySetting(ESR_Settings.REVEAL, reveal);
  Surrender.ApplySetting(ESR_Settings.IDENTITY, identity);
  Surrender.ApplySetting(ESR_Settings.RADIUS, 2000);
  Surrender.ApplySetting(ESR_Settings.ATTEMPTS, attempts);
  Surrender.ApplySetting(ESR_Settings.LIFETIME, 0);
  Surrender.ApplySetting(ESR_Settings.DIAGNOSTICS, 1);
 }

 int StaticMarkers()
 {
  SCR_MapMarkerManagerComponent markers = SCR_MapMarkerManagerComponent.GetInstance();
  if (!markers) return -1;
  return markers.GetStaticMarkers().Count();
 }

 override void EOnFrame(IEntity owner, float timeSlice)
 {
  if (Finished || Now() < Next) return;
  Next = Now() + 0.5;
  if (Now() - Started > FIXTURE_SECONDS) { Check(false, string.Format("phase %1 finished before the fixture deadline", Phase)); Finish("timeout"); return; }
  Step();
 }

 void Step()
 {
  if (Phase == 0)
  {
   if (!Check(GetGame().GetPlayerManager().GetPlayerCount() == 0 && ESR_SurrenderManager.PrisonerCount() == 0, "isolated server: no players, no prisoners")) { Finish("setup"); return; }
   Resource modulePrefab = Resource.Load(MODULE);
   IEntity moduleEntity = GetGame().SpawnEntityPrefab(modulePrefab, GetGame().GetWorld(), Params(Origin));
   Surrender = ESR_SurrenderModule.Cast(moduleEntity);
   if (!Check(Surrender != null, "surrender module prefab spawned")) { Finish("setup"); return; }
   Check(ESR_SurrenderModule.ActiveCount() == 1 && ESR_SurrenderManager.IsListening(), "module registered and listening with prefab defaults");
   Check(Surrender.GetSetting(ESR_Settings.CHANCE) == ESR_Settings.Default(ESR_Settings.CHANCE) && Surrender.GetSetting(ESR_Settings.ENABLED) == 1, "prefab defaults adopted as the global settings");
   Configure(100, 10, 100, 0, 3, true);
   Check(Surrender.GetSetting(ESR_Settings.CHANCE) == 100 && ESR_Settings.Get(ESR_Settings.THRESHOLD) == 10 && ESR_Settings.Get(ESR_Settings.RADIUS) == 2000, "public settings applied and mirrored");
   Surrender.ApplySetting(ESR_Settings.ATTEMPTS, 99);
   Check(ESR_Settings.Get(ESR_Settings.ATTEMPTS) == 5, "out-of-range setting clamped");
   Surrender.ApplySetting(ESR_Settings.ATTEMPTS, 3);
   Broken = SpawnSquad(Origin + Vector(0, 0, 20));
   Witness = SpawnSquad(Origin + WitnessOffset);
   if (!Check(Broken != null && Witness != null, "two native USSR squads spawned")) { Finish("setup"); return; }
   Phase = 1; PhaseAt = Now(); return;
  }
  if (Phase == 1)
  {
   if (!Broken || !Witness || Broken.GetAgentsCount() != SIZE || Witness.GetAgentsCount() != SIZE)
   {
    if (Now() - PhaseAt > 30) { Check(false, "both squads completed their initial spawn"); Finish("setup"); }
    return;
   }
   // Two casualties (never the leader): 2 of 6 = 33% >= 10%, chance 100: the four able soldiers surrender.
   array<AIAgent> agents = {};
   Broken.GetAgents(agents);
   IEntity leader = Broken.GetLeaderEntity();
   foreach (AIAgent agent : agents)
   {
    SCR_ChimeraCharacter member = SCR_ChimeraCharacter.Cast(agent.GetControlledEntity());
    if (member && member != leader && Victims.Count() < 2) Victims.Insert(member);
   }
   foreach (SCR_ChimeraCharacter victim : Victims)
   {
    SCR_CharacterDamageManagerComponent damage = SCR_CharacterDamageManagerComponent.Cast(victim.GetDamageManager());
    if (damage) damage.Kill(Instigator.CreateInstigator(null));
   }
   PrintFormat("[ESR TEST KILLED] casualties=%1 squad=%2", Victims.Count(), Broken);
   Phase = 2; PhaseAt = Now(); return;
  }
  if (Phase == 2)
  {
   if (ESR_SurrenderManager.PrisonerCount() < SIZE - 2)
   {
    if (Now() - PhaseAt > 10) { Check(false, string.Format("four survivors surrendered (got %1)", ESR_SurrenderManager.PrisonerCount())); Finish("surrender"); }
    return;
   }
   // Weapon drops and the sit-down are scheduled; give them time.
   if (Now() - PhaseAt < 8) return;
   Check(ESR_SurrenderManager.PrisonerCount() == SIZE - 2, "exactly the four able survivors surrendered");
   int outsideGroup, passive, unarmed, civilian, points, sitting;
   for (int i = 0; i < ESR_SurrenderManager.PrisonerCount(); i++)
   {
    ESR_Prisoner prisoner = ESR_SurrenderManager.GetPrisonerAt(i);
    if (!prisoner || !prisoner.Character) continue;
    SCR_ChimeraCharacter character = prisoner.Character;
    if (!ESR_SurrenderManager.GroupOf(character)) outsideGroup++;
    AIControlComponent control = AIControlComponent.Cast(character.FindComponent(AIControlComponent));
    if (control && !control.IsAIActivated()) passive++;
    BaseWeaponManagerComponent weapons = BaseWeaponManagerComponent.Cast(character.FindComponent(BaseWeaponManagerComponent));
    array<IEntity> carried = {};
    if (weapons) weapons.GetWeaponsList(carried);
    if (carried.IsEmpty()) unarmed++;
    Faction faction = character.GetFaction();
    if (faction && faction.GetFactionKey() == ESR_SurrenderManager.CIVILIAN_FACTION) civilian++;
    if (prisoner.Point && vector.Distance(prisoner.Point.GetOrigin(), character.GetOrigin() + vector.Up * ESR_SurrenderManager.CHEST_HEIGHT) < 1.2) points++;
    SCR_CharacterControllerComponent controller = SCR_CharacterControllerComponent.Cast(character.GetCharacterController());
    if (controller && controller.IsLoitering()) sitting++;
    if (i == 0) Revealer = prisoner;
    else if (i == 1) Talker = prisoner;
    else if (i == 2) Refuser = prisoner;
    else if (i == 3) Doomed = prisoner;
   }
   // An emptied squad may already be deleted by the base game.
   int squadLeft = -1;
   if (Broken) squadLeft = Broken.GetAgentsCount();
   PrintFormat("[ESR TEST PRISONERS] count=%1 outsideGroup=%2 aiOff=%3 unarmed=%4 civilian=%5 points=%6 sitting=%7 squadLeft=%8", ESR_SurrenderManager.PrisonerCount(), outsideGroup, passive, unarmed, civilian, points, sitting, squadLeft);
   Check(outsideGroup == SIZE - 2, "prisoners left their squad");
   Check(passive == SIZE - 2, "prisoner AI deactivated");
   Check(unarmed == SIZE - 2, "prisoners dropped every weapon");
   Check(civilian == SIZE - 2, "prisoners joined the civilian faction");
   Check(points == SIZE - 2, "one interrogation point at each prisoner's chest");
   Check(sitting == SIZE - 2, "prisoners hold the vanilla sit-on-ground loiter");
   Phase = 3; PhaseAt = Now(); return;
  }
  if (Phase == 3)
  {
   if (!Check(Revealer && Revealer.Point && Talker && Refuser && Doomed, "four prisoner records with points")) { Finish("records"); return; }
   MarkersBefore = StaticMarkers();
   int outcome = ESR_SurrenderManager.Interrogate(Revealer.Point, Revealer.Point, 0);
   int markersAfter = StaticMarkers();
   FirstMarker = Revealer.MarkerId;
   PrintFormat("[ESR TEST REVEAL] outcome=%1 count=%2 distance=%3 bearing=%4 marker=%5 markers=%6->%7 text='%8'", outcome, Revealer.RevealCount, Revealer.RevealDistance, Revealer.RevealBearing, FirstMarker, MarkersBefore, markersAfter, Revealer.Point.Describe(outcome, Revealer.RevealCount, Revealer.RevealDistance, Revealer.RevealBearing, 2));
   Check(outcome == ESR_SurrenderManager.OUTCOME_REVEAL, "interrogation reveals a nearby squad");
   Check(Revealer.RevealCount == SIZE, "revealed squad is the intact witness squad");
   Check(Revealer.RevealDistance >= 350 && Revealer.RevealDistance <= 500, "revealed distance matches the witness offset");
   Check(Revealer.RevealBearing == 1, "revealed bearing is north-east");
   if (MarkersBefore >= 0) Check(FirstMarker >= 0 && markersAfter == MarkersBefore + 1, "one static intel marker created");
   else PrintFormat("[ESR TEST NOTE] no SCR_MapMarkerManagerComponent in this game mode; marker check skipped");
   Phase = 4; PhaseAt = Now(); return;
  }
  if (Phase == 4)
  {
   // Past the per-prisoner question cooldown: asking again repeats the fixed answer.
   if (Now() - PhaseAt < ESR_SurrenderManager.QUESTION_COOLDOWN + 0.5) return;
   int repeat = ESR_SurrenderManager.Interrogate(Revealer.Point, Revealer.Point, 0);
   Check(repeat == ESR_SurrenderManager.OUTCOME_REVEAL && Revealer.MarkerId == FirstMarker && (MarkersBefore < 0 || StaticMarkers() == MarkersBefore + 1), "repeated interrogation repeats the answer without a second marker");
   Check(ESR_SurrenderManager.Interrogate(Revealer.Point, Revealer.Point, 0) == ESR_SurrenderManager.OUTCOME_BUSY, "question cooldown refuses an immediate repeat");
   Configure(100, 10, 0, 100, 3, true);
   int identity = ESR_SurrenderManager.Interrogate(Talker.Point, Talker.Point, 0);
   string text = Talker.Point.Describe(identity, 0, 0, 0, 2);
   PrintFormat("[ESR TEST IDENTITY] outcome=%1 leader=%2 text='%3'", identity, Talker.Dossier.Leader, text);
   Check(identity == ESR_SurrenderManager.OUTCOME_IDENTITY, "interrogation gives identity");
   Check(!Talker.Dossier.Name.IsEmpty() || !Talker.Dossier.Surname.IsEmpty(), "identity dossier holds a name");
   Check(Talker.Dossier.Leader || !Talker.Dossier.LeaderName.IsEmpty() || !Talker.Dossier.LeaderSurname.IsEmpty(), "identity dossier names the squad leader");
   Configure(100, 10, 0, 0, 2, true);
   Check(ESR_SurrenderManager.Interrogate(Refuser.Point, Refuser.Point, 0) == ESR_SurrenderManager.OUTCOME_REFUSED, "first refusal");
   Phase = 5; PhaseAt = Now(); return;
  }
  if (Phase == 5)
  {
   if (Now() - PhaseAt < ESR_SurrenderManager.QUESTION_COOLDOWN + 0.5) return;
   Check(ESR_SurrenderManager.Interrogate(Refuser.Point, Refuser.Point, 0) == ESR_SurrenderManager.OUTCOME_REFUSED && Refuser.Attempts == 2, "second refusal uses the last attempt");
   Phase = 6; PhaseAt = Now(); return;
  }
  if (Phase == 6)
  {
   if (Now() - PhaseAt < ESR_SurrenderManager.QUESTION_COOLDOWN + 0.5) return;
   Check(ESR_SurrenderManager.Interrogate(Refuser.Point, Refuser.Point, 0) == ESR_SurrenderManager.OUTCOME_SILENT, "prisoner goes silent after his attempts");
   // A prisoner's death releases him and deletes his point at once (event-driven).
   SCR_ChimeraCharacter doomed = Doomed.Character;
   SCR_CharacterDamageManagerComponent damage = SCR_CharacterDamageManagerComponent.Cast(doomed.GetDamageManager());
   if (damage) damage.Kill(Instigator.CreateInstigator(null));
   Phase = 7; PhaseAt = Now(); return;
  }
  if (Phase == 7)
  {
   if (ESR_SurrenderManager.FindPrisoner(Doomed.Character) || Doomed.Point)
   {
    if (Now() - PhaseAt > 3) { Check(false, "dead prisoner released and point deleted"); Finish("release"); }
    return;
   }
   Check(ESR_SurrenderManager.PrisonerCount() == SIZE - 3, "dead prisoner released and point deleted");
   // Switched off: further casualties never create prisoners.
   Surrender.ApplySetting(ESR_Settings.ENABLED, 0);
   Check(!ESR_SurrenderManager.IsListening() && ESR_SurrenderManager.IsWatching(), "disabled module stops listening; prisoners stay watched");
   array<AIAgent> agents = {};
   Witness.GetAgents(agents);
   int killed;
   foreach (AIAgent agent : agents)
   {
    SCR_ChimeraCharacter member = SCR_ChimeraCharacter.Cast(agent.GetControlledEntity());
    if (!member || killed >= 4) continue;
    SCR_CharacterDamageManagerComponent witnessDamage = SCR_CharacterDamageManagerComponent.Cast(member.GetDamageManager());
    if (witnessDamage) { witnessDamage.Kill(Instigator.CreateInstigator(null)); killed++; }
   }
   Phase = 8; PhaseAt = Now(); return;
  }
  if (Phase == 8)
  {
   if (Now() - PhaseAt < 4) return;
   Check(ESR_SurrenderManager.PrisonerCount() == SIZE - 3, "no surrender while the module is disabled");
   Finish("complete");
  }
 }
}
