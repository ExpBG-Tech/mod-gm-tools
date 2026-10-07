// TEST ONLY. EXPBG AI Surrender: an interrogated prisoner also points out nearby intel items
// (dedicated server, no players, no GM UI).
// pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -FixturePath tests/ESR_IntelRevealGameplay.c -TimeoutSeconds 300 -OrchestratorSlotGranted -ExpectResult '\[ESR INTEL RESULT\] checks=[1-9]\d* failures=0 listed=3 markers=3 refused=1 chanceZero=1 reason=complete'
// The runner copies this file to EXPG_GarrisonGameplay.c; the class names are fixed. Judge
// the run by its one [ESR INTEL RESULT] line (-ExpectResult also rejects script errors).
// Real module prefab and public settings: reveal 0%, identity 100% (every answer is his
// identity, so only intel markers are new), intel 100%, radius 300 m, lifetime 0. One
// native USSR rifle squad; soldiers are taken through the production Surrender call and
// questioned through the production Interrogate call, as tests/ESR_RevealGameplay.c does.
// Settings first: prefab default 30%, clamp, and save compatibility (ten and twelve values
// restore with the intel default, thirteen restore it, fourteen are refused).
// Scene around the first prisoner (seven real EXPBG Intel Items prefabs):
//  listed     a Soviet manual in a killed squad mate's inventory (on his body, a few
//             metres away), a blue notebook 50 m east, a US manual 100 m north;
//  capped     a tablet 200 m south: unclaimed, but the fourth nearest (at most three);
//  excluded   an armoured laptop 20 m west whose startup token is spent (a player read
//             or picked it up), an orange notebook in the prisoner's own pocket, and a
//             black notebook 330 m east (outside the 300 m radius).
//  0. The production ESR_IntelQuery alone finds four unclaimed items inside 300 m and five
//     inside 400 m (the radius alone leaves out the black notebook).
//  1. Intel 100%: he points out exactly the three nearest unclaimed items, in order, with
//     the distances (25 m steps) and compass sectors computed here from the holders'
//     positions; three static "Intel (interrogation)" markers at those positions; the
//     dialog line lists them. Asking again repeats it without new markers.
//  2. A second prisoner with identity 0%: he refuses, and no intel is rolled.
//  3. Intel 0%, identity 100%: he answers with no intel line and no marker.
// Not covered here: items carried by a player (no players in this fixture).
class EXPG_GarrisonGameplayClass : GenericEntityClass {}
class EXPG_GarrisonGameplay : GenericEntity
{
 static const float FIXTURE_SECONDS = 180;
 static const ResourceName MODULE = "{7F2E668385984EA1}PrefabsEditable/EXPSR/ESR_SurrenderModule.et";
 static const ResourceName SQUAD = "{E552DABF3636C2AD}Prefabs/Groups/OPFOR/Group_USSR_RifleSquad.et";
 static const ResourceName LAPTOP = "{46DA23F715D50DAB}PrefabsEditable/EXPII/EII_ArmoredLaptop.et";
 static const ResourceName MANUAL_SOVIET = "{7E8D916D8F449094}PrefabsEditable/EXPII/EII_ManualSoviet.et";
 static const ResourceName MANUAL_US = "{D3DCA7AB761413C6}PrefabsEditable/EXPII/EII_ManualUS.et";
 static const ResourceName NOTEBOOK_BLACK = "{4940337955AE3340}PrefabsEditable/EXPII/EII_NotebookBlack.et";
 static const ResourceName NOTEBOOK_BLUE = "{23BD5CC4A9B2CC83}PrefabsEditable/EXPII/EII_NotebookBlue.et";
 static const ResourceName NOTEBOOK_ORANGE = "{9AA03E7756B80823}PrefabsEditable/EXPII/EII_NotebookOrange.et";
 static const ResourceName TABLET = "{1B3F61C4291552DE}PrefabsEditable/EXPII/EII_Tablet2.et";
 static const string MARKER_TEXT = "Intel (interrogation)";
 static const int SIZE = 6;
 static const int RADIUS = 300;
 static const int ITEMS = 7;
 vector Origin = "4773.46 0 7094.57";
 vector EastOffset = "50 0 0";
 vector NorthOffset = "0 0 100";
 vector SouthOffset = "0 0 -200";
 vector WestOffset = "-20 0 0";
 vector OutsideOffset = "330 0 0";
 ESR_SurrenderModule Surrender;
 SCR_AIGroup Squad;
 SCR_ChimeraCharacter FirstCaptive;
 SCR_ChimeraCharacter BodyMate;
 SCR_ChimeraCharacter SecondCaptive;
 // Strong references: the records stay readable whatever the manager does with them.
 ref ESR_Prisoner FirstPrisoner;
 ref ESR_Prisoner SecondPrisoner;
 IEntity PocketItem;
 IEntity BodyItem;
 IEntity EastItem;
 IEntity NorthItem;
 IEntity SouthItem;
 IEntity SpentItem;
 IEntity OutsideItem;
 ref array<int> FirstMarkers = {};
 int RegisteredBefore;
 int MarkersBefore;
 int Listed;
 int MarkersVerified;
 int RefusedPassed;
 int ChanceZeroPassed;
 int Phase;
 int Checks;
 int Failures;
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
  PrintFormat("[ESR INTEL BEGIN] origin=%1 radius=%2 items=%3 deadline=%4", Origin, RADIUS, ITEMS, FIXTURE_SECONDS);
 }

 bool Check(bool ok, string label)
 {
  Checks++;
  if (!ok) Failures++;
  PrintFormat("[ESR INTEL CHECK] pass=%1 %2", ok, label);
  return ok;
 }

 void Finish(string reason)
 {
  if (Finished) return;
  Finished = true;
  ClearEventMask(EntityEvent.FRAME);
  PrintFormat("[ESR INTEL RESULT] checks=%1 failures=%2 listed=%3 markers=%4 refused=%5 chanceZero=%6 reason=%7", Checks, Failures, Listed, MarkersVerified, RefusedPassed, ChanceZeroPassed, reason);
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

 IEntity SpawnItem(ResourceName prefab, vector p)
 {
  // Keep the Resource in a local before spawning.
  Resource item = Resource.Load(prefab);
  IEntity spawned = GetGame().SpawnEntityPrefab(item, GetGame().GetWorld(), Params(p));
  return spawned;
 }

 SCR_AIGroup SpawnSquad(vector p)
 {
  Resource squad = Resource.Load(SQUAD);
  IEntity spawned = GetGame().SpawnEntityPrefab(squad, GetGame().GetWorld(), Params(p));
  return SCR_AIGroup.Cast(spawned);
 }

 void Configure(int identity, int intel)
 {
  Surrender.ApplySetting(ESR_Settings.ENABLED, ESR_Settings.FromBool(true));
  // No casualty-driven surrender here: prisoners come only from the Surrender calls.
  Surrender.ApplySetting(ESR_Settings.CHANCE, 0);
  Surrender.ApplySetting(ESR_Settings.THRESHOLD, 100);
  Surrender.ApplySetting(ESR_Settings.RANDOM, 0);
  Surrender.ApplySetting(ESR_Settings.REVEAL, 0);
  Surrender.ApplySetting(ESR_Settings.IDENTITY, identity);
  Surrender.ApplySetting(ESR_Settings.INTEL, intel);
  Surrender.ApplySetting(ESR_Settings.RADIUS, RADIUS);
  Surrender.ApplySetting(ESR_Settings.ATTEMPTS, 3);
  Surrender.ApplySetting(ESR_Settings.LIFETIME, 0);
  Surrender.ApplySetting(ESR_Settings.DIAGNOSTICS, 1);
  // The save checks left commander values behind; back to their defaults.
  Surrender.ApplySetting(ESR_Settings.GRENADE, 0);
  Surrender.ApplySetting(ESR_Settings.GRENADE_CARRY, 1);
 }

 int Registered()
 {
  array<IEntity> items = {};
  return EII_IntelComponent.GetRegistered(items);
 }

 int StaticMarkers()
 {
  SCR_MapMarkerManagerComponent markers = SCR_MapMarkerManagerComponent.GetInstance();
  if (!markers) return -1;
  return markers.GetStaticMarkers().Count();
 }

 // The one intel item in a soldier's inventory.
 IEntity CarriedIntel(IEntity character)
 {
  if (!character) return null;
  InventoryStorageManagerComponent inventory = InventoryStorageManagerComponent.Cast(character.FindComponent(InventoryStorageManagerComponent));
  if (!inventory) return null;
  array<IEntity> items = {};
  inventory.FindItemsWithComponents(items, {EII_IntelComponent}, EStoragePurpose.PURPOSE_ANY);
  if (items.Count() != 1) return null;
  return items[0];
 }

 bool GiveIntel(IEntity character, ResourceName prefab)
 {
  if (!character) return false;
  InventoryStorageManagerComponent inventory = InventoryStorageManagerComponent.Cast(character.FindComponent(InventoryStorageManagerComponent));
  if (!inventory) return false;
  return inventory.TrySpawnPrefabToStorage(prefab);
 }

 // The reported distance (25 m steps) and compass sector (0 = north, clockwise) of a
 // point seen from the prisoner, computed independently of production.
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

 string CompassName(int sector)
 {
  if (sector == 0) return "north";
  if (sector == 1) return "north-east";
  if (sector == 2) return "east";
  if (sector == 3) return "south-east";
  if (sector == 4) return "south";
  if (sector == 5) return "south-west";
  if (sector == 6) return "west";
  return "north-west";
 }

 string ExpectedPlace(int distance, int sector)
 {
  if (distance <= 0) return "a few metres " + CompassName(sector);
  return string.Format("about %1 m %2", distance, CompassName(sector));
 }

 // A living soldier of the squad other than its leader and the excluded ones.
 SCR_ChimeraCharacter Member(IEntity excluded, IEntity excludedToo)
 {
  if (!Squad) return null;
  array<AIAgent> agents = {};
  Squad.GetAgents(agents);
  IEntity leader = Squad.GetLeaderEntity();
  foreach (AIAgent agent : agents)
  {
   if (!agent) continue;
   SCR_ChimeraCharacter member = SCR_ChimeraCharacter.Cast(agent.GetControlledEntity());
   if (!member || member == leader || member == excluded || member == excludedToo || !member.GetCharacterController() || member.GetCharacterController().IsDead()) continue;
   return member;
  }
  return null;
 }

 string OneLine(string text)
 {
  string line = text;
  line.Replace("\n", " | ");
  return line;
 }

 override void EOnFrame(IEntity owner, float timeSlice)
 {
  if (Finished || Now() < Next) return;
  Next = Now() + 0.5;
  if (Now() - Started > FIXTURE_SECONDS) { Check(false, string.Format("phase %1 finished before the fixture deadline", Phase)); Finish("timeout"); return; }
  Step();
 }

 void Settings()
 {
  Check(ESR_Settings.INTEL == 12 && ESR_Settings.COUNT == 13 && ESR_Settings.COUNT_V2 == 12 && ESR_Settings.COUNT_V1 == 10 && !ESR_Settings.IsBoolean(ESR_Settings.INTEL), "intel is key 12 of thirteen; twelve in esrVersion 2 saves");
  Check(ESR_Settings.Default(ESR_Settings.INTEL) == 30 && Surrender.GetSetting(ESR_Settings.INTEL) == 30 && ESR_Settings.Get(ESR_Settings.INTEL) == 30, "prefab default: reveal intel items 30%");
  array<int> saved = {};
  for (int key = 0; key < ESR_Settings.COUNT_V1; key++) saved.Insert(ESR_Settings.Default(key));
  Surrender.ApplySetting(ESR_Settings.INTEL, 70);
  bool restoredV1 = Surrender.RestoreSettings(saved);
  Check(restoredV1 && ESR_Settings.Get(ESR_Settings.INTEL) == 30 && Surrender.GetSetting(ESR_Settings.INTEL) == 30, "a ten-value save restores with the intel default");
  saved.Insert(40);
  saved.Insert(0);
  Surrender.ApplySetting(ESR_Settings.INTEL, 70);
  bool restoredV2 = Surrender.RestoreSettings(saved);
  Check(restoredV2 && ESR_Settings.Get(ESR_Settings.GRENADE) == 40 && ESR_Settings.Get(ESR_Settings.INTEL) == 30, "a twelve-value save restores with the intel default");
  saved.Insert(55);
  bool restoredV3 = Surrender.RestoreSettings(saved);
  Check(restoredV3 && Surrender.GetSetting(ESR_Settings.INTEL) == 55 && ESR_Settings.Get(ESR_Settings.INTEL) == 55, "a thirteen-value save restores and mirrors the intel setting");
  saved.Insert(1);
  Check(!Surrender.RestoreSettings(saved) && ESR_Settings.Get(ESR_Settings.INTEL) == 55, "a fourteen-value save is refused");
  Surrender.ApplySetting(ESR_Settings.INTEL, 150);
  Check(ESR_Settings.Get(ESR_Settings.INTEL) == 100 && Surrender.GetSetting(ESR_Settings.INTEL) == 100, "intel chance clamped to 100 and mirrored");
 }

 // One method per phase: every local keeps its own scope.
 void Step()
 {
  if (Phase == 0) SetupScene();
  else if (Phase == 1) GiveCarriedIntel();
  else if (Phase == 2) TakeFirstPrisoner();
  else if (Phase == 3) PlaceLooseIntel();
  else if (Phase == 4) AskFirst();
  else if (Phase == 5) AskFirstAgain();
  else if (Phase == 6) AskRefusing();
  else if (Phase == 7) AskChanceZero();
 }

 void NextPhase()
 {
  Phase++;
  PhaseAt = Now();
 }

 void SetupScene()
 {
  if (!Check(GetGame().GetPlayerManager().GetPlayerCount() == 0 && ESR_SurrenderManager.PrisonerCount() == 0, "isolated server: no players, no prisoners")) { Finish("setup"); return; }
  RegisteredBefore = Registered();
  Resource modulePrefab = Resource.Load(MODULE);
  IEntity moduleEntity = GetGame().SpawnEntityPrefab(modulePrefab, GetGame().GetWorld(), Params(Origin));
  Surrender = ESR_SurrenderModule.Cast(moduleEntity);
  if (!Check(Surrender != null, "surrender module prefab spawned")) { Finish("setup"); return; }
  Settings();
  Configure(100, 100);
  Check(ESR_Settings.Get(ESR_Settings.INTEL) == 100 && ESR_Settings.Get(ESR_Settings.REVEAL) == 0 && ESR_Settings.Get(ESR_Settings.IDENTITY) == 100 && ESR_Settings.Get(ESR_Settings.RADIUS) == RADIUS, "intel 100%, reveal 0%, identity 100%, radius 300 m");
  Squad = SpawnSquad(Origin);
  if (!Check(Squad != null, "native USSR squad spawned")) { Finish("setup"); return; }
  NextPhase();
 }

 void GiveCarriedIntel()
 {
  if (!Squad || Squad.GetAgentsCount() != SIZE)
  {
   if (Now() - PhaseAt > 30) { Check(false, "the squad completed its initial spawn"); Finish("setup"); }
   return;
  }
  FirstCaptive = Member(null, null);
  BodyMate = Member(FirstCaptive, null);
  SecondCaptive = Member(FirstCaptive, BodyMate);
  if (!Check(FirstCaptive && BodyMate && SecondCaptive, "three squad members picked")) { Finish("setup"); return; }
  bool pocket = GiveIntel(FirstCaptive, NOTEBOOK_ORANGE);
  bool body = GiveIntel(BodyMate, MANUAL_SOVIET);
  if (!Check(pocket && body, "intel spawned into two soldiers' inventories")) { Finish("setup"); return; }
  NextPhase();
 }

 void TakeFirstPrisoner()
 {
  // Inventory operations settle first.
  if (Now() - PhaseAt < 2) return;
  PocketItem = CarriedIntel(FirstCaptive);
  BodyItem = CarriedIntel(BodyMate);
  if (!Check(PocketItem && BodyItem && PocketItem.GetRootParent() == FirstCaptive && BodyItem.GetRootParent() == BodyMate, "one intel item in each soldier's inventory")) { Finish("setup"); return; }
  bool taken = ESR_SurrenderManager.Surrender(FirstCaptive, Squad);
  FirstPrisoner = ESR_SurrenderManager.FindPrisoner(FirstCaptive);
  if (!Check(taken && FirstPrisoner != null && FirstPrisoner.Point != null, "first soldier surrendered through production with an interrogation point")) { Finish("surrender"); return; }
  SCR_CharacterDamageManagerComponent damage = SCR_CharacterDamageManagerComponent.Cast(BodyMate.GetDamageManager());
  if (damage) damage.Kill(Instigator.CreateInstigator(null));
  NextPhase();
 }

 void PlaceLooseIntel()
 {
  // Weapon drop, sit-down and the body's fall settle.
  if (Now() - PhaseAt < 3) return;
  if (!Check(BodyMate && BodyMate.GetCharacterController() && BodyMate.GetCharacterController().IsDead(), "the squad mate carrying intel is dead")) { Finish("kill"); return; }
  if (!Check(FirstPrisoner && FirstPrisoner.Character && FirstPrisoner.Point, "first prisoner record still holds him and his point")) { Finish("records"); return; }
  vector origin = FirstPrisoner.Character.GetOrigin();
  EastItem = SpawnItem(NOTEBOOK_BLUE, origin + EastOffset);
  NorthItem = SpawnItem(MANUAL_US, origin + NorthOffset);
  SouthItem = SpawnItem(TABLET, origin + SouthOffset);
  SpentItem = SpawnItem(LAPTOP, origin + WestOffset);
  OutsideItem = SpawnItem(NOTEBOOK_BLACK, origin + OutsideOffset);
  if (!Check(EastItem && NorthItem && SouthItem && SpentItem && OutsideItem, "five loose intel items spawned")) { Finish("items"); return; }
  // A player reading or picking up a computer item consumes its startup token.
  EII_IntelComponent spent = EII_IntelComponent.Cast(SpentItem.FindComponent(EII_IntelComponent));
  if (spent) spent.TryStartup(SpentItem.GetOrigin());
  EII_IntelComponent tablet = EII_IntelComponent.Cast(SouthItem.FindComponent(EII_IntelComponent));
  Check(spent && spent.HasStarted() && tablet && !tablet.HasStarted(), "the laptop's startup token is spent, the tablet's is not");
  NextPhase();
 }

 // Inserts a candidate position into the nearest-first list.
 void InsertNearest(notnull array<vector> positions, notnull array<float> distances, vector origin, vector candidate)
 {
  float distanceSq = vector.DistanceSqXZ(origin, candidate);
  int slot = distances.Count();
  while (slot > 0 && distances[slot - 1] > distanceSq) slot--;
  distances.InsertAt(distanceSq, slot);
  positions.InsertAt(candidate, slot);
 }

 // A placed intel marker: exact text and type, at the item's (truncated) map position.
 bool MarkerAt(int markerId, vector spot)
 {
  SCR_MapMarkerManagerComponent markers = SCR_MapMarkerManagerComponent.GetInstance();
  if (!markers || markerId < 0) return false;
  SCR_MapMarkerBase marker = markers.GetStaticMarkerByID(markerId);
  if (!marker) return false;
  int pos[2];
  marker.GetWorldPos(pos);
  int x = spot[0];
  int z = spot[2];
  bool placed = marker.GetCustomText() == MARKER_TEXT && marker.GetType() == SCR_EMapMarkerType.PLACED_CUSTOM && pos[0] == x && pos[1] == z;
  PrintFormat("[ESR INTEL MARKER] id=%1 text='%2' type=%3 pos=%4,%5 expected=%6,%7 ok=%8", markerId, marker.GetCustomText(), marker.GetType(), pos[0], pos[1], x, z, placed);
  return placed;
 }

 void AskFirst()
 {
  // Let the loose items settle.
  if (Now() - PhaseAt < 3) return;
  if (!Check(FirstPrisoner && FirstPrisoner.Character && FirstPrisoner.Point && BodyMate && BodyItem && PocketItem, "prisoner, body and carried items still present")) { Finish("records"); return; }
  int registered = Registered();
  Check(registered == RegisteredBefore + ITEMS, string.Format("Intel Items registry lists the seven items (%1 -> %2)", RegisteredBefore, registered));
  Check(PocketItem.GetRootParent() == FirstCaptive && BodyItem.GetRootParent() == BodyMate, "the pocket notebook is still on the prisoner, the manual on the body");
  vector origin = FirstPrisoner.Character.GetOrigin();
  Check(vector.DistanceXZ(origin, SpentItem.GetOrigin()) < 40 && vector.DistanceXZ(origin, OutsideItem.GetOrigin()) > RADIUS + 10, "the spent laptop lies near, the black notebook outside the radius");
  // Expected: the three nearest of the four unclaimed items inside the radius.
  array<vector> expected = {};
  array<float> expectedSq = {};
  InsertNearest(expected, expectedSq, origin, BodyMate.GetOrigin());
  InsertNearest(expected, expectedSq, origin, EastItem.GetOrigin());
  InsertNearest(expected, expectedSq, origin, NorthItem.GetOrigin());
  InsertNearest(expected, expectedSq, origin, SouthItem.GetOrigin());
  // The production query on its own: four unclaimed items inside 300 m (the spent laptop
  // and the prisoner's notebook never count); at 400 m the black notebook joins them.
  ESR_IntelQuery probe = new ESR_IntelQuery();
  probe.Collect(origin, RADIUS, FirstPrisoner.Character);
  int eligible = probe.EligibleCount();
  int listedByProbe = probe.Count();
  probe.Collect(origin, 400, FirstPrisoner.Character);
  PrintFormat("[ESR INTEL PROBE] registered=%1 scanned=%2 eligible300=%3 listed300=%4 eligible400=%5", probe.RegisteredCount(), probe.ScannedCount(), eligible, listedByProbe, probe.EligibleCount());
  Check(eligible == 4 && listedByProbe == 3 && probe.EligibleCount() == 5, "query: four unclaimed items inside the radius, at most three listed, the item beyond it left out by the radius alone");
  MarkersBefore = StaticMarkers();
  if (!Check(MarkersBefore >= 0, "the game mode has a map marker manager")) { Finish("markers"); return; }
  int outcome = ESR_SurrenderManager.Interrogate(FirstPrisoner.Point, FirstPrisoner.Point, 0);
  int markersAfter = StaticMarkers();
  Listed = FirstPrisoner.IntelDistances.Count();
  PrintFormat("[ESR INTEL FIRST] outcome=%1 rolled=%2 listed=%3 markers=%4->%5", outcome, FirstPrisoner.IntelRolled, Listed, MarkersBefore, markersAfter);
  Check(outcome == ESR_SurrenderManager.OUTCOME_IDENTITY && FirstPrisoner.IntelRolled, "his answer is his identity, and the intel chance was rolled");
  bool lists = Listed == 3 && FirstPrisoner.IntelBearings.Count() == 3 && FirstPrisoner.IntelMarkers.Count() == 3;
  bool order = lists;
  string expectedLine = "He also points out 3 intel items: ";
  for (int i = 0; i < 3; i++)
  {
   vector spot = expected[i];
   int distance = ExpectedDistance(origin, spot);
   int sector = ExpectedSector(origin, spot);
   if (i > 0) expectedLine += ", ";
   expectedLine += ExpectedPlace(distance, sector);
   if (!lists) continue;
   PrintFormat("[ESR INTEL ITEM] index=%1 distance=%2 bearing=%3 marker=%4 expectedDistance=%5 expectedBearing=%6 at %7", i, FirstPrisoner.IntelDistances[i], FirstPrisoner.IntelBearings[i], FirstPrisoner.IntelMarkers[i], distance, sector, spot);
   if (FirstPrisoner.IntelDistances[i] != distance || FirstPrisoner.IntelBearings[i] != sector) order = false;
   FirstMarkers.Insert(FirstPrisoner.IntelMarkers[i]);
   if (MarkerAt(FirstPrisoner.IntelMarkers[i], spot)) MarkersVerified++;
  }
  expectedLine += ". Each is marked on your map.";
  Check(order, "he lists the three nearest unclaimed items, nearest first, with their 25 m distances and compass sectors");
  if (Listed == 3)
  {
   Check(FirstPrisoner.IntelDistances[0] <= 25, "first: the manual on the body a few metres away");
   Check(FirstPrisoner.IntelDistances[1] == 50 && FirstPrisoner.IntelBearings[1] == 2, "second: the notebook 50 m east");
   Check(FirstPrisoner.IntelDistances[2] == 100 && FirstPrisoner.IntelBearings[2] == 0, "third: the manual 100 m north");
  }
  Check(MarkersVerified == 3 && markersAfter == MarkersBefore + 3, "three 'Intel (interrogation)' map markers at the items' positions");
  string text = FirstPrisoner.Point.Describe(outcome, FirstPrisoner.RevealCount, FirstPrisoner.RevealDistance, FirstPrisoner.RevealBearing, 2, FirstPrisoner.IntelDistances, FirstPrisoner.IntelBearings);
  PrintFormat("[ESR INTEL LINE] expected='%1' text='%2'", expectedLine, OneLine(text));
  Check(text.Contains(expectedLine), "the dialog lists the three items");
  NextPhase();
 }

 void AskFirstAgain()
 {
  // Past the question cooldown.
  if (Now() - PhaseAt < ESR_SurrenderManager.QUESTION_COOLDOWN + 0.5) return;
  int before = StaticMarkers();
  int repeat = ESR_SurrenderManager.Interrogate(FirstPrisoner.Point, FirstPrisoner.Point, 0);
  bool same = FirstPrisoner.IntelMarkers.Count() == FirstMarkers.Count();
  for (int i = 0; i < FirstMarkers.Count() && same; i++)
  {
   if (FirstPrisoner.IntelMarkers[i] != FirstMarkers[i]) same = false;
  }
  PrintFormat("[ESR INTEL REPEAT] outcome=%1 listed=%2 markers=%3->%4", repeat, FirstPrisoner.IntelDistances.Count(), before, StaticMarkers());
  Check(repeat == ESR_SurrenderManager.OUTCOME_IDENTITY && same && FirstPrisoner.IntelDistances.Count() == Listed && StaticMarkers() == before, "asking again repeats the items without new markers");
  // A second prisoner who refuses: identity 0%, intel 100%.
  Configure(0, 100);
  bool taken = ESR_SurrenderManager.Surrender(SecondCaptive, Squad);
  SecondPrisoner = ESR_SurrenderManager.FindPrisoner(SecondCaptive);
  if (!Check(taken && SecondPrisoner != null && SecondPrisoner.Point != null, "second soldier surrendered through production")) { Finish("surrender"); return; }
  NextPhase();
 }

 void AskRefusing()
 {
  if (Now() - PhaseAt < 3) return;
  if (!Check(SecondPrisoner && SecondPrisoner.Character && SecondPrisoner.Point, "second prisoner record still holds him and his point")) { Finish("records"); return; }
  int before = StaticMarkers();
  int refused = ESR_SurrenderManager.Interrogate(SecondPrisoner.Point, SecondPrisoner.Point, 0);
  PrintFormat("[ESR INTEL REFUSED] outcome=%1 rolled=%2 listed=%3 markers=%4->%5", refused, SecondPrisoner.IntelRolled, SecondPrisoner.IntelDistances.Count(), before, StaticMarkers());
  if (Check(refused == ESR_SurrenderManager.OUTCOME_REFUSED && !SecondPrisoner.IntelRolled && SecondPrisoner.IntelDistances.IsEmpty() && StaticMarkers() == before, "a refusal rolls no intel (intel 100%)")) RefusedPassed = 1;
  // He talks next time, with intel at 0%.
  Configure(100, 0);
  NextPhase();
 }

 void AskChanceZero()
 {
  if (Now() - PhaseAt < ESR_SurrenderManager.QUESTION_COOLDOWN + 0.5) return;
  int before = StaticMarkers();
  int answer = ESR_SurrenderManager.Interrogate(SecondPrisoner.Point, SecondPrisoner.Point, 0);
  string text = SecondPrisoner.Point.Describe(answer, SecondPrisoner.RevealCount, SecondPrisoner.RevealDistance, SecondPrisoner.RevealBearing, 1, SecondPrisoner.IntelDistances, SecondPrisoner.IntelBearings);
  PrintFormat("[ESR INTEL ZERO] outcome=%1 rolled=%2 listed=%3 markers=%4->%5 text='%6'", answer, SecondPrisoner.IntelRolled, SecondPrisoner.IntelDistances.Count(), before, StaticMarkers(), OneLine(text));
  if (Check(answer == ESR_SurrenderManager.OUTCOME_IDENTITY && SecondPrisoner.IntelRolled && SecondPrisoner.IntelDistances.IsEmpty() && SecondPrisoner.IntelMarkers.IsEmpty() && StaticMarkers() == before && !text.Contains("intel item"), "intel 0%: he answers with no intel line and no marker")) ChanceZeroPassed = 1;
  Finish("complete");
 }
}
