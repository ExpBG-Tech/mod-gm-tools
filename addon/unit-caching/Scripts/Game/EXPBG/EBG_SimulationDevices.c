// GRS reapplies device presentation from character channel state every 100ms.
// Suspend the authoritative channels through its public replicated API rather
// than competing with that callback over individual LightEntity enable flags.
class EBG_SimulationDevices
{
 protected Managed m_State;
 protected ref array<ref EBG_OptionalScalar> m_Channels = {};
 protected ref array<ref EBG_OptionalScalar> m_Pending = {};
 bool Capture(SCR_ChimeraCharacter character)
 {
  array<Managed> components = {};
  character.FindComponents(GenericComponent, components);
  foreach (Managed component : components)
   if (component.Type().ToString() == "GRS_DeviceStateComponent") m_State = component;
  if (!m_State) return true;
  array<string> fields = {"m_iIRLaser", "m_iIRIlluminator", "m_iWhiteLight", "m_iVisibleLaser", "m_iIRStrobe", "m_iHelmetL", "m_iHelmetR", "m_iChest"};
  foreach (string field : fields)
  {
   EBG_OptionalScalar value = new EBG_OptionalScalar();
   value.Field = field; value.Parameter = m_Channels.Count();
   value.Setter = "RequestSetChannel";
   if (!value.Read(m_State) || value.IntValue < 0 || value.IntValue > 2) return false;
   m_Channels.Insert(value);
  }
  EBG_OptionalScalar deployed = new EBG_OptionalScalar();
  deployed.Field = "m_bNVGDeployed"; deployed.Setter = "SetNVGDeployed"; deployed.Kind = 2;
  if (!deployed.Read(m_State)) return false;
  m_Channels.Insert(deployed);
  return true;
 }
 bool Suspend()
 {
  if (!m_State) return m_Channels.IsEmpty();
  foreach (EBG_OptionalScalar saved : m_Channels)
  {
   EBG_OptionalScalar off = new EBG_OptionalScalar();
   off.Field = saved.Field; off.Parameter = saved.Parameter; off.Setter = saved.Setter; off.Kind = saved.Kind;
   if (off.Matches(m_State)) continue;
   // Retain even a failed setter: it may have changed state before verification.
   m_Pending.Insert(saved);
   if (!off.Restore(m_State)) return false;
  }
  return true;
 }
 bool Restore()
 {
  if (!m_State) return m_Pending.IsEmpty();
  for (int i = m_Pending.Count() - 1; i >= 0; i--)
   if (m_Pending[i].Restore(m_State)) m_Pending.Remove(i);
  // A later recovery retry must not overwrite changes made after a successful wake.
  if (!m_Pending.IsEmpty()) return false;
  Discard();
  return true;
 }
 void Discard()
 {
  m_Pending.Clear(); m_Channels.Clear(); m_State = null;
 }
}
