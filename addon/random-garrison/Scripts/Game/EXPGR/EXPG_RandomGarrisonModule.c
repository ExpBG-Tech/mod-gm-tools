// Random Garrison (Game Master Systems entity). Generate garrisons random buildings
// within the radius with random squads of one or two factions (one faction per
// building). Every squad is an ordinary garrison made through the shared spawner
// (EXPG_GarrisonSpawner, the same path as EXPBG Add Garrison) and carries this
// zone's token in its record (GeneratedBy); the zone keeps no list of its squads and
// finds them again through EXPG_GarrisonManager.CollectGenerated, also after a save
// was loaded (the garrison ledger saves GeneratedBy). Casualties are never refilled
// and the zone never generates again by itself. Server-authoritative; clients only
// see the replicated radius (GM area mesh) and the status line.
[EntityEditorProps(category: "EXPBG/Garrison", description: "Random Garrison: garrisons random buildings in a radius with random squads (ordinary garrisons)")]
class EXPG_RandomGarrisonModuleClass : GenericEntityClass
{
}

// The analysis request of one building (EXPG_GarrisonManager.Wait). Background:
// Game Masters' own EXPBG Add Garrison requests are analysed first. It holds no
// pointer to the zone; the zone reads it.
class EXPG_RGAnalysis : EXPG_PlanWaiter
{
 bool Ready;
 bool Failed;
 string Failure;

 void EXPG_RGAnalysis()
 {
  Background = true;
 }

 override bool OnReady()
 {
  Ready = true;
  return true;
 }

 override void OnFailed(string reason)
 {
  Failed = true;
  Failure = reason;
 }
}

// One eligible building of a generation.
class EXPG_RGSite
{
 static const int RESERVE = 0;
 static const int QUEUED = 1;
 static const int ANALYSING = 2;
 static const int READY = 3;
 static const int DONE = 4;
 static const int FAILED = 5;
 static const int SKIPPED = 6;
 IEntity Structure;
 string Key;
 string PrefabName;
 vector Origin;
 int Stage;
 string Note;
 string FactionId;
 int Wanted;
 int Placed;
 bool Drawn;
 bool Retried;
 // Squads of a smaller bucket only (after a squad that never deployed).
 int RetryBelow = 16;
 bool Reanalysed;
 bool Analysed;
 float DeferredSince = -1;
 ref RandomGenerator Rng;
 ref EXPG_RGAnalysis Analysis;
 ref array<string> Prefabs = {};

 bool Active()
 {
  return Stage == QUEUED || Stage == ANALYSING || Stage == READY;
 }
}

// A squad spawned by the zone that has not taken its posts yet. It is kept out of
// every save (the zone flags it) until it is a Ready garrison, which saves itself.
class EXPG_RGPending
{
 SCR_AIGroup Squad;
 ref EXPG_GarrisonRecord Record;
 EXPG_RGSite Site;
 ResourceName Prefab;
 int Members;
 int Bucket;
 float Started;
 ref array<IEntity> Flagged = {};
 ref array<bool> FlagSet = {};
 ref array<bool> WasTracked = {};
}

class EXPG_RandomGarrisonModule : GenericEntity
{
 // Setting keys: Edit.conf m_Key values, and the native save's settings array (0-13);
 // the two factions are saved as keys.
 static const int KEY_RADIUS = 0;
 static const int KEY_SIZES = 1;
 static const int KEY_EXCLUDE_SUPPORT = 2;
 static const int KEY_BUILDINGS = 3;
 static const int KEY_SHARE = 4;
 static const int KEY_SQUADS_MIN = 5;
 static const int KEY_SQUADS_MAX = 6;
 static const int KEY_PLAYER_DISTANCE = 7;
 static const int KEY_ALLOW_GARRISONED = 8;
 static const int KEY_SEED = 9;
 static const int KEY_CACHE_MODE = 10;
 static const int KEY_WAKE = 11;
 static const int KEY_SLEEP = 12;
 static const int KEY_ON_DELETE = 13;
 static const int INT_SETTINGS = 14;
 static const int KEY_FACTION = 14;
 static const int KEY_SECOND_FACTION = 15;
 static const int SETTING_KEYS = 16;

 static const int ACTION_NONE = 0;
 static const int ACTION_GENERATE = 1;
 static const int ACTION_REGENERATE = 2;
 static const int ACTION_CLEAR = 3;
 static const int ACTION_STOP = 4;

 static const int STATE_IDLE = 0;
 static const int STATE_CENSUS = 1;
 static const int STATE_CATALOG = 2;
 static const int STATE_RUNNING = 3;
 static const int STATE_DONE = 4;
 static const int STATE_CLEARING = 5;
 static const int STATE_STOPPED = 6;

 static const int ON_DELETE_KEEP = 0;
 static const int ON_DELETE_GARRISONS = 1;

 static const int RADIUS_MIN = 25;
 static const int RADIUS_MAX = 1000;
 static const int MAX_ANALYSES_PER_ZONE = 2;
 // Leaves 8 of the manager's 64 building plans for Game Masters.
 static const int PLAN_HEADROOM = 56;
 static const float PENDING_TIMEOUT = 60;
 static const float PLAYER_DEFER_LIMIT = 120;
 static const float AI_LIMIT_WAIT = 60;
 static const float STATUS_INTERVAL = 0.5;
 static const float OBSERVER_INTERVAL = 1;
 static const int SETTINGS_PER_TICK = 8;
 static const int DISMISS_PER_TICK = 2;
 static const int MAX_OUTCOMES = 32;
 static const float NOTICE_SECONDS = 30;
 // Added squads spawn 4 m outside a garrisoned building's bounds.
 static const float SPAWN_MARGIN = 5;

 [Attribute("150", UIWidgets.Slider, "Radius (m)", "25 1000 25", category: "EXPBG Random Garrison"), RplProp(onRplName: "OnRadiusReplicated")]
 protected int m_iRadius;
 [Attribute("6", UIWidgets.EditBox, "Squad sizes (bucket mask): 2 fire teams 4-5, 6 fire teams and squads 4-9, 4 squads 6-9, 12 squads and large 6+, 3 small teams and fire teams 1-5, 15 any", category: "EXPBG Random Garrison")]
 protected int m_iSizes;
 [Attribute("1", UIWidgets.CheckBox, "Exclude medical, logistics and essential squads", category: "EXPBG Random Garrison")]
 protected bool m_bExcludeSupport;
 [Attribute("4", UIWidgets.Slider, "Buildings to garrison", "1 32 1", category: "EXPBG Random Garrison")]
 protected int m_iBuildings;
 [Attribute("100", UIWidgets.Slider, "Share of the eligible buildings (%)", "1 100 1", category: "EXPBG Random Garrison")]
 protected int m_iShare;
 [Attribute("1", UIWidgets.Slider, "Squads per building, minimum", "1 4 1", category: "EXPBG Random Garrison")]
 protected int m_iSquadsMin;
 [Attribute("2", UIWidgets.Slider, "Squads per building, maximum", "1 4 1", category: "EXPBG Random Garrison")]
 protected int m_iSquadsMax;
 [Attribute("200", UIWidgets.Slider, "No building closer than this to a player (m)", "0 1000 25", category: "EXPBG Random Garrison")]
 protected int m_iPlayerDistance;
 [Attribute("0", UIWidgets.CheckBox, "Allow buildings that already have a garrison", category: "EXPBG Random Garrison")]
 protected bool m_bAllowGarrisoned;
 [Attribute("0", UIWidgets.Slider, "Seed (0: a new seed on every Generate)", "0 9999 1", category: "EXPBG Random Garrison")]
 protected int m_iSeed;
 [Attribute("2", UIWidgets.Slider, "Garrison cache mode: 0 Off, 1 Simulation, 2 Full", "0 2 1", category: "EXPBG Random Garrison")]
 protected int m_iCacheMode;
 [Attribute("300", UIWidgets.Slider, "Garrison wake distance (m)", "50 3000 25", category: "EXPBG Random Garrison")]
 protected int m_iWake;
 [Attribute("400", UIWidgets.Slider, "Garrison sleep distance (m)", "75 4000 25", category: "EXPBG Random Garrison")]
 protected int m_iSleep;
 [Attribute("0", UIWidgets.Slider, "When the module is deleted: 0 keep the garrisons, 1 delete them", "0 1 1", category: "EXPBG Random Garrison")]
 protected int m_iOnDelete;
 [Attribute("", UIWidgets.EditBox, "Faction key (empty: USSR when present, else the first military faction)", category: "EXPBG Random Garrison")]
 protected string m_sFaction;
 [Attribute("", UIWidgets.EditBox, "Second faction key (empty: none); each building draws one of the two", category: "EXPBG Random Garrison")]
 protected string m_sSecondFaction;
 [Attribute(uiwidget: UIWidgets.ResourceAssignArray, desc: "Mission makers: squad prefabs that replace the faction catalogs (empty: the faction catalogs)", params: "et", category: "EXPBG Random Garrison")]
 protected ref array<ResourceName> m_aSquadPrefabs;
 [Attribute("0", UIWidgets.CheckBox, "Mission makers: generate once at mission start when this zone has not generated yet", category: "EXPBG Random Garrison")]
 protected bool m_bGenerateOnStart;
 [RplProp()]
 protected string m_sStatus;

 protected bool m_bRegistered;
 protected int m_iZoneId;
 protected int m_iTokenHi;
 protected int m_iTokenLo;
 protected string m_sToken;
 protected int m_iState;
 protected bool m_bGenerated;
 protected bool m_bLoaded;
 protected bool m_bSettingsChanged;
 protected bool m_bPaused;
 protected bool m_bStopping;
 protected bool m_bRegenerate;
 protected bool m_bPlanWait;
 protected bool m_bRunQueued;
 protected int m_iQueuedAction;
 protected int m_iQueuedPlayer;
 protected int m_iRunPlayer;
 // Inputs of the current generation, copied by BeginGenerate: later edits wait for
 // Regenerate (cache mode, wake and sleep still apply at once).
 protected string m_sRunFaction;
 protected string m_sRunSecondFaction;
 protected int m_iRunSizes;
 protected int m_iRunSquadsMin;
 protected int m_iRunSquadsMax;
 protected int m_iRunPlayerDistance;
 protected int m_iRunBuildings;
 protected int m_iRunShare;
 protected bool m_bRunExcludeSupport;
 protected bool m_bRunAllowGarrisoned;
 protected int m_iLastSeed;
 protected int m_iTarget;
 protected int m_iAttempts;
 protected int m_iAttemptCap;
 protected int m_iOrderCursor;
 protected int m_iSpawnCursor;
 protected int m_iAnalysed;
 protected int m_iExcludedPlayers;
 protected int m_iExcludedTaken;
 protected int m_iFailures;
 protected int m_iOrphans;
 protected int m_iSpawned;
 protected int m_iDeferred;
 protected int m_iClearTotal;
 protected int m_iAutoTries;
 protected float m_fAILimitedSince = -1;
 protected float m_fNextStatus;
 protected float m_fNextObservers;
 protected float m_fNoticeUntil;
 protected string m_sPause;
 protected string m_sAILimit;
 protected string m_sStopReason;
 protected string m_sNotice;
 protected string m_sCatalogProblem;
 protected ref EXPG_RandomGarrisonCensus m_Census;
 protected ref EXPG_SquadCatalog m_Explicit;
 protected ref array<ref EXPG_RGSite> m_aSites = {};
 // Shuffled order and the sites still at work (weak references into m_aSites).
 protected ref array<EXPG_RGSite> m_aOrder = {};
 protected ref array<EXPG_RGSite> m_aActive = {};
 protected ref array<ref EXPG_RGPending> m_aPending = {};
 protected ref array<ref EXPG_GarrisonRecord> m_aClear = {};
 protected ref array<ref EXPG_GarrisonRecord> m_aSettingsQueue = {};
 // Soldiers and squads to delete a few per tick; out of every save until deleted.
 protected ref EXPG_RetireList m_Retire = new EXPG_RetireList();
 protected ref array<vector> m_aObservers = {};
 protected ref array<string> m_aOutcomes = {};

 //------------------------------------------------------------------------------------------------
 void EXPG_RandomGarrisonModule(IEntitySource src, IEntity parent)
 {
  SetEventMask(EntityEvent.INIT);
 }

 static bool IsRuntime()
 {
  if (!GetGame() || !GetGame().GetWorld())
  {
   return false;
  }
  return GetGame().InPlayMode();
 }

 override void EOnInit(IEntity owner)
 {
  super.EOnInit(owner);
  if (!IsRuntime() || SCR_Global.IsEditMode(owner) || !Replication.IsServer())
  {
   return;
  }
  NormalizeAll();
  if (m_iTokenHi < 1 || m_iTokenLo < 1)
  {
   m_iTokenHi = EXPG_RGRules.RandomTokenPart();
   m_iTokenLo = EXPG_RGRules.RandomTokenPart();
  }
  m_sToken = EXPG_RGRules.Token(m_iTokenHi, m_iTokenLo);
  m_iZoneId = EXPG_RandomGarrisonDirector.Register(this);
  m_bRegistered = m_iZoneId > 0;
  if (m_bGenerateOnStart && m_bRegistered)
  {
   GetGame().GetCallqueue().CallLater(AutoGenerate, 15000, false);
  }
  PrintFormat("[EXPG RANDOM] zone %1 created at %2 (registered %3)", m_sToken, GetOrigin(), m_bRegistered);
 }

 void ~EXPG_RandomGarrisonModule()
 {
  if (GetGame() && GetGame().GetCallqueue())
  {
   GetGame().GetCallqueue().Remove(RunQueued);
   GetGame().GetCallqueue().Remove(AutoGenerate);
  }
  if (!m_bRegistered)
  {
   return;
  }
  m_bRegistered = false;
  array<ref EXPG_GarrisonRecord> records = {};
  // Squads that never deployed, and whatever waits for deletion, are NON_SERIALIZABLE,
  // which a CDF load's Clear skips: whenever the world stays (also during a save or a
  // load) they go to the janitor still kept out of saves. A world teardown owns them.
  int retired;
  if (EXPG_RandomGarrisonDirector.WorldStays())
  {
   foreach (EXPG_RGPending undeployed : m_aPending)
   {
    if (PendingReady(undeployed)) { continue; }
    RetirePending(undeployed, m_Retire);
    retired++;
   }
  }
  // A world teardown or a load replacing the scene owns the garrisons: nothing more here.
  if (!EXPG_RandomGarrisonDirector.Teardown())
  {
   EXPG_GarrisonManager manager = EXPG_GarrisonManager.Get();
   foreach (EXPG_RGSite site : m_aActive)
   {
    if (site && site.Analysis && manager) { manager.StopWaiting(site.Analysis); }
   }
   foreach (EXPG_RGPending pending : m_aPending)
   {
    if (PendingReady(pending)) { ReturnFlagged(pending); }
   }
   if (m_iOnDelete == ON_DELETE_GARRISONS && manager) { manager.CollectGenerated(m_sToken, records); }
   PrintFormat("[EXPG RANDOM] zone %1 deleted: %2 squads that never deployed removed, %3 garrisons %4", m_sToken, retired, records.Count(), DeleteModeName());
  }
  // Unregister moves the retire list to the janitor only while the world stays.
  EXPG_RandomGarrisonDirector.Unregister(this, m_iZoneId, m_Retire, records);
 }

 protected string DeleteModeName()
 {
  if (m_iOnDelete == ON_DELETE_GARRISONS)
  {
   return "deleted";
  }
  return "kept";
 }

 //------------------------------------------------------------------------------------------------
 // Settings
 //------------------------------------------------------------------------------------------------
 int GetSetting(int key)
 {
  int value;
  if (key == KEY_RADIUS) { value = m_iRadius; }
  else if (key == KEY_SIZES) { value = m_iSizes; }
  else if (key == KEY_EXCLUDE_SUPPORT && m_bExcludeSupport) { value = 1; }
  else if (key == KEY_BUILDINGS) { value = m_iBuildings; }
  else if (key == KEY_SHARE) { value = m_iShare; }
  else if (key == KEY_SQUADS_MIN) { value = m_iSquadsMin; }
  else if (key == KEY_SQUADS_MAX) { value = m_iSquadsMax; }
  else if (key == KEY_PLAYER_DISTANCE) { value = m_iPlayerDistance; }
  else if (key == KEY_ALLOW_GARRISONED && m_bAllowGarrisoned) { value = 1; }
  else if (key == KEY_SEED) { value = m_iSeed; }
  else if (key == KEY_CACHE_MODE) { value = m_iCacheMode; }
  else if (key == KEY_WAKE) { value = m_iWake; }
  else if (key == KEY_SLEEP) { value = m_iSleep; }
  else if (key == KEY_ON_DELETE) { value = m_iOnDelete; }
  return value;
 }

 static bool ValidSetting(int key, int value)
 {
  if (key == KEY_RADIUS)
  {
   return value >= RADIUS_MIN && value <= RADIUS_MAX;
  }
  if (key == KEY_SIZES)
  {
   return EXPG_RGRules.ValidSizes(value);
  }
  if (key == KEY_EXCLUDE_SUPPORT || key == KEY_ALLOW_GARRISONED || key == KEY_ON_DELETE)
  {
   return value == 0 || value == 1;
  }
  if (key == KEY_BUILDINGS)
  {
   return value >= 1 && value <= EXPG_RGRules.MAX_TARGET;
  }
  if (key == KEY_SHARE)
  {
   return value >= 1 && value <= 100;
  }
  if (key == KEY_SQUADS_MIN || key == KEY_SQUADS_MAX)
  {
   return value >= 1 && value <= 4;
  }
  if (key == KEY_PLAYER_DISTANCE)
  {
   return value >= 0 && value <= 1000;
  }
  if (key == KEY_SEED)
  {
   return value >= 0 && value <= 9999;
  }
  if (key == KEY_CACHE_MODE)
  {
   return value >= 0 && value <= 2;
  }
  if (key == KEY_WAKE)
  {
   return value >= 50 && value <= 3000;
  }
  if (key == KEY_SLEEP)
  {
   return value >= 75 && value <= 4000;
  }
  return false;
 }

 protected void StoreSetting(int key, int value)
 {
  if (key == KEY_RADIUS) { m_iRadius = value; }
  else if (key == KEY_SIZES) { m_iSizes = value; }
  else if (key == KEY_EXCLUDE_SUPPORT) { m_bExcludeSupport = value != 0; }
  else if (key == KEY_BUILDINGS) { m_iBuildings = value; }
  else if (key == KEY_SHARE) { m_iShare = value; }
  else if (key == KEY_SQUADS_MIN) { m_iSquadsMin = value; }
  else if (key == KEY_SQUADS_MAX) { m_iSquadsMax = value; }
  else if (key == KEY_PLAYER_DISTANCE) { m_iPlayerDistance = value; }
  else if (key == KEY_ALLOW_GARRISONED) { m_bAllowGarrisoned = value != 0; }
  else if (key == KEY_SEED) { m_iSeed = value; }
  else if (key == KEY_CACHE_MODE) { m_iCacheMode = value; }
  else if (key == KEY_WAKE) { m_iWake = value; }
  else if (key == KEY_SLEEP) { m_iSleep = value; }
  else if (key == KEY_ON_DELETE) { m_iOnDelete = value; }
 }

 // Clamps every value; a raised minimum lifts the maximum and a lowered maximum
 // lowers the minimum (changed: the key just set, -1 for none); wake and sleep keep
 // 25 m apart (EXPG_GarrisonSettings).
 protected void Normalize(int changed)
 {
  m_iRadius = Math.ClampInt(m_iRadius, RADIUS_MIN, RADIUS_MAX);
  if (!EXPG_RGRules.ValidSizes(m_iSizes)) { m_iSizes = 6; }
  m_iBuildings = Math.ClampInt(m_iBuildings, 1, EXPG_RGRules.MAX_TARGET);
  m_iShare = Math.ClampInt(m_iShare, 1, 100);
  m_iSquadsMin = Math.ClampInt(m_iSquadsMin, 1, 4);
  m_iSquadsMax = Math.ClampInt(m_iSquadsMax, 1, 4);
  if (m_iSquadsMax < m_iSquadsMin)
  {
   if (changed == KEY_SQUADS_MAX) { m_iSquadsMin = m_iSquadsMax; }
   else { m_iSquadsMax = m_iSquadsMin; }
  }
  m_iPlayerDistance = Math.ClampInt(m_iPlayerDistance, 0, 1000);
  m_iSeed = Math.ClampInt(m_iSeed, 0, 9999);
  m_iOnDelete = Math.ClampInt(m_iOnDelete, 0, 1);
  vector cache = EXPG_GarrisonSettings.Normalize(Vector(m_iWake, m_iSleep, m_iCacheMode));
  m_iWake = Math.Round(cache[0]);
  m_iSleep = Math.Round(cache[1]);
  m_iCacheMode = Math.Round(cache[2]);
 }

 protected void NormalizeAll()
 {
  Normalize(-1);
 }

 // Server: one Game Master edit (interactive) or one attribute restore (CDF, player -1).
 void ApplySetting(int key, int value, bool interactive)
 {
  if (!Replication.IsServer() || key < 0 || key >= INT_SETTINGS)
  {
   return;
  }
  int radius = m_iRadius;
  int previous = GetSetting(key);
  int previousMin = m_iSquadsMin;
  int previousMax = m_iSquadsMax;
  int previousWake = m_iWake;
  int previousSleep = m_iSleep;
  int previousMode = m_iCacheMode;
  StoreSetting(key, value);
  Normalize(key);
  bool changed = GetSetting(key) != previous || m_iSquadsMin != previousMin || m_iSquadsMax != previousMax;
  bool cacheChanged = m_iWake != previousWake || m_iSleep != previousSleep || m_iCacheMode != previousMode;
  if (m_iRadius != radius)
  {
   Replication.BumpMe();
   OnRadiusReplicated();
  }
  if (!changed && !cacheChanged)
  {
   return;
  }
  if (!interactive)
  {
   return;
  }
  // Applied at once to this zone's live garrisons, at most 8 per tick.
  if (cacheChanged)
  {
   EXPG_GarrisonManager manager = EXPG_GarrisonManager.Get();
   if (manager) { manager.CollectGenerated(m_sToken, m_aSettingsQueue); }
  }
  // Everything else takes effect on the next Regenerate.
  if (key != KEY_ON_DELETE && !cacheChanged && (m_bGenerated || m_iState == STATE_RUNNING || m_iState == STATE_CENSUS || m_iState == STATE_CATALOG))
  {
   m_bSettingsChanged = true;
  }
  EXPG_RandomGarrisonDirector.Wake();
 }

 // Raw faction setting (empty: the default); slot 0 or 1.
 string GetFactionSetting(int slot)
 {
  if (slot == 1)
  {
   return m_sSecondFaction;
  }
  return m_sFaction;
 }

 // The faction a generation uses: the setting when it is a military faction with a
 // squad catalog, else USSR when present, else the first one (slot 1: none).
 string ResolveFaction(int slot)
 {
  array<string> keys = {};
  EXPG_SquadPool.MilitaryFactions(keys);
  string first = m_sFaction;
  if (!keys.Contains(first))
  {
   first = string.Empty;
   if (keys.Contains("USSR")) { first = "USSR"; }
   else if (!keys.IsEmpty()) { first = keys[0]; }
  }
  if (slot == 0)
  {
   return first;
  }
  if (m_sSecondFaction.IsEmpty() || m_sSecondFaction == first || !keys.Contains(m_sSecondFaction))
  {
   return string.Empty;
  }
  return m_sSecondFaction;
 }

 void SetFactionSetting(int slot, string factionId, bool interactive)
 {
  if (!Replication.IsServer() || factionId.Length() > 64)
  {
   return;
  }
  // Compared as resolved: saving the dialog unchanged picks the default explicitly.
  string previous = ResolveFaction(slot);
  if (slot == 1) { m_sSecondFaction = factionId; }
  else { m_sFaction = factionId; }
  if (!interactive || previous == ResolveFaction(slot))
  {
   return;
  }
  if (m_bGenerated || m_iState == STATE_RUNNING || m_iState == STATE_CENSUS || m_iState == STATE_CATALOG) { m_bSettingsChanged = true; }
  EXPG_RandomGarrisonDirector.Wake();
 }

 // Native save: the whole batch or nothing.
 bool RestoreSettings(notnull array<int> values, notnull array<string> factions)
 {
  if (!Replication.IsServer() || values.Count() != INT_SETTINGS || factions.Count() != 2)
  {
   return false;
  }
  for (int key = 0; key < INT_SETTINGS; key++)
  {
   if (!ValidSetting(key, values[key]))
   {
    return false;
   }
  }
  if (values[KEY_SQUADS_MAX] < values[KEY_SQUADS_MIN] || factions[0].Length() > 64 || factions[1].Length() > 64)
  {
   return false;
  }
  for (int index = 0; index < INT_SETTINGS; index++) { StoreSetting(index, values[index]); }
  m_sFaction = factions[0];
  m_sSecondFaction = factions[1];
  Normalize(-1);
  Replication.BumpMe();
  OnRadiusReplicated();
  return true;
 }

 // The GM-side cache settings of the zone's garrisons.
 EXPG_GarrisonSpawnSettings CacheSettings()
 {
  EXPG_GarrisonSpawnSettings settings = new EXPG_GarrisonSpawnSettings();
  settings.CacheMode = m_iCacheMode;
  settings.WakeDistance = m_iWake;
  settings.SleepDistance = m_iSleep;
  return settings;
 }

 bool IsSquadPrefabListed()
 {
  return m_aSquadPrefabs && !m_aSquadPrefabs.IsEmpty();
 }

 // Replication callback; the authority calls it directly.
 void OnRadiusReplicated()
 {
  EXPG_RandomGarrisonAreaComponent area = EXPG_RandomGarrisonAreaComponent.Cast(FindComponent(EXPG_RandomGarrisonAreaComponent));
  if (area) { area.Refresh(); }
 }

 int GetRadius()
 {
  return m_iRadius;
 }

 //------------------------------------------------------------------------------------------------
 // Token and saved state
 //------------------------------------------------------------------------------------------------
 string Token()
 {
  return m_sToken;
 }

 int TokenHi()
 {
  return m_iTokenHi;
 }

 int TokenLo()
 {
  return m_iTokenLo;
 }

 // Attribute saves (CDF): token halves and generated | last seed << 1.
 vector SavedState()
 {
  int flags = m_iLastSeed * 2;
  if (IsGenerated()) { flags += 1; }
  return Vector(m_iTokenHi, m_iTokenLo, flags);
 }

 // A loaded zone never resumes by itself: Stopped when it had placed a building,
 // else Idle. Its garrisons are found again by the token.
 bool RestoreSaved(int tokenHi, int tokenLo, bool generated, int lastSeed, array<string> outcomes)
 {
  if (!Replication.IsServer() || EXPG_RGRules.Token(tokenHi, tokenLo).IsEmpty())
  {
   return false;
  }
  if (m_iState == STATE_CENSUS || m_iState == STATE_CATALOG || m_iState == STATE_RUNNING || m_iState == STATE_CLEARING) { StopWork("a save was loaded"); }
  m_iTokenHi = tokenHi;
  m_iTokenLo = tokenLo;
  m_sToken = EXPG_RGRules.Token(tokenHi, tokenLo);
  m_bGenerated = generated;
  m_iLastSeed = Math.ClampInt(lastSeed, 0, 9999);
  m_aOutcomes.Clear();
  if (outcomes)
  {
   foreach (string outcome : outcomes)
   {
    if (m_aOutcomes.Count() < MAX_OUTCOMES && outcome.Length() <= 1024) { m_aOutcomes.Insert(outcome); }
   }
  }
  m_bLoaded = true;
  m_bSettingsChanged = false;
  m_iState = STATE_IDLE;
  if (m_bGenerated) { m_iState = STATE_STOPPED; }
  m_sStopReason = "loaded from a save";
  PrintFormat("[EXPG RANDOM] zone %1 restored: generated=%2 seed=%3 outcomes=%4", m_sToken, m_bGenerated, m_iLastSeed, m_aOutcomes.Count());
  EXPG_RandomGarrisonDirector.Wake();
  return true;
 }

 bool RestoreSavedVector(vector saved)
 {
  int tokenHi = Math.Round(saved[0]);
  int tokenLo = Math.Round(saved[1]);
  int flags = Math.Round(saved[2]);
  bool generated = (flags & 1) != 0;
  int lastSeed = flags >> 1;
  return RestoreSaved(tokenHi, tokenLo, generated, lastSeed, null);
 }

 // True once any squad of this zone has taken its posts, also while the zone is still
 // running: the garrison ledger saves exactly those garrisons (Ready and unfinished,
 // with this token), so a save made during a run must say generated.
 bool IsGenerated()
 {
  if (m_bGenerated)
  {
   return true;
  }
  EXPG_GarrisonManager manager = EXPG_GarrisonManager.Get();
  if (!manager)
  {
   return false;
  }
  array<ref EXPG_GarrisonRecord> records = {};
  manager.CollectGenerated(m_sToken, records);
  foreach (EXPG_GarrisonRecord record : records)
  {
   if (record && record.Ready)
   {
    return true;
   }
  }
  return false;
 }

 int LastSeed()
 {
  return m_iLastSeed;
 }

 void GetOutcomes(notnull array<string> outOutcomes)
 {
  outOutcomes.Clear();
  foreach (string outcome : m_aOutcomes) { outOutcomes.Insert(outcome); }
 }

 //------------------------------------------------------------------------------------------------
 // Accessors (status, fixtures)
 //------------------------------------------------------------------------------------------------
 int State()
 {
  return m_iState;
 }

 string GetStatus()
 {
  return m_sStatus;
 }

 int EligibleCount()
 {
  return m_aSites.Count();
 }

 int ExcludedByPlayers()
 {
  return m_iExcludedPlayers;
 }

 int OrphansDeleted()
 {
  return m_iOrphans;
 }

 int TargetCount()
 {
  return m_iTarget;
 }

 int PendingCount()
 {
  return m_aPending.Count();
 }

 int RetireCount()
 {
  return m_Retire.Count();
 }

 // Buildings that took at least one squad, and their squads, in sorted building order.
 int PlacedSites(notnull array<IEntity> outStructures, notnull array<string> outPrefabs)
 {
  outStructures.Clear();
  outPrefabs.Clear();
  foreach (EXPG_RGSite site : m_aSites)
  {
   if (site.Placed < 1) { continue; }
   outStructures.Insert(site.Structure);
   string prefabs;
   foreach (string prefab : site.Prefabs) { prefabs += prefab + ";"; }
   outPrefabs.Insert(prefabs);
  }
  return outStructures.Count();
 }

 // Buildings within the player distance of these positions are not chosen. Test seam:
 // players' characters (or their vehicles); Game Master cameras never count.
 protected void ObserverPositions(notnull array<vector> positions)
 {
  if (!GetGame()) { return; }
  PlayerManager players = GetGame().GetPlayerManager();
  if (!players) { return; }
  array<int> ids = {};
  players.GetPlayers(ids);
  foreach (int id : ids)
  {
   IEntity controlled = players.GetPlayerControlledEntity(id);
   if (!controlled) { continue; }
   SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(controlled);
   if (character)
   {
    CharacterControllerComponent controller = character.GetCharacterController();
    if (controller && controller.IsDead()) { continue; }
   }
   positions.Insert(controlled.GetOrigin());
  }
 }

 protected void RefreshObservers(float now, bool force = false)
 {
  if (!force && now < m_fNextObservers) { return; }
  m_fNextObservers = now + OBSERVER_INTERVAL;
  m_aObservers.Clear();
  ObserverPositions(m_aObservers);
 }

 // The building's bounds plus the margin where an added squad spawns beside it
 // (EXPG_GarrisonSpawner.ReinforcementSpawn), against every player position.
 protected bool NearPlayers(EXPG_RGSite site)
 {
  if (m_iRunPlayerDistance <= 0 || !site.Structure)
  {
   return false;
  }
  vector mins, maxs;
  site.Structure.GetWorldBounds(mins, maxs);
  mins = mins - Vector(SPAWN_MARGIN, SPAWN_MARGIN, SPAWN_MARGIN);
  maxs = maxs + Vector(SPAWN_MARGIN, SPAWN_MARGIN, SPAWN_MARGIN);
  foreach (vector point : m_aObservers)
  {
   if (EXPG_RGRules.BoxDistance(point, mins, maxs) < m_iRunPlayerDistance)
   {
    return true;
   }
  }
  return false;
 }

 //------------------------------------------------------------------------------------------------
 // Actions
 //------------------------------------------------------------------------------------------------
 // Server: a confirmed Game Master action runs after every other attribute of the same
 // Save has landed.
 void QueueRun(int action, int playerId)
 {
  if (!Replication.IsServer() || action <= ACTION_NONE || action > ACTION_STOP)
  {
   return;
  }
  m_iQueuedAction = action;
  m_iQueuedPlayer = playerId;
  if (m_bRunQueued)
  {
   return;
  }
  m_bRunQueued = true;
  GetGame().GetCallqueue().CallLater(RunQueued, 0, false);
 }

 protected void RunQueued()
 {
  m_bRunQueued = false;
  Run(m_iQueuedAction, m_iQueuedPlayer);
 }

 protected void AutoGenerate()
 {
  if (!m_bRegistered || m_bGenerated || m_bLoaded || m_iState != STATE_IDLE)
  {
   return;
  }
  string why;
  if (EXPG_GarrisonSpawner.Paused(why) && m_iAutoTries < 20)
  {
   m_iAutoTries++;
   GetGame().GetCallqueue().CallLater(AutoGenerate, 15000, false);
   return;
  }
  Print("[EXPG RANDOM] zone " + m_sToken + ": Generate at mission start");
  Run(ACTION_GENERATE, -1);
 }

 // Server. False (with the reason in the status, and told to the Game Master) when refused.
 bool Run(int action, int playerId)
 {
  if (!Replication.IsServer() || !m_bRegistered)
  {
   return false;
  }
  PrintFormat("[EXPG RANDOM] zone %1: action %2 by player %3 in state %4", m_sToken, action, playerId, m_iState);
  if (action == ACTION_GENERATE)
  {
   if (m_iState == STATE_CENSUS || m_iState == STATE_CATALOG || m_iState == STATE_RUNNING || m_iState == STATE_CLEARING)
   {
    Refuse("Busy: use Stop first", playerId);
    return false;
   }
   if (m_bGenerated)
   {
    Refuse("Already generated: use Regenerate or Clear", playerId);
    return false;
   }
   BeginGenerate(playerId);
   return true;
  }
  if (action == ACTION_REGENERATE)
  {
   m_iRunPlayer = playerId;
   BeginClear(true);
   return true;
  }
  if (action == ACTION_CLEAR)
  {
   BeginClear(false);
   return true;
  }
  if (action == ACTION_STOP)
  {
   if (m_iState == STATE_CLEARING)
   {
    Refuse("Clearing: wait until it has finished", playerId);
    return false;
   }
   if (m_iState != STATE_CENSUS && m_iState != STATE_CATALOG && m_iState != STATE_RUNNING)
   {
    Refuse("Nothing to stop", playerId);
    return false;
   }
   StopGeneration("stopped by the Game Master");
   return true;
  }
  return false;
 }

 protected void Refuse(string message, int playerId)
 {
  m_sNotice = message;
  m_fNoticeUntil = Now() + NOTICE_SECONDS;
  m_fNextStatus = 0;
  PrintFormat("[EXPG RANDOM] zone %1 refused: %2", m_sToken, message);
  if (playerId > 0)
  {
   SCR_EditorManagerCore core = SCR_EditorManagerCore.Cast(SCR_EditorManagerCore.GetInstance(SCR_EditorManagerCore));
   SCR_EditorManagerEntity editor;
   if (core) { editor = core.GetEditorManager(playerId); }
   if (editor) { editor.EXPG_Notice("Random Garrison: " + message + "."); }
  }
  EXPG_RandomGarrisonDirector.Wake();
 }

 protected float Now()
 {
  if (!GetGame() || !GetGame().GetWorld())
  {
   return 0;
  }
  return GetGame().GetWorld().GetWorldTime() * 0.001;
 }

 protected void ResetWork()
 {
  m_Census = null;
  m_Explicit = null;
  m_aSites.Clear();
  m_aOrder.Clear();
  m_aActive.Clear();
  m_aOutcomes.Clear();
  m_iTarget = 0;
  m_iAttempts = 0;
  m_iAttemptCap = 0;
  m_iOrderCursor = 0;
  m_iSpawnCursor = 0;
  m_iAnalysed = 0;
  m_iExcludedPlayers = 0;
  m_iExcludedTaken = 0;
  m_iFailures = 0;
  m_iOrphans = 0;
  m_iSpawned = 0;
  m_iDeferred = 0;
  m_fAILimitedSince = -1;
  m_bPaused = false;
  m_bPlanWait = false;
  m_sPause = "";
  m_sAILimit = "";
  m_sStopReason = "";
  m_sCatalogProblem = "";
 }

 protected void BeginGenerate(int playerId)
 {
  ResetWork();
  m_iRunPlayer = playerId;
  m_bLoaded = false;
  m_bSettingsChanged = false;
  m_iLastSeed = m_iSeed;
  if (m_iLastSeed <= 0) { m_iLastSeed = Math.RandomInt(1, 10000); }
  // This generation's inputs: settings edited from here on wait for Regenerate.
  m_sRunFaction = ResolveFaction(0);
  m_sRunSecondFaction = ResolveFaction(1);
  m_iRunSizes = m_iSizes;
  m_iRunSquadsMin = m_iSquadsMin;
  m_iRunSquadsMax = m_iSquadsMax;
  m_iRunPlayerDistance = m_iPlayerDistance;
  m_iRunBuildings = m_iBuildings;
  m_iRunShare = m_iShare;
  m_bRunExcludeSupport = m_bExcludeSupport;
  m_bRunAllowGarrisoned = m_bAllowGarrisoned;
  m_Census = new EXPG_RandomGarrisonCensus();
  m_Census.Begin(GetOrigin(), m_iRadius);
  m_iState = STATE_CENSUS;
  m_fNextStatus = 0;
  PrintFormat("[EXPG RANDOM] zone %1 generate: radius=%2 buildings=%3 share=%4 squads=%5-%6 sizes=%7 seed=%8 faction=%9", m_sToken, m_iRadius, m_iBuildings, m_iShare, m_iSquadsMin, m_iSquadsMax, m_iSizes, m_iLastSeed, m_sRunFaction);
  EXPG_RandomGarrisonDirector.Wake();
 }

 // Regenerate: Clear, wait until it has finished, then Generate.
 protected void BeginClear(bool regenerate)
 {
  StopWork("cleared");
  m_aClear.Clear();
  m_aSettingsQueue.Clear();
  EXPG_GarrisonManager manager = EXPG_GarrisonManager.Get();
  if (manager) { manager.CollectGenerated(m_sToken, m_aClear); }
  m_iClearTotal = m_aClear.Count();
  m_bRegenerate = regenerate;
  m_iState = STATE_CLEARING;
  m_fNextStatus = 0;
  PrintFormat("[EXPG RANDOM] zone %1 clear: %2 garrisons (regenerate %3)", m_sToken, m_iClearTotal, regenerate);
  EXPG_RandomGarrisonDirector.Wake();
 }

 // Stop: pending analyses are cancelled and squads that never deployed are deleted;
 // deployed garrisons stay.
 protected void StopGeneration(string reason)
 {
  StopWork(reason);
  BuildOutcomes();
  m_bGenerated = PlacedCount() > 0;
  m_sStopReason = reason;
  m_iState = STATE_STOPPED;
  m_fNextStatus = 0;
  PrintFormat("[EXPG RANDOM] zone %1 stopped (%2): buildings=%3 squads=%4 orphans=%5", m_sToken, reason, PlacedCount(), SquadCount(), m_iOrphans);
 }

 // Legacy CDF Prepare for Save (EXPG_RandomGarrisonDirector): nothing pending is left.
 void AbortForSave()
 {
  if (m_iState == STATE_CENSUS || m_iState == STATE_CATALOG || m_iState == STATE_RUNNING)
  {
   StopGeneration("save preparation");
  }
 }

 protected void StopWork(string reason)
 {
  m_bStopping = true;
  EXPG_GarrisonManager manager = EXPG_GarrisonManager.Get();
  foreach (EXPG_RGPending pending : m_aPending)
  {
   if (PendingReady(pending))
   {
    ReturnFlagged(pending);
    if (pending.Site)
    {
     pending.Site.Placed++;
     pending.Site.Prefabs.Insert(pending.Prefab);
    }
    continue;
   }
   RetirePending(pending, m_Retire);
  }
  m_aPending.Clear();
  foreach (EXPG_RGSite site : m_aActive)
  {
   if (!site) { continue; }
   if (site.Analysis && manager) { manager.StopWaiting(site.Analysis); }
   site.Analysis = null;
   EXPG_RandomGarrisonDirector.ReleaseClaim(site.Structure, m_iZoneId);
   site.Note = reason;
   if (site.Placed > 0) { site.Stage = EXPG_RGSite.DONE; }
   else { site.Stage = EXPG_RGSite.SKIPPED; }
  }
  m_aActive.Clear();
  m_bStopping = false;
 }

 //------------------------------------------------------------------------------------------------
 // Director step (server)
 //------------------------------------------------------------------------------------------------
 bool Step(float now, EXPG_RGBudget budget)
 {
  bool busy = false;
  if (!m_Retire.IsEmpty())
  {
   RetireSome(budget);
   busy = true;
  }
  if (m_iState == STATE_CENSUS)
  {
   StepCensus(now, budget);
   busy = true;
  }
  else if (m_iState == STATE_CATALOG)
  {
   StepCatalog(now, budget);
   busy = true;
  }
  else if (m_iState == STATE_RUNNING)
  {
   StepRunning(now, budget);
   busy = true;
  }
  else if (m_iState == STATE_CLEARING)
  {
   StepClearing(now);
   busy = true;
  }
  if (!m_aSettingsQueue.IsEmpty())
  {
   ApplyQueuedSettings();
   busy = true;
  }
  PublishStatus(now);
  return busy;
 }

 protected void RetireSome(EXPG_RGBudget budget)
 {
  budget.Deletes -= m_Retire.DeleteSome(budget.Deletes);
 }

 protected void ApplyQueuedSettings()
 {
  EXPG_GarrisonSpawnSettings settings = CacheSettings();
  int applied;
  while (!m_aSettingsQueue.IsEmpty() && applied < SETTINGS_PER_TICK)
  {
   EXPG_GarrisonRecord record = m_aSettingsQueue[0];
   m_aSettingsQueue.RemoveOrdered(0);
   EXPG_GarrisonSpawner.ApplyRecordSettings(record, settings);
   applied++;
  }
 }

 protected void StepCensus(float now, EXPG_RGBudget budget)
 {
  if (!m_Census.Step(budget.Deadline))
  {
   return;
  }
  BuildSites();
  m_Census = null;
  m_iTarget = EXPG_RGRules.Target(m_iRunBuildings, m_iRunShare, m_aSites.Count());
  if (m_aSites.IsEmpty())
  {
   FinishGeneration("no eligible building in the radius");
   return;
  }
  if (IsSquadPrefabListed())
  {
   m_Explicit = new EXPG_SquadCatalog();
   m_Explicit.BeginExplicit(m_aSquadPrefabs);
  }
  m_iState = STATE_CATALOG;
 }

 // Eligible buildings in a stable order: sorted by position (centimetres) and prefab.
 protected void BuildSites()
 {
  m_aSites.Clear();
  array<string> keys = {};
  map<string, IEntity> byKey = new map<string, IEntity>();
  foreach (IEntity entity : m_Census.Eligible)
  {
   if (!entity) { continue; }
   string prefab;
   if (entity.GetPrefabData()) { prefab = entity.GetPrefabData().GetPrefabName(); }
   string key = EXPG_RGRules.SortKey(entity.GetOrigin(), prefab);
   if (byKey.Contains(key)) { continue; }
   byKey.Insert(key, entity);
   keys.Insert(key);
  }
  keys.Sort();
  foreach (string sorted : keys)
  {
   IEntity structure = byKey.Get(sorted);
   EXPG_RGSite site = new EXPG_RGSite();
   site.Structure = structure;
   site.Key = sorted;
   site.Origin = structure.GetOrigin();
   if (structure.GetPrefabData()) { site.PrefabName = structure.GetPrefabData().GetPrefabName(); }
   m_aSites.Insert(site);
  }
 }

 protected EXPG_SquadCatalog CatalogFor(string factionId)
 {
  if (m_Explicit)
  {
   return m_Explicit;
  }
  if (factionId.IsEmpty())
  {
   return null;
  }
  return EXPG_SquadPool.Get(factionId);
 }

 protected void StepCatalog(float now, EXPG_RGBudget budget)
 {
  array<EXPG_SquadCatalog> catalogs = {};
  if (m_Explicit) { catalogs.Insert(m_Explicit); }
  else
  {
   string first = m_sRunFaction;
   if (first.IsEmpty())
   {
    StopGeneration("no military faction with a squad catalog in this mission");
    return;
   }
   catalogs.Insert(EXPG_SquadPool.Get(first));
   string second = m_sRunSecondFaction;
   if (!second.IsEmpty()) { catalogs.Insert(EXPG_SquadPool.Get(second)); }
  }
  foreach (EXPG_SquadCatalog catalog : catalogs)
  {
   if (!catalog.Step(budget.Deadline))
   {
    return;
   }
  }
  // Every faction a building can draw needs a squad of an enabled size (the budget is
  // checked per building); otherwise each building would be analysed only to fail.
  m_sCatalogProblem = "";
  array<string> drawable = {};
  drawable.Insert(m_sRunFaction);
  string drawSecond = m_sRunSecondFaction;
  if (!drawSecond.IsEmpty()) { drawable.Insert(drawSecond); }
  foreach (string drawFaction : drawable)
  {
   EXPG_SquadCatalog source = CatalogFor(drawFaction);
   if (source && source.CountUsable(m_iRunSizes, m_bRunExcludeSupport, drawFaction) > 0) { continue; }
   if (source && !source.Problem.IsEmpty()) { m_sCatalogProblem = source.Problem; }
   else if (m_Explicit) { m_sCatalogProblem = string.Format("the module's squad prefab list has no squad of faction %1 in the chosen sizes", drawFaction); }
   else { m_sCatalogProblem = string.Format("faction %1 has no squad of the chosen sizes", drawFaction); }
   if (m_bRunExcludeSupport && (!source || source.Problem.IsEmpty())) { m_sCatalogProblem += " (support squads excluded)"; }
   StopGeneration(m_sCatalogProblem);
   return;
  }
  Select();
  m_iState = STATE_RUNNING;
  PrintFormat("[EXPG RANDOM] zone %1 selection: eligible=%2 target=%3 queued=%4 excludedPlayers=%5 excludedTaken=%6", m_sToken, m_aSites.Count(), m_iTarget, m_aActive.Count(), m_iExcludedPlayers, m_iExcludedTaken);
 }

 // The seeded shuffle of the sorted list; exclusions are skipped after it, so the
 // order of the rest never changes. The first Target available buildings are queued,
 // the others are reserves.
 protected void Select()
 {
  array<int> order = {};
  for (int i = 0; i < m_aSites.Count(); i++) { order.Insert(i); }
  EXPG_RGRules.Shuffle(order, m_iLastSeed);
  m_aOrder.Clear();
  foreach (int index : order) { m_aOrder.Insert(m_aSites[index]); }
  m_iOrderCursor = 0;
  m_iAttempts = 0;
  int extra = m_iTarget;
  if (extra < 8) { extra = 8; }
  m_iAttemptCap = m_iTarget + extra;
  // Every eligible building near a player is left out (and counted) first.
  RefreshObservers(Now(), true);
  foreach (EXPG_RGSite site : m_aSites)
  {
   if (!NearPlayers(site)) { continue; }
   site.Stage = EXPG_RGSite.SKIPPED;
   site.Note = string.Format("a player was within %1 m", m_iRunPlayerDistance);
   m_iExcludedPlayers++;
  }
  int queued;
  while (queued < m_iTarget && PromoteNext()) { queued++; }
 }

 // Queues the next reserve that is free and away from players. False when none is left.
 protected bool PromoteNext()
 {
  if (m_bStopping || m_iAttempts >= m_iAttemptCap)
  {
   return false;
  }
  EXPG_GarrisonManager manager = EXPG_GarrisonManager.Get();
  while (m_iOrderCursor < m_aOrder.Count())
  {
   EXPG_RGSite site = m_aOrder[m_iOrderCursor];
   m_iOrderCursor++;
   if (!site || site.Stage != EXPG_RGSite.RESERVE) { continue; }
   string taken = Unavailable(site, manager);
   if (!taken.IsEmpty())
   {
    site.Stage = EXPG_RGSite.SKIPPED;
    site.Note = taken;
    m_iExcludedTaken++;
    continue;
   }
   if (NearPlayers(site))
   {
    // A player came near since the selection: this reserve waits for no one.
    site.Stage = EXPG_RGSite.SKIPPED;
    site.Note = string.Format("a player was within %1 m", m_iRunPlayerDistance);
    continue;
   }
   if (!EXPG_RandomGarrisonDirector.Claim(site.Structure, m_iZoneId))
   {
    site.Stage = EXPG_RGSite.SKIPPED;
    site.Note = "claimed by another Random Garrison zone";
    m_iExcludedTaken++;
    continue;
   }
   Draw(site);
   site.Stage = EXPG_RGSite.QUEUED;
   m_aActive.Insert(site);
   m_iAttempts++;
   return true;
  }
  return false;
 }

 // Why the building cannot be taken now (empty: it can).
 protected string Unavailable(EXPG_RGSite site, EXPG_GarrisonManager manager)
 {
  if (!site.Structure || site.Structure.IsDeleted())
  {
   return "the building no longer exists";
  }
  if (manager && manager.HasGarrison(site.Structure))
  {
   if (!m_bRunAllowGarrisoned)
   {
    return "already garrisoned";
   }
   // One faction per building: only a building held by one of this zone's factions
   // (the one it drew, once drawn) takes more squads. A zone without a faction (squad
   // prefab list, no military faction) takes the faction already there.
   array<string> held = {};
   manager.GarrisonFactions(site.Structure, held);
   if (held.Count() > 1)
   {
    return "garrisoned by several factions";
   }
   if (held.Count() == 1)
   {
    string heldKey = held[0];
    if (site.Drawn && !site.FactionId.IsEmpty() && site.FactionId != heldKey)
    {
     return "garrisoned by another faction (" + heldKey + ")";
    }
    if (!m_sRunFaction.IsEmpty() && heldKey != m_sRunFaction && heldKey != m_sRunSecondFaction)
    {
     return "garrisoned by another faction (" + heldKey + ")";
    }
   }
  }
  if (EXPG_RandomGarrisonDirector.ClaimedByOther(site.Structure, m_iZoneId))
  {
   return "claimed by another Random Garrison zone";
  }
  return string.Empty;
 }

 // The building's own generator draws its faction and its squad count.
 protected void Draw(EXPG_RGSite site)
 {
  if (site.Drawn) { return; }
  site.Drawn = true;
  site.Rng = new RandomGenerator();
  site.Rng.SetSeed(EXPG_RGRules.SiteSeed(m_iLastSeed, site.Key));
  site.FactionId = m_sRunFaction;
  string second = m_sRunSecondFaction;
  int roll = site.Rng.RandInt(0, 2);
  if (!second.IsEmpty() && roll == 1) { site.FactionId = second; }
  site.Wanted = site.Rng.RandInt(m_iRunSquadsMin, m_iRunSquadsMax + 1);
  // A garrisoned building (Allow garrisoned) keeps the faction already in it;
  // Unavailable has checked that it is one of this zone's factions.
  EXPG_GarrisonManager manager = EXPG_GarrisonManager.Get();
  array<string> held = {};
  if (manager && manager.GarrisonFactions(site.Structure, held) == 1) { site.FactionId = held[0]; }
 }

 protected void StepRunning(float now, EXPG_RGBudget budget)
 {
  EXPG_GarrisonManager manager = EXPG_GarrisonManager.Get();
  if (!manager) { return; }
  RefreshObservers(now);
  ServicePending(now, manager);
  m_bPaused = EXPG_GarrisonSpawner.Paused(m_sPause);
  ServiceAnalyses(now, manager);
  if (!m_bPaused) { ServiceSpawns(now, manager); }
  if (AllSettled()) { FinishGeneration(""); }
 }

 protected bool PendingReady(EXPG_RGPending pending)
 {
  return pending.Record && pending.Record.Ready && !pending.Record.Finished;
 }

 protected bool HasPending(EXPG_RGSite site)
 {
  foreach (EXPG_RGPending pending : m_aPending)
  {
   if (pending.Site == site)
   {
    return true;
   }
  }
  return false;
 }

 protected void ServicePending(float now, EXPG_GarrisonManager manager)
 {
  for (int i = m_aPending.Count() - 1; i >= 0; i--)
  {
   EXPG_RGPending pending = m_aPending[i];
   EXPG_RGSite site = pending.Site;
   if (PendingReady(pending))
   {
    ReturnFlagged(pending);
    m_aPending.RemoveOrdered(i);
    if (!site) { continue; }
    site.Placed++;
    site.Prefabs.Insert(pending.Prefab);
    PrintFormat("[EXPG RANDOM] zone %1: %2 deployed in %3 (%4/%5)", m_sToken, pending.Prefab, site.PrefabName, site.Placed, site.Wanted);
    if (site.Placed >= site.Wanted) { CompleteSite(site, ""); }
    continue;
   }
   bool failed = !pending.Record || pending.Record.Finished || !pending.Squad || now - pending.Started > PENDING_TIMEOUT;
   if (!failed)
   {
    KeepOut(pending, false);
    continue;
   }
   m_aPending.RemoveOrdered(i);
   if (EXPG_RandomGarrisonDirector.SavePreparing())
   {
    // Prepare for Save releases garrisons in legacy mode: the squad is an ordinary one now.
    ReturnFlagged(pending);
    PrintFormat("[EXPG RANDOM] zone %1: squad %2 forgotten during save preparation", m_sToken, pending.Squad);
    if (site) { EndSite(site, "save preparation"); }
    continue;
   }
   // An orphan: it never deployed. Deleting its squad lets the manager finish the record.
   string why = "it never took its posts";
   if (pending.Record && !pending.Record.ReleaseReason.IsEmpty()) { why = pending.Record.ReleaseReason; }
   RetirePending(pending, m_Retire);
   m_iOrphans++;
   PrintFormat("[EXPG RANDOM] zone %1: squad %2 (%3) deleted: %4", m_sToken, pending.Squad, pending.Prefab, why);
   if (!site) { continue; }
   if (!site.Retried)
   {
    site.Retried = true;
    site.RetryBelow = pending.Bucket;
    continue;
   }
   EndSite(site, "a squad never took its posts: " + why);
  }
 }

 protected void ServiceAnalyses(float now, EXPG_GarrisonManager manager)
 {
  int running;
  for (int i = m_aActive.Count() - 1; i >= 0; i--)
  {
   EXPG_RGSite site = m_aActive[i];
   if (!site || site.Stage != EXPG_RGSite.ANALYSING) { continue; }
   EXPG_RGAnalysis analysis = site.Analysis;
   if (!analysis)
   {
    EndSite(site, "its analysis was lost");
    continue;
   }
   if (analysis.Ready)
   {
    site.Stage = EXPG_RGSite.READY;
    NoteAnalysed(site);
    continue;
   }
   if (analysis.Failed)
   {
    NoteAnalysed(site);
    EndSite(site, analysis.Failure);
    continue;
   }
   running++;
  }
  m_bPlanWait = false;
  if (m_bPaused)
  {
   return;
  }
  while (running < MAX_ANALYSES_PER_ZONE && EXPG_RandomGarrisonDirector.AnalysesRunning() < EXPG_RandomGarrisonDirector.MAX_ANALYSES)
  {
   EXPG_RGSite next = NextQueued();
   if (!next) { break; }
   if (!manager.FindPlan(next.Structure) && manager.PlanCount() >= PLAN_HEADROOM)
   {
    m_bPlanWait = true;
    break;
   }
   if (StartAnalysis(next, manager)) { running++; }
  }
 }

 protected void NoteAnalysed(EXPG_RGSite site)
 {
  if (site.Analysed) { return; }
  site.Analysed = true;
  m_iAnalysed++;
 }

 protected EXPG_RGSite NextQueued()
 {
  foreach (EXPG_RGSite site : m_aActive)
  {
   if (site && site.Stage == EXPG_RGSite.QUEUED)
   {
    return site;
   }
  }
  return null;
 }

 // True while the analysis runs (false: ready at once, or failed and ended).
 protected bool StartAnalysis(EXPG_RGSite site, EXPG_GarrisonManager manager)
 {
  string taken = Unavailable(site, manager);
  if (!taken.IsEmpty() && site.Placed == 0)
  {
   SkipSite(site, taken);
   return false;
  }
  EXPG_RGAnalysis analysis = new EXPG_RGAnalysis();
  analysis.Structure = site.Structure;
  site.Analysis = analysis;
  site.Stage = EXPG_RGSite.ANALYSING;
  string reason;
  if (!manager.Wait(analysis, reason))
  {
   NoteAnalysed(site);
   EndSite(site, reason);
   return false;
  }
  if (analysis.Ready)
  {
   NoteAnalysed(site);
   site.Stage = EXPG_RGSite.READY;
   return false;
  }
  if (analysis.Failed)
  {
   NoteAnalysed(site);
   EndSite(site, analysis.Failure);
   return false;
  }
  return true;
 }

 int AnalysesRunning()
 {
  int running;
  foreach (EXPG_RGSite site : m_aActive)
  {
   if (site && site.Stage == EXPG_RGSite.ANALYSING) { running++; }
  }
  return running;
 }

 // Squads of this zone still spawning their soldiers (shared limit of two).
 int SpawningSquads()
 {
  int spawning;
  foreach (EXPG_RGPending pending : m_aPending)
  {
   if (pending.Squad && !pending.Squad.EBG_HasCompletedInitialSpawn()) { spawning++; }
  }
  return spawning;
 }

 protected void ServiceSpawns(float now, EXPG_GarrisonManager manager)
 {
  m_iDeferred = 0;
  int count = m_aActive.Count();
  if (count == 0)
  {
   return;
  }
  // Plans of buildings still to spawn stay in use (the manager evicts unused plans).
  foreach (EXPG_RGSite kept : m_aActive)
  {
   if (!kept || kept.Stage != EXPG_RGSite.READY) { continue; }
   EXPG_BuildingPlan keptPlan = manager.FindPlan(kept.Structure);
   if (keptPlan) { keptPlan.LastUsed = manager.Now(); }
  }
  bool canSpawn = EXPG_RandomGarrisonDirector.CanSpawn(now);
  for (int visit = 0; visit < count; visit++)
  {
   int at = (m_iSpawnCursor + visit) % count;
   if (at >= m_aActive.Count()) { break; }
   EXPG_RGSite site = m_aActive[at];
   if (!site || site.Stage != EXPG_RGSite.READY || HasPending(site)) { continue; }
   if (site.Placed >= site.Wanted)
   {
    CompleteSite(site, "");
    continue;
   }
   EXPG_BuildingPlan plan = manager.FindPlan(site.Structure);
   if (!site.Structure || site.Structure.IsDeleted())
   {
    EndSite(site, "the building no longer exists");
    continue;
   }
   if (!plan || !plan.Done || !plan.Valid())
   {
    // The plan was evicted or the building moved: analysed once more, then given up.
    if (!site.Reanalysed)
    {
     site.Reanalysed = true;
     site.Stage = EXPG_RGSite.QUEUED;
     continue;
    }
    EndSite(site, "its analysis was discarded");
    continue;
   }
   if (!plan.Error.IsEmpty())
   {
    EndSite(site, plan.Error);
    continue;
   }
   if (NearPlayers(site))
   {
    m_iDeferred++;
    if (site.DeferredSince < 0) { site.DeferredSince = now; }
    if (now - site.DeferredSince > PLAYER_DEFER_LIMIT) { SkipSite(site, string.Format("players stayed within %1 m for two minutes", m_iRunPlayerDistance)); }
    continue;
   }
   site.DeferredSince = -1;
   if (!canSpawn) { continue; }
   // One faction per building: another faction may have garrisoned it meanwhile.
   array<string> present = {};
   manager.GarrisonFactions(site.Structure, present);
   // A zone without a faction (squad prefab list) keeps the faction its first squad brought.
   if (site.FactionId.IsEmpty() && present.Count() == 1) { site.FactionId = present[0]; }
   if (present.Count() > 1 || (present.Count() == 1 && present[0] != site.FactionId))
   {
    SkipSite(site, "garrisoned by another faction");
    continue;
   }
   string why;
   EXPG_SquadEntry entry = PickSquad(site, plan, manager, why);
   if (!entry)
   {
    if (site.Placed > 0) { CompleteSite(site, "fewer squads: " + why); }
    else { EndSite(site, why); }
    continue;
   }
   string limit;
   if (!EXPG_GarrisonSpawner.AIHeadroom(entry.Members, entry.FactionId, limit))
   {
    WaitForAILimit(now, limit);
    return;
   }
   m_fAILimitedSince = -1;
   m_iSpawnCursor = (at + 1) % count;
   SpawnSquad(site, entry, now, manager);
   return;
  }
 }

 protected void WaitForAILimit(float now, string limit)
 {
  m_sAILimit = limit;
  if (m_fAILimitedSince < 0) { m_fAILimitedSince = now; }
  if (now - m_fAILimitedSince > AI_LIMIT_WAIT) { StopGeneration("the AI limit was reached (" + limit + ")"); }
 }

 // The building's squad budget is its planned posts minus the soldiers already there;
 // a bucket among the enabled ones with a squad that fits, then a squad of it, both
 // from the building's own generator. Fresh squads are never trimmed.
 protected EXPG_SquadEntry PickSquad(EXPG_RGSite site, EXPG_BuildingPlan plan, EXPG_GarrisonManager manager, out string why)
 {
  why = "too small for the chosen squad sizes";
  int budget = plan.Slots.Count() - manager.AssignedSoldiers(site.Structure);
  if (budget < 1)
  {
   return null;
  }
  EXPG_SquadCatalog catalog = CatalogFor(site.FactionId);
  if (!catalog || !catalog.Ready)
  {
   why = "no squad catalog for faction " + site.FactionId;
   return null;
  }
  array<int> buckets = {};
  array<int> all = {EXPG_RGRules.BUCKET_TEAM, EXPG_RGRules.BUCKET_FIRETEAM, EXPG_RGRules.BUCKET_SQUAD, EXPG_RGRules.BUCKET_LARGE};
  foreach (int bucket : all)
  {
   if ((m_iRunSizes & bucket) == 0 || bucket >= site.RetryBelow) { continue; }
   if (catalog.Fits(bucket, budget, m_bRunExcludeSupport, site.FactionId)) { buckets.Insert(bucket); }
  }
  if (buckets.IsEmpty())
  {
   // "Too small" only when the budget, not the catalog, is the cause.
   if (catalog.CountUsable(m_iRunSizes, m_bRunExcludeSupport, site.FactionId) == 0) { why = "no squad of the chosen sizes for faction " + site.FactionId; }
   return null;
  }
  int chosen = buckets[site.Rng.RandInt(0, buckets.Count())];
  array<EXPG_SquadEntry> choices = {};
  catalog.Collect(chosen, budget, m_bRunExcludeSupport, site.FactionId, choices);
  if (choices.IsEmpty())
  {
   return null;
  }
  return choices[site.Rng.RandInt(0, choices.Count())];
 }

 protected void SpawnSquad(EXPG_RGSite site, EXPG_SquadEntry entry, float now, EXPG_GarrisonManager manager)
 {
  EXPG_GarrisonSpawnRequest request = new EXPG_GarrisonSpawnRequest();
  request.Structure = site.Structure;
  request.Prefab = entry.Prefab;
  request.Members = entry.Members;
  request.CreatorId = m_iRunPlayer;
  request.GeneratedBy = m_sToken;
  request.FactionId = entry.FactionId;
  if (request.FactionId.IsEmpty()) { request.FactionId = site.FactionId; }
  request.CacheSettings = CacheSettings();
  request.CheckAILimit = true;
  request.DeleteOnRefusal = true;
  string reason;
  SCR_AIGroup squad = EXPG_GarrisonSpawner.Spawn(request, reason);
  EXPG_RandomGarrisonDirector.NoteSpawn(now);
  if (!squad)
  {
   if (request.AILimited)
   {
    WaitForAILimit(now, reason);
    return;
   }
   PrintFormat("[EXPG RANDOM] zone %1: %2 refused in %3: %4", m_sToken, entry.Prefab, site.PrefabName, reason);
   if (site.Placed > 0) { CompleteSite(site, "fewer squads: " + reason); }
   else { EndSite(site, reason); }
   return;
  }
  m_iSpawned++;
  EXPG_RGPending pending = new EXPG_RGPending();
  pending.Squad = squad;
  pending.Record = manager.Find(squad);
  pending.Site = site;
  pending.Prefab = entry.Prefab;
  pending.Members = entry.Members;
  pending.Bucket = entry.Bucket;
  pending.Started = now;
  KeepOut(pending, false);
  m_aPending.Insert(pending);
  PrintFormat("[EXPG RANDOM] zone %1: %2 (%3 soldiers, %4) spawned for %5 at %6", m_sToken, entry.Prefab, entry.Members, request.FactionId, site.PrefabName, site.Origin);
 }

 // The building is done: every squad wanted (or fewer, with the reason) has deployed.
 protected void CompleteSite(EXPG_RGSite site, string note)
 {
  site.Stage = EXPG_RGSite.DONE;
  site.Note = note;
  site.Analysis = null;
  m_aActive.RemoveItem(site);
  EXPG_RandomGarrisonDirector.ReleaseClaim(site.Structure, m_iZoneId);
 }

 // The building ends early: done with fewer squads when one deployed, else failed and
 // the next reserve is queued.
 protected void EndSite(EXPG_RGSite site, string note)
 {
  if (site.Placed > 0)
  {
   CompleteSite(site, "fewer squads: " + note);
   return;
  }
  EXPG_GarrisonManager manager = EXPG_GarrisonManager.Get();
  if (site.Analysis && !site.Analysis.Finished && manager) { manager.StopWaiting(site.Analysis); }
  site.Analysis = null;
  site.Stage = EXPG_RGSite.FAILED;
  site.Note = note;
  m_iFailures++;
  m_aActive.RemoveItem(site);
  EXPG_RandomGarrisonDirector.ReleaseClaim(site.Structure, m_iZoneId);
  PrintFormat("[EXPG RANDOM] zone %1: %2 at %3 failed: %4", m_sToken, site.PrefabName, site.Origin, note);
  PromoteNext();
 }

 // Not taken (players, garrisoned); the next reserve is queued when nothing was placed.
 protected void SkipSite(EXPG_RGSite site, string note)
 {
  if (site.Placed > 0)
  {
   CompleteSite(site, "fewer squads: " + note);
   return;
  }
  EXPG_GarrisonManager manager = EXPG_GarrisonManager.Get();
  if (site.Analysis && !site.Analysis.Finished && manager) { manager.StopWaiting(site.Analysis); }
  site.Analysis = null;
  site.Stage = EXPG_RGSite.SKIPPED;
  site.Note = note;
  m_aActive.RemoveItem(site);
  EXPG_RandomGarrisonDirector.ReleaseClaim(site.Structure, m_iZoneId);
  PromoteNext();
 }

 protected bool AllSettled()
 {
  if (!m_aPending.IsEmpty())
  {
   return false;
  }
  foreach (EXPG_RGSite site : m_aActive)
  {
   if (site && site.Active())
   {
    return false;
   }
  }
  return true;
 }

 protected int PlacedCount()
 {
  int placed;
  foreach (EXPG_RGSite site : m_aSites)
  {
   if (site.Placed > 0) { placed++; }
  }
  return placed;
 }

 protected int SquadCount()
 {
  int squads;
  foreach (EXPG_RGSite site : m_aSites) { squads += site.Placed; }
  return squads;
 }

 protected int SquadsWanted()
 {
  int wanted;
  foreach (EXPG_RGSite site : m_aSites)
  {
   if (site.Placed > 0 || site.Stage == EXPG_RGSite.READY) { wanted += site.Wanted; }
  }
  return wanted;
 }

 protected void FinishGeneration(string note)
 {
  StopWork("finished");
  BuildOutcomes();
  m_bGenerated = PlacedCount() > 0;
  m_sStopReason = note;
  m_iState = STATE_DONE;
  m_fNextStatus = 0;
  PrintFormat("[EXPG RANDOM] zone %1 done: buildings=%2 squads=%3 failed=%4 excludedPlayers=%5 orphans=%6 seed=%7 %8", m_sToken, PlacedCount(), SquadCount(), m_iFailures, m_iExcludedPlayers, m_iOrphans, m_iLastSeed, note);
 }

 // "x;z;prefab;squads;outcome" per building that was tried (at most 32).
 protected void BuildOutcomes()
 {
  m_aOutcomes.Clear();
  foreach (EXPG_RGSite site : m_aSites)
  {
   if (m_aOutcomes.Count() >= MAX_OUTCOMES) { break; }
   if (site.Placed == 0 && site.Stage != EXPG_RGSite.FAILED) { continue; }
   string outcome = "placed";
   if (site.Stage == EXPG_RGSite.FAILED) { outcome = "failed: " + site.Note; }
   else if (!site.Note.IsEmpty() && site.Note != "finished") { outcome = site.Note; }
   m_aOutcomes.Insert(string.Format("%1;%2;%3;%4;%5", Math.Round(site.Origin[0]), Math.Round(site.Origin[2]), site.PrefabName, site.Placed, outcome));
  }
 }

 protected void StepClearing(float now)
 {
  string why;
  if (EXPG_GarrisonSpawner.Paused(why))
  {
   m_sPause = why;
   return;
  }
  int dismissed;
  while (!m_aClear.IsEmpty() && dismissed < DISMISS_PER_TICK)
  {
   EXPG_GarrisonRecord record = m_aClear[0];
   m_aClear.RemoveOrdered(0);
   EXPG_GarrisonSpawner.Dismiss(record, m_Retire, "Cleared by Random Garrison");
   dismissed++;
  }
  if (!m_aClear.IsEmpty() || !m_Retire.IsEmpty())
  {
   return;
  }
  ResetWork();
  m_bGenerated = false;
  m_bLoaded = false;
  m_bSettingsChanged = false;
  m_iState = STATE_IDLE;
  m_fNextStatus = 0;
  PrintFormat("[EXPG RANDOM] zone %1 cleared: %2 garrisons removed", m_sToken, m_iClearTotal);
  if (m_bRegenerate)
  {
   m_bRegenerate = false;
   BeginGenerate(m_iRunPlayer);
  }
 }

 //------------------------------------------------------------------------------------------------
 // Saves: a squad that has not taken its posts is in no save; a Ready garrison saves
 // itself through the garrison ledger (EXPG_SaveExclusion keeps it out of the rest).
 //------------------------------------------------------------------------------------------------
 void KeepPendingOutOfSaves()
 {
  foreach (EXPG_RGPending pending : m_aPending) { KeepOut(pending, true); }
  m_Retire.KeepOut(true);
 }

 protected void KeepOut(EXPG_RGPending pending, bool force)
 {
  if (!pending.Squad) { return; }
  KeepEntityOut(pending, pending.Squad, force);
  array<AIAgent> agents = {};
  pending.Squad.GetAgents(agents);
  foreach (AIAgent agent : agents)
  {
   if (agent) { KeepEntityOut(pending, agent.GetControlledEntity(), force); }
  }
 }

 protected void KeepEntityOut(EXPG_RGPending pending, IEntity entity, bool force)
 {
  if (!entity || EXPG_GarrisonSpawner.IsPlayerCharacter(entity)) { return; }
  int index = pending.Flagged.Find(entity);
  SCR_PersistenceSystem persistence = SCR_PersistenceSystem.GetByEntityWorld(entity);
  if (index >= 0)
  {
   if (persistence && (force || persistence.IsTracked(entity)))
   {
    if (persistence.IsTracked(entity)) { pending.WasTracked[index] = true; }
    persistence.StopTracking(entity);
   }
   return;
  }
  bool flagSet;
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.GetEditableEntity(entity);
  if (editable && !editable.HasEntityFlag(EEditableEntityFlag.NON_SERIALIZABLE))
  {
   editable.SetEntityFlag(EEditableEntityFlag.NON_SERIALIZABLE, true);
   flagSet = true;
  }
  bool tracked;
  if (persistence)
  {
   tracked = persistence.IsTracked(entity);
   persistence.StopTracking(entity);
  }
  pending.Flagged.Insert(entity);
  pending.FlagSet.Insert(flagSet);
  pending.WasTracked.Insert(tracked);
 }

 // Only what this zone changed is handed back.
 protected void ReturnFlagged(EXPG_RGPending pending)
 {
  for (int i = 0; i < pending.Flagged.Count(); i++)
  {
   IEntity entity = pending.Flagged[i];
   if (!entity) { continue; }
   if (pending.FlagSet[i])
   {
    SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.GetEditableEntity(entity);
    if (editable) { editable.SetEntityFlag(EEditableEntityFlag.NON_SERIALIZABLE, false); }
   }
   if (pending.WasTracked[i])
   {
    SCR_PersistenceSystem persistence = SCR_PersistenceSystem.GetByEntityWorld(entity);
    if (persistence) { persistence.StartTracking(entity); }
   }
  }
  pending.Flagged.Clear();
  pending.FlagSet.Clear();
  pending.WasTracked.Clear();
 }

 // A squad that never deployed is deleted: its soldiers, then the squad, stay out of
 // saves until each is gone (what this zone changed moves with them); whatever left
 // the squad meanwhile is handed back.
 protected void RetirePending(EXPG_RGPending pending, notnull EXPG_RetireList retire)
 {
  array<IEntity> members = {};
  EXPG_GarrisonSpawner.CollectSquad(pending.Squad, members);
  foreach (IEntity entity : members)
  {
   int index = pending.Flagged.Find(entity);
   if (index < 0)
   {
    retire.Add(entity);
    continue;
   }
   retire.Add(entity, pending.FlagSet[index], pending.WasTracked[index]);
   pending.Flagged.Remove(index);
   pending.FlagSet.Remove(index);
   pending.WasTracked.Remove(index);
  }
  ReturnFlagged(pending);
 }

 //------------------------------------------------------------------------------------------------
 // Status (replicated; the Game Master reopens the dialog to refresh it)
 //------------------------------------------------------------------------------------------------
 protected void PublishStatus(float now)
 {
  if (now < m_fNextStatus)
  {
   return;
  }
  m_fNextStatus = now + STATUS_INTERVAL;
  string text = ComposeStatus(now);
  if (text == m_sStatus)
  {
   return;
  }
  m_sStatus = text;
  Replication.BumpMe();
 }

 protected string ComposeStatus(float now)
 {
  string text = StateText();
  if (m_bSettingsChanged && (m_iState == STATE_DONE || m_iState == STATE_STOPPED || m_iState == STATE_RUNNING)) { text += ". Settings changed since the last generation: Regenerate applies them"; }
  if (!m_sNotice.IsEmpty() && now < m_fNoticeUntil) { text = m_sNotice + ". " + text; }
  return text;
 }

 protected string StateText()
 {
  if (m_iState == STATE_CENSUS && m_Census)
  {
   if (m_Census.Reading())
   {
    return string.Format("Collecting buildings %1/%2 cells", m_Census.CellsDone, m_Census.CellsTotal);
   }
   return string.Format("Checking buildings %1/%2", m_Census.CheckedCount(), m_Census.HitCount());
  }
  if (m_iState == STATE_CATALOG)
  {
   return CatalogText();
  }
  if (m_iState == STATE_RUNNING)
  {
   return RunningText();
  }
  if (m_iState == STATE_CLEARING)
  {
   if (!m_aClear.IsEmpty() || m_iClearTotal > 0)
   {
    return string.Format("Clearing %1/%2 squads", m_iClearTotal - m_aClear.Count(), m_iClearTotal);
   }
   return "Clearing";
  }
  if (m_iState == STATE_DONE)
  {
   return DoneText("Done");
  }
  if (m_iState == STATE_STOPPED)
  {
   if (m_bLoaded)
   {
    return LoadedText();
   }
   return DoneText("Stopped (" + m_sStopReason + ")");
  }
  if (m_bLoaded)
  {
   return "Loaded: not generated yet; press Generate";
  }
  return "Idle: press Generate";
 }

 protected string CatalogText()
 {
  EXPG_SquadCatalog catalog = m_Explicit;
  if (!catalog && !m_sRunFaction.IsEmpty()) { catalog = EXPG_SquadPool.Get(m_sRunFaction); }
  if (catalog && !catalog.Ready)
  {
   return string.Format("Reading squad catalog %1/%2", catalog.Checked(), catalog.Total());
  }
  string second = m_sRunSecondFaction;
  if (!m_Explicit && !second.IsEmpty())
  {
   EXPG_SquadCatalog other = EXPG_SquadPool.Get(second);
   if (other && !other.Ready)
   {
    return string.Format("Reading squad catalog %1/%2", other.Checked(), other.Total());
   }
  }
  return "Reading squad catalog";
 }

 protected string RunningText()
 {
  if (m_bPaused)
  {
   return "Paused: " + m_sPause;
  }
  if (m_fAILimitedSince >= 0)
  {
   return "Waiting: " + m_sAILimit;
  }
  int analysing;
  int percent;
  foreach (EXPG_RGSite site : m_aActive)
  {
   if (!site || site.Stage != EXPG_RGSite.ANALYSING) { continue; }
   analysing++;
   if (site.Analysis && site.Analysis.Percent > percent) { percent = site.Analysis.Percent; }
  }
  if (analysing > 0)
  {
   int current = m_iAnalysed + 1;
   if (current > m_iTarget) { current = m_iTarget; }
   string failed;
   if (m_iFailures > 0) { failed = string.Format(", %1 failed", m_iFailures); }
   string head = string.Format("Analysing %1/%2 buildings (", current, m_iTarget);
   return head + percent.ToString() + "%" + failed + ")";
  }
  if (!m_aPending.IsEmpty() || HasReady())
  {
   if (m_iDeferred > 0 && m_aPending.IsEmpty())
   {
    return string.Format("Waiting: players within %1 m of %2 building(s)", m_iRunPlayerDistance, m_iDeferred);
   }
   return string.Format("Spawning %1/%2 squads", SquadCount() + m_aPending.Count(), SquadsWanted());
  }
  if (m_bPlanWait)
  {
   return "Waiting: the garrison planner is busy (Game Masters first)";
  }
  return "Working";
 }

 protected bool HasReady()
 {
  foreach (EXPG_RGSite site : m_aActive)
  {
   if (site && site.Stage == EXPG_RGSite.READY)
   {
    return true;
   }
  }
  return false;
 }

 protected string DoneText(string head)
 {
  string text = string.Format("%1: %2 buildings, %3 squads (seed %4)", head, PlacedCount(), SquadCount(), m_iLastSeed);
  if (m_iFailures > 0) { text += string.Format("; %1 failed: %2", m_iFailures, FirstFailure()); }
  if (m_iExcludedPlayers > 0) { text += string.Format("; %1 skipped near players", m_iExcludedPlayers); }
  if (!m_sStopReason.IsEmpty() && head == "Done") { text += "; " + m_sStopReason; }
  return text;
 }

 protected string FirstFailure()
 {
  foreach (EXPG_RGSite site : m_aSites)
  {
   if (site.Stage == EXPG_RGSite.FAILED)
   {
    return site.Note;
   }
  }
  return "unknown";
 }

 protected string LoadedText()
 {
  int held = 0;
  EXPG_GarrisonManager manager = EXPG_GarrisonManager.Get();
  array<ref EXPG_GarrisonRecord> records = {};
  if (manager) { held = manager.CollectGenerated(m_sToken, records); }
  string text = string.Format("Loaded: generated earlier (seed %1), %2 garrisons owned", m_iLastSeed, held);
  if (held == 0 && EXPG_GarrisonPersistence.Legacy()) { text += " (released for the save); Regenerate adds new squads beside the released ones"; }
  return text;
 }
}
