// Client-local intel reader: the vanilla configurable dialog with the vanilla scrollable
// message content, built in code (no custom layout or preset file). It never depends on
// the hint system and is never replicated; only the reading player's machine opens it.
class EII_ReadDialog : SCR_ConfigurableDialogUi
{
 static const ResourceName BASE_LAYOUT = "{E6B607B27BCC1477}UI/layouts/Menus/Dialogs/ConfigurableDialog.layout";
 static const ResourceName SCROLL_CONTENT = "{CC2566ADAD892072}UI/layouts/Menus/Dialogs/ReportDialog/ScrollMessageDialogContent.layout";
 static const string TAG = "EII_READ";
 // Weak reference: at most one reader is open at a time.
 protected static EII_ReadDialog s_Open;

 static bool IsOpen() { return s_Open != null; }
 static bool Open(string title, string content)
 {
  if (s_Open || System.IsConsoleApp() || !GetGame() || !GetGame().GetMenuManager() || !GetGame().GetWorkspace()) return false;
  // [Attribute] defaults do not apply to presets created in code: set every field used.
  SCR_ConfigurableDialogUiPreset preset = new SCR_ConfigurableDialogUiPreset();
  preset.m_sLayout = BASE_LAYOUT;
  preset.m_sContentLayout = SCROLL_CONTENT;
  preset.m_sTag = TAG;
  preset.m_sTitle = title;
  preset.m_bShowIcon = false;
  preset.m_aButtons = {};
  SCR_ConfigurableDialogUiButtonPreset closeButton = new SCR_ConfigurableDialogUiButtonPreset();
  // The "cancel" tag binds OnCancel -> Close; MenuBack is Escape / gamepad back.
  closeButton.m_sTag = SCR_ConfigurableDialogUi.BUTTON_CANCEL;
  closeButton.m_sActionName = "MenuBack";
  closeButton.m_sLabel = "Close";
  closeButton.m_eAlign = EConfigurableDialogUiButtonAlign.LEFT;
  closeButton.m_bShowButton = true;
  closeButton.m_sSoundHovered = SCR_SoundEvent.SOUND_FE_BUTTON_HOVER;
  closeButton.m_sSoundClicked = SCR_SoundEvent.CLICK_CANCEL;
  preset.m_aButtons.Insert(closeButton);
  EII_ReadDialog dialog = new EII_ReadDialog();
  if (!CreateByPreset(preset, dialog)) return false;
  s_Open = dialog;
  Widget root = dialog.GetRootWidget();
  TextWidget text;
  if (root) text = TextWidget.Cast(root.FindAnyWidget("ScrollMessage"));
  if (text)
  {
   text.SetVisible(true);
   text.SetText(content);
  }
  else dialog.SetMessage(content);
  return true;
 }
 // Over the game a dialog is not a menu, so the in-game context stays active; keep the
 // dialog/menu contexts active every frame so MenuBack (Escape) reaches the Close button.
 override void OnMenuUpdate(float tDelta)
 {
  super.OnMenuUpdate(tDelta);
  InputManager input = GetGame().GetInputManager();
  if (!input) return;
  input.ActivateContext("DialogContext", 2);
  input.ActivateContext("InteractableDialogContext", 2);
  input.ActivateContext("MenuContext", 2);
 }
 override void OnMenuClose()
 {
  super.OnMenuClose();
  if (s_Open == this) s_Open = null;
 }
}

// The vanilla action menu (the interaction prompt) is on the HUD ALWAYS_TOP layer, drawn
// above every MenuManager layout. Vanilla hides it and blocks interactions only while a
// menu is open (MenuManager.IsAnyMenuOpen), and the intel reader is a dialog. Apply
// the same rule while it is open: the prompt fades out, no hidden action can be
// performed, and both come back as soon as it closes.
modded class SCR_InteractionHandlerComponent
{
 protected override bool GetCanInteractScript(IEntity controlledEntity)
 {
  if (EII_ReadDialog.IsOpen()) return false;
  return super.GetCanInteractScript(controlledEntity);
 }
}
