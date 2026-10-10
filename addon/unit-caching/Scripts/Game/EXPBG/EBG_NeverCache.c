// "Never cache" on an AI squad, or on one of its soldiers (it applies to his squad):
// Unit Caching skips the squad (EBG_Exclude, the same switch as the Workbench
// "Exclude mission-critical group" attribute) and an already cached squad is woken
// and released through the normal scheduler. Shown in the EXPBG Unit Caching tab of
// the squad's and soldier's Edit properties. Session saves: CDF Game Master Save
// reads it with a null manager and writes it back on load with playerID -1.
//
// Air vehicles are never cached either way (EBG_CacheManager.HasAirMember).
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EBG_NeverCacheAttribute : SCR_BaseEditorAttribute
{
 override bool IsSerializable() { return true; }

 protected static SCR_AIGroup Target(Managed item)
 {
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.Cast(item);
  if (!editable || !editable.GetOwner()) return null;
  SCR_AIGroup group = SCR_AIGroup.Cast(editable.GetOwner());
  if (group) return group;
  SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(editable.GetOwner());
  if (!character) return null;
  AIControlComponent control = AIControlComponent.Cast(character.FindComponent(AIControlComponent));
  if (!control || !control.GetControlAIAgent()) return null;
  return SCR_AIGroup.Cast(control.GetControlAIAgent().GetParentGroup());
 }

 override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
 {
  SCR_AIGroup group = Target(item);
  if (!group || group.GetPlayerCount() > 0) return null;
  // Session saves keep only a set switch.
  if (!manager && !group.EBG_Exclude) return null;
  return SCR_BaseEditorAttributeVar.CreateBool(group.EBG_Exclude);
 }

 override void WriteVariable(Managed item, SCR_BaseEditorAttributeVar var, SCR_AttributesManagerEditorComponent manager, int playerID)
 {
  if (!Replication.IsServer() || !var) return;
  if (manager)
  {
   SCR_EditorManagerEntity editor = manager.GetManager();
   if (!editor || editor.GetPlayerID() != playerID || editor.IsLimited()) return;
  }
  else if (playerID != -1) return;
  SCR_AIGroup group = Target(item);
  if (!group) return;
  bool never = var.GetBool();
  if (group.EBG_Exclude == never) return;
  group.EBG_Exclude = never;
  PrintFormat("[EBG NEVER CACHE] group=%1 never cache=%2 (player %3)", group, never, playerID);
  if (!never) return;
  EBG_CacheManager cache = EBG_CacheManager.Get();
  if (!cache) return;
  EBG_CacheGroup record = cache.FindGroup(group);
  if (!record) return;
  record.ReleaseRequested = true;
  if (record.Full || record.Simulation) record.WakeRequested = true;
 }
}
