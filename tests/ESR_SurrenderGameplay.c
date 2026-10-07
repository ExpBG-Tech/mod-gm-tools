// TEST ONLY. EXPBG AI Surrender server fixture (dedicated server, no players, no GM UI).
// pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -FixturePath tests/ESR_SurrenderGameplay.c -TimeoutSeconds 420 -OrchestratorSlotGranted -ExpectResult '\[ESR TEST RESULT\] checks=[1-9]\d* failures=0 prisoners=4 reason=complete'
// The runner copies this file to EXPG_GarrisonGameplay.c; the class names are fixed. Its
// built-in evidence matcher is Garrison's, so judge this run by the [ESR TEST ...] lines
// in build/gameplay-*/ (see the module report): RESULT must say failures=0 reason=complete.
// Real module prefab and public settings, real squads, native Kill, production surrender,
// interrogation (server API; the interrogation point stands in for the player) and upkeep.
// Face case: each prisoner's Interrogate context ("face") lies within 0.25 m of his head
// bone, ahead of it and above his torso's Chest context, and its collider answers a trace
// from straight ahead; after a native 0.8 m teleport the same point follows his face.
// Cache case: a squad enrolled in a real Unit Caching zone loses one prisoner; the awake
// record forgets him at once, a second soldier marked before he leaves the native group is
// retired by the regroup scan without a hold, the other four Simulation cache and wake, and
// the prisoner is never cached, restored, kept in a cleanup ledger or deleted.
class EXPG_GarrisonGameplayClass : GenericEntityClass {}
class EXPG_GarrisonGameplay : GenericEntity
{
 static const float FIXTURE_SECONDS = 300;
 static const ResourceName CACHE_ZONE = "{7E1080ED8F0633FD}PrefabsEditable/EXPBG/EBG_CacheZone.et";
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
 // Face case: the teleported prisoner's point and his spot before the move.
 ESR_InterrogationPoint MovedPoint;
 vector MovedFrom;
 // Face case: faces on the previous step and when one last moved (FacesSettled).
 ref array<vector> SettleFaces = {};
 float SettleMovedAt = -1;
 // Unit Caching case: dry land used by the unit-cleanup fixture, far from both squads.
 vector CacheOffset = "-600 0 -600";
 SCR_AIGroup CacheSquad;
 EBG_CacheZone CacheZone;
 ref EBG_CacheGroup CacheRecord;
 SCR_ChimeraCharacter CacheCaptive;
 EntityID CacheCaptiveId;
 SCR_ChimeraCharacter CacheLeaver;
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

 // AI Surrender's state, untouched by Unit Caching: the same living entity, out of every
 // squad, AI off, visible, never Simulation cached, in no cache record or cleanup ledger.
 bool CaptiveUntouched(string label)
 {
  IEntity found;
  if (GetGame().GetWorld()) found = GetGame().GetWorld().FindEntityByID(CacheCaptiveId);
  bool sameEntity = CacheCaptive != null && found == CacheCaptive;
  bool living, isPrisoner, outside, aiOff, shown, simCached, inRecord, cleanupHeld;
  if (sameEntity)
  {
   CharacterControllerComponent captiveController = CacheCaptive.GetCharacterController();
   living = captiveController != null && !captiveController.IsDead();
   isPrisoner = ESR_SurrenderManager.FindPrisoner(CacheCaptive) != null;
   outside = ESR_SurrenderManager.GroupOf(CacheCaptive) == null && CacheCaptive.GetCharacterGroup() == null;
   AIControlComponent captiveControl = AIControlComponent.Cast(CacheCaptive.FindComponent(AIControlComponent));
   aiOff = captiveControl != null && !captiveControl.IsAIActivated();
   shown = (CacheCaptive.GetFlags() & EntityFlags.VISIBLE) != 0;
   simCached = CacheCaptive.EBG_IsSimulationCached();
   inRecord = EBG_CacheManager.Get().FindMember(CacheCaptive) != null;
   cleanupHeld = EBG_CacheCleanup.Get().IsHeld(CacheCaptive);
  }
  PrintFormat("[ESR TEST CACHE CAPTIVE] when='%1' same=%2 alive=%3 prisoner=%4 outsideSquads=%5 aiOff=%6 visible=%7 simCached=%8", label, sameEntity, living, isPrisoner, outside, aiOff, shown, simCached);
  PrintFormat("[ESR TEST CACHE CAPTIVE] when='%1' cacheMember=%2 cleanupHeld=%3", label, inRecord, cleanupHeld);
  return Check(sameEntity && living && isPrisoner && outside && aiOff && shown && !simCached && !inRecord && !cleanupHeld, "cache case: prisoner untouched by Unit Caching " + label);
 }

 int StaticMarkers()
 {
  SCR_MapMarkerManagerComponent markers = SCR_MapMarkerManagerComponent.GetInstance();
  if (!markers) return -1;
  return markers.GetStaticMarkers().Count();
 }

 // A dedicated server moves interrogation points only in the manager's 5 s upkeep
 // (clients follow every frame tick), so the server's points are measured once every face
 // has held still (reads 0.5 s apart within 3 cm) for one upkeep interval plus a margin.
 bool FacesSettled(array<ESR_Prisoner> prisoners)
 {
  bool moved = SettleMovedAt < 0 || SettleFaces.Count() != prisoners.Count();
  SettleFaces.Resize(prisoners.Count());
  for (int i = 0; i < prisoners.Count(); i++)
  {
   vector face = SettleFaces[i];
   if (prisoners[i] && prisoners[i].Character) face = ESR_SurrenderManager.PointPosition(prisoners[i]);
   if (vector.Distance(face, SettleFaces[i]) > 0.03) moved = true;
   SettleFaces[i] = face;
  }
  if (moved) SettleMovedAt = Now();
  return Now() - SettleMovedAt >= ESR_SurrenderManager.UPKEEP_MS * 0.001 + 0.5;
 }

 // Face case: the Interrogate action's context sits on the prisoner's face, within 0.25 m
 // of his head bone (read here, independently of production), ahead of it and above his
 // Chest context; the point follows his face and its collider answers a trace from ahead.
 bool FaceReady(ESR_Prisoner prisoner, string label)
 {
  if (!prisoner || !prisoner.Character || !prisoner.Point)
  {
   PrintFormat("[ESR TEST FACE] when='%1' point=0", label);
   return false;
  }
  SCR_ChimeraCharacter character = prisoner.Character;
  ESR_InterrogationPoint point = prisoner.Point;
  ActionsManagerComponent actions = ActionsManagerComponent.Cast(point.FindComponent(ActionsManagerComponent));
  UserActionContext faceContext;
  int contexts = -1;
  if (actions)
  {
   faceContext = actions.GetContext(ESR_InterrogationPoint.CONTEXT_NAME);
   contexts = actions.GetContextCount();
  }
  bool interrogate;
  float radius = -1;
  vector spot = point.GetOrigin();
  if (faceContext)
  {
   interrogate = faceContext.GetActionsCount() == 1 && ESR_InterrogateAction.Cast(faceContext.GetAction(0)) != null;
   radius = faceContext.GetRadius();
   spot = faceContext.GetOrigin();
  }
  vector feet = character.GetOrigin();
  vector head = feet;
  bool bone;
  Animation animation = character.GetAnimation();
  if (animation)
  {
   TNodeId headBone = animation.GetBoneIndex("Head");
   vector boneMatrix[4];
   if (headBone >= 0 && animation.GetBoneMatrix(headBone, boneMatrix))
   {
    vector world[4];
    character.GetWorldTransform(world);
    Math3D.MatrixMultiply4(world, boneMatrix, boneMatrix);
    head = boneMatrix[3];
    bone = true;
   }
  }
  vector forward = character.GetWorldTransformAxis(2);
  forward[1] = 0;
  forward.Normalize();
  float fromHead = vector.Distance(spot, head);
  float ahead = vector.Dot(spot - head, forward);
  float aboveChest = -1;
  ActionsManagerComponent body = ActionsManagerComponent.Cast(character.FindComponent(ActionsManagerComponent));
  UserActionContext chest;
  if (body) chest = body.GetContext("Chest");
  if (chest)
  {
   vector chestSpot = chest.GetOrigin();
   aboveChest = spot[1] - chestSpot[1];
  }
  float drift = vector.Distance(point.GetOrigin(), ESR_SurrenderManager.PointPosition(prisoner));
  TraceParam trace = new TraceParam();
  trace.Start = spot + forward;
  trace.End = spot;
  trace.Flags = TraceFlags.ENTS;
  trace.TargetLayers = EPhysicsLayerDefs.Interaction;
  trace.Include = point;
  float fraction = GetGame().GetWorld().TraceMove(trace, null);
  bool traced = trace.TraceEnt == point && fraction > 0.75 && fraction < 0.95;
  PrintFormat("[ESR TEST FACE] when='%1' context=%2 contexts=%3 interrogate=%4 radius=%5 bone=%6 fromHead=%7 ahead=%8 aboveChest=%9", label, faceContext != null, contexts, interrogate, radius, bone, fromHead, ahead, aboveChest);
  PrintFormat("[ESR TEST FACE] when='%1' headHeight=%2 drift=%3 traceFraction=%4 traced=%5", label, head[1] - feet[1], drift, fraction, traced);
  return faceContext && contexts == 1 && interrogate && radius > 0 && radius <= 0.3 && bone && fromHead <= 0.25 && ahead > 0.05 && aboveChest > 0 && drift <= 0.1 && traced;
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
   // The fixture mod set has no ACE: the optional ACE Captives adapter must say so and stay idle.
   // Absence is judged by ACE's type, as the adapter detects it: stable ACE has another GUID.
   array<string> addons = {};
   GameProject.GetLoadedAddons(addons);
   bool aceAddon = addons.Contains(ESR_AceCaptives.ADDON_CAPTIVES_DEV);
   string aceSystemName = ESR_AceCaptives.TYPE_SYSTEM;
   typename aceSystem = aceSystemName.ToType();
   bool aceTypes;
   if (aceSystem) aceTypes = true;
   PrintFormat("[ESR TEST ACE] mode=%1 aceCaptivesAddon=%2 aceTypes=%3 calls=%4", ESR_AceCaptives.Mode(), aceAddon, aceTypes, ESR_AceCaptives.CallCount());
   if (!aceTypes) Check(!ESR_AceCaptives.Available() && ESR_AceCaptives.Mode() == ESR_AceCaptives.MODE_VANILLA && ESR_AceCaptives.CallCount() == 0, "without ACE Captives the adapter reports vanilla and makes no ACE call");
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
   // Weapon drops and the sit-down are scheduled; give them time, then let one server
   // upkeep move the points after the faces settled (FacesSettled), at most until 30 s.
   if (Now() - PhaseAt < 8) return;
   array<ESR_Prisoner> seated = {};
   for (int s = 0; s < ESR_SurrenderManager.PrisonerCount(); s++) seated.Insert(ESR_SurrenderManager.GetPrisonerAt(s));
   if (!FacesSettled(seated) && Now() - PhaseAt < 30) return;
   Check(ESR_SurrenderManager.PrisonerCount() == SIZE - 2, "exactly the four able survivors surrendered");
   int outsideGroup, passive, unarmed, civilian, points, sitting, aceHeld;
   bool ace = ESR_AceCaptives.Available();
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
    if (FaceReady(prisoner, string.Format("prisoner %1", i))) points++;
    SCR_CharacterControllerComponent controller = SCR_CharacterControllerComponent.Cast(character.GetCharacterController());
    if (controller && controller.IsLoitering()) sitting++;
    bool aceSurrendered, aceCaptive, aceCarried;
    if (ace && prisoner.AceMode && ESR_AceCaptives.ReadState(character, aceSurrendered, aceCaptive, aceCarried) && aceSurrendered && ESR_AceCaptives.InHelper(character)) aceHeld++;
    if (i == 0) Revealer = prisoner;
    else if (i == 1) Talker = prisoner;
    else if (i == 2) Refuser = prisoner;
    else if (i == 3) Doomed = prisoner;
   }
   // An emptied squad may already be deleted by the base game.
   int squadLeft = -1;
   if (Broken) squadLeft = Broken.GetAgentsCount();
   PrintFormat("[ESR TEST PRISONERS] count=%1 outsideGroup=%2 aiOff=%3 unarmed=%4 civilian=%5 points=%6 sitting=%7 squadLeft=%8 mode=%9", ESR_SurrenderManager.PrisonerCount(), outsideGroup, passive, unarmed, civilian, points, sitting, squadLeft, ESR_AceCaptives.Mode());
   Check(outsideGroup == SIZE - 2, "prisoners left their squad");
   Check(passive == SIZE - 2, "prisoner AI deactivated");
   Check(unarmed == SIZE - 2, "prisoners dropped every weapon");
   Check(civilian == SIZE - 2, "prisoners joined the civilian faction");
   Check(points == SIZE - 2, "Interrogate context on each prisoner's face: within 0.25 m of his head bone, ahead of it, above his chest, traced from ahead");
   if (ace) Check(aceHeld == SIZE - 2 && sitting == 0, "prisoners hold ACE's surrender state, never the vanilla sit");
   else
   {
    Check(sitting == SIZE - 2, "prisoners hold the vanilla sit-on-ground loiter");
    Check(ESR_AceCaptives.CallCount() == 0, "no ACE call during surrender without ACE");
   }
   // A Game Master move (native teleport; 0.8 m stays below the upkeep's 1 m respawn
   // distance): the same point must follow his face by itself.
   if (Talker && Talker.Character && Talker.Point)
   {
    vector moved[4];
    Talker.Character.GetWorldTransform(moved);
    MovedFrom = moved[3];
    moved[3] = Ground(MovedFrom + Vector(0.8, 0, 0), 0);
    MovedPoint = Talker.Point;
    Talker.Character.Teleport(moved);
   }
   SettleMovedAt = -1;
   Phase = 3; PhaseAt = Now(); return;
  }
  if (Phase == 3)
  {
   // The move may end his sit and the 5 s upkeep sit him down again; a face still in
   // that animation outruns any follow, and a dedicated server follows in that upkeep
   // only. Measured once his face has held still for one upkeep interval (FacesSettled),
   // 8 to 26 s after the move.
   if (Now() - PhaseAt < 8) return;
   if (Talker && Talker.Character)
   {
    array<ESR_Prisoner> movedOne = {Talker};
    bool still = FacesSettled(movedOne);
    if (!still && Now() - PhaseAt < 26) return;
    float movedBy = vector.Distance(Talker.Character.GetOrigin(), MovedFrom);
    PrintFormat("[ESR TEST FACE] teleport movedBy=%1 samePoint=%2 still=%3 after=%4", movedBy, Talker.Point == MovedPoint, still, Now() - PhaseAt);
    Check(movedBy > 0.5 && Talker.Point == MovedPoint && FaceReady(Talker, "after a 0.8 m teleport"), "the same interrogation point follows a moved prisoner's face");
   }
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
   if (!ESR_AceCaptives.Available()) Check(ESR_AceCaptives.CallCount() == 0, "no ACE call during upkeep, interrogation and release without ACE");
   Phase = 9; PhaseAt = Now(); return;
  }
  if (Phase == 9)
  {
   // Unit Caching interplay: a squad enrolled in a cache zone loses one prisoner.
   CacheSquad = SpawnSquad(Origin + CacheOffset);
   if (!Check(CacheSquad != null, "cache case: native squad spawned")) { Finish("cache-setup"); return; }
   Phase = 10; PhaseAt = Now(); return;
  }
  if (Phase == 10)
  {
   if (!CacheSquad || CacheSquad.GetAgentsCount() != SIZE || !CacheSquad.EBG_HasCompletedInitialSpawn())
   {
    if (Now() - PhaseAt > 30) { Check(false, "cache case: squad completed its initial spawn"); Finish("cache-setup"); }
    return;
   }
   Resource zonePrefab = Resource.Load(CACHE_ZONE);
   IEntity zoneEntity = GetGame().SpawnEntityPrefab(zonePrefab, GetGame().GetWorld(), Params(Origin + CacheOffset));
   CacheZone = EBG_CacheZone.Cast(zoneEntity);
   if (!Check(CacheZone != null, "cache case: cache zone prefab spawned")) { Finish("cache-setup"); return; }
   // Simulation mode, whole zone, 40 m enrollment, cleanup on. A 600 s clear delay keeps
   // the squad awake through the surrender and the regroup; phase 12 lowers it to 5 s.
   CacheZone.SetValue(1, 0); CacheZone.SetValue(2, 0);
   CacheZone.SetValue(3, 40); CacheZone.SetValue(4, 60); CacheZone.SetValue(5, 200);
   CacheZone.SetValue(12, 600); CacheZone.SetValue(15, 1); CacheZone.SetValue(21, 1);
   CacheZone.SetValue(0, 1);
   Check(CacheZone.Enabled == 1 && CacheZone.Mode == 0 && CacheZone.Affected == 40 && CacheZone.SleepDelay == 600 && CacheZone.Cleanup == 1, "cache case: public zone settings applied");
   Phase = 11; PhaseAt = Now(); return;
  }
  if (Phase == 11)
  {
   CacheRecord = EBG_CacheManager.Get().FindGroup(CacheSquad);
   if (!CacheRecord || CacheRecord.Members.Count() != SIZE || !CacheRecord.CleanupRegistered || CacheZone.Editing)
   {
    if (Now() - PhaseAt > 40)
    {
     PrintFormat("[ESR TEST CACHE ZONE] status='%1' editing=%2", CacheZone.Status, CacheZone.Editing);
     Check(false, "cache case: zone enrolled the squad with a cleanup ledger");
     Finish("cache-enroll");
    }
    return;
   }
   Check(!EBG_CacheManager.IsCacheHeld(CacheSquad) && CacheRecord.RegroupReason == "", "cache case: enrolled squad awake with no cache state held");
   IEntity cacheLeader = CacheSquad.GetLeaderEntity();
   foreach (EBG_CacheMember cacheEnrolled : CacheRecord.Members)
   {
    if (!CacheCaptive && cacheEnrolled.Entity && cacheEnrolled.Entity != cacheLeader) CacheCaptive = cacheEnrolled.Entity;
   }
   if (!Check(CacheCaptive != null, "cache case: a non-leader member chosen")) { Finish("cache-enroll"); return; }
   CacheCaptiveId = CacheCaptive.GetID();
   bool heldBefore = EBG_CacheCleanup.Get().IsHeld(CacheCaptive);
   int prisonersBefore = ESR_SurrenderManager.PrisonerCount();
   // Production surrender of exactly one member (the module stays disabled: no roll).
   bool surrendered = ESR_SurrenderManager.Surrender(CacheCaptive, CacheSquad);
   PrintFormat("[ESR TEST CACHE SURRENDER] record=%1 captive=%2 surrendered=%3 heldBefore=%4 leftSquad=%5 squadLeft=%6 members=%7", CacheRecord.Id, CacheCaptive, surrendered, heldBefore, CacheCaptive.EBG_HasLeftSquad(), CacheSquad.GetAgentsCount(), CacheRecord.Members.Count());
   Check(surrendered && ESR_SurrenderManager.PrisonerCount() == prisonersBefore + 1, "cache case: one member of the enrolled squad surrendered");
   Check(CacheCaptive.EBG_HasLeftSquad() && ESR_SurrenderManager.GroupOf(CacheCaptive) == null && CacheSquad.GetAgentsCount() == SIZE - 1, "cache case: prisoner marked as having left his squad");
   EBG_CacheManager surrenderManager = EBG_CacheManager.Get();
   Check(surrenderManager.FindMember(CacheCaptive) == null && CacheRecord.Members.Count() == SIZE - 1 && CacheRecord.RegroupReason == "", "cache case: the awake squad record forgot the prisoner at once");
   // Fallback rule, for a module that marks a soldier before he leaves the native group:
   // the mark alone retires nobody; once he is out, the regroup scan retires him.
   foreach (EBG_CacheMember cacheOther : CacheRecord.Members)
   {
    if (!CacheLeaver && cacheOther.Entity && cacheOther.Entity != cacheLeader && cacheOther.Entity != CacheCaptive) CacheLeaver = cacheOther.Entity;
   }
   if (!Check(CacheLeaver != null, "cache case: a second non-leader member chosen")) { Finish("cache-enroll"); return; }
   CacheLeaver.EBG_MarkLeftSquad();
   Check(surrenderManager.FindMember(CacheLeaver) != null, "cache case: a marked soldier still in his native group stays a member");
   AIControlComponent leaverControl = CacheLeaver.GetAIControlComponent();
   AIAgent leaverAgent;
   if (leaverControl) leaverAgent = leaverControl.GetControlAIAgent();
   if (leaverAgent && leaverAgent.GetParentGroup() == CacheSquad) CacheSquad.RemoveAgent(leaverAgent);
   if (leaverControl) leaverControl.DeactivateAI();
   Check(CacheLeaver.GetCharacterGroup() == null && CacheSquad.GetAgentsCount() == SIZE - 2, "cache case: the marked soldier left the native group");
   Phase = 12; PhaseAt = Now(); return;
  }
  if (Phase == 12)
  {
   // The regroup scan runs every 5 s and commits after two stable scans.
   EBG_CacheManager cacheManager = EBG_CacheManager.Get();
   if (CacheRecord.RegroupReason.StartsWith("Regroup held"))
   {
    PrintFormat("[ESR TEST CACHE HELD] reason='%1'", CacheRecord.RegroupReason);
    Check(false, "cache case: a soldier who left never holds the squad record");
    Finish("cache-regroup");
    return;
   }
   bool forgotten = cacheManager.FindMember(CacheLeaver) == null && !cacheManager.Regroup.ReservesMember(CacheLeaver) && cacheManager.FindMember(CacheCaptive) == null;
   if (!forgotten || CacheRecord.Members.Count() != SIZE - 2 || CacheRecord.RegroupReason != "")
   {
    if (Now() - PhaseAt > 40)
    {
     PrintFormat("[ESR TEST CACHE PENDING] members=%1 regroup='%2' reason='%3'", CacheRecord.Members.Count(), CacheRecord.RegroupReason, CacheRecord.Reason);
     Check(false, "cache case: the regroup scan retired the soldier who left");
     Finish("cache-regroup");
    }
    return;
   }
   PrintFormat("[ESR TEST CACHE REGROUP] record=%1 members=%2 squad=%3 seconds=%4", CacheRecord.Id, CacheRecord.Members.Count(), CacheSquad.GetAgentsCount(), Now() - PhaseAt);
   Check(true, "cache case: the regroup scan retired the soldier who left without a hold");
   Check(cacheManager.FindGroup(CacheSquad) == CacheRecord && CacheRecord.Group == CacheSquad && CacheSquad.GetAgentsCount() == SIZE - 2, "cache case: the remaining squad keeps its record and native group");
   Check(!EBG_CacheCleanup.Get().IsHeld(CacheLeaver), "cache case: cleanup released the soldier who left");
   CaptiveUntouched("after the regroup");
   // Let the remaining squad sleep: 5 s clear delay.
   CacheZone.SetValue(12, 5);
   Phase = 13; PhaseAt = Now(); return;
  }
  if (Phase == 13)
  {
   bool asleep = CacheRecord.Simulation != null && CacheRecord.Simulation.Suspended;
   if (!asleep)
   {
    if (Now() - PhaseAt > 120)
    {
     PrintFormat("[ESR TEST CACHE AWAKE] reason='%1' zone='%2'", CacheRecord.Reason, CacheZone.Status);
     Check(false, "cache case: the remaining squad Simulation cached");
     Finish("cache-sleep");
    }
    return;
   }
   PrintFormat("[ESR TEST CACHE SLEPT] record=%1 cachedMembers=%2 seconds=%3", CacheRecord.Id, CacheRecord.Simulation.Members.Count(), Now() - PhaseAt);
   Check(CacheRecord.Simulation.Members.Count() == SIZE - 2, "cache case: the remaining squad Simulation cached without the soldiers who left");
   Check(EBG_CacheManager.IsCacheHeld(CacheSquad), "cache case: the cached squad reports its cache state held");
   Check(!CacheLeaver.EBG_IsSimulationCached() && (CacheLeaver.GetFlags() & EntityFlags.VISIBLE) != 0, "cache case: the soldier who left was not cached with his squad");
   // A cached soldier is asleep (permanent LOD pin): AI Surrender never takes him.
   SCR_ChimeraCharacter cacheSleeper;
   foreach (EBG_CacheMember cacheMember : CacheRecord.Members)
   {
    if (!cacheSleeper && cacheMember.Entity && !cacheMember.Dead) cacheSleeper = cacheMember.Entity;
   }
   int prisonersAsleep = ESR_SurrenderManager.PrisonerCount();
   bool sleeperTaken;
   if (cacheSleeper) sleeperTaken = ESR_SurrenderManager.Surrender(cacheSleeper, CacheSquad);
   Check(cacheSleeper != null && !sleeperTaken && ESR_SurrenderManager.PrisonerCount() == prisonersAsleep && !cacheSleeper.EBG_HasLeftSquad(), "cache case: a Simulation cached soldier never surrenders");
   CaptiveUntouched("while the squad is cached");
   // Wake: hold the zone active for editing; the scheduler restores the snapshot.
   CacheZone.SetValue(101, 1);
   Phase = 14; PhaseAt = Now(); return;
  }
  if (Phase == 14)
  {
   if (CacheRecord.Simulation)
   {
    if (Now() - PhaseAt > 30) { Check(false, "cache case: the remaining squad woke"); Finish("cache-wake"); }
    return;
   }
   int cacheAwake;
   foreach (EBG_CacheMember cacheWoken : CacheRecord.Members)
   {
    SCR_ChimeraCharacter wokenEntity = cacheWoken.Entity;
    if (wokenEntity && wokenEntity.GetCharacterController() && !wokenEntity.GetCharacterController().IsDead() && !wokenEntity.EBG_IsSimulationCached() && wokenEntity.GetCharacterGroup() == CacheSquad) cacheAwake++;
   }
   PrintFormat("[ESR TEST CACHE WOKE] record=%1 awake=%2 squad=%3 members=%4", CacheRecord.Id, cacheAwake, CacheSquad.GetAgentsCount(), CacheRecord.Members.Count());
   Check(cacheAwake == SIZE - 2 && CacheSquad.GetAgentsCount() == SIZE - 2 && CacheRecord.Members.Count() == SIZE - 2, "cache case: the same four soldiers woke; nobody who left was restored into the squad");
   Check(CacheLeaver.GetCharacterGroup() == null && EBG_CacheManager.Get().FindMember(CacheLeaver) == null, "cache case: the soldier who left stays out of the squad and its record");
   CaptiveUntouched("after the squad woke");
   if (!ESR_AceCaptives.Available()) Check(ESR_AceCaptives.CallCount() == 0, "no ACE call in the cache case without ACE");
   Finish("complete");
  }
 }
}
