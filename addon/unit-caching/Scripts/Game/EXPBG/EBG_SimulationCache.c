// EXPBG GM Optimizer | M.Pac and K.Edgar
// 1.8.0.13 candidate. Multiplayer acceptance is tracked in Docs/SimulationAudit.md.
class EBG_SimulationPart
{
 IEntity Entity;
 // The character whose snapshot array holds this part (weak), and the EntityID under
 // which SCR_ChimeraCharacter indexes it. Kept at every insert, move and removal.
 SCR_ChimeraCharacter Owner;
 EntityID Key;
 EntityFlags Flags;
 bool PhysicsCaptured;
 SimulationState Simulation;
 vector Velocity;
 vector AngularVelocity;
 PerceivableComponent Perceivable;
 bool PerceivableActive;
 BaseActionsManagerComponent Actions;
 bool ActionsActive;
 LightEntity Light;
 bool LightEnabled;
 InventoryItemComponent Inventory;
 InventoryStorageSlot OriginalSlot;
 void Capture(IEntity entity)
 {
  Entity = entity;
  Light = LightEntity.Cast(entity);
  if (Light) LightEnabled = Light.IsEnabled();
  Flags = entity.GetFlags() & (EntityFlags.VISIBLE | EntityFlags.TRACEABLE);
  CapturePhysics();
  Perceivable = PerceivableComponent.Cast(entity.FindComponent(PerceivableComponent));
  if (Perceivable) PerceivableActive = Perceivable.IsActive();
  Actions = BaseActionsManagerComponent.Cast(entity.FindComponent(BaseActionsManagerComponent));
  if (Actions) ActionsActive = Actions.IsActive();
  Inventory = InventoryItemComponent.Cast(entity.FindComponent(InventoryItemComponent));
  if (Inventory) OriginalSlot = Inventory.GetParentSlot();
 }
 void CapturePhysics()
 {
  Physics Body = Entity.GetPhysics();
  if (Body)
  {
   PhysicsCaptured = true;
   Simulation = Body.GetSimulationState();
   if (Body.IsDynamic()) { Velocity = Body.GetVelocity(); AngularVelocity = Body.GetAngularVelocity(); }
  }
 }
 void Hide()
 {
  if (!Entity) return;
  // Physics can be created after an attachment entity streams in.
  Physics Body = Entity.GetPhysics();
  if (!PhysicsCaptured && Body) CapturePhysics();
  if ((Entity.GetFlags() & (EntityFlags.VISIBLE | EntityFlags.TRACEABLE)) != 0)
   Entity.ClearFlags(EntityFlags.VISIBLE | EntityFlags.TRACEABLE);
  if (Light && Light.IsEnabled()) Light.SetEnabled(false);
  if (Body && Body.GetSimulationState() != SimulationState.NONE) Body.ChangeSimulationState(SimulationState.NONE);
  if (Perceivable && Perceivable.IsActive()) Perceivable.Deactivate(Entity);
  if (Actions && Actions.IsActive()) Actions.Deactivate(Entity);
 }
 void Restore()
 {
  if (!Entity) return;
  Entity.ClearFlags(EntityFlags.VISIBLE | EntityFlags.TRACEABLE);
  Entity.SetFlags(Flags);
  if (Light) Light.SetEnabled(LightEnabled);
  Physics Body = Entity.GetPhysics();
  if (Body && PhysicsCaptured)
  {
   Body.ChangeSimulationState(Simulation);
   if (Body.IsDynamic()) { Body.SetVelocity(Velocity); Body.SetAngularVelocity(AngularVelocity); }
  }
  if (Perceivable && PerceivableActive) Perceivable.Activate(Entity);
  if (Actions && ActionsActive) Actions.Activate(Entity);
 }
 bool EnteredDifferentStorage()
 {
  return Inventory && Inventory.GetParentSlot() && Inventory.GetParentSlot() != OriginalSlot;
 }
 void RebaseTransferredStorage()
 {
  if (!Entity || !EnteredDifferentStorage()) return;
  // Completed native insertion owns this item's new visibility/physics policy.
  // Descendant slots which did not change keep their original local snapshots.
  Flags = Entity.GetFlags() & (EntityFlags.VISIBLE | EntityFlags.TRACEABLE);
  CapturePhysics();
  OriginalSlot = Inventory.GetParentSlot();
 }
 // Native insertion owns the transferred root's new visibility/physics values.
 // For detached parts, release fields still at our applied suppressed values.
 void RestoreReleased()
 {
  if (!Entity || Entity.IsDeleted()) return;
  if (!EnteredDifferentStorage())
  {
   if (Light && !Light.IsEnabled()) Light.SetEnabled(LightEnabled);
   if ((Entity.GetFlags() & (EntityFlags.VISIBLE | EntityFlags.TRACEABLE)) == 0) Entity.SetFlags(Flags);
   Physics Body = Entity.GetPhysics();
   if (Body && PhysicsCaptured && Body.GetSimulationState() == SimulationState.NONE)
    Body.ChangeSimulationState(Simulation);
  }
  if (Perceivable && PerceivableActive && !Perceivable.IsActive()) Perceivable.Activate(Entity);
  if (Actions && ActionsActive && !Actions.IsActive()) Actions.Activate(Entity);
 }
 int CheckHidden()
 {
  if (!Entity) return 1;
  Physics Body = Entity.GetPhysics();
  if ((Entity.GetFlags() & (EntityFlags.VISIBLE | EntityFlags.TRACEABLE)) != 0) return 1;
  if (Light && Light.IsEnabled()) return 1;
  if (Body && Body.GetSimulationState() != SimulationState.NONE) return 1;
  if (Perceivable && Perceivable.IsActive()) return 1;
  if (Actions && Actions.IsActive()) return 1;
  return 0;
 }
}

// Kept on each replicated character so a late client receives current state.
// Local snapshots contain local physics/visibility values, never guessed defaults.
modded class SCR_ChimeraCharacter
{
 // Local owners with retained snapshots only; never a world/entity scan.
 protected static ref array<SCR_ChimeraCharacter> s_EBG_SimulationOwners;
 // EntityID -> the part holding that entity's snapshot, in whichever owner's array.
 // Weak values: the owners' arrays keep the parts alive. Every part is created once
 // (EBG_CaptureSimulationTreeIndexed) and only moved between owners, never copied, so
 // this answers EBG_FindSimulationPartOwner without scanning every owner and part.
 protected static ref map<EntityID, EBG_SimulationPart> s_EBG_PartIndex;
 // Round-robin position of the stale-owner pruning done on each owner insert.
 protected static int s_EBG_OwnerPruneCursor;
 [RplProp(onRplName: "EBG_OnSimulationState"), NonSerialized()]
 protected bool m_EBG_SimulationCached;
 [NonSerialized()]
 protected bool m_EBG_SimulationInitialized;
 // True exactly while this character is in s_EBG_SimulationOwners.
 [NonSerialized()]
 protected bool m_EBG_SimulationListed;
 protected ref array<ref EBG_SimulationPart> m_EBG_SimulationParts;
 bool EBG_IsSimulationCached() { return m_EBG_SimulationCached; }
 bool EBG_HasSimulationInitialized() { return m_EBG_SimulationInitialized; }
 // The part index stays: unlisted owners are never returned, and each character's
 // destructor drops the keys of the parts that die with it.
 static void EBG_ClearSimulationOwnersForWorldCleanup() {
		EXPBG_LazyStatics_SCR_ChimeraCharacter();
  foreach (SCR_ChimeraCharacter listed : s_EBG_SimulationOwners)
   if (listed) listed.m_EBG_SimulationListed = false;
  s_EBG_SimulationOwners.Clear(); s_EBG_OwnerPruneCursor = 0; }
 // Adds an owner once and prunes one stale entry per call (round robin): the old
 // lookup scan pruned null or empty owners on every inventory event instead.
 protected static void EBG_ListSimulationOwner(SCR_ChimeraCharacter owner)
 {
  EXPBG_LazyStatics_SCR_ChimeraCharacter();
  int count = s_EBG_SimulationOwners.Count();
  if (count > 0)
  {
   if (s_EBG_OwnerPruneCursor >= count) s_EBG_OwnerPruneCursor = 0;
   SCR_ChimeraCharacter stale = s_EBG_SimulationOwners[s_EBG_OwnerPruneCursor];
   if (stale != owner && (!stale || !stale.m_EBG_SimulationParts || stale.m_EBG_SimulationParts.IsEmpty()))
   {
    if (stale) stale.m_EBG_SimulationListed = false;
    s_EBG_SimulationOwners.Remove(s_EBG_OwnerPruneCursor);
   }
   else s_EBG_OwnerPruneCursor++;
  }
  if (!owner || owner.m_EBG_SimulationListed) return;
  owner.m_EBG_SimulationListed = true;
  s_EBG_SimulationOwners.Insert(owner);
 }
 protected static void EBG_UnlistSimulationOwner(SCR_ChimeraCharacter owner)
 {
  EXPBG_LazyStatics_SCR_ChimeraCharacter();
  s_EBG_SimulationOwners.RemoveItem(owner);
  if (owner) owner.m_EBG_SimulationListed = false;
 }
 protected static void EBG_IndexSimulationPart(EBG_SimulationPart part)
 {
  EXPBG_LazyStatics_SCR_ChimeraCharacter();
  part.Key = part.Entity.GetID();
  s_EBG_PartIndex.Set(part.Key, part);
 }
 // A part leaving every snapshot. Its key may already belong to a newer part.
 protected static void EBG_UnindexSimulationPart(EBG_SimulationPart part)
 {
  EXPBG_LazyStatics_SCR_ChimeraCharacter();
  if (part && s_EBG_PartIndex.Get(part.Key) == part) s_EBG_PartIndex.Remove(part.Key);
 }
 static SCR_ChimeraCharacter EBG_FindSimulationIdentityOwner(CharacterIdentityComponent identity)
 {
		EXPBG_LazyStatics_SCR_ChimeraCharacter();
  if (!identity)
  {
   return null;
  }
  // CharacterIdentityComponent derives from GenericComponent, which exposes
  // neither GetOwner nor OnPostInit. Match the component on existing cached
  // owners instead; no new owner refs, lifecycle override or world scan.
  foreach (SCR_ChimeraCharacter owner : s_EBG_SimulationOwners)
  {
   if (owner && !owner.IsDeleted() && owner.m_EBG_SimulationInitialized && owner.m_EBG_SimulationCached && owner.FindComponent(CharacterIdentityComponent) == identity)
   {
    return owner;
   }
  }
  return null;
 }
 // Runs on every inventory event of any character, on server and clients, and for
 // every newly captured entity: one index lookup plus the owner's own part check.
 static SCR_ChimeraCharacter EBG_FindSimulationPartOwner(IEntity entity)
 {
		EXPBG_LazyStatics_SCR_ChimeraCharacter();
  if (!entity) return null;
  EntityID key = entity.GetID();
  SCR_ChimeraCharacter found;
  EBG_SimulationPart part = s_EBG_PartIndex.Get(key);
  if (part)
  {
   SCR_ChimeraCharacter holder = part.Owner;
   // A deleted or reused id, or a part no longer in its owner's snapshot: stale key.
   if (part.Entity != entity || !holder || !holder.m_EBG_SimulationParts || !holder.m_EBG_SimulationParts.Contains(part))
    s_EBG_PartIndex.Remove(key);
   else if (holder.m_EBG_SimulationListed)
    found = holder;
  }
  if (EBG_DebugChecks.Enabled)
  {
   SCR_ChimeraCharacter scanned = EBG_ScanSimulationPartOwner(entity);
   if (scanned != found) EBG_DebugChecks.Mismatch(string.Format("simulation part owner entity=%1 index=%2 scan=%3", entity, found, scanned));
  }
  return found;
 }
 // The pre-0.1.15 lookup over every listed owner and part, without its pruning side
 // effect. Debug cross-check only (EBG_DebugChecks.Enabled).
 protected static SCR_ChimeraCharacter EBG_ScanSimulationPartOwner(IEntity entity)
 {
  for (int i = s_EBG_SimulationOwners.Count() - 1; i >= 0; i--)
  {
   SCR_ChimeraCharacter owner = s_EBG_SimulationOwners[i];
   if (!owner || !owner.m_EBG_SimulationParts) continue;
   foreach (EBG_SimulationPart part : owner.m_EBG_SimulationParts)
   {
    if (part.Entity == entity)
    {
     return owner;
    }
   }
  }
  return null;
 }
 // Native inventory ownership can change before the visible hierarchy catches up.
 protected SCR_ChimeraCharacter EBG_SimulationHolder(IEntity entity)
 {
  for (int depth = 0; entity && depth < 32; depth++)
  {
   SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(entity);
   if (character) return character;
   InventoryItemComponent item = InventoryItemComponent.Cast(entity.FindComponent(InventoryItemComponent));
   if (item && item.GetParentSlot())
   {
    BaseInventoryStorageComponent storage = item.GetParentSlot().GetStorage();
    if (!storage || storage.GetOwner() == entity) return null;
    entity = storage.GetOwner();
   }
   else entity = entity.GetParent();
  }
  return null;
 }
 protected bool EBG_IsSimulationTreePart(IEntity entity)
 {
  for (int depth = 0; entity && depth < 32; depth++)
  {
   if (entity == this) return true;
   entity = entity.GetParent();
  }
  return false;
 }
 protected EBG_SimulationPart EBG_TakeSimulationPart(IEntity entity)
 {
  if (!m_EBG_SimulationParts) return null;
  for (int i = m_EBG_SimulationParts.Count() - 1; i >= 0; i--)
  {
   if (m_EBG_SimulationParts[i].Entity != entity) continue;
   ref EBG_SimulationPart part = m_EBG_SimulationParts[i];
   m_EBG_SimulationParts.Remove(i);
   return part;
  }
  return null;
 }
 void EBG_CaptureSimulationTree(IEntity entity)
 {
  // Never capture constructor/default physics or a different inventory owner's kit.
  if (!m_EBG_SimulationInitialized || !entity || entity.IsDeleted() || EBG_SimulationHolder(entity) != this) return;
  if (!m_EBG_SimulationParts) m_EBG_SimulationParts = {};
  // One local membership index avoids searching the full snapshot array for
  // every attachment. Ownership and descendants are still checked every pass.
  map<IEntity, bool> captured = new map<IEntity, bool>();
  foreach (EBG_SimulationPart known : m_EBG_SimulationParts)
   if (known.Entity) captured.Insert(known.Entity, true);
  EBG_CaptureSimulationTreeIndexed(entity, captured, true);
 }
 protected void EBG_CaptureSimulationTreeIndexed(IEntity entity, map<IEntity, bool> captured, bool first = false)
 {
		EXPBG_LazyStatics_SCR_ChimeraCharacter();
  if (!m_EBG_SimulationInitialized || !entity || entity.IsDeleted() || EBG_SimulationHolder(entity) != this) return;
  if (!captured.Contains(entity))
  {
   ref EBG_SimulationPart part;
   SCR_ChimeraCharacter previousOwner = EBG_FindSimulationPartOwner(entity);
   if (previousOwner && previousOwner != this) part = previousOwner.EBG_TakeSimulationPart(entity);
   if (part) part.RebaseTransferredStorage();
   if (!part) { part = new EBG_SimulationPart(); part.Capture(entity); EBG_IndexSimulationPart(part); }
   part.Owner = this;
   m_EBG_SimulationParts.Insert(part);
   captured.Insert(entity, true);
  }
  if (first) EBG_ListSimulationOwner(this);
  IEntity child = entity.GetChildren();
  while (child)
  {
   EBG_CaptureSimulationTreeIndexed(child, captured);
   child = child.GetSibling();
  }
 }
 void EBG_SetSimulationCached(bool cached)
 {
  if (!Replication.IsServer()) return;
  m_EBG_SimulationCached = cached;
  EBG_OnSimulationState();
  Replication.BumpMe();
 }
 protected void EBG_OnSimulationState()
 {
		EXPBG_LazyStatics_SCR_ChimeraCharacter();
  if (!m_EBG_SimulationInitialized || !GetGame()) return;
  GetGame().GetCallqueue().Remove(EBG_FinishSimulationChange);
  if (m_EBG_SimulationCached)
  {
   EBG_RefreshSimulationLocal();
   EBG_QueueSimulationChange();
  }
  else if (m_EBG_SimulationParts)
  {
   EBG_ReconcileSimulationParts();
   foreach (EBG_SimulationPart part : m_EBG_SimulationParts)
   {
    part.Restore();
   }
   foreach (EBG_SimulationPart released : m_EBG_SimulationParts) EBG_UnindexSimulationPart(released);
   m_EBG_SimulationParts = null;
   EBG_UnlistSimulationOwner(this);
  }
 }
 protected void EBG_ReconcileSimulationParts()
 {
		EXPBG_LazyStatics_SCR_ChimeraCharacter();
  if (!m_EBG_SimulationParts) return;
  for (int i = m_EBG_SimulationParts.Count() - 1; i >= 0; i--)
  {
   ref EBG_SimulationPart part = m_EBG_SimulationParts[i];
   if (!part.Entity || part.Entity.IsDeleted()) { m_EBG_SimulationParts.Remove(i); EBG_UnindexSimulationPart(part); continue; }
   SCR_ChimeraCharacter holder = EBG_SimulationHolder(part.Entity);
   if (holder == this && EBG_IsSimulationTreePart(part.Entity))
   {
    part.RebaseTransferredStorage();
    continue;
   }
   m_EBG_SimulationParts.Remove(i);
   if (holder && holder.m_EBG_SimulationInitialized && holder.EBG_IsSimulationCached() && holder.EBG_IsSimulationTreePart(part.Entity))
   {
    // Transfer the original baseline, never recapture somebody else's hidden state.
    if (!holder.m_EBG_SimulationParts) holder.m_EBG_SimulationParts = {};
    part.Owner = holder;
    holder.m_EBG_SimulationParts.Insert(part);
    EBG_ListSimulationOwner(holder);
    part.RebaseTransferredStorage();
    part.Hide();
   }
   else
   {
    EBG_UnindexSimulationPart(part);
    part.RestoreReleased();
   }
  }
 }
 void EBG_RefreshSimulationInventory()
 {
  if (!m_EBG_SimulationInitialized) return;
  if (m_EBG_SimulationCached)
  {
   EBG_RefreshSimulationLocal();
   EBG_QueueSimulationChange();
  }
  else EBG_ReconcileSimulationParts();
 }
 protected void EBG_QueueSimulationChange()
 {
  // Native clothing insertion can finish after the inventory callback. One
  // trailing refresh coalesces that change; it never schedules itself again.
  GetGame().GetCallqueue().Remove(EBG_FinishSimulationChange);
  GetGame().GetCallqueue().CallLater(EBG_FinishSimulationChange, 100, false);
 }
 protected void EBG_FinishSimulationChange() { EBG_RefreshSimulationLocal(); }
 protected void EBG_RefreshSimulationLocal()
 {
  if (!m_EBG_SimulationInitialized || !m_EBG_SimulationCached) return;
  // Native identity commits delete the old head before creating its replacement.
  // Remove snapshots of deleted entities; capture and check the current tree below.
  EBG_ReconcileSimulationParts();
  EBG_CaptureSimulationTree(this);
  foreach (EBG_SimulationPart part : m_EBG_SimulationParts) part.Hide();
 }
 int EBG_CheckSimulationLocal()
 {
  if (!m_EBG_SimulationParts) return 1;
  int failed;
  foreach (EBG_SimulationPart part : m_EBG_SimulationParts) failed += part.CheckHidden();
  return failed;
 }
 override void EOnInit(IEntity owner)
 {
  super.EOnInit(owner);
  m_EBG_SimulationInitialized = true;
  EBG_OnSimulationState();
 }
 void ~SCR_ChimeraCharacter()
 {
		EXPBG_LazyStatics_SCR_ChimeraCharacter();
  EBG_UnlistSimulationOwner(this);
  // This character's parts die with it; drop their index keys too.
  if (m_EBG_SimulationParts)
  {
   foreach (EBG_SimulationPart part : m_EBG_SimulationParts) EBG_UnindexSimulationPart(part);
  }
  if (!GetGame()) return;
  GetGame().GetCallqueue().Remove(EBG_FinishSimulationChange);
  GetGame().GetCallqueue().Remove(EBG_OnSimulationState);
 }

	//------------------------------------------------------------------------------------------------
	//! Creates the collections on first use (not in the global static initializer, which has a
	//! per-function instruction limit that large modsets exceed on Windows).
	protected static void EXPBG_LazyStatics_SCR_ChimeraCharacter()
	{
		if (!s_EBG_SimulationOwners)
			s_EBG_SimulationOwners = new array<SCR_ChimeraCharacter>();
		if (!s_EBG_PartIndex)
			s_EBG_PartIndex = new map<EntityID, EBG_SimulationPart>();
	}
}

modded class SCR_InventoryStorageManagerComponent
{
 protected void EBG_RefreshSimulationAfterInventory(IEntity item)
 {
  SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(GetOwner());
  SCR_ChimeraCharacter previousOwner = SCR_ChimeraCharacter.EBG_FindSimulationPartOwner(item);
  if (previousOwner && previousOwner != character) previousOwner.EBG_RefreshSimulationInventory();
  if (character) character.EBG_RefreshSimulationInventory();
 }
 override protected void OnItemAdded(BaseInventoryStorageComponent storageOwner, IEntity item)
 {
  super.OnItemAdded(storageOwner, item);
  // The full stock override also executes weapon insertion after its base invoker.
  EBG_RefreshSimulationAfterInventory(item);
 }
 override protected void OnItemRemoved(BaseInventoryStorageComponent storageOwner, IEntity item)
 {
  super.OnItemRemoved(storageOwner, item);
  EBG_RefreshSimulationAfterInventory(item);
 }
}

// Native identity changes can create a head without inventory insertion. Apply
// the existing owned-tree policy after the stock body-part callback completes.
// Unchanged dormant trees have no recurring refresh timer.
modded class SCR_CharacterIdentityComponent
{
 override void OnBodyPartStateChanged(string bodyPart, bool visible, bool wounded)
 {
  super.OnBodyPartStateChanged(bodyPart, visible, wounded);
  SCR_ChimeraCharacter character = SCR_ChimeraCharacter.EBG_FindSimulationIdentityOwner(this);
  if (character && character.EBG_HasSimulationInitialized() && character.EBG_IsSimulationCached()) character.EBG_RefreshSimulationInventory();
 }
}

class EBG_SimulationAgent
{
 ref EBG_SimulationDevices Devices;
 ref EBG_StaticEmplacement Emplacement;
 bool Restored;
 AIAgent Agent;
 SCR_ChimeraCharacter Character;
 int PermanentLOD;
 int LOD;
 bool Active;
 bool WeaponRaised;
 vector Position;
 void Capture(AIAgent agent, SCR_ChimeraCharacter character = null)
 {
  Agent = agent; Character = character;
  PermanentLOD = agent.GetPermanentLOD();
  LOD = agent.GetLOD();
  Active = agent.IsAIActivated();
  if (character)
  {
   Position = character.GetOrigin();
   WeaponRaised = character.GetCharacterController().IsWeaponRaised();
  }
 }
 void Suspend()
 {
  Agent.SetPermanentLOD(AIAgent.GetMaxLOD());
  // Max LOD disables native AI itself. Do not add an explicit deactivation latch.
  Agent.SetLOD(AIAgent.GetMaxLOD());
 }
 void Restore()
 {
  if (!Agent) return;
#ifdef EBG_ACCEPTANCE_TEST
  string ebgWorldFile = GetGame().GetWorldFile();
  bool ebgTraceLOD = ebgWorldFile == "$EXPBG_GM_Optimizer:Worlds/EXPBG_OptimizerTest.ent" || ebgWorldFile == "Worlds/EXPBG_OptimizerTest.ent" || ebgWorldFile == "{E4BFDE2E52CCCF77}Worlds/EXPBG_OptimizerTest.ent";
  if (ebgTraceLOD) PrintFormat("[EBG LOD RESTORE] before-unpin agent=%1 LOD=%2 permanent=%3 active=%4 savedLOD=%5 savedPermanent=%6", Agent, Agent.GetLOD(), Agent.GetPermanentLOD(), Agent.IsAIActivated(), LOD, PermanentLOD);
#endif
  // Release our permanent pin before issuing the single native wake request.
  Agent.SetPermanentLOD(PermanentLOD);
#ifdef EBG_ACCEPTANCE_TEST
  if (ebgTraceLOD) PrintFormat("[EBG LOD RESTORE] after-unpin agent=%1 LOD=%2 permanent=%3 active=%4", Agent, Agent.GetLOD(), Agent.GetPermanentLOD(), Agent.IsAIActivated());
#endif
  // Automatic-policy trial: request LOD0 once to reset the suppressed native
  // state. There is no recurring request or permanent clamp; uncached near/far
  // acceptance must verify that observer-driven throttling resumes afterward.
  if (PermanentLOD == -1) Agent.SetLOD(0);
  else Agent.SetLOD(LOD);
#ifdef EBG_ACCEPTANCE_TEST
  if (ebgTraceLOD) PrintFormat("[EBG LOD RESTORE] after-policy-restore agent=%1 LOD=%2 permanent=%3 active=%4", Agent, Agent.GetLOD(), Agent.GetPermanentLOD(), Agent.IsAIActivated());
#endif
  if (Active) Agent.ActivateAI();
  // Captured inactive can mean ordinary distant LOD. Leave its native activation
  // ownership untouched instead of replaying that transient value as a command.
  if (Character && Character.GetCharacterController()) Character.GetCharacterController().SetWeaponRaised(WeaponRaised);
 }

 void RestorePossessed()
 {
  if (!Agent) return;
#ifdef EBG_ACCEPTANCE_TEST
  string ebgWorldFile = GetGame().GetWorldFile();
  bool ebgTraceLOD = ebgWorldFile == "$EXPBG_GM_Optimizer:Worlds/EXPBG_OptimizerTest.ent" || ebgWorldFile == "Worlds/EXPBG_OptimizerTest.ent" || ebgWorldFile == "{E4BFDE2E52CCCF77}Worlds/EXPBG_OptimizerTest.ent";
  if (ebgTraceLOD) PrintFormat("[EBG LOD POSSESSED] before-unpin agent=%1 LOD=%2 permanent=%3 active=%4 savedLOD=%5 savedPermanent=%6", Agent, Agent.GetLOD(), Agent.GetPermanentLOD(), Agent.IsAIActivated(), LOD, PermanentLOD);
#endif
  // Release only our LOD ownership. Native player possession owns activation/input.
  Agent.SetPermanentLOD(PermanentLOD);
#ifdef EBG_ACCEPTANCE_TEST
  if (ebgTraceLOD) PrintFormat("[EBG LOD POSSESSED] after-unpin agent=%1 LOD=%2 permanent=%3 active=%4", Agent, Agent.GetLOD(), Agent.GetPermanentLOD(), Agent.IsAIActivated());
#endif
  if (PermanentLOD >= 0) Agent.SetLOD(LOD);
#ifdef EBG_ACCEPTANCE_TEST
  if (ebgTraceLOD) PrintFormat("[EBG LOD POSSESSED] after-policy-restore agent=%1 LOD=%2 permanent=%3 active=%4", Agent, Agent.GetLOD(), Agent.GetPermanentLOD(), Agent.IsAIActivated());
#endif
 }
}
class EBG_SimulationState
{
 SCR_AIGroup Group;
 ref EBG_SimulationAgent GroupState;
 ref array<ref EBG_SimulationAgent> Members = {};
 bool Suspended;
 bool PreparationFailed;
}
class EBG_SimulationCache
{
 static string UnsupportedRHSDevices(SCR_ChimeraCharacter character)
 {
  typename rhsDevice = "RHS_LightDevice".ToType();
  if (!rhsDevice) return "";
  array<IEntity> tree = {};
  SCR_EntityHelper.GetHierarchyEntityList(character, tree);
  foreach (IEntity entity : tree)
  {
   if (!entity) return "RHS device hierarchy could not be inspected";
   array<Managed> components = {};
   entity.FindComponents(GenericComponent, components);
   foreach (Managed component : components)
   {
    if (!component) continue;
    // Include installed and future subclasses without requiring RHS at compile time.
    if (!component.Type().IsInherited(rhsDevice)) continue;
    // Named only for RHS devices: every other component skips the string.
    string type = component.Type().ToString();
    bool powered, suspended;
    if (!GetGame().GetScriptModule().Call(component, "IsTurnedOn", false, powered)) return "RHS device IsTurnedOn unreadable: " + type;
    if (powered) return "Powered RHS device: " + type;
    if (!GetGame().GetScriptModule().Call(component, "IsSuspended", false, suspended)) return "RHS device IsSuspended unreadable: " + type;
    if (!suspended) continue;
    // Native RHS also suspends ordinary off devices. Only a saved on-state
    // resumes power; the stale latch of an unsuspended off device is irrelevant.
    EBG_OptionalScalar previous = new EBG_OptionalScalar();
    previous.Field = "m_bPreSuspensionStatus"; previous.Kind = 2;
    if (!previous.Read(component)) return "RHS device pending power unreadable: " + type;
    if (previous.BoolValue) return "RHS device has pending power resumption: " + type;
   }
  }
  return "";
 }
 static bool HasActiveOperation(CharacterControllerComponent controller)
 {
  if (!controller) return true;
  CharacterInputContext input = controller.GetInputContext();
  if (!input) return true;
  return controller.IsReloading() || controller.IsUsingItem() || controller.IsChangingItem() || controller.IsChangingFireMode() || controller.IsMeleeAttack() || input.WeaponIsPullingTrigger() || input.WeaponIsStartReloading() || input.GetThrow() || input.GetMeleeAttack();
 }
 static string Unsupported(SCR_ChimeraCharacter character, bool preserveDevices = true)
 {
  if (!character) return "Non-character member";
  if (!character.EBG_HasSimulationInitialized()) return "Character initialization incomplete";
  if (character.EBG_IsSimulationCached()) return "Member already suspended";
  CharacterControllerComponent controller = character.GetCharacterController();
  if (!controller || character.EBG_WasPlayerControlled()) return "Current/previous player or missing character controller";
  if (controller.IsDead() || controller.IsUnconscious() || controller.IsFalling() || controller.IsSwimming() || controller.IsClimbing()) return "Unsupported life or movement state";
  string compartment = EBG_StaticEmplacement.Unsupported(character);
  if (!compartment.IsEmpty()) return compartment;
  if (HasActiveOperation(controller)) return "Active trigger, reload, throw, melee or item operation";
  SCR_CharacterDamageManagerComponent damage = SCR_CharacterDamageManagerComponent.Cast(character.GetDamageManager());
  if (!damage || damage.IsBleeding()) return "Active bleeding or unavailable damage state";
  // Stable wounds stay on Simulation originals. Full restores prefab defaults.
  // This eligibility path runs on authority; inspect all of the character's own
  // native hit zones, with a finite bound before allocating their array.
  int hitZoneCount = damage.CountAllHitZones();
  if (hitZoneCount < 1 || hitZoneCount > 64) return "Unverified character hit-zone set";
  array<HitZone> hitZones = {};
  if (damage.GetAllHitZones(hitZones) != hitZoneCount) return "Incomplete character hit-zone set";
  foreach (HitZone hitZone : hitZones)
   if (!hitZone || !EBG_MissionPersistence.Finite(hitZone.GetHealthScaled())) return "Invalid character hit-zone health";
  array<ref SCR_PersistentDamageEffect> effects = {};
  damage.GetPersistentEffects(effects);
  foreach (SCR_PersistentDamageEffect effect : effects)
   if (effect && effect.IsActive() && effect.GetDamageType() != EDamageType.REGENERATION && effect.GetDamageType() != EDamageType.HEALING) return "Active medical or contact effect";
  ChimeraAIControlComponent control = ChimeraAIControlComponent.Cast(character.GetAIControlComponent());
  if (!control || !control.GetControlAIAgent()) return "Native Chimera AI control required";
  AIAgent agent = control.GetControlAIAgent();
  string modOperation = EBG_OptionalModState.ActiveAgentOperation(agent);
  if (!modOperation.IsEmpty()) return modOperation;
  if (agent.GetPermanentLOD() >= 0 || EBG_CacheManager.HasBlockingDanger(agent)) return "External permanent LOD or danger event";
  if (!agent.IsAIActivated() && agent.GetLOD() != AIAgent.GetMaxLOD()) return "AI inactive outside native maximum LOD";
  if (!character.GetPhysics() || !character.FindComponent(PerceivableComponent)) return "Missing native physics or perceivable";
  if (!character.GetPrefabData()) return "Non-prefab character";
   // Content mods inherit these same native character/AI components. The actual
   // life, operation, physics and control checks above determine eligibility;
   // a prefab folder name does not describe its runtime behavior.
  if (preserveDevices) return UnsupportedRHSDevices(character);
  return "";
 }
 static EBG_SimulationState Suspend(SCR_AIGroup group, out string reason)
 {
  reason = "Server play mode required";
  if (!Replication.IsServer() || !GetGame().InPlayMode()) return null;
  reason = "Incomplete, player-owned or externally managed group";
  if (!group || group.EBG_Exclude || !group.EBG_HasCompletedInitialSpawn() || group.GetPlayerCount() > 0 || group.IsSlave() || group.GetMaster() || group.IsCreatedByCommander()) return null;
  if (group.GetLifecyclePolicy() == SCR_EAIGroupLifecyclePolicy.ProximityDriven || group.GetPermanentLOD() >= 0) return null;
  array<AIAgent> agents = {};
  group.GetAgents(agents);
  if (agents.IsEmpty()) { reason = "No live members"; return null; }
  EBG_SimulationState state = new EBG_SimulationState();
  state.Group = group;
  state.GroupState = new EBG_SimulationAgent();
  state.GroupState.Capture(group);
  foreach (AIAgent agent : agents)
  {
   SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(agent.GetControlledEntity());
   reason = Unsupported(character);
   if (reason != "") return null;
   EBG_CacheMember managed;
   if (EBG_CacheManager.Instance) managed = EBG_CacheManager.Instance.FindMember(character);
   if (managed && managed.WasPlayer) { reason = "Previously possessed member"; return null; }
   EBG_SimulationAgent member = new EBG_SimulationAgent();
   member.Capture(agent, character);
   member.Devices = new EBG_SimulationDevices();
   member.Emplacement = new EBG_StaticEmplacement();
   if (!member.Emplacement.Capture(character)) { reason = "Compartment changed during Simulation capture"; return null; }
   if (!member.Devices.Capture(character)) { reason = "Mod device channel state could not be captured"; return null; }
   state.Members.Insert(member);
  }
  foreach (EBG_SimulationAgent member : state.Members)
  {
   if (member.Devices.Suspend()) continue;
   bool rollbackComplete = true;
   foreach (EBG_SimulationAgent rollback : state.Members)
    if (!rollback.Devices.Restore()) rollbackComplete = false;
   reason = "Mod device channel suspension did not verify";
   if (!rollbackComplete)
   {
    state.PreparationFailed = true;
    reason = "Device suspension rollback incomplete; originals remain active; retain recovery record";
    return state;
   }
   return null;
  }
  // Snapshot every member and attachment BEFORE high LOD can alter character state.
  foreach (EBG_SimulationAgent member : state.Members) member.Character.EBG_CaptureSimulationTree(member.Character);
  state.GroupState.Suspend();
  foreach (EBG_SimulationAgent member : state.Members)
  {
   member.Suspend();
   member.Character.EBG_SetSimulationCached(true);
  }
  state.Suspended = true;
  reason = "Suspended candidate; native activity, targetability and multiplayer observation required";
  return state;
 }
 static bool Restore(EBG_SimulationState state, out string reason)
 {
  reason = "Server and retained snapshot required";
  if (!Replication.IsServer() || !state) return false;
  bool complete = true;
  if (!state.PreparationFailed && !state.GroupState.Restored)
  {
   state.GroupState.Restore();
   state.GroupState.Restored = true;
  }
  foreach (EBG_SimulationAgent member : state.Members)
  {
   if (!member.Character) { complete = false; continue; }
   CharacterControllerComponent controller = member.Character.GetCharacterController();
   // Never reactivate AI on a newly possessed character or resurrect a casualty.
   bool changedOwner = member.Character.EBG_WasPlayerControlled() || !controller || controller.IsDead();
   if (!state.PreparationFailed && !member.Restored)
   {
    if (member.Character.EBG_WasPlayerControlled()) member.RestorePossessed();
    else if (controller && !controller.IsDead()) member.Restore();
    member.Character.EBG_SetSimulationCached(false);
    member.Restored = true;
   }
   // Possession and death transfer presentation ownership away from this cache.
   if (changedOwner && member.Devices) member.Devices.Discard();
   if (member.Devices && !member.Devices.Restore()) complete = false;
  }
  state.Suspended = false;
  reason = "Original entities and saved local state restored; no spawn, healing or rearming";
  if (state.PreparationFailed) reason = "Device suspension rolled back; originals remained active";
  if (!complete) reason = "Missing member or device restoration incomplete; retain recovery record";
  return complete;
 }
}
