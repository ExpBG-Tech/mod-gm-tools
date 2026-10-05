// Optional runtime detection of ACE animations. ACE is NEVER a dependency of
// this addon: it is not in EXPBG_Ambient_Civilians.gproj, nothing is bundled or
// extracted from it, and with ACE absent this file changes no behaviour at all.
// When a server happens to have ACE loaded, the probe may adopt an ACE-owned
// animation ONLY through the vanilla CUSTOM loitering route, by ResourceName,
// with no ACE script class referenced and no ACE prefab spawned.
//
// MEASURED RESULT (ACE 1.5.34-1.5.39, 23 addons installed on this machine,
// 2026-09-16): the candidate table below is deliberately EMPTY, because ACE
// ships nothing the vanilla CUSTOM route can drive.
//
//  * Every full-body ACE character pose - Captives Surrender/Tied, Carrying
//    Carried/Dragged, Medical CPR - lives in an .asi bound to a
//    VehicleAnimationComponent on a helper prefab derived from
//    Prefabs/Vehicles/Core/Vehicle_Cargo_Base.et. Playing one requires moving
//    the character into that compartment, which is the compartment/vehicle
//    hack this addon removed in phase 2.
//  * The only ACE-authored character-graph command is ACE_Medical_CMD_HeadTilt
//    (ACE Medical Breathing), an additive Neck1/Neck2/Neck3/Head layer for an
//    unconscious casualty. It is not a pose and not a civilian routine.
//  * ACE_ContinuousLoiterCommand overrides StartLoiter() and immediately
//    requires SCR_PlayerController, then routes back into the helper
//    compartment. It is player-driven and unreachable from an ambient AI.
//  * No ACE addon references StartLoitering, ELoiteringType,
//    SCR_LoiterCustomAnimData, m_CustomCommand or m_GraphBindingName at all.
//
// The probe ships anyway, with its table empty, so the report and the code say
// the same thing and a later ACE release that does author a loiter graph needs
// one row here and nothing else. See docs/M3-activities-runtime.md.

// One ACE-owned animation this addon is willing to drive through vanilla CUSTOM
// loitering. Nothing here is an ACE script type; every field is a plain string,
// a ResourceName or a number.
class EAC_AceCandidate
{
 string Name;
 string AddonGuid;    // owning ACE addon, checked against the loaded list
 ResourceName GraphPath;   // SCR_LoiterCustomAnimData.m_sGraphName (.agr)
 ResourceName Instance;  // SCR_LoiterCustomAnimData.m_sGraphInstanceName (.asi)
 string Command;    // bound on the character's CharacterAnimationComponent
 string Binding;    // SCR_LoiterCustomAnimData.m_sGraphBindingName
 float MinSeconds, MaxSeconds;
}

// A candidate that passed every gate, with its resolved command index. Only
// these reach the routine catalog.
class EAC_CustomAnim
{
 string Name;
 ResourceName GraphPath;
 ResourceName Instance;
 string Command;
 string Binding;
 int CommandId;    // resolved TAnimGraphCommand, never -1 in a valid entry
 float MinSeconds, MaxSeconds;
}

class EAC_AceAnimations
{
 // The vanilla defaults SCR_LoiterCustomAnimData.CreateInstance uses, repeated
 // here as plain strings so the probe never dereferences a config to learn them.
 static const string BINDING_NPC = "NPC";
 static const string BINDING_COMMAND = "CMD_Gestures";

 // Known ACE addon GUIDs, used only to answer "is ACE loaded at all" for the
 // status line. Presence of any of these grants nothing on its own: a candidate
 // still has to pass every gate below.
 static const string ACE_CORE = "65AD7D0D9941A380";
 static const string ACE_CAPTIVES = "65AD7C249E4ECDFB";
 static const string ACE_CARRYING = "65AD7C379CBD394D";
 static const string ACE_MEDICAL_CIRCULATION = "65AD7D4F994EA327";
 static const string ACE_MEDICAL_BREATHING = "671F73D99978B4F2";

 protected static BaseWorld s_World;
 protected static bool s_Resolved;
 protected static bool s_AceLoaded;
 protected static ref array<ref EAC_CustomAnim> s_Valid = {};
 protected static ref array<ref EAC_AceCandidate> s_Candidates;

 protected static void Candidate(string name, string addonGuid, ResourceName graph, ResourceName instance, string command, string binding, float minSeconds, float maxSeconds)
 {
  EAC_AceCandidate entry = new EAC_AceCandidate();
  entry.Name = name;
  entry.AddonGuid = addonGuid;
  entry.GraphPath = graph;
  entry.Instance = instance;
  entry.Command = command;
  entry.Binding = binding;
  entry.MinSeconds = minSeconds;
  entry.MaxSeconds = maxSeconds;
  s_Candidates.Insert(entry);
 }

 // The candidate table. Built once, never per world: what ACE ships does not
 // change while the game is running, only whether it is loaded.
 //
 // Empty by measurement, not by omission - see the file header. A future ACE
 // release that authors a real civilian loiter graph needs exactly one
 // Candidate(...) line here, of the shape:
 //
 //  Candidate("Name", ACE_SOME_ADDON,
 //   "{GRAPHGUID}Assets/.../Some.agr",
 //   "{INSTGUID}Assets/.../Some.asi",
 //   "CMD_Gestures", BINDING_NPC, 90, 150);
 //
 // and nothing else in this addon changes.
 protected static void BuildCandidates()
 {
  if (s_Candidates) return;
  s_Candidates = {};
  // (intentionally empty - ACE ships no asset the vanilla CUSTOM loitering
  // route can drive; every ACE pose is a compartment pose)
 }

 // Drop everything when the world is replaced, so a different server load-out
 // is probed again rather than inheriting the previous mission's answer.
 protected static void CheckWorld()
 {
  BaseWorld world = GetGame().GetWorld();
  if (world == s_World) return;
  s_World = world;
  s_Resolved = false;
  s_AceLoaded = false;
  s_Valid.Clear();
 }

 // True once Detect has actually run against a real character in this world.
 // Until then ValidCount() is zero and the catalog stays vanilla.
 static bool Resolved()
 {
  CheckWorld();
  return s_Resolved;
 }

 // Whether any known ACE addon GUID was in the loaded list. Reported for the
 // status line only; it never enables a routine by itself.
 static bool AceLoaded()
 {
  CheckWorld();
  return s_AceLoaded;
 }

 static int ValidCount()
 {
  CheckWorld();
  return s_Valid.Count();
 }

 static EAC_CustomAnim Valid(int index)
 {
  CheckWorld();
  if (index < 0) return null;
  if (index >= s_Valid.Count()) return null;
  return s_Valid[index];
 }

 protected static bool AnyAceLoaded(array<string> loaded)
 {
  if (!loaded) return false;
  if (loaded.Contains(ACE_CORE)) return true;
  if (loaded.Contains(ACE_CAPTIVES)) return true;
  if (loaded.Contains(ACE_CARRYING)) return true;
  if (loaded.Contains(ACE_MEDICAL_CIRCULATION)) return true;
  if (loaded.Contains(ACE_MEDICAL_BREATHING)) return true;
  return false;
 }

 protected static bool ResourceUsable(ResourceName path)
 {
  if (path == "") return false;
  Resource resource = Resource.Load(path);
  if (!resource) return false;
  if (!resource.IsValid()) return false;
  return true;
 }

 // Run once per world against one live character. Creates nothing, spawns
 // nothing, plays nothing and never throws: every read is hoisted to a local
 // and every test is a positive braced guard.
 static array<ref EAC_CustomAnim> Detect(IEntity sampleCharacter)
 {
  CheckWorld();
  if (s_Resolved) return s_Valid;
  if (!sampleCharacter)
  {
   EAC_RoutineStats.RecordAceProbe(EAC_RoutineStats.ACE_NOSAMPLE);
   return s_Valid;
  }
  CharacterAnimationComponent animation = CharacterAnimationComponent.Cast(sampleCharacter.FindComponent(CharacterAnimationComponent));
  if (!animation)
  {
   EAC_RoutineStats.RecordAceProbe(EAC_RoutineStats.ACE_NOANIM);
   return s_Valid;
  }

  s_Resolved = true;
  s_Valid.Clear();
  EAC_RoutineStats.RecordAceProbe(EAC_RoutineStats.ACE_PROBED);

  array<string> loaded = {};
  GameProject.GetLoadedAddons(loaded);
  s_AceLoaded = AnyAceLoaded(loaded);
  if (s_AceLoaded) EAC_RoutineStats.RecordAceProbe(EAC_RoutineStats.ACE_ADDON);

  BuildCandidates();
  if (!s_Candidates) return s_Valid;

  int total = s_Candidates.Count();
  for (int i = 0; i < total; i++)
  {
   EAC_AceCandidate candidate = s_Candidates[i];
   if (!candidate) continue;
   EAC_RoutineStats.RecordAceProbe(EAC_RoutineStats.ACE_CANDIDATE);

   string guid = candidate.AddonGuid;
   if (!loaded.Contains(guid))
   {
    EAC_RoutineStats.RecordAceProbe(EAC_RoutineStats.ACE_NOADDON);
    continue;
   }
   if (!ResourceUsable(candidate.GraphPath))
   {
    EAC_RoutineStats.RecordAceProbe(EAC_RoutineStats.ACE_NOGRAPH);
    continue;
   }
   if (!ResourceUsable(candidate.Instance))
   {
    EAC_RoutineStats.RecordAceProbe(EAC_RoutineStats.ACE_NOINSTANCE);
    continue;
   }
   string commandName = candidate.Command;
   if (commandName == "") commandName = BINDING_COMMAND;
   int commandId = animation.BindCommand(commandName);
   if (commandId < 0)
   {
    EAC_RoutineStats.RecordAceProbe(EAC_RoutineStats.ACE_UNBOUND);
    continue;
   }
   float minSeconds = candidate.MinSeconds;
   float maxSeconds = candidate.MaxSeconds;
   if (minSeconds < 10) minSeconds = 10;
   if (maxSeconds < minSeconds) maxSeconds = minSeconds;

   // A slot with no name would fail the catalog fixture's well-formed check, so
   // give it one rather than publishing a malformed routine.
   string label = candidate.Name;
   if (label == "") label = "Detected pose";

   EAC_CustomAnim accepted = new EAC_CustomAnim();
   accepted.Name = label;
   accepted.GraphPath = candidate.GraphPath;
   accepted.Instance = candidate.Instance;
   accepted.Command = commandName;
   accepted.Binding = candidate.Binding;
   if (accepted.Binding == "") accepted.Binding = BINDING_NPC;
   accepted.CommandId = commandId;
   accepted.MinSeconds = minSeconds;
   accepted.MaxSeconds = maxSeconds;
   s_Valid.Insert(accepted);
   EAC_RoutineStats.RecordAceProbe(EAC_RoutineStats.ACE_VALID);
  }
  return s_Valid;
 }

 // The vanilla payload for one accepted candidate. Built only from values the
 // probe already validated, so no unbound command and no missing graph can ever
 // reach SCR_CharacterControllerComponent.StartLoitering.
 static SCR_LoiterCustomAnimData Payload(EAC_CustomAnim anim)
 {
  if (!anim) return null;
  if (anim.CommandId < 0) return null;
  return SCR_LoiterCustomAnimData.CreateInstance(anim.CommandId, -1, anim.Command, anim.Binding, anim.GraphPath, anim.Instance);
 }

 // Short status fragment for the GM debug HUD.
 static string Status()
 {
  CheckWorld();
  int poses = s_Valid.Count();
  if (poses > 0)
  {
   string enabled = "vanilla+ace(";
   enabled += poses.ToString();
   enabled += " poses)";
   return enabled;
  }
  return "vanilla";
 }
}
