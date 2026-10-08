// Native (vanilla) mission save support for EXPBG Unit Scripts. One world record
// lists every scripted soldier by his persistence UUID with his EUS_UnitState row;
// native persistence restores the characters, and each row is queued with the
// manager once its character is available again (EUS_UnitState.Restore binds it
// when his AI is ready). No native entity or component serializer is overridden.
// Registered in Configs/Systems/Persistence/GameMode/GameMaster.conf. Night
// discipline stays mission-only. An unreadable record never fails the whole load.
[BaseContainerProps()]
class EUS_ScriptPersistenceState : PersistentState
{
}

// One saved row: the soldier's persistence id and his script.
class EUS_PersistedScript
{
 UUID Id = UUID.NULL_UUID;
 ref EUS_UnitState State;
}

class EUS_ScriptPersistenceSerializer : ScriptedStateSerializer
{
 static const int VERSION = 1;
 // Seconds a saved row waits for its character after the initial load setup.
 static const float AVAILABLE_WAIT = 120;

 override static typename GetTargetType()
 {
  return EUS_ScriptPersistenceState;
 }

 override static EDeserializeFailHandling GetDeserializeFailHandling()
 {
  return EDeserializeFailHandling.IGNORE;
 }

 override protected ESerializeResult Serialize(notnull Managed instance, notnull SaveContext context)
 {
  EUS_Manager manager = EUS_Manager.Current();
  PersistenceSystem system = GetSystem();
  array<EUS_UnitControl> controls = {};
  if (!manager || !system || manager.GetControls(controls) == 0)
  {
   return ESerializeResult.DEFAULT;
  }
  array<ref EUS_PersistedScript> rows = {};
  int untracked;
  foreach (EUS_UnitControl control : controls)
  {
   EUS_UnitState state = EUS_UnitState.Capture(control.GetActor());
   if (!state || !state.Validate().IsEmpty()) continue;
   EUS_PersistedScript row = new EUS_PersistedScript();
   row.Id = system.GetId(control.GetActor());
   row.State = state;
   if (row.Id.IsNull())
   {
    untracked++;
    continue;
   }
   rows.Insert(row);
  }
  if (rows.IsEmpty())
  {
   return ESerializeResult.DEFAULT;
  }
  if (!context.WriteValue("eusVersion", VERSION) || !context.WriteValue("count", rows.Count()))
  {
   return ESerializeResult.ERROR;
  }
  foreach (int index, EUS_PersistedScript saved : rows)
  {
   if (!context.StartObject("unit" + index.ToString()) || !context.WriteValue("id", saved.Id) || !saved.State.Write(context) || !context.EndObject())
   {
    return ESerializeResult.ERROR;
   }
  }
  string line = string.Format("saved %1 unit script(s) in the native save", rows.Count());
  if (untracked > 0) line += string.Format("; %1 scripted soldier(s) without a persistence id were left out (not saved by the game)", untracked);
  EUS_UnitControl.Log(line);
  return ESerializeResult.OK;
 }

 override protected bool Deserialize(notnull Managed instance, notnull LoadContext context)
 {
  int version;
  int count;
  if (!context.ReadValue("eusVersion", version) || version != VERSION)
  {
   return false;
  }
  if (!context.ReadValue("count", count) || count < 0 || count > EUS_Manager.RESTORE_LIMIT)
  {
   return false;
  }
  // The whole record is validated before any restore is scheduled: reject, never truncate.
  array<ref EUS_PersistedScript> rows = {};
  array<UUID> ids = {};
  for (int i = 0; i < count; i++)
  {
   EUS_PersistedScript row = new EUS_PersistedScript();
   row.State = new EUS_UnitState();
   if (!context.StartObject("unit" + i.ToString()) || !context.ReadValue("id", row.Id) || !row.State.Read(context) || !context.EndObject())
   {
    return false;
   }
   if (row.Id.IsNull() || ids.Contains(row.Id) || !row.State.Validate().IsEmpty())
   {
    return false;
   }
   ids.Insert(row.Id);
   rows.Insert(row);
  }
  PersistenceSystem system = GetSystem();
  if (!system)
  {
   return false;
  }
  foreach (EUS_PersistedScript pending : rows)
  {
   PersistenceWhenAvailableTask task(EUS_OnUnitAvailable, pending);
   system.WhenAvailable(pending.Id, task, AVAILABLE_WAIT);
  }
  return true;
 }

 protected static void EUS_OnUnitAvailable(Managed instance, PersistenceDeferredDeserializeTask task, bool expired, Managed context)
 {
  EUS_PersistedScript row = EUS_PersistedScript.Cast(context);
  if (!row || !row.State)
  {
   return;
  }
  string reason;
  IEntity entity = IEntity.Cast(instance);
  // A soldier deleted or killed and removed before the save never returns.
  if (!entity)
  {
   reason = "the soldier did not return after the load";
  }
  else if (row.State.Restore(entity, "native save", reason))
  {
   return;
  }
  EUS_UnitControl.Log(string.Format("saved unit script %1 not restored: %2", row.State.Describe(), reason));
 }
}
