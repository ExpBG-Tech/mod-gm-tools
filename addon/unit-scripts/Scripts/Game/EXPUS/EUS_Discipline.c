// Force Night Discipline on one AI squad. Server only.
//
// Both modes add EUS_LightSetting to every living AI member so vanilla AI stops
// switching vest lights itself (idle at night, off when it feels unsafe), then
// own the light state:
// Light Discipline: flashlights off; NIGHT_VISION gadgets already worn in an
//   equipment slot are switched on (vanilla has none; modded NVGs only).
// Terror Tactics: flashlights on; each member looks at the nearest player within
//   TERROR_RANGE so the body-mounted beam (which follows the aiming angles) faces
//   the players. Identified enemies (look priority 80) and commanders still win.
// Members joining the squad are adopted and members leaving it are released on
// the next bounded tick. Only gadgets in an unoccluded equipment slot are
// touched, exactly as the vanilla AI flashlight node does.
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
  foreach (SCR_GadgetComponent raised : member.RaisedNightVision)
  {
   if (raised && raised.IsToggledOn()) raised.ToggleActive(false, SCR_EUseContext.FROM_ACTION);
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
 }

 // Toggles only on a mismatch, so a steady state sends no replication traffic.
 static int SetGadgets(array<SCR_GadgetComponent> list, bool state, array<SCR_GadgetComponent> raised)
 {
  if (!list) return 0;
  int changed;
  foreach (SCR_GadgetComponent gadget : list)
  {
   if (!gadget) continue;
   InventoryItemComponent item = InventoryItemComponent.Cast(gadget.GetOwner().FindComponent(InventoryItemComponent));
   if (!item) continue;
   EquipmentStorageSlot slot = EquipmentStorageSlot.Cast(item.GetParentSlot());
   if (!slot) continue;
   bool wanted = state && !slot.IsOccluded();
   if (gadget.IsToggledOn() == wanted) continue;
   gadget.ToggleActive(wanted, SCR_EUseContext.FROM_ACTION);
   changed++;
   if (raised && wanted && !raised.Contains(gadget)) raised.Insert(gadget);
  }
  return changed;
 }

 // Counts slotted flashlights and how many are on (fixture/diagnostics).
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
   InventoryItemComponent item = InventoryItemComponent.Cast(gadget.GetOwner().FindComponent(InventoryItemComponent));
   if (!item || !EquipmentStorageSlot.Cast(item.GetParentSlot())) continue;
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
