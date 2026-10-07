// EXPBG Time Skip black screen (clients, listen host and single player; never on a
// dedicated server). Its own layout at the workspace root with a high Z order, so it also
// covers the Game Master editor, spectators, the dead and the deploy menu (the vanilla
// character HUD fade hides itself outside the player camera). Opacity follows the local
// world clock every frame: fade out, hold with the text, fade in, then the widgets go.
class ETW_FadeOverlay
{
 static const ResourceName LAYOUT = "{AAF1355D54812314}UI/layouts/EXPTW/ETW_Fade.layout";
 static const int Z_ORDER = 10000;
 // Text fades in and out over this many seconds inside the black screen.
 static const float TEXT_FADE_S = 0.5;

 protected static Widget s_wRoot;
 protected static Widget s_wBlack;
 protected static TextWidget s_wText;
 protected static TextWidget s_wTime;
 protected static float s_fStart;
 protected static float s_fFadeOut;
 protected static float s_fHold;
 protected static float s_fFadeIn;
 // Evidence for fixtures: fade requests received (also on a dedicated server, which
 // ignores them) and black screens shown.
 protected static int s_iReceived;
 protected static int s_iShown;

 //------------------------------------------------------------------------------------------------
 static int GetReceived()
 {
  return s_iReceived;
 }

 //------------------------------------------------------------------------------------------------
 static int GetShown()
 {
  return s_iShown;
 }

 //------------------------------------------------------------------------------------------------
 static bool IsShowing()
 {
  return s_wRoot != null;
 }

 //------------------------------------------------------------------------------------------------
 static void Show(float fadeOut, float hold, float fadeIn, string text, string timeLine, bool includeGm)
 {
  s_iReceived++;
  if (System.IsConsoleApp())
   return;
  if (!includeGm)
  {
   SCR_EditorManagerEntity editor = SCR_EditorManagerEntity.GetInstance();
   if (editor && editor.IsOpened())
    return;
  }
  WorkspaceWidget workspace = GetGame().GetWorkspace();
  if (!workspace)
   return;
  if (!s_wRoot)
  {
   s_wRoot = workspace.CreateWidgets(LAYOUT);
   if (!s_wRoot)
   {
    Print("[ETW] time skip black screen layout could not be created", LogLevel.WARNING);
    return;
   }
   s_wRoot.SetFlags(WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS);
   s_wRoot.SetZOrder(Z_ORDER);
   s_wBlack = s_wRoot.FindAnyWidget("ETW_Black");
   s_wText = TextWidget.Cast(s_wRoot.FindAnyWidget("ETW_Text"));
   s_wTime = TextWidget.Cast(s_wRoot.FindAnyWidget("ETW_Time"));
  }
  if (s_wText)
   s_wText.SetText(text);
  if (s_wTime)
   s_wTime.SetText(timeLine);
  s_fFadeOut = Math.Max(fadeOut, 0.1);
  s_fHold = Math.Max(hold, 0.1);
  s_fFadeIn = Math.Max(fadeIn, 0.1);
  s_fStart = Now();
  s_wRoot.SetVisible(true);
  s_iShown++;
  ScriptCallQueue queue = GetGame().GetCallqueue();
  queue.Remove(ETW_FadeOverlay.Update);
  queue.CallLater(ETW_FadeOverlay.Update, 0, true);
  Update();
 }

 //------------------------------------------------------------------------------------------------
 protected static float Now()
 {
  BaseWorld world = GetGame().GetWorld();
  if (!world)
   return -1;
  return world.GetWorldTime() * 0.001;
 }

 //------------------------------------------------------------------------------------------------
 protected static void Update()
 {
  if (!s_wRoot)
  {
   Close();
   return;
  }
  float now = Now();
  float t = now - s_fStart;
  float total = s_fFadeOut + s_fHold + s_fFadeIn;
  // Mission ended or a new world started: the clock no longer belongs to this fade.
  if (now < 0 || t < 0 || t >= total)
  {
   Close();
   return;
  }
  float black = 1;
  if (t < s_fFadeOut)
   black = t / s_fFadeOut;
  else if (t > s_fFadeOut + s_fHold)
   black = 1 - (t - s_fFadeOut - s_fHold) / s_fFadeIn;
  float textFade = Math.Min(TEXT_FADE_S, s_fHold * 0.25);
  float inHold = t - s_fFadeOut;
  float text = 0;
  if (inHold > 0 && inHold < s_fHold)
   text = Math.Clamp(Math.Min(inHold, s_fHold - inHold) / textFade, 0, 1);
  if (s_wBlack)
   s_wBlack.SetOpacity(Math.Clamp(black, 0, 1));
  if (s_wText)
   s_wText.SetOpacity(text);
  if (s_wTime)
   s_wTime.SetOpacity(text);
 }

 //------------------------------------------------------------------------------------------------
 static void Close()
 {
  ArmaReforgerScripted game = GetGame();
  if (game && game.GetCallqueue())
   game.GetCallqueue().Remove(ETW_FadeOverlay.Update);
  if (s_wRoot)
   s_wRoot.RemoveFromHierarchy();
  s_wRoot = null;
  s_wBlack = null;
  s_wText = null;
  s_wTime = null;
 }
}
