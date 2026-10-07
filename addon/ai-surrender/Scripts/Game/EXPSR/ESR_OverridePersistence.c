// Native mission saves (1.8 persistence) of the AI Surrender squad and soldier overrides
// (ESR_Overrides): persistence ids of the squads and soldiers that have one, with four
// values each (surrender, reveal squad, identity, intel; -1 = module setting). The
// entities themselves are saved by vanilla; on load the values bind to them by id
// (ESR_Overrides.Import retries for two minutes). Saves without overrides write nothing
// (DEFAULT), so missions saved before this state existed load unchanged.
// Attribute-based savers (CDF Game Master Save) use ESR_OverrideAttributes.c instead.
class ESR_OverridesState : PersistentState
{
}

class ESR_OverridesSerializer : ScriptedStateSerializer
{
 protected static const int VERSION = 1;

 override static typename GetTargetType()
 {
  return ESR_OverridesState;
 }

 override static EDeserializeFailHandling GetDeserializeFailHandling()
 {
  return EDeserializeFailHandling.IGNORE;
 }

 override protected ESerializeResult Serialize(notnull Managed instance, notnull SaveContext context)
 {
  array<UUID> groupIds = {};
  array<int> groupValues = {};
  array<UUID> unitIds = {};
  array<int> unitValues = {};
  ESR_Overrides.Export(groupIds, groupValues, unitIds, unitValues);
  if (groupIds.IsEmpty() && unitIds.IsEmpty())
   return ESerializeResult.DEFAULT;
  int version = VERSION;
  if (!context.WriteValue("version", version))
   return ESerializeResult.ERROR;
  if (!context.WriteValue("groupIds", groupIds) || !context.WriteValue("groupValues", groupValues))
   return ESerializeResult.ERROR;
  if (!context.WriteValue("unitIds", unitIds) || !context.WriteValue("unitValues", unitValues))
   return ESerializeResult.ERROR;
  ESR_SurrenderManager.Trace(string.Format("overrides saved: squads=%1 soldiers=%2", groupIds.Count(), unitIds.Count()));
  return ESerializeResult.OK;
 }

 override protected bool Deserialize(notnull Managed instance, notnull LoadContext context)
 {
  int version;
  if (!context.ReadValue("version", version) || version != VERSION)
   return false;
  array<UUID> groupIds = {};
  array<int> groupValues = {};
  array<UUID> unitIds = {};
  array<int> unitValues = {};
  if (!context.ReadValue("groupIds", groupIds) || !context.ReadValue("groupValues", groupValues))
   return false;
  if (!context.ReadValue("unitIds", unitIds) || !context.ReadValue("unitValues", unitValues))
   return false;
  if (groupValues.Count() != groupIds.Count() * ESR_Overrides.COUNT || unitValues.Count() != unitIds.Count() * ESR_Overrides.COUNT)
   return false;
  if (groupIds.Count() > ESR_Overrides.MAX_TRACKED || unitIds.Count() > ESR_Overrides.MAX_TRACKED)
   return false;
  ESR_Overrides.Import(groupIds, groupValues, unitIds, unitValues);
  return true;
 }
}
