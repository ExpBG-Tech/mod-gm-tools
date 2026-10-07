// Garrison ledger, schema 1. Every save carries the garrisons itself: a Ready
// garrison's squad, living soldiers and waypoints are left out of the CDF entity
// tree and of native persistence (EXPG_SaveExclusion), and this ledger records
// them instead. One format, two carriers: a native PersistentState
// (EXPG_GarrisonPersistenceSerializer) and the CDF document (EXPBG CDF Compat).
// Kept: the building (prefab and transform, found again with the Ambient
// Destruction identity rule), cache state, settings, release request and order,
// the squad (EBG_CacheGroupSnapshot, with the AI Surrender and AI Global Skills
// squad overrides) and one row per member, dead ones included: post kind, fixed
// or patrolling, post, look and last position in building-local coordinates,
// prefab, author and the per-soldier overrides (module seams below). Not kept:
// controls, reservations, parks, holds, Simulation snapshots (a loaded garrison
// caches again), timers, status and the creator's session id. Loadouts, damage
// and identities restore as prefab defaults, as Full caching does. Building
// plans are analysed again on load; saved node indices are hints only.

// The building a garrison holds: the prefab and its world transform. Found again
// as exactly one supported building of that prefab whose four transform rows are
// within 5 cm, among at most 512 entities in a 2 m box (ambiguity fails closed).
class EXPG_BuildingRef
{
 static const int MAX_VISITS = 512;
 ResourceName Prefab;
 vector Transform[4];
 protected IEntity m_Match;
 protected int m_Matches;
 protected int m_Visits;

 bool Capture(IEntity building)
 {
  if (!building || !building.GetPrefabData()) { return false; }
  Prefab = building.GetPrefabData().GetPrefabName();
  building.GetWorldTransform(Transform);
  return !Prefab.IsEmpty();
 }

 void CopyFrom(EXPG_BuildingRef other)
 {
  if (!other) { return; }
  Prefab = other.Prefab;
  for (int axis = 0; axis < 4; axis++) { Transform[axis] = other.Transform[axis]; }
 }

 IEntity Find(out string reason)
 {
  m_Match = null;
  m_Matches = 0;
  m_Visits = 0;
  reason = "";
  if (!GetGame() || !GetGame().GetWorld() || Prefab.IsEmpty())
  {
   reason = "no world or no saved building";
   return null;
  }
  vector origin = Transform[3];
  GetGame().GetWorld().QueryEntitiesByAABB(origin - "2 2 2", origin + "2 2 2", Visit, null, EQueryEntitiesFlags.ALL);
  if (m_Visits > MAX_VISITS) { reason = "the building search exceeded 512 entities"; return null; }
  if (m_Matches > 1) { reason = string.Format("%1 identical buildings match", m_Matches); return null; }
  if (m_Matches == 0) { reason = "the building is gone (destroyed, removed or moved)"; return null; }
  return m_Match;
 }

 protected bool Visit(IEntity entity)
 {
  m_Visits++;
  if (m_Visits > MAX_VISITS) { return false; }
  if (!entity || !SCR_DestructibleBuildingEntity.Cast(entity) || !entity.GetPrefabData() || entity.GetPrefabData().GetPrefabName() != Prefab) { return true; }
  vector actual[4];
  entity.GetWorldTransform(actual);
  for (int axis = 0; axis < 4; axis++)
  {
   if (vector.DistanceSq(actual[axis], Transform[axis]) > 0.0025) { return true; }
  }
  m_Match = entity;
  m_Matches++;
  return true;
 }

 vector ToLocal(vector point)
 {
  return point.InvMultiply4(Transform);
 }

 vector ToWorld(vector local)
 {
  return local.Multiply4(Transform);
 }

 vector DirToLocal(vector direction)
 {
  vector local = (direction + Transform[3]).InvMultiply4(Transform);
  return local;
 }

 vector DirToWorld(vector direction)
 {
  vector world = direction.Multiply4(Transform) - Transform[3];
  return world;
 }

 bool Valid()
 {
  if (Prefab.IsEmpty() || Prefab.Length() > 1024) { return false; }
  for (int axis = 0; axis < 4; axis++)
  {
   if (!EXPG_Ledger.FiniteVector(Transform[axis])) { return false; }
  }
  return Transform[0].LengthSq() > 0.0001 && Transform[1].LengthSq() > 0.0001 && Transform[2].LengthSq() > 0.0001;
 }

 bool Write(SaveContext context)
 {
  array<vector> rows = {Transform[0], Transform[1], Transform[2], Transform[3]};
  return context.WriteValue("prefab", Prefab) && context.WriteValue("matrix", rows);
 }

 bool Read(LoadContext context)
 {
  array<vector> rows = {};
  if (!context.ReadValue("prefab", Prefab) || !context.ReadValue("matrix", rows) || rows.Count() != 4) { return false; }
  for (int axis = 0; axis < 4; axis++) { Transform[axis] = rows[axis]; }
  return Valid();
 }

 string Describe()
 {
  return string.Format("%1 at %2", Prefab, Transform[3]);
 }
}

// One member of a garrison, in record order. Dead rows are kept and never spawned.
// A living row restores at its last position (building-local), never on top of a
// post he did not hold; the post (or a patroller's claimed stop) is remapped onto
// the analysed plan by position on load.
class EXPG_MemberSnapshot
{
 int Id;
 bool Dead;
 bool Fixed = true;
 int PostKind;
 int NodeHint = -1;
 vector LocalPost;
 vector LocalLook;
 vector LocalLast;
 vector LocalFacing;
 // World fallback, used when the building is gone on load.
 vector WorldLast;
 vector WorldFacing;
 ResourceName Prefab;
 ref EBG_CacheAuthor Author = new EBG_CacheAuthor();

 // Module seams: per-soldier values other modules keep across a save (AI Surrender
 // and AI Global Skills overrides, ESR_OverrideCache.c and EGS_OverrideCache.c).
 // Read from the survivor carry they already fill, written back into the carry a
 // restored survivor applies when he is respawned.
 void CaptureCarry(EBG_SurvivorCarry carry) {}

 void FillCarry(EBG_SurvivorCarry carry) {}

 bool WriteExtras(SaveContext context)
 {
  return true;
 }

 bool ReadExtras(LoadContext context)
 {
  return true;
 }

 bool Write(SaveContext context)
 {
  if (!context.WriteValue("id", Id) || !context.WriteValue("dead", Dead) || !context.WriteValue("fixed", Fixed) || !context.WriteValue("kind", PostKind) || !context.WriteValue("node", NodeHint)) { return false; }
  if (!context.WriteValue("post", LocalPost) || !context.WriteValue("look", LocalLook) || !context.WriteValue("last", LocalLast) || !context.WriteValue("facing", LocalFacing)) { return false; }
  if (!context.WriteValue("worldLast", WorldLast) || !context.WriteValue("worldFacing", WorldFacing) || !context.WriteValue("prefab", Prefab) || !Author.Write(context)) { return false; }
  return WriteExtras(context);
 }

 bool Read(LoadContext context)
 {
  if (!context.ReadValue("id", Id) || !context.ReadValue("dead", Dead) || !context.ReadValue("fixed", Fixed) || !context.ReadValue("kind", PostKind) || !context.ReadValue("node", NodeHint)) { return false; }
  if (!context.ReadValue("post", LocalPost) || !context.ReadValue("look", LocalLook) || !context.ReadValue("last", LocalLast) || !context.ReadValue("facing", LocalFacing)) { return false; }
  if (!context.ReadValue("worldLast", WorldLast) || !context.ReadValue("worldFacing", WorldFacing) || !context.ReadValue("prefab", Prefab) || !Author.Read(context)) { return false; }
  return ReadExtras(context) && Valid();
 }

 bool Valid()
 {
  if (Id < 1 || Id > 1024 || PostKind < 0 || PostKind > 4 || NodeHint < -1 || NodeHint >= EXPG_BuildingPlan.MAX_NODES || Prefab.Length() > 1024) { return false; }
  array<vector> vectors = {LocalPost, LocalLook, LocalLast, LocalFacing, WorldLast, WorldFacing};
  foreach (vector value : vectors)
  {
   if (!EXPG_Ledger.FiniteVector(value)) { return false; }
  }
  return Dead || !Prefab.IsEmpty();
 }
}

// One garrison (one squad) of the ledger.
class EXPG_GarrisonSnapshot
{
 // Unique per garrison; made at adoption and kept across saves.
 string Token;
 // Reserved for the Random Garrison zone module: the id of the module that made
 // this garrison. Written and read, unused by this version.
 string GeneratedBy;
 // Record order: placement of later squads and the shared plan depend on it.
 int OrderIndex;
 ref EXPG_BuildingRef Site = new EXPG_BuildingRef();
 int CacheMode = 2;
 float WakeDistance = 300;
 float SleepDistance = 400;
 // 0 awake, 1 Simulation, 2 Full.
 int CacheState;
 bool ReleaseRequested;
 string ReleaseReason;
 // The member Id of the squad leader (0 when unknown).
 int LeaderId;
 ref EBG_CacheGroupSnapshot Squad;
 ref array<ref EXPG_MemberSnapshot> Members = {};

 int AliveCount()
 {
  int alive;
  foreach (EXPG_MemberSnapshot member : Members)
  {
   if (!member.Dead) { alive++; }
  }
  return alive;
 }

 bool Write(SaveContext context)
 {
  if (!Squad || !context.WriteValue("token", Token) || !context.WriteValue("generatedBy", GeneratedBy) || !context.WriteValue("order", OrderIndex)) { return false; }
  if (!context.WriteValue("mode", CacheMode) || !context.WriteValue("wake", WakeDistance) || !context.WriteValue("sleep", SleepDistance) || !context.WriteValue("cacheState", CacheState)) { return false; }
  if (!context.WriteValue("release", ReleaseRequested) || !context.WriteValue("releaseReason", ReleaseReason) || !context.WriteValue("leader", LeaderId)) { return false; }
  if (!context.StartObject("building") || !Site.Write(context) || !context.EndObject()) { return false; }
  if (!context.StartObject("squad") || !Squad.Write(context) || !context.EndObject()) { return false; }
  if (!context.WriteValue("members", Members.Count())) { return false; }
  for (int i = 0; i < Members.Count(); i++)
  {
   if (!context.StartObject("member" + i.ToString()) || !Members[i].Write(context) || !context.EndObject()) { return false; }
  }
  return true;
 }

 bool Read(LoadContext context, out string reason)
 {
  reason = "unreadable garrison fields";
  if (!context.ReadValue("token", Token) || !context.ReadValue("generatedBy", GeneratedBy) || !context.ReadValue("order", OrderIndex)) { return false; }
  if (!context.ReadValue("mode", CacheMode) || !context.ReadValue("wake", WakeDistance) || !context.ReadValue("sleep", SleepDistance) || !context.ReadValue("cacheState", CacheState)) { return false; }
  if (!context.ReadValue("release", ReleaseRequested) || !context.ReadValue("releaseReason", ReleaseReason) || !context.ReadValue("leader", LeaderId)) { return false; }
  reason = "unreadable or unknown building";
  if (!context.StartObject("building") || !Site.Read(context) || !context.EndObject()) { return false; }
  reason = "unreadable squad, or its prefab, faction or orders are unavailable in this mission";
  Squad = new EBG_CacheGroupSnapshot();
  Squad.Tolerant = true;
  if (!context.StartObject("squad") || !Squad.Read(context) || !context.EndObject()) { return false; }
  int count;
  reason = "unreadable member count";
  if (!context.ReadValue("members", count) || count < 0 || count > EXPG_Ledger.MAX_MEMBERS) { return false; }
  array<int> ids = {};
  for (int i = 0; i < count; i++)
  {
   EXPG_MemberSnapshot member = new EXPG_MemberSnapshot();
   reason = "unreadable member " + i.ToString();
   if (!context.StartObject("member" + i.ToString()) || !member.Read(context) || !context.EndObject() || ids.Contains(member.Id)) { return false; }
   ids.Insert(member.Id);
   Members.Insert(member);
  }
  reason = "";
  return Valid(reason);
 }

 bool Valid(out string reason)
 {
  reason = "invalid garrison token";
  if (Token.IsEmpty() || Token.Length() > 64 || GeneratedBy.Length() > 128 || ReleaseReason.Length() > 512) { return false; }
  reason = "invalid garrison settings";
  if (CacheMode < 0 || CacheMode > 2 || CacheState < 0 || CacheState > 2 || OrderIndex < 0) { return false; }
  if (!EXPG_Ledger.Finite(WakeDistance) || !EXPG_Ledger.Finite(SleepDistance)) { return false; }
  reason = "invalid building";
  if (!Site.Valid()) { return false; }
  reason = "missing squad";
  if (!Squad) { return false; }
  reason = "";
  return true;
 }
}

class EXPG_Ledger
{
 static const int VERSION = 1;
 static const int MAX_GARRISONS = 1024;
 static const int MAX_MEMBERS = 32;
 static const int MAX_BUILDINGS = 64;
 static const int MAX_JSON = 16000000;

 static bool Finite(float value)
 {
  return value == value && value > -1000000000 && value < 1000000000;
 }

 static bool FiniteVector(vector value)
 {
  return Finite(value[0]) && Finite(value[1]) && Finite(value[2]);
 }

 static string WorldFile()
 {
  if (!GetGame()) { return string.Empty; }
  return GetGame().GetWorldFile();
 }

 static bool Write(SaveContext context, array<ref EXPG_GarrisonSnapshot> garrisons)
 {
  if (!context || !garrisons || garrisons.Count() > MAX_GARRISONS) { return false; }
  if (!context.WriteValue("expgGarrisonVersion", VERSION) || !context.WriteValue("world", WorldFile()) || !context.WriteValue("garrisons", garrisons.Count())) { return false; }
  for (int i = 0; i < garrisons.Count(); i++)
  {
   if (!context.StartObject("garrison" + i.ToString()) || !garrisons[i].Write(context) || !context.EndObject()) { return false; }
  }
  return true;
 }

 static bool Read(LoadContext context, array<ref EXPG_GarrisonSnapshot> garrisons, out string reason)
 {
  reason = "the ledger is unreadable";
  int version, count;
  string savedWorld;
  if (!context || !garrisons || !context.ReadValue("expgGarrisonVersion", version)) { return false; }
  reason = string.Format("the ledger has schema %1; this EXPBG GM Tools reads schema %2", version, VERSION);
  if (version != VERSION) { return false; }
  reason = "the ledger has no world or garrison count";
  if (!context.ReadValue("world", savedWorld) || !context.ReadValue("garrisons", count)) { return false; }
  reason = string.Format("the ledger was saved on %1, not on this world (%2)", savedWorld, WorldFile());
  if (savedWorld != WorldFile()) { return false; }
  reason = string.Format("the ledger holds %1 garrisons (at most %2)", count, MAX_GARRISONS);
  if (count < 0 || count > MAX_GARRISONS) { return false; }
  for (int i = 0; i < count; i++)
  {
   EXPG_GarrisonSnapshot garrison = new EXPG_GarrisonSnapshot();
   string why;
   if (!context.StartObject("garrison" + i.ToString()) || !garrison.Read(context, why) || !context.EndObject())
   {
    reason = string.Format("garrison %1: %2", i, why);
    return false;
   }
   garrisons.Insert(garrison);
  }
  return Validate(garrisons, reason);
 }

 // Limits, unique tokens and loadable prefabs. No world state is changed.
 static bool Validate(array<ref EXPG_GarrisonSnapshot> garrisons, out string reason)
 {
  reason = "";
  if (!garrisons) { reason = "no ledger"; return false; }
  if (garrisons.Count() > MAX_GARRISONS) { reason = string.Format("%1 garrisons (at most %2)", garrisons.Count(), MAX_GARRISONS); return false; }
  array<string> tokens = {};
  array<vector> sites = {};
  foreach (int index, EXPG_GarrisonSnapshot garrison : garrisons)
  {
   string why;
   if (!garrison || !garrison.Valid(why)) { reason = string.Format("garrison %1: %2", index, why); return false; }
   if (tokens.Contains(garrison.Token)) { reason = "duplicate garrison token " + garrison.Token; return false; }
   tokens.Insert(garrison.Token);
   bool known;
   foreach (vector site : sites)
   {
    if (vector.DistanceSq(site, garrison.Site.Transform[3]) < 0.0025) { known = true; break; }
   }
   if (!known) { sites.Insert(garrison.Site.Transform[3]); }
   if (garrison.Members.Count() > MAX_MEMBERS) { reason = string.Format("garrison %1 has %2 members (at most %3)", index, garrison.Members.Count(), MAX_MEMBERS); return false; }
   foreach (EXPG_MemberSnapshot member : garrison.Members)
   {
    if (member.Dead) { continue; }
    EBG_CachePose pose = new EBG_CachePose();
    pose.Prefab = member.Prefab;
    if (!pose.PrefabType(SCR_ChimeraCharacter)) { reason = string.Format("garrison %1: soldier prefab %2 is unavailable", index, member.Prefab); return false; }
   }
  }
  if (sites.Count() > MAX_BUILDINGS) { reason = string.Format("%1 garrisoned buildings (at most %2)", sites.Count(), MAX_BUILDINGS); return false; }
  return true;
 }

 static bool ToJson(array<ref EXPG_GarrisonSnapshot> garrisons, out string json, out string reason)
 {
  json = "";
  JsonSaveContext context = new JsonSaveContext();
  reason = "the ledger could not be written";
  if (!Write(context, garrisons) || !context.IsValid()) { return false; }
  json = context.SaveToString();
  reason = string.Format("the ledger is %1 bytes (at most %2)", json.Length(), MAX_JSON);
  if (json.IsEmpty() || json.Length() > MAX_JSON) { json = ""; return false; }
  reason = "";
  return true;
 }

 static bool FromJson(string json, array<ref EXPG_GarrisonSnapshot> garrisons, out string reason)
 {
  reason = "the ledger is empty or too large";
  if (json.IsEmpty() || json.Length() > MAX_JSON) { return false; }
  SCR_PersistenceJsonLoadContext context = new SCR_PersistenceJsonLoadContext();
  reason = "the ledger is not valid JSON";
  if (!context.LoadFromString(json)) { return false; }
  return Read(context, garrisons, reason);
 }

 static int CountMembers(array<ref EXPG_GarrisonSnapshot> garrisons, bool aliveOnly)
 {
  int total;
  foreach (EXPG_GarrisonSnapshot garrison : garrisons)
  {
   if (aliveOnly) { total += garrison.AliveCount(); }
   else { total += garrison.Members.Count(); }
  }
  return total;
 }
}
