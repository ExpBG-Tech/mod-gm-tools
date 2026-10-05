// Portable logical state only. Full deliberately recreates prefab-default kits.
class EBG_CachePose
{
 ResourceName Prefab;
 ref array<vector> Matrix = {};
 void Capture(IEntity entity)
 {
  Prefab = SCR_ResourceNameUtils.GetPrefabName(entity);
  vector transform[4];
  entity.GetWorldTransform(transform);
  for (int i = 0; i < 4; i++) Matrix.Insert(transform[i]);
 }
 void Transform(out vector transform[4])
 {
  for (int i = 0; i < 4; i++) transform[i] = Matrix[i];
 }
 bool Valid()
 {
  if (Prefab.IsEmpty() || Matrix.Count() != 4) return false;
  Resource resource = Resource.Load(Prefab);
  if (!resource || !resource.IsValid()) return false;
  foreach (vector row : Matrix) for (int axis = 0; axis < 3; axis++) if (!EBG_MissionPersistence.Finite(row[axis])) return false;
  return Matrix[0].LengthSq() > 0.0001 && Matrix[1].LengthSq() > 0.0001 && Matrix[2].LengthSq() > 0.0001;
 }
 bool PrefabType(typename expected, bool exact = false)
 {
  Resource resource = Resource.Load(Prefab);
  if (!resource || !resource.IsValid()) return false;
  IEntitySource source = resource.GetResource().ToEntitySource();
  if (!source) return false;
  typename actual = source.GetClassName().ToType();
  if (exact) return actual == expected;
  return actual && actual.IsInherited(expected);
 }
 bool Write(SaveContext context)
 {
  return context.WriteValue("prefab", Prefab) && context.WriteValue("matrix", Matrix);
 }
 bool Read(LoadContext context)
 {
  return context.ReadValue("prefab", Prefab) && context.ReadValue("matrix", Matrix) && Valid();
 }
}

class EBG_CacheAuthor
{
 string UID, PlatformID;
 int Platform, Updated, PlayerID;
 void Capture(IEntity entity)
 {
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.GetEditableEntity(entity);
  if (!editable) return;
  UID = editable.GetAuthorUID();
  PlatformID = editable.GetAuthorPlatformID();
  Platform = editable.GetAuthorPlatform();
  Updated = editable.GetAuthorLastUpdated();
  PlayerID = editable.GetAuthorPlayerID();
 }
 void Apply(IEntity entity)
 {
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.GetEditableEntity(entity);
  if (!editable) return;
  string previousUID = editable.GetAuthorUID();
  SCR_EditableEntityAuthor previous = editable.GetAuthor();
  SCR_EditableEntityAuthor author = new SCR_EditableEntityAuthor();
  author.Initialize(UID, PlatformID, Platform, PlayerID);
  SCR_EditableEntityCore core = SCR_EditableEntityCore.Cast(SCR_EditableEntityCore.GetInstance(SCR_EditableEntityCore));
  if (core && previous && !previousUID.IsEmpty() && previousUID != UID) core.AuthorEntityRemovedServer(previous);
  editable.SetAuthor(author);
  editable.SetAuthorUpdatedTime(Updated);
  // The saved-author setter only assigns. Native editor deletion expects its
  // ownership registry too; retries for the same entity must not count twice.
  if (core && !UID.IsEmpty() && previousUID != UID) core.RegisterAuthorServer(author);
 }
 bool Write(SaveContext context)
 {
  return context.WriteValue("authorUID", UID) && context.WriteValue("authorPlatformID", PlatformID) && context.WriteValue("authorPlatform", Platform) && context.WriteValue("authorUpdated", Updated) && context.WriteValue("authorPlayerID", PlayerID);
 }
 bool Read(LoadContext context)
 {
  return context.ReadValue("authorUID", UID) && context.ReadValue("authorPlatformID", PlatformID) && context.ReadValue("authorPlatform", Platform) && context.ReadValue("authorUpdated", Updated) && context.ReadValue("authorPlayerID", PlayerID) && UID.Length() <= 256 && PlatformID.Length() <= 256;
 }
}

[BaseContainerProps()]
modded class SCR_DefendWaypointPreset
{
 void EBG_SetSnapshotTags(array<string> tags)
 {
  // Script-created presets do not receive the prefab container's array.
  if (!m_aTagsForSearch) m_aTagsForSearch = new array<string>();
  SetTagsForSearch(tags);
 }
}

class EBG_CacheDefendPreset
{
 string Name;
 bool Turrets;
 float Fraction;
 ref array<string> Tags = {};
 void Capture(SCR_DefendWaypointPreset preset)
 {
  Name = preset.GetPresetName();
  Turrets = preset.GetUseTurrets();
  Fraction = preset.GetFractionOfSA();
  preset.GetTagsForSearch(Tags);
 }
 SCR_DefendWaypointPreset Create()
 {
  SCR_DefendWaypointPreset preset = new SCR_DefendWaypointPreset();
  preset.SetPresetName(Name);
  preset.SetUseTurrets(Turrets);
  preset.SetFractionOfSA(Fraction);
  preset.EBG_SetSnapshotTags(Tags);
  return preset;
 }
 bool Write(SaveContext context)
 {
  return context.WriteValue("name", Name) && context.WriteValue("turrets", Turrets) && context.WriteValue("fraction", Fraction) && context.WriteValue("tags", Tags);
 }
 bool Read(LoadContext context)
 {
  if (!context.ReadValue("name", Name) || !context.ReadValue("turrets", Turrets) || !context.ReadValue("fraction", Fraction) || !context.ReadValue("tags", Tags) || Name.Length() > 512 || !EBG_MissionPersistence.Finite(Fraction) || Fraction < 0 || Fraction > 1 || Tags.Count() > 32) return false;
  foreach (string tag : Tags) if (tag.Length() > 256) return false;
  return true;
 }
}

modded class SCR_DefendWaypoint
{
 bool EBG_CapturePresets(array<ref EBG_CacheDefendPreset> rows)
 {
  if (!m_aDefendPresets || !m_aDefendPresets.IsIndexValid(m_iCurrentDefendPreset) || !m_aDefendPresets[m_iCurrentDefendPreset]) return false;
  foreach (SCR_DefendWaypointPreset preset : m_aDefendPresets)
  {
   if (!preset) return false;
   EBG_CacheDefendPreset row = new EBG_CacheDefendPreset();
   row.Capture(preset);
   rows.Insert(row);
  }
  return true;
 }
 bool EBG_ApplyPresets(array<ref EBG_CacheDefendPreset> rows, int current)
 {
  ClearDefendPresets();
  if (!m_aDefendPresets) m_aDefendPresets = new array<ref SCR_DefendWaypointPreset>();
  foreach (EBG_CacheDefendPreset row : rows) AddDefendPreset(row.Create());
  if (rows.IsEmpty()) return false;
  return SetCurrentDefendPreset(current);
 }
}

class EBG_CacheOrder : EBG_CachePose
{
 string Kind;
 float Radius, Precision, Priority, Hold;
 int Completion, Preset;
 bool FastInit;
 ref EBG_CacheAuthor Author = new EBG_CacheAuthor();
 ref array<ref EBG_CacheDefendPreset> Presets = {};
 AIWaypoint Entity;
 bool RestoreBound;
 string CaptureProblem;
 bool CaptureOrder(AIWaypoint waypoint)
 {
  if (!EBG_PrefabFullCache.CanDeleteFullEntity(waypoint))
  {
   CaptureProblem = "Native editor protects this waypoint from deletion";
   return false;
  }
  Kind = waypoint.Type().ToString();
  if (Kind != "AIWaypoint" && Kind != "SCR_AIWaypoint" && Kind != "SCR_TimedWaypoint" && Kind != "SCR_DefendWaypoint")
  {
   CaptureProblem = "Unsupported waypoint class " + Kind;
   return false;
  }
  SCR_AIWaypoint scripted = SCR_AIWaypoint.Cast(waypoint);
  if (scripted)
  {
   array<SCR_AISettingBase> settings = {};
   scripted.GetSettings(settings);
   // Unknown live setting objects are not silently reduced to prefab defaults.
   if (!settings.IsEmpty())
   {
    CaptureProblem = "Unsupported waypoint setting " + settings[0].Type().ToString();
    return false;
   }
   Priority = scripted.GetPriorityLevel();
  }
  Capture(waypoint);
  Author.Capture(waypoint);
  Entity = waypoint;
  Radius = waypoint.GetCompletionRadius();
  Precision = waypoint.GetCompletionYPrecision();
  Completion = waypoint.GetCompletionType();
  SCR_TimedWaypoint timed = SCR_TimedWaypoint.Cast(waypoint);
  if (timed) Hold = timed.GetHoldingTime();
  SCR_DefendWaypoint defend = SCR_DefendWaypoint.Cast(waypoint);
  if (defend)
  {
   FastInit = defend.GetFastInit();
   Preset = defend.GetCurrentDefendPresetIndex();
   if (!defend.EBG_CapturePresets(Presets))
   {
    CaptureProblem = "Defend waypoint has no valid selected native preset";
    return false;
   }
  }
  if (!ValidOrder())
  {
   CaptureProblem = string.Format("Waypoint=%1 prefab=%2 poseValid=%3 prefabTypeMatches=%4 radius=%5 precision=%6 priority=%7 hold=%8 completion=%9", Kind, Prefab, Valid(), PrefabType(Kind.ToType(), true), Radius, Precision, Priority, Hold, Completion);
   CaptureProblem += string.Format(" preset=%1 presets=%2", Preset, Presets.Count());
   return false;
  }
  return true;
 }
 bool ValidOrder()
 {
  if (!Valid() || (Kind != "AIWaypoint" && Kind != "SCR_AIWaypoint" && Kind != "SCR_TimedWaypoint" && Kind != "SCR_DefendWaypoint") || !PrefabType(Kind.ToType(), true) || Presets.Count() > 32) return false;
  if (!EBG_MissionPersistence.Finite(Radius) || !EBG_MissionPersistence.Finite(Precision) || !EBG_MissionPersistence.Finite(Priority) || !EBG_MissionPersistence.Finite(Hold) || Radius < 0 || Precision < -1 || Hold < -1 || Completion < 0 || Completion > 8) return false;
  if (Kind == "SCR_DefendWaypoint") return !Presets.IsEmpty() && Preset >= 0 && Preset < Presets.Count();
  return Presets.IsEmpty() && Preset == 0;
 }
 bool Spawn()
 {
  if (!Entity)
  {
   // Retrying settings never replaces an order removed after restoration began.
   if (RestoreBound) { return false; }
   EntitySpawnParams params = new EntitySpawnParams();
   params.TransformMode = ETransformMode.WORLD;
   Transform(params.Transform);
   // Keep the loaded resource alive through the spawn call.
   Resource orderResource = Resource.Load(Prefab);
   Entity = AIWaypoint.Cast(GetGame().SpawnEntityPrefab(orderResource, GetGame().GetWorld(), params));
  }
  if (!Entity) { return false; }
  RestoreBound = true;
  if (Entity.Type().ToString() != Kind) { return false; }
  Entity.SetCompletionRadius(Radius);
  Entity.SetCompletionYPrecision(Precision);
  Entity.SetCompletionType(Completion);
  SCR_AIWaypoint scripted = SCR_AIWaypoint.Cast(Entity);
  if (scripted) scripted.SetPriorityLevel(Priority);
  SCR_TimedWaypoint timed = SCR_TimedWaypoint.Cast(Entity);
  if (timed) timed.SetHoldingTime(Hold);
  SCR_DefendWaypoint defend = SCR_DefendWaypoint.Cast(Entity);
  if (defend)
  {
   defend.SetFastInit(FastInit);
   if (!defend.EBG_ApplyPresets(Presets, Preset)) return false;
  }
  Author.Apply(Entity);
  return true;
 }
 override bool Write(SaveContext context)
 {
  if (!super.Write(context) || !Author.Write(context) || !context.WriteValue("kind", Kind) || !context.WriteValue("radius", Radius) || !context.WriteValue("precision", Precision) || !context.WriteValue("priority", Priority) || !context.WriteValue("hold", Hold) || !context.WriteValue("completion", Completion) || !context.WriteValue("preset", Preset) || !context.WriteValue("fastInit", FastInit) || !context.WriteValue("presets", Presets.Count())) return false;
  for (int i = 0; i < Presets.Count(); i++) if (!context.StartObject("preset" + i.ToString()) || !Presets[i].Write(context) || !context.EndObject()) return false;
  return true;
 }
 override bool Read(LoadContext context)
 {
  int count;
  if (!super.Read(context) || !Author.Read(context) || !context.ReadValue("kind", Kind) || !context.ReadValue("radius", Radius) || !context.ReadValue("precision", Precision) || !context.ReadValue("priority", Priority) || !context.ReadValue("hold", Hold) || !context.ReadValue("completion", Completion) || !context.ReadValue("preset", Preset) || !context.ReadValue("fastInit", FastInit) || !context.ReadValue("presets", count) || count < 0 || count > 32) return false;
  for (int i = 0; i < count; i++)
  {
   EBG_CacheDefendPreset row = new EBG_CacheDefendPreset();
   if (!context.StartObject("preset" + i.ToString()) || !row.Read(context) || !context.EndObject()) return false;
   Presets.Insert(row);
  }
  return ValidOrder();
 }
}

class EBG_CacheGroupSnapshot : EBG_CachePose
{
 int Token, Dead;
 string FactionName, Formation, Name, Description;
 ResourceName Flag;
 ref array<int> Settings = {};
 ref EBG_CacheAuthor Author = new EBG_CacheAuthor();
 ref array<ref EBG_CacheOrder> Orders = {};
 bool Cycled;
 int CycleReruns;
 ref array<int> CycleQueue = {};
 bool OrdersBound;
 protected int m_NextRestoreOrder;
 SCR_EditableEntityComponent Parent;
 string CaptureProblem;
 protected bool CaptureRefusal(string reason)
 {
  CaptureProblem = reason;
  return false;
 }
 // Only anonymous, unparented native Defend orders have a supported non-editor
 // ownership path. Named/scene orders and unknown mod order classes stay held.
 protected static bool NativeDefend(AIWaypoint order)
 {
  return order && order.Type() == SCR_DefendWaypoint && order.GetName().IsEmpty() && !order.GetParent() && !order.GetChildren() && !SCR_EditableEntityComponent.GetEditableEntity(order);
 }
 protected static bool NativeOrdersUnshared(SCR_AIGroup owner, set<AIWaypoint> targets)
 {
  if (targets.IsEmpty()) { return true; }
  AIWorld world = GetGame().GetAIWorld();
  if (!world || !owner) { return false; }
  array<AIAgent> agents = {};
  world.GetAIAgents(agents);
  if (agents.Count() > 16384) { return false; }
  set<AIGroup> visited = new set<AIGroup>();
  int remaining = 8192;
  bool ownerFound;
  // Transition-only, bounded proof across ALL native groups, including groups
  // outside Optimizer and inactive groups. Never infer ownership from our roster.
  foreach (AIAgent agent : agents)
  {
   if (!agent) { continue; }
   AIGroup group = AIGroup.Cast(agent);
   if (!group) { group = agent.GetParentGroup(); }
   if (!group || visited.Contains(group)) { continue; }
   visited.Insert(group);
   if (group == owner)
   {
    ownerFound = true;
    continue;
   }
   array<AIWaypoint> queue = {};
   group.GetWaypoints(queue);
   SCR_AIGroup scripted = SCR_AIGroup.Cast(group);
   if (scripted) { scripted.EBG_AppendRetainedOrders(queue); }
   SCR_EditableGroupComponent editable = SCR_EditableGroupComponent.Cast(SCR_EditableEntityComponent.GetEditableEntity(group));
   if (editable)
   {
    array<AIWaypoint> editorOrders = {};
    editable.EBG_SnapshotOrders(editorOrders);
    queue.InsertAll(editorOrders);
   }
   set<AIWaypointCycle> cycles = new set<AIWaypointCycle>();
   for (int i = 0; i < queue.Count(); i++)
   {
    if (queue.Count() - i > remaining || --remaining < 0) { return false; }
    AIWaypoint waypoint = queue[i];
    if (!waypoint) { continue; }
    if (targets.Contains(waypoint)) { return false; }
    AIWaypointCycle cycle = AIWaypointCycle.Cast(waypoint);
    if (!cycle || cycles.Contains(cycle)) { continue; }
    cycles.Insert(cycle);
    array<AIWaypoint> cycled = {};
    cycle.GetWaypoints(cycled);
    if (cycled.Count() > remaining) { return false; }
    queue.InsertAll(cycled);
   }
  }
  return ownerFound;
 }
 bool OwnsOrders(SCR_AIGroup group, bool restoring = false)
 {
  if (!group) { return false; }
  SCR_EditableGroupComponent editable = SCR_EditableGroupComponent.Cast(SCR_EditableEntityComponent.GetEditableEntity(group));
  array<AIWaypoint> queued = {};
  if (!restoring)
  {
   if (editable) { editable.EBG_SnapshotOrders(queued); }
   else { group.GetWaypoints(queued); }
   if (queued.Count() != Orders.Count()) { return false; }
  }
  set<AIWaypoint> nativeOrders = new set<AIWaypoint>();
  foreach (EBG_CacheOrder row : Orders)
  {
   if (!row.Entity)
   {
    if (!restoring || row.RestoreBound) { return false; }
    continue;
   }
   if (!restoring && !queued.Contains(row.Entity)) { return false; }
   SCR_EditableEntityComponent child = SCR_EditableEntityComponent.GetEditableEntity(row.Entity);
   if (child)
   {
    if (child.GetParentEntity() != editable && (!restoring || child.GetParentEntity())) { return false; }
   }
   else
   {
    if (!NativeDefend(row.Entity)) { return false; }
    nativeOrders.Insert(row.Entity);
   }
   if (!restoring && !EBG_PrefabFullCache.CanDeleteFullEntity(row.Entity)) { return false; }
  }
  // GME garrisons use one native Defend. Mixed/cycled native queues stay held;
  // do not extend deletion semantics to arbitrary mod waypoint graphs.
  if (!nativeOrders.IsEmpty() && (Cycled || Orders.Count() != 1)) { return false; }
  return NativeOrdersUnshared(group, nativeOrders);
 }
 bool CaptureGroup(SCR_AIGroup group)
 {
  if (group.Type() != SCR_AIGroup || group.IsPlayable() || group.GetMaster() || group.GetSlave() || group.IsCreatedByCommander() || group.GetRallyPointId() >= 0) return CaptureRefusal("Full capture holds externally managed, playable, linked or custom group type " + group.Type().ToString());
  SCR_AIGroupSettingsComponent settings = SCR_AIGroupSettingsComponent.Cast(group.FindComponent(SCR_AIGroupSettingsComponent));
  if (settings)
  {
   array<SCR_AISettingBase> entries = {};
   settings.GetAllSettings(entries);
   foreach (SCR_AISettingBase entry : entries)
   if (entry.GetOrigin() != SCR_EAISettingOrigin.DEFAULT && entry.GetOrigin() != SCR_EAISettingOrigin.WAYPOINT) return CaptureRefusal("Full capture holds unsupported dynamic AI setting " + entry.Type().ToString());
  }
  Capture(group);
  Author.Capture(group);
  FactionName = group.GetFactionName();
  AIFormationComponent formation = group.GetFormationComponent();
  if (formation && formation.GetFormation()) Formation = formation.GetFormation().GetName();
  Name = group.GetCustomName();
  Description = group.GetCustomDescription();
  Flag = group.GetGroupFlag();
  SCR_AIGroupUtilityComponent utility = group.GetGroupUtilityComponent();
  int combat;
  if (utility) combat = utility.GetCombatModeExternal();
  Settings = {group.EBG_NativeDeleteWhenEmpty(), group.GetDeleteIfNoPlayer(), group.GetDormantAliveCount(), group.GetDormantDeadCount(), group.GetRadioFrequency(), group.GetMaxMembers(), group.IsPrivate(), group.IsPrivacyChangeable(), group.GetGroupRole(), group.GetRequiredRank(), group.GetGroupID(), group.GetDefaultActiveRadioChannel(), group.GetFlagIsFromImageSet(), group.GetNameAuthorID(), group.GetDescriptionAuthorID(), combat, group.GetPermanentLOD(), group.GetLOD(), group.IsAIActivated()};
  SCR_EditableGroupComponent editable = SCR_EditableGroupComponent.Cast(SCR_EditableEntityComponent.GetEditableEntity(group));
  array<AIWaypoint> nativeOrderedWaypoints = {};
  if (editable)
  {
   Cycled = editable.EBG_SavedCycleEnabled();
   editable.EBG_SnapshotOrders(nativeOrderedWaypoints);
   Parent = editable.GetParentEntity();
  }
  else group.GetWaypoints(nativeOrderedWaypoints);
  if (nativeOrderedWaypoints.Count() > 128) return CaptureRefusal("Full capture holds waypoint count above 128");
  array<AIWaypoint> seen = {};
  foreach (AIWaypoint waypoint : nativeOrderedWaypoints)
  {
   if (!waypoint || AIWaypointCycle.Cast(waypoint) || seen.Contains(waypoint)) return CaptureRefusal("Full capture holds missing, nested-cycle or duplicate ordered waypoint");
   seen.Insert(waypoint);
   EBG_CacheOrder order = new EBG_CacheOrder();
   if (!order.CaptureOrder(waypoint)) return CaptureRefusal("Full capture holds: " + order.CaptureProblem);
   Orders.Insert(order);
  }
  if (!OwnsOrders(group)) return CaptureRefusal("Full capture holds shared, unsupported or unresolved waypoint ownership");
  if (Cycled && (!editable || !editable.EBG_SnapshotCycleQueue(nativeOrderedWaypoints, CycleQueue, CycleReruns))) return CaptureRefusal("Full capture holds unresolved native cycle queue");
  if (!ValidGroup()) return CaptureRefusal("Full capture holds unavailable group prefab/faction/formation or invalid supported group scalars");
  return true;
 }
 bool ValidGroup()
 {
  if (!Valid() || !PrefabType(SCR_AIGroup, true) || Settings.Count() != 19 || Orders.Count() > 128 || Dead < 0 || Dead > 128 || FactionName.IsEmpty() || Name.Length() > 4096 || Description.Length() > 4096 || Formation.Length() > 256) return false;
  array<int> booleans = {0, 1, 6, 7, 12, 18};
  foreach (int key : booleans) if (Settings[key] < 0 || Settings[key] > 1) return false;
  if (Settings[2] < -1 || Settings[2] > 128 || Settings[3] < 0 || Settings[3] > 128 || Settings[5] < 0 || Settings[5] > 10000 || Settings[16] != -1 || Settings[17] < 0 || Settings[17] > AIAgent.GetMaxLOD()) return false;
  typename roleType = SCR_EGroupRole, rankType = SCR_ECharacterRank, combatType = EAIGroupCombatMode;
  if (Settings[8] < 0 || Settings[8] >= roleType.GetVariableCount() || Settings[9] < 0 || Settings[9] >= rankType.GetVariableCount() || Settings[15] < 0 || Settings[15] >= combatType.GetVariableCount()) return false;
  FactionManager factions = GetGame().GetFactionManager();
  if (!factions || !factions.GetFactionByKey(FactionName)) return false;
  if (!Formation.IsEmpty() && (!GetGame().GetAIWorld() || !GetGame().GetAIWorld().GetFormation(Formation))) return false;
  foreach (EBG_CacheOrder order : Orders) if (!order.ValidOrder()) return false;
  if (CycleQueue.Count() > 256 || CycleReruns < -1) return false;
  if (!Cycled && (!CycleQueue.IsEmpty() || CycleReruns != 0)) return false;
  foreach (int queued : CycleQueue) if (queued < 0 || queued >= Orders.Count()) return false;
  return true;
 }
 // Detach retained orders before deleting a native group. Its destructor owns
 // prefab-created waypoint references; release those references too.
 void Detach(SCR_AIGroup group)
 {
  SCR_EditableGroupComponent editable = SCR_EditableGroupComponent.Cast(SCR_EditableEntityComponent.GetEditableEntity(group));
  if (editable && Cycled) editable.EnableCycledWaypoints(false);
  // Keep queue entries until their explicit native editable deletion below.
  // Removing an ordinary order here invokes OnWaypointRemoved, which deletes
  // it through the editor and would make a second ownership removal unsafe.
  group.EBG_DetachCachedWaypointReferences();
 }
 bool Apply(SCR_AIGroup group)
 {
  if (!group.SetFaction(GetGame().GetFactionManager().GetFactionByKey(FactionName))) return false;
  group.SetCanDeleteIfNoPlayer(false);
  group.SetDeleteWhenEmpty(false);
  group.EBG_PrefabCacheHeld = true;
  group.SetRadioFrequency(Settings[4]);
  group.SetMaxMembers(Settings[5]);
  group.SetPrivate(Settings[6]);
  group.SetPrivacyChangeable(Settings[7]);
  group.SetGroupRole(Settings[8]);
  group.SetRequiredRank(Settings[9]);
  group.SetGroupID(Settings[10]);
  group.SetDefaultActiveRadioChannel(Settings[11]);
  group.SetFlagIsFromImageSet(Settings[12]);
  group.SetCustomGroupFlag(Flag);
  group.SetCustomName(Name, Settings[13]);
  group.SetCustomDescription(Description, Settings[14]);
  SCR_AIGroupUtilityComponent utility = group.GetGroupUtilityComponent();
  if (utility) utility.SetCombatMode(Settings[15]);
  if (!Formation.IsEmpty() && (!group.GetFormationComponent() || !group.GetFormationComponent().SetFormation(Formation))) return false;
  Author.Apply(group);
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.GetEditableEntity(group);
  if (editable && Parent) editable.SetParentEntity(Parent);
  return true;
 }
 bool RestoreOrders(SCR_AIGroup group)
 {
  SCR_EditableGroupComponent editable = SCR_EditableGroupComponent.Cast(SCR_EditableEntityComponent.GetEditableEntity(group));
  // A retained order is not permission to undo an intervening GM reassignment.
  // Check every existing order before setters, spawning or editor reparenting.
  if (!OwnsOrders(group, true)) { return false; }
  if (OrdersBound) { return true; }
  // True permits continued progress; OrdersBound alone confirms completion.
  // Keep the same coordinator budget for orders as for individual survivors.
  if (m_NextRestoreOrder < Orders.Count())
  {
   if (!Orders[m_NextRestoreOrder].Spawn()) { return false; }
   m_NextRestoreOrder++;
   if (m_NextRestoreOrder < Orders.Count()) { return true; }
  }
  if (!OwnsOrders(group, true)) { return false; }
  foreach (EBG_CacheOrder row : Orders)
  {
   SCR_EditableEntityComponent child = SCR_EditableEntityComponent.GetEditableEntity(row.Entity);
   if (child && editable) child.SetParentEntity(editable);
   array<AIWaypoint> queued = {};
   group.GetWaypoints(queued);
   if (!queued.Contains(row.Entity)) group.AddWaypoint(row.Entity);
  }
  if (Cycled)
  {
   if (!editable) return false;
   editable.EnableCycledWaypoints(true);
   array<AIWaypoint> restored = {};
   foreach (EBG_CacheOrder order : Orders) restored.Insert(order.Entity);
   if (!editable.EBG_RestoreSnapshotCycleQueue(restored, CycleQueue, CycleReruns)) return false;
  }
  if (!OwnsOrders(group)) { return false; }
  OrdersBound = true;
  return true;
 }
 override bool Write(SaveContext context)
 {
  if (!super.Write(context) || !Author.Write(context) || !context.WriteValue("token", Token) || !context.WriteValue("dead", Dead) || !context.WriteValue("faction", FactionName) || !context.WriteValue("formation", Formation) || !context.WriteValue("name", Name) || !context.WriteValue("description", Description) || !context.WriteValue("flag", Flag) || !context.WriteValue("groupSettings", Settings) || !context.WriteValue("cycled", Cycled) || !context.WriteValue("cycleQueue", CycleQueue) || !context.WriteValue("cycleReruns", CycleReruns) || !context.WriteValue("orders", Orders.Count())) return false;
  for (int i = 0; i < Orders.Count(); i++) if (!context.StartObject("order" + i.ToString()) || !Orders[i].Write(context) || !context.EndObject()) return false;
  return true;
 }
 override bool Read(LoadContext context)
 {
  int count;
  if (!super.Read(context) || !Author.Read(context) || !context.ReadValue("token", Token) || !context.ReadValue("dead", Dead) || !context.ReadValue("faction", FactionName) || !context.ReadValue("formation", Formation) || !context.ReadValue("name", Name) || !context.ReadValue("description", Description) || !context.ReadValue("flag", Flag) || !context.ReadValue("groupSettings", Settings) || !context.ReadValue("cycled", Cycled) || !context.ReadValue("cycleQueue", CycleQueue) || !context.ReadValue("cycleReruns", CycleReruns) || !context.ReadValue("orders", count) || Token <= 0 || count < 0 || count > 128) return false;
  for (int i = 0; i < count; i++)
  {
   EBG_CacheOrder row = new EBG_CacheOrder();
   if (!context.StartObject("order" + i.ToString()) || !row.Read(context) || !context.EndObject()) return false;
   Orders.Insert(row);
  }
  return ValidGroup();
 }
}

modded class SCR_AIGroup
{
 void EBG_AppendRetainedOrders(array<AIWaypoint> orders)
 {
  // Prefab-created orders can outlive their queue entry and remain destructor-owned.
  if (!m_aSceneWaypointInstances) { return; }
  foreach (IEntity entity : m_aSceneWaypointInstances)
  {
   AIWaypoint order = AIWaypoint.Cast(entity);
   if (order) { orders.Insert(order); }
  }
 }
 static bool EBG_SnapshotIgnoresTerrain()
 {
  return s_bIgnoreSnapToTerrain;
 }
 void EBG_DetachCachedWaypointReferences()
 {
  // Native owns scene-spawned unit references too, including existing corpses.
  // Complete group deletion must not turn caching into casualty cleanup.
  ClearRefs(m_aSceneGroupUnitInstances);
  ClearRefs(m_aSceneWaypointInstances);
  if (m_aStaticWaypoints) m_aStaticWaypoints.Clear();
 }
}

modded class SCR_EditableGroupComponent
{
 void EBG_SnapshotOrders(array<AIWaypoint> orders)
 {
  GetGroupWaypoints(orders);
 }
 bool EBG_SnapshotCycleQueue(array<AIWaypoint> orders, array<int> queue, out int reruns)
 {
  if (!m_Group || !m_CycleWaypoint) return false;
  reruns = m_CycleWaypoint.GetRerunCounter();
  array<AIWaypoint> nativeQueue = {};
  m_Group.GetWaypoints(nativeQueue);
  foreach (AIWaypoint waypoint : nativeQueue)
  {
   if (waypoint == m_CycleWaypoint) continue;
   int index = orders.Find(waypoint);
   if (index < 0) return false;
   queue.Insert(index);
  }
  return true;
 }
 bool EBG_RestoreSnapshotCycleQueue(array<AIWaypoint> orders, array<int> queue, int reruns)
 {
  if (!m_Group || !m_CycleWaypoint) return false;
  array<AIWaypoint> nativeQueue = {};
  m_Group.GetWaypoints(nativeQueue);
  for (int i = 0; i < nativeQueue.Count(); i++) m_Group.RemoveWaypointAt(0);
  m_CycleWaypoint.SetWaypoints(orders);
  m_CycleWaypoint.SetRerunCounter(reruns);
  foreach (int index : queue) m_Group.AddWaypoint(orders[index]);
  m_Group.AddWaypoint(m_CycleWaypoint);
  ReindexWaypoints();
  return true;
 }
}

class EBG_CacheSnapshot
{
 static bool Loading;
 protected static bool s_ImportPrepared;
 static void ShutdownForWorldCleanup()
 {
  // World teardown owns saving policy. Clear only portable import latches.
  Loading = false;
  s_ImportPrepared = false;
 }
 static bool PrepareImport(out string reason)
 {
  reason = "";
  if (!Replication.IsServer() || Loading)
  {
   reason = "Optimizer import is already loading or unloading";
   return false;
  }
  EBG_CacheManager manager = EBG_CacheManager.ReadyForPortableImport();
  if (!manager)
  {
   reason = "Optimizer import requires an active world outside teardown";
   return false;
  }
  if (!EBG_FullSaveGate.TryAcquire(manager, reason)) return false;
  s_ImportPrepared = true;
  return true;
 }
 static void EndImport()
 {
  Loading = false;
  s_ImportPrepared = false;
  if (EBG_CacheManager.Instance) EBG_FullSaveGate.CancelUncommittedAcquire(EBG_CacheManager.Instance);
 }
 static bool CanSave(out string reason)
 {
  reason = "";
  if (!Replication.IsServer() || Loading || !EBG_CacheManager.IsPortableWorldReady() || EBG_FullCacheGroup.IsNativeOperationBusy() || SCR_AIGroupSerializer.EBG_HasPendingMemberCallbacks())
  {
   reason = "Optimizer loading or native restoration is in progress";
   return false;
  }
  if (EBG_OptimizerControl.Preparing && EBG_OptimizerControl.State != 2)
  {
   reason = "Global preparation is not Ready; restoration/recovery remains pending";
   return false;
  }
  foreach (EBG_CacheZone zone : EBG_CacheZone.Zones) if (zone && (zone.HasPendingSettings() || zone.EBG_HasSettingsLoadHold()))
  {
   reason = "Module settings are still loading or pending";
   return false;
  }
  EBG_CacheManager manager = EBG_CacheManager.Instance;
  if (!manager) return true;
  foreach (EBG_CacheGroup record : manager.Records)
  {
   if (record.Recovery != "" || record.PersistenceIssue != "" || record.PersistentScalarRollbackPending || record.ReleaseRequested || record.WakeRequested || (record.Simulation && !record.Simulation.Suspended))
   {
    reason = "Optimizer recovery or restoration is pending";
    return false;
   }
   if (record.Full)
   {
    EBG_PrefabFullCache full = EBG_PrefabFullCache.Cast(record.Full);
    if (!record.Zone || !EBG_CacheZone.Zones.Contains(record.Zone))
    {
     reason = "Full snapshot has no registered saveable zone; restore orphan recovery before saving";
     return false;
    }
    if (!full || !full.CanExport(reason)) return false;
   }
  }
  return true;
 }
 static bool WriteZone(EBG_CacheZone zone, out string json)
 {
  json = "";
  string reason;
  if (!zone || !CanSave(reason))
  {
   Print("[EBG SNAPSHOT EXPORT] authority/readiness: " + reason, LogLevel.WARNING);
   return false;
  }
  JsonSaveContext context = new JsonSaveContext();
  if (!context.WriteValue("ebgCacheVersion", 1))
  {
   Print("[EBG SNAPSHOT EXPORT] version write failed", LogLevel.WARNING);
   return false;
  }
  if (!zone.EBG_WritePersistentSettings(context))
  {
   Print("[EBG SNAPSHOT EXPORT] module settings write failed", LogLevel.WARNING);
   return false;
  }
  array<EBG_PrefabFullCache> groups = {};
  EBG_CacheManager manager = EBG_CacheManager.Instance;
  if (manager) foreach (EBG_CacheGroup record : manager.Records) if (record.Zone == zone && record.Full) groups.Insert(EBG_PrefabFullCache.Cast(record.Full));
  if (groups.Count() > 2048 || !context.WriteValue("cachedGroups", groups.Count()))
  {
   Print("[EBG SNAPSHOT EXPORT] group count write failed", LogLevel.WARNING);
   return false;
  }
  for (int i = 0; i < groups.Count(); i++) if (!context.StartObject("cached" + i.ToString()) || !groups[i].WriteSnapshot(context) || !context.EndObject())
  {
   PrintFormat("[EBG SNAPSHOT EXPORT] group snapshot write failed index=%1", i);
   return false;
  }
  if (!context.IsValid())
  {
   Print("[EBG SNAPSHOT EXPORT] JSON context invalid after writes", LogLevel.WARNING);
   return false;
  }
  json = context.SaveToString();
  if (json.IsEmpty()) Print("[EBG SNAPSHOT EXPORT] serialized JSON is empty", LogLevel.WARNING);
  return !json.IsEmpty();
 }
 protected static bool Decode(string json, out SCR_PersistenceJsonLoadContext context, array<ref EBG_PrefabFullCache> groups, out string reason)
 {
  reason = "Invalid optimizer snapshot or unavailable prefab";
  if (json.IsEmpty() || json.Length() > 16000000) return false;
  context = new SCR_PersistenceJsonLoadContext();
  int version, count, settingsVersion;
  array<int> settings = {}, pendingKeys = {};
  array<float> pendingValues = {};
  if (!context.LoadFromString(json) || !context.ReadValue("ebgCacheVersion", version) || version != 1 || !context.ReadValue("ebgZoneVersion", settingsVersion) || (settingsVersion != 4 && settingsVersion != 5) || !context.ReadValue("settings", settings) || settings.Count() != settingsVersion + 19 || !context.ReadValue("pendingKeys", pendingKeys) || !context.ReadValue("pendingValues", pendingValues) || !pendingKeys.IsEmpty() || !pendingValues.IsEmpty() || !context.ReadValue("cachedGroups", count) || count < 0 || count > 2048) return false;
  array<int> tokens = {};
  for (int i = 0; i < count; i++)
  {
   EBG_PrefabFullCache full = new EBG_DurableFullCache();
   if (!context.StartObject("cached" + i.ToString()) || !full.ReadSnapshot(context) || !context.EndObject() || tokens.Contains(full.SnapshotToken())) return false;
   tokens.Insert(full.SnapshotToken());
   groups.Insert(full);
  }
  reason = "";
  return true;
 }
 static bool ValidateZone(string json, out string reason)
 {
  SCR_PersistenceJsonLoadContext context;
  array<ref EBG_PrefabFullCache> groups = {};
  return Decode(json, context, groups, reason);
 }
 static bool ReadZone(EBG_CacheZone zone, string json)
 {
  if (!Replication.IsServer() || !Loading || !zone || zone.EBG_HasImportedCacheSnapshot()) return false;
  SCR_PersistenceJsonLoadContext context;
  array<ref EBG_PrefabFullCache> groups = {};
  string reason;
  if (!Decode(json, context, groups, reason)) return false;
  EBG_CacheManager manager = EBG_CacheManager.Get();
  foreach (EBG_CacheGroup existing : manager.Records) if (existing.Zone == zone) return false;
  // The adapter acquires this before clearing the old CDF world. A second
  // acquisition here would turn save-policy refusal into data loss after Clear.
  if (!groups.IsEmpty() && !s_ImportPrepared) return false;
  if (!zone.EBG_ReadPersistentSettings(context)) return false;
  zone.EBG_CompletePortableSettingsLoad();
  foreach (EBG_PrefabFullCache full : groups) full.Import(manager, zone);
  zone.EBG_MarkImportedCacheSnapshot();
  manager.Register(zone);
  return true;
 }
}

modded class EBG_CacheZone
{
 protected bool m_EBG_ImportedCacheSnapshot;
 bool EBG_HasImportedCacheSnapshot()
 {
  return m_EBG_ImportedCacheSnapshot;
 }
 void EBG_MarkImportedCacheSnapshot()
 {
  m_EBG_ImportedCacheSnapshot = true;
 }
}
