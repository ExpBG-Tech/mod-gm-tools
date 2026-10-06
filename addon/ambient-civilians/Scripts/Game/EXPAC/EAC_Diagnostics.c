enum EAC_ESpawnReason
{
 NONE, NO_HOMES, NO_CONTROLLED_CHARACTERS, UNKNOWN_OBSERVER, OBSERVER_LIMIT,
 OUTSIDE_DISTANCE, EXCLUSION, GEOMETRY, WATER, TOO_NEAR, VISIBLE, BAD_PREFAB,
 BAD_FACTION, BUDGET, PENDING, CACHE_RECOVERY, RESIDENT_INELIGIBLE, CAPACITY,
 BAD_HOME, BAD_OWNER, THEME_PENDING, STARTED, ACTIVATED, ACTIVATION_FAILED,
 NAVMESH_PENDING, SLOT_ADVANCED, NAVMESH_REJECTED, LIVE_QUALIFICATION, LOAD_PAUSED, LOCAL_BUDGET, CACHE_BLOCKED,
 SCENE_PENDING, SCENE_BARREN, SPOT_TAKEN,
 // Appended immediately before COUNT so no existing ordinal moves: m_Counts is
 // sized from COUNT and a shifted ordinal would silently rename every counter a
 // recorded campaign already printed.
 ISOLATED_HOME, AI_LIMIT, CACHED_WAKE_HEADROOM, RUINED_HOME, COUNT
}

// One fixed-size mission ledger. Counts are gate evaluations, not unique residents.
// Keep the most recent rejection separate from progress so pending work cannot hide it.
class EAC_Diagnostics
{
 static const int COUNTER_LIMIT = 100000;
 static const int MAX_DRAW_POINTS = 64;
 // Shared GM snapshot contract; renderer chooses presentation, never spawn relevance.
 static const int HOME = 0;
 static const int ACTIVE = 1;
 static const int CACHED = 2;
 static const int PENDING = 3;
 static const int REJECTED = 4;
 static const int EXCLUSION_MARK = 5;
 static const int SCENE = 6;
 protected ref array<int> m_Counts = {};
 protected int m_LastReason, m_LastRejection;
 protected string m_LastFailure;
 protected vector m_LastPosition;
 protected bool m_HasPosition;
 // Sixteen distinct bounded failures per mission, shared by pedestrian cleanup,
 // activation and traffic. Neither retries nor verbose settings reset this cap.
 protected ref array<string> m_ReportedFailures = {};

 void EAC_Diagnostics() { Reset(); }

 void Reset()
 {
  m_Counts.Clear(); m_Counts.Resize(EAC_ESpawnReason.COUNT);
  m_LastReason = EAC_ESpawnReason.NONE; m_LastRejection = EAC_ESpawnReason.NONE;
  m_LastFailure = ""; m_LastPosition = "0 0 0"; m_HasPosition = false;
  m_ReportedFailures.Clear();
 }

 bool TakeFailureLog(string detail)
 {
  if (detail.IsEmpty() || m_ReportedFailures.Count() >= 16) return false;
  if (detail.Length() > 160) detail = detail.Substring(0, 160);
  if (m_ReportedFailures.Contains(detail)) return false;
  m_ReportedFailures.Insert(detail);
  return true;
 }

 void Record(int reason, string detail = "")
 {
  if (reason <= EAC_ESpawnReason.NONE || reason >= EAC_ESpawnReason.COUNT) return;
  if (m_Counts[reason] < COUNTER_LIMIT) m_Counts[reason] = m_Counts[reason] + 1;
  m_LastReason = reason;
  if (reason != EAC_ESpawnReason.STARTED && reason != EAC_ESpawnReason.ACTIVATED && reason != EAC_ESpawnReason.PENDING && reason != EAC_ESpawnReason.NAVMESH_PENDING && reason != EAC_ESpawnReason.SLOT_ADVANCED)
   m_LastRejection = reason;
  if (!detail.IsEmpty())
  {
   if (detail.Length() > 160) detail = detail.Substring(0, 160);
   m_LastFailure = detail;
  }
 }

 void RememberRejectedPosition(vector position) { m_LastPosition = position; m_HasPosition = true; }
 int GetLastReason() { return m_LastReason; }
 int GetLastRejection() { return m_LastRejection; }
 int GetCount(int reason)
 {
  if (reason < 0 || reason >= m_Counts.Count()) return 0;
  return m_Counts[reason];
 }

 string Describe()
 {
  string description = "state=" + ReasonName(m_LastReason) + " last_rejection=" + ReasonName(m_LastRejection);
  if (!m_LastFailure.IsEmpty()) description += " last_failure=" + m_LastFailure;
  return description;
 }

 string DescribeCounts()
 {
  string result;
  for (int i = 1; i < m_Counts.Count(); i++)
   if (m_Counts[i] > 0) result += ReasonName(i) + "=" + m_Counts[i].ToString() + " ";
  return result;
 }

 void AppendRejectedPoint(array<vector> positions, array<int> kinds)
 {
  if (m_HasPosition) AddDrawPoint(positions, kinds, m_LastPosition, REJECTED);
 }

 static void AddDrawPoint(array<vector> positions, array<int> kinds, vector position, int kind)
 {
  // Cached residents/parties are logical records only, never world markers.
  if (kind == CACHED) return;
  if (!positions || !kinds || positions.Count() != kinds.Count() || positions.Count() >= MAX_DRAW_POINTS) return;
  positions.Insert(position); kinds.Insert(kind);
 }

 static string ReasonName(int reason)
 {
  switch (reason)
  {
   case EAC_ESpawnReason.NONE: return "idle";
   case EAC_ESpawnReason.NO_HOMES: return "no_homes";
   case EAC_ESpawnReason.NO_CONTROLLED_CHARACTERS: return "no_controlled_characters";
   case EAC_ESpawnReason.UNKNOWN_OBSERVER: return "unknown_observer";
   case EAC_ESpawnReason.OBSERVER_LIMIT: return "observer_limit";
   case EAC_ESpawnReason.OUTSIDE_DISTANCE: return "outside_distance";
   case EAC_ESpawnReason.EXCLUSION: return "exclusion";
   case EAC_ESpawnReason.GEOMETRY: return "blocked_geometry";
   case EAC_ESpawnReason.WATER: return "water";
   case EAC_ESpawnReason.TOO_NEAR: return "too_near";
   case EAC_ESpawnReason.VISIBLE: return "visible";
   case EAC_ESpawnReason.BAD_PREFAB: return "bad_prefab";
   case EAC_ESpawnReason.BAD_FACTION: return "bad_faction";
   case EAC_ESpawnReason.BUDGET: return "budget";
   case EAC_ESpawnReason.PENDING: return "pending_activation";
   case EAC_ESpawnReason.CACHE_RECOVERY: return "cache_recovery";
   case EAC_ESpawnReason.RESIDENT_INELIGIBLE: return "resident_ineligible";
   case EAC_ESpawnReason.CAPACITY: return "record_capacity";
   case EAC_ESpawnReason.BAD_HOME: return "missing_home";
   case EAC_ESpawnReason.BAD_OWNER: return "not_active_server_owner";
   case EAC_ESpawnReason.THEME_PENDING: return "theme_not_ready";
   case EAC_ESpawnReason.STARTED: return "activation_started";
   case EAC_ESpawnReason.ACTIVATED: return "activated";
   case EAC_ESpawnReason.ACTIVATION_FAILED: return "activation_failed";
   case EAC_ESpawnReason.NAVMESH_PENDING: return "navmesh_pending";
   case EAC_ESpawnReason.SLOT_ADVANCED: return "next_household";
   case EAC_ESpawnReason.NAVMESH_REJECTED: return "navmesh_rejected";
   case EAC_ESpawnReason.LIVE_QUALIFICATION: return "live_qualification";
   case EAC_ESpawnReason.LOAD_PAUSED: return "load_paused";
   case EAC_ESpawnReason.LOCAL_BUDGET: return "local_budget";
   case EAC_ESpawnReason.CACHE_BLOCKED: return "cache_blocked";
   case EAC_ESpawnReason.SCENE_PENDING: return "scene_not_surveyed";
   case EAC_ESpawnReason.SCENE_BARREN: return "scene_barren";
   case EAC_ESpawnReason.SPOT_TAKEN: return "spot_taken";
   case EAC_ESpawnReason.ISOLATED_HOME: return "isolated_home";
   case EAC_ESpawnReason.AI_LIMIT: return "engine_ai_limit";
   case EAC_ESpawnReason.CACHED_WAKE_HEADROOM: return "cached_wake_headroom";
   case EAC_ESpawnReason.RUINED_HOME: return "ruined_home";
  }
  return "invalid_reason";
 }
}
