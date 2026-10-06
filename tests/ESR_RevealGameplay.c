// TEST ONLY. EXPBG AI Surrender: the prisoner's reveal in a crowded scene (dedicated
// server, no players, no GM UI).
// pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -FixturePath tests/ESR_RevealGameplay.c -TimeoutSeconds 420 -OrchestratorSlotGranted -ExpectResult '\[ESR REVEAL RESULT\] checks=[1-9]\d* failures=0 props=2704 reason=complete'
// The runner copies this file to EXPG_GarrisonGameplay.c; the class names are fixed. Judge
// the run by its one [ESR REVEAL RESULT] line (-ExpectResult also rejects script errors).
// Live bug (0.1.8, Everon/Morton, about 119 mods): a USSR prisoner answered "He insists
// there is nobody else out here." while a USSR fire team stood 182 m away. The old reveal
// walked a sphere query over every dynamic entity in the radius (attached gear included,
// in no particular order) and stopped after 2048 of them, before it reached the squad.
// Scene: the real module prefab, two native USSR rifle squads, and 2704 loose magazines
// (dynamic entities with physics) 45-155 m south-west of the near squad. One near soldier
// is taken through the production Surrender call; his five squad mates stay beside him.
// The far squad stands 283 m north-east. Reveal 100%, radius 1000 m.
//  1. The scene holds more dynamic entities in the radius than the old 2048 cap (counted
//     with the old query flags). Printed only: whether the old capped walk would have
//     reached the far squad (engine visit order, not distance, decides that).
//  2. The first prisoner reveals the far squad, not his own remnants: OUTCOME_REVEAL with
//     its living count, its distance in 25 m steps and its compass sector, all computed
//     here from the soldiers' positions.
//  3. With the far squad dead, a second prisoner gives away his own remnants (nothing else
//     is near).
class EXPG_GarrisonGameplayClass : GenericEntityClass {}
class EXPG_GarrisonGameplay : GenericEntity
{
 static const float FIXTURE_SECONDS = 240;
 static const ResourceName MODULE = "{7F2E668385984EA1}PrefabsEditable/EXPSR/ESR_SurrenderModule.et";
 static const ResourceName SQUAD = "{E552DABF3636C2AD}Prefabs/Groups/OPFOR/Group_USSR_RifleSquad.et";
 static const ResourceName PROP = "{0A84AA5A3884176F}Prefabs/Weapons/Magazines/Magazine_545x39_AK_30rnd_Last_5Tracer.et";
 static const int SIZE = 6;
 static const int RADIUS = 1000;
 // The old query's visit cap (ESR_SquadQuery.MAX_VISITED in 0.1.8).
 static const int LEGACY_CAP = 2048;
 static const int PROP_SIDE = 52;
 static const float PROP_SPACING = 1.5;
 static const int PROPS_PER_STEP = 400;
 vector Origin = "4773.46 0 7094.57";
 vector FarOffset = "200 0 200";
 vector PropOffset = "-70 0 -70";
 ESR_SurrenderModule Surrender;
 SCR_AIGroup NearSquad;
 SCR_AIGroup FarSquad;
 SCR_ChimeraCharacter FirstCaptive;
 SCR_ChimeraCharacter SecondCaptive;
 // Strong references: the records stay readable whatever the manager does with them.
 ref ESR_Prisoner FirstPrisoner;
 ref ESR_Prisoner SecondPrisoner;
 int Phase;
 int Checks;
 int Failures;
 int PropsTried;
 int PropsSpawned;
 int LegacyVisited;
 int LegacyFarVisit = -1;
 float Started;
 float PhaseAt;
 float Next;
 bool Finished;

 void EXPG_GarrisonGameplay(IEntitySource src, IEntity parent) { SetEventMask(EntityEvent.INIT | EntityEvent.FRAME); }

 float Now()
 {
  return GetGame().GetWorld().GetWorldTime() * 0.001;
 }

 override void EOnInit(IEntity owner)
 {
  if (!Replication.IsServer()) { ClearEventMask(EntityEvent.FRAME); return; }
  Started = Now(); Next = Started + 10; PhaseAt = Started;
  PrintFormat("[ESR REVEAL BEGIN] origin=%1 farOffset=%2 props=%3 radius=%4 deadline=%5", Origin, FarOffset, PROP_SIDE * PROP_SIDE, RADIUS, FIXTURE_SECONDS);
 }

 bool Check(bool ok, string label)
 {
  Checks++;
  if (!ok) Failures++;
  PrintFormat("[ESR REVEAL CHECK] pass=%1 %2", ok, label);
  return ok;
 }

 void Finish(string reason)
 {
  if (Finished) return;
  Finished = true;
  ClearEventMask(EntityEvent.FRAME);
  PrintFormat("[ESR REVEAL RESULT] checks=%1 failures=%2 props=%3 reason=%4", Checks, Failures, PropsSpawned, reason);
  GetGame().RequestClose();
 }

 vector Ground(vector p, float lift)
 {
  p[1] = GetGame().GetWorld().GetSurfaceY(p[0], p[2]) + lift;
  return p;
 }

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

 void Configure()
 {
  Surrender.ApplySetting(ESR_Settings.ENABLED, ESR_Settings.FromBool(true));
  // No casualty-driven surrender here: prisoners come only from the Surrender calls.
  Surrender.ApplySetting(ESR_Settings.CHANCE, 0);
  Surrender.ApplySetting(ESR_Settings.THRESHOLD, 100);
  Surrender.ApplySetting(ESR_Settings.RANDOM, 0);
  Surrender.ApplySetting(ESR_Settings.REVEAL, 100);
  Surrender.ApplySetting(ESR_Settings.IDENTITY, 0);
  Surrender.ApplySetting(ESR_Settings.RADIUS, RADIUS);
  Surrender.ApplySetting(ESR_Settings.ATTEMPTS, 3);
  Surrender.ApplySetting(ESR_Settings.LIFETIME, 0);
  Surrender.ApplySetting(ESR_Settings.DIAGNOSTICS, 1);
 }

 // The old reveal's walk, uncapped: every dynamic entity the sphere touches, and the
 // visit at which the first living far soldier came up.
 bool LegacyVisit(IEntity entity)
 {
  LegacyVisited++;
  if (LegacyFarVisit >= 0) return true;
  SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(entity);
  if (!character || !character.GetCharacterController() || character.GetCharacterController().GetLifeState() != ECharacterLifeState.ALIVE) return true;
  if (ESR_SurrenderManager.GroupOf(character) == FarSquad) LegacyFarVisit = LegacyVisited;
  return true;
 }

 // Living soldiers of a squad and their mean position, read independently of production.
 int Living(SCR_AIGroup group, out vector center)
 {
  center = vector.Zero;
  if (!group) return 0;
  array<AIAgent> agents = {};
  group.GetAgents(agents);
  int living = 0;
  vector sum = vector.Zero;
  foreach (AIAgent agent : agents)
  {
   if (!agent) continue;
   SCR_ChimeraCharacter member = SCR_ChimeraCharacter.Cast(agent.GetControlledEntity());
   if (!member || !member.GetCharacterController() || member.GetCharacterController().IsDead()) continue;
   living++;
   sum = sum + member.GetOrigin();
  }
  if (living > 0) center = sum * (1.0 / living);
  return living;
 }

 // The reported distance (25 m steps) and compass sector (0 = north, clockwise) of a
 // point seen from the prisoner.
 int ExpectedDistance(vector from, vector to)
 {
  int steps = Math.Round(vector.DistanceXZ(from, to) / 25);
  return steps * 25;
 }

 int ExpectedSector(vector from, vector to)
 {
  vector offset = to - from;
  float angle = Math.Atan2(offset[0], offset[2]) * Math.RAD2DEG;
  if (angle < 0) angle += 360;
  int sector = Math.Round(angle / 45);
  return sector % 8;
 }

 // A living soldier of the squad other than its leader and the excluded one.
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
   Configure();
   Check(ESR_Settings.Get(ESR_Settings.REVEAL) == 100 && ESR_Settings.Get(ESR_Settings.RADIUS) == RADIUS && ESR_Settings.Get(ESR_Settings.CHANCE) == 0, "reveal 100%, radius 1000 m, no casualty surrender");
   NearSquad = SpawnSquad(Origin);
   FarSquad = SpawnSquad(Origin + FarOffset);
   if (!Check(NearSquad != null && FarSquad != null, "two native USSR squads spawned")) { Finish("setup"); return; }
   Phase = 1; PhaseAt = Now(); return;
  }
  if (Phase == 1)
  {
   if (!NearSquad || !FarSquad || NearSquad.GetAgentsCount() != SIZE || FarSquad.GetAgentsCount() != SIZE)
   {
    if (Now() - PhaseAt > 30) { Check(false, "both squads completed their initial spawn"); Finish("setup"); }
    return;
   }
   Phase = 2; PhaseAt = Now(); return;
  }
  if (Phase == 2)
  {
   // A grid of loose magazines, a few hundred per step.
   int total = PROP_SIDE * PROP_SIDE;
   Resource prop = Resource.Load(PROP);
   int stepEnd = PropsTried + PROPS_PER_STEP;
   if (stepEnd > total) stepEnd = total;
   while (PropsTried < stepEnd)
   {
    int row = PropsTried / PROP_SIDE;
    int column = PropsTried % PROP_SIDE;
    PropsTried++;
    float half = (PROP_SIDE - 1) * 0.5;
    vector spot = Origin + PropOffset + Vector((column - half) * PROP_SPACING, 0, (row - half) * PROP_SPACING);
    IEntity spawnedProp = GetGame().SpawnEntityPrefab(prop, GetGame().GetWorld(), Params(spot));
    if (spawnedProp) PropsSpawned++;
   }
   if (PropsTried < total) return;
   PrintFormat("[ESR REVEAL PROPS] tried=%1 spawned=%2", PropsTried, PropsSpawned);
   if (!Check(PropsSpawned == total, "every loose magazine spawned")) { Finish("props"); return; }
   Phase = 3; PhaseAt = Now(); return;
  }
  if (Phase == 3)
  {
   // Let the magazines settle.
   if (Now() - PhaseAt < 3) return;
   FirstCaptive = Member(NearSquad, null);
   bool taken = FirstCaptive != null && ESR_SurrenderManager.Surrender(FirstCaptive, NearSquad);
   FirstPrisoner = ESR_SurrenderManager.FindPrisoner(FirstCaptive);
   if (!Check(taken && FirstPrisoner != null && FirstPrisoner.Point != null, "first near soldier surrendered through production with an interrogation point")) { Finish("surrender"); return; }
   Phase = 4; PhaseAt = Now(); return;
  }
  if (Phase == 4)
  {
   // Weapon drop and stand-up are scheduled; give them time.
   if (Now() - PhaseAt < 3) return;
   if (!Check(FirstPrisoner && FirstPrisoner.Character && FirstPrisoner.Point, "first prisoner record still holds him and his point")) { Finish("records"); return; }
   vector origin = FirstPrisoner.Character.GetOrigin();
   GetGame().GetWorld().QueryEntitiesBySphere(origin, RADIUS, LegacyVisit, null, EQueryEntitiesFlags.DYNAMIC);
   bool legacyReached = LegacyFarVisit > 0 && LegacyFarVisit <= LEGACY_CAP;
   PrintFormat("[ESR REVEAL LEGACY] dynamicInRadius=%1 cap=%2 farFirstVisit=%3 oldQueryReachedFar=%4", LegacyVisited, LEGACY_CAP, LegacyFarVisit, legacyReached);
   Check(LegacyVisited > LEGACY_CAP, "the radius holds more dynamic entities than the old 2048 visit cap");
   vector farCenter;
   int farLiving = Living(FarSquad, farCenter);
   vector ownCenter;
   int ownLiving = Living(NearSquad, ownCenter);
   int farDistance = ExpectedDistance(origin, farCenter);
   int farSector = ExpectedSector(origin, farCenter);
   int outcome = ESR_SurrenderManager.Interrogate(FirstPrisoner.Point, FirstPrisoner.Point, 0);
   PrintFormat("[ESR REVEAL FIRST] outcome=%1 count=%2 distance=%3 bearing=%4 expectedCount=%5 expectedDistance=%6 expectedBearing=%7 ownRemnants=%8", outcome, FirstPrisoner.RevealCount, FirstPrisoner.RevealDistance, FirstPrisoner.RevealBearing, farLiving, farDistance, farSector, ownLiving);
   Check(farLiving == SIZE && ownLiving == SIZE - 1, "far squad intact, five near squad mates beside the prisoner");
   Check(outcome == ESR_SurrenderManager.OUTCOME_REVEAL, "first interrogation reveals a squad in the crowded scene");
   Check(FirstPrisoner.RevealCount == farLiving, "revealed count is the far squad's living strength");
   Check(FirstPrisoner.RevealDistance == farDistance && farDistance >= 250 && farDistance <= 325, "revealed distance is the far squad's (283 m layout), not his remnants'");
   Check(FirstPrisoner.RevealBearing == farSector && farSector == 1, "revealed bearing is north-east");
   // Far squad dead: only his own remnants are left near the next prisoner.
   array<AIAgent> agents = {};
   FarSquad.GetAgents(agents);
   foreach (AIAgent agent : agents)
   {
    SCR_ChimeraCharacter member = SCR_ChimeraCharacter.Cast(agent.GetControlledEntity());
    if (!member) continue;
    SCR_CharacterDamageManagerComponent damage = SCR_CharacterDamageManagerComponent.Cast(member.GetDamageManager());
    if (damage) damage.Kill(Instigator.CreateInstigator(null));
   }
   Phase = 5; PhaseAt = Now(); return;
  }
  if (Phase == 5)
  {
   vector deadCenter;
   if (Living(FarSquad, deadCenter) > 0)
   {
    if (Now() - PhaseAt > 10) { Check(false, "far squad killed"); Finish("kill"); }
    return;
   }
   if (Now() - PhaseAt < 2) return;
   SecondCaptive = Member(NearSquad, FirstCaptive);
   bool secondTaken = SecondCaptive != null && ESR_SurrenderManager.Surrender(SecondCaptive, NearSquad);
   SecondPrisoner = ESR_SurrenderManager.FindPrisoner(SecondCaptive);
   if (!Check(secondTaken && SecondPrisoner != null && SecondPrisoner.Point != null, "second near soldier surrendered through production")) { Finish("surrender"); return; }
   Phase = 6; PhaseAt = Now(); return;
  }
  if (Phase == 6)
  {
   if (Now() - PhaseAt < 3) return;
   if (!Check(SecondPrisoner && SecondPrisoner.Character && SecondPrisoner.Point, "second prisoner record still holds him and his point")) { Finish("records"); return; }
   vector secondOrigin = SecondPrisoner.Character.GetOrigin();
   vector remnantCenter;
   int remnants = Living(NearSquad, remnantCenter);
   int remnantDistance = ExpectedDistance(secondOrigin, remnantCenter);
   int secondOutcome = ESR_SurrenderManager.Interrogate(SecondPrisoner.Point, SecondPrisoner.Point, 0);
   PrintFormat("[ESR REVEAL OWN] outcome=%1 count=%2 distance=%3 bearing=%4 expectedCount=%5 expectedDistance=%6", secondOutcome, SecondPrisoner.RevealCount, SecondPrisoner.RevealDistance, SecondPrisoner.RevealBearing, remnants, remnantDistance);
   Check(remnants == SIZE - 2, "four near squad mates left");
   Check(secondOutcome == ESR_SurrenderManager.OUTCOME_REVEAL && SecondPrisoner.RevealCount == remnants && SecondPrisoner.RevealDistance == remnantDistance && remnantDistance <= 25, "with nothing else near, he gives away his own remnants");
   Finish("complete");
   return;
  }
 }
}
