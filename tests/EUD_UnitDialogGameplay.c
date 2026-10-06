// TEST ONLY. EXPBG Unit Dialog native server fixture.
// pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -FixturePath tests/EUD_UnitDialogGameplay.c -OrchestratorSlotGranted
// The runner copies this file to EXPG_GarrisonGameplay.c; the class names are fixed.
// Until Run-Gameplay.ps1 gains a -UnitDialog evidence switch, its garrison evidence
// check reports FAIL; judge this run by the [EXPG UNIT DIALOG RESULT] line instead.
// Real vanilla riflemen, production state/validation/registry/attribute/codec code,
// the Character_Base action entry, native gesture start and native Kill. No players,
// no GM UI, no conversation window, no multiplayer/JIP and no save/load round trip.
class EXPG_GarrisonGameplayClass : GenericEntityClass {}
class EXPG_GarrisonGameplay : GenericEntity
{
 static const float FIXTURE_SECONDS = 120;
 static const ResourceName RIFLEMAN = "{26A9756790131354}Prefabs/Characters/Factions/BLUFOR/US_Army/Character_US_Rifleman.et";
 int Checks;
 int Failures;
 int Phase;
 float Started;
 float Next;
 float PhaseAt;
 bool Finished;
 bool GesturePlayed;
 EntityID SpeakerId;
 EntityID ListenerId;
 vector Origin = "4773.46 0 7094.57";

 void EXPG_GarrisonGameplay(IEntitySource src, IEntity parent) { SetEventMask(EntityEvent.INIT | EntityEvent.FRAME); }
 float Now() { return GetGame().GetWorld().GetWorldTime() * 0.001; }
 bool Check(bool ok, string label)
 {
  Checks++;
  if (!ok) Failures++;
  PrintFormat("[EXPG UNIT DIALOG CHECK] pass=%1 %2", ok, label);
  return ok;
 }
 void Finish(string reason)
 {
  if (Finished) return;
  Finished = true;
  ClearEventMask(EntityEvent.FRAME);
  PrintFormat("[EXPG UNIT DIALOG RESULT] checks=%1 failures=%2 reason=%3", Checks, Failures, reason);
  GetGame().RequestClose();
 }
 vector Ground(vector p, float lift) { p[1] = GetGame().GetWorld().GetSurfaceY(p[0], p[2]) + lift; return p; }
 IEntity Spawn(vector at)
 {
  EntitySpawnParams spawn = new EntitySpawnParams();
  spawn.TransformMode = ETransformMode.WORLD;
  Math3D.AnglesToMatrix(vector.Zero, spawn.Transform);
  spawn.Transform[3] = Ground(at, 0.2);
  // Keep the Resource and the spawned entity in locals (inline form returned null natively).
  Resource prefab = Resource.Load(RIFLEMAN);
  IEntity spawned = GetGame().SpawnEntityPrefab(prefab, GetGame().GetWorld(), spawn);
  return spawned;
 }
 IEntity Speaker() { return GetGame().GetWorld().FindEntityByID(SpeakerId); }
 IEntity Listener() { return GetGame().GetWorld().FindEntityByID(ListenerId); }
 string Repeat(string unit, int count)
 {
  string text;
  for (int i = 0; i < count; i++) text += unit;
  return text;
 }
 EUD_SpeakAction FindSpeakAction(IEntity owner)
 {
  if (!owner) return null;
  ActionsManagerComponent manager = ActionsManagerComponent.Cast(owner.FindComponent(ActionsManagerComponent));
  if (!manager) return null;
  array<BaseUserAction> actions = {};
  manager.GetActionsList(actions);
  foreach (BaseUserAction candidate : actions)
  {
   EUD_SpeakAction speak = EUD_SpeakAction.Cast(candidate);
   if (speak) return speak;
  }
  return null;
 }
 int ConfiguredCount()
 {
  array<SCR_EditableCharacterComponent> units = {};
  return EUD_Dialog.GetConfigured(units);
 }

 override void EOnInit(IEntity owner)
 {
  if (!Replication.IsServer()) { ClearEventMask(EntityEvent.FRAME); return; }
  Started = Now();
  Next = Started + 5;
  PrintFormat("[EXPG UNIT DIALOG BEGIN] players=%1 deadline=%2", GetGame().GetPlayerManager().GetPlayerCount(), FIXTURE_SECONDS);
 }

 override void EOnFrame(IEntity owner, float timeSlice)
 {
  if (Finished || Now() < Next) return;
  Next = Now() + 0.25;
  if (Now() - Started > FIXTURE_SECONDS) { Check(false, "fixture finished before its deadline"); Finish("timeout"); return; }
  if (Phase == 0) { SpawnPair(); return; }
  if (Phase == 1) { if (Now() - PhaseAt >= 3) { StateChecks(); Phase = 2; } return; }
  if (Phase == 2) { StartGesture(); return; }
  if (Phase == 3) { ObserveGesture(); return; }
  if (Phase == 4) { KillSpeaker(); return; }
  if (Phase == 5) { if (Now() - PhaseAt >= 2) DeathChecks(); return; }
  if (Phase == 6) { if (Now() - PhaseAt >= 1) CleanupChecks(); return; }
 }

 void SpawnPair()
 {
  IEntity speaker = Spawn(Origin);
  IEntity listener = Spawn(Origin + Vector(1.5, 0, 0));
  bool spawned = Check(speaker != null && listener != null, "two vanilla US riflemen spawned");
  if (!spawned) { Finish("setup"); return; }
  SpeakerId = speaker.GetID();
  ListenerId = listener.GetID();
  Phase = 1;
  PhaseAt = Now();
 }

 void StateChecks()
 {
  IEntity speaker = Speaker();
  IEntity listener = Listener();
  SCR_EditableCharacterComponent unit = EUD_Dialog.Find(speaker);
  SCR_EditableCharacterComponent other = EUD_Dialog.Find(listener);
  if (!Check(unit && other, "riflemen carry the editable character component (dialog state host)")) { Finish("setup"); return; }
  Check(EUD_Dialog.FromEditable(unit) == unit, "AI rifleman is an attribute target (not a player)");
  Check(!unit.EUD_IsConfigured() && !unit.EUD_HasDialog() && ConfiguredCount() == 0, "fresh unit has no dialog and the registry is empty");
  string fallback = unit.EUD_GetDisplayName();
  PrintFormat("[EXPG UNIT DIALOG NAME] fallback='%1'", fallback);
  Check(!fallback.IsEmpty(), "display name falls back to identity or entity name");

  // Validation boundaries (UTF-8 bytes).
  Check(EUD_Dialog.ValidName(Repeat("a", 64)) && !EUD_Dialog.ValidName(Repeat("a", 65)), "name limit 64 bytes");
  Check(EUD_Dialog.ValidLine(Repeat("b", 512)) && !EUD_Dialog.ValidLine(Repeat("b", 513)), "line limit 512 bytes");
  Check(EUD_Dialog.ValidGesture(4) && !EUD_Dialog.ValidGesture(5) && !EUD_Dialog.ValidGesture(-1), "gesture choices 0..4");
  Check(!unit.EUD_SetLine(10, "x") && !unit.EUD_SetLine(-1, "x"), "line index outside 0..9 refused");
  Check(!unit.EUD_SetLine(0, Repeat("c", 513)) && !unit.EUD_SetName(Repeat("d", 65)) && !unit.EUD_IsConfigured(), "oversized text refused whole, state untouched");

  // Production writes.
  bool written = unit.EUD_SetName("Sgt. Test") && unit.EUD_SetLine(0, "Hello there.") && unit.EUD_SetLine(2, "Third[br]line");
  Check(written, "name and two lines written on the server");
  array<string> spoken = {};
  int count = unit.EUD_GetSpokenLines(spoken);
  Check(count == 2 && spoken[0] == "Hello there." && spoken[1] == "Third[br]line", "spoken lines keep slot order and skip empty slots");
  Check(unit.EUD_GetLine(2) == "Third[br]line" && unit.EUD_GetLine(1).IsEmpty() && unit.EUD_GetLine(9).IsEmpty(), "ten slots kept for attribute lines");
  Check(EUD_Dialog.DisplayText("Third[br]line") == "Third\nline", "[br] shown as a line break");
  Check(unit.EUD_GetDisplayName() == "Sgt. Test", "GM name overrides the identity name");
  Check(unit.EUD_HasDialog() && ConfiguredCount() == 1, "configured unit registered for saving");

  // Conversation gates.
  Check(unit.EUD_IsAvailableSpeaker(), "alive AI is an available speaker");
  Check(unit.EUD_CanTalkWith(listener, EUD_Dialog.TALK_RANGE), "listener at 1.5 m may talk");
  Check(!unit.EUD_CanTalkWith(listener, 1.0) && !unit.EUD_CanTalkWith(speaker, EUD_Dialog.TALK_RANGE), "range and self refused");

  ActionChecks(speaker, listener);
  AttributeChecks(unit);
  CodecChecks();

  // Capture/restore path used by native saves and a CDF adapter.
  EUD_DialogRecord record = unit.EUD_Capture();
  Check(record && record.Valid() && record.lines.Count() == EUD_Dialog.MAX_LINES, "capture is a valid ten-slot record");
  Check(other.EUD_RestoreState(record.name, record.lines, record.gesture) && other.EUD_GetDisplayName() == "Sgt. Test" && ConfiguredCount() == 2, "record restores onto another unit");
  array<string> tooMany = {};
  for (int i = 0; i < 11; i++) tooMany.Insert("x");
  Check(!other.EUD_RestoreState("x", tooMany, 0) && !other.EUD_RestoreState("x", record.lines, 9), "invalid restore refused whole");

  PersistenceSystem persistence = PersistenceSystem.GetInstance();
  bool tracked;
  bool stateRegistered;
  if (persistence)
  {
   UUID id = persistence.GetId(speaker);
   tracked = !id.IsNull();
   stateRegistered = persistence.GetPersistentState(EUD_DialogPersistenceState) != null;
  }
  // Observations only: lazy native tracking and the world's persistence config vary by mode.
  PrintFormat("[EXPG UNIT DIALOG PERSISTENCE] system=%1 speakerTracked=%2 stateRegistered=%3", persistence != null, tracked, stateRegistered);
 }

 void ActionChecks(IEntity speaker, IEntity listener)
 {
  EUD_SpeakAction action = FindSpeakAction(speaker);
  // Requires the EUD_SpeakAction entry in the single Character_Base.et override.
  if (!Check(action != null, "Character_Base lists EUD_SpeakAction")) return;
  Check(action.CanBeShownScript(listener) && action.CanBePerformedScript(listener), "action shown and performable for a listener in range");
  Check(!action.CanBeShownScript(speaker), "action hidden for the speaker itself");
  string label;
  bool named = action.GetActionNameScript(label);
  PrintFormat("[EXPG UNIT DIALOG ACTION] label='%1'", label);
  Check(named && label == "Speak to Sgt. Test", "action label is Speak to <Name>");
 }

 void AttributeChecks(SCR_EditableCharacterComponent unit)
 {
  EUD_Line3Attribute line3 = new EUD_Line3Attribute();
  EUD_Line10Attribute line10 = new EUD_Line10Attribute();
  EUD_NameAttribute name = new EUD_NameAttribute();
  EUD_GestureAttribute gesture = new EUD_GestureAttribute();
  Check(line3.GetLineIndex() == 2 && line10.GetLineIndex() == 9 && name.GetLimit() == EUD_Dialog.NAME_LIMIT && line3.GetLimit() == EUD_Dialog.LINE_LIMIT, "attribute slots and limits");
  Check(!line3.IsSerializable() && !name.IsSerializable() && !gesture.IsSerializable(), "attributes excluded from numeric session saves");
  Check(line3.ReadVariable(unit, null) == null && gesture.ReadVariable(unit, null) == null, "no attribute read without an editing manager");
  line3.WriteVariable(unit, SCR_BaseEditorAttributeVar.EUD_CreateText("forged"), null, 1);
  Check(unit.EUD_GetLine(2) == "Third[br]line", "write without a verified Game Master editor ignored");
 }

 void CodecChecks()
 {
  SCR_BaseEditorAttributeVar var = SCR_BaseEditorAttributeVar.EUD_CreateText("Hello there.");
  var.SetFloat(3);
  SSnapshot snapshot = new SSnapshot(1024);
  SSnapSerializer writer = SSnapSerializer.MakeWriter(snapshot);
  SCR_BaseEditorAttributeVar.Extract(var, null, writer);
  SSnapSerializer reader = SSnapSerializer.MakeReader(snapshot);
  SCR_BaseEditorAttributeVar copy = new SCR_BaseEditorAttributeVar();
  SCR_BaseEditorAttributeVar.Inject(reader, null, copy);
  Check(copy.EUD_GetText() == "Hello there." && copy.GetFloat() == 3, "attribute variable text survives snapshot extract/inject with every loaded string layer");
  SSnapSerializer same = SSnapSerializer.MakeReader(snapshot);
  Check(SCR_BaseEditorAttributeVar.PropCompare(var, same, null), "unchanged short text compares equal");
  SCR_BaseEditorAttributeVar changed = SCR_BaseEditorAttributeVar.EUD_CreateText("Hello there!");
  changed.SetFloat(3);
  SSnapSerializer other = SSnapSerializer.MakeReader(snapshot);
  Check(!SCR_BaseEditorAttributeVar.PropCompare(changed, other, null), "edited text compares changed");
  SCR_BaseEditorAttributeVar longText = SCR_BaseEditorAttributeVar.EUD_CreateText(Repeat("e", 100));
  SSnapSerializer longReader = SSnapSerializer.MakeReader(snapshot);
  Check(!SCR_BaseEditorAttributeVar.PropCompare(longText, longReader, null), "long text always counts as changed");
 }

 void StartGesture()
 {
  SCR_EditableCharacterComponent unit = EUD_Dialog.Find(Speaker());
  if (!Check(unit && unit.EUD_SetGesture(1) && unit.EUD_GetGesture() == 1, "talking gesture choice stored")) { Phase = 4; return; }
  unit.EUD_PlayGesture();
  Phase = 3;
  PhaseAt = Now();
 }

 // Observation only: whether the native AI controller accepts a scripted gesture.
 void ObserveGesture()
 {
  ChimeraCharacter character = ChimeraCharacter.Cast(Speaker());
  CharacterControllerComponent controller;
  if (character) controller = character.GetCharacterController();
  if (controller && controller.IsPlayingGesture()) GesturePlayed = true;
  if (!GesturePlayed && Now() - PhaseAt < 3) return;
  PrintFormat("[EXPG UNIT DIALOG GESTURE] choice=1 playingWithin3s=%1", GesturePlayed);
  SCR_EditableCharacterComponent unit = EUD_Dialog.Find(Speaker());
  if (unit) unit.EUD_StopGesture();
  Phase = 4;
 }

 void KillSpeaker()
 {
  SCR_ChimeraCharacter victim = SCR_ChimeraCharacter.Cast(Speaker());
  SCR_CharacterDamageManagerComponent damage;
  if (victim) damage = SCR_CharacterDamageManagerComponent.Cast(victim.GetDamageManager());
  if (!Check(damage != null, "speaker damage manager found")) { Finish("kill"); return; }
  damage.Kill(Instigator.CreateInstigator(null));
  Phase = 5;
  PhaseAt = Now();
 }

 void DeathChecks()
 {
  IEntity speaker = Speaker();
  IEntity listener = Listener();
  SCR_EditableCharacterComponent unit = EUD_Dialog.Find(speaker);
  Check(unit && !unit.EUD_IsAvailableSpeaker() && !unit.EUD_CanTalkWith(listener, EUD_Dialog.TALK_RANGE), "dead speaker cannot talk");
  EUD_SpeakAction action = FindSpeakAction(speaker);
  if (action) Check(!action.CanBeShownScript(listener), "action hidden on a dead speaker");
  // Deleting the body unregisters it; clearing the other unit empties the registry.
  SCR_EntityHelper.DeleteEntityAndChildren(speaker);
  SCR_EditableCharacterComponent other = EUD_Dialog.Find(listener);
  Check(other && other.EUD_Clear() && !other.EUD_IsConfigured() && !other.EUD_HasDialog(), "clearing all fields removes the dialog");
  Phase = 6;
  PhaseAt = Now();
 }

 void CleanupChecks()
 {
  Check(!Speaker() && ConfiguredCount() == 0, "deleted and cleared units leave the registry");
  Finish("complete");
 }
}
