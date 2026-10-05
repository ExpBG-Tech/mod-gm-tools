// Only the character is cached. The native static turret and its inventory stay in-world.
class EBG_StaticEmplacement
{
 bool HadMount;
 IEntity Owner;
 int SlotId = -1;

 static bool InTransition(CompartmentAccessComponent access)
 {
  return access && (access.IsGettingIn() || access.IsGettingOut() || access.IsSwitchingSeatsAnim());
 }
 static bool IsStaticSlot(BaseCompartmentSlot slot)
 {
  if (!slot || !TurretCompartmentSlot.Cast(slot) || slot.GetType() != ECompartmentType.TURRET) return false;
  IEntity owner = slot.GetOwner();
  if (!owner || owner.IsDeleted() || !Turret.Cast(owner) || slot.GetVehicle() != owner) return false;
  TurretComponent turret = TurretComponent.Cast(owner.FindComponent(TurretComponent));
  Physics body = owner.GetPhysics();
  if (!turret || turret.IsVehicleMounted() || !body || body.IsDynamic()) return false;
  DamageManagerComponent damage = DamageManagerComponent.Cast(owner.FindComponent(DamageManagerComponent));
  if (!damage || damage.IsDestroyed()) return false;
  // Vehicle MGs are also Turret entities. Reject their full ancestry, including
  // a later attachment to a vehicle; a stationary car is still a vehicle.
  IEntity ancestor = owner;
  for (int depth = 0; ancestor && depth < 32; depth++)
  {
   if (BaseVehicle.Cast(ancestor) || ancestor.FindComponent(VehicleControllerComponent)) return false;
   ancestor = ancestor.GetParent();
  }
  if (ancestor) return false;
  BaseCompartmentManagerComponent manager = BaseCompartmentManagerComponent.Cast(owner.FindComponent(BaseCompartmentManagerComponent));
  return manager && manager.FindCompartment(slot.GetCompartmentSlotID()) == slot;
 }
 static string Unsupported(SCR_ChimeraCharacter character)
 {
  if (!character) return "Missing compartment owner";
  CompartmentAccessComponent access = character.GetCompartmentAccessComponent();
  if (EBG_StaticEmplacement.InTransition(access)) return "Compartment entry, exit or seat change in progress";
  BaseCompartmentSlot slot;
  if (access) slot = access.GetCompartment();
  if (!slot)
  {
   if (character.IsInVehicle() || (access && access.IsInCompartment())) return "Unresolved vehicle compartment";
   return "";
  }
  if (!IsStaticSlot(slot) || slot.GetOccupant() != character || !access.IsInCompartment()) return "Only static emplacement turret occupants supported";
  TurretControllerComponent controller = TurretControllerComponent.Cast(slot.GetOwner().FindComponent(TurretControllerComponent));
  if (!controller || controller.GetReloadingState() != ETurretReloadState.NONE) return "Static emplacement turret controller missing or reloading";
  return "";
 }
 bool Capture(SCR_ChimeraCharacter character)
 {
  if (!Unsupported(character).IsEmpty()) return false;
  CompartmentAccessComponent access = character.GetCompartmentAccessComponent();
  BaseCompartmentSlot slot;
  if (access) slot = access.GetCompartment();
  HadMount = slot != null; Owner = null; SlotId = -1;
  if (slot) { Owner = slot.GetOwner(); SlotId = slot.GetCompartmentSlotID(); }
  return true;
 }
 BaseCompartmentSlot Resolve()
 {
  if (!HadMount || !Owner || Owner.IsDeleted()) return null;
  BaseCompartmentManagerComponent manager = BaseCompartmentManagerComponent.Cast(Owner.FindComponent(BaseCompartmentManagerComponent));
  if (!manager) return null;
  BaseCompartmentSlot slot = manager.FindCompartment(SlotId);
  if (!IsStaticSlot(slot)) return null;
  return slot;
 }
 bool Matches(SCR_ChimeraCharacter character)
 {
  if (!character) return false;
  CompartmentAccessComponent access = character.GetCompartmentAccessComponent();
  if (EBG_StaticEmplacement.InTransition(access)) return false;
  if (!HadMount) return !character.IsInVehicle() && (!access || (!access.IsInCompartment() && !access.GetCompartment()));
  BaseCompartmentSlot slot = Resolve();
  return slot && access && access.IsInCompartment() && access.GetCompartment() == slot && slot.GetOccupant() == character;
 }
 bool RestoreOnFootIfUnavailable(SCR_ChimeraCharacter character, SCR_AIGroup group, bool mountTimedOut = false)
 {
  if (!HadMount || !Replication.IsServer() || !group || !character || character.GetCharacterGroup() != group || character.EBG_WasPlayerControlled()) return false;
  CharacterControllerComponent controller = character.GetCharacterController();
  CompartmentAccessComponent access = character.GetCompartmentAccessComponent();
  if (!character.EBG_HasSimulationInitialized() || !controller || !access || controller.IsDead() || controller.IsUnconscious()) return false;
  ChimeraAIControlComponent control = ChimeraAIControlComponent.Cast(character.GetAIControlComponent());
  if (!control || !control.GetControlAIAgent() || control.GetControlAIAgent().GetControlledEntity() != character) return false;
  if (EBG_StaticEmplacement.InTransition(access) || access.GetCompartment() || access.IsInCompartment() || character.IsInVehicle()) return false;
  BaseCompartmentSlot slot = Resolve();
  if (!mountTimedOut && slot && !slot.GetOccupant() && (!slot.IsReserved() || slot.IsReservedBy(character)) && slot.IsCompartmentAccessible() && !slot.IsGetInLockedFor(character)) return false;
  // Entry can be accepted without completing even for an apparently valid seat.
  // After the bounded wait, relinquish only its expectation on confirmed foot AI.
  // Keep its native position and ownership; never recreate a gun or evict an occupant.
  HadMount = false; Owner = null; SlotId = -1;
  return true;
 }
 string RestoreState(SCR_ChimeraCharacter character)
 {
  if (!character) return "survivor missing";
  CompartmentAccessComponent access = character.GetCompartmentAccessComponent();
  CharacterControllerComponent controller = character.GetCharacterController();
  bool compartment = access && (access.GetCompartment() || access.IsInCompartment());
  bool alive = controller && !controller.IsDead();
  bool unconscious = controller && controller.IsUnconscious();
  return string.Format("capturedMount=%1 slot=%2 transition=%3 compartment=%4 inVehicle=%5 alive=%6 unconscious=%7 player=%8", HadMount, SlotId, EBG_StaticEmplacement.InTransition(access), compartment, character.IsInVehicle(), alive, unconscious, character.EBG_WasPlayerControlled());
 }
 // Request acceptance is not proof of mounting. The caller must check Matches
 // on a later tick, bound retries, and use the guarded foot fallback if needed.
 bool RequestMount(SCR_ChimeraCharacter character, out string reason)
 {
  reason = "";
  if (!Replication.IsServer() || !character || character.EBG_WasPlayerControlled()) { reason = "Static emplacement restore requires authority-owned AI"; return false; }
  if (Matches(character)) return true;
  if (!HadMount) { reason = "Unseated survivor entered an external compartment"; return false; }
  CharacterControllerComponent controller = character.GetCharacterController();
  CompartmentAccessComponent access = character.GetCompartmentAccessComponent();
  if (!character.EBG_HasSimulationInitialized() || !controller || !access) { reason = "Survivor compartment initialization pending"; return false; }
  if (controller.IsDead() || controller.IsUnconscious()) { reason = "Survivor life state prevents remount"; return false; }
  if (EBG_StaticEmplacement.InTransition(access)) { reason = "Native compartment transition pending"; return false; }
  if (access.GetCompartment() || access.IsInCompartment() || character.IsInVehicle()) { reason = "Survivor entered a different compartment"; return false; }
  BaseCompartmentSlot slot = Resolve();
  if (!slot) { reason = "Original static emplacement or slot unavailable"; return false; }
  if (slot.GetOccupant() || (slot.IsReserved() && !slot.IsReservedBy(character)) || !slot.IsCompartmentAccessible() || slot.IsGetInLockedFor(character))
  { reason = "Original static emplacement slot occupied, reserved or inaccessible"; return false; }
  // This is the authority path used by native SpawnCharacterInCompartment.
  // No RPC retry queue, seat theft, turret spawn, or vehicle serialization.
  ChimeraWorld world = GetGame().GetWorld();
  if (!world || !access.GetInVehicle(Owner, slot, true, -1, ECloseDoorAfterActions.INVALID, world.IsGameTimePaused()))
  { reason = "Native static emplacement entry request refused"; return false; }
  reason = "Native static emplacement entry requested; verification pending";
  return true;
 }
}
