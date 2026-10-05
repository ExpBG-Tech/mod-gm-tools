class EAD_Catalog
{
 static const int COUNT = 41;
 static bool IsWreck(int asset) { return asset == 2 || asset == 3 || (asset >= 8 && asset <= 30); }
 static bool IsSeated(int asset) { return asset == 4 || asset == 6 || (asset >= 31 && asset <= 33); }
 static bool IsLitter(int asset) { return asset >= 36 && asset <= 40; }

 static ResourceName Prefab(int asset)
 {
  switch (asset)
  {
   case 0: return "{EAD1000000000020}Prefabs/EAD/EAD_BricksSmall.et";
   case 1: return "{EAD1000000000021}Prefabs/EAD/EAD_BricksMedium.et";
   case 2: return "{EAD1000000000022}Prefabs/EAD/EAD_BRDMWreck.et";
   case 3: return "{EAD1000000000023}Prefabs/EAD/EAD_UAZWreck.et";
   case 4: return "{74894F38DD9B4431}Prefabs/EAD/Bodies/EAD_Body01.et";
   case 5: return "{DD2BA16DEEE44E5A}Prefabs/EAD/Bodies/EAD_Body02.et";
   case 6: return "{CA9E27BE96B34817}Prefabs/EAD/Bodies/EAD_Body03.et";
   case 7: return "{AF080164D57145D3}Prefabs/EAD/Bodies/EAD_Body04.et";
   case 8: return "{EAD1000000000028}Prefabs/EAD/EAD_CarWreck.et";
   case 9: return "{EAD1000000000029}Prefabs/EAD/EAD_TruckWreck.et";
   case 10: return "{E92249AE1A395FB5}Prefabs/EAD/EAD_BMP1.et";
   case 11: return "{9A5A4FA6D2983524}Prefabs/EAD/EAD_BTR70.et";
   case 12: return "{17C9ECA393875627}Prefabs/EAD/EAD_M113.et";
   case 13: return "{BB58BC6C0B750500}Prefabs/EAD/EAD_M151A2.et";
   case 14: return "{48C05699080FD2DC}Prefabs/EAD/EAD_M998.et";
   case 15: return "{1452347DFEA299FA}Prefabs/EAD/EAD_T62.et";
   case 16: return "{9F07E1EE94472239}Prefabs/EAD/EAD_UAZ469.et";
   case 17: return "{E94D531E230CE77C}Prefabs/EAD/EAD_Ural4320.et";
   case 18: return "{9FEE5B0B3A26BDE5}Prefabs/EAD/Heine/EAD_AfghanTruck.et";
   case 19: return "{60A0905B831B01F5}Prefabs/EAD/Heine/EAD_PoliceCar.et";
   case 20: return "{3EED27F4EA2EE9DC}Prefabs/EAD/Heine/EAD_MiniVanOpen.et";
   case 21: return "{8017359B0530712E}Prefabs/EAD/Heine/EAD_TaxiCar.et";
   case 22: return "{FA0DD7E2F62D43AB}Prefabs/EAD/Heine/EAD_Ambulance.et";
   case 23: return "{7E3F6D8AD1831881}Prefabs/EAD/Heine/EAD_AfghanPickup.et";
   case 24: return "{1163EFB2757499BC}Prefabs/EAD/Heine/EAD_DirtyLada.et";
   case 25: return "{8B91E0925337CC72}Prefabs/EAD/Heine/EAD_DirtyCar.et";
   case 26: return "{63DBCED818E5E646}Prefabs/EAD/Heine/EAD_OldCarBlue.et";
   case 27: return "{5B56AA2EC193791D}Prefabs/EAD/Heine/EAD_OldCarWhite.et";
   case 28: return "{AC040A3595100BA0}Prefabs/EAD/Heine/EAD_OldCarGreen.et";
   case 29: return "{132F613C4E220AEA}Prefabs/EAD/Heine/EAD_OldCarRed.et";
   case 30: return "{1EC511C32A0C752B}Prefabs/EAD/Heine/EAD_Bus.et";
   case 31: return "{D549CFD37D33D3EE}Prefabs/EAD/Heine/EAD_FallenSoldier1.et";
   case 32: return "{D36327484A9F0A9A}Prefabs/EAD/Heine/EAD_FallenSoldier2.et";
   case 33: return "{C8A9A504D4678ECA}Prefabs/EAD/Heine/EAD_FallenSoldier3.et";
   case 34: return "{84A18B2A9B3F014B}Prefabs/EAD/EAD_TiresSmall.et";
   case 35: return "{1125A827A17953F9}Prefabs/EAD/EAD_TiresMedium.et";
   case 36: return "{8073047A1B91DA9B}Prefabs/EAD/EAD_Medical.et";
   case 37: return "{742BDD10D347AE0D}Prefabs/EAD/EAD_CasingsUSSR1.et";
   case 38: return "{F00C9D3DB6116124}Prefabs/EAD/EAD_CasingsUSSR2.et";
   case 39: return "{683CC3E104B6215F}Prefabs/EAD/EAD_CasingsUS1.et";
   case 40: return "{6F6C688C49C0A587}Prefabs/EAD/EAD_CasingsUS2.et";
  }
  return "";
 }
 static void Bounds(int asset, out vector mins, out vector maxs)
 {
  switch (asset)
  {
   case 0: mins = "-0.749026 0.008071 -0.626614"; maxs = "0.752625 0.690335 0.609247"; return;
   case 1: mins = "-0.752205 -0.001311 -0.630696"; maxs = "0.790463 0.886327 0.616757"; return;
   case 2: mins = "-1.143134 0.068827 -2.693564"; maxs = "1.155174 2.169178 3.056293"; return;
   case 3: mins = "-1.128666 -0.006361 -2.32822"; maxs = "1.142217 1.86239 2.243712"; return;
   case 4: mins = "-0.348415 -0.017601 -0.18484"; maxs = "0.400034 0.851231 1.026905"; return;
   case 5: mins = "-0.62657 -0.033834 -1.032403"; maxs = "0.396796 0.34745 0.79496"; return;
   case 6: mins = "-0.348415 -0.017601 -0.18484"; maxs = "0.400034 0.851231 1.026905"; return;
   case 7: mins = "-0.62657 -0.033834 -1.032403"; maxs = "0.396796 0.34745 0.79496"; return;
   case 8: mins = "-1.234388 0.015319 -2.295289"; maxs = "1.250391 1.476151 2.233948"; return;
   case 9: mins = "-1.390291 0.016709 -4.494812"; maxs = "1.812319 2.814633 3.770202"; return;
   case 10: mins = "-3.5 -0.073812 -7.5"; maxs = "3.5 4 7.5"; return;
   case 11: mins = "-1.453255 0.174796 -3.697334"; maxs = "1.47124 2.285518 3.713572"; return;
   case 12: mins = "-1.346028 -0.073754 -2.728968"; maxs = "1.360206 2.633578 2.93214"; return;
   case 13: mins = "-0.920693 0.275694 -1.948074"; maxs = "0.9535 1.372989 1.832735"; return;
   case 14: mins = "-1.159164 0.149457 -2.645112"; maxs = "1.202252 1.742485 2.307984"; return;
   case 15: mins = "-4 -0.061435 -10.5"; maxs = "4 4 10.5"; return;
   case 16: mins = "-1.164015 0.34612 -1.873981"; maxs = "1.10833 1.979748 2.212055"; return;
   case 17: mins = "-1.800894 0.114035 -3.713031"; maxs = "1.356316 2.722807 3.906781"; return;
   case 18: mins = "-1.077236 -0.002142 -2.017822"; maxs = "1.077526 2.239008 2.186479"; return;
   case 19: mins = "-1.015774 0.002657 -2.984237"; maxs = "1.015772 1.742664 2.792457"; return;
   case 20: mins = "-1.208224 -0.015798 -3.044288"; maxs = "1.045645 2.142033 2.378215"; return;
   case 21: mins = "-1.020234 -0.00743 -2.812937"; maxs = "1.019795 1.549017 2.892066"; return;
   case 22: mins = "-1.250683 -0.059032 -3.219255"; maxs = "1.430377 2.806664 4.338988"; return;
   case 23: mins = "-0.969315 0.001729 -3.117644"; maxs = "1.101856 2.491297 2.606628"; return;
   case 24: mins = "-0.809736 0.000788 -1.974013"; maxs = "0.803103 1.418017 1.898891"; return;
   case 25: mins = "-0.971537 0.0 -2.227264"; maxs = "0.971538 1.32435 2.227264"; return;
   case 26: mins = "-0.979322 -0.010942 -2.045879"; maxs = "0.823197 1.418845 2.075223"; return;
   case 27: mins = "-0.979322 -0.010942 -2.045879"; maxs = "0.823197 1.418845 2.075223"; return;
   case 28: mins = "-0.979322 -0.010942 -2.045879"; maxs = "0.823197 1.418845 2.075223"; return;
   case 29: mins = "-0.979322 -0.010942 -2.045879"; maxs = "0.823197 1.418845 2.075223"; return;
   case 30: mins = "-1.686494 0.000709 -5.608589"; maxs = "1.631957 2.894169 5.115043"; return;
   case 31: mins = "-0.330753 -0.00961 -0.995939"; maxs = "0.486287 0.847907 0.297482"; return;
   case 32: mins = "-0.330753 -0.00961 -0.995939"; maxs = "0.486287 0.847907 0.297482"; return;
   case 33: mins = "-0.330753 -0.00961 -0.995939"; maxs = "0.486287 0.847907 0.297482"; return;
   case 34: mins = "-0.934418 -0.050533 -1.141388"; maxs = "1.079524 0.59272 1.123769"; return;
   case 35: mins = "-1.863735 -0.022656 -1.584972"; maxs = "2.110444 0.942536 1.644863"; return;
   case 36: mins = "-0.888526 -0.0008 -0.968185"; maxs = "0.80144 0.091705 0.909113"; return;
   case 37: mins = "-0.409761 -0.001346 -0.471707"; maxs = "0.571656 0.025923 0.4742"; return;
   case 38: mins = "-0.446462 -0.00388 -0.501987"; maxs = "0.378326 0.027608 0.455879"; return;
   case 39: mins = "-0.425079 -0.001193 -0.427299"; maxs = "0.397682 0.02472 0.42376"; return;
   case 40: mins = "-0.450685 -0.000603 -0.536688"; maxs = "0.578874 0.022381 0.490561"; return;
  }
  mins = "-1.5 0 -1.5"; maxs = "1.5 2.5 1.5";
 }
 static float Extent(int asset)
 {
  vector mins, maxs; Bounds(asset, mins, maxs);
  float x = Math.Max(Math.AbsFloat(mins[0]), Math.AbsFloat(maxs[0]));
  float z = Math.Max(Math.AbsFloat(mins[2]), Math.AbsFloat(maxs[2]));
  return Math.Sqrt(x * x + z * z);
 }
 static IEntity Spawn(EAD_PropRecord record, BaseWorld world)
 {
  if (!record || !world) return null;
  ResourceName name = Prefab(record.Asset);
  if (name == "") return null;
  Resource resource = Resource.Load(name);
  if (!resource || !resource.IsValid()) return null;
  EntitySpawnParams parameters = new EntitySpawnParams();
  parameters.TransformMode = ETransformMode.WORLD;
  for (int axis = 0; axis < 4; axis++) parameters.Transform[axis] = record.Transform[axis];
  return GetGame().SpawnEntityPrefabLocal(resource, world, parameters);
 }
}
