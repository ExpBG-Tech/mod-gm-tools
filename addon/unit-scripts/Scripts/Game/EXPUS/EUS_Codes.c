// EXPBG Unit Scripts: shared codes, the approved animation catalog and small
// geometry helpers. Original implementation on the installed vanilla loiter,
// AI settings and editor APIs; no third-party code or assets.
class EUS_Codes
{
 // Unit script codes. They are the attribute entry values, the replicated
 // character state (SCR_ChimeraCharacter.EUS_Script) and the manager input.
 static const int NONE = 0;
 static const int HOLD = 1;
 static const int FREEZE = 2;
 // ANIMATION + catalog index (10..16).
 static const int ANIMATION = 10;
 // Squad attribute only: members differ, apply nothing.
 static const int MIXED = -1;

 // Night discipline modes (SCR_AIGroup.EUS_Discipline).
 static const int DISCIPLINE_OFF = 0;
 static const int DISCIPLINE_LIGHT = 1;
 static const int DISCIPLINE_TERROR = 2;

 // Context action codes.
 static const int ACTION_HOLD = 1;
 static const int ACTION_FREEZE = 2;
 static const int ACTION_RELEASE = 3;
 static const int ACTION_LIGHT = 4;
 static const int ACTION_TERROR = 5;

 // Above the installed editor (6000) and scenario (5000) setting origins.
 static const int SETTING_PRIORITY = 7000;
 // Look requests: above danger events and unknown targets (20/50), below an
 // identified enemy target (80) and the native commander (100).
 static const float LOOK_PRIORITY = 60;

 static bool IsAnimation(int code)
 {
  return code >= ANIMATION && code < ANIMATION + EUS_AnimationCatalog.COUNT;
 }

 static bool IsScript(int code)
 {
  return code == HOLD || code == FREEZE || IsAnimation(code);
 }

 static string Describe(int code)
 {
  if (code == NONE) return "Normal AI";
  if (code == HOLD) return "Hold position";
  if (code == FREEZE) return "Freeze";
  if (IsAnimation(code)) return "Animation: " + EUS_AnimationCatalog.Name(code - ANIMATION);
  return "Unknown";
 }

 static string DescribeDiscipline(int mode)
 {
  if (mode == DISCIPLINE_LIGHT) return "Light Discipline";
  if (mode == DISCIPLINE_TERROR) return "Terror Tactics";
  return "Off";
 }

 static string ActionName(int action)
 {
  if (action == ACTION_HOLD) return "EXPBG Hold Position";
  if (action == ACTION_FREEZE) return "EXPBG Freeze";
  if (action == ACTION_RELEASE) return "EXPBG Release Unit Scripts";
  if (action == ACTION_LIGHT) return "EXPBG Light Discipline";
  if (action == ACTION_TERROR) return "EXPBG Terror Tactics";
  return "EXPBG Unit Scripts";
 }

 static vector Forward(IEntity entity)
 {
  vector transform[4];
  entity.GetWorldTransform(transform);
  vector forward = transform[2];
  forward[1] = 0;
  if (forward.LengthSq() < 0.0001) return Vector(0, 0, 1);
  forward.Normalize();
  return forward;
 }

 static vector Eye(IEntity entity)
 {
  ChimeraCharacter character = ChimeraCharacter.Cast(entity);
  if (character) return character.EyePosition();
  return entity.GetOrigin() + Vector(0, 1.6, 0);
 }

 // Nearest living player within maxDistance of from. A non-zero forward limits
 // the search to a horizontal cone (minDot = cosine of the half angle).
 static IEntity Nearest(vector from, notnull array<IEntity> players, float maxDistance, vector forward, float minDot, IEntity self)
 {
  IEntity best;
  float bestDistance = maxDistance * maxDistance;
  bool cone = forward.LengthSq() > 0.0001;
  foreach (IEntity player : players)
  {
   if (!player || player == self) continue;
   vector delta = player.GetOrigin() - from;
   delta[1] = 0;
   float distance = delta.LengthSq();
   if (distance < 0.25 || distance > bestDistance) continue;
   if (cone)
   {
    delta.Normalize();
    if (vector.Dot(delta, forward) < minDot) continue;
   }
   best = player;
   bestDistance = distance;
  }
  return best;
 }
}

// Approved ambient animations: the installed vanilla loiter set that the
// commanding emote menu and ambient AI already use. Index = code - ANIMATION.
class EUS_AnimationCatalog
{
 static const int COUNT = 7;

 static string Name(int index)
 {
  switch (index)
  {
   case 0: return "Sit on the ground";
   case 1: return "Sit on a chair";
   case 2: return "Smoke";
   case 3: return "Stand at ease";
   case 4: return "Lean left";
   case 5: return "Lean right";
   case 6: return "Push-ups";
  }
  return "Unknown";
 }

 static ELoiteringType Type(int index)
 {
  switch (index)
  {
   case 0: return ELoiteringType.SIT;
   case 1: return ELoiteringType.CUSTOM;
   case 2: return ELoiteringType.SMOKING;
   case 3: return ELoiteringType.LOITERING;
   case 4: return ELoiteringType.LEAN_LEFT;
   case 5: return ELoiteringType.LEAN_RIGHT;
   case 6: return ELoiteringType.PUSHUPS;
  }
  return ELoiteringType.NONE;
 }

 // Vanilla ambient AI sits with the rifle in hand; every other pose holsters first.
 static bool Holster(int index)
 {
  return index != 0;
 }

 // Item presets from the installed LoiterItemPresets config: the chair emote
 // spawns its local chair, the smoking emote its cigarette and smoke.
 static SCR_ELoiterItemID Item(int index)
 {
  if (index == 1) return SCR_ELoiterItemID.CHAIR;
  if (index == 2) return SCR_ELoiterItemID.CIGAR;
  return SCR_ELoiterItemID.NONE;
 }

 static SCR_LoiterCustomAnimData CreateData(int index)
 {
  SCR_ELoiterItemID item = Item(index);
  if (item == SCR_ELoiterItemID.NONE) return null;
  return SCR_LoiterCustomAnimData.CreateInstance(-1, -1, SCR_CustomAnimData_Properties.BINDING_COMMAND_NAME, SCR_CustomAnimData_Properties.BINDING_NAME_NPC, ResourceName.Empty, ResourceName.Empty, -1, -1, string.Empty, item);
 }
}

// Native AI settings owned by EXPBG Unit Scripts. Each instance belongs to one
// unit control or discipline member and is removed by reference on release.
class EUS_SpeedSetting : SCR_AICharacterMovementSpeedSettingBase
{
 static EUS_SpeedSetting Create()
 {
  EUS_SpeedSetting setting = new EUS_SpeedSetting();
  setting.Init(SCR_EAISettingOrigin.SCENARIO, SCR_EAIBehaviorCause.ALWAYS);
  setting.m_iPriority = EUS_Codes.SETTING_PRIORITY;
  return setting;
 }

 override EMovementType GetSpeed(EMovementType desiredSpeed)
 {
  return EMovementType.IDLE;
 }

 override string GetDebugText()
 {
  return "EXPBG Unit Scripts: IDLE";
 }
}

class EUS_StanceSetting : SCR_AICharacterStanceSettingBase
{
 protected ECharacterStance m_EUS_Stance;

 static EUS_StanceSetting Create(ECharacterStance stance)
 {
  EUS_StanceSetting setting = new EUS_StanceSetting();
  setting.Init(SCR_EAISettingOrigin.SCENARIO, SCR_EAIBehaviorCause.ALWAYS);
  setting.m_iPriority = EUS_Codes.SETTING_PRIORITY;
  setting.m_EUS_Stance = stance;
  return setting;
 }

 ECharacterStance GetLockedStance()
 {
  return m_EUS_Stance;
 }

 override ECharacterStance GetStance(ECharacterStance desiredStance)
 {
  return m_EUS_Stance;
 }

 override string GetDebugText()
 {
  return "EXPBG Unit Scripts: " + typename.EnumToString(ECharacterStance, m_EUS_Stance);
 }
}

// Vanilla AI toggles vest lights itself (idle at night, off when unsafe). This
// setting keeps it from touching them so a discipline record owns the state.
class EUS_LightSetting : SCR_AICharacterLightInteractionSettingBase
{
 static EUS_LightSetting Create()
 {
  EUS_LightSetting setting = new EUS_LightSetting();
  setting.Init(SCR_EAISettingOrigin.SCENARIO, SCR_EAIBehaviorCause.ALWAYS);
  setting.m_iPriority = EUS_Codes.SETTING_PRIORITY;
  return setting;
 }

 override bool IsLightInterractionAllowed()
 {
  return false;
 }

 override string GetDebugText()
 {
  return "EXPBG Unit Scripts: lights owned by night discipline";
 }
}
