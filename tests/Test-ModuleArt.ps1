#requires -Version 7.0
# Module art (2026-10-08 set): each listed editor prefab shows its own card (m_Image) and icon (Icon),
# both cooked .edds textures in the prefab's module with a .meta naming the same GUID and path,
# cards 313 px (MaxSize 512) and icons 256 px (MaxSize 256). Sound modules use no imageset icon.
$ErrorActionPreference = 'Stop'
$addon = Join-Path (Split-Path -Parent $PSScriptRoot) 'addon'
$entries = [ordered]@{
 'intel-items/PrefabsEditable/EXPII/EIR_ServerRackA.et' = 'EXPII/EIR_ServerRackA'
 'intel-items/PrefabsEditable/EXPII/EIR_ServerRackB.et' = 'EXPII/EIR_ServerRackB'
 'intel-items/PrefabsEditable/EXPII/EIR_USBDrive.et' = 'EXPII/EIR_USBDrive'
 'advanced-briefing-map/PrefabsEditable/EXPBM/EBM_BriefingBoard.et' = 'EXPBM/EBM_BriefingBoard'
 'advanced-briefing-map/PrefabsEditable/EXPBM/EBM_BriefingProjector.et' = 'EXPBM/EBM_BriefingProjector'
 'unit-caching/PrefabsEditable/EXPBG/EBG_CacheZone.et' = 'EXPBG/EBG_CacheZone'
 'unit-caching/PrefabsEditable/EXPBG/EBG_OptimizerController.et' = 'EXPBG/EBG_OptimizerController'
}
foreach ($sound in Get-ChildItem -LiteralPath (Join-Path $addon 'ambient-sounds/PrefabsEditable/EXPBG/Sounds') -Filter 'E_EXPBG_Sound_*.et' -File) {
 $entries["ambient-sounds/PrefabsEditable/EXPBG/Sounds/$($sound.Name)"] = 'EXPBG/EAS_' + ($sound.BaseName -replace '^E_EXPBG_Sound_')
}
if (@($entries.Keys | Where-Object { $_ -like 'ambient-sounds/*' }).Count -ne 28) { throw 'Expected 28 placed sound prefabs.' }
$guids = @{}
foreach ($prefab in $entries.Keys) {
 $module = $prefab.Split('/')[0]
 $text = Get-Content -Raw -LiteralPath (Join-Path $addon $prefab)
 $ui = [regex]::Match($text, '(?s)m_UIInfo SCR_EditableEntityUIInfo "\{[0-9A-F]{16}\}" \{(.*?)\n\s*\}')
 if (!$ui.Success) { throw "$prefab has no editable entity UI info." }
 if ($ui.Groups[1].Value -match 'IconSetName') { throw "$prefab still uses an imageset icon." }
 foreach ($kind in @(@{ Field = 'm_Image'; Suffix = 'Card'; Size = 313; Max = '512' }, @{ Field = 'Icon'; Suffix = 'Icon'; Size = 256; Max = '256' })) {
  $path = "UI/Textures/$($entries[$prefab])_$($kind.Suffix).edds"
  $m = [regex]::Match($ui.Groups[1].Value, "\n\s*$($kind.Field) `"\{([0-9A-F]{16})\}([^`"]+)`"")
  if (!$m.Success -or $m.Groups[2].Value -cne $path) { throw "$prefab $($kind.Field) must be $path." }
  $file = Join-Path $addon "$module/$path"
  $meta = Get-Content -Raw -LiteralPath "$file.meta"
  if ($meta -notmatch [regex]::Escape("Name `"{$($m.Groups[1].Value)}$path`"") -or $meta -notmatch "MaxSize `"$($kind.Max)`"") { throw "$path.meta must name {$($m.Groups[1].Value)} with MaxSize $($kind.Max)." }
  $bytes = [IO.File]::ReadAllBytes($file)
  if ([Text.Encoding]::ASCII.GetString($bytes, 0, 4) -cne 'DDS ' -or [BitConverter]::ToInt32($bytes, 12) -ne $kind.Size -or [BitConverter]::ToInt32($bytes, 16) -ne $kind.Size) { throw "$path is not a cooked $($kind.Size) px texture." }
  if (!(Test-Path -LiteralPath ($file -replace '\.edds$', '.png'))) { throw "$path has no PNG source." }
  if ($guids.ContainsKey($m.Groups[1].Value)) { throw "$path reuses the GUID of $($guids[$m.Groups[1].Value])." }
  $guids[$m.Groups[1].Value] = $path
 }
}
"PASS: module art: $($entries.Count) editor prefabs (3 intel, 2 briefing, 2 unit caching, 28 sounds) each show their own cooked card (313 px) and icon (256 px) with matching .meta GUIDs and PNG sources; no imageset icons."
