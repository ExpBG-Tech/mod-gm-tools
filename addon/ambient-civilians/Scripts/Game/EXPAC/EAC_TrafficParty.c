// PARKED is appended, not inserted: several gates compare phases by order (pending
// is `<= BOARD`, simulation refresh is BOARD..EXIT), and a parked car with no crew
// belongs to none of those ranges.
enum EAC_TrafficPhase { NEW, SPAWN_CAR, SPAWN_CREW, BOARD, DRIVE, PARK, EXIT, WALK, REST, CLEARING, FAILED, PARKED }

class EAC_TrafficOccupant
{
 ResourceName Prefab;
 SCR_ChimeraCharacter Actor;
 ref EBG_CacheMember Member = new EBG_CacheMember();
 bool PlayerTouched, Dead, Joined, ExitQueueInterrupted;
 // Bound once with the actor. The per-frame horn read used to look both of these
 // up again on every frame, for every party, through FindComponent.
 AIControlComponent Control;
 protected SCR_CharacterControllerComponent m_Controller;

 // SCR_CharacterControllerComponent is a CharacterControllerComponent; the horn
 // path only needs the base surface.
 CharacterControllerComponent Controller() { return m_Controller; }

 void Bind()
 {
  Member.Entity = Actor;
  Control = AIControlComponent.Cast(Actor.FindComponent(AIControlComponent));
  m_Controller = SCR_CharacterControllerComponent.Cast(Actor.FindComponent(SCR_CharacterControllerComponent));
  if (!m_Controller) { PlayerTouched = true; return; }
  m_Controller.m_OnControlledByPlayer.Insert(OnControl);
  m_Controller.m_OnLifeStateChanged.Insert(OnLife);
  Observe();
 }
 void Observe()
 {
  if (!Actor) return;
  if (!m_Controller || m_Controller.IsPlayerControlled() || Actor.EBG_WasPlayerControlled() || SCR_PossessingManagerComponent.GetPlayerIdFromControlledEntity(Actor) != 0) PlayerTouched = true;
  if (m_Controller && m_Controller.GetLifeState() == ECharacterLifeState.DEAD) Dead = true;
  Member.WasPlayer = PlayerTouched; Member.Dead = Dead;
 }
 void OnControl(IEntity actor, bool controlled) { if (controlled && actor == Actor) { PlayerTouched = true; Member.WasPlayer = true; } }
 void OnLife(ECharacterLifeState previous, ECharacterLifeState current, bool isJIP) { if (current == ECharacterLifeState.DEAD) { Dead = true; Member.Dead = true; } }
 void Detach()
 {
  if (m_Controller)
  {
   m_Controller.m_OnControlledByPlayer.Remove(OnControl);
   m_Controller.m_OnLifeStateChanged.Remove(OnLife);
  }
  m_Controller = null; Control = null;
 }
 void ~EAC_TrafficOccupant() { Detach(); }
}

// An owned transaction exists only through physical spawn, activity and cleanup.
class EAC_TrafficParty
{
 int Id, Phase, Spawned;
 // BlockedTrips counts journeys this record began and never moved away from; it
 // resets the moment the car makes real progress. StallRetry/StallRecovered hold
 // the single re-order a stalled journey is allowed before the record retires.
 int BlockedTrips;
 bool Reserved, PlayerTouched, WantDespawn, Retire, Deleting;
 bool ExclusionRemoval, SessionRemoval;
 bool Moved, StallRetry, StallRecovered;
 // Set when the first drive order is placed. From then on the journey may leave
 // the module area: only manual transit zones retire the party, and distance plus
 // visibility remove it, instead of the module disc treating "out of town" as an
 // exclusion that deletes the car in front of a player at the boundary.
 bool Departed;
 // Set while the car this director spawned is bound to the record, cleared when
 // the director deletes it itself. A bound car that is gone without that was
 // removed by someone else (vanilla vehicle garbage collection, a Game Master).
 bool HadCar;
 ResourceName CarPrefab;
 IEntity Car;
 SCR_AIGroup Group;
 AIWaypoint Order;
 AICarMovementComponent Movement;
 CarControllerComponent CarControl;
 ref array<ref EAC_TrafficOccupant> Crew = {};
 vector Position, Direction, Destination, ReturnDestination, ProgressPosition;
 float Since, ProgressAt, ClearSince, LastClearSample, AlarmUntil, NextTrip;
 // One-way model (TrafficRoundTrips 0). HomePosition is the house the driver
 // walks from; DepartAt ends the parked wait; CrewProbe steps the driver's spawn
 // point in toward the car when the far ones fail; PullOver* is the short final
 // move to the kerb at the destination; NextStroll paces the driver's walks while a
 // player keeps the parked car in view.
 vector HomePosition, PullOverGoal;
 float DepartAt, PullOverUntil, NextStroll;
 int CrewProbe;
 bool PullingOver, PulledOver;
 string Problem;
 string PendingReason;
 vector PendingPosition;
 protected EventHandlerManagerComponent m_Events;
 // The car's replication component, resolved once when the car is bound. The
 // per-frame horn read used to resolve it again on every frame.
 protected RplComponent m_CarRpl;

 bool Owns(IEntity entity)
 {
  if (!entity) return false;
  if (entity == Car || entity == Group) return true;
  foreach (EAC_TrafficOccupant row : Crew) if (row.Actor == entity) return true;
  return false;
 }

 bool HasEntities()
 {
  if (Car || Group || Order) return true;
  foreach (EAC_TrafficOccupant row : Crew) if (row.Actor) return true;
  return false;
 }

 void ClearOrder()
 {
  if (!Order) return;
  if (Group) Group.RemoveWaypoint(Order);
  SCR_EntityHelper.DeleteEntityAndChildren(Order);
 }

 void Fail(string reason, bool reportFailure = true)
 {
  if (Phase != EAC_TrafficPhase.FAILED)
  {
   Problem = reason;
   EAC_TrafficDirector director = EAC_TrafficDirector.Get();
   if (director && reportFailure) director.RecordFailure(this, reason);
  }
  Retire = true; WantDespawn = false; Phase = EAC_TrafficPhase.FAILED;
  ClearOrder();
  // An external/player takeover is never sent a new movement or handbrake input.
  if (!PlayerTouched && Controlled() && Movement) Movement.SetCruiseSpeed(0);
 }

 void OnCompartmentEntered(IEntity vehicle, BaseCompartmentManagerComponent manager, IEntity occupant, int managerId, int slotId)
 {
  if (vehicle == Car && !Owns(occupant)) { PlayerTouched = true; ClearOrder(); }
 }

 bool BindCar()
 {
  AIControlComponent control = AIControlComponent.Cast(Car.FindComponent(AIControlComponent));
  if (control) Movement = AICarMovementComponent.Cast(control.FindComponent(AICarMovementComponent));
  CarControl = CarControllerComponent.Cast(Car.FindComponent(CarControllerComponent));
  m_Events = EventHandlerManagerComponent.Cast(Car.FindComponent(EventHandlerManagerComponent));
  m_CarRpl = RplComponent.Cast(Car.FindComponent(RplComponent));
  if (!Movement || !CarControl || !m_Events) return false;
  m_Events.RegisterScriptHandler("OnCompartmentEntered", this, OnCompartmentEntered, false);
  if (!Controlled()) return false;
  BaseCompartmentManagerComponent compartments = BaseCompartmentManagerComponent.Cast(Car.FindComponent(BaseCompartmentManagerComponent));
  array<BaseCompartmentSlot> slots = {}; compartments.GetCompartments(slots);
  int pilots, cargo;
  foreach (BaseCompartmentSlot slot : slots)
  {
   if (!slot || slot.GetOccupant() || slot.IsReserved()) return false;
   if (slot.GetType() == ECompartmentType.PILOT) pilots++;
   if (slot.GetType() == ECompartmentType.CARGO) cargo++;
  }
  if (pilots != 1 || cargo < Crew.Count() - 1) return false;
  return true;
 }

 void UnbindCar()
 {
  if (m_Events) m_Events.RemoveScriptHandler("OnCompartmentEntered", this, OnCompartmentEntered, false);
  m_Events = null; Movement = null; CarControl = null; m_CarRpl = null;
 }

 void ~EAC_TrafficParty() { UnbindCar(); }

 bool Controlled()
 {
  if (!Replication.IsServer() || PlayerTouched || !Group || Group.GetPlayerCount() != 0) return false;
  RplComponent groupRpl = RplComponent.Cast(Group.FindComponent(RplComponent));
  if (!groupRpl || groupRpl.IsProxy() || !groupRpl.IsOwner()) { PlayerTouched = true; return false; }
  if (Group.GetAgentsCount() > Crew.Count()) { PlayerTouched = true; return false; }
  if (Car)
  {
   RplComponent rpl = RplComponent.Cast(Car.FindComponent(RplComponent));
   if (!rpl || rpl.IsProxy() || !rpl.IsOwner()) { PlayerTouched = true; return false; }
   BaseCompartmentManagerComponent manager = BaseCompartmentManagerComponent.Cast(Car.FindComponent(BaseCompartmentManagerComponent));
   if (!manager) return false;
   array<BaseCompartmentSlot> slots = {}; manager.GetCompartments(slots);
   if (slots.Count() > 16) return false;
   foreach (BaseCompartmentSlot slot : slots)
   {
    if (!slot) return false;
    if (slot.GetOccupant() && !Owns(slot.GetOccupant())) { PlayerTouched = true; return false; }
    if (slot.IsReserved())
    {
     bool ours;
     foreach (EAC_TrafficOccupant row : Crew) if (row.Actor && slot.IsReservedBy(row.Actor)) ours = true;
     if (!ours) return false;
    }
   }
  }
  foreach (EAC_TrafficOccupant row : Crew)
  {
   row.Observe();
   if (row.PlayerTouched || row.Dead) { PlayerTouched = row.PlayerTouched || PlayerTouched; return false; }
   if (!row.Actor) continue;
   RplComponent rpl = RplComponent.Cast(row.Actor.FindComponent(RplComponent));
   if (!rpl || rpl.IsProxy() || !rpl.IsOwner()) { PlayerTouched = true; return false; }
   AIControlComponent control = AIControlComponent.Cast(row.Actor.FindComponent(AIControlComponent));
   if (!control || !control.GetAIAgent()) return false;
   if ((row.Joined || control.GetAIAgent().GetParentGroup()) && control.GetAIAgent().GetParentGroup() != Group) { PlayerTouched = true; return false; }
  }
  return true;
 }

 bool AllSeated()
 {
  if (!Controlled() || !CarControl || Crew.IsEmpty()) return false;
  BaseCompartmentSlot pilot = CarControl.GetPilotCompartmentSlot();
  if (!pilot || !Owns(pilot.GetOccupant())) return false;
  foreach (EAC_TrafficOccupant row : Crew)
  {
   if (!row.Actor) return false;
   CompartmentAccessComponent access = CompartmentAccessComponent.Cast(row.Actor.FindComponent(CompartmentAccessComponent));
   if (!access || access.IsGettingIn() || access.IsGettingOut() || CompartmentAccessComponent.GetVehicleIn(row.Actor) != Car) return false;
  }
  return true;
 }

 bool AllOutside()
 {
  foreach (EAC_TrafficOccupant row : Crew)
  {
   if (!row.Actor) continue;
   CompartmentAccessComponent access = CompartmentAccessComponent.Cast(row.Actor.FindComponent(CompartmentAccessComponent));
   if (!access || access.IsInCompartment() || access.IsGettingIn() || access.IsGettingOut()) return false;
  }
  return true;
 }

 // Native cruise is a target, not a physical speed clamp. Reduce its target
 // progressively on overspeed so the native driver brakes instead of coasting.
 //
 // The divisor is the band over which authority is given up, and it was 5 - the
 // same number the QA gate allows as tolerance. So the requested target reached
 // zero exactly where the gate's ceiling sits, and past that point nothing was
 // asking the native driver for anything at all: compat set ii peaked at 25.09
 // against a 20 km/h target and failed by 0.09, set i at 24.35, one car at 21.6.
 // Over 3 the target bottoms out at configured+3, leaving the gate a 2 km/h
 // margin in which the governor is still asking for something.
 static float CruiseRequest(float configuredKmh, float actualKmh)
 {
  return Math.Max(0, configuredKmh) * Math.Clamp(1 - Math.Max(0, actualKmh - configuredKmh) / 3, 0, 1);
 }

 // Road speed, not velocity magnitude: the vertical component is suspension
 // travel and crests, and a car cresting a rise was reading its climb rate as
 // cruise speed - part of the same overspeed failure the governor above answers.
 float Speed()
 {
  if (!Car || !Car.GetPhysics()) return 0;
  vector travel = Car.GetPhysics().GetVelocity();
  travel[1] = 0;
  return travel.Length() * 3.6;
 }

 // Cheap per-frame filter. A record with no car, or one whose journey has not
 // reached boarding or is already past the dismount, has no driver seat to read
 // an input from; the director skips those without touching a component at all.
 bool CanSoundHorn()
 {
  if (!Car || !CarControl || PlayerTouched) return false;
  return Phase >= EAC_TrafficPhase.BOARD && Phase <= EAC_TrafficPhase.EXIT;
 }

 // Runs once per BOARD..EXIT party per FRAME, so everything it needs was resolved
 // when the car and the crew were bound: three FindComponent calls per party per
 // frame is not "an input read and a compare" however small each one is.
 bool UpdateHorn()
 {
  if (!Replication.IsServer() || !CanSoundHorn() || !Group || Group.GetPlayerCount() != 0) return false;
  if (!m_CarRpl || m_CarRpl.IsProxy() || !m_CarRpl.IsOwner()) return false;
  foreach (EAC_TrafficOccupant row : Crew) if (row.PlayerTouched || row.Dead) return false;
  BaseCompartmentSlot pilot = CarControl.GetPilotCompartmentSlot();
  if (!pilot) return false;
  IEntity seated = pilot.GetOccupant();
  if (!seated) return false;
  EAC_TrafficOccupant driver;
  foreach (EAC_TrafficOccupant row : Crew) if (row.Actor == seated) driver = row;
  if (!driver || !driver.Control || !driver.Control.GetAIAgent() || driver.Control.GetAIAgent().GetParentGroup() != Group) return false;
  CharacterControllerComponent controller = driver.Controller();
  if (!controller || controller.GetLifeState() != ECharacterLifeState.ALIVE || controller.IsPlayerControlled() || !controller.GetInputContext()) return false;
  bool requested = controller.GetInputContext().GetVehicleHorn() > 0;
  controller.GetInputContext().SetVehicleHorn(0);
  return requested;
 }

 bool EAC_SessionTransferred()
 {
  if (PlayerTouched) return true;
  foreach (EAC_TrafficOccupant row : Crew)
  {
   if (row.PlayerTouched || (row.Member && row.Member.WasPlayer)) return true;
   if (!row.Actor) continue;
   if (row.Actor.EBG_WasPlayerControlled() || SCR_PossessingManagerComponent.GetPlayerIdFromControlledEntity(row.Actor) != 0) return true;
   if (row.Control && row.Control.GetAIAgent() && row.Control.GetAIAgent().GetParentGroup() && row.Control.GetAIAgent().GetParentGroup() != Group) return true;
  }
  return false;
 }
}
