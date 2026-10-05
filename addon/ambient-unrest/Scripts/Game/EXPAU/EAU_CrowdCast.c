// Who protests and where they may stand. Civilian prefabs come from the Ambient
// Civilians theme catalog ("Mixed civilians", theme 0) and are validated by its own
// asset rules, one candidate per pick; a three-prefab vanilla fallback covers a
// catalog that fails to load.
class EAU_CrowdCast
{
 static const ResourceName THEMES = "{CA1A000000000030}Configs/EXPAC/Themes.conf";
 static const int THEME_INDEX = 0;
 protected static ref EAC_ThemeCatalog s_Catalog;
 protected static ref array<ref EAC_ThemeAsset> s_Fallback;
 // Plain pointers into s_Catalog / s_Fallback, which own the assets.
 protected static ref array<EAC_ThemeAsset> s_Assets;
 // 0 unchecked, 1 valid, -1 rejected; parallel to s_Assets.
 protected static ref array<int> s_State;
 protected static ref array<ResourceName> s_Canonical;
 protected static int s_TotalWeight;

 protected static void Prepare()
 {
  if (s_Assets) return;
  s_Assets = {}; s_State = {}; s_Canonical = {};
  s_TotalWeight = 0;
  s_Catalog = EAC_ThemeCatalog.Load(THEMES);
  EAC_ThemeDefinition theme;
  if (s_Catalog) theme = s_Catalog.GetDefinition(THEME_INDEX);
  if (theme && theme.Characters)
  {
   foreach (EAC_ThemeAsset asset : theme.Characters) Add(asset);
  }
  if (!s_Assets.IsEmpty()) return;
  Print("[EAU] Ambient Civilians theme catalog unavailable; using the vanilla civilian fallback", LogLevel.WARNING);
  s_Fallback = {};
  Fallback("{8C7093AF368F496A}Prefabs/Characters/Factions/CIV/GenericCivilians/Character_CIV_CottonShirt_1.et");
  Fallback("{11EB9A0D2A5899EA}Prefabs/Characters/Factions/CIV/GenericCivilians/Character_CIV_DenimJacket_1.et");
  Fallback("{C943F3CC53D187B6}Prefabs/Characters/Factions/CIV/GenericCivilians/Character_CIV_Turtleneck_1.et");
 }

 protected static void Fallback(ResourceName prefab)
 {
  EAC_ThemeAsset asset = new EAC_ThemeAsset();
  asset.Prefab = prefab;
  asset.Weight = 1;
  asset.SourceFaction = "CIV";
  s_Fallback.Insert(asset);
  Add(asset);
 }

 protected static void Add(EAC_ThemeAsset asset)
 {
  if (!asset || asset.Prefab == "" || asset.Weight < 1 || asset.Weight > 1000 || s_Assets.Count() >= 64) return;
  s_Assets.Insert(asset);
  s_State.Insert(0);
  s_Canonical.Insert(ResourceName.Empty);
  s_TotalWeight += asset.Weight;
 }

 // Weighted pick. A not yet validated candidate is checked now (one resource load);
 // a rejected one leaves the pool and this pick returns empty for a retry next tick.
 static ResourceName Pick()
 {
  Prepare();
  if (s_TotalWeight <= 0) return ResourceName.Empty;
  int ticket = Math.RandomInt(0, s_TotalWeight);
  for (int i = 0; i < s_Assets.Count(); i++)
  {
   if (s_State[i] < 0) continue;
   int weight = s_Assets[i].Weight;
   if (ticket >= weight)
   {
    ticket -= weight;
    continue;
   }
   if (s_State[i] == 0) Validate(i);
   if (s_State[i] < 0) return ResourceName.Empty;
   return s_Canonical[i];
  }
  return ResourceName.Empty;
 }

 protected static void Validate(int index)
 {
  EAC_ThemeAsset asset = s_Assets[index];
  ResourceName canonical;
  string reason;
  if (EAC_ThemeSelection.ValidateAsset(asset, false, canonical, reason) && canonical != "")
  {
   s_State[index] = 1;
   s_Canonical[index] = canonical;
   return;
  }
  s_State[index] = -1;
  s_TotalWeight -= asset.Weight;
  PrintFormat("[EAU] Civilian prefab rejected: %1 (%2)", asset.Prefab, reason, level: LogLevel.WARNING);
 }

 // A spawned candidate that turned out armed never protests again this session.
 static void Reject(ResourceName canonical, string reason)
 {
  if (!s_Assets) return;
  for (int i = 0; i < s_Assets.Count(); i++)
  {
   if (s_State[i] != 1 || s_Canonical[i] != canonical) continue;
   s_State[i] = -1;
   s_TotalWeight -= s_Assets[i].Weight;
   PrintFormat("[EAU] Civilian prefab rejected: %1 (%2)", canonical, reason, level: LogLevel.WARNING);
   return;
  }
 }

 static bool IsUnarmed(IEntity actor)
 {
  if (!actor) return false;
  BaseWeaponManagerComponent weapons = BaseWeaponManagerComponent.Cast(actor.FindComponent(BaseWeaponManagerComponent));
  if (weapons)
  {
   array<IEntity> held = {};
   weapons.GetWeaponsList(held);
   if (!held.IsEmpty()) return false;
  }
  InventoryStorageManagerComponent inventory = InventoryStorageManagerComponent.Cast(actor.FindComponent(InventoryStorageManagerComponent));
  if (!inventory) return true;
  if (inventory.GetGrenadesCount() != 0) return false;
  array<IEntity> items = {};
  inventory.GetItems(items, EStoragePurpose.PURPOSE_ANY);
  if (items.Count() > 128) return false;
  foreach (IEntity item : items)
  {
   if (item && item.FindComponent(BaseWeaponComponent)) return false;
  }
  return true;
 }

 // Ground at the zone's own level, dry, and with room for a standing body.
 static bool GroundSpot(BaseWorld world, vector candidate, float baseHeight, out vector spot)
 {
  if (!world) return false;
  TraceParam down = new TraceParam();
  down.Start = Vector(candidate[0], baseHeight + 2.5, candidate[2]);
  down.End = Vector(candidate[0], baseHeight - 4, candidate[2]);
  down.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
  float hit = world.TraceMove(down, null);
  if (hit >= 1) return false;
  vector ground = down.Start + (down.End - down.Start) * hit;
  // A roof, vehicle top or the bottom of a drop is not the zone's street.
  if (ground[1] > baseHeight + 1.5 || ground[1] < baseHeight - 3) return false;
  if (ChimeraWorldUtils.TryGetWaterSurfaceSimple(world, ground - "0 0.1 0")) return false;
  TraceBox body = new TraceBox();
  body.Start = ground;
  body.Mins = "-0.3 0.3 -0.3";
  body.Maxs = "0.3 1.8 0.3";
  body.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
  if (world.TracePosition(body, null) < 0) return false;
  spot = ground + "0 0.05 0";
  return true;
 }
}

// Protest animation from stock character gestures (ECharacterGestures), the same
// ones the player emote menu plays. The raised open hand of "halt" is the closest
// vanilla pose to a raised protest arm; pointing and the two arm-sweep commands
// break the rhythm so neighbours are never in step.
class EAU_Gestures
{
 static void Pick(int previous, out int gesture, out float seconds)
 {
  int roll = Math.RandomInt(0, 100);
  if (roll < 50)
  {
   gesture = ECharacterGestures.COMMAND_STOP;
   seconds = Math.RandomFloat(2.5, 4.5);
  }
  else if (roll < 75)
  {
   gesture = ECharacterGestures.POINT_WITH_FINGER;
   seconds = Math.RandomFloat(1.5, 3);
  }
  else if (roll < 88)
  {
   gesture = ECharacterGestures.COMMAND_FOLLOW;
   seconds = 2;
  }
  else
  {
   gesture = ECharacterGestures.COMMAND_MOVE;
   seconds = 2;
  }
  // Repeated sweeps read as a signal, not a protest; fall back to the raised hand.
  if (gesture == previous && gesture != ECharacterGestures.COMMAND_STOP)
  {
   gesture = ECharacterGestures.COMMAND_STOP;
   seconds = Math.RandomFloat(2.5, 4.5);
  }
 }

 static bool IsProtestGesture(int gesture)
 {
  return gesture == ECharacterGestures.COMMAND_STOP || gesture == ECharacterGestures.POINT_WITH_FINGER || gesture == ECharacterGestures.COMMAND_FOLLOW || gesture == ECharacterGestures.COMMAND_MOVE;
 }
}
