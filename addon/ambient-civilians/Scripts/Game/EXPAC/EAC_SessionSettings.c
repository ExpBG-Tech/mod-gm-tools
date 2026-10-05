// The native editor session format stores three floats per attribute. CDF 1.4.1
// uses that public format too, so no optional mod type or dependency is needed.
// The fixed class occurrences in Edit.conf are a save contract: append, never
// reorder/reuse them. Unused string blocks are omitted from each saved entity.
class EAC_SessionSettings
{
 static const int VERSION = 1;
 static const int SETTINGS = 52;
 static const int TEXT_FIRST = 19;
 static const int COMMIT = 76;
 static const int MAX_CATALOG = 384;
 static const int MAX_FACTION = 128;

 static bool TextSupported(string value, int maximum)
 {
  if (value.Length() > maximum) return false;
  for (int i = 0; i < value.Length(); i++)
  {
   int character = value.ToAscii(i);
   if (character < 32 || character > 126) return false;
  }
  return true;
 }

 // Three ASCII bytes fit exactly in a float's 24-bit integer mantissa.
 static vector PackText(string value, int offset)
 {
  vector packed;
  for (int lane = 0; lane < 3; lane++)
  {
   int word = 0;
   int multiplier = 1;
   for (int digit = 0; digit < 3; digit++)
   {
    int index = offset + lane * 3 + digit;
    if (index < value.Length()) word += value.ToAscii(index) * multiplier;
    multiplier *= 256;
   }
   packed[lane] = word;
  }
  return packed;
 }

 static bool UnpackText(vector packed, int remaining, out string text)
 {
  text = string.Empty;
  for (int lane = 0; lane < 3; lane++)
  {
   int word = Math.Round(packed[lane]);
   if (word < 0 || word > 16777215 || word != packed[lane]) return false;
   for (int digit = 0; digit < 3; digit++)
   {
    int character = word % 256;
    word = word / 256;
    if (lane * 3 + digit >= remaining)
    {
     if (character != 0) return false;
     continue;
    }
    if (character < 32 || character > 126) return false;
    text += character.AsciiToString();
   }
  }
  return true;
 }
}

[BaseContainerProps()]
class EAC_SessionSettingsAttribute : SCR_BaseEditorAttribute
{
 [Attribute("0")] int m_Block;

 override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
 {
  // Serialization-only entries must never appear in the normal GM dialog.
  if (manager || !Replication.IsServer()) return null;
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.Cast(item);
  if (!editable) return null;
  EAC_AmbientModule module = EAC_AmbientModule.Cast(editable.GetOwner());
  if (!module) return null;
  if (m_Block == 0) module.NormalizeSettings();
  string catalog = module.ThemeCatalogResource;
  string faction = EAC_CivilianFactions.KeyAt(module.CivilianFaction);
  if (m_Block == 0)
  {
   if (!EAC_SessionSettings.TextSupported(catalog, EAC_SessionSettings.MAX_CATALOG) || !EAC_SessionSettings.TextSupported(faction, EAC_SessionSettings.MAX_FACTION))
   {
    Print("[EAC session] ERROR unsupported catalog/faction identity; saved settings are marked invalid, not truncated", LogLevel.ERROR);
    return SCR_BaseEditorAttributeVar.CreateVector("-1 0 0");
   }
   return SCR_BaseEditorAttributeVar.CreateVector(Vector(EAC_SessionSettings.VERSION, catalog.Length(), faction.Length()));
  }
  if (m_Block == EAC_SessionSettings.COMMIT) return SCR_BaseEditorAttributeVar.CreateVector(Vector(EAC_SessionSettings.VERSION, EAC_SessionSettings.SETTINGS, 0));
  if (m_Block > 0 && m_Block < EAC_SessionSettings.TEXT_FIRST)
  {
   vector values;
   for (int lane = 0; lane < 3; lane++)
   {
    int key = (m_Block - 1) * 3 + lane;
    if (key < EAC_SessionSettings.SETTINGS) values[lane] = module.GetSetting(key);
   }
   return SCR_BaseEditorAttributeVar.CreateVector(values);
  }
  int offset = (m_Block - EAC_SessionSettings.TEXT_FIRST) * 9;
  string identity = catalog + faction;
  if (offset < 0 || offset >= identity.Length()) return null;
  return SCR_BaseEditorAttributeVar.CreateVector(EAC_SessionSettings.PackText(identity, offset));
 }

 override void WriteVariable(Managed item, SCR_BaseEditorAttributeVar var, SCR_AttributesManagerEditorComponent manager, int playerID)
 {
  if (!Replication.IsServer() || manager || playerID != -1 || !var) return;
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.Cast(item);
  if (!editable) return;
  EAC_AmbientModule module = EAC_AmbientModule.Cast(editable.GetOwner());
  if (module) module.EAC_ReadSessionBlock(m_Block, var.GetVector());
 }
}
