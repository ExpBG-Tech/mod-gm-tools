#requires -Version 7.0
# Portable guard for the WP15 small performance fixes (0.1.15 performance plan).
#  ambient-and-small-3: the SelectFireMode "proper fire mode was not found" warning is logged
#   once per weapon prefab and requested mode (a lazily created set, at most 256 pairs), with
#   the string formatting inside that branch; the node still fails and the fire-mode
#   selection and SetSafety/SetFireMode path are unchanged.
#  ambient-and-small-8: a protester keeps a weak AIControlComponent found once at spawn;
#   Observe looks it up again only while it is unset and still re-checks IsAIActivated and
#   DeactivateAI on every tick, as before.
# No engine is launched.
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$addon = Join-Path $repo 'addon'
function Assert([bool]$Condition, [string]$Message) { if (!$Condition) { throw "FAIL: $Message" } }
# Source without // comments (neither file has // inside a string literal; asserted below).
function Read-Code([string]$Relative) {
 $text = [IO.File]::ReadAllText((Join-Path $addon $Relative))
 Assert (![regex]::IsMatch($text, '"[^"\n]*//')) "$Relative has '//' inside a string; this guard's comment stripping would misread it"
 [regex]::Replace($text, '(?m)//.*$', '')
}
# Returns @{ Start; Open; Close; Body } for the first brace block after $Signature (from $From).
function Get-Block([string]$Text, [string]$Signature, [int]$From = 0) {
 $match = [regex]::new($Signature).Match($Text, $From)
 Assert $match.Success "signature not found: $Signature"
 $open = $Text.IndexOf('{', $match.Index + $match.Length); $depth = 0
 Assert ($open -ge 0) "no block after: $Signature"
 for ($i = $open; $i -lt $Text.Length; $i++) {
  if ($Text[$i] -eq '{') { $depth++ } elseif ($Text[$i] -eq '}') { $depth--; if ($depth -eq 0) { return @{ Start = $match.Index; Open = $open; Close = $i; Body = $Text.Substring($open + 1, $i - $open - 1) } } }
 }
 throw "FAIL: unbalanced block for $Signature"
}
function Count([string]$Text, [string]$Pattern) { [regex]::Matches($Text, $Pattern).Count }

# ---------------------------------------------------------------------------------------
# ambient-and-small-3: EXPBG_AISelectFireMode.c
# ---------------------------------------------------------------------------------------
$fireRel = 'persistent-battlefield/Scripts/Game/AI/ScriptedNodes/Soldier/EXPBG_AISelectFireMode.c'
$fire = Read-Code $fireRel
Assert ([regex]::IsMatch($fire, '(?m)^\s*protected\s+static\s+ref\s+set<string>\s+s_EXPBG_FireModeWarned\s*;')) 'the warned set must be a protected static ref set<string> declared without an initializer (0.1.13 lazy statics)'
Assert ((Count $fire 's_EXPBG_FireModeWarned\s*=\s*new\b') -eq 1) 'the warned set must be created in exactly one place, on first use'
Assert (!(Count $fire 'static\s+[^;]*=\s*(new\b|\{)')) 'no static initializer with a value may be added (0.1.13 Windows compile limit)'

$method = Get-Block $fire 'override\s+ENodeResult\s+EOnTaskSimulate\s*\(\s*AIAgent\s+owner\s*,\s*float\s+dt\s*\)'
$failBlock = Get-Block $fire 'if\s*\(\s*bestFm\s*==\s*-1\s*\)' $method.Open
Assert ($failBlock.Close -lt $method.Close) 'the bestFm == -1 block must sit inside EOnTaskSimulate'
$fail = $failBlock.Body
Assert ([regex]::IsMatch($fail, 'if\s*\(\s*!\s*s_EXPBG_FireModeWarned\s*\)\s*s_EXPBG_FireModeWarned\s*=\s*new\s+set<string>\s*\(\s*\)\s*;')) 'the set must be created lazily inside the bestFm == -1 block'
$key = [regex]::Match($fail, 'int\s+(\w+)\s*=\s*m_FiremodeType\s*;\s*string\s+(\w+)\s*=\s*prefabName\s*\+\s*":"\s*\+\s*(\w+)\.ToString\(\)\s*;')
Assert ($key.Success -and $key.Groups[1].Value -ceq $key.Groups[3].Value) 'the dedupe key must be "prefab:mode" (prefabName + ":" + the requested mode as int)'
$guardSig = 'if\s*\(\s*s_EXPBG_FireModeWarned\.Count\(\)\s*<\s*(\d+)\s*&&\s*s_EXPBG_FireModeWarned\.Insert\(\s*' + [regex]::Escape($key.Groups[2].Value) + '\s*\)\s*\)'
$guard = [regex]::Match($fail, $guardSig)
Assert $guard.Success 'the warning must be gated by Count() < cap && Insert(key), with the cap checked first so a full set is never grown'
Assert ([int]$guard.Groups[1].Value -eq 256) "the dedupe cap must be 256 pairs (found $($guard.Groups[1].Value))"
$warnBlock = Get-Block $fail $guardSig
$outside = $fail.Remove($warnBlock.Open, $warnBlock.Close - $warnBlock.Open + 1)
$warn = $warnBlock.Body
Assert ($warn.Contains('string.Format("SCR_AISelectFireMode: proper fire mode was not found: %1. Weapon prefab: %2",')) 'the warning text must stay the vanilla text'
Assert ([regex]::IsMatch($warn, 'typename\.EnumToString\(\s*EWeaponFiremodeType\s*,\s*m_FiremodeType\s*\)\s*,\s*prefabName\s*\)\s*;')) 'the warning must still name the requested mode and the weapon prefab'
Assert ([regex]::IsMatch($warn, 'Print\(\s*str\s*,\s*LogLevel\.WARNING\s*\)\s*;')) 'the warning must still be a WARNING'
Assert (!(Count $outside '\bstring\.Format\s*\(|\bEnumToString\s*\(|\bPrint(Format)?\s*\(')) 'formatting and logging must only happen inside the once-per-pair branch'
Assert ((Count $fire '\bPrint(Format)?\s*\(') -eq 1) 'EXPBG_AISelectFireMode.c must keep exactly one log call (G4 pin)'
Assert ([regex]::IsMatch($outside, 'return\s+ENodeResult\.FAIL\s*;\s*$')) 'the bestFm == -1 block must still end with return ENodeResult.FAIL on every failing evaluation'
Assert ([regex]::IsMatch($outside, 'prefabName\s*=\s*weaponEntity\.GetPrefabData\(\)\.GetPrefabName\(\)\s*;')) 'the weapon prefab lookup must be unchanged'
$after = $fire.Substring($failBlock.Close + 1, $method.Close - $failBlock.Close - 1)
Assert ([regex]::IsMatch($after, '^\s*if\s*\(\s*fireModes\[bestFm\]\.GetFiremodeType\(\)\s*!=\s*EWeaponFiremodeType\.Safety\s*\)\s*controller\.SetSafety\(\s*false\s*,\s*false\s*\)\s*;\s*controller\.SetFireMode\(\s*bestFm\s*\)\s*;\s*return\s+ENodeResult\.SUCCESS\s*;\s*$')) 'the fire-mode apply path after the failure block must be unchanged'
$before = $fire.Substring($method.Open, $failBlock.Start - $method.Open)
Assert (!(Count $before 's_EXPBG_FireModeWarned')) 'the dedupe must not touch the fire-mode selection before the failure block'
$choose = (Get-Block $before 'switch\s*\(\s*m_FiremodeType\s*\)').Body
foreach ($order in @(
  @('Auto', 'autoId', 'burstId', 'semiAutoId', 'manualId'),
  @('Burst', 'burstId', 'autoId', 'semiAutoId', 'manualId'),
  @('Semiauto', 'semiAutoId', 'manualId', 'burstId', 'autoId'))) {
 $case = Get-Block $choose ('case\s+EWeaponFiremodeType\.' + $order[0] + '\s*:')
 $ids = @([regex]::Matches($case.Body, 'if\s*\(\s*(\w+)\s*!=\s*-1\s*\)\s*bestFm\s*=\s*\1\s*;') | ForEach-Object { $_.Groups[1].Value })
 Assert (($ids -join ',') -ceq ($order[1..4] -join ',')) "$($order[0]) fire-mode preference changed: $($ids -join ' > ')"
}
$safety = Get-Block $choose 'case\s+EWeaponFiremodeType\.Safety\s*:'
Assert ([regex]::IsMatch($safety.Body, '^\s*if\s*\(\s*safetyId\s*!=\s*-1\s*\)\s*bestFm\s*=\s*safetyId\s*;\s*break\s*;\s*$')) 'Safety must still only select a safety mode'

# Model of the gate as written (cap first, then Insert): every pair logs once, nothing past the cap.
$cap = [int]$guard.Groups[1].Value
$warned = [Collections.Generic.HashSet[string]]::new(); $logged = 0
foreach ($pass in 1..3) { foreach ($n in 1..300) { if ($warned.Count -lt $cap -and $warned.Add("prefab$($n):2")) { $logged++ } } }
Assert ($logged -eq $cap -and $warned.Count -eq $cap) "the gate model must log each pair once up to the cap (logged $logged)"

# ---------------------------------------------------------------------------------------
# ambient-and-small-8: EAU_ProtestZone.c
# ---------------------------------------------------------------------------------------
$zoneRel = 'ambient-unrest/Scripts/Game/EXPAU/EAU_ProtestZone.c'
$zone = Read-Code $zoneRel
$protester = Get-Block $zone '\bclass\s+EAU_Protester\s*(?=\{)'
Assert ([regex]::IsMatch($protester.Body, '(?m)^\s*AIControlComponent\s+Control\s*;')) 'EAU_Protester must hold a weak (non-ref) AIControlComponent Control'
Assert (!(Count $protester.Body '\bref\s+AIControlComponent\b')) 'the cached AIControlComponent must stay weak'
Assert ((Count $zone '\.Control\s*=') -eq 2) 'Control may only be set at spawn and by the Observe fallback'

$spawn = Get-Block $zone 'protected\s+void\s+SpawnProtester\s*\('
$spawnLookup = [regex]::Match($spawn.Body, 'AIControlComponent\s+control\s*=\s*AIControlComponent\.Cast\(\s*actor\.FindComponent\(\s*AIControlComponent\s*\)\s*\)\s*;\s*member\.Control\s*=\s*control\s*;\s*if\s*\(\s*control\s*\)')
Assert $spawnLookup.Success 'SpawnProtester must store the AIControlComponent it already looks up, before using it'
Assert ($spawn.Body.IndexOf('EAU_Protester member = new EAU_Protester();') -ge 0 -and $spawn.Body.IndexOf('EAU_Protester member = new EAU_Protester();') -lt $spawnLookup.Index) 'the member must exist before its Control is stored'
Assert ((Count $spawn.Body 'FindComponent\(\s*AIControlComponent\s*\)') -eq 1) 'SpawnProtester must keep a single AIControlComponent lookup'

$observe = Get-Block $zone 'protected\s+void\s+Observe\s*\(\s*float\s+now\s*\)'
$cached = [regex]::Match($observe.Body, 'AIControlComponent\s+control\s*=\s*member\.Control\s*;\s*if\s*\(\s*!\s*control\s*\)\s*\{\s*control\s*=\s*AIControlComponent\.Cast\(\s*actor\.FindComponent\(\s*AIControlComponent\s*\)\s*\)\s*;\s*member\.Control\s*=\s*control\s*;\s*\}\s*if\s*\(\s*control\s*&&\s*control\.IsAIActivated\(\)\s*\)\s*control\.DeactivateAI\(\)\s*;')
Assert $cached.Success 'Observe must use member.Control, look it up again only while unset, and keep the per-tick IsAIActivated/DeactivateAI re-check'
Assert ((Count $observe.Body 'FindComponent\(\s*AIControlComponent\s*\)') -eq 1) 'Observe may only look the AIControlComponent up in the unset fallback'
Assert ($observe.Body.IndexOf('if (!actor) { m_Members.Remove(i); continue; }') -ge 0 -and $observe.Body.IndexOf('if (!actor) { m_Members.Remove(i); continue; }') -lt $cached.Index) 'Observe must still drop deleted actors before it touches their cached control'
Assert ([regex]::IsMatch($observe.Body, 'if\s*\(\s*m_Group\s*&&\s*m_Group\.IsAIActivated\(\)\s*\)\s*m_Group\.DeactivateAI\(\)\s*;')) 'Observe must still re-check the crowd group every tick'
$release = Get-Block $zone 'protected\s+void\s+ReleaseToGameMaster\s*\('
Assert ([regex]::IsMatch($release.Body, 'AIControlComponent\s+control\s*=\s*AIControlComponent\.Cast\(\s*actor\.FindComponent\(\s*AIControlComponent\s*\)\s*\)\s*;\s*if\s*\(\s*control\s*&&\s*!\s*control\.IsAIActivated\(\)\s*\)\s*control\.ActivateAI\(\)\s*;')) 'ReleaseToGameMaster must be unchanged'

"PASS: SelectFireMode warns once per prefab and mode (cap $cap, formatting inside the branch, FAIL and selection unchanged); protest Observe uses the spawn-cached AIControlComponent with a lookup fallback and the same per-tick DeactivateAI re-check. Native engines were not launched."
