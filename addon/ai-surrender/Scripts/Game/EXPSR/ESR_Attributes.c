// Game Master attributes of the EXPBG AI Surrender module. Distinct subclasses are
// required by the editor's duplicate-type check; m_Key selects the global setting.
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class ESR_Attribute : SCR_BaseValueListEditorAttribute
{
 [Attribute("0")] protected int m_Key;

 // Native session saves and CDF restore the ten settings through these identities.
 override bool IsSerializable() { return m_Key >= 0 && m_Key < ESR_Settings.COUNT; }

 override int GetEntries(notnull array<ref SCR_BaseEditorAttributeEntry> outEntries)
 {
  if (ESR_Settings.IsBoolean(m_Key)) return outEntries.Count();
  return super.GetEntries(outEntries);
 }

 protected ESR_SurrenderModule Module(Managed item)
 {
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.Cast(item);
  if (!editable) return null;
  return ESR_SurrenderModule.Cast(editable.GetOwner());
 }

 override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
 {
  ESR_SurrenderModule module = Module(item);
  if (!module) return null;
  if (m_Key == ESR_Settings.PRISONERS)
  {
   // Status only; never part of a save.
   if (!manager) return null;
   return SCR_BaseEditorAttributeVar.CreateFloat(module.GetPrisonerCount());
  }
  if (!IsSerializable()) return null;
  if (ESR_Settings.IsBoolean(m_Key)) return SCR_BaseEditorAttributeVar.CreateBool(module.GetSetting(m_Key) != 0);
  return SCR_BaseEditorAttributeVar.CreateFloat(module.GetSetting(m_Key));
 }

 override void WriteVariable(Managed item, SCR_BaseEditorAttributeVar var, SCR_AttributesManagerEditorComponent manager, int playerID)
 {
  if (!Replication.IsServer() || !var || !IsSerializable()) return;
  // Native session and CDF restoration use the server-only system-write contract.
  if (!manager)
  {
   if (playerID != -1) return;
  }
  else
  {
   SCR_EditorManagerEntity editor = manager.GetManager();
   if (!editor || editor.GetPlayerID() != playerID || editor.IsLimited() || editor.GetCurrentMode() != EEditorMode.EDIT) return;
  }
  ESR_SurrenderModule module = Module(item);
  if (!module) return;
  if (ESR_Settings.IsBoolean(m_Key)) module.ApplySetting(m_Key, ESR_Settings.FromBool(var.GetBool()));
  else module.ApplySetting(m_Key, var.GetFloat());
 }
}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class ESR_EnabledAttribute : ESR_Attribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class ESR_ChanceAttribute : ESR_Attribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class ESR_ThresholdAttribute : ESR_Attribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class ESR_RandomAttribute : ESR_Attribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class ESR_RevealChanceAttribute : ESR_Attribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class ESR_IdentityChanceAttribute : ESR_Attribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class ESR_RevealRadiusAttribute : ESR_Attribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class ESR_AttemptsAttribute : ESR_Attribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class ESR_MarkerLifetimeAttribute : ESR_Attribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class ESR_DiagnosticsAttribute : ESR_Attribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class ESR_PrisonersAttribute : ESR_Attribute {}

// Some addons answer global attributes for every edited entity. When every edited item
// is a surrender module, keep only our attribute classes. Runs wherever the manager
// collects variables, so id/value/state arrays stay aligned on server and owner; any
// other or mixed selection is left exactly as vanilla built it.
modded class SCR_AttributesManagerEditorComponent
{
 protected bool ESR_OnlyModules(notnull array<Managed> items)
 {
  bool any;
  foreach (Managed item : items)
  {
   if (!item) continue;
   SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.Cast(item);
   if (!editable || !ESR_SurrenderModule.Cast(editable.GetOwner())) return false;
   any = true;
  }
  return any;
 }

 override protected int GetVariables(bool onlyServer, notnull array<Managed> items, notnull out array<int> outIds, notnull out array<ref SCR_BaseEditorAttributeVar> outVars, notnull out array<ref EEditorAttributeMultiSelect> outAttributesMultiSelect)
 {
  int count = super.GetVariables(onlyServer, items, outIds, outVars, outAttributesMultiSelect);
  if (count <= 0 || !ESR_OnlyModules(items)) return count;
  SCR_AttributesManagerEditorComponentClass data = SCR_AttributesManagerEditorComponentClass.Cast(GetEditorComponentData());
  if (!data || outIds.Count() != outVars.Count() || outIds.Count() != outAttributesMultiSelect.Count()) return count;
  for (int i = outIds.Count() - 1; i >= 0; i--)
  {
   if (ESR_Attribute.Cast(data.GetAttribute(outIds[i]))) continue;
   outIds.RemoveOrdered(i); outVars.RemoveOrdered(i); outAttributesMultiSelect.RemoveOrdered(i);
  }
  return outVars.Count();
 }
}
