#requires -Version 7.0
# Portable guard: the Game Master entity browser search (WidgetManager.SearchLocalized)
# only finds editable entities whose Name is a string-table key. Every editable entity
# UI info in the pack must name itself with a '#' key, and every pack key used by a
# resource must exist with the same text in its source table and runtime table.
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$addon = Join-Path $repo 'addon'
$project = Get-Content -LiteralPath (Join-Path $addon 'EXPBG_GM_Tools.gproj') -Raw

function Find-ModuleFile([string]$Relative) {
 $found = @(Get-ChildItem -LiteralPath $addon -Directory | ForEach-Object { Join-Path $_.FullName $Relative } | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf })
 if ($found.Count -ne 1) { throw "Registered string table must exist in exactly one module: $Relative ($($found.Count) found)" }
 $found[0]
}
function Read-Quoted([string]$Block) { @([regex]::Matches($Block, '"((?:[^"\\]|\\.)*)"') | ForEach-Object { $_.Groups[1].Value }) }
function Find-Close([string]$Text, [int]$Open) {
 $depth = 0; $quoted = $false
 for ($i = $Open; $i -lt $Text.Length; $i++) {
  $c = $Text[$i]
  if ($quoted) { if ($c -eq '\') { $i++ } elseif ($c -eq '"') { $quoted = $false }; continue }
  if ($c -eq '"') { $quoted = $true } elseif ($c -eq '{') { $depth++ } elseif ($c -eq '}') { $depth--; if ($depth -eq 0) { return $i } }
 }
 throw 'Unbalanced braces.'
}

$keys = @{}
# One entry per table: a table registered in several configurations (PC, HEADLESS) is checked once.
$tables = @([regex]::Matches($project, '(?s)StringTableSource\s+"\{[0-9A-F]{16}\}([^"]+)".*?StringTableRuntime\s+"\{[0-9A-F]{16}\}([^"]+)"') | Group-Object { $_.Groups[1].Value } | ForEach-Object { $_.Group[0] })
if (!$tables.Count) { throw 'No string tables registered in the project.' }
foreach ($table in $tables) {
 $source = Get-Content -LiteralPath (Find-ModuleFile $table.Groups[1].Value) -Raw
 $runtime = Get-Content -LiteralPath (Find-ModuleFile $table.Groups[2].Value) -Raw
 $items = @{}
 foreach ($item in [regex]::Matches($source, '(?s)CustomStringTableItem\s+"\{[0-9A-F]{16}\}"\s*\{(.*?)\n\s*\}')) {
  $id = [regex]::Match($item.Groups[1].Value, '\bId\s+"([^"]+)"').Groups[1].Value
  $text = [regex]::Match($item.Groups[1].Value, '\bTarget_en_us\s+"((?:[^"\\]|\\.)*)"')
  if (!$id -or !$text.Success) { throw "Incomplete string-table item in $($table.Groups[1].Value)" }
  if ($items.ContainsKey($id)) { throw "Duplicate string-table id $id" }
  $items[$id] = $text.Groups[1].Value
 }
 $ids = @(Read-Quoted ([regex]::Match($runtime, '(?s)\bIds\s*\{(.*?)\}').Groups[1].Value))
 $texts = @(Read-Quoted ([regex]::Match($runtime, '(?s)\bTexts\s*\{(.*?)\}').Groups[1].Value))
 if ($ids.Count -ne $texts.Count) { throw "Runtime table Ids/Texts differ in length: $($table.Groups[2].Value)" }
 if ($ids.Count -ne $items.Count) { throw "Runtime table and source table differ in size: $($table.Groups[2].Value)" }
 for ($i = 0; $i -lt $ids.Count; $i++) {
  if (!$items.ContainsKey($ids[$i])) { throw "Runtime id missing from source table: $($ids[$i])" }
  if ($items[$ids[$i]] -cne $texts[$i]) { throw "Runtime text differs from source text: $($ids[$i])" }
  if ($keys.ContainsKey($ids[$i])) { throw "String-table id registered twice: $($ids[$i])" }
  $keys[$ids[$i]] = $texts[$i]
 }
}

$checked = 0
foreach ($file in Get-ChildItem -LiteralPath $addon -Recurse -File -Include '*.et','*.conf','*.layout') {
 $text = Get-Content -LiteralPath $file.FullName -Raw
 $relative = $file.FullName.Substring($addon.Length + 1).Replace('\', '/')
 foreach ($reference in [regex]::Matches($text, '"#([A-Za-z0-9]+)-([^"\s]+)"')) {
  if ($reference.Groups[1].Value -ieq 'AR') { continue }
  $key = $reference.Groups[1].Value + '-' + $reference.Groups[2].Value
  if (!$keys.ContainsKey($key)) { throw "Unregistered localization key #$key in $relative" }
 }
 if ($file.Extension -ne '.et') { continue }
 foreach ($info in [regex]::Matches($text, 'm_UIInfo\s+SCR_Editable\w*UIInfo\s+(?:"\{[0-9A-F]{16}\}"\s*)?\{')) {
  $open = $info.Index + $info.Length - 1
  $close = Find-Close $text $open
  # Keep only the info's own fields: drop nested blocks.
  $own = [Text.StringBuilder]::new(); $depth = 0; $quoted = $false
  foreach ($c in $text.Substring($open + 1, $close - $open - 1).ToCharArray()) {
   if ($c -eq '"') { $quoted = !$quoted }
   if (!$quoted -and $c -eq '{') { $depth++ } elseif (!$quoted -and $c -eq '}') { $depth-- } elseif ($depth -eq 0) { [void]$own.Append($c) }
  }
  $name = [regex]::Match($own.ToString(), '(?m)^\s*Name\s+"([^"]*)"')
  if (!$name.Success) { continue }
  if (!$name.Groups[1].Value.StartsWith('#')) { throw "Editable entity Name is plain text (invisible to Game Master search): $relative" }
  $checked++
 }
}
if ($checked -lt 50) { throw "Expected the pack's editable entity UI infos, found $checked." }
"PASS: $checked editable entity names are string-table keys; $($keys.Count) keys agree across $($tables.Count) source and runtime tables."
