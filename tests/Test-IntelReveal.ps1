#requires -Version 7.0
# Portable guard for the AI Surrender intel reveal (Unreleased): GM setting
# "Interrogation: reveal intel items (%)" (key 12, default 30, slider 0-100 step 5),
# replicated, mirrored and saved like the others (esrVersion 3 with thirteen values;
# esrVersion 1 and 2 saves still load with the default). Rolled once on its own, with the
# prisoner's first answer other than a refusal, after the unchanged squad/identity roll.
# One bounded pass over the EXPBG Intel Items registry within the reveal search radius:
# unclaimed items only (computer startup token not spent, not carried by a player or by
# the prisoner), nearest first, at most three; one squad-style marker each ("Intel
# (interrogation)", marker lifetime setting) and a dialog line with the same 25 m and
# compass helpers. Also checks tests/ESR_IntelRevealGameplay.c is wired to the runner and
# its RESULT line matches its -ExpectResult regex. No engine is launched.
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$module = Join-Path $repo 'addon/ai-surrender'
$scripts = Join-Path $module 'Scripts/Game/EXPSR'
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

$settingsPath = Join-Path $scripts 'ESR_Settings.c'
$modulePath = Join-Path $scripts 'ESR_SurrenderModule.c'
$attributesPath = Join-Path $scripts 'ESR_Attributes.c'
$managerPath = Join-Path $scripts 'ESR_SurrenderManager.c'
$pointPath = Join-Path $scripts 'ESR_InterrogationPoint.c'
$listPath = Join-Path $module 'Configs/Editor/AttributeLists/Edit.conf'
$intelPath = Join-Path $repo 'addon/intel-items/Scripts/Game/EXPII/EII_IntelComponent.c'
$fixturePath = Join-Path $repo 'tests/ESR_IntelRevealGameplay.c'
$settings = Read-Text $settingsPath
$moduleText = Read-Text $modulePath
$attributes = Read-Text $attributesPath
$manager = Read-Text $managerPath
$point = Read-Text $pointPath
$list = Read-Text $listPath
$intel = Read-Text $intelPath
$fixture = Read-Text $fixturePath

# Settings: key 12 appended, thirteen in all, default 30, a 0-100 slider (not a switch).
Assert ($settings -match 'static const int INTEL = 12;' -and $settings -match 'static const int COUNT = 13;') 'intel must be key 12 of thirteen settings'
Assert ($settings -match 'static const int COUNT_V1 = 10;' -and $settings -match 'static const int COUNT_V2 = 12;') 'esrVersion 1 holds ten values, esrVersion 2 twelve'
$default = Get-Body $settings 'static\s+int\s+Default\s*\(\s*int\s+key\s*\)'
Assert ($default -match 'if \(key == INTEL\) return 30;') 'intel chance defaults to 30%'
Assert ($default -match 'if \(key == REVEAL\) return 40;' -and $default -match 'if \(key == IDENTITY\) return 40;' -and $default -match 'if \(key == RADIUS\) return 1000;') 'existing interrogation defaults unchanged'
Assert (!((Get-Body $settings 'static\s+int\s+Maximum\s*\(') -match 'INTEL') -and !((Get-Body $settings 'static\s+int\s+Minimum\s*\(') -match 'INTEL')) 'intel chance uses the 0-100 range'
Assert (!($settings -match 'static bool IsBoolean\(int key\) \{[^}]*INTEL')) 'intel chance must not be a switch'

# Module: replicated attribute, read, mirrored and saved like the others.
Assert ($moduleText -match '\[Attribute\("30", UIWidgets\.Slider, "Interrogation: reveal intel items \(%\)", "0 100 5", category: "EXPBG AI Surrender"\), RplProp\(\)\]\s*protected int m_iIntel;') 'module intel attribute must be a replicated 0-100 step 5 slider defaulting to 30'
Assert ($moduleText.IndexOf('protected int m_iIdentity;') -lt $moduleText.IndexOf('protected int m_iIntel;') -and $moduleText.IndexOf('protected int m_iIntel;') -lt $moduleText.IndexOf('protected int m_iRadius;')) 'module intel attribute sits with the interrogation settings'
Assert ((Get-Body $moduleText 'int\s+GetSetting\s*\(\s*int\s+key\s*\)') -match 'if \(key == ESR_Settings\.INTEL\) return m_iIntel;') 'GetSetting must read the intel chance'
$mirror = Get-Body $moduleText 'protected\s+void\s+Mirror\s*\(\s*\)'
Assert ($mirror -match 'int intel = ESR_Settings\.Get\(ESR_Settings\.INTEL\);' -and $mirror -match 'm_iIntel == intel\) return;' -and $mirror -match 'm_iIntel = intel;' -and $mirror -match 'Replication\.BumpMe\(\);') 'Mirror must replicate the intel chance'
Assert ((Get-Body $moduleText 'bool\s+RestoreSettings\s*\(') -match 'values\.Count\(\) < ESR_Settings\.COUNT_V1 \|\| values\.Count\(\) > ESR_Settings\.COUNT\)') 'RestoreSettings must accept ten- to thirteen-value sets'
$serialize = Get-Body $moduleText 'override\s+protected\s+ESerializeResult\s+Serialize\s*\('
Assert ($serialize -match 'for \(int key = 0; key < ESR_Settings\.COUNT; key\+\+\) settings\.Insert\(module\.GetSetting\(key\)\);' -and $serialize -match 'context\.WriteValue\("esrVersion", 3\)') 'native saves must write all settings as esrVersion 3'
$deserialize = Get-Body $moduleText 'override\s+protected\s+bool\s+Deserialize\s*\('
Assert ($deserialize -match 'version == 1 && settings\.Count\(\) == ESR_Settings\.COUNT_V1;' -and $deserialize -match 'version == 2 && settings\.Count\(\) == ESR_Settings\.COUNT_V2;' -and $deserialize -match 'version == 3 && settings\.Count\(\) == ESR_Settings\.COUNT;' -and $deserialize -match 'if \(!savedV1 && !savedV2 && !savedV3\) return false;') 'native loads must accept esrVersion 1, 2 and 3'
Assert ((Get-Body $settings 'static\s+void\s+Adopt\s*\(') -match 'if \(values\.IsIndexValid\(key\)\) value = values\[key\];') 'older saves fill the intel chance with its default'

# GM attribute: its own class (CDF keys attributes by class name), placed after identity.
Assert ($attributes -match '\[BaseContainerProps\(\), SCR_BaseEditorAttributeCustomTitle\(\)\]\s*class ESR_IntelChanceAttribute : ESR_Attribute \{\}') 'attribute class ESR_IntelChanceAttribute must exist'
$order = [regex]::Matches($list, '(?m)^  (ESR_\w+Attribute) \{') | ForEach-Object { $_.Groups[1].Value }
$identityAt = [array]::IndexOf([string[]]$order, 'ESR_IdentityChanceAttribute')
Assert ($identityAt -ge 0 -and $order[$identityAt + 1] -eq 'ESR_IntelChanceAttribute' -and $order[$identityAt + 2] -eq 'ESR_RevealRadiusAttribute') "intel attribute must follow the identity chance: $($order -join ',')"
Assert (($order | Where-Object { $_ -eq 'ESR_IntelChanceAttribute' }).Count -eq 1) 'intel attribute listed once'
$entry = [regex]::Match($list, '(?s)ESR_IntelChanceAttribute \{(.*?)\n  \}').Groups[1].Value
Assert ($entry -match 'm_Key 12' -and $entry -match 'Name "Interrogation: reveal intel items \(%\)"' -and $entry -match 'AttributePrefab_Slider\.layout' -and $entry -match 'm_fMin 0 m_fMax 100 m_fStep 5 m_iDecimals 0' -and $entry -match 'EXPBG_Surrender\.conf') 'intel attribute entry must be key 12, slider 0-100 step 5'
$radiusEntry = [regex]::Match($list, '(?s)ESR_RevealRadiusAttribute \{(.*?)\n  \}').Groups[1].Value
Assert ($radiusEntry -match 'intel items') 'the reveal search radius description must say it also bounds intel items'

# Interrogation: the squad/identity roll is unchanged; intel is rolled after it, once.
$interrogate = Get-Body $manager 'static\s+int\s+Interrogate\s*\('
Assert ($interrogate -match '(?s)float roll = Math\.RandomFloat\(0, 100\);\s*if \(roll < reveal\) outcome = Reveal\(prisoner, user, playerId\);\s*else if \(roll < Math\.Min\(100, reveal \+ identity\)\) outcome = OUTCOME_IDENTITY;\s*else outcome = OUTCOME_REFUSED;') 'the squad and identity roll must stay as it was'
Assert ($interrogate -match '(?s)if \(outcome != OUTCOME_REFUSED\)\s*\{\s*prisoner\.Outcome = outcome;\s*//[^\n]*\n\s*RevealIntel\(prisoner, user, playerId\);\s*\}') 'intel is rolled only with an answer other than a refusal, after the answer is fixed'
Assert (([regex]::Matches($interrogate, 'RevealIntel\(')).Count -eq 1) 'one intel call per interrogation'
Assert ($interrogate -match 'point\.SendResult\(playerId, outcome, prisoner\.RevealCount, prisoner\.RevealDistance, prisoner\.RevealBearing, left, prisoner\.IntelDistances, prisoner\.IntelBearings\);') 'SendResult must carry the stored intel items'
$reveal = Get-Body $manager 'protected\s+static\s+void\s+RevealIntel\s*\('
Assert ($reveal -match 'if \(prisoner\.IntelRolled \|\| !prisoner\.Character\) return;\s*prisoner\.IntelRolled = true;') 'the intel chance is rolled at most once per prisoner'
Assert ($reveal -match 'ESR_Settings\.Get\(ESR_Settings\.INTEL\)' -and $reveal -match 'if \(chance <= 0\) return;' -and $reveal -match 'if \(chance < 100 && Math\.RandomFloat\(0, 100\) >= chance\)') 'intel roll: skipped at 0%, certain at 100%, never an empty range'
Assert ($reveal -match 'ESR_Settings\.Get\(ESR_Settings\.RADIUS\)' -and $reveal -match 'query\.Collect\(origin, radius, prisoner\.Character\);') 'intel search uses the reveal search radius and excludes the prisoner'
Assert ($reveal -match 'ReportedDistance\(origin, position\)' -and $reveal -match 'ReportedBearing\(origin, position\)' -and $reveal -match 'PlaceIntelMarker\(position, user, playerId\)' -and $reveal -match 'Trace\(') 'each item: 25 m distance, compass sector, marker, diagnostics'
$revealSquad = Get-Body $manager 'protected\s+static\s+int\s+Reveal\s*\('
Assert ($revealSquad -match 'ReportedDistance\(origin, center\)' -and $revealSquad -match 'ReportedBearing\(origin, center\)') 'the squad reveal uses the same distance and bearing helpers'
Assert ((Get-Body $manager 'static\s+int\s+ReportedDistance\s*\(') -match 'Math\.Round\(vector\.DistanceXZ\(from, to\) / 25\);\s*return rounded \* 25;') '25 m rounding'
Assert ((Get-Body $manager 'static\s+int\s+ReportedBearing\s*\(') -match '(?s)Math\.Atan2\(offset\[0\], offset\[2\]\).*Math\.Round\(angle / 45\);\s*return sector % 8;') '8-way bearing'

# Markers: the squad marker's mechanism, owner, faction and lifetime.
Assert ($manager -match 'static const string INTEL_MARKER_TEXT = "Intel \(interrogation\)";') 'intel marker title'
$placeIntel = Get-Body $manager 'protected\s+static\s+int\s+PlaceIntelMarker\s*\('
Assert ($placeIntel -match 'SCR_EMapMarkerType\.PLACED_CUSTOM' -and $placeIntel -match 'return PublishMarker\(markers, marker, position, INTEL_MARKER_TEXT, user, playerId\);') 'intel markers are published like the squad marker'
Assert ((Get-Body $manager 'protected\s+static\s+int\s+PlaceMarker\s*\(') -match 'return PublishMarker\(markers, marker, center,') 'the squad marker goes through the same publisher'
$publish = Get-Body $manager 'protected\s+static\s+int\s+PublishMarker\s*\('
Assert ($publish -match 'ViewerFaction\(user, playerId\)' -and $publish -match 'AddMarkerFactionFlags' -and $publish -match 'SetMarkerOwnerID\(ownerId\)' -and $publish -match 'OnAskAddStaticMarker\(marker\)' -and $publish -match 'ESR_Settings\.Get\(ESR_Settings\.LIFETIME\)' -and $publish -match 'ESR_SurrenderManager\.RemoveMarker') 'publisher: interrogator faction, owner, static marker, lifetime removal'

# Query: bounded registry pass, unclaimed items, nearest first, at most three.
$queryClass = Get-Body $manager 'class\s+ESR_IntelQuery\s*\r?\n'
Assert ($queryClass -match 'static const int MAX_ITEMS = 3;' -and $queryClass -match 'static const int MAX_SCAN = \d+;' -and $queryClass -match 'static const int MAX_DEPTH = \d+;') 'query caps: three items, bounded scan and holder depth'
$collect = Get-Body $queryClass 'void\s+Collect\s*\('
Assert ($collect -match 'EII_IntelComponent\.GetRegistered\(items\)' -and $collect -match 'if \(total > MAX_SCAN\) total = MAX_SCAN;') 'one capped pass over the Intel Items registry'
Assert ($collect -match 'if \(!intel \|\| intel\.HasStarted\(\)\) continue;') 'spent computer items are excluded'
Assert ($collect -match 'vector\.DistanceSqXZ\(origin, position\)' -and $collect -match 'if \(distanceSq > radiusSq\) continue;' -and $collect -match 'if \(slot >= MAX_ITEMS\) continue;' -and $collect -match 'RemoveOrdered\(MAX_ITEMS\)') 'radius, nearest-first order and the three-item cap'
Assert ($collect -match 'Holder\(item, prisoner\)' -and $collect -match 'vector position = holder\.GetOrigin\(\);') 'items in containers and bodies are placed at their outermost holder'
$holder = Get-Body $queryClass 'protected\s+static\s+IEntity\s+Holder\s*\('
Assert ($holder -match 'while \(parent && depth < MAX_DEPTH\)' -and $holder -match 'if \(parent == prisoner\) return null;' -and $holder -match 'ESR_SurrenderManager\.IsPlayerCharacter\(parent\)\) return null;') 'items carried by a player or by the prisoner are excluded'
Assert (![regex]::IsMatch($queryClass, '(?m)^[ \t]*(static[ \t]+)?(int|vector|bool|float|string|IEntity)[ \t]+\w+[ \t]*\([^)\n]*\)[ \t]*\{[ \t]*return')) 'non-void returns of the query stay on separate lines'

# Intel Items: read-only registry of items in play (server).
Assert ($intel -match 'protected static ref array<IEntity> s_aRegistered = \{\};') 'Intel Items registry'
$postInit = Get-Body $intel 'override\s+void\s+OnPostInit\s*\('
Assert ($postInit -match '(?s)if \(!IsAuthority\(\)\) return;\s*s_aRegistered\.Insert\(owner\);') 'items register on the server once in play'
Assert ((Get-Body $intel 'override\s+void\s+OnDelete\s*\(') -match 's_aRegistered\.RemoveItem\(owner\);') 'deleted items leave the registry'
$registered = Get-Body $intel 'static\s+int\s+GetRegistered\s*\('
Assert ($registered -match 'outItems\.Copy\(s_aRegistered\);' -and $registered -match 'if \(!s_aRegistered\[i\]\) s_aRegistered\.Remove\(i\);') 'GetRegistered copies the live items'
Assert (!((Get-Body $intel 'void\s+TryStartup\s*\(') -match 's_aRegistered')) 'the registry never changes item state'

# Dialog: SendResult/RPC carry the lists; one line lists the items.
Assert ($point -match 'void SendResult\(int playerId, int outcome, int count, int distance, int bearing, int attemptsLeft, notnull array<int> intelDistances, notnull array<int> intelBearings\)') 'SendResult signature carries the intel lists'
Assert ($point -match 'protected void RpcDo_Result\(int playerId, int outcome, int count, int distance, int bearing, int attemptsLeft, array<int> intelDistances, array<int> intelBearings\)' -and $point -match 'Describe\(outcome, count, distance, bearing, attemptsLeft, intelDistances, intelBearings\)') 'the RPC hands the lists to the dialog text'
$intelText = Get-Body $point 'static\s+string\s+IntelText\s*\('
Assert ($intelText -match 'string\.Format\("He also points out %1 intel items: %2\. Each is marked on your map\.", count, places\)' -and $intelText -match '"He also points out an intel item: " \+ places \+ "\. It is marked on your map\."') 'intel dialog line'
$place = Get-Body $point 'protected\s+static\s+string\s+IntelPlace\s*\('
Assert ($place -match 'if \(distance <= 0\) return "a few metres " \+ Compass\(bearing\);' -and $place -match 'string\.Format\("about %1 m %2", distance, Compass\(bearing\)\)') 'each place in 25 m steps and compass words'
$describe = Get-Body $point 'string\s+Describe\s*\('
Assert ($describe -match 'IdentityText\(\) \+ intel;' -and $describe -match 'Compass\(bearing\)\) \+ intel;') 'the intel line follows the squad or identity answer'
Assert ($point -match 'string Describe\(int outcome, int count, int distance, int bearing, int attemptsLeft, array<int> intelDistances = null, array<int> intelBearings = null\)') 'older Describe callers keep compiling'

# Enforce gotchas in the touched sources and the fixture; ASCII with LF.
foreach ($path in @($settingsPath, $modulePath, $attributesPath, $managerPath, $pointPath, $intelPath, $fixturePath, $PSCommandPath)) {
 $text = Read-Text $path
 if ($path -ne $PSCommandPath) {
  Assert (![regex]::IsMatch($text, '\b(int|float|bool|string|vector|auto|IEntity|ResourceName)\s+(owned|Sleep)\b')) "reserved Enforce name used as a variable in $path"
  Assert (![regex]::IsMatch($text, 'Math\.RandomFloat\(\s*0\s*,\s*0\s*\)')) "Math.RandomFloat(0, 0) logs an engine error: $path"
 }
 $bytes = [IO.File]::ReadAllBytes($path)
 Assert (($bytes | Where-Object { $_ -gt 127 }).Count -eq 0) "non-ASCII byte in $path"
 Assert (!$text.Contains("`r")) "CR line ending in $path"
}

# Native fixture wiring (executed only by the orchestrator through Run-Gameplay).
Assert ($fixture -match 'class\s+EXPG_GarrisonGameplayClass\s*:\s*GenericEntityClass' -and $fixture -match 'class\s+EXPG_GarrisonGameplay\s*:\s*GenericEntity') 'intel fixture must keep the runner driver class names'
$regex = '\[ESR INTEL RESULT\] checks=[1-9]\d* failures=0 listed=3 markers=3 refused=1 chanceZero=1 reason=complete'
Assert ($fixture.Contains("-FixturePath tests/ESR_IntelRevealGameplay.c -TimeoutSeconds 300 -OrchestratorSlotGranted -ExpectResult '$regex'")) 'intel fixture header must carry its runner command'
$format = [regex]::Match($fixture, 'PrintFormat\("(\[ESR INTEL RESULT\][^"]*)"').Groups[1].Value
Assert ($format -eq '[ESR INTEL RESULT] checks=%1 failures=%2 listed=%3 markers=%4 refused=%5 chanceZero=%6 reason=%7') "intel fixture RESULT format changed: $format"
function Fill([string]$Format, [string[]]$Values) { $line = $Format; for ($n = $Values.Count; $n -ge 1; $n--) { $line = $line.Replace("%$n", $Values[$n - 1]) }; $line }
Assert ((Fill $format @('37', '0', '3', '3', '1', '1', 'complete')) -match $regex) 'intel fixture RESULT line must match its -ExpectResult regex'
Assert (!((Fill $format @('37', '1', '3', '3', '1', '1', 'complete')) -match $regex)) 'a failing RESULT line must not match the regex'
Assert (!((Fill $format @('20', '0', '2', '2', '1', '1', 'complete')) -match $regex)) 'a short list must not match the regex'
Assert (!((Fill $format @('20', '0', '3', '3', '1', '0', 'complete')) -match $regex)) 'a failed 0% case must not match the regex'
Assert ($fixture -match 'Resource modulePrefab = Resource\.Load\(MODULE\);' -and $fixture -match 'Resource squad = Resource\.Load\(SQUAD\);' -and $fixture -match 'Resource item = Resource\.Load\(prefab\);') 'intel fixture must keep Resource.Load results in locals'
Assert ($fixture -match 'ESR_SurrenderManager\.Surrender\(FirstCaptive, Squad\)' -and $fixture -match 'ESR_SurrenderManager\.Interrogate\(FirstPrisoner\.Point, FirstPrisoner\.Point, 0\)') 'intel fixture uses the production Surrender and Interrogate calls'
Assert ($fixture -match 'Configure\(100, 100\);' -and $fixture -match 'Configure\(0, 100\);' -and $fixture -match 'Configure\(100, 0\);') 'intel fixture covers 100%, refusal and 0%'
Assert ($fixture -match 'spent\.TryStartup\(' -and $fixture -match 'GiveIntel\(FirstCaptive, NOTEBOOK_ORANGE\)' -and $fixture -match 'OutsideOffset = "330 0 0"' -and $fixture -match 'RADIUS = 300;') 'intel fixture covers spent, prisoner-carried and out-of-radius items'
# The fixture's expected line must follow production's wording.
Assert ($fixture -match '"He also points out 3 intel items: "' -and $fixture -match '"\. Each is marked on your map\."' -and $fixture -match '"a few metres " \+ CompassName\(sector\)' -and $fixture -match 'string\.Format\("about %1 m %2", distance, CompassName\(sector\)\)') 'fixture expectation matches the production dialog wording'
Assert ($fixture -match 'static const string MARKER_TEXT = "Intel \(interrogation\)";') 'fixture marker title matches production'
'PASS: AI Surrender intel reveal setting (key 12, default 30%) replicated and saved as esrVersion 3 with esrVersion 1/2 compatibility, independent once-only roll after the unchanged answer roll, bounded Intel Items registry query (unclaimed, nearest three), squad-style markers and dialog line; intel fixture wired.'
