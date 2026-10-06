// Native (vanilla) mission save support. One world record lists every configured
// unit by its persistence UUID; native persistence restores the characters, and
// each row is applied once its character is available again. No native entity
// or component serializer is overridden. Registered in GameMaster.conf.
[BaseContainerProps()]
class EUD_DialogPersistenceState : PersistentState
{
}

class EUD_DialogPersistenceSerializer : ScriptedStateSerializer
{
 static const int VERSION = 1;
 // Seconds a saved row waits for its character after the initial load setup.
 static const float AVAILABLE_WAIT = 120;

 override static typename GetTargetType() { return EUD_DialogPersistenceState; }

 override protected ESerializeResult Serialize(notnull Managed instance, notnull SaveContext context)
 {
  PersistenceSystem system = GetSystem();
  array<SCR_EditableCharacterComponent> units = {};
  EUD_Dialog.GetConfigured(units);
  array<ref EUD_DialogRecord> rows = {};
  int untracked;
  foreach (SCR_EditableCharacterComponent unit : units)
  {
   if (!unit || !unit.GetOwner() || !unit.EUD_IsConfigured()) continue;
   EUD_DialogRecord row = unit.EUD_Capture();
   if (system) row.id = system.GetId(unit.GetOwner());
   if (row.id.IsNull()) { untracked++; continue; }
   rows.Insert(row);
  }
  if (!context.WriteValue("eudVersion", VERSION) || !context.WriteValue("count", rows.Count())) return ESerializeResult.ERROR;
  // Distinct names per function: Enforce rejects a redeclared local in sibling blocks.
  foreach (int index, EUD_DialogRecord saved : rows)
  {
   if (!context.StartObject("unit" + index.ToString()) || !saved.Write(context) || !context.EndObject()) return ESerializeResult.ERROR;
  }
  if (untracked > 0)
   Print(string.Format("[EUD] Saved %1 dialog units; %2 units without a persistence id were skipped", rows.Count(), untracked), LogLevel.WARNING);
  return ESerializeResult.OK;
 }

 override protected bool Deserialize(notnull Managed instance, notnull LoadContext context)
 {
  int version, count;
  if (!context.ReadValue("eudVersion", version) || version != VERSION) return false;
  if (!context.ReadValue("count", count) || count < 0 || count > EUD_Dialog.MAX_UNITS) return false;
  // Validate the whole payload before scheduling any restore: reject, never truncate.
  array<ref EUD_DialogRecord> rows = {};
  array<UUID> ids = {};
  for (int i = 0; i < count; i++)
  {
   EUD_DialogRecord row = new EUD_DialogRecord();
   if (!context.StartObject("unit" + i.ToString()) || !row.Read(context) || !context.EndObject()) return false;
   if (row.id.IsNull() || ids.Contains(row.id) || !row.Valid()) return false;
   ids.Insert(row.id);
   rows.Insert(row);
  }
  PersistenceSystem system = GetSystem();
  if (!system) return false;
  foreach (EUD_DialogRecord pending : rows)
  {
   PersistenceWhenAvailableTask task(EUD_OnUnitAvailable, pending);
   system.WhenAvailable(pending.id, task, AVAILABLE_WAIT);
  }
  return true;
 }

 protected static void EUD_OnUnitAvailable(Managed instance, PersistenceDeferredDeserializeTask task, bool expired, Managed context)
 {
  EUD_DialogRecord row = EUD_DialogRecord.Cast(context);
  if (!row) return;
  IEntity entity = IEntity.Cast(instance);
  // A unit deleted or killed and removed before the save never returns.
  if (!entity)
  {
   Print("[EUD] Saved dialog unit did not return after load; row dropped", LogLevel.WARNING);
   return;
  }
  SCR_EditableCharacterComponent unit = EUD_Dialog.Find(entity);
  if (!unit || !unit.EUD_RestoreState(row.name, row.lines, row.gesture))
   Print("[EUD] Saved dialog could not be restored onto its unit", LogLevel.ERROR);
 }
}
