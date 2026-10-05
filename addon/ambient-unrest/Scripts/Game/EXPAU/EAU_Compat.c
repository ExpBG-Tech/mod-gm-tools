// Unit Caching: a protest crowd is owned by its zone. Use the published reservation
// seam so it is never enrolled, cached or restored by a cache zone.
modded class EBG_CacheManager
{
 override bool IsReserved(SCR_AIGroup group)
 {
  if (EAU_Director.ReservesGroup(group)) return true;
  return super.IsReserved(group);
 }
}

// Native regroup builds its plan synchronously and bypasses IsReserved.
modded class EBG_CacheRegroup
{
 override protected EBG_RegroupPlan Build(EBG_CacheManager manager, EBG_CacheGroup seed)
 {
  EBG_RegroupPlan plan = super.Build(manager, seed);
  if (!plan) return plan;
  foreach (SCR_AIGroup group : plan.Groups)
  {
   if (EAU_Director.ReservesGroup(group))
   {
    plan.Problem = "Regroup held: EXPBG Civil Protest Zone ownership";
    break;
   }
  }
  return plan;
 }
}

// Keeps other mods' global switches out of the protest zone dialog. Only a
// selection made entirely of protest zones is filtered; the same filter runs on
// server and owner, so ids, values and multi-select states stay aligned.
modded class SCR_AttributesManagerEditorComponent
{
 protected bool EAU_OnlyProtestZones(notnull array<Managed> items)
 {
  bool any;
  foreach (Managed item : items)
  {
   if (!item) continue;
   SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.Cast(item);
   if (!editable || !EAU_ProtestZone.Cast(editable.GetOwner())) return false;
   any = true;
  }
  return any;
 }

 override protected int GetVariables(bool onlyServer, notnull array<Managed> items, notnull out array<int> outIds, notnull out array<ref SCR_BaseEditorAttributeVar> outVars, notnull out array<ref EEditorAttributeMultiSelect> outAttributesMultiSelect)
 {
  int count = super.GetVariables(onlyServer, items, outIds, outVars, outAttributesMultiSelect);
  if (count <= 0 || !EAU_OnlyProtestZones(items)) return count;
  SCR_AttributesManagerEditorComponentClass data = SCR_AttributesManagerEditorComponentClass.Cast(GetEditorComponentData());
  if (!data || outIds.Count() != outVars.Count() || outIds.Count() != outAttributesMultiSelect.Count()) return count;
  for (int i = outIds.Count() - 1; i >= 0; i--)
  {
   if (EAU_ZoneAttribute.Cast(data.GetAttribute(outIds[i]))) continue;
   outIds.RemoveOrdered(i);
   outVars.RemoveOrdered(i);
   outAttributesMultiSelect.RemoveOrdered(i);
  }
  return outVars.Count();
 }
}
