#requires -Version 7.0
# Portable guard for the Ambient Destruction zone setting "Vehicle types" (Civilian /
# Military / Both). Every wreck in EAD_Catalog carries a vehicle category as catalog data
# (pinned below by prefab file name, reviewed case by case), the setting is zone key 12
# with default Both, replicated and saved, the wreck bag filters each pass by category and
# keeps the original order and random draws for Both, snapshots moved to schema 3 while
# schema 1/2 still read (as Both), and the GM attribute is a Civilian/Military/Both
# spinbox. Also checks tests/EAD_VehicleCategoryGameplay.c is wired to the runner and the
# docs mention the setting. No engine is launched.
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$module = Join-Path $repo 'addon/ambient-destruction'
$scripts = Join-Path $module 'Scripts/Game/EXPAD'
function Assert([bool]$Condition, [string]$Message) { if (!$Condition) { throw "FAIL: $Message" } }
function Read-Text([string]$Path) { [IO.File]::ReadAllText($Path) }
function Get-Body([string]$Text, [string]$Signature) {
 $match = [regex]::Match($Text, $Signature)
 Assert $match.Success "signature not found: $Signature"
 $open = $Text.IndexOf('{', $match.Index); $depth = 0
 for ($i = $open; $i -lt $Text.Length; $i++) {
  if ($Text[$i] -eq '{') { $depth++ } elseif ($Text[$i] -eq '}') { $depth--; if ($depth -eq 0) { return $Text.Substring($open + 1, $i - $open - 1) } }
 }
 throw "FAIL: unbalanced body for $Signature"
}

$catalogPath = Join-Path $scripts 'EAD_Catalog.c'
$recordsPath = Join-Path $scripts 'EAD_Records.c'
$zonePath = Join-Path $scripts 'EAD_Zone.c'
$snapshotPath = Join-Path $scripts 'EAD_Snapshot.c'
$attributePath = Join-Path $scripts 'EAD_Attribute.c'
$placementPath = Join-Path $scripts 'EAD_Placement.c'
$listPath = Join-Path $module 'Configs/Editor/AttributeLists/Edit.conf'
$fixturePath = Join-Path $repo 'tests/EAD_VehicleCategoryGameplay.c'
$catalog = Read-Text $catalogPath
$records = Read-Text $recordsPath
$zone = Read-Text $zonePath
$snapshot = Read-Text $snapshotPath
$attributes = Read-Text $attributePath
$placement = Read-Text $placementPath
$list = Read-Text $listPath
$fixture = Read-Text $fixturePath

# Catalog: the saved catalog size is unchanged, every wreck has a category, nothing else has one.
$count = [regex]::Match($catalog, 'static\s+const\s+int\s+COUNT\s*=\s*(\d+);')
Assert ($count.Success -and [int]$count.Groups[1].Value -eq 41) 'EAD_Catalog.COUNT must stay 41: saved snapshots carry it'
$isWreck = [regex]::Match($catalog, 'static\s+bool\s+IsWreck\s*\(\s*int\s+asset\s*\)\s*\{\s*return\s+([^;]+);')
Assert $isWreck.Success 'EAD_Catalog.IsWreck not found'
$wrecks = [Collections.Generic.List[int]]::new()
foreach ($term in ($isWreck.Groups[1].Value -split '\|\|')) {
 $part = $term.Trim()
 if ($part -match '^asset\s*==\s*(\d+)$') { $wrecks.Add([int]$Matches[1]) }
 elseif ($part -match '^\(\s*asset\s*>=\s*(\d+)\s*&&\s*asset\s*<=\s*(\d+)\s*\)$') { foreach ($n in [int]$Matches[1]..[int]$Matches[2]) { $wrecks.Add($n) } }
 else { throw "FAIL: unexpected IsWreck term: $part" }
}
$wrecks.Sort()
$prefabs = @{}
foreach ($m in [regex]::Matches((Get-Body $catalog 'static\s+ResourceName\s+Prefab\s*\(\s*int\s+asset\s*\)'), 'case\s+(\d+):\s*return\s+"\{[0-9A-F]{16}\}([^"]+)";')) { $prefabs[[int]$m.Groups[1].Value] = $m.Groups[2].Value }
Assert ($prefabs.Count -eq 41) "EAD_Catalog.Prefab must list 41 assets ($($prefabs.Count))"
$categoryBody = Get-Body $catalog 'static\s+int\s+Category\s*\(\s*int\s+asset\s*\)'
$categories = @{}
foreach ($m in [regex]::Matches($categoryBody, 'case\s+(\d+):\s*return\s+(\w+);')) {
 $asset = [int]$m.Groups[1].Value
 Assert (!$categories.ContainsKey($asset)) "EAD_Catalog.Category lists asset $asset twice"
 Assert ($m.Groups[2].Value -cin @('CIVILIAN', 'MILITARY')) "EAD_Catalog.Category must return CIVILIAN or MILITARY for asset $asset"
 $categories[$asset] = $m.Groups[2].Value
}
Assert ($categoryBody -match '\}\s*return\s+-1;\s*$') 'EAD_Catalog.Category must return -1 for assets that are not vehicle wrecks'
Assert ($catalog -match 'static\s+const\s+int\s+CIVILIAN\s*=\s*0;' -and $catalog -match 'static\s+const\s+int\s+MILITARY\s*=\s*1;' -and $catalog -match 'static\s+const\s+int\s+BOTH\s*=\s*2;') 'category values must equal the spinbox indices: Civilian 0, Military 1, Both 2'

# Reviewed classification (ambiguous ones are documented in EAD_Catalog.Category and the changelog).
$expected = [ordered]@{
 'EAD_BRDMWreck.et' = 'MILITARY'; 'EAD_UAZWreck.et' = 'MILITARY'; 'EAD_CarWreck.et' = 'CIVILIAN'; 'EAD_TruckWreck.et' = 'MILITARY'
 'EAD_BMP1.et' = 'MILITARY'; 'EAD_BTR70.et' = 'MILITARY'; 'EAD_M113.et' = 'MILITARY'; 'EAD_M151A2.et' = 'MILITARY'
 'EAD_M998.et' = 'MILITARY'; 'EAD_T62.et' = 'MILITARY'; 'EAD_UAZ469.et' = 'MILITARY'; 'EAD_Ural4320.et' = 'MILITARY'
 'EAD_AfghanTruck.et' = 'CIVILIAN'; 'EAD_PoliceCar.et' = 'CIVILIAN'; 'EAD_MiniVanOpen.et' = 'CIVILIAN'; 'EAD_TaxiCar.et' = 'CIVILIAN'
 'EAD_Ambulance.et' = 'CIVILIAN'; 'EAD_AfghanPickup.et' = 'CIVILIAN'; 'EAD_DirtyLada.et' = 'CIVILIAN'; 'EAD_DirtyCar.et' = 'CIVILIAN'
 'EAD_OldCarBlue.et' = 'CIVILIAN'; 'EAD_OldCarWhite.et' = 'CIVILIAN'; 'EAD_OldCarGreen.et' = 'CIVILIAN'; 'EAD_OldCarRed.et' = 'CIVILIAN'
 'EAD_Bus.et' = 'CIVILIAN'
}
Assert ($wrecks.Count -eq $expected.Count) "the catalog has $($wrecks.Count) wrecks; the reviewed table has $($expected.Count)"
foreach ($asset in $wrecks) {
 Assert $prefabs.ContainsKey($asset) "wreck asset $asset has no prefab"
 $name = Split-Path -Leaf $prefabs[$asset]
 Assert $categories.ContainsKey($asset) "wreck asset $asset ($name) has no vehicle category"
 Assert $expected.Contains($name) "wreck $name is not in the reviewed classification table"
 Assert ($categories[$asset] -ceq $expected[$name]) "wreck $name must be $($expected[$name]); the catalog says $($categories[$asset])"
 Assert (Test-Path -LiteralPath (Join-Path $module $prefabs[$asset])) "wreck prefab missing: $($prefabs[$asset])"
}
foreach ($asset in $categories.Keys) { Assert ($wrecks.Contains($asset)) "only vehicle wrecks carry a category (asset $asset)" }
$civilian = @($wrecks | Where-Object { $categories[$_] -eq 'CIVILIAN' }).Count
$military = @($wrecks | Where-Object { $categories[$_] -eq 'MILITARY' }).Count
Assert ($civilian -ge 4 -and $military -ge 4) "both categories need several wrecks (civilian $civilian, military $military)"

$allowed = Get-Body $catalog 'static\s+bool\s+Allowed\s*\(\s*int\s+asset,\s*int\s+types\s*\)'
Assert ($allowed -match 'if\s*\(\s*types\s*!=\s*CIVILIAN\s*&&\s*types\s*!=\s*MILITARY\s*\)\s*return\s+true;' -and $allowed -match 'return\s+Category\(asset\)\s*==\s*types;') 'Allowed: Civilian/Military need that category, Both admits every wreck'
$variantsBody = Get-Body $catalog 'static\s+int\s+Variants\s*\(\s*int\s+asset\s*\)'
Assert ($variantsBody -match 'if\s*\(\s*asset\s*==\s*26\s*\)\s*return\s+4;' -and $variantsBody -match 'if\s*\(\s*asset\s*>=\s*27\s*&&\s*asset\s*<=\s*29\s*\)\s*return\s+0;' -and $variantsBody -match 'return\s+1;') 'the four old-car paints 26-29 must share one family slot'

# Wreck bag: catalog order filtered by the zone's types; Both equals the pre-setting bag.
$next = Get-Body $records 'int\s+Next\s*\(\s*EAD_Random\s+random\s*\)'
Assert ($next -match 'if\s*\(\s*m_Assets\.IsEmpty\(\)\s*\)\s*Fill\(random,\s*m_Types\);' -and $next -match 'if\s*\(\s*m_Assets\.IsEmpty\(\)\s*\)\s*Fill\(random,\s*EAD_Catalog\.BOTH\);' -and $next -match 'if\s*\(\s*m_Assets\.IsEmpty\(\)\s*\)\s*return\s+-1;') 'EAD_WreckBag.Next must fill by the zone types and fall back to every wreck'
Assert ($next -match 'int\s+index\s*=\s*Math\.Min\(m_Assets\.Count\(\)\s*-\s*1,\s*Math\.Floor\(random\.Next\(\)\s*\*\s*m_Assets\.Count\(\)\)\);') 'the bag pick must keep its original random draw'
$fill = Get-Body $records 'protected\s+void\s+Fill\s*\(\s*EAD_Random\s+random,\s*int\s+types\s*\)'
Assert ($fill -match 'for\s*\(\s*int\s+asset\s*=\s*0;\s*asset\s*<\s*EAD_Catalog\.COUNT;\s*asset\+\+\s*\)' -and $fill -match '!EAD_Catalog\.IsWreck\(asset\)\s*\|\|\s*!EAD_Catalog\.Allowed\(asset,\s*types\)' -and $fill -match 'int\s+variants\s*=\s*EAD_Catalog\.Variants\(asset\);') 'EAD_WreckBag.Fill must walk the catalog in order and filter by category'
Assert ($fill -match 'if\s*\(\s*variants\s*==\s*1\s*\)\s*m_Assets\.Insert\(asset\);' -and $fill -match 'else\s+if\s*\(\s*variants\s*>\s*1\s*\)\s*m_Assets\.Insert\(asset\s*\+\s*Math\.Min\(variants\s*-\s*1,\s*Math\.Floor\(random\.Next\(\)\s*\*\s*variants\)\)\);') 'a multi-paint family must take one random paint per pass, like the original bag'
Assert ($records -match 'void\s+EAD_WreckBag\s*\(\s*int\s+types\s*\)') 'EAD_WreckBag must take the zone vehicle types'
# Simulated Both pass: the pre-setting bag was 2, 3, 8-25, one paint of 26-29, 30 (one paint draw).
$order = foreach ($asset in $wrecks) { $v = if ($asset -eq 26) { 4 } elseif ($asset -ge 27 -and $asset -le 29) { 0 } else { 1 }; if ($v -eq 1) { "$asset" } elseif ($v -gt 1) { "$asset+" } }
$original = @('2', '3') + @(8..25 | ForEach-Object { "$_" }) + @('26+', '30')
Assert (($order -join ',') -eq ($original -join ',')) "Both must fill the bag exactly like the pre-setting bag: $($order -join ',')"

# Zone: key 12, default Both, clamped, replicated, rebuilds, feeds the bag.
Assert ($zone -match '\[Attribute\("2",\s*UIWidgets\.EditBox,\s*"Vehicle types: 0 civilian, 1 military, 2 both",\s*"0 2 1"\),\s*RplProp\(\)\]\s*int\s+VehicleTypes;') 'EAD_Zone.VehicleTypes must default to 2 (Both) and replicate'
Assert ((Get-Body $zone 'void\s+Normalize\s*\(\s*\)') -match 'VehicleTypes\s*=\s*Math\.Clamp\(VehicleTypes,\s*0,\s*2\);') 'Normalize must clamp VehicleTypes to 0-2'
Assert ((Get-Body $zone 'int\s+GetSetting\s*\(\s*int\s+key\s*\)') -match 'case\s+12:\s*return\s+VehicleTypes;') 'GetSetting(12) must read VehicleTypes'
$set = Get-Body $zone 'void\s+SetSetting\s*\(\s*int\s+key,\s*int\s+value\s*\)'
Assert ($set -match 'case\s+12:\s*VehicleTypes\s*=\s*value;\s*break;' -and $set -match 'key\s*==\s*11\s*\|\|\s*key\s*==\s*12\)\s*RequestRebuild\(\);') 'SetSetting(12) must store VehicleTypes and regenerate the layout'
Assert ($zone -match 'm_WreckBag\s*=\s*new\s+EAD_WreckBag\(VehicleTypes\);') 'generation must hand the zone vehicle types to the wreck bag'
Assert ($placement -match '(?s)static\s+EAD_PropRecord\s+Candidate\s*\([^)]*\)\s*\{\s*//[^\n]*\n\s*if\s*\(\s*EAD_Catalog\.Prefab\(asset\)\s*==\s*""\s*\)\s*return\s+null;') 'Candidate must never record an asset without a prefab'

# Snapshot: schema 3 with 13 settings; schema 1 and 2 still read, as Both.
Assert ($snapshot -match 'static\s+const\s+int\s+SETTING_COUNT\s*=\s*13;') 'snapshots must hold 13 settings'
$valid = Get-Body $snapshot 'static\s+bool\s+ValidSettings\s*\('
Assert ($valid -match 'values\.Count\(\)\s*!=\s*SETTING_COUNT' -and $valid -match 'values\[12\]\s*<\s*0\s*\|\|\s*values\[12\]\s*>\s*2') 'ValidSettings must require 13 settings and vehicle types 0-2'
Assert ((Get-Body $snapshot 'static\s+bool\s+WriteZone\s*\(') -match 'for\s*\(\s*int\s+key\s*=\s*0;\s*key\s*<\s*SETTING_COUNT;') 'WriteZone must save every setting, vehicle types included'
$encode = Get-Body $snapshot 'static\s+string\s+EncodeZone\s*\('
Assert ($encode -match 'ctx\.WriteValue\("schema",\s*3\);' -and $encode -match 'ctx\.WriteValue\("catalog",\s*EAD_Catalog\.COUNT\);') 'EncodeZone must write schema 3 with the unchanged catalog size'
$read = Get-Body $snapshot 'static\s+EAD_ZoneSnapshot\s+ReadZone\s*\('
Assert ($read -match 'schema\s*<\s*1\s*\|\|\s*schema\s*>\s*3') 'ReadZone must accept schemas 1, 2 and 3'
Assert ($read -match '(?s)if\s*\(\s*schema\s*==\s*1\s*\)\s*\{\s*if\s*\(\s*data\.Settings\.Count\(\)\s*!=\s*11\s*\)\s*return\s+null;\s*data\.Settings\.Insert\(2\);') 'schema 1 still gets the mixed body type'
Assert ($read -match '(?s)if\s*\(\s*schema\s*<\s*3\s*\)\s*\{\s*if\s*\(\s*data\.Settings\.Count\(\)\s*!=\s*12\s*\)\s*return\s+null;\s*data\.Settings\.Insert\(EAD_Catalog\.BOTH\);') 'schema 1/2 snapshots must read as Both vehicle types'
Assert ($read.IndexOf('schema < 3') -lt $read.IndexOf('ValidSettings(data.Settings)')) 'old snapshots must be upgraded before validation'

# GM attribute: one spinbox entry, key 12, next to the wreck intensity.
Assert ($attributes -match '\[BaseContainerProps\(\),\s*SCR_BaseEditorAttributeCustomTitle\(\)\]\s*class\s+EAD_VehicleTypesAttribute\s*:\s*EAD_Attribute\s*\{\}') 'EAD_VehicleTypesAttribute must exist'
Assert ([regex]::Matches($list, 'EAD_VehicleTypesAttribute \{').Count -eq 1) 'Edit.conf must hold exactly one Vehicle types attribute'
$entry = [regex]::Match($list, '(?s)EAD_VehicleTypesAttribute \{(.*?)\n  \}').Groups[1].Value
Assert ($entry -match 'm_Key 12' -and $entry -match 'Name "Vehicle types"' -and $entry -match '\{C3A25A51777A99BD\}UI/layouts/Editor/Attributes/AttributePrefabs/AttributePrefab_Spinbox\.layout' -and $entry -match '\{EAD1000000000013\}Configs/Editor/AttributeCategories/EXPAD_Destruction\.conf') 'Vehicle types must be a key 12 spinbox in the Ambient Destruction tab'
$choices = [regex]::Matches($entry, 'm_sEntryName "([^"]+)" m_fEntryFloatValue (\d+)') | ForEach-Object { "$($_.Groups[1].Value)=$($_.Groups[2].Value)" }
Assert (($choices -join ';') -eq 'Civilian=0;Military=1;Both=2') "Vehicle types choices must be Civilian 0, Military 1, Both 2: $($choices -join ';')"
$keys = [regex]::Matches($list, 'm_Key (\d+)') | ForEach-Object { $_.Groups[1].Value }
Assert (@($keys | Sort-Object -Unique).Count -eq @($keys).Count) "Ambient Destruction attribute keys must be unique: $($keys -join ',')"
$order = [regex]::Matches($list, '(?m)^  (EAD_\w+Attribute) \{') | ForEach-Object { $_.Groups[1].Value }
Assert ([array]::IndexOf(@($order), 'EAD_VehicleTypesAttribute') -eq [array]::IndexOf(@($order), 'EAD_WrecksAttribute') + 1) "Vehicle types must follow Wreck intensity: $($order -join ',')"

# Enforce gotchas in the touched sources; the new fixture is ASCII with LF.
foreach ($path in @($catalogPath, $recordsPath, $zonePath, $snapshotPath, $attributePath, $placementPath, $fixturePath)) {
 $text = Read-Text $path
 Assert (![regex]::IsMatch($text, '\b(int|float|bool|string|vector|auto|IEntity|ResourceName)\s+(owned|Sleep)\b')) "reserved Enforce name used as a variable in $path"
 Assert (![regex]::IsMatch($text, 'Math\.RandomFloat\(\s*0\s*,\s*0\s*\)')) "Math.RandomFloat(0, 0) logs an engine error: $path"
 $bytes = [IO.File]::ReadAllBytes($path)
 Assert (($bytes | Where-Object { $_ -gt 127 }).Count -eq 0) "non-ASCII byte in $path"
}
Assert (!$fixture.Contains("`r")) 'the vehicle category fixture must use LF line endings'

# Native fixture wiring (executed only by the orchestrator through Run-Gameplay).
Assert ($fixture -match 'class\s+EXPG_GarrisonGameplayClass\s*:\s*GenericEntityClass' -and $fixture -match 'class\s+EXPG_GarrisonGameplay\s*:\s*GenericEntity') 'vehicle category fixture must keep the runner driver class names'
$regex = '\[EXPG EAD VEHICLE RESULT\] checks=[1-9]\d* failures=0 military=([4-9]|[1-9]\d+) civilian=([4-9]|[1-9]\d+) both=([6-9]|[1-9]\d+) mixed=1 replay=1 legacy=1 reason=complete'
$command = "pwsh -File tests/Run-Gameplay.ps1 -SourceSnapshot <indexed pack> -FixturePath tests/EAD_VehicleCategoryGameplay.c -TimeoutSeconds 420 -OrchestratorSlotGranted -ExpectResult '$regex'"
Assert ($fixture.Contains($command)) 'vehicle category fixture header must carry its runner command'
$format = [regex]::Match($fixture, 'PrintFormat\("(\[EXPG EAD VEHICLE RESULT\][^"]*)"').Groups[1].Value
Assert ($format -eq '[EXPG EAD VEHICLE RESULT] checks=%1 failures=%2 military=%3 civilian=%4 both=%5 mixed=%6 replay=%7 legacy=%8 reason=%9') "vehicle category fixture RESULT format changed: $format"
function Format-Result([string[]]$Values) { $line = $format; for ($i = $Values.Count; $i -ge 1; $i--) { $line = $line.Replace("%$i", $Values[$i - 1]) }; $line }
Assert ((Format-Result @('44', '0', '7', '9', '14', '1', '1', '1', 'complete')) -match $regex) 'a passing RESULT line must match the -ExpectResult regex'
Assert (!((Format-Result @('44', '1', '7', '9', '14', '1', '1', '1', 'complete')) -match $regex)) 'a RESULT line with failures must not match'
Assert (!((Format-Result @('44', '0', '3', '9', '14', '1', '1', '1', 'complete')) -match $regex)) 'fewer than four military wrecks must not match'
Assert (!((Format-Result @('44', '0', '7', '9', '14', '0', '1', '1', 'complete')) -match $regex)) 'an unmixed Both layout must not match'
Assert ($fixture -match 'Resource\s+resource\s*=\s*Resource\.Load\(prefab\);' -and $fixture -match 'SpawnEntityPrefab\(resource,') 'vehicle category fixture must keep Resource.Load in a local before SpawnEntityPrefab'
Assert ($fixture -match 'Choose\(EAD_Catalog\.MILITARY, "Military"\)' -and $fixture -match 'Choose\(EAD_Catalog\.CIVILIAN, "Civilian"\)' -and $fixture -match 'Choose\(EAD_Catalog\.BOTH, "Both"\)' -and $fixture -match 'Choose\(EAD_Catalog\.MILITARY, "Military again"\)') 'vehicle category fixture must cover Military, Civilian, Both and the Military replay'
Assert ($fixture -match 'WriteVariable\(m_Editable,\s*SCR_BaseEditorAttributeVar\.CreateInt\(types\),\s*null,\s*-1\);') 'the fixture must switch through the attribute system write'
Assert ($fixture -match 'ctx\.WriteValue\("schema",\s*2\);' -and $fixture -match 'for\s*\(\s*int\s+key\s*=\s*0;\s*key\s*<\s*12;') 'the fixture must read a schema 2 payload with twelve settings'
Assert ($fixture -match 'm_Zone\.ImportSnapshot\(m_LegacyData\)') 'the fixture must import the schema 2 snapshot'

# Docs.
$gameplay = Read-Text (Join-Path $repo 'tests/GAMEPLAY.md')
Assert ($gameplay.Contains($command)) 'tests/GAMEPLAY.md must carry the vehicle category fixture command'
$readme = Read-Text (Join-Path $repo 'README.md')
Assert ($readme -match '"Vehicle types"') 'README must describe the Vehicle types setting'
$changelog = Read-Text (Join-Path $repo 'CHANGELOG.md')
$unreleased = [regex]::Match($changelog, '(?s)## (?:Unreleased|0\.1\.10)\b(.*?)(\n## |\z)').Groups[1].Value
Assert ($unreleased -match 'Vehicle types') 'CHANGELOG Unreleased must mention Vehicle types'
Assert ([regex]::Matches($changelog, '(?m)^## Unreleased').Count -le 1) 'CHANGELOG must keep at most one Unreleased header'
'PASS: Ambient Destruction vehicle types (Civilian/Military/Both, key 12, default Both): every wreck categorised as catalog data, Both keeps the original bag order and draws, schema 3 snapshots with schema 1/2 read as Both, spinbox attribute; vehicle category fixture wired.'
