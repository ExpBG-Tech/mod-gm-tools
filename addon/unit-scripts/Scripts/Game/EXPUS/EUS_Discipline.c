// Force Night Discipline on one AI squad. Server only.
//
// Both modes add EUS_LightSetting to every living AI member so vanilla AI stops
// switching vest lights itself (idle at night, off when it feels unsafe), then
// own the light state:
// Light Discipline: flashlights off; NIGHT_VISION gadgets already worn are
//   switched on (vanilla has none; modded gear only, e.g. RHS helmet NVGs).
// Terror Tactics: flashlights on; each member looks at the nearest player within
//   TERROR_RANGE so the body-mounted beam (which follows the aiming angles) faces
//   the players. Identified enemies (look priority 80) and commanders still win.
//   Night vision switched on by Light Discipline is switched off again.
// Members joining the squad are adopted and members leaving it are released on
// the next bounded tick. Only worn gadgets are touched: gear in an equipment
// slot (switched off while the slot is occluded, exactly as the vanilla AI
// flashlight node does), or gear its own component reports as IN_SLOT without
// one (RHS NVGs sit in the worn helmet's loadout slot).
class EUS_DisciplineMember
{
 SCR_ChimeraCharacter Actor;
 SCR_AICharacterSettingsComponent Settings;
 SCR_AIUtilityComponent Utility;
 ref EUS_LightSetting Setting;
 // Night vision devices this record switched on; switched off again on release.
 ref array<SCR_GadgetComponent> RaisedNightVision = {};
}

class EUS_DisciplineRecord
{
 static const float TERROR_RANGE = 60;

 SCR_AIGroup Group;
 int Mode;
 ref array<ref EUS_DisciplineMember> Members = {};
 bool Ended;

 static string Eligibility(SCR_AIGroup group)
 {
  if (!group || group.IsDeleted()) return "the squad no longer exists";
  if (group.GetPlayerCount() > 0 || group.IsPlayable()) return "the squad has players";
  if (group.GetAgentsCount() < 1) return "the squad has no AI members";
  return string.Empty;
 }

 void Apply(int mode)
 {
  Mode = mode;
  Reconcile();
  foreach (EUS_DisciplineMember member : Members) ApplyLights(member);
  if (Group) Group.EUS_SetDiscipline(mode);
  PrintFormat("[EUS] discipline group=%1 mode='%2' members=%3", Group, EUS_Codes.DescribeDiscipline(mode), Members.Count());
 }

 int CountMembers()
 {
  return Members.Count();
 }

 // False once the squad is gone or no longer eligible.
 bool Tick(notnull array<IEntity> players)
 {
  if (Ended) return false;
  if (!Eligibility(Group).IsEmpty())
  {
   Release("the squad is gone, empty or player-led");
   return false;
  }
  Reconcile();
  foreach (EUS_DisciplineMember member : Members)
  {
   ApplyLights(member);
   if (Mode == EUS_Codes.DISCIPLINE_TERROR) PointAtPlayers(member, players);
  }
  return true;
 }

 protected EUS_DisciplineMember Find(SCR_ChimeraCharacter actor)
 {
  foreach (EUS_DisciplineMember member : Members)
  {
   if (member.Actor == actor) return member;
  }
  return null;
 }

 protected bool IsLivingAI(SCR_ChimeraCharacter actor)
 {
  if (!actor || actor.IsDeleted()) return false;
  CharacterControllerComponent controller = actor.GetCharacterController();
  return controller && !controller.IsDead() && !controller.IsPlayerControlled();
 }

 protected void Reconcile()
 {
  if (!Group) return;
  for (int index = Members.Count() - 1; index >= 0; index--)
  {
   EUS_DisciplineMember stale = Members[index];
   if (IsLivingAI(stale.Actor) && stale.Actor.GetCharacterGroup() == Group) continue;
   Detach(stale);
   Members.RemoveOrdered(index);
  }
  array<AIAgent> agents = {};
  Group.GetAgents(agents);
  foreach (AIAgent agent : agents)
  {
   SCR_ChimeraCharacter actor = SCR_ChimeraCharacter.Cast(agent.GetControlledEntity());
   if (!IsLivingAI(actor) || Find(actor)) continue;
   Attach(actor, agent);
  }
 }

 protected void Attach(SCR_ChimeraCharacter actor, AIAgent agent)
 {
  SCR_AICharacterSettingsComponent settings = SCR_AICharacterSettingsComponent.Cast(agent.FindComponent(SCR_AICharacterSettingsComponent));
  if (!settings) return;
  EUS_DisciplineMember member = new EUS_DisciplineMember();
  member.Actor = actor;
  member.Settings = settings;
  member.Utility = SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent));
  member.Setting = EUS_LightSetting.Create();
  if (!settings.AddCharacterSetting(member.Setting, false, false)) return;
  Members.Insert(member);
 }

 protected void Detach(EUS_DisciplineMember member)
 {
  if (member.Settings && member.Setting) member.Settings.RemoveSetting(member.Setting);
  member.Setting = null;
  LowerNightVision(member);
 }

 // Switches off the night vision this record switched on (release or a switch
 // to Terror Tactics); gear the AI already had on before stays untracked.
 protected void LowerNightVision(EUS_DisciplineMember member)
 {
  foreach (SCR_GadgetComponent raised : member.RaisedNightVision)
  {
   if (raised && raised.IsToggledOn()) SwitchGadget(raised, false);
  }
  member.RaisedNightVision.Clear();
 }

 protected void ApplyLights(EUS_DisciplineMember member)
 {
  if (!member.Actor) return;
  SCR_GadgetManagerComponent gadgets = SCR_GadgetManagerComponent.GetGadgetManager(member.Actor);
  if (!gadgets) return;
  SetGadgets(gadgets.GetGadgetsByType(EGadgetType.FLASHLIGHT), Mode == EUS_Codes.DISCIPLINE_TERROR, null);
  if (Mode == EUS_Codes.DISCIPLINE_LIGHT) SetGadgets(gadgets.GetGadgetsByType(EGadgetType.NIGHT_VISION), true, member.RaisedNightVision);
  else if (!member.RaisedNightVision.IsEmpty()) LowerNightVision(member);
 }

 // Toggles only on a mismatch, so a steady state sends no replication traffic.
 static int SetGadgets(array<SCR_GadgetComponent> list, bool state, array<SCR_GadgetComponent> raised)
 {
  if (!list) return 0;
  int changed;
  foreach (SCR_GadgetComponent gadget : list)
  {
   if (!gadget) continue;
   bool occluded;
   if (!IsWorn(gadget, occluded)) continue;
   bool wanted = state && !occluded;
   if (gadget.IsToggledOn() == wanted) continue;
   SwitchGadget(gadget, wanted);
   changed++;
   if (raised && wanted && !raised.Contains(gadget)) raised.Insert(gadget);
  }
  return changed;
 }

 // True when the gadget is worn rather than carried. Vanilla gear is worn in an
 // EquipmentStorageSlot (the vest light strap), which can be occluded by other
 // clothing. RHS NVGs sit in the worn helmet's RHS_LoadoutSlotInfo, which is not
 // an equipment slot; RHS_RhinoAttachmentComponent reports IN_SLOT only while
 // that helmet is on the head (RHS_RpcManager.GetOwnedNVG uses the same test).
 static bool IsWorn(notnull SCR_GadgetComponent gadget, out bool occluded)
 {
  occluded = false;
  InventoryItemComponent item = InventoryItemComponent.Cast(gadget.GetOwner().FindComponent(InventoryItemComponent));
  if (!item) return false;
  InventoryStorageSlot parentSlot = item.GetParentSlot();
  if (!parentSlot) return false;
  EquipmentStorageSlot equipmentSlot = EquipmentStorageSlot.Cast(parentSlot);
  if (equipmentSlot)
  {
   occluded = equipmentSlot.IsOccluded();
   return true;
  }
  return gadget.GetMode() == EGadgetMode.IN_SLOT;
 }

 // SCR_GadgetComponent.ToggleActive ignores a context outside the gadget's use
 // mask. Vanilla flashlights (mask 7) keep FROM_ACTION, as the vanilla AI
 // flashlight node uses; RHS NVGs (NVG_Base.et: CUSTOM only) get their own mask.
 static void SwitchGadget(notnull SCR_GadgetComponent gadget, bool wanted)
 {
  SCR_EUseContext useContext = gadget.GetUseMask();
  if ((useContext & SCR_EUseContext.FROM_ACTION) != 0) useContext = SCR_EUseContext.FROM_ACTION;
  gadget.ToggleActive(wanted, useContext);
 }

 // Counts worn flashlights and how many are on (fixture/diagnostics).
 static void CountLights(SCR_ChimeraCharacter actor, out int total, out int lit)
 {
  total = 0;
  lit = 0;
  SCR_GadgetManagerComponent gadgets = SCR_GadgetManagerComponent.GetGadgetManager(actor);
  if (!gadgets) return;
  array<SCR_GadgetComponent> list = gadgets.GetGadgetsByType(EGadgetType.FLASHLIGHT);
  if (!list) return;
  foreach (SCR_GadgetComponent gadget : list)
  {
   if (!gadget) continue;
   bool occluded;
   if (!IsWorn(gadget, occluded)) continue;
   total++;
   if (gadget.IsToggledOn()) lit++;
  }
 }

 protected void PointAtPlayers(EUS_DisciplineMember member, notnull array<IEntity> players)
 {
  if (!member.Utility || !member.Utility.m_LookAction) return;
  IEntity target = EUS_Codes.Nearest(member.Actor.EyePosition(), players, TERROR_RANGE, vector.Zero, 0, member.Actor);
  if (!target) return;
  member.Utility.m_LookAction.LookAt(EUS_Codes.Eye(target), EUS_Codes.LOOK_PRIORITY, 2.0);
 }

 void Release(string reason)
 {
  if (Ended) return;
  Ended = true;
  foreach (EUS_DisciplineMember member : Members) Detach(member);
  Members.Clear();
  if (Group) Group.EUS_SetDiscipline(EUS_Codes.DISCIPLINE_OFF);
  if (!reason.IsEmpty()) PrintFormat("[EUS] discipline released group=%1 reason='%2'", Group, reason);
 }

 void ~EUS_DisciplineRecord()
 {
  Release(string.Empty);
 }
}
