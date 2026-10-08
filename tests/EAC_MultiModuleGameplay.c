// TEST ONLY. EXPBG Ambient Civilians (addon/ambient-civilians) native server fixture: TWO
// Ambient Civilians modules in two GM_Eden towns must BOTH populate (0.1.18; production
// report 2026-10-08 on 0.1.17: "I placed a second module and it's not spawning any civs",
// server log "[EAC] a second Ambient Civilians module is placed; only the first placed
// module admits residents").
// pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -FixturePath tests/EAC_MultiModuleGameplay.c -TimeoutSeconds 420 -OrchestratorSlotGranted -ExpectResult '\[EAC MULTI RESULT\] checks=[1-9]\d* failures=0 areas=2 areaA=[1-9]\d* areaB=[1-9]\d* independent=1 split=1 reason=complete'
// The runner copies this file to EXPG_GarrisonGameplay.c, so the driver class name is fixed.
// The runner's -ExpectResult verdict accepts exactly one "[EAC MULTI RESULT] ... failures=0
// ... reason=complete" line, no script errors and "Game destroyed".
// Real production prefabs and code paths: two EAC_AmbientModule entities (A in Morton, B at
// the GM_Eden town centre 4773 169 7094, about 3.1 km apart), the real coordinator tick
// (themes for both areas, automatic exclusions, area refresh), the real EAC_HomeIndex with
// one scan cursor per area, and the real spawner admission ladder (SelectNext, Begin, the
// pending activation, Commit). A dedicated server has no players, so one stand-in civilian
// character per town is passed as the observer list, exactly where the scheduler passes the
// players' controlled characters; spawns are indoor-only so walls hide them from both.
// Checks: the second module is an area of its own (not refused as a duplicate); the index
// covers both discs; each town gets at least one committed resident from its own module; a
// module at its own limit leaves the other admitting; the 200 ceiling is split
// proportionally and reported as capped.
// Not covered: real players, GM UI, multiplayer/JIP, save/load.
class EXPG_GarrisonGameplayClass : GenericEntityClass {}
class EXPG_GarrisonGameplay : GenericEntity
{
 static const float FIXTURE_SECONDS = 330;
 static const float PREPARE_DEADLINE = 90;
 static const float INDEX_DEADLINE = 60;
 static const float ADMIT_DEADLINE = 150;
 static const int AREA_RADIUS = 200;
 static const int AREA_LIMIT = 4;
 static const ResourceName MODULE_PREFAB = "{CA1A000000000010}PrefabsEditable/EXPAC/EAC_AmbientModule.et";
 static const ResourceName STAND_IN_PREFAB = "{E024A74F8A4BC644}Prefabs/Characters/Factions/CIV/Businessman/Character_CIV_Businessman_1.et";
 // Morton, Everon (tests/EAC_AmbientGameplay.c) and the driver's own town centre.
 static const vector TOWN_A = "5080 0 4000";
 int m_Phase;
 int m_Checks;
 int m_Failures;
 int m_HomesA;
 int m_HomesB;
 int m_CommittedA;
 int m_CommittedB;
 bool m_Independent;
 bool m_Split;
 bool m_Finished;
 float m_Started;
 float m_PhaseStarted;
 float m_NextTick;
 float m_NextProgress;
 vector m_TownB;
 EAC_AmbientModule m_ModuleA;
 EAC_AmbientModule m_ModuleB;
 IEntity m_WatcherA;
 IEntity m_WatcherB;
 ref array<IEntity> m_Observers = {};
 ref array<vector> m_Centres = {};
 ref array<int> m_Radii = {};

 void EXPG_GarrisonGameplay(IEntitySource src, IEntity parent) { SetEventMask(EntityEvent.INIT | EntityEvent.FRAME); }

 float Now()
 {
  return GetGame().GetWorld().GetWorldTime() * 0.001;
 }

 override void EOnInit(IEntity owner)
 {
  if (!Replication.IsServer())
  {
   ClearEventMask(EntityEvent.FRAME);
   return;
  }
  m_Started = Now();
  m_PhaseStarted = m_Started;
  m_NextTick = m_Started + 3;
  m_TownB = GetOrigin();
  PrintFormat("[EAC MULTI BEGIN] deadline=%1 townA=%2 townB=%3", FIXTURE_SECONDS, TOWN_A, m_TownB);
 }

 bool Check(bool ok, string label)
 {
  m_Checks++;
  if (!ok) m_Failures++;
  PrintFormat("[EAC MULTI CHECK] pass=%1 %2", ok, label);
  return ok;
 }

 void Finish(string reason)
 {
  if (m_Finished)
   return;
  m_Finished = true;
  ClearEventMask(EntityEvent.FRAME);
  int independent = 0;
  if (m_Independent) independent = 1;
  int split = 0;
  if (m_Split) split = 1;
  PrintFormat("[EAC MULTI RESULT] checks=%1 failures=%2 areas=2 areaA=%3 areaB=%4 independent=%5 split=%6 reason=%7", m_Checks, m_Failures, m_CommittedA, m_CommittedB, independent, split, reason);
  GetGame().RequestClose();
 }

 void Advance(int phase)
 {
  m_Phase = phase;
  m_PhaseStarted = Now();
 }

 vector Ground(vector position)
 {
  vector grounded = position;
  grounded[1] = GetGame().GetWorld().GetSurfaceY(grounded[0], grounded[2]) + 0.2;
  return grounded;
 }

 IEntity SpawnAt(ResourceName prefab, vector position)
 {
  Resource resource = Resource.Load(prefab);
  if (!resource || !resource.IsValid())
   return null;
  EntitySpawnParams params = new EntitySpawnParams();
  params.TransformMode = ETransformMode.WORLD;
  Math3D.MatrixIdentity4(params.Transform);
  params.Transform[3] = Ground(position);
  IEntity spawned = GetGame().SpawnEntityPrefab(resource, GetGame().GetWorld(), params);
  return spawned;
 }

 // Households the index holds per area, and committed residents with a live character.
 void CountPopulation()
 {
  m_HomesA = 0;
  m_HomesB = 0;
  int committedA = 0;
  int committedB = 0;
  EAC_HouseholdRegistry registry = m_ModuleA.GetHomeIndex().GetRegistry();
  for (int index = 0; index < registry.GetHomeCount(); index++)
  {
   EAC_HouseholdRecord home = registry.GetHome(index);
   if (!home)
    continue;
   EAC_AmbientModule holder = EAC_AmbientModule.FindPopulationArea(home.Position);
   if (holder == m_ModuleA) m_HomesA++;
   if (holder == m_ModuleB) m_HomesB++;
   foreach (EAC_ResidentRecord resident : home.Residents)
   {
    EAC_ResidentClaim claim = m_ModuleA.GetResidentActivation(home, resident);
    if (!claim || !claim.Committed || !claim.Character)
     continue;
    if (holder == m_ModuleA) committedA++;
    if (holder == m_ModuleB) committedB++;
   }
  }
  if (committedA > m_CommittedA) m_CommittedA = committedA;
  if (committedB > m_CommittedB) m_CommittedB = committedB;
 }

 override void EOnFrame(IEntity owner, float timeSlice)
 {
  if (m_Finished)
   return;
  float now = Now();
  if (now - m_Started > FIXTURE_SECONDS)
  {
   Check(false, string.Format("phase %1 finished before the fixture deadline", m_Phase));
   Finish("timeout");
   return;
  }
  if (now < m_NextTick)
   return;
  m_NextTick = now + 0.5;
  if (m_Phase == 0)
  {
   Check(!EAC_AmbientModule.GetActive(), "the world starts without an Ambient Civilians module");
   m_ModuleA = EAC_AmbientModule.Cast(SpawnAt(MODULE_PREFAB, TOWN_A));
   m_ModuleB = EAC_AmbientModule.Cast(SpawnAt(MODULE_PREFAB, m_TownB));
   if (!Check(m_ModuleA && m_ModuleB, "two production Ambient Civilians modules spawned"))
   {
    Finish("modules");
    return;
   }
   array<EAC_AmbientModule> placed = {};
   placed.Insert(m_ModuleA);
   placed.Insert(m_ModuleB);
   foreach (EAC_AmbientModule placedModule : placed)
   {
    placedModule.SetSetting(36, AREA_RADIUS);
    placedModule.SetSetting(0, AREA_LIMIT);
    // Indoor-only admission: walls hide a placement from both stand-in observers.
    placedModule.SetSetting(25, 100);
    placedModule.SetSetting(22, 100);
   }
   m_WatcherA = SpawnAt(STAND_IN_PREFAB, TOWN_A);
   m_WatcherB = SpawnAt(STAND_IN_PREFAB, m_TownB + "0 0 6");
   Check(ChimeraCharacter.Cast(m_WatcherA) && ChimeraCharacter.Cast(m_WatcherB), "one stand-in observer character per town");
   Advance(1);
   return;
  }
  if (m_Phase == 1)
  {
   Check(EAC_AmbientModule.GetActive() == m_ModuleA, "the first placed module coordinates the shared scheduler");
   Check(m_ModuleB.GetAreaId() > m_ModuleA.GetAreaId(), "the second module is registered as an area of its own");
   Check(EAC_AmbientModule.FindPopulationArea(TOWN_A) == m_ModuleA, "town A belongs to module A");
   Check(EAC_AmbientModule.FindPopulationArea(m_TownB) == m_ModuleB, "town B belongs to module B");
   vector between = (TOWN_A + m_TownB) * 0.5;
   Check(!EAC_AmbientModule.FindPopulationArea(between), "open country between the towns belongs to no module");
   Check(!EAC_AmbientModule.IsOutsidePopulationArea(m_TownB), "town B is inside the mission population area (0.1.17 refused it)");
   Check(EAC_AmbientModule.IsOutsidePopulationArea(between), "the population area is still bounded by the module discs");
   Check(m_ModuleA.ContainsPopulationPosition(m_TownB) && m_ModuleB.ContainsOwnArea(m_TownB) && !m_ModuleA.ContainsOwnArea(m_TownB), "the coordinator's area test is the union; each module's own disc is its own");
   Advance(2);
   return;
  }
  if (m_Phase == 2)
  {
   // The real coordinator tick prepares both themes, the automatic exclusions and the
   // area set; nothing else is driven by hand until all of it is ready.
   EAC_ThemeSelection themeA = m_ModuleA.GetThemeSelection();
   EAC_ThemeSelection themeB = m_ModuleB.GetThemeSelection();
   EAC_HomeIndex index = m_ModuleA.GetHomeIndex();
   bool themesReady = themeA && themeA.IsReady() && themeB && themeB.IsReady();
   bool ready = themesReady && EAC_AutoExclusions.IsReady() && index && index.GetPopulationAreaCount() == 2;
   if (!ready)
   {
    if (now - m_PhaseStarted < PREPARE_DEADLINE)
     return;
    Check(false, string.Format("coordinator prepared both themes, exclusions and two areas (themes=%1 exclusions=%2)", themesReady, EAC_AutoExclusions.IsReady()));
    Finish("prepare");
    return;
   }
   Check(true, "the coordinator tick prepared module B's own theme and indexed two areas");
   Check(m_ModuleA.GetWorldPopulationLimit() == AREA_LIMIT * 2 && m_ModuleB.GetAreaPopulationLimit() == AREA_LIMIT, string.Format("mission limit is the sum of the module limits (%1) and B keeps its own (%2)", m_ModuleA.GetWorldPopulationLimit(), m_ModuleB.GetAreaPopulationLimit()));
   // From here the fixture drives the scheduler's steps itself with the stand-in observers.
   m_ModuleA.ClearEventMask(EntityEvent.FRAME | EntityEvent.POSTFRAME);
   m_Centres.Insert(m_ModuleA.GetOrigin());
   m_Centres.Insert(m_ModuleB.GetOrigin());
   m_Radii.Insert(m_ModuleA.SettlementRadius);
   m_Radii.Insert(m_ModuleB.SettlementRadius);
   m_Observers.Insert(m_WatcherA);
   m_Observers.Insert(m_WatcherB);
   Advance(3);
   return;
  }
  m_ModuleA.RefreshPopulationAreas();
  EAC_HomeIndex homes = m_ModuleA.GetHomeIndex();
  homes.SetPopulationAreas(m_Centres, m_Radii);
  if (m_Phase == 3)
  {
   // One scan cursor per area, as the coordinator keys them.
   bool workA;
   bool workB;
   for (int attempt = 0; attempt < 8; attempt++)
   {
    if (homes.DiscoverNearPlayer(EAC_HomeIndex.AREA_FOCUS_BASE + m_ModuleA.GetAreaId(), m_ModuleA.GetOrigin(), m_ModuleA.SettlementRadius, 1, 2)) workA = true;
    if (homes.DiscoverNearPlayer(EAC_HomeIndex.AREA_FOCUS_BASE + m_ModuleB.GetAreaId(), m_ModuleB.GetOrigin(), m_ModuleB.SettlementRadius, 1, 2)) workB = true;
   }
   homes.Reconcile(1, 2, true);
   CountPopulation();
   if ((workA || workB) && now - m_PhaseStarted < INDEX_DEADLINE)
    return;
   Check(m_HomesA > 0, string.Format("households indexed in town A (%1)", m_HomesA));
   Check(m_HomesB > 0, string.Format("households indexed in town B through the second module's area (%1)", m_HomesB));
   if (m_HomesA == 0 || m_HomesB == 0)
   {
    Finish("index");
    return;
   }
   Advance(4);
   return;
  }
  if (m_Phase == 4)
  {
   EAC_PedestrianSpawner spawner = m_ModuleA.GetSpawner();
   spawner.Step(m_ModuleA, m_Observers);
   spawner.SelectNext(m_ModuleA, m_Observers);
   CountPopulation();
   if (now >= m_NextProgress)
   {
    m_NextProgress = now + 10;
    PrintFormat("[EAC MULTI PROGRESS] homesA=%1 homesB=%2 committedA=%3 committedB=%4 areaA=%5 areaB=%6 %7", m_HomesA, m_HomesB, m_CommittedA, m_CommittedB, m_ModuleA.DescribeArea(), m_ModuleB.DescribeArea(), spawner.GetDiagnostics().Describe());
   }
   if ((m_CommittedA == 0 || m_CommittedB == 0) && now - m_PhaseStarted < ADMIT_DEADLINE)
    return;
   Check(m_CommittedA > 0, string.Format("module A populated town A (%1 committed)", m_CommittedA));
   Check(m_CommittedB > 0, string.Format("module B populated town B (%1 committed)", m_CommittedB));
   int populationA = m_ModuleA.GetAreaPopulation();
   int populationB = m_ModuleB.GetAreaPopulation();
   Check(populationA <= AREA_LIMIT && populationB <= AREA_LIMIT, string.Format("each area stays within its own limit (%1, %2)", populationA, populationB));
   // A full area A must not hold area B back.
   m_ModuleA.SetSetting(0, populationA);
   m_ModuleB.SetSetting(0, populationB + 2);
   m_ModuleA.RefreshPopulationAreas();
   m_Independent = Check(!m_ModuleA.HasAreaCapacity() && m_ModuleB.HasAreaCapacity(), "module A at its own limit leaves module B admitting");
   // Together above the 200 ceiling: proportional shares, both reported capped.
   m_ModuleA.SetSetting(0, 150);
   m_ModuleB.SetSetting(0, 150);
   m_ModuleA.RefreshPopulationAreas();
   bool proportional = m_ModuleA.GetAreaPopulationLimit() == 100 && m_ModuleB.GetAreaPopulationLimit() == 100 && m_ModuleA.GetWorldPopulationLimit() == 200;
   bool flagged = m_ModuleA.IsAreaCapped() && m_ModuleB.IsAreaCapped();
   m_ModuleA.SetSetting(0, 50);
   m_ModuleA.RefreshPopulationAreas();
   bool restored = m_ModuleA.GetAreaPopulationLimit() == 50 && m_ModuleB.GetAreaPopulationLimit() == 150 && !m_ModuleA.IsAreaCapped() && !m_ModuleB.IsAreaCapped();
   m_Split = Check(proportional && flagged && restored, string.Format("the 200 ceiling is split 100/100 and reported capped, then restored 50/150 (proportional=%1 flagged=%2 restored=%3)", proportional, flagged, restored));
   Finish("complete");
  }
 }
}
