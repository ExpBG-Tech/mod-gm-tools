// Installed vanilla StopLoitering ignores terminateFast and repeatedly sends -1.
// -2 cleared native SMOKING tags in the bounded DangerExit fixture. This is not
// a general animation profile: all other actors/animations keep vanilla behavior.
modded class SCR_CharacterCommandLoiter
{
 protected bool m_EAC_FastSmokeExit;

 protected bool EAC_CanExitSmoke()
 {
  if (!Replication.IsServer() || !m_rplComponent || !m_rplComponent.IsOwner() || !m_pScrInputCtx || !m_pCharacter) return false;
  if (m_pScrInputCtx.m_iLoiteringType != ELoiteringType.SMOKING) return false;
  // IsDefault also tests editor defaults absent on vanilla's plain new object.
  // Match actual custom graph/command dispatch fields instead.
  if (m_customAnimData && (m_customAnimData.m_iCommandBindID != -1 || m_customAnimData.m_iControlVariableID != -1 || !m_customAnimData.m_sGraphName.IsEmpty() || !m_customAnimData.m_sGraphInstanceName.IsEmpty())) return false;
  EAC_ActivityPoint point = EAC_ActivityPoint.Cast(m_pScrInputCtx.GetLoiterEntity());
  return point && point.Activity && point.Activity.CanInterrupt(m_pCharacter, point);
 }

 override void StopLoitering(bool terminateFast)
 {
  super.StopLoitering(terminateFast);
  if (terminateFast && EAC_CanExitSmoke()) m_EAC_FastSmokeExit = true;
 }

 override void PrePhysUpdate(float pDt)
 {
  super.PrePhysUpdate(pDt);
  if (!m_EAC_FastSmokeExit || IsFlagFinished() || m_eState != ELoiterCommandState.EXITING) return;
  // Recheck possession/claim ownership while exiting; never force logical finish.
  if (!EAC_CanExitSmoke()) { m_EAC_FastSmokeExit = false; return; }
  m_pCharAnimComponent.CallCommand(m_pStaticTable.m_CommandGesture, -2, 0.0);
 }
}
