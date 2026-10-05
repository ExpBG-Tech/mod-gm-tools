// EXPBG server rack attributes, shown in the existing EXPBG Intel category. Text uses the
// Intel Items string variable and edit boxes; it is read for the editing GM only, as
// numeric-only session serializers cannot preserve a string. Numbers use native vars so
// session/CDF attribute restoration (no manager, player -1) can apply them.
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EIR_RackTitleAttribute : SCR_BaseEditorAttribute
{
 override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
 {
  EIR_RackComponent rack = EIR_RackComponent.FromItem(item);
  if (!rack || !manager) return null;
  return SCR_BaseEditorAttributeVar.EII_CreateString(rack.GetTitle());
 }
 override void WriteVariable(Managed item, SCR_BaseEditorAttributeVar var, SCR_AttributesManagerEditorComponent manager, int playerID)
 {
  EIR_RackComponent rack = EIR_RackComponent.FromItem(item);
  if (!rack || !var || !manager || !EIR_RackComponent.EditAllowed(manager, playerID)) return;
  if (!rack.SetText(var.EII_GetString(), rack.GetContent()))
   Print("[EIR] Rack title edit rejected: maximum 128 bytes", LogLevel.WARNING);
 }
}

// Inherits the Intel text attribute only so the shared edit box applies the 4096-byte
// text limit instead of the 128-byte title limit; both methods target the rack.
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EIR_RackContentAttribute : EII_ContentAttribute
{
 override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
 {
  EIR_RackComponent rack = EIR_RackComponent.FromItem(item);
  if (!rack || !manager) return null;
  return SCR_BaseEditorAttributeVar.EII_CreateString(rack.GetContent());
 }
 override void WriteVariable(Managed item, SCR_BaseEditorAttributeVar var, SCR_AttributesManagerEditorComponent manager, int playerID)
 {
  EIR_RackComponent rack = EIR_RackComponent.FromItem(item);
  if (!rack || !var || !manager || !EIR_RackComponent.EditAllowed(manager, playerID)) return;
  if (!rack.SetText(rack.GetTitle(), var.EII_GetString()))
   Print("[EIR] Rack text edit rejected: maximum 4096 bytes", LogLevel.WARNING);
 }
}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EIR_RackSecondsAttribute : SCR_BaseValueListEditorAttribute
{
 override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
 {
  EIR_RackComponent rack = EIR_RackComponent.FromItem(item);
  if (!rack) return null;
  return SCR_BaseEditorAttributeVar.CreateFloat(rack.GetSeconds());
 }
 override void WriteVariable(Managed item, SCR_BaseEditorAttributeVar var, SCR_AttributesManagerEditorComponent manager, int playerID)
 {
  EIR_RackComponent rack = EIR_RackComponent.FromItem(item);
  if (!rack || !var || !EIR_RackComponent.EditAllowed(manager, playerID)) return;
  rack.SetSeconds(Math.Round(var.GetFloat()));
 }
}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EIR_RackDebugAttribute : SCR_BaseEditorAttribute
{
 override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
 {
  EIR_RackComponent rack = EIR_RackComponent.FromItem(item);
  if (!rack) return null;
  return SCR_BaseEditorAttributeVar.CreateBool(rack.GetDebug());
 }
 override void WriteVariable(Managed item, SCR_BaseEditorAttributeVar var, SCR_AttributesManagerEditorComponent manager, int playerID)
 {
  EIR_RackComponent rack = EIR_RackComponent.FromItem(item);
  if (!rack || !var || !EIR_RackComponent.EditAllowed(manager, playerID)) return;
  rack.SetDebug(var.GetBool());
 }
}
