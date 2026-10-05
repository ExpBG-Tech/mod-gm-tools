[ComponentEditorProps(category: "EXPBG/Intel", description: "Portable USB drive that stores intel downloaded from an EXPBG server rack")]
class EIR_DriveComponentClass : ScriptComponentClass {}

// The stored intel lives on the item, so it follows the drive through drops, hand-overs
// and inventories. Replicated like Intel Items so the inventory tooltip can show it.
class EIR_DriveComponent : ScriptComponent
{
 [Attribute("", UIWidgets.EditBox, "Stored intel title (up to 128 UTF-8 bytes)"), RplProp()]
 protected string m_sTitle;
 [Attribute("", UIWidgets.EditBox, "Stored intel text (up to 4096 UTF-8 bytes)"), RplProp()]
 protected string m_sContent;

 bool IsAuthority()
 {
  RplComponent rpl = RplComponent.Cast(GetOwner().FindComponent(RplComponent));
  return !rpl || !rpl.IsProxy();
 }
 string GetTitle() { return m_sTitle; }
 string GetContent() { return m_sContent; }
 bool IsEmpty() { return m_sTitle.IsEmpty() && m_sContent.IsEmpty(); }
 bool Holds(string title, string content) { return m_sTitle == title && m_sContent == content; }
 string GetDescription()
 {
  if (m_sTitle.IsEmpty()) return m_sContent;
  if (m_sContent.IsEmpty()) return m_sTitle;
  return m_sTitle + "\n\n" + m_sContent;
 }
 // Same display markers as Intel Items: the GM text box cannot enter a line break,
 // so a typed [br] (or \n) marks one. Display only; stored text keeps the marker.
 static string DisplayText(string text)
 {
  string display = text;
  display.Replace("[br]", "\n");
  display.Replace("\\n", "\n");
  return display;
 }
 // Server only. Also the restore entry point for a persistence bridge.
 bool Store(string title, string content)
 {
  if (!IsAuthority() || !EIR_RackComponent.ValidText(title, content)) return false;
  m_sTitle = title;
  m_sContent = content;
  Replication.BumpMe();
  return true;
 }
 override void OnPostInit(IEntity owner)
 {
  super.OnPostInit(owner);
  if (!GetGame().InPlayMode() || !IsAuthority()) return;
  // Dropped drives may carry intel; keep them out of vanilla item cleanup like Intel Items.
  SCR_GarbageSystem garbage = SCR_GarbageSystem.GetByEntityWorld(owner);
  if (garbage) garbage.UpdateBlacklist(owner, true);
 }
 // Any drive anywhere in the user's inventory. Preference: an empty drive, then one
 // that already holds this exact intel, then the first drive (overwrite = true).
 // Bounded by the carrier's inventory; the client prompt caches the result.
 static EIR_DriveComponent FindDrive(IEntity user, string title, string content, out bool overwrite)
 {
  overwrite = false;
  if (!user) return null;
  InventoryStorageManagerComponent inventory = InventoryStorageManagerComponent.Cast(user.FindComponent(InventoryStorageManagerComponent));
  if (!inventory) return null;
  array<IEntity> items = {};
  inventory.FindItemsWithComponents(items, {EIR_DriveComponent}, EStoragePurpose.PURPOSE_ANY);
  EIR_DriveComponent same;
  EIR_DriveComponent first;
  foreach (IEntity item : items)
  {
   if (!item) continue;
   EIR_DriveComponent drive = EIR_DriveComponent.Cast(item.FindComponent(EIR_DriveComponent));
   if (!drive) continue;
   if (drive.IsEmpty()) return drive;
   if (!same && drive.Holds(title, content)) same = drive;
   if (!first) first = drive;
  }
  if (same) return same;
  overwrite = first != null;
  return first;
 }
}

// Inventory hover shows the stored intel, like Intel Items; an empty drive keeps the
// prefab description.
[BaseContainerProps()]
class EIR_DriveUIInfo : SCR_InventoryUIInfo
{
 protected EIR_DriveComponent Drive(InventoryItemComponent item)
 {
  if (!item || !item.GetOwner()) return null;
  return EIR_DriveComponent.Cast(item.GetOwner().FindComponent(EIR_DriveComponent));
 }
 override string GetInventoryItemName(InventoryItemComponent item)
 {
  EIR_DriveComponent drive = Drive(item);
  if (!drive) return super.GetInventoryItemName(item);
  if (drive.IsEmpty()) return super.GetInventoryItemName(item) + " (empty)";
  return super.GetInventoryItemName(item) + " (intel)";
 }
 override string GetInventoryItemDescription(InventoryItemComponent item)
 {
  EIR_DriveComponent drive = Drive(item);
  if (!drive || drive.IsEmpty()) return super.GetInventoryItemDescription(item);
  return EIR_DriveComponent.DisplayText(drive.GetDescription());
 }
}
