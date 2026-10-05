// The world ledger survives module deletion. A CDF bridge stores it once per save file.
class EAD_BuildingSnapshot
{
 static const int MAX_SAVED = 4096;
 ref array<ref EAD_BuildingChoice> Choices = {};
 static bool Write(out string payload)
 {
  payload = "";
  EAD_Buildings.EnsureWorld(GetGame().GetWorld());
  array<ref EAD_BuildingChoice> choices = {};
  EAD_Buildings.GetLedger(choices);
  if (choices.Count() > MAX_SAVED) return false;
  JsonSaveContext ctx = new JsonSaveContext();
  ctx.WriteValue("schema", 2); ctx.WriteValue("world", GetGame().GetWorldFile()); ctx.WriteValue("count", choices.Count());
  for (int index = 0; index < choices.Count(); index++)
  {
   EAD_BuildingChoice choice = choices[index];
   ctx.StartObject("building" + index.ToString());
   ctx.WriteValue("prefab", choice.Prefab); ctx.WriteValue("selected", choice.Selected);
   ctx.WriteValue("finished", choice.Finished); ctx.WriteValue("destroyed", choice.Destroyed);
   ctx.WriteValue("targetPhase", choice.TargetPhase); ctx.WriteValue("appliedPhase", choice.AppliedPhase);
   for (int axis = 0; axis < 4; axis++) ctx.WriteValue("t" + axis.ToString(), choice.Transform[axis]);
   ctx.EndObject();
  }
  // Validate the serialized form too: native ResourceName encoding must retain
  // every identity. Refuse the entire save rather than lose permanent history.
  string candidate = ctx.SaveToString();
  if (!Read(candidate)) return false;
  payload = candidate;
  return true;
 }
 static EAD_BuildingSnapshot Read(string payload)
 {
  if (payload.IsEmpty() || payload.Length() > 8388608) return null;
  JsonLoadContext ctx = new JsonLoadContext();
  int schema, count; string world;
  if (!ctx.LoadFromString(payload) || !ctx.ReadValue("schema", schema) || (schema != 1 && schema != 2) || !ctx.ReadValue("world", world) || world != GetGame().GetWorldFile() || !ctx.ReadValue("count", count) || count < 0 || count > MAX_SAVED) return null;
  EAD_BuildingSnapshot data = new EAD_BuildingSnapshot();
  map<string, bool> identities = new map<string, bool>();
  for (int index = 0; index < count; index++)
  {
   EAD_BuildingChoice choice = new EAD_BuildingChoice();
   if (!ctx.StartObject("building" + index.ToString()) || !ctx.ReadValue("prefab", choice.Prefab) || choice.Prefab.IsEmpty() || choice.Prefab.Length() > 1024) return null;
   if (!ctx.ReadValue("selected", choice.Selected) || !ctx.ReadValue("finished", choice.Finished) || !ctx.ReadValue("destroyed", choice.Destroyed)) return null;
   if (choice.Destroyed && (!choice.Selected || !choice.Finished)) return null;
   if (schema == 2)
   {
    if (!ctx.ReadValue("targetPhase", choice.TargetPhase) || !ctx.ReadValue("appliedPhase", choice.AppliedPhase)) return null;
    if (choice.TargetPhase < 0 || choice.TargetPhase > 15 || choice.AppliedPhase < 0 || choice.AppliedPhase > choice.TargetPhase) return null;
    if (choice.TargetPhase > 0 && choice.Destroyed) return null;
    if (choice.AppliedPhase > 0 && (!choice.Selected || !choice.Finished)) return null;
   }
   for (int axis = 0; axis < 4; axis++)
   {
    vector value;
    if (!ctx.ReadValue("t" + axis.ToString(), value) || !EAD_Snapshot.FiniteVector(value)) return null;
    choice.Transform[axis] = value;
   }
   if (!ctx.EndObject()) return null;
   Resource prefab = Resource.Load(choice.Prefab);
   if (!prefab || !prefab.IsValid()) return null;
   string identity = choice.Prefab + choice.Transform[3].ToString();
   if (identities.Contains(identity)) return null;
   identities.Insert(identity, true);
   data.Choices.Insert(choice);
  }
  return data;
 }
}
// Bounded replay on an existing map. Never spawns replacements or repairs prior damage.
class EAD_BuildingRestore
{
 protected ref EAD_BuildingSnapshot m_Data;
 protected EAD_BuildingChoice m_LookingFor;
 protected IEntity m_Match;
 protected int m_Matches, m_Visits, m_Next;
 protected BaseWorld m_World;
 protected ref array<ref EAD_BuildingChoice> m_Previous = {};
 bool Failed;
 bool Finished;
 string Reason;
 // Saved collapses whose original was already absent (native persistence) during import.
 int NativeSatisfied;
 void EAD_BuildingRestore(EAD_BuildingSnapshot data)
 {
  m_Data = data; m_World = GetGame().GetWorld();
  EAD_Buildings.EnsureWorld(m_World); EAD_Buildings.GetLedger(m_Previous);
 }
 static bool SameIdentity(EAD_BuildingChoice a, EAD_BuildingChoice b)
 {
  if (!a || !b || a.Prefab != b.Prefab) return false;
  for (int axis = 0; axis < 4; axis++) { if (vector.DistanceSq(a.Transform[axis], b.Transform[axis]) > 0.0025) return false; }
  return true;
 }
 protected EAD_BuildingChoice PreviousDestruction(EAD_BuildingChoice choice)
 {
  if (!choice.Destroyed || GetGame().GetWorld() != m_World) return null;
  foreach (EAD_BuildingChoice previous : m_Previous)
  {
   if (previous.Destroyed && SameIdentity(previous, choice)) return previous;
  }
  return null;
 }
 // A missing original is accepted only with matching server-observed destruction
 // history from this world, never for ambiguous or truncated spatial queries.
 bool HasCompletedHistory(EAD_BuildingChoice choice)
 {
  EAD_BuildingChoice previous = PreviousDestruction(choice);
  return m_LookingFor == choice && previous && !previous.Entity && !previous.Authored && m_Matches == 0 && m_Visits <= 512;
 }
 // After a server restart, native (vanilla) persistence can already have removed a
 // building that the save records as collapsed, while the fresh ledger has no history.
 // An unambiguous, complete miss of a saved collapse (or of a building still selected
 // for a collapse) is that saved end state. Ambiguous, truncated or phase records never are.
 bool SatisfiedByNativeDestruction(EAD_BuildingChoice choice)
 {
  if (m_LookingFor != choice || m_Matches != 0 || m_Visits > 512) return false;
  if (!choice.Selected || choice.TargetPhase != 0 || choice.AppliedPhase != 0) return false;
  return choice.Destroyed || !choice.Finished;
 }
 string MissReason(EAD_BuildingChoice choice)
 {
  string where = choice.Prefab + " at " + choice.Transform[3].ToString();
  if (m_Visits > 512) return "Building query exceeded its limit: " + where;
  if (m_Matches > 1) return "Ambiguous building (" + m_Matches.ToString() + " matches): " + where;
  if (choice.TargetPhase > 0 || choice.AppliedPhase > 0) return "Missing building for saved damage phase: " + where;
  return "Missing building that the save records as standing: " + where;
 }
 protected bool Visit(IEntity entity)
 {
  m_Visits++;
  if (m_Visits > 512) return false;
  if (!entity || !entity.GetPrefabData() || entity.GetPrefabData().GetPrefabName() != m_LookingFor.Prefab) return true;
  vector transform[4]; entity.GetWorldTransform(transform);
  for (int axis = 0; axis < 4; axis++) { if (vector.DistanceSq(transform[axis], m_LookingFor.Transform[axis]) > 0.0025) return true; }
  if (!EAD_Buildings.IsSupported(entity)) return true;
  m_Match = entity; m_Matches++;
  return true;
 }
 IEntity Find(EAD_BuildingChoice choice)
 {
  m_LookingFor = choice; m_Match = null; m_Matches = 0; m_Visits = 0;
  vector origin = choice.Transform[3];
  m_World.QueryEntitiesByAABB(origin - "2 2 2", origin + "2 2 2", Visit, null, EQueryEntitiesFlags.ALL);
  if (m_Matches != 1 || m_Visits > 512) return null;
  return m_Match;
 }
 // Called before CDF is allowed to clear the old scene. Invalid identities fail closed.
 bool Preflight()
 {
  if (!m_Data || !m_World) return false;
  EAD_Buildings.EnsureWorld(m_World);
  array<ref EAD_BuildingChoice> current = {};
  EAD_Buildings.GetLedger(current);
  foreach (EAD_BuildingChoice existing : current)
  {
   if (!existing.Destroyed && existing.AppliedPhase == 0) continue;
   bool retained;
   foreach (EAD_BuildingChoice saved : m_Data.Choices)
   {
    if (!SameIdentity(saved, existing)) continue;
    if ((existing.Destroyed && saved.Destroyed) || (existing.AppliedPhase > 0 && saved.AppliedPhase >= existing.AppliedPhase)) { retained = true; break; }
   }
   if (!retained) { Reason = "Older save would lose permanent building history; load it in a fresh world"; return false; }
  }
  foreach (EAD_BuildingChoice choice : m_Data.Choices)
  {
   IEntity entity = Find(choice);
   EAD_BuildingChoice previous = PreviousDestruction(choice);
   if (previous && previous.Entity) { Reason = "Native building collapse still in progress; wait before loading"; return false; }
   if (previous && entity) { Reason = "Original identity reappeared after permanent destruction"; return false; }
   if (!entity)
   {
    if (HasCompletedHistory(choice) || SatisfiedByNativeDestruction(choice)) continue;
    Reason = MissReason(choice); return false;
   }
   SCR_DestructibleBuildingComponent damage = SCR_DestructibleBuildingComponent.Cast(entity.FindComponent(SCR_DestructibleBuildingComponent));
   if (choice.TargetPhase > 0)
   {
    SCR_DestructionMultiPhaseComponent phases = EAD_Buildings.NativePhases(entity);
    if (!phases || choice.TargetPhase >= phases.GetNumDamagePhases()) { Reason = "Saved building phases no longer supported"; return false; }
    for (int phase = 1; phase <= choice.TargetPhase; phase++)
    {
     ResourceName model; string remap;
     SCR_Global.GetModelAndRemapFromResource(phases.GetDamagePhaseData(phase).m_PhaseModel, model, remap);
     Resource resource = Resource.Load(model);
     if (!resource || !resource.IsValid()) { Reason = "Saved phase model unavailable"; return false; }
    }
    if (choice.AppliedPhase > 0 && (phases.GetDamagePhase() > choice.AppliedPhase || Occupied(entity))) { Reason = "Occupied or more-damaged building blocks saved phase replay"; return false; }
   }
   if (choice.Destroyed)
   {
    if (!damage) { Reason = "Saved collapse no longer supported"; return false; }
    if (damage.GetDefaultHitZone().GetDamageState() == EDamageState.DESTROYED) { Reason = "Native building collapse still in progress; wait before loading"; return false; }
    EAD_BuildingReplay replay = new EAD_BuildingReplay();
    if (!replay.Prepare(entity)) { Reason = "Building subtree exceeds native replay limit"; return false; }
    foreach (EAD_BuildingReplayToken part : replay.Parts)
    {
     if (Occupied(part.Entity)) { Reason = "Occupied building blocks saved destruction before scene clearing"; return false; }
    }
   }
  }
  return true;
 }
 bool Step()
 {
  if (Failed || Finished || !EAD_Snapshot.Loading || !Replication.IsServer()) return false;
  if (GetGame().GetWorld() != m_World) { Failed = true; Reason = "World changed during building restore"; return false; }
  if (m_Next == 0) EAD_Buildings.ResetLedger(m_World);
  if (m_Next >= m_Data.Choices.Count())
  {
   Finished = true;
   if (NativeSatisfied > 0) PrintFormat("[EAD RESTORE] nativeSatisfied=%1 of %2 saved buildings were already absent; recorded as completed collapses", NativeSatisfied, m_Data.Choices.Count());
   return false;
  }
  EAD_BuildingChoice choice = m_Data.Choices[m_Next];
  IEntity entity = Find(choice);
  if (entity && PreviousDestruction(choice)) { Failed = true; Reason = "Original identity reappeared during import"; return false; }
  if (!entity)
  {
   if (HasCompletedHistory(choice)) { EAD_Buildings.RestoreChoice(choice, null); m_Next++; return true; }
   if (SatisfiedByNativeDestruction(choice))
   {
    // Record the saved end state: a completed collapse whose original no longer exists.
    choice.Finished = true; choice.Destroyed = true;
    EAD_Buildings.RestoreChoice(choice, null); NativeSatisfied++; m_Next++;
    return true;
   }
   Failed = true; Reason = "Building disappeared during import: " + MissReason(choice); return false;
  }
  if (choice.Destroyed)
  {
   EAD_BuildingReplay replay = new EAD_BuildingReplay();
   if (!replay.Prepare(entity)) { Failed = true; Reason = "Building subtree exceeds native replay limit"; return false; }
   foreach (EAD_BuildingReplayToken part : replay.Parts)
   {
    if (Occupied(part.Entity)) { Failed = true; Reason = "Occupied building blocks saved destruction"; return false; }
   }
   if (!replay.Run()) { Failed = true; Reason = "Native destruction did not complete within replay budget"; return false; }
  }
  else if (choice.AppliedPhase > 0)
  {
   if (Occupied(entity) || !EAD_Buildings.ApplyPhase(entity, choice.AppliedPhase)) { Failed = true; Reason = "Saved native building phase did not restore"; return false; }
   SCR_DestructionMultiPhaseComponent phases = EAD_Buildings.NativePhases(entity);
   if (!phases || !phases.EAD_CompletePhaseReplay(choice.AppliedPhase)) { Failed = true; Reason = "Saved phase model/collision did not complete"; return false; }
  }
  EAD_Buildings.RestoreChoice(choice, entity); m_Next++;
  return true;
 }
 protected bool Occupied(IEntity entity)
 {
  vector mins, maxs; entity.GetWorldBounds(mins, maxs);
  m_Visits = 0; m_Matches = 0;
  m_World.QueryEntitiesByAABB(mins - "2 2 2", maxs + "2 2 2", Occupant, null, EQueryEntitiesFlags.DYNAMIC);
  return m_Matches > 0 || m_Visits > 512;
 }
 protected bool Occupant(IEntity entity)
 {
  m_Visits++;
  if (m_Visits > 512) return false;
  if (ChimeraCharacter.Cast(entity) || Vehicle.Cast(entity)) { m_Matches++; return false; }
  return true;
 }
}
