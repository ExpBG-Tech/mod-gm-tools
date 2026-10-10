// EXPBG Mission Intro module (Game Master Systems entity). Every player, including late
// joiners, sees it once when he first spawns as a character in this game session: the
// screen is black, the title and a "time | date | location" line fade in, and the view
// fades back in. It reuses the Time Skip black screen (ETW_FadeOverlay). Nothing is sent
// over the network at spawn time: the settings are replicated module properties and each
// client plays the intro locally, at most once per game session.
//
// Title: the module's text, else the mission name. Location: the module's text, else
// the six-figure grid of the spawn position. Session memory only for the texts: like the
// Time Skip text they are not part of attribute saves (CDF keeps the module, On and the
// duration; an empty text falls back to the automatic title and grid).
[EntityEditorProps(category: "EXPBG/Time and Weather", description: "Mission Intro: black screen with title, time and location once per player spawn")]
class ETW_IntroModuleClass : GenericEntityClass {}

class ETW_IntroModule : GenericEntity
{
 static const int TEXT_LIMIT = 96;
 static const float HOLD_MIN = 2;
 static const float HOLD_MAX = 15;
 // Non-owning: entries become null when their module is deleted.
 protected static ref array<ETW_IntroModule> s_aModules;
 // Client: the intro already played in this game session.
 protected static bool s_bShown;

 [Attribute("1", UIWidgets.CheckBox, "On: players see the intro when they first spawn", category: "EXPBG Mission Intro"), RplProp()]
 protected bool m_bOn;
 [Attribute("4", UIWidgets.Slider, "Seconds the text stays on the black screen", "2 15 0.5", category: "EXPBG Mission Intro"), RplProp()]
 protected float m_fHold;
 [Attribute("", UIWidgets.EditBox, "Title (empty: mission name)", category: "EXPBG Mission Intro"), RplProp()]
 protected string m_sTitle;
 [Attribute("", UIWidgets.EditBox, "Location (empty: grid of the spawn position)", category: "EXPBG Mission Intro"), RplProp()]
 protected string m_sLocation;

 //------------------------------------------------------------------------------------------------
 void ETW_IntroModule(IEntitySource src, IEntity parent)
 {
  SetEventMask(EntityEvent.INIT);
 }

 //------------------------------------------------------------------------------------------------
 void ~ETW_IntroModule()
 {
  if (s_aModules)
   s_aModules.RemoveItem(this);
 }

 //------------------------------------------------------------------------------------------------
 override void EOnInit(IEntity owner)
 {
  super.EOnInit(owner);
  if (!GetGame().InPlayMode())
   return;
  if (!s_aModules)
   s_aModules = {};
  if (!s_aModules.Contains(this))
   s_aModules.Insert(this);
 }

 //------------------------------------------------------------------------------------------------
 bool IsOn()
 {
  return m_bOn;
 }

 float GetHold()
 {
  return m_fHold;
 }

 string GetTitle()
 {
  return m_sTitle;
 }

 string GetLocation()
 {
  return m_sLocation;
 }

 //------------------------------------------------------------------------------------------------
 //! Server: one GM setting (0 On, 1 seconds).
 void SetValue(int key, float value)
 {
  if (!Replication.IsServer())
   return;
  if (key == 0)
   m_bOn = value != 0;
  else if (key == 1)
   m_fHold = Math.Clamp(value, HOLD_MIN, HOLD_MAX);
  Replication.BumpMe();
 }

 //------------------------------------------------------------------------------------------------
 float GetValue(int key)
 {
  if (key == 0)
  {
   if (m_bOn)
    return 1;
   return 0;
  }
  return m_fHold;
 }

 //------------------------------------------------------------------------------------------------
 //! Server: title (key 0) or location (key 1); false when too long.
 bool SetText(int key, string text)
 {
  if (!Replication.IsServer() || text.Length() > TEXT_LIMIT)
   return false;
  if (key == 0)
   m_sTitle = text;
  else
   m_sLocation = text;
  Replication.BumpMe();
  return true;
 }

 //------------------------------------------------------------------------------------------------
 //! The first module that is on, or null.
 static ETW_IntroModule Active()
 {
  if (!s_aModules)
   return null;
  foreach (ETW_IntroModule module : s_aModules)
  {
   if (module && module.IsOn())
    return module;
  }
  return null;
 }

 //------------------------------------------------------------------------------------------------
 //! Owner client: his controlled entity became a living character.
 static void OnLocalSpawn(IEntity character)
 {
  if (s_bShown || System.IsConsoleApp() || !SCR_ChimeraCharacter.Cast(character))
   return;
  ETW_IntroModule module = Active();
  if (!module)
   return;
  s_bShown = true;
  string title = module.GetTitle();
  if (title.IsEmpty())
   title = GetGame().GetMissionName();
  string location = module.GetLocation();
  if (location.IsEmpty())
  {
   vector origin = character.GetOrigin();
   location = string.Format("Grid %1 %2", Grid(origin[0]), Grid(origin[2]));
  }
  string line = location;
  ChimeraWorld world = GetGame().GetWorld();
  TimeAndWeatherManagerEntity manager;
  if (world)
   manager = world.GetTimeAndWeatherManager();
  if (manager)
  {
   int year, month, day;
   manager.GetDate(year, month, day);
   line = string.Format("%1  |  %2  |  %3", ETW_TimeMath.FormatClock(manager.GetTimeOfTheDay()), ETW_TimeMath.FormatDate(year, month, day), location);
  }
  // Black at once, the text, then a 2 s fade back to the view.
  ETW_FadeOverlay.Show(0.1, module.GetHold(), 2, title, line, true);
 }

 //------------------------------------------------------------------------------------------------
 protected static string Grid(float metres)
 {
  int value = Math.Clamp(Math.Floor(metres / 100), 0, 999);
  string text = value.ToString();
  while (text.Length() < 3)
   text = "0" + text;
  return text;
 }
}

// The owner client learns of his new character here (also after a respawn; the intro
// plays only once per game session).
modded class SCR_PlayerController
{
 override void OnControlledEntityChanged(IEntity from, IEntity to)
 {
  super.OnControlledEntityChanged(from, to);
  if (to && GetGame().GetPlayerController() == this)
   ETW_IntroModule.OnLocalSpawn(to);
 }
}
