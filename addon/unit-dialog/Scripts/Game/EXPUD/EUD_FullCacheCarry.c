// Unit Caching and Garrison Full caching delete living survivors and respawn
// them from their prefab. A survivor's dialog rides in the Unit Caching
// survivor carry: copied on the server before deletion, then restored onto the
// respawned character before it joins its group, so clients and late joiners
// receive it through the replicated fields on stream-in. It also travels in the
// portable (CDF) Full snapshot ("eudDialog"); native saves wait for wake.
modded class EBG_SurvivorCarry
{
 protected ref EUD_DialogRecord m_EUD_Dialog;

 // The captured dialog of this survivor, or null when it had none.
 EUD_DialogRecord EUD_GetDialog() { return m_EUD_Dialog; }

 override void Capture(SCR_ChimeraCharacter entity)
 {
  super.Capture(entity);
  m_EUD_Dialog = null;
  SCR_EditableCharacterComponent unit = EUD_Dialog.Find(entity);
  if (!unit || !unit.EUD_IsConfigured()) return;
  EUD_DialogRecord record = unit.EUD_Capture();
  if (!record.Valid())
  {
   Print("[EUD] Full cache: survivor dialog failed validation and is not carried", LogLevel.WARNING);
   return;
  }
  m_EUD_Dialog = record;
 }

 override bool Write(SaveContext context)
 {
  if (!super.Write(context)) return false;
  if (!m_EUD_Dialog) return true;
  return context.StartObject("eudDialog") && m_EUD_Dialog.Write(context) && context.EndObject();
 }

 override void Read(LoadContext context)
 {
  super.Read(context);
  m_EUD_Dialog = null;
  if (!context.StartObject("eudDialog")) return;
  EUD_DialogRecord record = new EUD_DialogRecord();
  bool read = record.Read(context);
  context.EndObject();
  if (read && record.Valid()) m_EUD_Dialog = record;
  else Print("[EUD] Full cache snapshot: saved dialog dropped (unreadable or invalid)", LogLevel.WARNING);
 }

 override void Apply(SCR_ChimeraCharacter entity)
 {
  super.Apply(entity);
  if (!m_EUD_Dialog) return;
  SCR_EditableCharacterComponent unit = EUD_Dialog.Find(entity);
  // EUD_RestoreState validates, registers and replicates; it refuses when the
  // mission's dialog unit limit was filled while this survivor was cached.
  if (unit && unit.EUD_RestoreState(m_EUD_Dialog.name, m_EUD_Dialog.lines, m_EUD_Dialog.gesture)) return;
  Print("[EUD] Full cache: dialog could not be restored onto the respawned unit", LogLevel.WARNING);
 }
}
