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

// Reuse Optimizer's prefab transaction in its durable form: the squad itself is
// captured (EBG_CacheGroupSnapshot) and deleted with the survivors at sleep, and
// recreated from that snapshot at wake, so no empty group is left behind for a
// save, a load or a Game Master to delete. Its survivor transform/CREATED ledger is
// authoritative; Full restores prefab-default kits and health. Every respawned
// survivor's AI is pinned in the spawning call itself, before he joins the squad,
// and released only after the garrison bound him to his post (WakeFull).
class EXPG_FullCache : EBG_PrefabFullCache
{
 protected EXPG_GarrisonRecord m_Owner;
 protected ref array<ref EXPG_FullHold> m_Holds = {};
 protected ref array<ref EXPG_FullOriginal> m_Originals = {};
 protected ref array<EBG_CacheMember> m_Admitted = {};
 // Diagnostics and fixtures: survivors held in their spawning call, and those whose
 // AI agent only appeared after it (held later in the same Poll).
 static int s_SpawnHeld;
 static int s_SpawnLate;

 void SetOwner(EXPG_GarrisonRecord owner) { m_Owner = owner; }

 override protected bool CapturesNativeGroup()
 {
  return true;
 }

 override bool BeginManagedSleep(SCR_AIGroup group)
 {
  bool captured = super.BeginManagedSleep(group);
  // Garrison transactions use negative record ids; a portable snapshot needs a positive token.
  if (m_Snapshot && m_Record) { m_Snapshot.Token = Math.AbsInt(m_Record.Id); }
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

 // A loaded garrison: a CACHED transaction made from the ledger. Nothing exists in
 // the world yet; the squad and the survivors are spawned at wake, casualties never.
 bool ImportCached(EBG_CacheGroupSnapshot squad, array<ref EBG_PrefabSurvivor> rows)
 {
  if (!m_Record || !squad || !rows || m_State != EBG_FullGroupPhase.NEW || squad.Settings.Count() != 19) { return false; }
  m_Snapshot = squad;
  if (m_Snapshot.Token <= 0) { m_Snapshot.Token = Math.AbsInt(m_Record.Id); }
  foreach (EBG_PrefabSurvivor row : rows) { m_Survivors.Insert(row); }
  m_DeleteEmpty = m_Snapshot.Settings[0];
  m_DeleteNoPlayer = m_Snapshot.Settings[1];
  m_PrefabDormantAlive = m_Snapshot.Settings[2];
  m_PrefabDormantDead = m_Snapshot.Settings[3];
  m_Deleted = true;
  m_State = EBG_FullGroupPhase.CACHED;
  return true;
 }

 // Ledger export: the squad snapshot and the row of one member (null when he has none).
 EBG_CacheGroupSnapshot LedgerSquad()
 {
  return m_Snapshot;
 }

 EBG_PrefabSurvivor FindRow(EBG_CacheMember member)
 {
  foreach (EBG_PrefabSurvivor row : m_Survivors)
  {
   if (row.Member == member) { return row; }
  }
  return null;
 }

 // The squad this transaction recreated was removed afterwards (a Game Master deleted
 // it, with its soldiers): nothing is left to restore.
 bool GroupLost()
 {
  return m_GroupCreated && !m_Group;
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

 // Pins this actor's AI (maximum LOD) until the garrison releases him.
 protected bool Hold(EBG_CacheMember member, SCR_ChimeraCharacter actor)
 {
  if (!member || !actor) { return false; }
  CharacterControllerComponent controller = actor.GetCharacterController();
  AIControlComponent control = actor.GetAIControlComponent();
  if (!controller || !control || !control.GetAIAgent()) { return false; }
  EXPG_FullHold hold = new EXPG_FullHold();
  hold.Member = member;
  hold.Policy = new EBG_SimulationAgent();
  hold.Policy.Capture(control.GetAIAgent(), actor);
  hold.Policy.Suspend();
  m_Holds.Insert(hold);
  return true;
 }

 // The spawning call itself: the newborn is excluded from other saves and his AI is
 // pinned before he joins the squad.
 override protected void OnSurvivorSpawned(EBG_PrefabSurvivor row)
 {
  if (!row || !row.Entity || !row.Member) { return; }
  EXPG_GarrisonPersistence.KeepNewborn(row.Entity);
  if (FindHold(row.Member)) { return; }
  if (Hold(row.Member, row.Entity)) { s_SpawnHeld++; }
  else { s_SpawnLate++; }
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
   if (!Hold(member, actor)) { ready = false; }
  }
  return ready;
 }

 override void Poll()
 {
  if (!ObserveAndHold()) { return; }
  super.Poll();
  // Native adapter spawns at most one actor per Poll, pinned in its spawning call
  // (OnSurvivorSpawned); this pass holds any whose agent appeared only after it.
  ObserveAndHold();
  // The recreated squad belongs to the garrison in the call that spawned it: the
  // record, its settings and the save exclusion follow before it replicates.
  if (m_Group && m_Owner && m_Owner.Group != m_Group) { m_Owner.AdoptRestoredGroup(m_Group); }
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

 // A load replaced the world's garrisons: forget this transaction without waking
 // it. Nothing is spawned or deleted here.
 override void ReleaseForWorldCleanup()
 {
  if (m_Record) { EBG_CacheCleanup.Get().ReleaseGroup(m_Record); }
  m_Holds.Clear();
  m_Originals.Clear();
  m_Admitted.Clear();
  m_Owner = null;
  super.ReleaseForWorldCleanup();
 }

 // The recreated squad was deleted, or a load replaced the scene: whoever is still
 // held gets his AI back, and the rest of the transaction is forgotten.
 void Abandon()
 {
  foreach (EXPG_FullHold hold : m_Holds)
  {
   if (!hold.Policy || !hold.Policy.Agent || hold.Policy.Agent.GetPermanentLOD() != AIAgent.GetMaxLOD()) { continue; }
   hold.Policy.Restore();
  }
  ReleaseForWorldCleanup();
 }

 // Fixture and diagnostics access: is this actor's AI still pinned by this transaction?
 bool IsHeld(SCR_ChimeraCharacter actor)
 {
  foreach (EXPG_FullHold hold : m_Holds)
  {
   if (hold.Policy && hold.Policy.Character == actor) { return true; }
  }
  return false;
 }
}
