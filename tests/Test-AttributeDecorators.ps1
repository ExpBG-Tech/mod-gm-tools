#requires -Version 7.0
# Portable guards for Game Master attribute dialogs:
# - every attribute category has an icon whose resource exists in the pack: the vanilla dialog
#   shows tabs as icons only (m_iMaxTabsUntilImageOnly 1), so a category without an icon is an
#   invisible tab (0.1.13: the EXPBG Garrison tab could not be found);
# - every modded editor attribute repeats [BaseContainerProps()]: without it Edit.conf reports
#   "Unknown class" and the attribute disappears (0.1.13: vanilla instant weather).
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$addon = Join-Path $repo 'addon'
$guids = [Collections.Generic.HashSet[string]]::new()
foreach ($meta in Get-ChildItem -LiteralPath $addon -Recurse -File -Filter '*.meta') {
    foreach ($m in [regex]::Matches((Get-Content -LiteralPath $meta.FullName -Raw), 'Name "\{([0-9A-F]{16})\}')) { [void]$guids.Add($m.Groups[1].Value) }
}
$categories = @(Get-ChildItem -LiteralPath $addon -Recurse -File -Filter '*.conf' | Where-Object { $_.FullName -match '[\\/]Configs[\\/]Editor[\\/]AttributeCategories[\\/]' })
if (!$categories.Count) { throw 'No attribute categories found.' }
foreach ($file in $categories) {
    $text = Get-Content -LiteralPath $file.FullName -Raw
    $icon = [regex]::Match($text, 'Icon "\{([0-9A-F]{16})\}[^"]+\.edds"')
    if (!$icon.Success) { throw "Attribute category without an icon (invisible tab): $($file.FullName.Substring($repo.Length + 1))" }
    if (!$guids.Contains($icon.Groups[1].Value)) { throw "Attribute category icon is not a pack resource: $($file.FullName.Substring($repo.Length + 1))" }
}
$modded = 0
foreach ($file in Get-ChildItem -LiteralPath $addon -Recurse -File -Filter '*.c') {
    $lines = @(Get-Content -LiteralPath $file.FullName)
    for ($i = 0; $i -lt $lines.Count; $i++) {
        $m = [regex]::Match($lines[$i], '^\s*modded\s+class\s+(\w+Attribute)\b')
        if (!$m.Success) { continue }
        $modded++
        $previous = if ($i -gt 0) { $lines[$i - 1] } else { '' }
        if ($previous -notmatch '^\s*\[BaseContainerProps\(') { throw "modded class $($m.Groups[1].Value) needs [BaseContainerProps(), ...] like its original: $($file.FullName.Substring($repo.Length + 1)):$($i + 1)" }
    }
}
"PASS: $($categories.Count) attribute categories with pack icons; $modded modded editor attributes keep their container decorator."
