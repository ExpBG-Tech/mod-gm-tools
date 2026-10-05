// Some addons return global setting values for every edited item. Keep those
// controls out of our entity dialogs without changing their scenario dialogs.
class EAS_AttributeScope
{
 static void Filter(notnull array<typename> types, notnull array<ResourceName> categories, notnull array<int> ids, notnull array<ref SCR_BaseEditorAttributeVar> vars, notnull array<ref EEditorAttributeMultiSelect> states)
 {
  if (types.IsEmpty()) return;
  foreach (typename type: types)
   if (!type.IsInherited(EAS_AmbientModule) && !type.IsInherited(EAS_RadioModule)) return;

  for (int i = ids.Count() - 1; i >= 0; i--)
  {
   if (categories[i].IsEmpty()) continue;
   Resource resource = BaseContainerTools.LoadContainer(categories[i]);
   if (!resource) continue;
   SCR_EditorAttributeCategory category = SCR_EditorAttributeCategory.Cast(BaseContainerTools.CreateInstanceFromContainer(resource.GetResource().ToBaseContainer()));
   if (!category || !category.GetIsGlobalAttributeCategory()) continue;
   // Native RPC IDs stay unchanged; remove matching rows, never renumber them.
   ids.RemoveOrdered(i);
   vars.RemoveOrdered(i);
   states.RemoveOrdered(i);
  }
 }
}

modded class SCR_AttributesManagerEditorComponent
{
 override protected int GetVariables(bool onlyServer, notnull array<Managed> items, notnull out array<int> outIds, notnull out array<ref SCR_BaseEditorAttributeVar> outVars, notnull out array<ref EEditorAttributeMultiSelect> outAttributesMultiSelect)
 {
  super.GetVariables(onlyServer, items, outIds, outVars, outAttributesMultiSelect);
  array<typename> types = {};
  foreach (Managed item: items)
  {
   SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.Cast(item);
   if (!editable || !editable.GetOwner()) return outVars.Count();
   types.Insert(editable.GetOwner().Type());
  }
  SCR_AttributesManagerEditorComponentClass data = SCR_AttributesManagerEditorComponentClass.Cast(GetEditorComponentData());
  if (!data) return outVars.Count();
  array<ResourceName> categories = {};
  foreach (int id: outIds)
  {
   SCR_BaseEditorAttribute attribute = data.GetAttribute(id);
   if (attribute) categories.Insert(attribute.GetCategoryConfig());
   else categories.Insert(string.Empty);
  }
  EAS_AttributeScope.Filter(types, categories, outIds, outVars, outAttributesMultiSelect);
  return outVars.Count();
 }
}
