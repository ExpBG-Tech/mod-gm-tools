// EXPBG Unit Scripts ambient animations play until the Game Master stops them.
//
// Vanilla ends a loiter when its animation sequence ends on its own: the loiter
// command sees the graph's IsLoitering tag drop and stops (SCR_CharacterCommandLoiter
// .PrePhysUpdate, "m_bWasTag && !isTag"). Smoke and Stand at ease are such finite
// sequences. Unit Scripts used to issue a new loiter from its 250 ms manager tick:
// the old command played its exit, the soldier stood in normal locomotion for up to
// several seconds (his AI could step and turn), holstered and aligned again, and
// every new entry started from wherever the last cycle left him (production
// 2026-10-08: smoke cycles every ~16 s, "could not be kept after 4 attempts" during
// a cache pause, then he walked off).
//
// Here the same command plays its pose again the moment the sequence ends: the
// gesture is re-issued exactly as the command's own LOITERING state issues it, so
// the soldier never leaves the root-motion-controlled loiter command. It runs on
// every machine that runs the command (the server owner and each proxy), keyed by
// the replicated EUS_Script, so proxies loop in step without any message. Only the
// end the vanilla command would take by itself is looped: a requested stop
// (EXITING), a fall, swimming or a moving platform still end it as in vanilla, and
// Unit Scripts then re-issues the pose on the held spot (EUS_UnitControl.KeepLoiter).
// If the graph does not take the re-issued gesture within REISSUE_SECONDS it is
// sent again, at most REISSUES times; after that the owner ends the command so the
// manager starts a fresh loiter (never an endless pose-less loiter).
//
// Cost: the vanilla per-frame update of a loitering character plus one state and
// tag test; the character lookup runs only on the frame a sequence ends, and the
// extra tag test only during the second after a re-issue.
modded class SCR_CharacterCommandLoiter
{
 static const float EUS_REISSUE_SECONDS = 1;
 static const int EUS_REISSUES = 3;

 // Seconds since the last re-issue while the pose's tag has not come back; -1 idle.
 protected float m_fEUS_Waiting = -1;
 protected int m_iEUS_Reissues;

 override void PrePhysUpdate(float pDt)
 {
  if (m_eState == ELoiterCommandState.LOITERING)
  {
   if (m_bWasTag)
   {
    if (!m_pCharAnimComponent.IsPrimaryTag(m_pStaticTable.m_IsLoiteringTag)) EUS_Cycle();
    else m_fEUS_Waiting = -1;
   }
   else if (m_fEUS_Waiting >= 0)
   {
    EUS_Wait(pDt);
   }
  }
  super.PrePhysUpdate(pDt);
 }

 // The pose's sequence ended by itself: play it again in this command.
 protected void EUS_Cycle()
 {
  SCR_ChimeraCharacter character = EUS_LoopedCharacter();
  if (!character)
  {
   return;
  }
  // Clears m_bWasTag and re-issues the gesture; vanilla below then sees no tag drop
  // this frame and keeps its fall and platform checks.
  SwitchState(ELoiterCommandState.LOITERING);
  character.EUS_PoseCycles++;
  m_fEUS_Waiting = 0;
  m_iEUS_Reissues = 0;
 }

 // After a re-issue: the tag is back (the pose plays), or the gesture is sent again.
 protected void EUS_Wait(float pDt)
 {
  if (m_pCharAnimComponent.IsPrimaryTag(m_pStaticTable.m_IsLoiteringTag))
  {
   m_fEUS_Waiting = -1;
   return;
  }
  m_fEUS_Waiting += pDt;
  if (m_fEUS_Waiting < EUS_REISSUE_SECONDS)
  {
   return;
  }
  if (m_iEUS_Reissues < EUS_REISSUES && EUS_LoopedCharacter())
  {
   m_iEUS_Reissues++;
   m_fEUS_Waiting = 0;
   SwitchState(ELoiterCommandState.LOITERING);
   return;
  }
  m_fEUS_Waiting = -1;
  // The owner ends it; Unit Scripts issues a fresh loiter on the held spot.
  if (m_rplComponent && m_rplComponent.IsOwner()) m_pCommandHandler.StopLoitering(false);
 }

 // The character when this command plays the pose his Unit Scripts animation
 // asked for, otherwise null (vanilla ambient loiters, emotes, other scripts).
 protected SCR_ChimeraCharacter EUS_LoopedCharacter()
 {
  SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(m_pCharacter);
  if (!character || !EUS_Codes.IsAnimation(character.EUS_Script) || !m_pScrInputCtx)
  {
   return null;
  }
  if (m_pScrInputCtx.m_iLoiteringType != EUS_AnimationCatalog.Type(character.EUS_Script - EUS_Codes.ANIMATION))
  {
   return null;
  }
  return character;
 }
}
