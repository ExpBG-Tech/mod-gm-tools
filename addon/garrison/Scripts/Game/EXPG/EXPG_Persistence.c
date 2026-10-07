// Garrison persistence: the public API seam for save carriers (the native state
// below and the EXPBG CDF Compat bridge) and the persistence mode. Server only.
//
// Modes, read from the loaded addons (EXPG_GarrisonManager.PersistenceMode):
//  - Native: CDF Game Master Save is not loaded. Owned entities leave native
//    tracking; the native ledger carries them. Full caching allowed.
//  - CDF bridged: CDF is loaded with a bridge whose CdfBridgeVersion() equals
//    BRIDGE_API. Owned entities are also NON_SERIALIZABLE for CDF and the bridge
//    writes the same ledger into the CDF document. Full caching allowed.
//  - CDF legacy: CDF is loaded without a bridge (or an older one): 0.1.8 rules for
//    CDF (Full runs as Simulation, CDF capture refused while a garrison is active,
//    Unit Caching Prepare for Save releases garrisons). The native ledger still works.
//
// Save: SyncSaveExclusion, then Export (CanExport first for CDF). Nothing is spawned
// or materialized to save. Load: BeginImport (Add Garrison refused meanwhile), the
// carrier's own restore, DiscardForImport when it replaced the scene, QueueImport,
// then FinishImport once the carrier's world is final. A loaded garrison is created
// Full cached with no soldier in the world, waits for its building's analysis, takes
// its posts back by position and wakes with AI pinned until every guard is bound.
class EXPG_GarrisonPersistence
{
 static const int BRIDGE_API = 1;
 static const int MODE_NATIVE = 0;
 static const int MODE_CDF_BRIDGED = 1;
 static const int MODE_CDF_LEGACY = 2;

 // Native carrier: the ledger read by the serializer, imported once native
 // persistence is active (EXPG_GarrisonManager.ServiceNativeImport).
 protected static ref array<ref EXPG_GarrisonSnapshot> s_NativeLedger;
 protected static string s_NativeProblem;
 protected static BaseWorld s_NativeWorld;
 protected static int s_KickTries;

 protected static EXPG_GarrisonManager Manager()
 {
  if (!Replication.IsServer()) { return null; }
  return EXPG_GarrisonManager.Get();
 }

 static string ModeName(int mode)
 {
  if (mode == MODE_CDF_BRIDGED) { return "CDF bridged"; }
  if (mode == MODE_CDF_LEGACY) { return "CDF legacy (no EXPBG CDF Compat bridge)"; }
  return "native";
 }

 static int Mode()
 {
  EXPG_GarrisonManager manager = Manager();
  if (!manager) { return MODE_NATIVE; }
  return manager.PersistenceMode();
 }

 static bool Legacy()
 {
  return Mode() == MODE_CDF_LEGACY;
 }

 // CDF semantics: false while a load replaces the garrisons, and in legacy mode
 // while any garrison is active (the 0.1.8 refusal).
 static bool CanExport(out string reason)
 {
  reason = "";
  EXPG_GarrisonManager manager = Manager();
  if (!manager) { return true; }
  return manager.LedgerAllowed(true, reason);
 }

 // Every Ready garrison and every queued import, in record order. False (with the
 // reason) refuses the save; nothing in the world is changed either way.
 static bool Export(notnull array<ref EXPG_GarrisonSnapshot> ledger, out string reason)
 {
  reason = "";
  EXPG_GarrisonManager manager = Manager();
  if (!manager) { return true; }
  return manager.ExportLedger(ledger, reason);
 }

 // CDF carrier: CanExport, Export and the JSON text in one call. Empty json: no garrison.
 static bool ExportJson(out string json, out string reason)
 {
  json = "";
  if (!CanExport(reason)) { return false; }
  SyncSaveExclusion();
  array<ref EXPG_GarrisonSnapshot> ledger = {};
  if (!Export(ledger, reason)) { return false; }
  if (ledger.IsEmpty()) { return true; }
  return EXPG_Ledger.ToJson(ledger, json, reason);
 }

 // True for a garrison-owned entity the ledger saves (squad, waypoint, living guard).
 static bool OwnsForSave(IEntity entity)
 {
  if (!entity || !Replication.IsServer()) { return false; }
  return EXPG_SaveExclusion.Owns(entity);
 }

 static void SyncSaveExclusion()
 {
  EXPG_GarrisonManager manager = Manager();
  if (manager) { manager.SyncExclusion(); }
 }

 // A soldier or squad the garrison spawned for a restore: excluded in the same call.
 static void KeepNewborn(IEntity entity)
 {
  EXPG_GarrisonManager manager = Manager();
  if (manager) { manager.KeepOwned(entity); }
 }

 static bool Importing()
 {
  EXPG_GarrisonManager manager = Manager();
  return manager && manager.IsImporting();
 }

 static bool BeginImport(out string reason)
 {
  reason = "Garrisons are only loaded on the server during play";
  EXPG_GarrisonManager manager = Manager();
  if (!manager) { return false; }
  return manager.StartImport(reason);
 }

 // The carrier replaced the scene: forget every garrison of the old one, without
 // waking or respawning anyone.
 static void DiscardForImport(string why)
 {
  EXPG_GarrisonManager manager = Manager();
  if (manager) { manager.DiscardAll(why); }
 }

 static bool QueueImport(array<ref EXPG_GarrisonSnapshot> ledger, out string reason)
 {
  reason = "Garrisons are only loaded on the server during play";
  EXPG_GarrisonManager manager = Manager();
  if (!manager) { return false; }
  return manager.Queue(ledger, reason);
 }

 static void FinishImport()
 {
  EXPG_GarrisonManager manager = Manager();
  if (manager) { manager.Materialize(); }
 }

 // Ends a load that queued nothing (no ledger, or the carrier refused before mutating).
 static void EndImport()
 {
  EXPG_GarrisonManager manager = Manager();
  if (manager) { manager.StopImport(); }
 }

 static bool WriteLedger(SaveContext context, array<ref EXPG_GarrisonSnapshot> ledger)
 {
  return EXPG_Ledger.Write(context, ledger);
 }

 static bool ReadLedger(LoadContext context, array<ref EXPG_GarrisonSnapshot> ledger, out string reason)
 {
  return EXPG_Ledger.Read(context, ledger, reason);
 }

 static bool ValidateLedger(array<ref EXPG_GarrisonSnapshot> ledger, out string reason)
 {
  return EXPG_Ledger.Validate(ledger, reason);
 }

 static bool ParseJson(string json, array<ref EXPG_GarrisonSnapshot> ledger, out string reason)
 {
  return EXPG_Ledger.FromJson(json, ledger, reason);
 }

 // Every Game Master (now and for the next ten minutes) sees this once.
 static void Notify(string message)
 {
  EXPG_GarrisonNotice.Post(message);
 }

 //------------------------------------------------------------------------------------------------
 // Native carrier
 //------------------------------------------------------------------------------------------------
 static ESerializeResult SerializeNative(SaveContext context)
 {
  if (!Replication.IsServer()) { return ESerializeResult.DEFAULT; }
  string reason;
  EXPG_GarrisonManager manager = Manager();
  array<ref EXPG_GarrisonSnapshot> ledger = {};
  if (manager)
  {
   manager.SyncExclusion();
   if (!manager.LedgerAllowed(false, reason) || !manager.ExportLedger(ledger, reason))
   {
    Print("[EXPG SAVE] Native save refused by the garrison ledger: " + reason, LogLevel.ERROR);
    return ESerializeResult.ERROR;
   }
  }
  else if (s_NativeLedger && s_NativeWorld == GetGame().GetWorld())
  {
   // Loaded but not imported yet: written back verbatim.
   foreach (EXPG_GarrisonSnapshot pending : s_NativeLedger) { ledger.Insert(pending); }
  }
  if (ledger.IsEmpty()) { return ESerializeResult.DEFAULT; }
  if (!EXPG_Ledger.Write(context, ledger))
  {
   Print("[EXPG SAVE] Native save refused: the garrison ledger could not be written", LogLevel.ERROR);
   return ESerializeResult.ERROR;
  }
  PrintFormat("[EXPG SAVE] native ledger garrisons=%1 members=%2 alive=%3", ledger.Count(), EXPG_Ledger.CountMembers(ledger, false), EXPG_Ledger.CountMembers(ledger, true));
  return ESerializeResult.OK;
 }

 // A ledger that cannot be read never fails the whole native load: the rest of the
 // save loads, the garrisons do not, and every Game Master is told why.
 static bool DeserializeNative(LoadContext context)
 {
  if (!GetGame()) { return false; }
  s_NativeWorld = GetGame().GetWorld();
  s_NativeProblem = "";
  s_NativeLedger = {};
  string reason;
  if (!EXPG_Ledger.Read(context, s_NativeLedger, reason))
  {
   s_NativeLedger = null;
   s_NativeProblem = reason;
   Print("[EXPG LOAD] Native garrison ledger unreadable: " + reason, LogLevel.ERROR);
  }
  else { PrintFormat("[EXPG LOAD] native ledger read: garrisons=%1 alive=%2", s_NativeLedger.Count(), EXPG_Ledger.CountMembers(s_NativeLedger, true)); }
  // The manager may not exist yet; it imports once native persistence is active.
  s_KickTries = 0;
  if (GetGame().GetCallqueue())
  {
   GetGame().GetCallqueue().Remove(KickNativeImport);
   GetGame().GetCallqueue().CallLater(KickNativeImport, 500, true);
  }
  return true;
 }

 protected static void KickNativeImport()
 {
  s_KickTries++;
  EXPG_GarrisonManager manager;
  if (GetGame() && GetGame().InPlayMode()) { manager = Manager(); }
  if ((manager && manager.NativeImportChecked()) || s_KickTries > 600)
  {
   if (GetGame()) { GetGame().GetCallqueue().Remove(KickNativeImport); }
  }
 }

 // Once per world: the native ledger (null when none) and why it could not be read.
 static array<ref EXPG_GarrisonSnapshot> TakeNativeLedger(out string problem)
 {
  problem = "";
  if (!GetGame() || s_NativeWorld != GetGame().GetWorld())
  {
   s_NativeLedger = null;
   s_NativeProblem = "";
   return null;
  }
  problem = s_NativeProblem;
  array<ref EXPG_GarrisonSnapshot> ledger = s_NativeLedger;
  s_NativeLedger = null;
  s_NativeProblem = "";
  return ledger;
 }
}

// Native mission save of the garrison ledger (EXPG_Snapshot.c). Registered in
// Configs/Systems/Persistence/GameMode/GameMaster.conf. Writes nothing (DEFAULT)
// while no garrison exists.
class EXPG_GarrisonPersistenceState : PersistentState
{
 [NonSerialized()]
 int SerializeCalls;
 [NonSerialized()]
 int DeserializeCalls;

 static EXPG_GarrisonPersistenceState Get()
 {
  PersistenceSystem system = PersistenceSystem.GetInstance();
  if (!system) { return null; }
  return EXPG_GarrisonPersistenceState.Cast(system.GetPersistentState(EXPG_GarrisonPersistenceState));
 }
}

class EXPG_GarrisonPersistenceSerializer : ScriptedStateSerializer
{
 override static typename GetTargetType()
 {
  return EXPG_GarrisonPersistenceState;
 }

 override static EDeserializeFailHandling GetDeserializeFailHandling()
 {
  return EDeserializeFailHandling.IGNORE;
 }

 override protected ESerializeResult Serialize(notnull Managed instance, notnull SaveContext context)
 {
  EXPG_GarrisonPersistenceState state = EXPG_GarrisonPersistenceState.Cast(instance);
  ESerializeResult result = EXPG_GarrisonPersistence.SerializeNative(context);
  if (state && result != ESerializeResult.ERROR) { state.SerializeCalls++; }
  return result;
 }

 override protected bool Deserialize(notnull Managed instance, notnull LoadContext context)
 {
  EXPG_GarrisonPersistenceState state = EXPG_GarrisonPersistenceState.Cast(instance);
  if (state) { state.DeserializeCalls++; }
  return EXPG_GarrisonPersistence.DeserializeNative(context);
 }
}

// Load and save outcomes for Game Masters: a hint and the same line in chat history,
// delivered once to every Game Master present now or joining in the next ten minutes
// (a native load imports garrisons at mission start, before anyone has joined).
class EXPG_GarrisonNotice
{
 protected static string s_Message;
 protected static float s_Until;
 protected static ref array<int> s_Told = {};
 protected static BaseWorld s_World;

 protected static float Now()
 {
  if (!GetGame() || !GetGame().GetWorld()) { return 0; }
  return GetGame().GetWorld().GetWorldTime() * 0.001;
 }

 static void Post(string message)
 {
  if (!Replication.IsServer() || message.IsEmpty() || !GetGame()) { return; }
  Print("[EXPG LOAD NOTICE] " + message);
  s_World = GetGame().GetWorld();
  s_Message = message;
  s_Until = Now() + 600;
  s_Told.Clear();
  Deliver();
 }

 // Server; cheap when nothing is pending. Called by the garrison pump.
 static void Deliver()
 {
  if (s_Message.IsEmpty() || !GetGame()) { return; }
  if (s_World != GetGame().GetWorld() || Now() > s_Until)
  {
   s_Message = "";
   s_Told.Clear();
   return;
  }
  SCR_EditorManagerCore core = SCR_EditorManagerCore.Cast(SCR_EditorManagerCore.GetInstance(SCR_EditorManagerCore));
  if (!core) { return; }
  array<int> players = {};
  GetGame().GetPlayerManager().GetPlayers(players);
  foreach (int playerId : players)
  {
   if (s_Told.Contains(playerId)) { continue; }
   SCR_EditorManagerEntity editor = core.GetEditorManager(playerId);
   if (!editor || editor.IsLimited()) { continue; }
   editor.EXPG_Notice(s_Message);
   s_Told.Insert(playerId);
  }
 }
}

modded class SCR_EditorManagerEntity
{
 void EXPG_Notice(string message)
 {
  if (Replication.IsClient()) { return; }
  if (IsOwner())
  {
   EXPG_Feedback.Show(message);
   return;
  }
  Rpc(EXPG_NoticeOwner, message);
 }

 [RplRpc(RplChannel.Reliable, RplRcver.Owner)]
 protected void EXPG_NoticeOwner(string message)
 {
  EXPG_Feedback.Show(message);
 }
}
