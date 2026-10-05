// Keeps other mods' settings out of the Ambient Civilians and No Civilian Zone
// property dialogs.
//
// The attribute manager offers an attribute for an edited entity whenever that
// attribute's ReadVariable returns a value. An attribute written as a global switch
// (seen 2026-09-19: a drive-by mod's "Enabled" and "Friendly Fire" pair, whose
// ReadVariable never looks at the item) therefore answers for every entity and was
// listed inside our module's dialog, next to our sliders. Nothing of ours is
// affected by it, but it reads as if this module owned those settings.
//
// When every edited item is one of our two entities, only our attribute classes are
// kept. The same filter runs wherever the manager collects variables - server and
// owner - so the id, value and multi-select arrays stay aligned on both sides and
// the manager's index-based replication is unchanged. Any other selection, and any
// selection that mixes our entities with others, is left exactly as vanilla built it.
modded class SCR_AttributesManagerEditorComponent
{
 protected bool EAC_OnlyOwnEntities(notnull array<Managed> items)
 {
  bool any;
  foreach (Managed item : items)
  {
   if (!item) continue;
   SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.Cast(item);
   if (!editable || !editable.GetOwner()) return false;
   IEntity owner = editable.GetOwner();
   if (!EAC_AmbientModule.Cast(owner) && !EAC_ExclusionZone.Cast(owner)) return false;
   any = true;
  }
  return any;
 }

 override protected int GetVariables(bool onlyServer, notnull array<Managed> items, notnull out array<int> outIds, notnull out array<ref SCR_BaseEditorAttributeVar> outVars, notnull out array<ref EEditorAttributeMultiSelect> outAttributesMultiSelect)
 {
  int count = super.GetVariables(onlyServer, items, outIds, outVars, outAttributesMultiSelect);
  if (count <= 0 || !EAC_OnlyOwnEntities(items)) return count;
  SCR_AttributesManagerEditorComponentClass data = SCR_AttributesManagerEditorComponentClass.Cast(GetEditorComponentData());
  if (!data || outIds.Count() != outVars.Count() || outIds.Count() != outAttributesMultiSelect.Count()) return count;
  for (int i = outIds.Count() - 1; i >= 0; i--)
  {
   SCR_BaseEditorAttribute attribute = data.GetAttribute(outIds[i]);
   if (EAC_AmbientAttribute.Cast(attribute) || EAC_ExclusionAttribute.Cast(attribute)) continue;
   outIds.RemoveOrdered(i); outVars.RemoveOrdered(i); outAttributesMultiSelect.RemoveOrdered(i);
  }
  return outVars.Count();
 }
}
