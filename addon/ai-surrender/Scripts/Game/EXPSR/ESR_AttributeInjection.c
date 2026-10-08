// The squad and soldier attributes of the "EXPBG Surrender & Intel" tab are listed in the
// vanilla Edit attribute list override (Configs/Editor/AttributeLists/Edit.conf) and, as a
// fallback, appended here from this module's own list
// (Configs/Editor/AttributeLists/EXPSR/ESR_SquadSoldier.conf). Another mod that replaces the
// vanilla Edit list, or an editor mode prefab override that swaps the list, therefore cannot
// hide them. Only classes the Game Master Edit list does not hold yet are appended, once,
// lazily, before the class first hands out attribute indexes; server and clients run the
// same code on the same lists, so the indexes sent between them agree. Only the Edit mode
// list is extended (it holds the vanilla "Set combat mode" attribute); Admin and Photo modes
// keep theirs.
modded class SCR_AttributesManagerEditorComponentClass
{
 protected static const ResourceName ESR_SQUAD_SOLDIER_ATTRIBUTES = "{867D8E3D247F48BF}Configs/Editor/AttributeLists/EXPSR/ESR_SquadSoldier.conf";

 protected bool m_bESR_Injected;
 // Owns the appended attributes (m_aAttributes holds weak references).
 protected ref SCR_EditorAttributeList m_ESR_SquadSoldierList;

 // The Game Master Edit list: it holds the vanilla "Set combat mode" attribute or this
 // module's own attributes.
 protected bool ESR_IsEditList()
 {
  foreach (SCR_BaseEditorAttribute attribute : m_aAttributes)
  {
   if (SCR_AIGroupCombatModeAttribute.Cast(attribute) || ESR_Attribute.Cast(attribute))
    return true;
  }
  return false;
 }

 protected bool ESR_HasAttributeType(typename type)
 {
  foreach (SCR_BaseEditorAttribute attribute : m_aAttributes)
  {
   if (attribute && attribute.Type() == type)
    return true;
  }
  return false;
 }

 protected void ESR_InjectSquadSoldier()
 {
  if (m_bESR_Injected)
   return;
  m_bESR_Injected = true;
  if (!m_aAttributes || !ESR_IsEditList())
   return;
  Resource resource = BaseContainerTools.LoadContainer(ESR_SQUAD_SOLDIER_ATTRIBUTES);
  if (!resource || !resource.IsValid())
  {
   Print("[EXPBG SURRENDER] squad and soldier attribute list missing: only the Edit list entries are used", LogLevel.WARNING);
   return;
  }
  BaseResourceObject container = resource.GetResource();
  if (!container)
   return;
  m_ESR_SquadSoldierList = SCR_EditorAttributeList.Cast(BaseContainerTools.CreateInstanceFromContainer(container.ToBaseContainer()));
  if (!m_ESR_SquadSoldierList)
   return;
  int appended;
  for (int i = 0, count = m_ESR_SquadSoldierList.GetAttributesCount(); i < count; i++)
  {
   SCR_BaseEditorAttribute attribute = m_ESR_SquadSoldierList.GetAttribute(i);
   if (!attribute || ESR_HasAttributeType(attribute.Type()))
    continue;
   m_aAttributes.Insert(attribute);
   appended++;
  }
  if (appended > 0)
   PrintFormat("[EXPBG SURRENDER] %1 squad/soldier attributes appended to the Game Master Edit list (its override did not carry them)", appended);
 }

 override SCR_BaseEditorAttribute GetAttribute(int index)
 {
  ESR_InjectSquadSoldier();
  return super.GetAttribute(index);
 }

 override int GetAttributesCount()
 {
  ESR_InjectSquadSoldier();
  return super.GetAttributesCount();
 }

 override int FindAttribute(SCR_BaseEditorAttribute attribute)
 {
  ESR_InjectSquadSoldier();
  return super.FindAttribute(attribute);
 }
}
