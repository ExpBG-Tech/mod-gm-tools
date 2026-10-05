[EntityEditorProps(category: "EXPBG/Ambient", description: "Civilian population within the module radius")]
class EAC_AmbientModuleClass : GenericEntityClass {}

class EAC_AmbientModule : GenericEntity
{
 // One active module schedules bounded indexing and pedestrian admission.
 protected static ref array<EAC_AmbientModule> s_Modules = {};
 protected static ref EAC_HomeIndex s_HomeIndex;
 protected static BaseWorld s_IndexWorld;
 protected static ref EAC_PopulationBudget s_Budget;
 protected static ref EAC_ResidentClaims s_Claims;
 protected static ref EAC_PedestrianSpawner s_Spawner;
 protected static ref EAC_TrafficDirector s_Traffic;
 protected static float s_NextHouseScan;
 // Incremental household reconcile cadence and the two escape hatches that keep
 // it responsive: any settings write resets s_NextReconcile through
 // NormalizeSettings, and a registry that grew is reconciled on the next tick.
 protected static float s_NextReconcile;
 protected static int s_ReconcileHomes;
 protected int m_NextPlayer;
 // Reused every scheduler tick instead of allocated. Two arrays per tick is not a
 // frame problem on its own; they are here because "no per-tick allocations in
 // the hot path" is easier to keep true when the hot path owns no news at all.
 // Neither is retained by any callee: the spawner, the traffic director and the
 // voice step all read them within the call.
 protected ref array<int> m_Players = {};
 protected ref array<IEntity> m_Observers = {};
 static const ResourceName DEFAULT_THEME_CATALOG = "{CA1A000000000030}Configs/EXPAC/Themes.conf";
 protected ref EAC_ThemeCatalog m_ThemeCatalog;
 protected ref EAC_ThemeSelection m_ThemeSelection;
 protected bool m_CatalogAttempted;
 protected int m_PreparedThemeIndex = -1;
 protected float m_NextDebugLog;
 protected bool m_ConfigDirty = true;
 protected int m_DebugPlayers, m_DebugCharacters;
 protected float m_FrameAverage, m_LoadSeconds, m_RecoverySeconds;
 protected bool m_LoadLimited;
 // Log hygiene (readiness plan S1). Static mirror of DebugLevel, so the static
 // diagnostics that never hold a module - EAC_RoutineStats.TraceRejection,
 // EAC_TrafficDirector.RecordFailure, EAC_CivilianVoice.BlockSpeaker - can gate
 // their prints instead of writing to a stock server console forever. Written
 // from NormalizeSettings (which every SetSetting ends with and EOnInit begins
 // with), from SetSetting case 10 directly, and again every scheduler tick in
 // EOnFrame, because a replicated write reaches the member without passing
 // through either. Read-only elsewhere; nothing decides admission from it.
 protected static int s_DebugLevel;
 // The world this addon last reported its shared-resource environment for. The
 // two facts below are configuration, not state: they are read once per world and
 // never again, and nothing reads them back for a decision.
 protected static BaseWorld s_EnvironmentWorld;

 // Everything this addon spawns is CIV, and four installed mods are known to
 // override Configs/Factions/CIV.conf. One name, one place, so a missing or
 // renamed key produces one cause rather than eleven scattered literals and a
 // bare BAD_FACTION counter.
 static const string CIV_FACTION = "CIV";
 // The shared Game Master attribute list this addon appends to. Eleven installed
 // mods write the same resource; one that REPLACES the array instead of appending
 // removes every slider from the GM panel while the module keeps running on its
 // defaults, with nothing at all in the log.
 static const ResourceName ATTRIBUTE_LIST = "{F3D6C6D25642352C}Configs/Editor/AttributeLists/Edit.conf";
 // 13 visible controller entries, 2 exclusion entries and 77 hidden session
 // serialization blocks. The hidden blocks never appear in the GM dialog. The
 // Game Master panel carries only the settings an operator changes in play; the
 // other runtime settings keep their defaults and clamps and stay reachable from
 // the World Editor and SetSetting (owner feedback 2026-09-19: 47 sliders was too
 // many). tools/Test-Repository.ps1 keeps this number honest: it parses the same
 // file and fails when an entry has no script class or a duplicate key.
 static const int OWNED_ATTRIBUTES = 92;
 // The shared Game Master placeable catalog our two modules are appended to.
 // Four other installed mods write the same resource. Unlike the attribute list
 // its array holds plain resource names rather than nested objects, so it is read
 // with Get() into an array<ResourceName> instead of GetObjectArray().
 static const ResourceName SYSTEMS_CATALOG = "{3A9124B8692C3F39}Configs/Editor/PlaceableEntities/Systems/Systems.conf";
 static const int OWNED_CATALOG_MODULES = 2;

 // How many entries of the shared attribute list are ours, or -1 when the
 // resource itself could not be read. One resource read, no per-tick cost.
 static int CountOwnedAttributes()
 {
  Resource resource = Resource.Load(ATTRIBUTE_LIST);
  if (!resource || !resource.IsValid()) return -1;
  BaseResourceObject object = resource.GetResource();
  if (!object) return -1;
  BaseContainer container = object.ToBaseContainer();
  if (!container) return -1;
  BaseContainerList entries = container.GetObjectArray("m_aAttributes");
  if (!entries) return -1;
  int mine;
  int count = entries.Count();
  if (count > 4096) count = 4096;
  for (int i = 0; i < count; i++)
  {
   BaseContainer entry = entries.Get(i);
   if (!entry) continue;
   string className = entry.GetClassName();
   if (className.IndexOf("EAC_") == 0) mine++;
  }
  return mine;
 }

 // How many of the shared placeable catalog's entries are ours, or -1 when the
 // resource itself could not be read. Same one-read-per-world cost as the
 // attribute count above, and the same reason: a mod that replaces the array
 // instead of appending removes our modules from the Game Master's own list,
 // and nothing else in the log would say so.
 static int CountOwnedCatalogModules()
 {
  Resource resource = Resource.Load(SYSTEMS_CATALOG);
  if (!resource || !resource.IsValid()) return -1;
  BaseResourceObject object = resource.GetResource();
  if (!object) return -1;
  BaseContainer container = object.ToBaseContainer();
  if (!container) return -1;
  array<ResourceName> prefabs = {};
  if (!container.Get("m_Prefabs", prefabs)) return -1;
  int mine;
  int count = prefabs.Count();
  if (count > 4096) count = 4096;
  for (int i = 0; i < count; i++)
  {
   string path = prefabs[i];
   if (path.Contains("EXPAC/EAC_")) mine++;
  }
  return mine;
 }

 // One report per world about the two shared resources this addon does not own.
 // A wrong answer here is an admin-visible configuration failure, not a runtime
 // event, so the failing cases print at any DebugLevel and the healthy ones only
 // at DebugLevel >= 1; either way it happens exactly once per world.
 protected void ReportEnvironment()
 {
  if (!Replication.IsServer() || s_EnvironmentWorld == GetWorld()) return;
  s_EnvironmentWorld = GetWorld();
  int present = CountOwnedAttributes();
  int expected = OWNED_ATTRIBUTES;
  int level = DebugLevel;
  if (present != expected)
  {
   // Deliberately unconditional: every Game Master slider this addon ships has
   // disappeared from the panel, the module is running on its compiled defaults,
   // and no other line in the log says so.
   PrintFormat("[EAC settings] attributes=%1/%2 - another mod replaced the shared editor attribute list instead of appending to it; the module is running on its defaults", present, expected);
  }
  else if (level >= 1) PrintFormat("[EAC settings] attributes=%1/%2", present, expected);
  int modules = CountOwnedCatalogModules();
  int expectedModules = OWNED_CATALOG_MODULES;
  if (modules != expectedModules)
  {
   // Unconditional for the same reason as the attribute line: the five EXPAC
   // entries are gone from the Game Master's placeable list, so nobody can place
   // the module at all, and no other line says why.
   PrintFormat("[EAC catalog] modules=%1/%2 - another mod replaced the shared placeable catalog instead of appending to it; the EXPAC modules are not offered in the Game Master list", modules, expectedModules);
  }
  else if (level >= 1) PrintFormat("[EAC catalog] modules=%1/%2", modules, expectedModules);
  // Always printed once per world: which civilian factions this mod list offers.
  PrintFormat("[EAC factions] %1 selected=%2", EAC_CivilianFactions.Describe(), CivilianFaction);
  FactionManager factions = GetGame().GetFactionManager();
  Faction civ;
  if (factions) civ = factions.GetFactionByKey(CIV_FACTION);
  int civLimit = -1;
  ChimeraAIWorld ai = ChimeraAIWorld.Cast(GetGame().GetAIWorld());
  if (ai) civLimit = ai.GetAILimitForFaction(CIV_FACTION);
  string key = CIV_FACTION;
  if (!civ)
  {
   // Also unconditional: without this faction nothing can ever be admitted, and
   // the only other evidence is a BAD_FACTION rejection counter with no cause.
   PrintFormat("[EAC faction] %1 did not resolve (faction_manager=%2); no civilian can be admitted in this world", key, factions != null);
   return;
  }
  if (level >= 1) PrintFormat("[EAC faction] %1 resolved ai_limit=%2", key, civLimit);
 }
 static int GetDebugLevelMirror() { return s_DebugLevel; }
 // Duplicate-module notice (readiness plan S3). Set on both the duplicate and the
 // module that already owns the world, so whichever one a Game Master opens says
 // duplicate=1 rather than reporting a healthy mission.
 protected bool m_Duplicate;
 void MarkDuplicate() { m_Duplicate = true; }
 bool IsDuplicated() { return m_Duplicate; }
 // Terrain safety valve (readiness plan S2). One sticky line per world.
 protected bool m_EmptyIndexReported;

 [Attribute("0", UIWidgets.EditBox, "Diagnostics: 0 off, 1 summary, 2 counters, 3 index detail. Does not enable radius guides or markers.", "0 3 1", category: "Diagnostics"), RplProp()]
 int DebugLevel;
 [Attribute("0", UIWidgets.EditBox, "Opt in to radius guides and markers while Game Master is open; independent of logging", "0 1 1", category: "Diagnostics"), RplProp()]
 int DebugDraw;

 [Attribute("{CA1A000000000030}Configs/EXPAC/Themes.conf", UIWidgets.ResourceNamePicker, "Authored civilian theme catalog", "conf", category: "Population")]
 ResourceName ThemeCatalogResource;
 [Attribute("0", UIWidgets.EditBox, "Theme index: 0 mixed, 1 workers, 2 urban in default catalog", "0 31 1", category: "Population"), RplProp()]
 int ThemeIndex;

 [Attribute("24", UIWidgets.EditBox, "Global active civilian limit, including vehicle occupants", "0 200 1", category: "Population"), RplProp()]
 int PopulationLimit;
 [Attribute("1", UIWidgets.EditBox, "Residents assigned per small house", "0 20 1", category: "Population"), RplProp()]
 int SmallHouseResidents;
 [Attribute("2", UIWidgets.EditBox, "Residents assigned per large house", "0 20 1", category: "Population"), RplProp()]
 int LargeHouseResidents;
 [Attribute("700", UIWidgets.EditBox, "Prepare civilian households within this distance before arrival (metres)", "50 5000 1", category: "Caching"), RplProp()]
 int WakeDistance;
 [Attribute("900", UIWidgets.EditBox, "Distance from each live civilian to the nearest player before despawning (metres)", "51 6000 1", category: "Caching"), RplProp()]
 int SleepDistance;
 [Attribute("100", UIWidgets.EditBox, "Indoor civilians appear only within this player distance (metres); 0 disables indoor slots", "0 300 1", category: "Caching"), RplProp()]
 int IndoorSpawnDistance;
 // FIXED VALUES (owner decision 2026-09-19: "no need to make it configurable").
 // Every member below that is declared with `= value` and no [Attribute] used to be
 // a World Editor field and, before that, a Game Master slider. They are internal
 // tuning: each was set once from a measured campaign and nobody should have to
 // touch them to run a town. They keep their place in the GetSetting/SetSetting key
 // table and their clamp, because the native fixtures and the integrated campaign
 // pin several of them; nothing else can change them.
 int ClearDelay = 30;
 // Indoors hides a placement behind walls, which is what allows populating close
 // to a player at all; outdoors costs no emergence and puts life in the street
 // immediately. Mixing the two stops a town being either empty on approach or a
 // queue of people all walking out of doors at once.
 int IndoorSpawnShare = 60;

 // Scene survey. Routine positions are currently guessed with traces at the
 // moment a resident wants one, which wastes most of the work and leaves rare
 // anchor kinds nearly unreachable. These configure a per-home survey that finds
 // and verifies positions once, ahead of demand. Zero disables it and restores
 // exactly the present behaviour, which is what makes an A/B measurement possible.
 int RoutineRange = 60;
 int SceneSurvey = 1;
 int SceneSurveyRate = 4;
 // Reserved legacy key; existing-furniture sitting uses the activity and scene
 // survey controls. Retained for mission attribute compatibility.
 int SceneSitting = 0;
 // How often a resident that could sit at a table does. Nine of the last
 // campaigns drew table_wanted between 0 and 8 from plain step-modulo selection,
 // and five drew 0 inside the routine window, so nothing downstream of selection
 // could be measured. 0 keeps step-modulo selection exactly as it was.
 int TableBias = 35;
 // Campaign 45: 14 voice intervals fired, 0 issued, and 161 of 164 candidate
 // rejections were 'far' - no player within 20 m of any resident at the tick.
 // The cough was never silent; it was never asked for. 20 m on a 45 s global
 // timer is almost never satisfied by a moving player.
 // 20 m is how far the shipped sounds carry on a client (measured, probe 04), so a
 // sound is only ever triggered for a player who can hear it.
 int VoiceRange = 20;

 // A routine is a chain of stops, not one pose. 1 restores the single-occupation
 // behaviour measured in run 69 exactly, which is what makes an A/B comparison
 // possible and is the first thing to try if anything regresses.
 int RoutineStops = 4;
 // Stop 0 always keeps the historic 8 m rule. Raising this makes travel between
 // stops more visible; too high in a dense village starves chains to two stops.
 int RoutineLegSpacing = 15;
 // Multiplied by RandomFloat(1, 1.5), so 30 gives 30-45 s of ordinary wandering
 // between routines. This is the rest EVERY routine that produced something pays
 // - one entered occupation is enough, and so is a built point or an acquired
 // station. ActivityInterval is charged only by a routine that produced no supply
 // at all, and even that is clamped to max(RoutineGap, 20) s, because a 120 s
 // interval times the 1.5 multiplier is 180 s of standing still for a geometry
 // failure the resident did not cause (EAC_CivilianActivity.FinishRoutine, audit
 // item 1). A start that created nothing never reaches that rest: TryActivity
 // charges its own 20 s retry instead.
 int RoutineGap = 30;

 // All-vanilla indoor occupation. The seat used to be a vehicle compartment from
 // an optional third-party addon; this drives the native AI animation classes
 // instead, so no dependency and no compartment are involved. The legacy value
 // zero was removed in phase 2 once campaigns 46 and 47 measured the native route
 // posing every seat it issued. The key is kept at 30 so saved GM settings for
 // every other key do not shift.
 int VanillaSeating = 1;

 // A routine now holds its allowance for two to four occupations instead of one.
 // Run 65 tracked about 23 active residents; at a cap of 4 at least 19 are
 // structurally idle at any instant regardless of the state machine, so the chain
 // would be invisible to most of the town. CanAdmitNewWork()/SampleLoad remain the
 // automatic brake, and an owner can dial the value back live at any time.
 // Raised 8 -> 12: campaign 73 held all 8 slots for 3.6 minutes continuously with
 // 12 active residents, six of those slots taken by 180-360 s seat stops, so at
 // least one resident was guaranteed no routine for the whole phase.
 // Clamp raised 16 -> 64 on city evidence: benchmark qa-city-benchmark-02
 // (PopulationLimit 96, 94 active residents, 61 homes, 215 residents) failed both
 // idle gates because 78 of the 94 could not hold a routine at all - the slot
 // count, not the state machine, was the ceiling. A routine is now a resident's
 // whole chain, so the rule of thumb is ActivityLimit >= expected active
 // residents. The ceiling moved first; the DEFAULT now follows it.
 // Authored default raised 16 -> 24 (audit item 4). PopulationLimit ships at 24,
 // so a default of 16 meant eight admitted residents could never hold a routine
 // at any instant - not because of geometry, anchors or the state machine, but
 // because the slot count was eight short of the population the same prefab
 // authorises. Whatever else refuses a routine should be measurable; a
 // structurally unreachable slot is not. The rule the presets follow is
 // ActivityLimit >= PopulationLimit, up to the clamp.
 // Clamp raised 64 -> 256 on city benchmark 06: PopulationLimit 96 with 94 active
 // residents pinned the activity count at the 64 ceiling for the whole run
 // (activitySaturation 9 %), so about thirty residents could never hold a routine
 // and fell through to the wander - which the terrain-height rule was refusing at
 // the same time. All three idle gates failed there. 256 keeps the rule of thumb
 // usable at every authored population (the PopulationLimit slider itself stops
 // at 200) and costs nothing on its own: a slot is only ever occupied by a
 // routine that passed every other gate.
 [Attribute("24", UIWidgets.EditBox, "Maximum concurrent civilian activities; zero disables them", "0 256 1", category: "Activities"), RplProp()]
 int ActivityLimit;
 int ActivityInterval = 120;

 [Attribute("30", UIWidgets.EditBox, "Continuously calm seconds before routine activities and caching resume", "5 120 1", category: "Activities"), RplProp()]
 int CalmDelay;

 // Civilian driving is EXPERIMENTAL and off by default (owner decision 2026-09-19):
 // the native AI driver sounds its horn at anything in its path. CarsEnabled is the
 // Game Master's yes/no switch; TrafficLimit is how many cars once it is on. The
 // director reads GetCarLimit(), which is zero while the switch is off.
 [Attribute("0", UIWidgets.CheckBox, "EXPERIMENTAL: civilian cars", category: "Ambient"), RplProp()]
 int CarsEnabled;
 [Attribute("3", UIWidgets.EditBox, "Reserved traffic parties at once; surviving crews retain their slot after a car is removed", "0 16 1", category: "Ambient"), RplProp()]
 int TrafficLimit;
 int GetCarLimit()
 {
  if (CarsEnabled <= 0) return 0;
  return TrafficLimit;
 }

 // Small human sounds from walking or stationary civilians near a player.
 [Attribute("0", UIWidgets.CheckBox, "Civilian sounds", category: "Ambient"), RplProp()]
 int SoundsEnabled;

 // Which faction the residents look like: 0 the shipped vanilla themes, 1.. the
 // civilian factions other loaded mods provide (EAC_CivilianFactions).
 [Attribute("0", UIWidgets.EditBox, "Civilian faction: 0 vanilla themes, 1 and up the civilian factions of other loaded mods, in key order", "0 15 1", category: "Population"), RplProp()]
 int CivilianFaction;
 protected int m_PreparedFaction = -1;

 // "How busy": one choice instead of five sliders. Writing it sets the global and
 // local civilian limits, the residents per house and the concurrent activities
 // together (ApplyDensity); the individual values stay World Editor fields for a
 // mission maker who wants something in between.
 [Attribute("1", UIWidgets.ComboBox, "How busy: sets population limits, residents per house and activities together", enums: { ParamEnum("Quiet", "0"), ParamEnum("Normal", "1"), ParamEnum("Busy", "2"), ParamEnum("Crowded", "3") }, category: "Population"), RplProp()]
 int Density;
 void ApplyDensity()
 {
  if (Density <= 0) { PopulationLimit = 16; LocalPopulationLimit = 8; SmallHouseResidents = 1; LargeHouseResidents = 1; ActivityLimit = 16; }
  else if (Density == 1) { PopulationLimit = 24; LocalPopulationLimit = 12; SmallHouseResidents = 1; LargeHouseResidents = 2; ActivityLimit = 24; }
  else if (Density == 2) { PopulationLimit = 48; LocalPopulationLimit = 18; SmallHouseResidents = 2; LargeHouseResidents = 3; ActivityLimit = 48; }
  else { PopulationLimit = 96; LocalPopulationLimit = 28; SmallHouseResidents = 2; LargeHouseResidents = 5; ActivityLimit = 96; }
 }
 // Yes/no switches from the Game Master panel.
 void SetSwitch(int key, bool on)
 {
  int value = 0;
  if (on) value = 1;
  SetSetting(key, value);
 }

 [Attribute("20", UIWidgets.EditBox, "Civilian driving speed (km/h)", "10 40 1", category: "Ambient"), RplProp()]
 int TrafficSpeed;

 int TrafficDistance = 700;

 // One passenger is the hard ceiling, not a default. A civilian car carries a
 // driver and at most one passenger; the range itself enforces it, so the GM
 // editor cannot exceed it either.
 [Attribute("0", UIWidgets.EditBox, "Passengers per civilian car (maximum one; default is the driver alone)", "0 1 1", category: "Ambient"), RplProp()]
 int TrafficPassengers;

 int TrafficInterval = 60;

 int TrafficStuckDelay = 90;

 int TrafficSleepDistance = 1000;

 [Attribute("12", UIWidgets.EditBox, "Local civilian limit", "0 200 1", category: "Ambient"), RplProp()]
 int LocalPopulationLimit;

 int LocalPopulationRadius = 250;

 int LoadTargetFps = 20;

 // Bundled non-verbal clips; walking and stationary residents share one clock.
 [Attribute("15", UIWidgets.EditBox, "Shared sound interval in seconds; zero disables. Each resident pauses 6-16 times this value. Changing it retimes pending pauses; the same sound category waits at least 10 seconds.", "0 600 1", category: "Ambient"), RplProp()]
 int VoiceInterval;

 // A committed resident must never stand still indefinitely. This is a watchdog
 // over the existing fallbacks, not a relaxation of any placement gate.
 int IdleRecoverySeconds = 45;

 // The hard guarantee behind "no civilian stands around for longer than two
 // minutes". IdleRecoverySeconds above re-opens the ordinary fallbacks and then
 // waits for the maintenance rotation to reach the resident; this is the deadline
 // at which the scheduler issues the movement itself, on the tick it is crossed,
 // bypassing the routine cooldown and the walk backoff (EAC_PedestrianSpawner
 // ForceIdle/ForceOne). It bypasses no placement, exclusion, ownership, budget or
 // visibility gate. There is deliberately no zero: the clamp floor is 30, because
 // a disabled deadline is the defect the 2026-09-17 audit was written about.
 // 75 leaves a 45-second margin under the 120-second QA gate for the rotation to
 // reach every idle resident even at the city benchmark's 94.
 // 45 since 0.0.7 (owner feedback 2026-09-19: residents should keep moving); 75 before.
 int IdleForceSeconds = 45;

 // 0 (default): the parked-car model the owner asked for on 2026-09-19 - cars wait
 // parked at the kerb, a driver walks over from a house, drives out of town and the
 // whole party is removed once it is far away and out of sight. 1 keeps live
 // parties driving round trips; either model discards a fully despawned party.
 [Attribute("0", UIWidgets.EditBox, "EXPERIMENTAL traffic model: 0 parked cars and one-way trips, 1 live round trips", "0 1 1", category: "Ambient"), RplProp()]
 int TrafficRoundTrips;

 // Mission-start prewarm. 0 is today's behaviour exactly: houses are discovered
 // one 64 m cell per half-second around each player and nowhere else. 1 also
 // walks the terrain author's own settlement labels ahead of any player, so a
 // town is already indexed when somebody arrives. 2 is reserved for widening the
 // scene survey's reach and currently behaves exactly as 1; the survey's two
 // engine preconditions (a borrowed pathfinder and a loaded navmesh tile) have
 // not been proven reachable ahead of a player, and surveying unloaded ground
 // marks records barren for fifteen minutes.
 // Zero must stay the shipped default: any other value changes enrolment and
 // invalidates the recorded run 64/65 baselines.
 [Attribute("0", UIWidgets.EditBox, "Mission-start indexing: 0 off (today), 1 index settlements ahead of players, 2 reserved (currently identical to 1)", "0 2 1", category: "Population"), RplProp()]
 int PrewarmMode;
 // Cells indexed per half-second scheduler tick while no player is near any
 // settlement. Drops to one automatically when somebody is, so arrival always
 // outranks background work.
 int PrewarmCellsPerTick = 8;
 protected bool m_PrewarmReported;
 protected int m_PrewarmPeakTickMs;

 // Isolated houses (readiness plan section 3, keys 33-36). Zero is mandatory as
 // the shipped default: any other value changes enrolment and invalidates the
 // recorded run 64/65 baselines. A lone farmhouse in open country is populated at
 // rule 0, exactly as it is today.
 [Attribute("0", UIWidgets.EditBox, "Isolated houses: 0 enrol every supported house, 1 require neighbouring homes, 2 require a settlement (city limits), 3 require both", "0 3 1", category: "Population"), RplProp()]
 int HomeIsolationRule;
 int HomeClusterMin = 2;
 // 256 is 4 x EAC_HomeIndex.CELL_SIZE, the honest ceiling of the bounded block
 // scan. Raising it requires raising EAC_HomeIndex.MAX_NEIGHBOUR_CELL_SPAN in the
 // same change or the extra metres are silently unsearched.
 int HomeClusterRadius = 140;
 // Retain key 36/property identity for existing editor/CDF settings.
 [Attribute("400", UIWidgets.EditBox, "Civilian population radius around this module (m); also the town-membership distance for house rules 2 and 3", "100 1500 1", category: "Population"), RplProp()]
 int SettlementRadius;

 // Population floor (readiness plan section 4.3, keys 37-39). A rate, never a
 // bypass: nothing below relaxes CanAdmitNewWork, PopulationLimit, ValidPosition
 // or GetHiddenReason. Zero and one keep today's behaviour exactly.
 int MinLocalPopulation = 0;
 int AdmissionsPerTick = 1;
 int CatchUpAdmissionsPerTick = 1;

 // The default covers inspected vanilla/Arma Terrain Core homes and their intact
 // inherited variants. The optional folder fallback can also admit loose parts.
 [Attribute("0", UIWidgets.EditBox, "House recognition: 0 inspected homes, inherited variants and whole buildings with residential names, 1 also any prefab under prefabs/structures/houses/ (unvalidated)", "0 1 1", category: "Population"), RplProp()]
 int HouseFilter;

 override void EOnInit(IEntity owner)
 {
  super.EOnInit(owner);
  if (!GetGame().InPlayMode() || !Replication.IsServer()) return;
  NormalizeSettings();
  s_Modules.Insert(this);
  // Duplicate-module notice (readiness plan S3). GetActive returns the first
  // module placed in this world, so a second one silently admits nothing: its
  // budget calls are refused by the GetActive() == this guard on every entry
  // point. That refusal used to be invisible. One line at any DebugLevel, once
  // per surplus module, plus a member both modules carry into the GM snapshot.
  EAC_AmbientModule admitting = GetActive();
  if (admitting != this)
  {
   m_Duplicate = true;
   if (admitting) admitting.MarkDuplicate();
   Print("[EAC] a second Ambient Civilians module is placed; only the first placed module admits residents");
  }
  // Outside the world block below on purpose: the ledger is a static and must
  // notice a world change even on a module that arrives after the one that
  // rebuilt the index, which is the failure mode statics-per-world is about.
  EAC_SchedulerStats.CheckWorld();
  // Late GM placement can prepare immediately; startup placement waits for
  // terrain readiness on the shared tick. No per-resident query can trigger it.
  EAC_AutoExclusions.Prepare();
  if (!s_HomeIndex || s_IndexWorld != GetWorld())
  {
   s_HomeIndex = new EAC_HomeIndex();
   s_Budget = new EAC_PopulationBudget();
   s_Claims = new EAC_ResidentClaims(s_HomeIndex.GetRegistry(), s_Budget, GetWorld());
   s_Spawner = new EAC_PedestrianSpawner(GetWorld());
   s_Traffic = new EAC_TrafficDirector(GetWorld());
   s_IndexWorld = GetWorld();
   s_NextHouseScan = 0;
   s_NextReconcile = 0;
   s_ReconcileHomes = 0;
   EAC_SceneIndex.CheckWorld();
  }
  Replication.BumpMe();
 }

 void EAC_AmbientModule(IEntitySource src, IEntity parent)
 {
  SetEventMask(EntityEvent.INIT | EntityEvent.FRAME | EntityEvent.POSTFRAME);
 }

 void ~EAC_AmbientModule()
 {
  s_Modules.RemoveItem(this);
  if (Replication.IsServer() && s_IndexWorld == GetWorld())
  {
   EAC_AmbientModule replacement = GetActive();
   if (DebugLevel > 0) PrintFormat("[EAC controller] removed position=%1 reserved=%2 replacement=%3", GetOrigin(), GetReservedPopulation(), replacement != null);
   if (replacement) replacement.m_ConfigDirty = true;
  }
  if (s_Spawner && s_IndexWorld == GetWorld() && Replication.IsServer() && !GetActive()) s_Spawner.StopOwnedWalks();
  if (s_IndexWorld == GetWorld() && Replication.IsServer() && !GetActive()) EAC_TrafficDirector.Drain(GetWorld().GetWorldTime() * 0.001);
 }

 EAC_HomeIndex GetHomeIndex()
 {
  if (!Replication.IsServer() || s_IndexWorld != GetWorld()) return null;
  return s_HomeIndex;
 }

 EAC_PedestrianSpawner GetSpawner()
 {
  if (!Replication.IsServer() || s_IndexWorld != GetWorld()) return null;
  return s_Spawner;
 }

 // Ownership queries must survive intervals without a placed module.
 static EAC_ResidentClaims GetMissionClaims()
 {
  if (!Replication.IsServer() || !GetGame() || s_IndexWorld != GetGame().GetWorld()) return null;
  return s_Claims;
 }

 // The native AI driver raises its horn input during the frame, for anything in
 // its path, and holds it for as long as the obstacle is there (owner report
 // 2026-09-19: cars driving with the horn held down). EOnFrame clears the input,
 // but whichever of the two runs second wins that frame; clearing again after the
 // frame removes the ordering from the question. Same bounded loop, no allocation.
 override void EOnPostFrame(IEntity owner, float timeSlice)
 {
  if (GetActive() != this || !s_Traffic) return;
  s_Traffic.UpdateHorn();
 }

 override void EOnFrame(IEntity owner, float timeSlice)
 {
  if (GetActive() != this || !s_HomeIndex) return;
  // Re-apply the static DebugLevel mirror every tick. NormalizeSettings and
  // SetSetting case 10 both write it, but a replicated or World Editor write
  // lands on the member directly, and a diagnostic that reads a stale zero is
  // exactly the silent failure S1 is about. One integer store per frame.
  s_DebugLevel = DebugLevel;
  SampleLoad(timeSlice);
  // The only work this handler does on an ordinary frame. It cannot move to the
  // 0.5 s tick: it reads the driver's live horn input, and a horn sampled twice a
  // second is a horn that misses most presses and holds the rest for half a
  // second after release. Bounded by EAC_TrafficDirector.MAX_PARTIES (64) and by
  // TrafficLimit (16 at the clamp) before that. A party is skipped outright
  // unless it holds a car and its journey is between boarding and dismount, and a
  // polled party's UpdateHorn is then an input read and a compare against
  // components resolved when the car and crew were bound - it used to resolve
  // three of them through FindComponent on every party on every frame, which is
  // what this comment previously denied (audit item 7). Measured as the `horn`
  // term of `sched_steps`, and `horn_polled_parties` in the traffic summary says
  // how many records the loop actually touched, so the claim stays checked.
  if (s_Traffic)
  {
   int hornTick = System.GetTickCount();
   s_Traffic.UpdateHorn();
   EAC_SchedulerStats.Record(EAC_SchedulerStats.STEP_HORN, hornTick);
  }
  float now = GetWorld().GetWorldTime() * 0.001;
  if (now < s_NextHouseScan) return;
  s_NextHouseScan = now + 0.5;
  // Editor/CDF attributes restore synchronously before this shared tick.
  EAC_FinishSessionSettingsLoad();
  // GM-save exclusion flags follow ownership at 1 Hz (self-throttled; one pass
  // over the ledgers). See EAC_SessionLifecycle.
  EAC_SessionLifecycle.Sync(now);
  // Everything below runs at 2 Hz. STEP_FRAME measures the whole of it.
  int tickStart = System.GetTickCount();
  EAC_SchedulerStats.BeginTick();
  // First scheduler tick of this world, not EOnInit: the faction manager and the
  // resource system are both up by the time a tick runs, and a report that reads
  // them too early would report a false failure. One early return thereafter.
  ReportEnvironment();
  int indexTick = System.GetTickCount();
  EAC_AutoExclusions.Prepare();
  bool themeReady = PrepareTheme();
  if (themeReady) s_HomeIndex.GetRegistry().SetCharacterPool(m_ThemeSelection.GetCharacters());
  // Isolation policy before Reconcile, so a rule change is applied on the very
  // next occupancy pass rather than one tick late. SetPolicy is a no-op unless a
  // value actually moved, and rules 2 and 3 need the settlement list: Prepare
  // self-limits to four attempts, so this is a single early return thereafter.
  s_HomeIndex.SetPolicy(HomeIsolationRule, HomeClusterMin, HomeClusterRadius, SettlementRadius);
  // House recognition is consulted from a static classifier, so it travels the
  // same way the isolation policy does: pushed from the module each tick rather
  // than read back out of it. At 0 the classifier behaves exactly as before.
  EAC_HomeIndex.SetHouseFilter(HouseFilter);
  s_HomeIndex.SetPopulationArea(GetOrigin(), SettlementRadius);
  if (HomeIsolationRule >= 2) EAC_SettlementIndex.Prepare();
  // The incremental reconcile re-sweeps the whole registry forever at eight
  // households a call. At 2 Hz that is sixteen records a second which, on a
  // settled registry, change nothing: every household is already at its authored
  // occupancy and every isolation verdict is already cached. It now runs at 1 Hz,
  // and immediately when either input that can invalidate it moved - any settings
  // write (NormalizeSettings resets the gate) or a registry that grew this tick.
  // The cost of the slower cadence is the time a live occupancy change takes to
  // reach the far end of a large registry: homes/8 seconds, 150 s at the 1200-home
  // ceiling, against 75 s before.
  int reconcileHomes = s_HomeIndex.GetRegistry().GetHomeCount();
  if (now >= s_NextReconcile || reconcileHomes != s_ReconcileHomes)
  {
   s_NextReconcile = now + 1;
   s_ReconcileHomes = reconcileHomes;
   s_HomeIndex.Reconcile(SmallHouseResidents, LargeHouseResidents, themeReady);
  }
  // Mission-start prewarm, deliberately before the PlayerManager fetch below so
  // it runs with zero players connected - which is the entire point. It is
  // additive work inside the existing 2 Hz gate: no new event mask, no new
  // callqueue entry, no new timer, and no world scan beyond the same bounded
  // QueryCell that player-proximity discovery already issues.
  //
  // themeReady is load-bearing, not decoration. Enrolling a map of households
  // before the character catalog resolves would permanently assign
  // EAC_HomeIndex.DEFAULT_CHARACTER to every resident on the map, with no
  // recovery path, because SetCharacterPool only affects new slots.
  if (PrewarmMode > 0 && themeReady && CanAdmitNewWork() && !s_HomeIndex.IsPrewarmComplete())
  {
   // Prepare self-limits to four attempts, so this is a single early return
   // thereafter. Wait those attempts out before stepping: the map instance is not
   // always resolvable on the first tick (802 recorded evidence lines show
   // towns=0 early), and latching prewarm complete against a list that had not
   // finished resolving would silently disable the feature for the whole mission.
   EAC_SettlementIndex.Prepare();
   bool settlementsSettled = EAC_SettlementIndex.HasData() || EAC_SettlementIndex.GetAttemptCount() >= EAC_SettlementIndex.MAX_ATTEMPTS;
   if (settlementsSettled)
   {
    s_HomeIndex.SetPrewarmPolicy(PrewarmMode, PrewarmCellsPerTick);
    int prewarmBudget = PrewarmCellsPerTick;
    if (ObserverNearSettlement()) prewarmBudget = 1;
    int prewarmStart = System.GetTickCount();
    s_HomeIndex.StepPrewarm(prewarmBudget, SmallHouseResidents, LargeHouseResidents);
    int prewarmSpent = System.GetTickCount() - prewarmStart;
    if (prewarmSpent > m_PrewarmPeakTickMs) m_PrewarmPeakTickMs = prewarmSpent;
    EAC_SchedulerStats.Record(EAC_SchedulerStats.STEP_PREWARM, prewarmStart);
    ReportPrewarm();
   }
  }
  PlayerManager manager = GetGame().GetPlayerManager();
  if (!manager)
  {
   m_DebugPlayers = 0; m_DebugCharacters = 0;
   s_Spawner.GetDiagnostics().Record(EAC_ESpawnReason.NO_CONTROLLED_CHARACTERS);
   // Order safety still runs with no player manager. m_Observers is empty here,
   // which is what it means: nobody is watching, so nothing is near, and the
   // sweep falls to its 0.5 Hz half on its own.
   m_Observers.Clear();
   s_Spawner.MonitorOrders(this, m_Observers);
   EAC_SchedulerStats.Record(EAC_SchedulerStats.STEP_INDEX, indexTick);
   EAC_SchedulerStats.Record(EAC_SchedulerStats.STEP_FRAME, tickStart);
   UpdateDebugLog(now); return;
  }
  array<int> players = m_Players;
  players.Clear();
  manager.GetPlayers(players);
  m_DebugPlayers = players.Count(); m_DebugCharacters = 0;
  array<IEntity> observers = m_Observers;
  observers.Clear();
  foreach (int id : players)
  {
   IEntity observedCharacter = manager.GetPlayerControlledEntity(id);
   if (ChimeraCharacter.Cast(observedCharacter)) m_DebugCharacters++;
   // Preserve unknown/GM/spectator slots so destructive admission fails closed.
   observers.Insert(observedCharacter);
  }
  // Index only the controller's area. Players activate residents inside it;
  // travelling to another town never relocates the population boundary.
  s_HomeIndex.RetainPlayers(players);
  bool discovered;
  if (themeReady && !players.IsEmpty() && CanAdmitNewWork())
  {
   m_NextPlayer = m_NextPlayer % players.Count();
   int playerId = players[m_NextPlayer++];
   IEntity character = manager.GetPlayerControlledEntity(playerId);
   if (ChimeraCharacter.Cast(character))
   {
    if (vector.DistanceXZ(character.GetOrigin(), GetOrigin()) <= SettlementRadius + WakeDistance)
     discovered = s_HomeIndex.DiscoverNearPlayer(playerId, GetOrigin(), SettlementRadius, SmallHouseResidents, LargeHouseResidents);
   }
  }
  EAC_SchedulerStats.Record(EAC_SchedulerStats.STEP_INDEX, indexTick);
  // House discovery keeps absolute priority; the survey consumes only the ticks
  // it left idle, under the same theme and load guards.
  if (!discovered && themeReady && !players.IsEmpty() && CanAdmitNewWork())
  {
   int sceneTick = System.GetTickCount();
   EAC_SceneIndex.Step(this, observers, now);
   EAC_SchedulerStats.Record(EAC_SchedulerStats.STEP_SCENE, sceneTick);
  }
  // Terrain safety valve (S2). discovered == false on a tick that was allowed to
  // discover means the ring around that player holds no un-indexed cell left -
  // the first full pass. A registry still empty at that point is a terrain whose
  // houses this build does not recognise, which is otherwise indistinguishable
  // from "nobody has walked into a town yet".
  if (themeReady && m_DebugCharacters > 0 && CanAdmitNewWork()) ReportEmptyIndex(discovered);
  // The spawner instruments its own sub-steps (monitor, service, maintain,
  // voice, pending); nothing here wraps it a second time.
  s_Spawner.Step(this, observers);
  if (s_Traffic)
  {
   int trafficTick = System.GetTickCount();
   s_Traffic.Step(this, observers, now);
   EAC_SchedulerStats.Record(EAC_SchedulerStats.STEP_TRAFFIC, trafficTick);
  }
  int admitTick = System.GetTickCount();
  s_Spawner.SelectNext(this, observers);
  EAC_SchedulerStats.Record(EAC_SchedulerStats.STEP_ADMIT, admitTick);
  if (!themeReady) s_Spawner.GetDiagnostics().Record(EAC_ESpawnReason.THEME_PENDING);
  // Recorded before UpdateDebugLog deliberately: step_ms is the cost of the
  // scheduler, and the debug summary built below exists only at DebugLevel above
  // zero, which no production server runs. DescribeSteps names every sub-step
  // that IS inside the figure.
  EAC_SchedulerStats.Record(EAC_SchedulerStats.STEP_FRAME, tickStart);
  UpdateDebugLog(now);
 }

 // Frame-time smoothing admits less work under sustained load, never deleting AI.
 void SampleLoad(float frameSeconds)
 {
  if (frameSeconds <= 0) return;
  if (LoadTargetFps <= 0) { m_LoadLimited = false; m_LoadSeconds = 0; m_RecoverySeconds = 0; return; }
  float elapsed = Math.Min(frameSeconds, 1);
  float weight = Math.Min(elapsed / 3, 1);
  if (m_FrameAverage <= 0) m_FrameAverage = frameSeconds;
  else m_FrameAverage += (frameSeconds - m_FrameAverage) * weight;
  if (m_FrameAverage > 1.0 / LoadTargetFps)
  {
   m_LoadSeconds += elapsed; m_RecoverySeconds = 0;
   if (m_LoadSeconds >= 5) m_LoadLimited = true;
  }
  else if (m_FrameAverage < 0.9 / LoadTargetFps)
  {
   m_RecoverySeconds += elapsed; m_LoadSeconds = 0;
   if (m_RecoverySeconds >= 10) m_LoadLimited = false;
  }
 }

 bool CanAdmitNewWork() { return !EAC_SessionSettingsBlocked() && EAC_AutoExclusions.IsReady() && (!m_LoadLimited || LoadTargetFps == 0); }

 // Is anybody close enough to a settlement that background indexing should get
 // out of the way? Deliberately cheap: at most observers x 256 squared XZ
 // compares against retained centres, no world query, early return. The rejected
 // alternative - testing against every registered home - is observers x 1200 and
 // grows with the very thing prewarm creates.
 protected bool ObserverNearSettlement()
 {
  int count = EAC_SettlementIndex.GetCount();
  if (count == 0) return false;
  PlayerManager manager = GetGame().GetPlayerManager();
  if (!manager) return false;
  array<int> nearby = {};
  manager.GetPlayers(nearby);
  if (nearby.IsEmpty()) return false;
  // Fail towards throttling: an implausible player count means slow down, not
  // speed up.
  if (nearby.Count() > 64) return true;
  float reach = WakeDistance + 960;
  float limit = reach * reach;
  foreach (int id : nearby)
  {
   IEntity observer = manager.GetPlayerControlledEntity(id);
   if (!observer) continue;
   vector position = observer.GetOrigin();
   for (int i = 0; i < count; i++)
   {
    vector centre = EAC_SettlementIndex.GetCentreAt(i);
    float dx = position[0] - centre[0];
    float dz = position[2] - centre[2];
    if (dx * dx + dz * dz <= limit) return true;
   }
  }
  return false;
 }

 // One line, once per world, at the moment prewarm latches complete. One
 // PrintFormat and no long '+' chain; the two composite terms are built by
 // separate statements above it. queries/records/spots are already carried by the
 // existing DebugLevel >= 3 index line and EAC_SceneIndex.Describe().
 protected void ReportPrewarm()
 {
  if (m_PrewarmReported || DebugLevel < 1 || !s_HomeIndex.IsPrewarmComplete()) return;
  m_PrewarmReported = true;
  EAC_HouseholdRegistry registry = s_HomeIndex.GetRegistry();
  string settlements = s_HomeIndex.GetPrewarmSettlementsDone().ToString();
  settlements += "/";
  settlements += s_HomeIndex.GetPrewarmSettlementCount().ToString();
  string cells = s_HomeIndex.GetPrewarmDone().ToString();
  cells += "/";
  cells += s_HomeIndex.GetPrewarmQueued().ToString();
  int duplicates = s_HomeIndex.GetPrewarmDuplicates();
  float seconds = s_HomeIndex.GetPrewarmSeconds();
  int peak = m_PrewarmPeakTickMs;
  int homes = registry.GetHomeCount();
  int residents = registry.GetResidentCount();
  int rejected = s_HomeIndex.GetRejectedHomeCount();
  int mode = PrewarmMode;
  PrintFormat("[EAC prewarm] mode=%1 settlements=%2 cells=%3 duplicate=%4 seconds=%5 peak_tick_ms=%6 homes=%7 residents=%8 rejected_homes=%9", mode, settlements, cells, duplicates, seconds, peak, homes, residents, rejected);
 }

 // Terrain safety valve (readiness plan S2). One sticky line per world, at any
 // DebugLevel, because a server that indexes a whole neighbourhood and enrols
 // nothing is inert and the admin has no other signal at all. Deliberately not
 // gated on DebugLevel: it is a one-shot configuration finding of the same kind
 // as the theme-catalog failure, not a diagnostic stream. Every instance read is
 // hoisted into a local before the PrintFormat call.
 // At least this many 64 m cells must have been swept before the line is honest.
 // A default WakeDistance 700 ring is 529 cells, so the threshold never delays the
 // real case; it only stops a wilderness spawn at a very small WakeDistance from
 // reporting a terrain problem that is really a geography one.
 static const int MIN_EMPTY_INDEX_CELLS = 64;

 protected void ReportEmptyIndex(bool discovered)
 {
  if (m_EmptyIndexReported || discovered || !s_HomeIndex) return;
  int cells = s_HomeIndex.GetRootCellCount();
  if (cells < MIN_EMPTY_INDEX_CELLS) return;
  EAC_HouseholdRegistry registry = s_HomeIndex.GetRegistry();
  if (!registry || registry.GetHomeCount() > 0) return;
  m_EmptyIndexReported = true;
  int rejected = s_HomeIndex.GetRejectedHomeCount();
  int filter = HouseFilter;
  PrintFormat("[EAC] No supported house found after indexing %1 cells (rejected=%2 HouseFilter=%3). HouseFilter 1 also enrols prefabs under prefabs/structures/houses/ and is unvalidated.", cells, rejected, filter);
 }

 EAC_ThemeSelection GetThemeSelection()
 {
  if (m_PreparedThemeIndex != ThemeIndex) return null;
  return m_ThemeSelection;
 }

 protected bool PrepareTheme()
 {
  if (!m_CatalogAttempted)
  {
   m_CatalogAttempted = true;
   m_ThemeCatalog = EAC_ThemeCatalog.Load(ThemeCatalogResource);
   if (!m_ThemeCatalog) PrintFormat("[EAC] Cannot load theme catalog %1; new household allocation suspended.", ThemeCatalogResource);
  }
  if (m_PreparedThemeIndex != ThemeIndex || m_PreparedFaction != CivilianFaction)
  {
   m_PreparedThemeIndex = ThemeIndex; m_PreparedFaction = CivilianFaction;
   m_ThemeSelection = new EAC_ThemeSelection();
   EAC_ThemeDefinition definition;
   if (m_ThemeCatalog) definition = m_ThemeCatalog.GetDefinition(ThemeIndex);
   // A chosen mod faction replaces the characters of the theme; a faction that turns
   // out to offer nobody falls back to the vanilla theme rather than an empty town.
   if (CivilianFaction > 0)
   {
    EAC_ThemeDefinition factionTheme = EAC_CivilianFactions.BuildTheme(CivilianFaction, definition);
    if (factionTheme) definition = factionTheme;
    else PrintFormat("[EAC] Civilian faction %1 is not available in this mission (%2); using the vanilla theme.", CivilianFaction, EAC_CivilianFactions.Describe());
   }
   m_ThemeSelection.Begin(definition);
  }
  // Four candidates per 2 Hz tick: the shipped theme lists about forty prefabs, and
  // one per tick held the first household back for twenty seconds.
  for (int candidate = 0; candidate < 4; candidate++) m_ThemeSelection.Step();
  return m_ThemeSelection.IsReady();
 }

 static EAC_AmbientModule GetActive()
 {
  if (!Replication.IsServer() || !GetGame()) return null;
  foreach (EAC_AmbientModule module : s_Modules)
   if (module && module.GetWorld() == GetGame().GetWorld()) return module;
  return null;
 }

 static bool ContainsPopulationPoint(float x, float z, float centreX, float centreZ, float radius)
 {
  float dx = x - centreX;
  float dz = z - centreZ;
  return radius > 0 && dx * dx + dz * dz <= radius * radius;
 }

 bool ContainsPopulationPosition(vector position)
 {
  vector centre = GetOrigin();
  return ContainsPopulationPoint(position[0], position[2], centre[0], centre[2], SettlementRadius);
 }

 // No active module already prevents spawning at the ownership gates. Keep
 // standalone exclusion/cleanup contracts usable during a controller gap.
 static bool IsOutsidePopulationArea(vector position)
 {
  EAC_AmbientModule module = GetActive();
  return module && !module.ContainsPopulationPosition(position);
 }

 void NormalizeSettings()
 {
  PopulationLimit = Math.Clamp(PopulationLimit, 0, 200);
  ThemeIndex = Math.Clamp(ThemeIndex, 0, 31);
  SmallHouseResidents = Math.Clamp(SmallHouseResidents, 0, 20);
  LargeHouseResidents = Math.Clamp(LargeHouseResidents, 0, 20);
  WakeDistance = Math.Clamp(WakeDistance, 50, 5000);
  SleepDistance = Math.Clamp(SleepDistance, WakeDistance + 1, 6000);
  ClearDelay = Math.Clamp(ClearDelay, 5, 600);
  IndoorSpawnDistance = Math.Clamp(IndoorSpawnDistance, 0, 300);
  IndoorSpawnShare = Math.Clamp(IndoorSpawnShare, 0, 100);
  RoutineRange = Math.Clamp(RoutineRange, 40, 160);
  SceneSurvey = Math.Clamp(SceneSurvey, 0, 1);
  SceneSurveyRate = Math.Clamp(SceneSurveyRate, 1, 8);
  SceneSitting = Math.Clamp(SceneSitting, 0, 1);
  TableBias = Math.Clamp(TableBias, 0, 100);
  VoiceRange = Math.Clamp(VoiceRange, 5, 120);
  VanillaSeating = Math.Clamp(VanillaSeating, 1, 2);
  RoutineStops = Math.Clamp(RoutineStops, 1, 4);
  RoutineLegSpacing = Math.Clamp(RoutineLegSpacing, 8, 40);
  RoutineGap = Math.Clamp(RoutineGap, 0, 300);
  ActivityLimit = Math.Clamp(ActivityLimit, 0, 256);
  ActivityInterval = Math.Clamp(ActivityInterval, 30, 600);
  CalmDelay = Math.Clamp(CalmDelay, 5, 120);
  DebugLevel = Math.Clamp(DebugLevel, 0, 3);
  // The static mirror is written here as well as in SetSetting case 10. Every
  // path that changes DebugLevel ends in this function, so this is the write that
  // cannot be forgotten; the case-10 write only makes the intent local to the
  // setting. Without both, EAC_RoutineStats.TraceRejection and the other static
  // diagnostics read zero forever and die silently.
  s_DebugLevel = DebugLevel;
  DebugDraw = Math.Clamp(DebugDraw, 0, 1);
  TrafficLimit = Math.Clamp(TrafficLimit, 0, 16);
  TrafficSpeed = Math.Clamp(TrafficSpeed, 10, 40);
  TrafficDistance = Math.Clamp(TrafficDistance, 100, 3000);
  TrafficPassengers = Math.Clamp(TrafficPassengers, 0, 1);
  TrafficInterval = Math.Clamp(TrafficInterval, 15, 300);
  TrafficStuckDelay = Math.Clamp(TrafficStuckDelay, 30, 300);
  TrafficSleepDistance = Math.Clamp(TrafficSleepDistance, TrafficDistance + 1, 6000);
  LocalPopulationLimit = Math.Clamp(LocalPopulationLimit, 0, 200);
  LocalPopulationRadius = Math.Clamp(LocalPopulationRadius, 50, 1000);
  LoadTargetFps = Math.Clamp(LoadTargetFps, 0, 60);
  VoiceInterval = Math.Clamp(VoiceInterval, 0, 600);
  IdleRecoverySeconds = Math.Clamp(IdleRecoverySeconds, 0, 600);
  IdleForceSeconds = Math.Clamp(IdleForceSeconds, 30, 120);
  TrafficRoundTrips = Math.Clamp(TrafficRoundTrips, 0, 1);
  Density = Math.Clamp(Density, 0, 3);
  CarsEnabled = Math.Clamp(CarsEnabled, 0, 1);
  SoundsEnabled = Math.Clamp(SoundsEnabled, 0, 1);
  CivilianFaction = Math.Clamp(CivilianFaction, 0, 15);
  PrewarmMode = Math.Clamp(PrewarmMode, 0, 2);
  PrewarmCellsPerTick = Math.Clamp(PrewarmCellsPerTick, 1, 32);
  HomeIsolationRule = Math.Clamp(HomeIsolationRule, 0, 3);
  HomeClusterMin = Math.Clamp(HomeClusterMin, 1, 8);
  HomeClusterRadius = Math.Clamp(HomeClusterRadius, 40, 256);
  SettlementRadius = Math.Clamp(SettlementRadius, 100, 1500);
  MinLocalPopulation = Math.Clamp(MinLocalPopulation, 0, 200);
  // The floor can never exceed the ceiling it shares a counter with; asking for
  // more civilians than LocalPopulationLimit admits would be a floor that can
  // never be satisfied and a catch-up rate that never switches off. Deliberately
  // a second statement, not part of the clamp: the Edit.conf slider bound is the
  // literal 0-200 range above, which is what the operator is offered.
  MinLocalPopulation = Math.Min(MinLocalPopulation, LocalPopulationLimit);
  AdmissionsPerTick = Math.Clamp(AdmissionsPerTick, 1, 4);
  CatchUpAdmissionsPerTick = Math.Clamp(CatchUpAdmissionsPerTick, 1, 8);
  HouseFilter = Math.Clamp(HouseFilter, 0, 1);
  // Any settings write reaches here, and the incremental reconcile reads four of
  // them. Clearing its 1 Hz gate is what keeps a live Game Master slider change
  // applied on the very next tick rather than up to a second later.
  s_NextReconcile = 0;
 }

 bool TryReservePopulation(int partyId, int residents)
 {
  if (!Replication.IsServer() || GetActive() != this || s_IndexWorld != GetWorld() || !s_Budget) return false;
  return s_Budget.TryReserve(partyId, residents, PopulationLimit);
 }

 // Admission policy only; a future spawn transaction must also reserve the budget.
 bool CanAdmitResident(EAC_HouseholdRecord home, EAC_ResidentRecord resident, vector spawnPosition)
 {
  return GetAdmissionReason(home, resident, spawnPosition) == EAC_ESpawnReason.NONE;
 }

 int GetAdmissionReason(EAC_HouseholdRecord home, EAC_ResidentRecord resident, vector spawnPosition)
 {
  if (!Replication.IsServer() || GetActive() != this || !s_HomeIndex || !s_Claims) return EAC_ESpawnReason.BAD_OWNER;
  if (!home || !home.BuildingEntity) return EAC_ESpawnReason.BAD_HOME;
  if (!s_HomeIndex.GetRegistry().OwnsResident(home, resident)) return EAC_ESpawnReason.RESIDENT_INELIGIBLE;
  EAC_ResidentClaim claim = s_Claims.Find(home, resident);
  if (claim)
  {
   if (claim.Cache) return EAC_ESpawnReason.CACHE_RECOVERY;
   if (!claim.Committed) return EAC_ESpawnReason.PENDING;
   return EAC_ESpawnReason.RESIDENT_INELIGIBLE;
  }
  if (!resident.Wanted || resident.Dead || resident.Removed) return EAC_ESpawnReason.RESIDENT_INELIGIBLE;
  // One line closes the window in which a record predates a policy change, and
  // gives the operator a named counter. Cost is the cached O(1) read; at rule 0
  // IsHomeEnrolled returns true before reading anything.
  if (!s_HomeIndex.IsHomeEnrolled(home)) return EAC_ESpawnReason.ISOLATED_HOME;
  if (!EAC_ExclusionZone.IsPopulationAllowed(home.BuildingEntity.GetOrigin()) || !EAC_ExclusionZone.IsPopulationAllowed(spawnPosition)) return EAC_ESpawnReason.EXCLUSION;
  return EAC_ESpawnReason.NONE;
 }

 protected bool OwnsClaims()
 {
  return Replication.IsServer() && GetActive() == this && s_IndexWorld == GetWorld() && s_Claims;
 }

 EAC_ResidentClaim GetResidentActivation(EAC_HouseholdRecord home, EAC_ResidentRecord resident)
 {
  if (!Replication.IsServer() || s_IndexWorld != GetWorld() || !s_Claims) return null;
  return s_Claims.Find(home, resident);
 }

 EAC_ResidentClaim BeginResidentActivation(EAC_HouseholdRecord home, EAC_ResidentRecord resident, vector position)
 {
  if (!OwnsClaims() || !CanAdmitResident(home, resident, position)) return null;
  return s_Claims.Begin(home, resident, PopulationLimit);
 }

 bool TrackResidentCharacter(EAC_ResidentClaim claim, IEntity character)
 {
  return OwnsClaims() && s_Claims.TrackCharacter(claim, character);
 }

 bool TrackResidentGroup(EAC_ResidentClaim claim, SCR_AIGroup group)
 {
  return OwnsClaims() && s_Claims.TrackGroup(claim, group);
 }

 bool CommitResidentActivation(EAC_ResidentClaim claim)
 {
  return OwnsClaims() && s_Claims.Commit(claim);
 }

 bool CancelResidentActivation(EAC_ResidentClaim claim)
 {
  return OwnsClaims() && s_Claims.Cancel(claim);
 }

 bool FinishResidentRemoval(EAC_ResidentClaim claim, bool died)
 {
  return OwnsClaims() && s_Claims.FinishRemoval(claim, died);
 }

 EAC_PedestrianCache BeginResidentCache(EAC_ResidentClaim claim)
 {
  if (!OwnsClaims()) return null;
  return s_Claims.BeginCache(claim);
 }

 bool FinishResidentCache(EAC_ResidentClaim claim)
 {
  return OwnsClaims() && s_Claims.FinishCache(claim);
 }

 bool ReleasePopulation(int partyId)
 {
  if (!Replication.IsServer() || GetActive() != this || s_IndexWorld != GetWorld() || !s_Budget) return false;
  return s_Budget.Release(partyId);
 }

 int GetReservedPopulation()
 {
  if (!Replication.IsServer() || s_IndexWorld != GetWorld() || !s_Budget) return 0;
  return s_Budget.GetUsed();
 }

 // Server snapshots are delivered only through the GM/admin-authorized debug view.
 // These methods expose existing state and never discover/spawn entities.
 // What the Game Master overlay shows: six short lines of numbers. The full
 // counter dump used to be drawn on screen once a second and covered it (owner
 // feedback 2026-09-19); that text still goes to the server console log through
 // BuildDebugSummary, which is where a log belongs.
 string BuildOverlayStats()
 {
  if (!Replication.IsServer() || !GetHomeIndex() || !GetSpawner()) return "server snapshot unavailable";
  if (GetActive() != this) return "inactive duplicate module - delete it";
  EAC_HouseholdRegistry registry = s_HomeIndex.GetRegistry();
  int fps;
  if (m_FrameAverage > 0) fps = Math.Round(1.0 / m_FrameAverage);
  string stats = "civilians " + GetReservedPopulation().ToString() + "/" + PopulationLimit.ToString();
  stats += "\nhomes " + registry.GetHomeCount().ToString() + "  residents " + registry.GetResidentCount().ToString();
  stats += "\nserver fps " + fps.ToString();
  if (m_LoadLimited) stats += "  (admission paused)";
  AIWorld aiWorld = GetGame().GetAIWorld();
  if (aiWorld) stats += "\nactive AI " + aiWorld.GetCurrentNumOfActiveAIs().ToString() + "/" + aiWorld.GetLimitOfActiveAIs().ToString();
  if (s_Traffic && GetCarLimit() > 0) stats += "\ncars " + s_Traffic.GetLiveCarCount().ToString() + " | traffic parties " + s_Traffic.GetActiveCount().ToString() + "/" + GetCarLimit().ToString();
  if (m_Duplicate) stats += "\nsecond module placed - delete it";
  stats += "\ndetail: server console log";
  return stats;
 }

 string BuildDebugSummary()
 {
  if (!Replication.IsServer() || !GetHomeIndex() || !GetSpawner()) return "Ambient diagnostics: server snapshot unavailable";
  if (GetActive() != this) return "Ambient diagnostics: inactive duplicate module duplicate=1";
  EAC_HouseholdRegistry registry = s_HomeIndex.GetRegistry();
  string summary = "players=" + m_DebugPlayers.ToString() + " characters=" + m_DebugCharacters.ToString() + " homes=" + registry.GetHomeCount().ToString() + " residents=" + registry.GetResidentCount().ToString() + " reserved=" + GetReservedPopulation().ToString() + "/" + PopulationLimit.ToString();
  int fps;
  if (m_FrameAverage > 0) fps = Math.Round(1.0 / m_FrameAverage);
  summary += " server_fps~" + fps.ToString() + " load_paused=" + m_LoadLimited.ToString();
  // Duplicate-module notice (S3). Its own statement rather than a term on the
  // concat above, which Enforce refuses past a certain chain length. Zero on a
  // correctly configured mission; one means a surplus module is placed and every
  // slider on it is doing nothing.
  int duplicate = 0;
  if (m_Duplicate) duplicate = 1;
  summary += " duplicate=" + duplicate.ToString();
  // Engine active-AI budget beside our own reserved count: one group per
  // resident is an agent-shaped object too, and whether it is charged against
  // this limit decides if grouping could ever matter (research 2026-09-16).
  int aiActive = -1;
  int aiLimit = -1;
  AIWorld aiWorld = GetGame().GetAIWorld();
  if (aiWorld)
  {
   aiActive = aiWorld.GetCurrentNumOfActiveAIs();
   aiLimit = aiWorld.GetLimitOfActiveAIs();
  }
  summary += " ai_active=" + aiActive.ToString() + "/" + aiLimit.ToString();
  // The engine's own per-faction AI budget, measured at 128 on this scenario with
  // every civilian character charged against it and groups not. Read-only: this
  // mod never calls SetLimitOfActiveAIs, which is a scenario-owned global. The
  // cast is null-guarded like every other ChimeraAIWorld call site, and a base
  // game that reports a sentinel for CIV simply prints that sentinel.
  int civAi = -1;
  int civLimit = -1;
  int evictions = -1;
  ChimeraAIWorld chimeraAi = ChimeraAIWorld.Cast(aiWorld);
  if (chimeraAi)
  {
   civAi = chimeraAi.GetCurrentAmountOfLimitedAIsForFaction(CIV_FACTION);
   civLimit = chimeraAi.GetAILimitForFaction(CIV_FACTION);
   evictions = chimeraAi.GetLastTickEvictions();
  }
  summary += " civ_ai=" + civAi.ToString();
  summary += "/" + civLimit.ToString();
  summary += " evictions=" + evictions.ToString();
  // Live prewarm progress, as its own statements rather than appended to the long
  // concat above: an operator should be able to watch the map index rather than
  // waiting for the one-shot completion line. All zero at PrewarmMode 0.
  int prewarmDone = s_HomeIndex.GetPrewarmDone();
  int prewarmQueued = s_HomeIndex.GetPrewarmQueued();
  int prewarmSettlements = s_HomeIndex.GetPrewarmSettlementsDone();
  int settlementTotal = s_HomeIndex.GetPrewarmSettlementCount();
  summary += " prewarm=" + prewarmDone.ToString();
  summary += "/" + prewarmQueued.ToString();
  summary += " settlements=" + prewarmSettlements.ToString();
  summary += "/" + settlementTotal.ToString();
  // Isolated houses. Separate statements rather than lengthening the long concat
  // above, which Enforce refuses past a certain chain length. settlement_centres=
  // rather than settlements= deliberately: the prewarm term immediately above
  // already owns settlements=, and two identical keys on one line would make the
  // log ambiguous to every parser that reads it.
  int isolatedHomes = s_HomeIndex.GetIsolatedHomeCount();
  int settlementCentres = EAC_SettlementIndex.GetCount();
  summary += " isolated_homes=" + isolatedHomes.ToString();
  summary += " settlement_centres=" + settlementCentres.ToString();
  summary += " rule=" + HomeIsolationRule.ToString();
  // Population floor. The count is the one the scheduler last read for a real
  // candidate neighbourhood, so it moves with admission rather than being a new
  // world scan of its own.
  int localFloorCount = s_Spawner.GetLastLocalCount();
  int catchUp = 0;
  if (s_Spawner.IsCatchingUp()) catchUp = 1;
  summary += " local_floor=" + localFloorCount.ToString();
  summary += "/" + MinLocalPopulation.ToString();
  summary += " catchup=" + catchUp.ToString();
  summary += "\n" + s_Spawner.BuildDebugStats() + "\n" + s_Spawner.GetDiagnostics().Describe();
  if (s_Traffic) summary += "\n" + s_Traffic.BuildDebugSummary(DebugLevel);
  summary += "\n" + EAC_ActivityProfiles.CompatibilityStatus();
  summary += "\n" + EAC_AutoExclusions.Describe();
  if (DebugLevel >= 2)
  {
   summary += "\ngates (evaluations, max100000): " + s_Spawner.GetDiagnostics().DescribeCounts();
   // Anchor availability decides whether the routine catalog is real or cosmetic.
   summary += "\n" + EAC_RoutineStats.Describe();
   summary += "\n" + EAC_RoutineStats.DescribeIndoor();
   // Chained stops, walked metres per stop and the honest idle gap. Every
   // EAC_RoutineStats line needs DebugLevel >= 2; runs 66-70 were blind to their
   // own counters by construction.
   summary += "\n" + EAC_RoutineStats.DescribeRoutines();
   summary += "\n" + EAC_RoutineStats.DescribeJoins();
   summary += "\n" + EAC_RoutineStats.DescribeSeats();
   summary += "\n" + EAC_RoutineStats.DescribeVanillaSeats();
   summary += "\n" + EAC_RoutineStats.DescribeAcquire();
   summary += "\n" + EAC_RoutineStats.DescribeApproach();
   // The LOD-pin and walker levers. Counters only: nothing is tuned from them
   // until a campaign has printed them.
   summary += "\n" + EAC_RoutineStats.DescribePrewarm();
   summary += " | " + EAC_RoutineStats.DescribeWalk();
   summary += "\n" + EAC_RoutineStats.DescribeVoice();
   summary += "\n" + EAC_CivilianVoice.DescribeSpeakers();
   summary += "\n" + EAC_SceneIndex.DescribeKinds();
   // Measured scheduler cost, the gradual spawn/despawn evidence and the idle
   // watchdog, all from EAC_SchedulerStats. The tag is spelled the same way the
   // QA fixture spells it so one grep finds both.
   summary += "\n[EAC QA SCHED] " + EAC_SchedulerStats.Describe();
   summary += "\n" + EAC_SchedulerStats.DescribeSteps();
   summary += "\n" + EAC_SchedulerStats.DescribePacing();
   summary += " | " + EAC_SchedulerStats.DescribeForced();
  }
  if (DebugLevel >= 3) summary += "\n" + EAC_SceneIndex.Describe();
  if (DebugLevel >= 3) summary += "\n" + s_Spawner.DescribeMonitor();
  if (DebugLevel >= 3) summary += "\nindex queries=" + s_HomeIndex.GetQueryCount().ToString() + " cells=" + s_HomeIndex.GetRootCellCount().ToString() + " pending_cells=" + s_HomeIndex.GetPendingCellCount().ToString() + " saturated=" + s_HomeIndex.GetSaturatedCellCount().ToString() + " rejected_homes=" + s_HomeIndex.GetRejectedHomeCount().ToString();
  return summary;
 }

 void GetDebugDrawData(array<vector> positions, array<int> kinds)
 {
  if (!positions || !kinds) return;
  positions.Clear(); kinds.Clear();
  if (!EAC_DebugView.IsDrawingEnabled(DebugLevel, DebugDraw) || !Replication.IsServer() || GetActive() != this || !GetHomeIndex() || !GetSpawner()) return;
  EAC_ExclusionZone.AppendDebugDrawData(positions, kinds);
  if (s_Traffic) s_Traffic.AppendDebug(positions, kinds);
  s_Spawner.AppendDebugDrawData(positions, kinds);
  s_HomeIndex.AppendDebugDrawData(positions, kinds);
  EAC_SceneIndex.AppendDebugDrawData(positions, kinds);
 }

 // The visible boundary is the same module-centred area used by admission.
 void GetDebugRangeCenters(array<vector> centers)
 {
  if (!centers || !EAC_DebugView.IsDrawingEnabled(DebugLevel, DebugDraw) || !Replication.IsServer() || GetActive() != this) return;
  centers.Insert(GetOrigin());
 }

 protected void UpdateDebugLog(float now)
 {
  if (DebugLevel == 0) return;
  if (m_ConfigDirty)
  {
   // Configuration edits are coalesced on the existing scheduler, not logged
   // once per restored attribute. Keys match GetSetting and the saved schema.
   string settings;
   for (int key = 0; key <= 51; key++) settings += key.ToString() + ":" + GetSetting(key).ToString() + " ";
   PrintFormat("[EAC config] active position=%1 faction=%2 catalog=%3 settings=%4", GetOrigin(), EAC_CivilianFactions.KeyAt(CivilianFaction), ThemeCatalogResource, settings);
   m_ConfigDirty = false;
  }
  if (now < m_NextDebugLog) return;
  m_NextDebugLog = now + 10;
  Print("[EAC] " + BuildDebugSummary());
 }

 // Server picks the speaker; every client plays it locally. The event name is
 // re-validated on receipt so only the allowlisted native event can ever reach
 // a sound component.
 void EAC_BroadcastVocalisation(IEntity actor, string eventName)
 {
  if (!Replication.IsServer() || !actor || !EAC_CivilianVoice.IsApprovedEvent(eventName)) return;
  // An EntityID is local to the machine that assigned it. Six campaigns
  // broadcast the server's ID and every client resolved it to null (run 52:
  // voice_issued=1, client counters all zero). Only the replication id crosses
  // the network; the client turns it back into its own entity.
  RplComponent rpl = RplComponent.Cast(actor.FindComponent(RplComponent));
  if (!rpl) return;
  vector origin = actor.GetOrigin();
  Rpc(RPC_EAC_Vocalise, rpl.Id(), eventName, origin, VoiceRange);
  RPC_EAC_Vocalise(rpl.Id(), eventName, origin, VoiceRange);
 }

 [RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
 protected void RPC_EAC_Vocalise(RplId target, string eventName, vector origin, int audibleRange)
 {
  // The module can arrive before a newly born speaker. Resolve briefly on the
  // client's shared sound update loop instead of losing that first event.
  EAC_CivilianVoice.ReceiveLocal(target, eventName, origin, audibleRange, DebugLevel);
 }

 int GetSetting(int key)
 {
  switch (key)
  {
   case 0: return PopulationLimit;
   case 1: return SmallHouseResidents;
   case 2: return LargeHouseResidents;
   case 3: return WakeDistance;
   case 4: return SleepDistance;
   case 5: return ClearDelay;
   case 6: return ThemeIndex;
   case 7: return ActivityLimit;
   case 8: return ActivityInterval;
   case 9: return CalmDelay;
   case 10: return DebugLevel;
   case 11: return DebugDraw;
   case 12: return TrafficLimit;
   case 13: return TrafficSpeed;
   case 14: return TrafficDistance;
   case 15: return TrafficPassengers;
   case 16: return TrafficInterval;
   case 17: return TrafficStuckDelay;
   case 18: return TrafficSleepDistance;
   case 19: return LocalPopulationLimit;
   case 20: return LocalPopulationRadius;
   case 21: return LoadTargetFps;
   case 22: return IndoorSpawnDistance;
   case 23: return VoiceInterval;
   case 24: return IdleRecoverySeconds;
   case 25: return IndoorSpawnShare;
   case 26: return RoutineRange;
   case 27: return SceneSurvey;
   case 28: return SceneSurveyRate;
   case 29: return SceneSitting;
   case 30: return VanillaSeating;
   case 31: return TableBias;
   case 32: return VoiceRange;
   // Keys 0-32 are a stable GM contract and must not be renumbered or reused.
   // 33-36 isolated houses, 37-39 the population floor, 40 house recognition.
   // Roaming routines start at 41.
   case 33: return HomeIsolationRule;
   case 34: return HomeClusterMin;
   case 35: return HomeClusterRadius;
   case 36: return SettlementRadius;
   case 37: return MinLocalPopulation;
   case 38: return AdmissionsPerTick;
   case 39: return CatchUpAdmissionsPerTick;
   case 40: return HouseFilter;
   case 41: return RoutineStops;
   case 42: return RoutineLegSpacing;
   case 43: return RoutineGap;
   // Mission-start prewarm.
   case 44: return PrewarmMode;
   case 45: return PrewarmCellsPerTick;
   // Idle watchdog deadline (the forced-movement guarantee).
   case 46: return IdleForceSeconds;
   case 47: return TrafficRoundTrips;
   case 48: return Density;
   case 49: return CarsEnabled;
   case 50: return SoundsEnabled;
   case 51: return CivilianFaction;
  }
  return 0;
 }

 void SetSetting(int key, int value)
 {
  if (!Replication.IsServer()) return;
  int previous = GetSetting(key);
  switch (key)
  {
   case 0: PopulationLimit = value; break;
   case 1: SmallHouseResidents = value; break;
   case 2: LargeHouseResidents = value; break;
   case 3: WakeDistance = value; break;
   case 4: SleepDistance = value; break;
   case 5: ClearDelay = value; break;
   case 6: ThemeIndex = value; break;
   case 7: ActivityLimit = value; break;
   case 8: ActivityInterval = value; break;
   case 9: CalmDelay = value; break;
   // The mirror is written here as well as in NormalizeSettings below, so the
   // intent is visible at the setting a Game Master actually moves.
   case 10: DebugLevel = value; s_DebugLevel = value; m_NextDebugLog = 0; break;
   case 11: DebugDraw = value; break;
   case 12: TrafficLimit = value; break;
   case 13: TrafficSpeed = value; break;
   case 14: TrafficDistance = value; break;
   case 15: TrafficPassengers = value; break;
   case 16: TrafficInterval = value; break;
   case 17: TrafficStuckDelay = value; break;
   case 18: TrafficSleepDistance = value; break;
   case 19: LocalPopulationLimit = value; break;
   case 20: LocalPopulationRadius = value; break;
   case 21: LoadTargetFps = value; break;
   case 22: IndoorSpawnDistance = value; break;
   case 23: VoiceInterval = value; break;
   case 24: IdleRecoverySeconds = value; break;
   case 25: IndoorSpawnShare = value; break;
   case 26: RoutineRange = value; break;
   case 27: SceneSurvey = value; break;
   case 28: SceneSurveyRate = value; break;
   case 29: SceneSitting = value; break;
   case 30: VanillaSeating = value; break;
   case 31: TableBias = value; break;
   case 32: VoiceRange = value; break;
   // 33-36 isolated houses, 37-39 the population floor, 40 house recognition.
   case 33: HomeIsolationRule = value; break;
   case 34: HomeClusterMin = value; break;
   case 35: HomeClusterRadius = value; break;
   case 36: SettlementRadius = value; break;
   case 37: MinLocalPopulation = value; break;
   case 38: AdmissionsPerTick = value; break;
   case 39: CatchUpAdmissionsPerTick = value; break;
   case 40: HouseFilter = value; break;
   // Roaming routines take 41-43.
   case 41: RoutineStops = value; break;
   case 42: RoutineLegSpacing = value; break;
   case 43: RoutineGap = value; break;
   case 44: PrewarmMode = value; break;
   case 45: PrewarmCellsPerTick = value; break;
   case 46: IdleForceSeconds = value; break;
   case 47: TrafficRoundTrips = value; break;
   case 48: Density = Math.Clamp(value, 0, 3); ApplyDensity(); break;
   case 49: CarsEnabled = value; break;
   case 50: SoundsEnabled = value; break;
   case 51: CivilianFaction = value; break;
   default: return;
  }
  NormalizeSettings();
  if (previous != GetSetting(key) || key == 48) m_ConfigDirty = true;
  Replication.BumpMe();
 }

 protected ref map<int, vector> m_EAC_SessionBlocks;
 protected bool m_EAC_SessionSettingsPending, m_EAC_SessionSettingsFailed, m_EAC_SessionHeaderSeen;

 // Called by the native editor-load hook. Legacy saves may have only the UI
 // attributes, so the absence of a versioned header is not itself an error.
 void EAC_BeginSessionSettingsLoad()
 {
  m_EAC_SessionBlocks = null;
  m_EAC_SessionSettingsPending = false;
  m_EAC_SessionSettingsFailed = false;
  m_EAC_SessionHeaderSeen = false;
 }

 bool EAC_SessionSettingsBlocked() { return m_EAC_SessionSettingsPending || m_EAC_SessionSettingsFailed; }

 // The first shared tick after EOnEditorSessionLoad runs after synchronous
 // attribute application. Missing commit blocks must fail visibly too.
 void EAC_FinishSessionSettingsLoad()
 {
  if (m_EAC_SessionSettingsPending) EAC_RejectSessionSettings("incomplete settings snapshot (no commit)");
 }

 protected void EAC_RejectSessionSettings(string reason)
 {
  if (!m_EAC_SessionSettingsFailed) Print("[EAC session] ERROR controller admissions held: " + reason, LogLevel.ERROR);
  m_EAC_SessionSettingsFailed = true;
  m_EAC_SessionSettingsPending = false;
  m_EAC_SessionBlocks = null;
 }

 void EAC_ReadSessionBlock(int block, vector value)
 {
  if (!Replication.IsServer()) return;
  if (m_EAC_SessionSettingsFailed) return;
  if (block == 0)
  {
   if (m_EAC_SessionHeaderSeen) { EAC_RejectSessionSettings("duplicate settings header"); return; }
   m_EAC_SessionHeaderSeen = true;
   m_EAC_SessionSettingsPending = true;
   if (value[0] != EAC_SessionSettings.VERSION || value[1] < 0 || value[1] > EAC_SessionSettings.MAX_CATALOG || value[2] < 1 || value[2] > EAC_SessionSettings.MAX_FACTION || Math.Round(value[1]) != value[1] || Math.Round(value[2]) != value[2])
   { EAC_RejectSessionSettings("invalid settings version or identity length"); return; }
   m_EAC_SessionBlocks = new map<int, vector>();
  }
  if (!m_EAC_SessionBlocks || block < 0 || block > EAC_SessionSettings.COMMIT || m_EAC_SessionBlocks.Contains(block))
  { EAC_RejectSessionSettings("missing header, duplicate or unknown settings block"); return; }
  m_EAC_SessionBlocks.Insert(block, value);
  if (block != EAC_SessionSettings.COMMIT) return;
  if (value != Vector(EAC_SessionSettings.VERSION, EAC_SessionSettings.SETTINGS, 0))
  { EAC_RejectSessionSettings("invalid settings commit"); return; }
  vector header = m_EAC_SessionBlocks.Get(0);
  array<int> settings = {};
  for (int key = 0; key < EAC_SessionSettings.SETTINGS; key++)
  {
   int settingsBlock = 1 + key / 3;
   if (!m_EAC_SessionBlocks.Contains(settingsBlock)) { EAC_RejectSessionSettings("incomplete controller settings"); return; }
   vector row = m_EAC_SessionBlocks.Get(settingsBlock);
   float number = row[key % 3];
   if (number < 0 || number > 1000000 || Math.Round(number) != number) { EAC_RejectSessionSettings("invalid controller setting"); return; }
   settings.Insert(Math.Round(number));
  }
  int length = header[1] + header[2];
  string identity;
  for (int offset = 0; offset < length; offset += 9)
  {
   int textBlock = EAC_SessionSettings.TEXT_FIRST + offset / 9;
   string decoded;
   if (!m_EAC_SessionBlocks.Contains(textBlock) || !EAC_SessionSettings.UnpackText(m_EAC_SessionBlocks.Get(textBlock), Math.Min(9, length - offset), decoded))
   { EAC_RejectSessionSettings("incomplete or invalid catalog/faction identity"); return; }
   identity += decoded;
  }
  EAC_ApplySessionSettings(settings, identity.Substring(0, header[1]), identity.Substring(header[1], header[2]));
  m_EAC_SessionBlocks = null;
  m_EAC_SessionSettingsPending = false;
 }

 // Raw assignment avoids per-field clamp/order drift and does not reapply the
 // Density preset over authored custom limits. Normalize once after all fields.
 void EAC_ApplySessionSettings(array<int> values, ResourceName catalog, string factionKey)
 {
  if (!Replication.IsServer() || !values || values.Count() != EAC_SessionSettings.SETTINGS) return;
  PopulationLimit = values[0]; SmallHouseResidents = values[1]; LargeHouseResidents = values[2];
  WakeDistance = values[3]; SleepDistance = values[4]; ClearDelay = values[5]; ThemeIndex = values[6];
  ActivityLimit = values[7]; ActivityInterval = values[8]; CalmDelay = values[9]; DebugLevel = values[10]; DebugDraw = values[11];
  TrafficLimit = values[12]; TrafficSpeed = values[13]; TrafficDistance = values[14]; TrafficPassengers = values[15];
  TrafficInterval = values[16]; TrafficStuckDelay = values[17]; TrafficSleepDistance = values[18];
  LocalPopulationLimit = values[19]; LocalPopulationRadius = values[20]; LoadTargetFps = values[21];
  IndoorSpawnDistance = values[22]; VoiceInterval = values[23]; IdleRecoverySeconds = values[24]; IndoorSpawnShare = values[25];
  RoutineRange = values[26]; SceneSurvey = values[27]; SceneSurveyRate = values[28]; SceneSitting = values[29];
  VanillaSeating = values[30]; TableBias = values[31]; VoiceRange = values[32]; HomeIsolationRule = values[33];
  HomeClusterMin = values[34]; HomeClusterRadius = values[35]; SettlementRadius = values[36];
  MinLocalPopulation = values[37]; AdmissionsPerTick = values[38]; CatchUpAdmissionsPerTick = values[39]; HouseFilter = values[40];
  RoutineStops = values[41]; RoutineLegSpacing = values[42]; RoutineGap = values[43]; PrewarmMode = values[44];
  PrewarmCellsPerTick = values[45]; IdleForceSeconds = values[46]; TrafficRoundTrips = values[47];
  Density = values[48]; CarsEnabled = values[49]; SoundsEnabled = values[50];
  // The saved numeric selection is deliberately superseded by its stable key.
  CivilianFaction = 0;
  bool found;
  for (int i = 0; i < EAC_CivilianFactions.Count(); i++)
   if (EAC_CivilianFactions.KeyAt(i) == factionKey) { CivilianFaction = i; found = true; break; }
  if (!found) PrintFormat("[EAC session] WARNING saved civilian faction '%1' is unavailable; using vanilla civilians", factionKey);
  ThemeCatalogResource = catalog;
  m_ThemeCatalog = null; m_ThemeSelection = null; m_CatalogAttempted = false; m_PreparedThemeIndex = -1;
  m_NextDebugLog = 0; m_ConfigDirty = true;
  NormalizeSettings();
  Replication.BumpMe();
 }

 static EAC_PedestrianSpawner EAC_GetSessionSpawner()
 {
  if (!Replication.IsServer() || !GetGame() || s_IndexWorld != GetGame().GetWorld()) return null;
  return s_Spawner;
 }
}
