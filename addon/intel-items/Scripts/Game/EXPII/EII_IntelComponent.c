[ComponentEditorProps(category: "EXPBG/Intel", description: "Editable, portable intel with optional one-time computer startup")]
class EII_IntelComponentClass : ScriptComponentClass {}

class EII_IntelComponent : ScriptComponent
{
 static const int TITLE_LIMIT = 128;
 static const int CONTENT_LIMIT = 4096;
 static const float SOUND_RADIUS = 10;
 static const ResourceName SOUND_PROJECT = "{E110000000000030}Audio/EXPII/EII_Startup.acp";
 // Nonzero only during synchronous CDF restoration; pickups during restore stay silent.
 static int RestoreDepth;
 // Server: every intel item in play, for read-only lookups by other modules (AI Surrender
 // interrogation). Non-owning: an entry turns null when its item is deleted, and OnDelete
 // removes it. No item state is kept or changed here.
 protected static ref array<IEntity> s_aRegistered = {};

 [Attribute("", UIWidgets.EditBox, "Intel title (up to 128 UTF-8 bytes)"), RplProp()]
 protected string m_sTitle;
 [Attribute("", UIWidgets.EditBox, "Intel text (up to 4096 UTF-8 bytes)"), RplProp()]
 protected string m_sContent;
 [Attribute("0", UIWidgets.CheckBox, "Play startup once on first read or pickup")]
 protected bool m_bComputer;
 [Attribute("0", UIWidgets.CheckBox, "Bounded lifecycle diagnostics; never logs intel text"), RplProp()]
 protected bool m_bDebug;
 [RplProp()]
 protected bool m_bStarted;

 protected InventoryItemComponent m_Item;
 protected AudioHandle m_Audio = AudioHandle.Invalid;
 protected int m_iLogCount;

 bool IsAuthority()
 {
  RplComponent rpl = RplComponent.Cast(GetOwner().FindComponent(RplComponent));
  return !rpl || !rpl.IsProxy();
 }
 string GetTitle() { return m_sTitle; }
 string GetContent() { return m_sContent; }
 bool HasStarted() { return m_bStarted; }
 bool GetDebug() { return m_bDebug; }
 string GetDescription()
 {
  if (m_sTitle.IsEmpty()) return m_sContent;
  if (m_sContent.IsEmpty()) return m_sTitle;
  return m_sTitle + "\n\n" + m_sContent;
 }
 // A line break typed or pasted into the GM text box arrives as a space (in-game test),
 // so a typed [br] (or \n) marks one. Display only: stored text keeps the typed marker.
 static string DisplayText(string text)
 {
  string display = text;
  display.Replace("[br]", "\n");
  display.Replace("\\n", "\n");
  return display;
 }
 static bool ValidText(string title, string content)
 {
  return title.Length() <= TITLE_LIMIT && content.Length() <= CONTENT_LIMIT;
 }
 // Server: copies the intel items currently in play (wherever they are: loose, in a
 // container or carried) into outItems and returns their count. Read-only.
 static int GetRegistered(notnull array<IEntity> outItems)
 {
  for (int i = s_aRegistered.Count() - 1; i >= 0; i--)
  {
   if (!s_aRegistered[i]) s_aRegistered.Remove(i);
  }
  outItems.Copy(s_aRegistered);
  return outItems.Count();
 }
 bool SetText(string title, string content)
 {
  if (!IsAuthority() || !ValidText(title, content)) return false;
  m_sTitle = title;
  m_sContent = content;
  Replication.BumpMe();
  Trace("edited");
  return true;
 }
 void SetDebug(bool enabled)
 {
  if (!IsAuthority()) return;
  m_bDebug = enabled;
  m_iLogCount = 0;
  Replication.BumpMe();
  Trace("diagnostics enabled; radius=10m weight=0.01kg");
 }
 bool RestoreState(string title, string content, bool started)
 {
  if (!IsAuthority() || !ValidText(title, content)) return false;
  m_sTitle = title;
  m_sContent = content;
  m_bStarted = started;
  StopAudio();
  Replication.BumpMe();
  Trace("restored silently");
  return true;
 }
 void Trace(string eventName)
 {
  if (!m_bDebug || !IsAuthority() || m_iLogCount >= 64) return;
  m_iLogCount++;
  PrintFormat("[EII] %1 entity=%2 computer=%3 spent=%4 titleBytes=%5 textBytes=%6", eventName, GetOwner(), m_bComputer, m_bStarted, m_sTitle.Length(), m_sContent.Length());
 }
 override void OnPostInit(IEntity owner)
 {
  super.OnPostInit(owner);
  if (!GetGame().InPlayMode()) return;
  m_Item = InventoryItemComponent.Cast(owner.FindComponent(InventoryItemComponent));
  if (!IsAuthority()) return;
  s_aRegistered.Insert(owner);
  if (m_Item) m_Item.m_OnParentSlotChangedInvoker.Insert(OnSlotChanged);
  SCR_GarbageSystem garbage = SCR_GarbageSystem.GetByEntityWorld(owner);
  if (garbage) garbage.UpdateBlacklist(owner, true);
  Trace("created");
 }
 protected void OnSlotChanged(InventoryStorageSlot oldSlot, InventoryStorageSlot newSlot)
 {
  Trace("inventory parent changed");
  if (!newSlot) return;
  IEntity carrier = GetOwner().GetRootParent();
  if (carrier && GetGame().GetPlayerManager().GetPlayerIdFromControlledEntity(carrier) > 0) TryStartup(carrier.GetOrigin());
 }
 // Authority alone consumes the token. Transient RPC deliberately has no JIP callback.
 void TryStartup(vector position)
 {
  if (!IsAuthority() || !m_bComputer || m_bStarted || RestoreDepth > 0) return;
  m_bStarted = true;
  Replication.BumpMe();
  Trace("startup consumed");
  Rpc(RpcDo_Startup, position);
  RpcDo_Startup(position);
 }
 [RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
 protected void RpcDo_Startup(vector position)
 {
  if (System.IsConsoleApp()) return;
  float distance = AudioSystem.GetDistance(position);
  if (distance < 0 || distance > SOUND_RADIUS) return;
  vector transform[4];
  Math3D.MatrixIdentity4(transform);
  transform[3] = position;
  StopAudio();
  m_Audio = AudioSystem.PlayEvent(SOUND_PROJECT, "SOUND_EII_STARTUP", transform);
  if (m_Audio == AudioHandle.Invalid)
   Print("[EII] Startup audio resource failed", LogLevel.ERROR);
  else
  {
   if (m_bDebug) PrintFormat("[EII client] startup handle created; distance=%1m", distance);
   GetGame().GetCallqueue().CallLater(StopAudio, 6500, false);
  }
 }
 protected void StopAudio()
 {
  if (GetGame()) GetGame().GetCallqueue().Remove(StopAudio);
  if (m_Audio != AudioHandle.Invalid && !System.IsConsoleApp()) AudioSystem.TerminateSound(m_Audio);
  m_Audio = AudioHandle.Invalid;
 }
 override void OnDelete(IEntity owner)
 {
  if (m_Item) m_Item.m_OnParentSlotChangedInvoker.Remove(OnSlotChanged);
  if (s_aRegistered) s_aRegistered.RemoveItem(owner);
  Trace("deleted; local audio released");
  StopAudio();
  super.OnDelete(owner);
 }
}

[BaseContainerProps()]
class EII_InventoryUIInfo : SCR_InventoryUIInfo
{
 override string GetInventoryItemDescription(InventoryItemComponent item)
 {
  if (!item || !item.GetOwner()) return super.GetInventoryItemDescription(item);
  EII_IntelComponent intel = EII_IntelComponent.Cast(item.GetOwner().FindComponent(EII_IntelComponent));
  if (!intel || intel.GetDescription().IsEmpty()) return super.GetInventoryItemDescription(item);
  return EII_IntelComponent.DisplayText(intel.GetDescription());
 }
}
