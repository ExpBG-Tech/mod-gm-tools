// EXPBG AI Surrender: optional ACE Captives adapter (server only).
// ACE is never a dependency: it is not in the project file, and no ACE class, method,
// field or enum is named in compiled code. Every ACE name below is a string resolved
// at runtime, so without ACE the pack compiles and behaves exactly as before: the
// first type lookup fails, the mode is "vanilla" and nothing calls into ACE.
//
// With ACE Captives loaded a prisoner takes ACE's own surrender state through ACE's
// public controller method ACE_Captives_SetSurrender(bool), the call ACE's server-side
// GM action "Toggle surrender" makes. ACE spawns its surrender helper (hands-up pose),
// sets its replicated surrendered flag (late joiners included), and its own Take
// prisoner / Escort / Release actions work on him.
//
// Mechanism: a blocking dynamic call, GetGame().GetScriptModule().Call(instance,
// "<method>", false, result, args...), on the character controller instance.
// Fallbacks: reads use reflection on ACE's replicated member; the setter spawns ACE's
// "Toggle surrender" context action by type name and performs it through its vanilla
// base class SCR_SelectedEntitiesContextAction. If both fail, one warning is logged and
// the prisoner keeps the vanilla sit.
class ESR_AceCaptives
{
 static const string TYPE_SYSTEM = "ACE_Captives_CaptiveSystem";
 static const string TYPE_HELPER = "ACE_AnimationHelperCompartment";
 static const string TYPE_TOGGLE = "ACE_Captives_ToggleSurrenderContextAction";
 static const string FN_SET = "ACE_Captives_SetSurrender";
 static const string FN_SURRENDERED = "ACE_Captives_HasSurrendered";
 static const string FN_CAPTIVE = "ACE_Captives_IsCaptive";
 static const string FN_CARRIED = "ACE_IsCarried";
 static const string FN_GETTING_IN = "ACE_IsGettingIn";
 static const string FIELD_SURRENDERED = "m_bACE_Captives_HasSurrendered";
 static const string FIELD_CAPTIVE = "m_bACE_Captives_IsCaptive";
 static const string FIELD_CARRIED = "m_bACE_IsCarried";
 static const ResourceName HELPER_PREFAB = "{56E6CF17253A28E1}Prefabs/Helpers/ACE_Captives_SurrenderHelperCompartment.et";
 // ACE Captives (development build) addon GUID, for the log line only: stable and
 // development ACE use different GUIDs, so detection is by type.
 static const string ADDON_CAPTIVES_DEV = "65AD7C249E4ECDFB";

 static const string MODE_VANILLA = "vanilla";
 static const string MODE_ACE = "ace";

 protected static BaseWorld s_World;
 protected static bool s_bResolved;
 protected static bool s_bAvailable;
 protected static bool s_bWarned;
 protected static int s_iCalls;

 //------------------------------------------------------------------------------------------------
 // Detection (once per world)
 //------------------------------------------------------------------------------------------------
 // Statics survive a world change: every new world is probed again.
 protected static void CheckWorld()
 {
  if (!GetGame()) return;
  BaseWorld current = GetGame().GetWorld();
  if (current == s_World) return;
  s_World = current;
  s_bResolved = false;
  s_bAvailable = false;
  s_bWarned = false;
  s_iCalls = 0;
 }

 // Server only: ACE Captives' surrender can be driven in this world. The first call
 // per world logs which surrender mode is active.
 static bool Available()
 {
  if (!GetGame() || !Replication.IsServer()) return false;
  CheckWorld();
  if (!s_bResolved) Resolve();
  return s_bAvailable;
 }

 static string Mode()
 {
  if (Available()) return MODE_ACE;
  return MODE_VANILLA;
 }

 // Test and diagnostics: ACE accesses made in this world (zero without ACE).
 static int CallCount()
 {
  CheckWorld();
  return s_iCalls;
 }

 protected static typename TypeOf(string typeName)
 {
  return typeName.ToType();
 }

 protected static void Resolve()
 {
  if (!GetGame().GetWorld()) return;
  s_bResolved = true;
  s_bAvailable = false;
  typename helperType = TypeOf(TYPE_HELPER);
  typename systemType = TypeOf(TYPE_SYSTEM);
  if (!helperType || !systemType)
  {
   Print("[EXPBG SURRENDER] ACE Captives not loaded: using vanilla surrender (sit on ground)");
   return;
  }
  // Keep the loaded resource in a local.
  Resource helperPrefab = Resource.Load(HELPER_PREFAB);
  if (!helperPrefab || !helperPrefab.IsValid())
  {
   Print("[EXPBG SURRENDER] ACE Captives types found but its surrender helper prefab is missing: using vanilla surrender (sit on ground)", LogLevel.WARNING);
   return;
  }
  s_bAvailable = true;
  // Reported only: surrender does not need ACE's captive system, ACE's Take prisoner does.
  bool captiveSystem = GetGame().GetWorld().FindSystem(systemType) != null;
  array<string> addons = {};
  GameProject.GetLoadedAddons(addons);
  bool devAddon = addons.Contains(ADDON_CAPTIVES_DEV);
  PrintFormat("[EXPBG SURRENDER] ACE Captives detected: using ACE surrender (captiveSystem=%1 devAddon=%2)", captiveSystem, devAddon);
 }

 //------------------------------------------------------------------------------------------------
 // Dynamic access
 //------------------------------------------------------------------------------------------------
 protected static void Warn(string text)
 {
  if (s_bWarned) return;
  s_bWarned = true;
  Print("[EXPBG SURRENDER] ACE Captives call failed (" + text + "): affected prisoners keep the vanilla sit", LogLevel.WARNING);
 }

 // Blocking dynamic call of a bool method on a script instance.
 protected static bool CallBool(Class target, string functionName, out bool value)
 {
  value = false;
  if (!target) return false;
  ScriptModule scripts = GetGame().GetScriptModule();
  if (!scripts) return false;
  s_iCalls++;
  bool result;
  if (!scripts.Call(target, functionName, false, result)) return false;
  value = result;
  return true;
 }

 // Reflection read of a bool member, for an ACE build whose method is not callable.
 protected static bool FieldBool(Class target, string fieldName, out bool value)
 {
  value = false;
  if (!target) return false;
  typename targetType = target.Type();
  int count = targetType.GetVariableCount();
  for (int i = 0; i < count; i++)
  {
   if (targetType.GetVariableName(i) != fieldName) continue;
   s_iCalls++;
   bool result;
   if (!targetType.GetVariableValue(target, i, result)) return false;
   value = result;
   return true;
  }
  return false;
 }

 protected static bool ReadBool(Class target, string functionName, string fieldName, out bool value)
 {
  value = false;
  if (!target) return false;
  if (CallBool(target, functionName, value)) return true;
  if (FieldBool(target, fieldName, value)) return true;
  Warn("read " + functionName);
  return false;
 }

 protected static Class ControllerOf(IEntity character)
 {
  SCR_ChimeraCharacter chimera = SCR_ChimeraCharacter.Cast(character);
  if (!chimera) return null;
  return chimera.GetCharacterController();
 }

 //------------------------------------------------------------------------------------------------
 // State (server)
 //------------------------------------------------------------------------------------------------
 // ACE's state of one character. False when ACE is absent or its state is unreadable.
 static bool ReadState(IEntity character, out bool surrendered, out bool captive, out bool carried)
 {
  surrendered = false;
  captive = false;
  carried = false;
  if (!character || !Available()) return false;
  Class controller = ControllerOf(character);
  if (!controller) return false;
  if (!ReadBool(controller, FN_SURRENDERED, FIELD_SURRENDERED, surrendered)) return false;
  ReadBool(controller, FN_CAPTIVE, FIELD_CAPTIVE, captive);
  ReadBool(controller, FN_CARRIED, FIELD_CARRIED, carried);
  return true;
 }

 // He occupies one of ACE's animation helpers (surrender, tied, carried, ...).
 static bool InHelper(IEntity character)
 {
  if (!character || !Available()) return false;
  IEntity parent = character.GetParent();
  if (!parent) return false;
  typename helperType = TypeOf(TYPE_HELPER);
  if (!helperType) return false;
  return parent.IsInherited(helperType);
 }

 // Vanilla compartment entry, or ACE's helper request (pending before entry starts).
 static bool IsGettingIn(IEntity character)
 {
  SCR_ChimeraCharacter chimera = SCR_ChimeraCharacter.Cast(character);
  if (!chimera) return false;
  CompartmentAccessComponent access = chimera.GetCompartmentAccessComponent();
  if (!access) return false;
  if (access.IsGettingIn()) return true;
  if (!Available()) return false;
  bool pending;
  CallBool(access, FN_GETTING_IN, pending);
  return pending;
 }

 static bool IsGettingOut(IEntity character)
 {
  SCR_ChimeraCharacter chimera = SCR_ChimeraCharacter.Cast(character);
  if (!chimera) return false;
  CompartmentAccessComponent access = chimera.GetCompartmentAccessComponent();
  if (!access) return false;
  return access.IsGettingOut();
 }

 //------------------------------------------------------------------------------------------------
 // Surrender (server)
 //------------------------------------------------------------------------------------------------
 // ACE's own surrender (true) or its end (false). Returns whether ACE's surrendered flag
 // now matches; the pose follows once the character has moved into ACE's helper.
 static bool SetSurrender(SCR_ChimeraCharacter character, bool surrender)
 {
  if (!character || !Replication.IsServer() || !Available()) return false;
  CharacterControllerComponent controller = character.GetCharacterController();
  ScriptModule scripts = GetGame().GetScriptModule();
  if (!controller || !scripts) return false;
  s_iCalls++;
  // ACE's method returns nothing; the slot only satisfies the call signature.
  int unused;
  bool requested = surrender;
  bool called = scripts.Call(controller, FN_SET, false, unused, requested);
  // Judged by ACE's flag (ACE sets it synchronously), not by the call result alone:
  // the call may have run although it reported failure; never toggle past the target.
  bool applied = StateIs(character, surrender);
  bool toggled;
  if (!applied)
  {
   toggled = ToggleByAction(character, surrender);
   applied = toggled && StateIs(character, surrender);
  }
  ESR_SurrenderManager.Trace(string.Format("ACE %1(%2) on %3: call=%4 action=%5 applied=%6", FN_SET, surrender, character, called, toggled, applied));
  if (!applied) Warn(FN_SET);
  return applied;
 }

 protected static bool StateIs(SCR_ChimeraCharacter character, bool surrender)
 {
  bool surrendered, captive, carried;
  if (!ReadState(character, surrendered, captive, carried)) return false;
  return surrendered == surrender;
 }

 // Fallback: ACE's own GM "Toggle surrender" context action, spawned by type name and
 // performed through its vanilla base class. It toggles, so the state is read first.
 protected static bool ToggleByAction(SCR_ChimeraCharacter character, bool surrender)
 {
  bool surrendered, captive, carried;
  if (!ReadState(character, surrendered, captive, carried)) return false;
  if (surrendered == surrender) return true;
  typename actionType = TypeOf(TYPE_TOGGLE);
  if (!actionType || !actionType.IsInherited(SCR_SelectedEntitiesContextAction)) return false;
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.GetEditableEntity(character);
  if (!editable) return false;
  // Keep the spawned instance in a local before casting.
  Managed spawned = actionType.Spawn();
  SCR_SelectedEntitiesContextAction toggleAction = SCR_SelectedEntitiesContextAction.Cast(spawned);
  if (!toggleAction || !toggleAction.CanBePerformed(editable, vector.Zero, 0)) return false;
  s_iCalls++;
  toggleAction.Perform(editable, vector.Zero);
  return true;
 }
}
