// Ambient Civilians native CDF ownership fixture (issue #4), ported from the
// archived standalone repo (tests/Enforce/EAC_CDFSessionTests.c, last run on
// standalone 0.0.19: 106 checks, 4 failures). Disposable integration fixture:
// compile it only in a throwaway fixture project that depends on the base game,
// EXPBG GM Tools and the real installed CDF GameMaster Save 1.4.1
// (6A1876F37D65AB09). The pack itself never depends on CDF.
//
// Runner contract (no runner exists in this repo yet):
//  - fixture project Scripts/Game: this file and EAC_SessionCodecTests.c;
//  - fixture project Configs: tests/Configs/EAC_SessionThemes.conf and .meta;
//  - one EAC_CDFSessionTests entity in a GM_Eden subscene at 4773 169 7094,
//    dedicated Diag server with the GameMasterSystems world systems config;
//  - pass: exactly one "[EAC CDF RESULT] checks=N failures=0" with N >= 100,
//    exit 0, "Game destroyed", no compile error, VM exception or SCRIPT (E).
//
// Ported to the 0.1.4 save-exclusion model. Transient civilians and traffic are
// no longer refused by a Serialize override at capture time: EAC_SessionLifecycle
// flags them NON_SERIALIZABLE on creation and on its 1 s Sync, and vanilla
// Serialize (which CDF calls) honours the flag. This fixture disables the module
// tick that drives Sync and builds its population by hand, so it now forces one
// Sync before each capture (EAC_TestSync). Without it nothing would be flagged
// and the capture checks would fail for a fixture reason. Spawns keep
// Resource.Load in a local.
modded class EAC_PedestrianSpawner
{
 void EAC_TestSessionTrack(EAC_PedestrianActivation activation) { m_Tracked.Insert(activation); }
}
modded class EAC_TrafficDirector
{
 void EAC_TestSessionTrack(EAC_TrafficParty party) { m_Parties.Insert(party); }
}
modded class EAC_PedestrianWalk
{
 void EAC_TestSessionOrder(SCR_AIGroup group, AIWaypoint order)
 {
  m_Group = group; m_Waypoint = order;
  group.AddWaypoint(order);
 }
}
modded class EAC_SessionLifecycle
{
 // One Sync now, whatever the 1 s throttle says: the module tick that normally
 // drives it is disabled in this fixture.
 static void EAC_TestSync(float now) { s_NextSync = 0; Sync(now); }
}
class EAC_CDFSessionTestsClass : GenericEntityClass {}
class EAC_CDFSessionTests : GenericEntity
{
 int m_Checks, m_Failures, m_Phase, m_CleanupTicks, m_RegroupedRecord = -1;
 float m_Next;
 EAC_AmbientModule m_Module;
 EAC_ExclusionZone m_Zone;
 ref CDF_GMSaveDocument m_Document;
 ref array<int> m_Expected = {};
 ref EAC_PedestrianActivation m_Owned, m_Transferred, m_Regrouped;
 ref EAC_TrafficParty m_Traffic;
 IEntity m_Manual, m_ManualSibling;
 AIWaypoint m_WalkOrder;
 SCR_AIGroup m_ForeignGroup;
 EntityID m_ManualId, m_TransferredId;
 static const string FILE = "$profile:eac-cdf-session.json";
 static const ResourceName CATALOG = "{BCEEC811A0C00031}Configs/EAC_SessionThemes.conf";

 void EAC_CDFSessionTests(IEntitySource src, IEntity parent) { SetEventMask(EntityEvent.INIT | EntityEvent.FRAME); }
 override void EOnInit(IEntity owner) { m_Next = GetGame().GetWorld().GetWorldTime() * 0.001 + 5; }
 void Check(bool pass, string label)
 {
  m_Checks++;
  if (!pass) m_Failures++;
  PrintFormat("[EAC CDF CHECK] pass=%1 %2", pass, label);
 }
 IEntity Spawn(ResourceName prefab, float offset = 0, bool authored = true)
 {
  EntitySpawnParams p = new EntitySpawnParams();
  p.TransformMode = ETransformMode.WORLD;
  Math3D.AnglesToMatrix(vector.Zero, p.Transform);
  vector position = "4773 169 7094";
  position += Vector(offset, 0, 0);
  position[1] = GetWorld().GetSurfaceY(position[0], position[2]) + 0.2;
  p.Transform[3] = position;
  Resource resource = Resource.Load(prefab);
  IEntity entity = GetGame().SpawnEntityPrefab(resource, GetGame().GetWorld(), p);
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.GetEditableEntity(entity);
  if (editable && authored)
  {
   SCR_EditableEntityAuthor author = new SCR_EditableEntityAuthor();
   author.Initialize("EAC_CDF_DISPOSABLE", "", 0, -1);
   editable.SetAuthor(author);
   SCR_EditableEntityCore core = SCR_EditableEntityCore.Cast(SCR_EditableEntityCore.GetInstance(SCR_EditableEntityCore));
   if (core) core.RegisterAuthorServer(author);
  }
  return entity;
 }

 EAC_PedestrianActivation MakeResident(EAC_HouseholdRecord home, int slot, float offset)
 {
  EAC_PedestrianActivation activation = new EAC_PedestrianActivation();
  SCR_AIGroup group = SCR_AIGroup.Cast(Spawn("{000CD338713F2B5A}Prefabs/AI/Groups/Group_Base.et", offset, false));
  IEntity actor = Spawn(EAC_HomeIndex.DEFAULT_CHARACTER, offset, false);
  if (!group || !actor) return null;
  group.SetDeleteWhenEmpty(false);
  activation.Claim = m_Module.BeginResidentActivation(home, home.Residents[slot], actor.GetOrigin());
  if (!activation.Claim || !m_Module.TrackResidentGroup(activation.Claim, group) || !m_Module.TrackResidentCharacter(activation.Claim, actor)) return null;
  group.SetFaction(GetGame().GetFactionManager().GetFactionByKey("CIV"));
  FactionAffiliationComponent.Cast(actor.FindComponent(FactionAffiliationComponent)).SetAffiliatedFactionByKey("CIV");
  group.AddAIEntityToGroup(actor);
  activation.SettleUntil = GetWorld().GetWorldTime() * 0.001;
  activation.Position = actor.GetOrigin();
  if (!activation.Bind() || !m_Module.CommitResidentActivation(activation.Claim)) return null;
  m_Module.GetSpawner().EAC_TestSessionTrack(activation);
  return activation;
 }

 void PreparePopulation()
 {
  m_Module.ClearEventMask(EntityEvent.FRAME);
  m_Module.HomeIsolationRule = 0;
  m_Zone.SetOrigin(m_Module.GetOrigin() + "2000 0 0");
  EAC_HouseholdRecord home = m_Module.GetHomeIndex().GetRegistry().Register(this, false, 3, EAC_HomeIndex.DEFAULT_CHARACTER);
  m_Owned = MakeResident(home, 0, 30);
  m_Transferred = MakeResident(home, 1, 50);
  m_Regrouped = MakeResident(home, 2, 70);
  Check(m_Owned && m_Transferred && m_Regrouped, "three real native residents are retained by the production spawner and ledger");
  if (!m_Owned || !m_Transferred || !m_Regrouped) return;
  m_WalkOrder = AIWaypoint.Cast(Spawn("{750A8D1695BD6998}Prefabs/AI/Waypoints/AIWaypoint_Move.et", 180, false));
  if (m_WalkOrder) m_Owned.Walking.EAC_TestSessionOrder(m_Owned.Claim.Group, m_WalkOrder);
  // The runtime uses the basic move waypoint, which has no editor component.
  array<AIWaypoint> walkingOrders = {};
  m_Owned.Claim.Group.GetWaypoints(walkingOrders);
  Check(m_WalkOrder && walkingOrders.Contains(m_WalkOrder), "native walking waypoint belongs to the owned group's order list");
  m_Transferred.OnControl(m_Transferred.Claim.Character, true);
  m_TransferredId = m_Transferred.Claim.Character.GetID();
  m_Manual = Spawn(EAC_HomeIndex.DEFAULT_CHARACTER, 80, false);
  if (m_Manual) m_ManualId = m_Manual.GetID();
  // A real authored parent/manual sibling must remain capturable despite its
  // managed child. This checks the CDF recursion boundary, not a regex model.
  m_ForeignGroup = SCR_AIGroup.Cast(Spawn("{000CD338713F2B5A}Prefabs/AI/Groups/Group_Base.et", 100));
  m_ManualSibling = Spawn(EAC_HomeIndex.DEFAULT_CHARACTER, 100);
  if (m_ForeignGroup)
  {
   m_ForeignGroup.SetDeleteWhenEmpty(false);
   SCR_EditableEntityComponent parent = SCR_EditableEntityComponent.GetEditableEntity(m_ForeignGroup);
   SCR_EditableEntityComponent.GetEditableEntity(m_Owned.Claim.Group).SetParentEntity(parent);
   if (m_ManualSibling) SCR_EditableEntityComponent.GetEditableEntity(m_ManualSibling).SetParentEntity(parent);
   m_ForeignGroup.AddAIEntityToGroup(m_Regrouped.Claim.Character);
   SCR_EditableEntityComponent.GetEditableEntity(m_Regrouped.Claim.Character).SetParentEntity(parent);
   Check(SCR_ChimeraCharacter.Cast(m_Regrouped.Claim.Character).GetCharacterGroup() == m_ForeignGroup, "native regroup moves a prior resident into the manually authored group");
   // Simulate the external editor removing the now-empty original group.
   if (m_Regrouped.Claim.Group && m_Regrouped.Claim.Group.GetAgentsCount() == 0) SCR_EntityHelper.DeleteEntityAndChildren(m_Regrouped.Claim.Group);
   if (m_ManualSibling)
   {
    SCR_EditableEntityComponent.GetEditableEntity(m_ManualSibling).SetParentEntity(SCR_EditableEntityComponent.GetEditableEntity(m_Owned.Claim.Group));
    // The flags must describe this exact hierarchy before CDF reads them.
    EAC_SessionLifecycle.EAC_TestSync(GetWorld().GetWorldTime() * 0.001);
    TraceState("mixed_owned_group_before", m_Owned.Claim.Group, m_Owned.Claim);
    TraceState("mixed_owned_actor_before", m_Owned.Claim.Character, m_Owned.Claim);
    CDF_GMSaveDocument mixed = CDF_GMSaveCapture.Capture("EAC mixed group boundary", "disposable fixture");
    TraceCapture(mixed, "mixed_owned_group", m_Owned.Claim.Group, m_Owned.Claim);
    TraceCapture(mixed, "mixed_owned_actor", m_Owned.Claim.Character, m_Owned.Claim);
    TraceCapture(mixed, "mixed_manual_sibling", m_ManualSibling);
    Check(mixed && HasRecord(mixed, m_Owned.Claim.Group) && HasRecord(mixed, m_ManualSibling) && !HasRecord(mixed, m_Owned.Claim.Character), "original Ambient group with a manual member remains capturable without its exact owned resident");
    SCR_EditableEntityComponent.GetEditableEntity(m_ManualSibling).SetParentEntity(parent);
   }
  }
  m_Traffic = new EAC_TrafficParty(); m_Traffic.Id = 1900001;
  m_Traffic.Group = SCR_AIGroup.Cast(Spawn("{000CD338713F2B5A}Prefabs/AI/Groups/Group_Base.et", 140, false));
  m_Traffic.Car = Spawn("{54C3CC22DEBD57BE}Prefabs/Vehicles/Wheeled/S105/S105_beige.et", 140, false);
  m_Traffic.Phase = EAC_TrafficPhase.PARKED;
  EAC_TrafficOccupant driver = new EAC_TrafficOccupant(); m_Traffic.Crew.Insert(driver);
  if (m_Traffic.Group) m_Traffic.Group.SetDeleteWhenEmpty(false);
  if (m_Traffic.Car) m_Traffic.BindCar();
  m_Traffic.Reserved = m_Module.TryReservePopulation(m_Traffic.Id, 1);
  EAC_TrafficDirector.Get().EAC_TestSessionTrack(m_Traffic);
  Check(m_Traffic.Car && m_Traffic.Group && m_Traffic.Reserved, "real owned parked traffic is retained with its population reservation");
 }

 bool HasRecord(CDF_GMSaveDocument document, IEntity entity)
 {
  if (!document || !entity) return false;
  foreach (CDF_GMSaveEntityRecord row : document.m_aEntities)
   if (row.m_Entity && row.m_Entity.GetOwner() == entity) return true;
  return false;
 }

 // The save-exclusion flag this pack sets, as CDF's Serialize call sees it.
 bool Flagged(IEntity entity)
 {
  if (!entity) return false;
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.GetEditableEntity(entity);
  if (!editable) return false;
  return editable.HasEntityFlag(EEditableEntityFlag.NON_SERIALIZABLE);
 }

 // Native and editor membership of one entity before a capture: the record the
 // standalone analysis lacked to tell a fixture regroup from a production defect.
 void TraceState(string label, IEntity entity, EAC_ResidentClaim claim = null)
 {
  if (!entity) { PrintFormat("[EAC CDF STATE] role=%1 entity_missing=1", label); return; }
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.GetEditableEntity(entity);
  EntityID parentId, nativeGroupId, claimedGroupId;
  int children = -1;
  if (editable)
  {
   children = editable.GetChildrenCount(true);
   SCR_EditableEntityComponent parent = editable.GetParentEntity();
   if (parent && parent.GetOwner()) parentId = parent.GetOwner().GetID();
  }
  AIControlComponent control = AIControlComponent.Cast(entity.FindComponent(AIControlComponent));
  if (control && control.GetAIAgent() && control.GetAIAgent().GetParentGroup()) nativeGroupId = control.GetAIAgent().GetParentGroup().GetID();
  int agents = -1;
  SCR_AIGroup group = SCR_AIGroup.Cast(entity);
  if (group) agents = group.GetAgentsCount();
  if (claim && claim.Group) claimedGroupId = claim.Group.GetID();
  PrintFormat("[EAC CDF STATE] role=%1 entity=%2 editor_parent=%3 editor_children=%4 native_group=%5 claimed_group=%6 agents=%7 flagged=%8", label, entity.GetID(), parentId, children, nativeGroupId, claimedGroupId, agents, Flagged(entity));
 }

 // Inspect after capture: observing transfer history must not alter its inputs.
 void TraceCapture(CDF_GMSaveDocument document, string label, IEntity entity, EAC_ResidentClaim claim = null)
 {
  if (!entity) { PrintFormat("[EAC CDF OWNERSHIP] role=%1 entity_missing=1", label); return; }
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.GetEditableEntity(entity);
  if (!editable) { PrintFormat("[EAC CDF OWNERSHIP] role=%1 editor_missing=1", label); return; }
  EntityID parentId, nativeGroupId, claimedGroupId;
  SCR_EditableEntityComponent parent = editable.GetParentEntity();
  if (parent && parent.GetOwner()) parentId = parent.GetOwner().GetID();
  AIControlComponent control = AIControlComponent.Cast(entity.FindComponent(AIControlComponent));
  if (control && control.GetAIAgent() && control.GetAIAgent().GetParentGroup()) nativeGroupId = control.GetAIAgent().GetParentGroup().GetID();
  bool transferred;
  if (claim)
  {
   if (claim.Group) claimedGroupId = claim.Group.GetID();
   transferred = claim.EAC_SessionTransferred();
  }
  PrintFormat("[EAC CDF OWNERSHIP] role=%1 captured=%2 entity=%3 editor_parent=%4 native_group=%5 claimed_group=%6 transferred=%7 transient=%8 flagged=%9", label, HasRecord(document, entity), entity.GetID(), parentId, nativeGroupId, claimedGroupId, transferred, EAC_SessionLifecycle.IsTransient(editable), Flagged(entity));
 }

 void CheckPackedFile()
 {
  string original;
  for (int i = 0; i < 512; i++)
  {
   int code = 32 + i % 95;
   original += code.AsciiToString();
  }
  CDF_GMSaveDocument document = new CDF_GMSaveDocument();
  CDF_GMSaveEntityRecord row = new CDF_GMSaveEntityRecord(); document.m_aEntities.Insert(row);
  for (int offset = 0; offset < 512; offset += 9)
  {
   vector packed = EAC_SessionSettings.PackText(original, offset);
   row.m_aAttributeIds.Insert(offset);
   row.m_aAttributeX.Insert(packed[0]); row.m_aAttributeY.Insert(packed[1]); row.m_aAttributeZ.Insert(packed[2]);
  }
  Check(document.SaveToFile(FILE + ".codec"), "actual CDF writes all 57 packed identity vectors");
  document = new CDF_GMSaveDocument();
  bool loaded = document.LoadFromFile(FILE + ".codec");
  Check(loaded && document.GetEntityCount() == 1, "actual CDF reads packed identity test file");
  if (!loaded || document.GetEntityCount() != 1) return;
  row = document.m_aEntities[0];
  string restored;
  bool exact = row.m_aAttributeIds.Count() == 57;
  for (int block = 0; block < row.m_aAttributeIds.Count(); block++)
  {
   string part;
   vector value = Vector(row.m_aAttributeX[block], row.m_aAttributeY[block], row.m_aAttributeZ[block]);
   if (!EAC_SessionSettings.UnpackText(value, Math.Min(9, 512 - block * 9), part)) exact = false;
   restored += part;
  }
  Check(exact && restored == original, "CDF JSON float round trip preserves every packed ASCII byte including final padding");
 }

 void ApplyBlocks(array<ref SCR_BaseEditorAttributeVar> blocks)
 {
  for (int i = 0; i < blocks.Count(); i++)
   if (blocks[i]) m_Module.EAC_ReadSessionBlock(i, blocks[i].GetVector());
 }

 void CheckIndoorSavedValues()
 {
  Check(m_Module.GetSetting(22) == 100, "fresh controller defaults to indoor distance 100");
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.GetEditableEntity(m_Module);
  EAC_SessionSettingsAttribute attribute = new EAC_SessionSettingsAttribute();
  array<int> savedValues = {40, 0, 100};
  foreach (int saved : savedValues)
  {
   m_Module.SetSetting(22, saved);
   array<ref SCR_BaseEditorAttributeVar> blocks = {};
   for (int block = 0; block <= EAC_SessionSettings.COMMIT; block++)
   {
    attribute.m_Block = block;
    blocks.Insert(attribute.ReadVariable(editable, null));
   }
   m_Module.SetSetting(22, 200);
   m_Module.EAC_BeginSessionSettingsLoad();
   ApplyBlocks(blocks);
   Check(!m_Module.EAC_SessionSettingsBlocked() && m_Module.GetSetting(22) == saved, "native settings roundtrip preserves saved indoor distance " + saved.ToString());
  }
 }

 void CheckMalformedSnapshots()
 {
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.GetEditableEntity(m_Module);
  array<ref SCR_BaseEditorAttributeVar> blocks = {};
  EAC_SessionSettingsAttribute attribute = new EAC_SessionSettingsAttribute();
  for (int i = 0; i <= EAC_SessionSettings.COMMIT; i++)
  {
   attribute.m_Block = i;
   blocks.Insert(attribute.ReadVariable(editable, null));
  }
  Check(blocks[0] && blocks[76] && !blocks[75], "header and commit exist while unused identity vectors are omitted");
  Print("[EAC CDF EXPECTED ERRORS] malformed snapshots below must log explicit rejection");
  m_Module.EAC_BeginSessionSettingsLoad();
  m_Module.EAC_ReadSessionBlock(1, "1 2 3");
  Check(m_Module.EAC_SessionSettingsBlocked(), "missing header holds admissions");
  ApplyBlocks(blocks);
  Check(m_Module.EAC_SessionSettingsBlocked(), "later header cannot clear rejection within the same load");
  m_Module.EAC_BeginSessionSettingsLoad();
  m_Module.EAC_ReadSessionBlock(0, blocks[0].GetVector());
  m_Module.EAC_FinishSessionSettingsLoad();
  Check(m_Module.EAC_SessionSettingsBlocked(), "missing commit is rejected on next shared tick");
  m_Module.EAC_BeginSessionSettingsLoad();
  m_Module.EAC_ReadSessionBlock(0, blocks[0].GetVector());
  m_Module.EAC_ReadSessionBlock(0, blocks[0].GetVector());
  Check(m_Module.EAC_SessionSettingsBlocked(), "duplicate header before commit is rejected");
  m_Module.EAC_BeginSessionSettingsLoad();
  ApplyBlocks(blocks);
  Check(!m_Module.EAC_SessionSettingsBlocked(), "a new editor load boundary accepts one complete snapshot");
  m_Module.EAC_ReadSessionBlock(0, blocks[0].GetVector());
  Check(m_Module.EAC_SessionSettingsBlocked(), "duplicate header after commit is also rejected");
  m_Module.EAC_BeginSessionSettingsLoad();
  vector original = blocks[1].GetVector();
  blocks[1] = SCR_BaseEditorAttributeVar.CreateVector("1.5 2 3");
  ApplyBlocks(blocks);
  Check(m_Module.EAC_SessionSettingsBlocked(), "fractional controller setting rejects the full snapshot");
  blocks[1] = SCR_BaseEditorAttributeVar.CreateVector(original);
  m_Module.EAC_BeginSessionSettingsLoad(); ApplyBlocks(blocks);
  m_Module.EAC_ApplySessionSettings(m_Expected, CATALOG, "EAC_fixture_missing_faction");
  Check(m_Module.CivilianFaction == 0, "missing stable faction explicitly falls back to vanilla civilians");
 }
 void Finish()
 {
  PrintFormat("[EAC CDF RESULT] checks=%1 failures=%2", m_Checks, m_Failures);
  ClearEventMask(EntityEvent.FRAME);
  GetGame().RequestClose();
 }
 override void EOnFrame(IEntity owner, float timeSlice)
 {
  float now = GetGame().GetWorld().GetWorldTime() * 0.001;
  if (now < m_Next) return;
  m_Next = now + 1;
  if (!Replication.IsServer()) { Check(false, "authority required"); Finish(); return; }
  if (m_Phase == 0)
  {
   Check(!EAC_AmbientModule.GetActive(), "disposable world starts without controller");
   CDF_GMSaveConfig config = CDF_GMSaveConfig.GetInstance();
   config.m_bAutoSaveEnabled = false; config.m_bSaveOnGameEnd = false; config.m_bAutoLoadOnStart = false;
   config.m_bCaptureOnlyAuthored = true; config.m_bClearBeforeLoad = true; config.m_bSaveAttributes = true; config.m_bUsePersistenceBlob = false;
   m_Module = EAC_AmbientModule.Cast(Spawn("{CA1A000000000010}PrefabsEditable/EXPAC/EAC_AmbientModule.et"));
   m_Zone = EAC_ExclusionZone.Cast(Spawn("{CA1A000000000020}PrefabsEditable/EXPAC/EAC_ExclusionZone.et"));
   Check(m_Module && m_Zone, "production controller and exclusion prefabs spawn");
   if (!m_Module || !m_Zone) { Finish(); return; }
   m_Phase = 1;
   return;
  }
  if (m_Phase == 1)
  {
   Check(EAC_SessionCodecTests.Run(), "pure codec edge cases pass");
   CheckIndoorSavedValues();
   CheckPackedFile();
   SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.GetEditableEntity(m_Module);
   EAC_VoiceIntervalAttribute voice = new EAC_VoiceIntervalAttribute(); voice.m_Key = 23;
   voice.WriteVariable(editable, SCR_BaseEditorAttributeVar.CreateVector("3 0 0"), null, 7);
   Check(m_Module.VoiceInterval == 15, "null manager with a player ID cannot bypass editor authorization");
   voice.WriteVariable(editable, SCR_BaseEditorAttributeVar.CreateVector("3 0 0"), null, -1);
   Check(m_Module.VoiceInterval == 5 && m_Module.SoundsEnabled == 1, "legacy CDF sound preset 3 restores Lively, not three seconds");
   voice.WriteVariable(editable, SCR_BaseEditorAttributeVar.CreateVector("0 0 0"), null, -1);
   Check(m_Module.SoundsEnabled == 0, "legacy sound Off remains disabled");
   EAC_ExclusionRadiusAttribute radius = new EAC_ExclusionRadiusAttribute(); radius.m_Key = 0;
   int initialRadius = m_Zone.RadiusMeters;
   radius.WriteVariable(SCR_EditableEntityComponent.GetEditableEntity(m_Zone), SCR_BaseEditorAttributeVar.CreateVector("333 0 0"), null, 7);
   Check(m_Zone.RadiusMeters == initialRadius, "exclusion null-manager player write is unauthorized");
   EAC_SessionSettingsAttribute hidden = new EAC_SessionSettingsAttribute(); hidden.m_Block = 0;
   hidden.WriteVariable(editable, SCR_BaseEditorAttributeVar.CreateVector("1 0 3"), null, 7);
   Check(!m_Module.EAC_SessionSettingsBlocked(), "hidden snapshot also rejects null-manager player writes");
   PreparePopulation();
   if (!m_Owned || !m_Transferred || !m_Regrouped) { Finish(); return; }
   // Customized dependent values deliberately disagree with Density's preset.
   // Restoring through SetSetting repeatedly or applying density last corrupts them.
   m_Module.Density = 3; m_Module.PopulationLimit = 7; m_Module.LocalPopulationLimit = 6;
   m_Module.SmallHouseResidents = 3; m_Module.LargeHouseResidents = 4; m_Module.ActivityLimit = 11;
   m_Module.WakeDistance = 88; m_Module.SleepDistance = 111;
   m_Module.TrafficDistance = 123; m_Module.TrafficSleepDistance = 129;
   m_Module.MinLocalPopulation = 5; m_Module.VoiceInterval = 17; m_Module.SoundsEnabled = 0;
   m_Module.IdleForceSeconds = 67; m_Module.DebugLevel = 2; m_Module.DebugDraw = 1;
   m_Module.ClearDelay = 8; m_Module.ThemeIndex = 1; m_Module.ActivityInterval = 50; m_Module.CalmDelay = 7;
   m_Module.TrafficLimit = 2; m_Module.TrafficSpeed = 12; m_Module.TrafficPassengers = 1;
   m_Module.TrafficInterval = 19; m_Module.TrafficStuckDelay = 33;
   m_Module.LocalPopulationRadius = 77; m_Module.LoadTargetFps = 45; m_Module.IndoorSpawnDistance = 12;
   m_Module.IdleRecoverySeconds = 13; m_Module.IndoorSpawnShare = 29; m_Module.RoutineRange = 53;
   m_Module.SceneSurvey = 0; m_Module.SceneSurveyRate = 3; m_Module.SceneSitting = 0;
   m_Module.VanillaSeating = 2; m_Module.TableBias = 42; m_Module.VoiceRange = 33;
   m_Module.HomeIsolationRule = 1; m_Module.HomeClusterMin = 2; m_Module.HomeClusterRadius = 60;
   m_Module.SettlementRadius = 333; m_Module.AdmissionsPerTick = 2; m_Module.CatchUpAdmissionsPerTick = 3;
   m_Module.HouseFilter = 1; m_Module.RoutineStops = 3; m_Module.RoutineLegSpacing = 11; m_Module.RoutineGap = 23;
   m_Module.PrewarmMode = 0; m_Module.PrewarmCellsPerTick = 2; m_Module.TrafficRoundTrips = 1;
   m_Module.CarsEnabled = 0; m_Module.CivilianFaction = 0;
   m_Module.ThemeCatalogResource = CATALOG;
   m_Module.NormalizeSettings();
   for (int key = 0; key < 52; key++) m_Expected.Insert(m_Module.GetSetting(key));
   m_Zone.SetSetting(0, 321); m_Zone.SetSetting(1, 0);
   // The flags must describe the final hierarchy, traffic included, before CDF reads them.
   EAC_SessionLifecycle.EAC_TestSync(now);
   TraceState("owned_group_before", m_Owned.Claim.Group, m_Owned.Claim);
   TraceState("owned_actor_before", m_Owned.Claim.Character, m_Owned.Claim);
   TraceState("traffic_group_before", m_Traffic.Group);
   TraceState("traffic_car_before", m_Traffic.Car);
   m_Document = CDF_GMSaveCapture.Capture("EAC exact settings", "disposable fixture");
   Check(m_Document && HasRecord(m_Document, m_Module) && HasRecord(m_Document, m_Zone), "actual authored CDF capture includes controller and exclusion");
   if (!m_Document) { Finish(); return; }
   TraceCapture(m_Document, "owned_actor", m_Owned.Claim.Character, m_Owned.Claim);
   TraceCapture(m_Document, "owned_group", m_Owned.Claim.Group, m_Owned.Claim);
   TraceCapture(m_Document, "foreign_group", m_ForeignGroup);
   TraceCapture(m_Document, "manual_sibling", m_ManualSibling);
   TraceCapture(m_Document, "traffic_car", m_Traffic.Car);
   TraceCapture(m_Document, "traffic_group", m_Traffic.Group);
   Check(!HasRecord(m_Document, m_Owned.Claim.Character) && !HasRecord(m_Document, m_Traffic.Car) && !HasRecord(m_Document, m_Traffic.Group), "actual CDF capture omits exact owned civilian and traffic transients");
   Check(Flagged(m_Traffic.Car) && Flagged(m_Traffic.Group), "owned traffic car and exclusive group carry the save-exclusion flag");
   Check(!HasRecord(m_Document, m_Owned.Claim.Group) && !HasRecord(m_Document, m_WalkOrder), "active exact owned waypoint does not make its exclusive group serializable");
   Check(HasRecord(m_Document, m_ForeignGroup) && HasRecord(m_Document, m_ManualSibling), "foreign authored parent and manual sibling stay in the actual CDF save");
   Check(HasRecord(m_Document, m_Regrouped.Claim.Character), "regrouped former resident follows CDF capture as a manual group member");
   for (int recordIndex = 0; recordIndex < m_Document.m_aEntities.Count(); recordIndex++)
    if (m_Document.m_aEntities[recordIndex].m_Entity && m_Document.m_aEntities[recordIndex].m_Entity.GetOwner() == m_Regrouped.Claim.Character) m_RegroupedRecord = recordIndex;
   Check(!HasRecord(m_Document, m_Manual), "unclaimed unauthored manual civilian follows CDF authored-only policy");
   Check(!Flagged(m_Transferred.Claim.Character), "historically player-used resident is ceded to ordinary CDF serialization");
   // Exercise retirement on an exclusive group; CDF owns deletion of the
   // foreign parent above and Ambient deliberately does not intercept it.
   SCR_EditableEntityComponent.GetEditableEntity(m_Owned.Claim.Group).SetParentEntity(null);
   Check(m_Document.SaveToFile(FILE), "actual CDF document writes a save file");
   m_Document = new CDF_GMSaveDocument();
   Check(m_Document.LoadFromFile(FILE), "actual CDF document reads the written file");
   bool foundFullState;
   foreach (CDF_GMSaveEntityRecord record : m_Document.m_aEntities)
   {
    if (!record.m_sPrefab.Contains("CA1A000000000010")) continue;
    foundFullState = record.m_aAttributeIds.Count() > 13;
    // Simulate an old numeric faction selection after the loaded list reordered.
    // The serialized stable key must win over this stale raw index.
    string factionBlockKey = "EAC_SessionSettingsAttribute#18";
    int factionBlock = record.m_aAttributeIds.Find(factionBlockKey.Hash());
    if (factionBlock >= 0) record.m_aAttributeX[factionBlock] = 15;
   }
   Check(foundFullState, "CDF file contains complete controller state beyond thirteen UI attributes");
   Check(CDF_GMSaveRestore.Restore(m_Document), "actual CDF restore accepts the file");
   m_Phase = 2;
   m_Next = now + 2;
   return;
  }
  if (m_Phase == 4)
  {
   array<IEntity> noObservers = {};
   m_Module.GetSpawner().Step(m_Module, noObservers);
   if (m_Module.GetResidentActivation(m_Transferred.Claim.Home, m_Transferred.Claim.Resident))
   {
    if (++m_CleanupTicks < 10) return;
    Check(false, "protected survivor external deletion eventually releases its orphan group and claim"); Finish(); return;
   }
   Check(!m_Transferred.Claim.Character && !m_Transferred.Claim.Group && m_Module.GetReservedPopulation() == 1, "later external survivor deletion releases old reservation while fresh resident remains reserved");
   Check(m_Transferred.Claim.Resident.Removed && EAC_AmbientModule.GetMissionClaims().GetSessionCompleted() == 3, "retired protected home stays terminal after bounded orphan cleanup");
   CheckMalformedSnapshots();
   Finish(); return;
  }
  if (m_Phase == 3)
  {
   array<IEntity> observers = {};
   m_Module.GetSpawner().Step(m_Module, observers);
   EAC_TrafficDirector.Get().Step(m_Module, observers, now);
   if (m_Module.GetResidentActivation(m_Owned.Claim.Home, m_Owned.Claim.Resident) || m_Module.GetResidentActivation(m_Regrouped.Claim.Home, m_Regrouped.Claim.Resident) || m_Traffic.Reserved)
   {
    if (++m_CleanupTicks < 15) return;
    Check(false, "bounded session cleanup completes"); Finish(); return;
   }
   Check(!m_Module.GetResidentActivation(m_Owned.Claim.Home, m_Owned.Claim.Resident), "same-world load releases the old owned claim only after all entities disappear");
   Check(EAC_AmbientModule.GetMissionClaims().GetSessionCompleted() == 2 && EAC_AmbientModule.GetMissionClaims().GetExclusionCompleted() == 0, "session retirement is distinct from manual exclusion cleanup");
   Check(m_Manual && m_Manual.GetID() == m_ManualId, "unclaimed manual civilian is untouched by Ambient cleanup");
   Check(m_Transferred.Claim.Character && m_Transferred.Claim.Character.GetID() == m_TransferredId && m_Transferred.Claim.Resident.Removed, "protected player-history survivor keeps identity and cannot open a replacement home slot");
   Check(m_Module.GetReservedPopulation() == 1, "only the protected survivor retains its population reservation");
   // Fresh generation starts an independent claim and native entity, never a
   // CDF copy of the old actor. Ownership is tested without placement randomness.
   m_Module.HomeIsolationRule = 0;
   // FRAME is disabled in this fixture, so propagate the policy explicitly.
   m_Module.GetHomeIndex().SetPolicy(m_Module.HomeIsolationRule, m_Module.HomeClusterMin, m_Module.HomeClusterRadius, m_Module.SettlementRadius);
   vector freshPosition = "4953 0 7094";
   freshPosition[1] = GetWorld().GetSurfaceY(freshPosition[0], freshPosition[2]) + 0.2;
   int freshReason = m_Module.GetAdmissionReason(m_Owned.Claim.Home, m_Owned.Claim.Home.Residents[0], freshPosition);
   PrintFormat("[EAC CDF ADMISSION] fresh_reason=%1", freshReason);
   Check(freshReason == EAC_ESpawnReason.NONE, "fresh resident fixture synchronizes its disabled scheduler's home policy");
   EAC_PedestrianActivation fresh = MakeResident(m_Owned.Claim.Home, 0, 180);
   Check(fresh && fresh.Claim != m_Owned.Claim && fresh.Claim.Character, "living household regenerates as a fresh native activation after load");
   Check(!m_Module.BeginResidentActivation(m_Transferred.Claim.Home, m_Transferred.Claim.Resident, GetOrigin()), "ceded resident cannot be regenerated alongside its player-owned survivor");
   Check(m_Regrouped.Claim.Resident.Removed && !m_Module.BeginResidentActivation(m_Regrouped.Claim.Home, m_Regrouped.Claim.Resident, GetOrigin()), "regrouped resident cannot be re-admitted after CDF clears the original actor");
   Check(m_RegroupedRecord >= 0 && m_Document.m_aEntities[m_RegroupedRecord].m_Entity, "CDF independently restores the captured manual group member exactly once");
   SCR_EntityHelper.DeleteEntityAndChildren(m_Transferred.Claim.Character);
   Check(!m_Transferred.Claim.Character && m_Transferred.Claim.Group, "external deletion leaves the protected survivor's original empty group for guarded cleanup");
   m_CleanupTicks = 0; m_Phase = 4;
   return;
  }
  EAC_AmbientModule restored = EAC_AmbientModule.GetActive();
  Check(restored != null, "loaded controller initializes as active owner");
  if (restored)
  {
   for (int restoredKey = 0; restoredKey < 52; restoredKey++)
    Check(restored.GetSetting(restoredKey) == m_Expected[restoredKey], "actual CDF file round trips normalized setting " + restoredKey.ToString());
   Check(restored.ThemeCatalogResource == CATALOG, "nondefault authored theme catalog identity round trips");
   vector restoredCentre = restored.GetOrigin();
   Check(restored.ContainsPopulationPosition(restoredCentre + "333 1000 0") && !EAC_ExclusionZone.IsPopulationAllowed(restoredCentre + "334 0 0"), "actual CDF load restores the effective module population boundary");
  }
  EAC_ExclusionZone restoredZone;
  foreach (CDF_GMSaveEntityRecord loaded : m_Document.m_aEntities)
   if (loaded.m_Entity && EAC_ExclusionZone.Cast(loaded.m_Entity.GetOwner())) restoredZone = EAC_ExclusionZone.Cast(loaded.m_Entity.GetOwner());
  Check(restoredZone && restoredZone.RadiusMeters == 321 && restoredZone.BlockTransit == 0, "actual CDF file restores both exclusion settings");
  if (!restored) { Finish(); return; }
  m_Module = restored; m_Module.ClearEventMask(EntityEvent.FRAME);
  Check(m_Owned.SessionRemoval && m_Transferred.SessionRemoval && m_Traffic.SessionRemoval, "native editor load hook marks prior pedestrians and traffic for guarded retirement");
  Check(!m_Module.EAC_SessionSettingsBlocked(), "complete actual CDF snapshot releases admission hold");
  m_Phase = 3;
 }
}
