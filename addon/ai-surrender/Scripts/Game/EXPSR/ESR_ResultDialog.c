// Client-local interrogation answer: the vanilla configurable dialog with the vanilla
// scrollable message content, built in code. Opened only on the interrogating player's
// machine; never replicated. A new answer replaces the text of an open dialog.
class ESR_ResultDialog : SCR_ConfigurableDialogUi
{
 static const ResourceName BASE_LAYOUT = "{E6B607B27BCC1477}UI/layouts/Menus/Dialogs/ConfigurableDialog.layout";
 static const ResourceName SCROLL_CONTENT = "{CC2566ADAD892072}UI/layouts/Menus/Dialogs/ReportDialog/ScrollMessageDialogContent.layout";
 static const string TAG = "ESR_RESULT";
 // Weak reference: at most one answer dialog is open at a time.
 protected static ESR_ResultDialog s_Open;
 protected TextWidget m_wESR_Text;

 static bool Open(string title, string content)
 {
  if (System.IsConsoleApp() || !GetGame() || !GetGame().GetMenuManager() || !GetGame().GetWorkspace()) return false;
  if (s_Open)
  {
   s_Open.ESR_SetContent(content);
   return true;
  }
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
  ESR_ResultDialog dialog = new ESR_ResultDialog();
  if (!CreateByPreset(preset, dialog)) return false;
  s_Open = dialog;
  Widget root = dialog.GetRootWidget();
  if (root) dialog.m_wESR_Text = TextWidget.Cast(root.FindAnyWidget("ScrollMessage"));
  dialog.ESR_SetContent(content);
  return true;
 }

 protected void ESR_SetContent(string content)
 {
  if (m_wESR_Text)
  {
   m_wESR_Text.SetVisible(true);
   m_wESR_Text.SetText(content);
  }
  else SetMessage(content);
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
