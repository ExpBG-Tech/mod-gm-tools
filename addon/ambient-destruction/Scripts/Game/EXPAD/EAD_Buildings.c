// Mission-only bounded discovery. Uses native collapse, with prefab-controlled effects.
class EAD_BuildingCell
{
 vector Min;
 int Size;
}
class EAD_BodySite
{
 vector Min, Max;
}
class EAD_BuildingChoice
{
 IEntity Entity;
 ResourceName Prefab;
 vector Transform[4];
 bool Selected;
 bool Finished;
 bool Destroyed;
 bool Authored;
 int Retries;
 // Zero retains the 0.0.6 collapse contract. Positive values are authored native phases.
 int TargetPhase;
 int AppliedPhase;
}
class EAD_Buildings
{
 static const int CELL_SIZE = 64;
 static const int MAX_CALLBACKS = 512;
 static const int MAX_CHOICES = 65536;
 // World-wide pacing of native collapses and phase changes across every zone. Each native
 // collapse runs debris, effects, sinking and interior deletion on every client; back-to-back
 // kills (one per 0.25 s step) froze a watching GM client. Scanning continues meanwhile.
 static const float COLLAPSE_INTERVAL = 3;
 protected static BaseWorld s_World;
 protected static float s_NextCollapse;
 protected static ref map<IEntity, ref EAD_BuildingChoice> s_Choices;
 protected static ref array<ref EAD_BuildingChoice> s_History;
 static void ResetLedger(BaseWorld world)
 {
		EXPBG_LazyStatics_EAD_Buildings();
  s_World = world; s_Choices.Clear(); s_History.Clear(); s_NextCollapse = 0;
 }
 static void EnsureWorld(BaseWorld world) { if (s_World != world) ResetLedger(world); }
 static void GetLedger(array<ref EAD_BuildingChoice> choices)
 {
		EXPBG_LazyStatics_EAD_Buildings();
  choices.Clear();
  foreach (EAD_BuildingChoice choice : s_History) choices.Insert(choice);
 }
 static void RestoreChoice(EAD_BuildingChoice choice, IEntity entity)
 {
		EXPBG_LazyStatics_EAD_Buildings();
  choice.Entity = entity;
  if (entity) s_Choices.Set(entity, choice);
  s_History.Insert(choice);
 }
 protected BaseWorld m_World;
 // Owning zone's server diagnostics level; 1 or more logs one line per native change.
 int DebugLevel;
 protected vector m_Center;
 protected float m_Radius;
 protected int m_Intensity, m_Seed, m_X, m_Z, m_MinX, m_MinZ, m_MaxX, m_MaxZ;
 protected int m_Callbacks, m_Destroyed, m_Unsupported;
 protected bool m_Aborted, m_Occupied, m_Finished;
 protected ref array<ref EAD_BuildingCell> m_Cells = {};
 protected ref array<ref EAD_BuildingChoice> m_Candidates = {};
 protected ref map<IEntity, bool> m_Queued = new map<IEntity, bool>();
 ref array<ref EAD_BodySite> Sites = {};
 protected ref map<IEntity, bool> m_SiteEntities = new map<IEntity, bool>();

 protected void AddBodySite(IEntity entity)
 {
  if (Sites.Count() >= 256 || m_SiteEntities.Contains(entity) || !EAD_Placement.BuildingContext(entity)) return;
  vector mins, maxs; entity.GetWorldBounds(mins, maxs);
  if (maxs[0] - mins[0] < 3 || maxs[2] - mins[2] < 3 || maxs[1] - mins[1] < 1) return;
  EAD_BodySite site = new EAD_BodySite(); site.Min = mins; site.Max = maxs;
  m_SiteEntities.Insert(entity, true); Sites.Insert(site);
 }

 // Native type alone is not capability: Building_Base has no destruction component.
 static bool IsSupported(IEntity entity)
 {
  if (!entity) return false;
  SCR_DamageManagerComponent damage = SCR_DamageManagerComponent.Cast(entity.FindComponent(SCR_DestructibleBuildingComponent));
  if (damage && !SCR_DestructibleBuildingEntity.Cast(entity)) return false;
  if (!damage)
  {
   if (!EAD_Placement.BuildingContext(entity)) return false;
   damage = NativePhases(entity);
  }
  if (!damage || !damage.GetDefaultHitZone()) return false;
  RplComponent rpl = RplComponent.Cast(entity.FindComponent(RplComponent));
  if (!rpl || rpl.IsProxy()) return false;
  IEntity parent = entity.GetParent();
  int parentDepth;
  while (parent && parentDepth < 16)
  {
   if (SCR_DestructibleBuildingEntity.Cast(parent) || parent.FindComponent(SCR_DamageManagerComponent)) return false;
   parent = parent.GetParent();
   parentDepth++;
  }
  if (parent) return false;
  EntityPrefabData data = entity.GetPrefabData();
  // Some map buildings have prefab data but no reusable prefab identity.
  // They cannot participate in exact map-ledger restore, so never destroy them.
  if (!data || data.GetPrefabName().IsEmpty() || RejectedPath(data.GetPrefabName(), true)) return false;
  BaseContainer prefab = data.GetPrefab();
  if (prefab) prefab = prefab.GetAncestor();
  for (int depth = 0; prefab && depth < 16; depth++)
  {
   if (RejectedPath(prefab.GetResourceName(), false)) return false;
   prefab = prefab.GetAncestor();
  }
  return !prefab;
 }
 // Health thresholds alone are not visual damage stages. Require real native
 // phase models and a retained final ruin; never invent meshes or delete-phase semantics.
 static SCR_DestructionMultiPhaseComponent NativePhases(IEntity entity)
 {
  if (!entity) return null;
  SCR_DestructionMultiPhaseComponent phases = SCR_DestructionMultiPhaseComponent.Cast(entity.FindComponent(SCR_DestructionMultiPhaseComponent));
  if (!phases || !phases.GetDefaultHitZone()) return null;
  SCR_DestructionMultiPhaseComponentClass data = SCR_DestructionMultiPhaseComponentClass.Cast(phases.GetComponentData(entity));
  if (!data || data.m_bDeleteAfterFinalPhase || data.m_bPassDamageToChildren || data.m_bDestroyParentWhenDestroyed || data.m_bDestroyChildrenWhenDestroyed) return null;
  int count = phases.GetNumDamagePhases();
  if (count < 3 || count > 16) return null;
  for (int phase = 1; phase < count; phase++)
  {
   SCR_DamagePhaseData stage = phases.GetDamagePhaseData(phase);
   if (!stage || stage.m_PhaseModel.IsEmpty() || stage.m_fPhaseHealth <= 0) return null;
  }
  return phases;
 }
 static bool ApplyPhase(IEntity entity, int target)
 {
  if (!Replication.IsServer() || !IsSupported(entity)) return false;
  SCR_DestructionMultiPhaseComponent phases = NativePhases(entity);
  if (!phases || target <= 0 || target >= phases.GetNumDamagePhases() || phases.GetDamagePhase() > target) return false;
  // At most fifteen native transitions. Use damage so native replication/JIP,
  // physics and navmesh changes remain owned by the existing component.
  for (int attempt = 0; attempt < 15 && phases.GetDamagePhase() < target; attempt++)
  {
   int before = phases.GetDamagePhase();
   int next = before + 1;
   float health;
   for (int phase = next; phase < phases.GetNumDamagePhases(); phase++) health += phases.GetDamagePhaseData(phase).m_fPhaseHealth;
   health -= phases.GetDamagePhaseData(next).m_fPhaseHealth * 0.5;
   if (next == phases.GetNumDamagePhases() - 1) health = 0;
   float amount = phases.GetDefaultHitZone().GetHealth() - health;
   if (amount <= 0) return false;
   vector hit[3]; hit[0] = entity.GetOrigin(); hit[1] = vector.Up; hit[2] = vector.Up;
   SCR_DamageContext context = new SCR_DamageContext(EDamageType.TRUE, amount, hit, entity, phases.GetDefaultHitZone(), Instigator.CreateInstigator(null), null, -1, -1);
   phases.HandleDamage(context);
   if (!entity || phases.GetDamagePhase() != next) return false;
  }
  return phases.GetDamagePhase() == target;
 }
 // Ruins, debris, building parts, addons and furniture; a leaf _base.et template too.
 // Memoised per distinct path (prefab and each ancestor) in EAD_Placement.PathClasses.
 protected static bool RejectedPath(ResourceName resource, bool leaf)
 {
  int classes = EAD_Placement.PathClasses(resource);
  if ((classes & EAD_PathClass.REJECTED) != 0)
   return true;
  return leaf && (classes & EAD_PathClass.TEMPLATE) != 0;
 }
 void Begin(BaseWorld world, vector center, float radius, int intensity, int seed)
 {
  if (s_World != world) ResetLedger(world);
  m_World = world;
  m_Center = center;
  m_Radius = Math.Clamp(radius, 25, 1000);
  m_Intensity = Math.Clamp(intensity, 0, 100);
  m_Seed = seed % 997;
  m_MinX = Math.Floor((center[0] - m_Radius) / CELL_SIZE);
  m_MinZ = Math.Floor((center[2] - m_Radius) / CELL_SIZE);
  m_MaxX = Math.Floor((center[0] + m_Radius) / CELL_SIZE);
  m_MaxZ = Math.Floor((center[2] + m_Radius) / CELL_SIZE);
  m_X = m_MinX; m_Z = m_MinZ;
  m_Cells.Clear(); m_Candidates.Clear(); m_Queued.Clear();
  Sites.Clear(); m_SiteEntities.Clear();
  m_Destroyed = 0; m_Unsupported = 0;
  m_Finished = !world;
 }
 int GetDestroyedCount() { return m_Destroyed; }
 int GetUnsupportedCount() { return m_Unsupported; }
 bool IsFinished() { return m_Finished; }
 // Stable position-based roll; leaves the engine global random generator untouched.
 protected bool Select(vector position)
 {
  return Selected(position, m_Intensity, m_Seed);
 }
 static bool Selected(vector position, int intensity, int seed)
 {
  if (intensity <= 0) return false;
  if (intensity == 100) return true;
  int x = Math.Floor(position[0] * 10);
  int y = Math.Floor(position[1] * 10);
  int z = Math.Floor(position[2] * 10);
  x = x % 100003;
  y = y % 100003;
  z = z % 100003;
  int roll = (x * 31 + z * 17 + y * 13 + (seed % 997) * 47) % 100;
  if (roll < 0) roll += 100;
  return roll < intensity;
 }
 // Keep decorations away from a selected building even before its bounded scan reaches it.
 static bool MayCollapse(IEntity entity, EAD_Zone zone)
 {
		EXPBG_LazyStatics_EAD_Buildings();
  if (zone.Enabled == 0 || zone.Destruction == 0 || !EAD_Policy.Near(entity.GetOrigin(), zone.GetOrigin(), zone.Radius, 0)) return false;
  EAD_BuildingChoice choice = s_Choices.Get(entity);
  if (choice) return choice.Selected && choice.TargetPhase == 0;
  if (NativePhases(entity)) return false;
  return Selected(entity.GetOrigin(), zone.Destruction, zone.Seed);
 }
 protected bool Visit(IEntity entity)
 {
		EXPBG_LazyStatics_EAD_Buildings();
  m_Callbacks++;
  if (m_Callbacks >= MAX_CALLBACKS) { m_Aborted = true; return false; }
  if (!entity) return true;
  vector position = entity.GetOrigin();
  if (vector.DistanceSqXZ(position, m_Center) > m_Radius * m_Radius) return true;
  AddBodySite(entity);
  if (m_Intensity == 0) return true;
  if (!IsSupported(entity)) { m_Unsupported++; return true; }
  if (m_Queued.Contains(entity)) return true;
  EAD_BuildingChoice choice = s_Choices.Get(entity);
  if (!choice)
  {
   if (s_Choices.Count() >= MAX_CHOICES) { m_Unsupported++; return true; }
   choice = new EAD_BuildingChoice();
   choice.Entity = entity;
   choice.Prefab = entity.GetPrefabData().GetPrefabName();
   entity.GetWorldTransform(choice.Transform);
   SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.Cast(entity.FindComponent(SCR_EditableEntityComponent));
   for (int authorDepth = 0; editable && authorDepth < 32; authorDepth++)
   {
    if (!editable.GetAuthorUID().IsEmpty()) choice.Authored = true;
    editable = editable.GetParentEntity();
   }
   choice.Selected = Select(position);
   SCR_DestructionMultiPhaseComponent phases = NativePhases(entity);
   if (phases)
   {
    choice.TargetPhase = phases.GetNumDamagePhases() - 1;
    if (Selected(position, 50, m_Seed + 431)) choice.TargetPhase--;
   }
   s_Choices.Insert(entity, choice);
   s_History.Insert(choice);
  }
  else if (!choice.Selected && Select(position)) choice.Selected = true;
  m_Queued.Insert(entity, true);
  if (choice.Selected && !choice.Finished) m_Candidates.Insert(choice);
  return true;
 }
 protected bool Occupant(IEntity entity)
 {
  m_Callbacks++;
  if (m_Callbacks >= MAX_CALLBACKS) { m_Occupied = true; return false; }
  if (entity && (ChimeraCharacter.Cast(entity) || Vehicle.Cast(entity))) { m_Occupied = true; return false; }
  return true;
 }
 // At most one spatial query per Step, globally scheduled by the shared manager.
 // Candidates wait for the world collapse interval; cell scanning does not.
 bool Step()
 {
  if (m_Finished || !Replication.IsServer() || !GetGame() || !m_World || GetGame().GetWorld() != m_World) return false;
  float now = m_World.GetWorldTime() * 0.001;
  if (s_NextCollapse > now + COLLAPSE_INTERVAL) s_NextCollapse = now; // world clock restarted
  bool scanned = m_Cells.IsEmpty() && m_Z > m_MaxZ;
  if (!m_Candidates.IsEmpty() && now >= s_NextCollapse)
  {
   EAD_BuildingChoice choice = m_Candidates[0];
   m_Candidates.RemoveOrdered(0);
   IEntity entity = choice.Entity;
   if (choice.Finished || !IsSupported(entity)) return true;
   SCR_DamageManagerComponent damage = SCR_DamageManagerComponent.Cast(entity.FindComponent(SCR_DamageManagerComponent));
   if (damage.GetDefaultHitZone().GetDamageState() != EDamageState.UNDAMAGED)
   {
    choice.Finished = true; // Preserve earlier gameplay damage.
    return true;
   }
   vector mins, maxs;
   entity.GetWorldBounds(mins, maxs);
   m_Callbacks = 0; m_Occupied = false;
   m_World.QueryEntitiesByAABB(mins - Vector(2, 2, 2), maxs + Vector(2, 2, 2), Occupant, null, EQueryEntitiesFlags.ALL);
   if (m_Occupied)
   {
    choice.Retries++;
    if (choice.Retries < 3) m_Candidates.Insert(choice);
    else choice.Finished = true; // Unsafe after bounded retries: preserve for this mission.
    return true;
   }
   // Only an actual native change consumes the world interval.
   s_NextCollapse = now + COLLAPSE_INTERVAL;
   vector position = choice.Transform[3];
   if (choice.TargetPhase > 0)
   {
    bool applied = ApplyPhase(entity, choice.TargetPhase);
    SCR_DestructionMultiPhaseComponent phases = NativePhases(entity);
    if (phases) choice.AppliedPhase = phases.GetDamagePhase();
    if (!applied) PrintFormat("[EAD] native phase incomplete prefab=%1 target=%2 actual=%3", choice.Prefab, choice.TargetPhase, choice.AppliedPhase);
    else if (DebugLevel > 0) PrintFormat("[EAD BUILDING] phase=%1 prefab=%2 pos=%3 pending=%4", choice.AppliedPhase, choice.Prefab, position, m_Candidates.Count());
    choice.Finished = true;
    return true;
   }
   // TRUE damage follows authority, reliable broadcast and native JIP serialization.
   // Does not spawn an explosion. Native collapse particles/audio remain enabled.
   damage.Kill(Instigator.CreateInstigator(null));
   choice.Finished = true;
   if (damage.GetDefaultHitZone().GetDamageState() == EDamageState.DESTROYED)
   {
    choice.Destroyed = true; m_Destroyed++;
    if (DebugLevel > 0) PrintFormat("[EAD BUILDING] killed prefab=%1 pos=%2 pending=%3", choice.Prefab, position, m_Candidates.Count());
   }
   return true;
  }
  // Scan complete: wait for paced candidates, then finish.
  if (scanned)
  {
   if (m_Candidates.IsEmpty()) m_Finished = true;
   return false;
  }
  EAD_BuildingCell cell;
  if (!m_Cells.IsEmpty())
  {
   cell = m_Cells[m_Cells.Count() - 1];
   m_Cells.Remove(m_Cells.Count() - 1);
  }
  else
  {
   cell = new EAD_BuildingCell();
   cell.Min = Vector(m_X * CELL_SIZE, -1000, m_Z * CELL_SIZE);
   cell.Size = CELL_SIZE;
   m_X++;
   if (m_X > m_MaxX) { m_X = m_MinX; m_Z++; }
  }
  m_Callbacks = 0; m_Aborted = false;
  m_World.QueryEntitiesByAABB(cell.Min, cell.Min + Vector(cell.Size, 11000, cell.Size), Visit, null, EQueryEntitiesFlags.ALL);
  if (m_Aborted && cell.Size > 8)
  {
   int half = cell.Size / 2;
   for (int x = 0; x < 2; x++)
   {
    for (int z = 0; z < 2; z++)
    {
     EAD_BuildingCell child = new EAD_BuildingCell();
     child.Min = cell.Min + Vector(x * half, 0, z * half);
     child.Size = half;
     m_Cells.Insert(child);
    }
   }
  }
  else if (m_Aborted) m_Unsupported++; // Minimum saturated tiles are incomplete.
  return true;
 }

	//------------------------------------------------------------------------------------------------
	//! Creates the collections on first use (not in the global static initializer, which has a
	//! per-function instruction limit that large modsets exceed on Windows).
	protected static void EXPBG_LazyStatics_EAD_Buildings()
	{
		if (!s_Choices)
			s_Choices = new map<IEntity, ref EAD_BuildingChoice>();
		if (!s_History)
			s_History = new array<ref EAD_BuildingChoice>();
	}
}
