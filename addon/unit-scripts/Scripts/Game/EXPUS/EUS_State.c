// Versioned state API of EXPBG Unit Scripts for persistence (native mission saves in
// EUS_Persistence.c, the CDF bridge in EXPBG CDF Compat). Server only.
//
// One row is the running script of one AI soldier: the script code (EUS_Codes:
// HOLD, FREEZE or ANIMATION + catalog index, which is the animation and its
// variant), the held spot and the held heading. Nothing else is needed to resume
// it: the AI settings, look, perception and pose are rebuilt by the bind.
//
// Export: EUS_UnitState.Capture(entity), then Encode() (JSON text) or Write(context).
// Import: EUS_UnitState.Decode(text, reason) or Read(context), then Restore(entity,
// source, reason). Restore validates, then queues the row with the manager, which
// binds the script once the character's AI is ready (bounded wait, budgeted pump):
// the soldier is put back on the saved spot and heading when the save system put
// him back within RESTORE_RADIUS of it (an animated soldier is saved where the
// pose carried him, e.g. seated), otherwise he keeps where he stands.
//
// JSON payload, version 1 (keys stay as written; a new field needs a new version):
// {"version":1,"code":2,"ax":0,"ay":0,"az":0,"fx":0,"fz":1}
class EUS_UnitState
{
 static const int VERSION = 1;
 // Farthest the saved spot may lie from where the save put him back (metres).
 static const float RESTORE_RADIUS = 3;
 // Coordinates beyond this are not a world position (also catches NaN).
 static const float WORLD_LIMIT = 1000000;

 int Code;
 vector Anchor;
 // Horizontal unit vector.
 vector Forward;

 // The running script of this soldier, or null.
 static EUS_UnitState Capture(IEntity entity)
 {
  SCR_ChimeraCharacter actor = SCR_ChimeraCharacter.Cast(entity);
  if (!actor || !EUS_Codes.IsScript(actor.EUS_Script))
  {
   return null;
  }
  EUS_Manager manager = EUS_Manager.Current();
  if (!manager)
  {
   return null;
  }
  EUS_UnitControl control = manager.FindControl(actor);
  if (!control)
  {
   return null;
  }
  EUS_UnitState state = new EUS_UnitState();
  state.Code = control.GetCode();
  state.Anchor = control.GetAnchor();
  state.Forward = control.GetForward();
  return state;
 }

 // Empty when the row can be restored, otherwise why not.
 string Validate()
 {
  if (!EUS_Codes.IsScript(Code))
  {
   return string.Format("unknown unit script code %1", Code);
  }
  for (int axis = 0; axis < 3; axis++)
  {
   if (!(Anchor[axis] > -WORLD_LIMIT && Anchor[axis] < WORLD_LIMIT))
   {
    return "the saved spot is not a world position";
   }
  }
  float length = Forward[0] * Forward[0] + Forward[2] * Forward[2];
  if (!(length > 0.81 && length < 1.21) || !(Forward[1] > -0.01 && Forward[1] < 0.01))
  {
   return "the saved heading is not a horizontal direction";
  }
  return string.Empty;
 }

 string Describe()
 {
  return string.Format("'%1' at %2", EUS_Codes.Describe(Code), Anchor);
 }

 // JSON payload (see the class comment); empty on failure.
 string Encode()
 {
  JsonSaveContext context = new JsonSaveContext();
  if (!Write(context))
  {
   return string.Empty;
  }
  return context.SaveToString();
 }

 // Null with the reason when the payload is not a valid version-1 row.
 static EUS_UnitState Decode(string payload, out string reason)
 {
  JsonLoadContext context = new JsonLoadContext();
  if (payload.IsEmpty() || !context.LoadFromString(payload))
  {
   reason = "the unit script payload is not JSON";
   return null;
  }
  EUS_UnitState state = new EUS_UnitState();
  if (!state.Read(context))
  {
   reason = "the unit script payload has another version or misses a field";
   return null;
  }
  reason = state.Validate();
  if (!reason.IsEmpty())
  {
   return null;
  }
  return state;
 }

 // Native and JSON serialization share the field names.
 bool Write(SaveContext context)
 {
  if (!context.WriteValue("version", VERSION) || !context.WriteValue("code", Code))
  {
   return false;
  }
  if (!context.WriteValue("ax", Anchor[0]) || !context.WriteValue("ay", Anchor[1]) || !context.WriteValue("az", Anchor[2]))
  {
   return false;
  }
  return context.WriteValue("fx", Forward[0]) && context.WriteValue("fz", Forward[2]);
 }

 bool Read(LoadContext context)
 {
  int version;
  if (!context.ReadValue("version", version) || version != VERSION)
  {
   return false;
  }
  float ax;
  float ay;
  float az;
  float fx;
  float fz;
  if (!context.ReadValue("code", Code) || !context.ReadValue("ax", ax) || !context.ReadValue("ay", ay) || !context.ReadValue("az", az))
  {
   return false;
  }
  if (!context.ReadValue("fx", fx) || !context.ReadValue("fz", fz))
  {
   return false;
  }
  Anchor = Vector(ax, ay, az);
  Forward = Vector(fx, 0, fz);
  return true;
 }

 // Server: queues this row onto the restored character (see the class comment).
 // False with the reason when it is refused at once; a refusal later (the AI never
 // became ready, no room for the pose) is logged by the manager. source names the
 // save system in the logs ("native save", "CDF save").
 bool Restore(IEntity entity, string source, out string reason)
 {
  reason = Validate();
  if (!reason.IsEmpty())
  {
   return false;
  }
  SCR_ChimeraCharacter actor = SCR_ChimeraCharacter.Cast(entity);
  if (!actor)
  {
   reason = "the saved unit script is not on a character";
   return false;
  }
  EUS_Manager manager = EUS_Manager.Get();
  if (!manager)
  {
   reason = "unit scripts run on the server in play mode only";
   return false;
  }
  return manager.QueueRestore(actor, this, source, reason);
 }
}
