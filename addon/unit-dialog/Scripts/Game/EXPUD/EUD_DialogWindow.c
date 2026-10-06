// Client-local conversation window: the vanilla configurable dialog with the
// vanilla scrollable message content, built in code. Continue walks through the
// non-empty lines; on the last line it becomes Restart. End conversation (or
// Escape) closes. It closes itself when the speaker dies, is taken over by a
// player, loses its dialog, or the reader walks away. Never replicated.
class EUD_DialogWindow : SCR_ConfigurableDialogUi
{
 static const ResourceName BASE_LAYOUT = "{E6B607B27BCC1477}UI/layouts/Menus/Dialogs/ConfigurableDialog.layout";
 static const ResourceName SCROLL_CONTENT = "{CC2566ADAD892072}UI/layouts/Menus/Dialogs/ReportDialog/ScrollMessageDialogContent.layout";
 static const string TAG = "EUD_DIALOG";
 static const string BUTTON_NEXT = "eud_next";
 static const float CHECK_SECONDS = 0.5;
 // Weak reference: at most one conversation is open at a time.
 protected static EUD_DialogWindow s_Open;

 protected ref array<string> m_aEUD_Lines = {};
 protected int m_iEUD_Line;
 protected string m_sEUD_Name;
 protected EntityID m_EUD_SpeakerId;
 protected RplId m_EUD_SpeakerRpl = RplId.Invalid();
 protected float m_fEUD_Check;
 protected bool m_bEUD_Ended;

 static bool IsOpen() { return s_Open != null; }

 static bool Open(IEntity speaker, SCR_EditableCharacterComponent unit)
 {
  if (s_Open || !speaker || !unit || System.IsConsoleApp() || !GetGame() || !GetGame().GetMenuManager() || !GetGame().GetWorkspace()) return false;
  array<string> lines = {};
  if (unit.EUD_GetSpokenLines(lines) == 0) return false;
  // [Attribute] defaults do not apply to presets created in code: set every field used.
  SCR_ConfigurableDialogUiPreset preset = new SCR_ConfigurableDialogUiPreset();
  preset.m_sLayout = BASE_LAYOUT;
  preset.m_sContentLayout = SCROLL_CONTENT;
  preset.m_sTag = TAG;
  preset.m_sTitle = unit.EUD_GetDisplayName();
  preset.m_bShowIcon = false;
  preset.m_aButtons = {};
  // The "cancel" tag binds OnCancel -> Close; MenuBack is Escape / gamepad back.
  preset.m_aButtons.Insert(CreateButtonPreset(SCR_ConfigurableDialogUi.BUTTON_CANCEL, "MenuBack", "End conversation", EConfigurableDialogUiButtonAlign.LEFT, SCR_SoundEvent.CLICK_CANCEL));
  // Custom tag: never bound to OnConfirm, which would close the window.
  preset.m_aButtons.Insert(CreateButtonPreset(BUTTON_NEXT, "DialogConfirm", "Continue", EConfigurableDialogUiButtonAlign.RIGHT, SCR_SoundEvent.CLICK));
  EUD_DialogWindow window = new EUD_DialogWindow();
  window.m_aEUD_Lines.Copy(lines);
  window.m_sEUD_Name = preset.m_sTitle;
  window.m_EUD_SpeakerId = speaker.GetID();
  RplComponent rpl = RplComponent.Cast(speaker.FindComponent(RplComponent));
  if (rpl) window.m_EUD_SpeakerRpl = rpl.Id();
  if (!CreateByPreset(preset, window)) return false;
  s_Open = window;
  window.EUD_Show();
  return true;
 }

 protected static SCR_ConfigurableDialogUiButtonPreset CreateButtonPreset(string tag, string action, string label, EConfigurableDialogUiButtonAlign align, string clickSound)
 {
  SCR_ConfigurableDialogUiButtonPreset button = new SCR_ConfigurableDialogUiButtonPreset();
  button.m_sTag = tag;
  button.m_sActionName = action;
  button.m_sLabel = label;
  button.m_eAlign = align;
  button.m_bShowButton = true;
  button.m_sSoundHovered = SCR_SoundEvent.SOUND_FE_BUTTON_HOVER;
  button.m_sSoundClicked = clickSound;
  return button;
 }

 protected void EUD_Show()
 {
  int count = m_aEUD_Lines.Count();
  if (count == 0) return;
  m_iEUD_Line = Math.ClampInt(m_iEUD_Line, 0, count - 1);
  SetTitle(string.Format("%1  (%2/%3)", m_sEUD_Name, m_iEUD_Line + 1, count));
  string line = EUD_Dialog.DisplayText(m_aEUD_Lines[m_iEUD_Line]);
  Widget root = GetRootWidget();
  TextWidget text;
  if (root) text = TextWidget.Cast(root.FindAnyWidget("ScrollMessage"));
  if (text)
  {
   text.SetVisible(true);
   text.SetText(line);
  }
  else SetMessage(line);
  ScrollLayoutWidget scroll;
  if (root) scroll = ScrollLayoutWidget.Cast(root.FindAnyWidget("ScrollLayout"));
  if (scroll) scroll.SetSliderPos(0, 0);
  SCR_InputButtonComponent next = FindButton(BUTTON_NEXT);
  if (!next) return;
  if (m_iEUD_Line < count - 1) next.SetLabel("Continue");
  else next.SetLabel("Restart");
 }

 override protected void OnButtonPressed(SCR_InputButtonComponent button)
 {
  super.OnButtonPressed(button);
  if (m_bEUD_Ended || GetButtonTag(button) != BUTTON_NEXT || m_aEUD_Lines.IsEmpty()) return;
  if (m_iEUD_Line < m_aEUD_Lines.Count() - 1) m_iEUD_Line++;
  else m_iEUD_Line = 0;
  EUD_Show();
  EUD_Dialog.RequestTalk(m_EUD_SpeakerRpl, false);
 }

 protected bool EUD_StillValid()
 {
  if (!GetGame() || !GetGame().GetWorld()) return false;
  IEntity speaker = GetGame().GetWorld().FindEntityByID(m_EUD_SpeakerId);
  PlayerController controller = GetGame().GetPlayerController();
  IEntity reader;
  if (controller) reader = controller.GetControlledEntity();
  SCR_EditableCharacterComponent unit = EUD_Dialog.Find(speaker);
  return unit && reader && unit.EUD_CanTalkWith(reader, EUD_Dialog.KEEP_RANGE);
 }

 // Over the game a dialog is not a menu, so the in-game context stays active; keep the
 // dialog/menu contexts active every frame so DialogConfirm and MenuBack reach the buttons.
 override void OnMenuUpdate(float tDelta)
 {
  super.OnMenuUpdate(tDelta);
  InputManager input = GetGame().GetInputManager();
  if (input)
  {
   input.ActivateContext("DialogContext", 2);
   input.ActivateContext("InteractableDialogContext", 2);
   input.ActivateContext("MenuContext", 2);
  }
  if (m_bEUD_Ended) return;
  m_fEUD_Check += tDelta;
  if (m_fEUD_Check < CHECK_SECONDS) return;
  m_fEUD_Check = 0;
  if (EUD_StillValid()) return;
  m_bEUD_Ended = true;
  Close();
 }

 override void OnMenuClose()
 {
  super.OnMenuClose();
  if (s_Open == this) s_Open = null;
  m_bEUD_Ended = true;
  EUD_Dialog.RequestTalk(m_EUD_SpeakerRpl, true);
 }
}
