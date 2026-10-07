class EXPG_FullHold
{
 EBG_CacheMember Member;
 ref EBG_SimulationAgent Policy;
}

// Presence in this ledger survives the engine nulling a deleted entity pointer.
// These originals were NOT acknowledged as deleted by our capture transaction.
class EXPG_FullOriginal
{
 EBG_CacheMember Member;
 SCR_ChimeraCharacter Entity;
}

// Reuse Optimizer's retained-group transaction. Its survivor transform/CREATED
// ledger is authoritative; Full restores prefab-default kits and health.
class EXPG_FullCache : EBG_PrefabFullCache
{
 protected EXPG_GarrisonRecord m_Owner;
 protected ref array<ref EXPG_FullHold> m_Holds = {};
 protected ref array<ref EXPG_FullOriginal> m_Originals = {};
 protected ref array<EBG_CacheMember> m_Admitted = {};

 void SetOwner(EXPG_GarrisonRecord owner) { m_Owner = owner; }

 override bool BeginManagedSleep(SCR_AIGroup group)
 {
  bool captured = super.BeginManagedSleep(group);
  // A partially refused deletion is still a live transaction. Quarantine any
  // retained originals in this callback, just as newly created wake actors.
  if (HasDeletionAttempted())
  {
   foreach (EBG_PrefabSurvivor row : m_Survivors)
   {
    if (!row.Entity) { continue; }
    EXPG_FullOriginal original = new EXPG_FullOriginal();
    original.Member = row.Member;
    original.Entity = row.Entity;
    m_Originals.Insert(original);
   }
   ObserveAndHold();
  }
  return captured;
 }

 protected EXPG_FullOriginal FindOriginal(EBG_CacheMember member)
 {
  foreach (EXPG_FullOriginal original : m_Originals) { if (original.Member == member) { return original; } }
  return null;
 }

 EXPG_FullHold FindHold(EBG_CacheMember member)
 {
  foreach (EXPG_FullHold hold : m_Holds) { if (hold.Member == member) { return hold; } }
  return null;
 }

 protected void ReleaseHold(EBG_CacheMember member, bool transferred)
 {
  EXPG_FullHold hold = FindHold(member);
  if (!hold) { return; }
  if (hold.Policy.Agent && hold.Policy.Agent.GetPermanentLOD() == AIAgent.GetMaxLOD())
  {
   if (transferred) { hold.Policy.RestorePossessed(); }
   else { hold.Policy.Restore(); }
  }
  m_Holds.RemoveItem(hold);
 }

 protected void ReleaseMemberControl(EBG_CacheMember member)
 {
  if (!m_Owner) { return; }
  foreach (EXPG_GarrisonMember assigned : m_Owner.Members)
  {
   if (assigned.CacheMember == member) { assigned.ReleaseControl(); return; }
  }
 }

 // This standalone record is deliberately absent from Optimizer's Records.
 // Update the SAME member refs before every base Poll/Release; core ConfirmDeath
 // cannot discover these members, and a CREATED slot must never be respawned.
 bool ObserveAndHold()
 {
  bool ready = true;
  for (int i = m_Survivors.Count() - 1; i >= 0; i--)
  {
   EBG_PrefabSurvivor row = m_Survivors[i];
   EBG_CacheMember member = row.Member;
   SCR_ChimeraCharacter actor = row.Entity;
   EXPG_FullOriginal original = FindOriginal(member);
   // Only acknowledged capture absence may create a survivor. An original
   // retained after failed deletion is already materialized, even before core
   // Poll has set CREATED. Its later removal must never authorize replacement.
   if ((row.Created || original) && !actor) { member.Dead = true; }
   CharacterControllerComponent controller;
   if (actor) { controller = actor.GetCharacterController(); }
   if (EXPG_GarrisonManager.IsDeadActor(actor)) { member.Dead = true; }
   // Possession is that guard's matter only; the others wake on their posts.
   if (actor && (actor.EBG_WasPlayerControlled() || (controller && controller.IsPlayerControlled())))
   { member.WasPlayer = true; }
   if (member.Dead || member.WasPlayer)
   {
    ReleaseMemberControl(member);
    ReleaseHold(member, true);
    continue;
   }
   if (!actor) { continue; } // Captured absence, not an unobserved casualty.
   SCR_AIGroup actualGroup = actor.GetCharacterGroup();
   // Core marks CREATED before its first native add, which can fail. A newborn
   // that has never joined and is still ungrouped remains our recovery actor.
   bool firstJoin = row.Created && !original && !m_Admitted.Contains(member) && !actualGroup;
   if ((!firstJoin && actualGroup != m_Group) || member.Entity != actor || (original && original.Entity != actor))
   {
    // External transfer owns this actor. Retire this restore slot permanently;
    // do not steal it back, fake a death, or use failure as permission to respawn.
    // The garrison forgets that guard alone (ForgetLeavers); nobody is released.
    ReleaseMemberControl(member);
    ReleaseHold(member, true);
    EBG_CacheCleanup.Get().ForgetPrefabMember(m_Record, member);
    m_Survivors.RemoveOrdered(i);
    if (original) { m_Originals.RemoveItem(original); }
    m_Admitted.RemoveItem(member);
    continue;
   }
   if (actualGroup && actualGroup == m_Group && !m_Admitted.Contains(member)) { m_Admitted.Insert(member); }
   if (firstJoin)
   {
    // A native OnAgentAdded listener can remove the actor before Add returns.
    // Null group therefore cannot prove join failure versus external removal.
    // Keep the exact actor held until an explicit native regroup resolves it.
    Refuse("Survivor group admission unresolved; existing actor held, manual regroup required");
    ready = false;
   }
   if (FindHold(member)) { continue; }
   AIControlComponent control = actor.GetAIControlComponent();
   if (!controller || !control || !control.GetAIAgent()) { ready = false; continue; }
   EXPG_FullHold hold = new EXPG_FullHold();
   hold.Member = member;
   hold.Policy = new EBG_SimulationAgent();
   hold.Policy.Capture(control.GetAIAgent(), actor);
   hold.Policy.Suspend();
   m_Holds.Insert(hold);
  }
  return ready;
 }

 override void Poll()
 {
  if (!ObserveAndHold()) { return; }
  super.Poll();
  // Native adapter spawns at most one actor per Poll. No scheduler yield occurs
  // between that spawn and this owned LOD hold. Native timing remains a test gate.
  ObserveAndHold();
 }

 override bool ReleaseRestored()
 {
  if (!ObserveAndHold()) { return false; }
  if (!super.ReleaseRestored()) { return false; }
  foreach (EXPG_FullHold hold : m_Holds)
  {
   if (!hold.Policy.Agent || hold.Policy.Agent.GetPermanentLOD() != AIAgent.GetMaxLOD()) { continue; }
   if (hold.Member.Dead || hold.Member.WasPlayer) { hold.Policy.RestorePossessed(); }
   else { hold.Policy.Restore(); }
  }
  m_Holds.Clear();
  m_Originals.Clear();
  m_Admitted.Clear();
  // Registration done by base Poll must not orphan cleanup ownership in an
  // external record after handoff. Subsequent cycles create a new transaction.
  EBG_CacheCleanup.Get().ReleaseGroup(m_Record);
  return true;
 }
}
