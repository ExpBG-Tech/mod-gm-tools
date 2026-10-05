[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EII_TitleAttribute : SCR_BaseEditorAttribute
{
 protected EII_IntelComponent Intel(Managed item)
 {
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.Cast(item);
  if (!editable || !editable.GetOwner()) return null;
  return EII_IntelComponent.Cast(editable.GetOwner().FindComponent(EII_IntelComponent));
 }
 // Numeric-only session serializers cannot preserve a string. CDF companion owns persistence.
 override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
 {
  EII_IntelComponent intel = Intel(item);
  if (!intel || !manager) return null;
  return SCR_BaseEditorAttributeVar.EII_CreateString(intel.GetTitle());
 }
 override void WriteVariable(Managed item, SCR_BaseEditorAttributeVar var, SCR_AttributesManagerEditorComponent manager, int playerID)
 {
  EII_IntelComponent intel = Intel(item);
  if (intel && var && manager && !intel.SetText(var.EII_GetString(), intel.GetContent()))
   Print("[EII] Title edit rejected: maximum 128 bytes", LogLevel.WARNING);
 }
}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EII_ContentAttribute : EII_TitleAttribute
{
 override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
 {
  EII_IntelComponent intel = Intel(item);
  if (!intel || !manager) return null;
  return SCR_BaseEditorAttributeVar.EII_CreateString(intel.GetContent());
 }
 override void WriteVariable(Managed item, SCR_BaseEditorAttributeVar var, SCR_AttributesManagerEditorComponent manager, int playerID)
 {
  EII_IntelComponent intel = Intel(item);
  if (intel && var && manager && !intel.SetText(intel.GetTitle(), var.EII_GetString()))
   Print("[EII] Text edit rejected: maximum 4096 bytes", LogLevel.WARNING);
 }
}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EII_DebugAttribute : EII_TitleAttribute
{
 override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
 {
  EII_IntelComponent intel = Intel(item);
  if (!intel) return null;
  return SCR_BaseEditorAttributeVar.CreateBool(intel.GetDebug());
 }
 override void WriteVariable(Managed item, SCR_BaseEditorAttributeVar var, SCR_AttributesManagerEditorComponent manager, int playerID)
 {
  EII_IntelComponent intel = Intel(item);
  if (intel && var) intel.SetDebug(var.GetBool());
 }
}
