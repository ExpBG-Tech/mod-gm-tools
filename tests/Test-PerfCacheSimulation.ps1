#requires -Version 7.0
# Portable guard for the 0.1.15 unit-caching leaf-file performance work (plan WP2):
#  1. Simulation part owner index: EBG_FindSimulationPartOwner answers from an EntityID
#     index kept at every part create, move and removal, with the old full scan kept as
#     the EBG_DebugChecks cross-check, instead of scanning every cached owner and part
#     on every inventory event.
#  2. UnsupportedRHSDevices names a component only after it is an RHS device, and
#     EBG_OptionalScalar.FieldIndex remembers each (class, field) answer.
#  3. EBG_FullSaveGate reads the loaded addons once per world, not every Tick and Poll.
#  4. Rail, bayonet and cloth-blade sampling run from one shared 250 ms sampler per kind
#     (a quarter of the registry per step, each entry once per four steps) instead of one
#     1 s timer per component for its whole life.
# No engine is launched.
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$scripts = Join-Path $repo 'addon/unit-caching/Scripts/Game/EXPBG'
function Assert([bool]$Condition, [string]$Message) { if (!$Condition) { throw "FAIL: $Message" } }
function Read-Text([string]$Path) { [IO.File]::ReadAllText($Path) }
# Comments blanked (same length and lines); string contents kept.
function Get-Code([string]$Text) {
 [regex]::Replace($Text, '"(?:\\.|[^"\\\r\n])*"|//[^\r\n]*|/\*[\s\S]*?\*/', [Text.RegularExpressions.MatchEvaluator]{
  param($m)
  if ($m.Value[0] -eq '"') { return $m.Value }
  return [regex]::Replace($m.Value, '[^\r\n]', ' ')
 })
}
function Get-Body([string]$Text, [string]$Signature) {
 $match = [regex]::Match($Text, $Signature)
 Assert $match.Success "signature not found: $Signature"
 $open = $Text.IndexOf('{', $match.Index); $depth = 0
 for ($i = $open; $i -lt $Text.Length; $i++) {
  if ($Text[$i] -eq '{') { $depth++ } elseif ($Text[$i] -eq '}') { $depth--; if ($depth -eq 0) { return $Text.Substring($open + 1, $i - $open - 1) } }
 }
 throw "FAIL: unbalanced body for $Signature"
}
function Get-Ordered([string]$Text, [string[]]$Needles, [string]$What) {
 $at = -1
 foreach ($needle in $Needles) {
  $next = $Text.IndexOf($needle, $at + 1)
  Assert ($next -gt $at) "$What must contain, in order: $($Needles -join ' -> ') (missing or out of order: $needle)"
  $at = $next
 }
}
$files = [ordered]@{}
foreach ($name in 'EBG_SimulationCache.c', 'EBG_OptionalModState.c', 'EBG_FullSaveGate.c', 'EBG_RHSWeaponRails.c', 'EBG_BayonetState.c') {
 $files[$name] = Get-Code (Read-Text (Join-Path $scripts $name))
}

# 1. Simulation part owner index.
$simulation = $files['EBG_SimulationCache.c']
$part = Get-Body $simulation 'class\s+EBG_SimulationPart\s*\{'
Assert ($part -match '\bSCR_ChimeraCharacter\s+Owner;' -and $part -match '\bEntityID\s+Key;') 'EBG_SimulationPart must carry its weak Owner and its index Key'
Assert ($simulation -match 'protected\s+static\s+ref\s+map<EntityID,\s*EBG_SimulationPart>\s+s_EBG_PartIndex;') 'the part index must be a lazy static map with weak part values'
$lazy = Get-Body $simulation 'protected\s+static\s+void\s+EXPBG_LazyStatics_SCR_ChimeraCharacter\s*\('
Assert ($lazy -match 'if\s*\(!s_EBG_PartIndex\)\s*s_EBG_PartIndex\s*=\s*new\s+map<EntityID,\s*EBG_SimulationPart>\(\);') 'the part index must be created on first use'
$find = Get-Body $simulation 'static\s+SCR_ChimeraCharacter\s+EBG_FindSimulationPartOwner\s*\('
Assert $find.Contains('s_EBG_PartIndex.Get(') 'EBG_FindSimulationPartOwner must answer from the part index'
Assert (!$find.Contains('foreach') -and !$find.Contains('for (')) 'EBG_FindSimulationPartOwner must not loop over owners or parts'
Assert ($find -match 'part\.Entity\s*!=\s*entity\s*\|\|\s*!holder\s*\|\|\s*!holder\.m_EBG_SimulationParts\s*\|\|\s*!holder\.m_EBG_SimulationParts\.Contains\(part\)') 'a part found by id must still hold that entity and sit in its owner''s snapshot'
Assert ($find -match 'else\s+if\s*\(holder\.m_EBG_SimulationListed\)') 'only owners in s_EBG_SimulationOwners are returned, as the old scan did'
Assert ($find -match '(?s)if\s*\(EBG_DebugChecks\.Enabled\).*EBG_ScanSimulationPartOwner\(entity\).*EBG_DebugChecks\.Mismatch\(') 'the old full scan must stay as the debug cross-check'
$scan = Get-Body $simulation 'protected\s+static\s+SCR_ChimeraCharacter\s+EBG_ScanSimulationPartOwner\s*\('
Assert ($scan -match 's_EBG_SimulationOwners\.Count\(\)\s*-\s*1;\s*i\s*>=\s*0;\s*i--' -and $scan -match 'part\.Entity\s*==\s*entity') 'the cross-check scan must be the old reverse owner and part scan'
Assert (!$scan.Contains('.Remove(')) 'the cross-check scan must not prune (debug runs must not change state)'
# Every part is created at one site, indexed there, and every insert names its owner.
Assert ([regex]::Matches($simulation, 'new\s+EBG_SimulationPart\(\)').Count -eq 1) 'EBG_SimulationPart must keep its single creation site'
$capture = Get-Body $simulation 'protected\s+void\s+EBG_CaptureSimulationTreeIndexed\s*\('
Assert ($capture -match 'new\s+EBG_SimulationPart\(\);\s*part\.Capture\(entity\);\s*EBG_IndexSimulationPart\(part\);') 'a new part must be indexed where it is created'
Get-Ordered $capture @('EBG_FindSimulationPartOwner(entity)', 'EBG_TakeSimulationPart(entity)', 'part.Owner = this;', 'm_EBG_SimulationParts.Insert(part);', 'if (first) EBG_ListSimulationOwner(this);') 'capture'
$reconcile = Get-Body $simulation 'protected\s+void\s+EBG_ReconcileSimulationParts\s*\('
Assert ($reconcile -match 'part\.Entity\.IsDeleted\(\)\)\s*\{\s*m_EBG_SimulationParts\.Remove\(i\);\s*EBG_UnindexSimulationPart\(part\);\s*continue;\s*\}') 'a deleted part must leave the index with its snapshot'
Get-Ordered $reconcile @('part.Owner = holder;', 'holder.m_EBG_SimulationParts.Insert(part);', 'EBG_ListSimulationOwner(holder);', 'else', 'EBG_UnindexSimulationPart(part);', 'part.RestoreReleased();') 'reconcile'
$state = Get-Body $simulation 'protected\s+void\s+EBG_OnSimulationState\s*\('
Get-Ordered $state @('part.Restore();', 'EBG_UnindexSimulationPart(released);', 'm_EBG_SimulationParts = null;', 'EBG_UnlistSimulationOwner(this);') 'uncaching'
$destructor = Get-Body $simulation 'void\s+~SCR_ChimeraCharacter\s*\('
Assert ($destructor -match 'EBG_UnlistSimulationOwner\(this\);' -and $destructor -match 'EBG_UnindexSimulationPart\(part\);') 'a destroyed owner must leave the owner list and drop its parts'' keys'
Assert ((Get-Body $simulation 'protected\s+static\s+void\s+EBG_UnindexSimulationPart\s*\(') -match 's_EBG_PartIndex\.Get\(part\.Key\)\s*==\s*part') 'unindexing must only drop a key that still names that part'
# The listed flag mirrors s_EBG_SimulationOwners at every change of the list.
$listChanges = [regex]::Matches($simulation, 's_EBG_SimulationOwners\.(Insert|Remove|RemoveItem|Clear)\(').Count
Assert ($listChanges -eq 4) "s_EBG_SimulationOwners must change only in EBG_ListSimulationOwner, EBG_UnlistSimulationOwner and the world cleanup (found $listChanges sites)"
$list = Get-Body $simulation 'protected\s+static\s+void\s+EBG_ListSimulationOwner\s*\('
Assert ($list -match 'stale\.m_EBG_SimulationListed\s*=\s*false;\s*s_EBG_SimulationOwners\.Remove\(s_EBG_OwnerPruneCursor\);' -and $list -match 'owner\.m_EBG_SimulationListed\s*=\s*true;\s*s_EBG_SimulationOwners\.Insert\(owner\);') 'owner insert and round-robin pruning must keep the listed flag'
Assert ((Get-Body $simulation 'protected\s+static\s+void\s+EBG_UnlistSimulationOwner\s*\(') -match 'RemoveItem\(owner\);\s*if\s*\(owner\)\s*owner\.m_EBG_SimulationListed\s*=\s*false;') 'unlisting must clear the listed flag'
Assert ((Get-Body $simulation 'static\s+void\s+EBG_ClearSimulationOwnersForWorldCleanup\s*\(') -match '(?s)listed\.m_EBG_SimulationListed\s*=\s*false;.*s_EBG_SimulationOwners\.Clear\(\);') 'world cleanup must clear the listed flags with the list'

# 2. RHS device names and the optional field index memo.
$rhs = Get-Body $simulation 'static\s+string\s+UnsupportedRHSDevices\s*\('
Get-Ordered $rhs @('IsInherited(rhsDevice)) continue;', 'string type = component.Type().ToString();', '"IsTurnedOn"') 'UnsupportedRHSDevices'
Assert ([regex]::Matches($rhs, 'Type\(\)\.ToString\(\)').Count -eq 1) 'UnsupportedRHSDevices must build the type name once, for RHS devices only'
$optional = $files['EBG_OptionalModState.c']
$fieldIndex = Get-Body $optional 'static\s+int\s+FieldIndex\s*\('
Get-Ordered $fieldIndex @('type.ToString() + ":" + field', 's_EBG_FieldIndexes.Find(key, index)', 'ScanFieldIndex(type, field)', 's_EBG_FieldIndexes.Insert(key, index);', 'EBG_DebugChecks.Enabled', 'EBG_DebugChecks.Mismatch(') 'FieldIndex'
Assert (!$fieldIndex.Contains('GetVariableName')) 'FieldIndex must not scan the variables itself on a remembered answer'
$scanField = Get-Body $optional 'protected\s+static\s+int\s+ScanFieldIndex\s*\('
Assert ($scanField -match 'count\s*>\s*2048' -and $scanField -match 'GetVariableName\(i\)\s*==\s*field') 'ScanFieldIndex must keep the 2048-variable bound and the first-match scan'
Assert ($optional -match 'protected\s+static\s+ref\s+map<string,\s*int>\s+s_EBG_FieldIndexes;' -and (Get-Body $optional 'protected\s+static\s+void\s+EXPBG_LazyStatics_EBG_OptionalScalar\s*\(') -match 's_EBG_FieldIndexes\s*=\s*new\s+map<string,\s*int>\(\);') 'the field index memo must be a lazy static'

# 3. Loaded addons once per world.
$gateText = Read-Text (Join-Path $scripts 'EBG_FullSaveGate.c')
$gate = $files['EBG_FullSaveGate.c']
$readAddons = Get-Body $gate 'protected\s+static\s+bool\s+ReadLoadedAddons\s*\('
Assert ([regex]::Matches($gate, '\bGetLoadedAddons\s*\(').Count -eq 1 -and $readAddons.Contains('GameProject.GetLoadedAddons(addons);')) 'GetLoadedAddons must appear only inside the cached ReadLoadedAddons'
Get-Ordered $readAddons @('if (s_AddonsRead && world == s_AddonWorld)', 'GameProject.GetLoadedAddons(addons);', 'if (addons.IsEmpty())', 'return false;', '"FC1402F65B2F4A45"', 'addons.Contains("6A1876F37D65AB09") && !addons.Contains("07BC942D90324CD9")', 's_AddonWorld = world;', 's_AddonsRead = true;') 'ReadLoadedAddons (an empty list is never remembered)'
$supported = Get-Body $gateText 'protected\s+static\s+bool\s+SupportedRuntime\s*\('
Get-Ordered $supported @('GameMasterSystems systems config, which this session does not run', 'if (!ReadLoadedAddons())', 'reason = "Loaded addon ownership could not be verified";', 'if (!s_OwnPack)', 'reason = "EXPBG GM Tools addon identity could not be verified";') 'SupportedRuntime (same refusals in the same order)'
Assert ((Get-Body $gate 'protected\s+static\s+bool\s+CanCaptureForCDF\s*\(') -match 'if\s*\(!ReadLoadedAddons\(\)\s*\|\|\s*!s_CDFWithoutCompanion\)\s*\{\s*return\s+true;\s*\}') 'CanCaptureForCDF must admit unless CDF is loaded without the companion (an unreadable list admitted before too)'
Assert ((Get-Body $gate 'static\s+void\s+ShutdownForWorldCleanup\s*\(') -match 's_AddonWorld\s*=\s*null;\s*s_AddonsRead\s*=\s*false;') 'world cleanup must forget the addon read'
Assert ($gate -notmatch 'static\s+int\s+s_\w+\s*=') 'addon statics must not use initializers (0.1.13 static initializer limit)'

# 4. One shared budgeted sampler per kind.
foreach ($name in $files.Keys) {
 Assert (![regex]::IsMatch($files[$name], 'CallLater\s*\(\s*EBG_Sample\w*\s*,\s*\d+\s*,\s*true')) "a per-component repeating sampler timer remains in $name"
 Assert (![regex]::IsMatch($files[$name], 'Callqueue\(\)\.\w+\(\s*EBG_Sample\w*')) "a sampler is still queued or removed per component in $name"
}
$samplers = @(
 @{ File = 'EBG_RHSWeaponRails.c'; Kind = 'Rail'; Class = 'SCR_WeaponAttachmentsStorageComponent'; Lazy = 'EXPBG_LazyStatics_SCR_WeaponAttachmentsStorageComponent'; Sample = 'EBG_SampleRHSRails'; Callers = @('void\s+EBG_PublishMissionRails\s*\(', 'bool\s+EBG_RestoreRHSRails\s*\(') }
 @{ File = 'EBG_BayonetState.c'; Kind = 'Bayonet'; Class = 'SCR_BayonetComponent'; Lazy = 'EXPBG_LazyStatics_SCR_BayonetComponent'; Sample = 'EBG_SampleBayonetNetState'; Callers = @('void\s+EBG_PublishMissionBlood\s*\(', 'bool\s+EBG_RestoreFullState\s*\(') }
 @{ File = 'EBG_BayonetState.c'; Kind = 'Blade'; Class = 'SCR_ChimeraCharacter'; Lazy = 'EXPBG_LazyStatics_SCR_ChimeraCharacter_Blades'; Sample = 'EBG_SampleClothBladeNetState'; Callers = @('bool\s+EBG_PublishClothBlade\s*\(') }
)
foreach ($sampler in $samplers) {
 $code = $files[$sampler.File]; $kind = $sampler.Kind
 $step = "EBG_Step$($kind)Samplers"; $register = "EBG_Register$($kind)Sampler"
 Assert ([regex]::Matches($code, 'CallLater\s*\(\s*' + $step + '\s*,\s*250\s*,\s*true\s*\)').Count -eq 1) "$step must be armed at exactly one site, every 250 ms"
 Assert ([regex]::Matches($code, 'CallLater\s*\(').Count -ge 1) "$($sampler.File) lost its timers"
 Assert ($code -match "protected\s+static\s+void\s+$step\s*\(" -and $code -match "protected\s+static\s+void\s+$register\s*\(") "$step and $register must be static (one timer for the kind, not one per instance)"
 Assert ($code -match "protected\s+static\s+ref\s+array<$($sampler.Class)>\s+s_EBG_$($kind)Samplers;") "the $kind registry must be a weak lazy static array"
 Assert ((Get-Body $code "protected\s+static\s+void\s+$($sampler.Lazy)\s*\(") -match "s_EBG_$($kind)Samplers\s*=\s*new\s+array<$($sampler.Class)>\(\);") "the $kind registry must be created on first use"
 $registerBody = Get-Body $code "protected\s+static\s+void\s+$register\s*\("
 Get-Ordered $registerBody @("EBG_$($kind)SamplerWorldChanged()", "m_EBG_$($kind)Listed = true;", "s_EBG_$($kind)Samplers.Insert(", "if (s_EBG_$($kind)Armed) return;", "s_EBG_$($kind)Armed = true;", "Remove($step);", "CallLater($step, 250, true);") $register
 $stepBody = Get-Body $code "protected\s+static\s+void\s+$step\s*\("
 Get-Ordered $stepBody @("EBG_$($kind)SamplerWorldChanged();", "if (s_EBG_$($kind)Samplers.IsEmpty())", "Remove($step);", "s_EBG_$($kind)Armed = false;", "if (s_EBG_$($kind)Phase == 0)", "s_EBG_$($kind)Due = s_EBG_$($kind)Samplers.Count();", "s_EBG_$($kind)Budget = (s_EBG_$($kind)Due + 3) / 4;", "if (visits > s_EBG_$($kind)Due) visits = s_EBG_$($kind)Due;", "s_EBG_$($kind)Due--;", ".$($sampler.Sample)();", "s_EBG_$($kind)Phase = (s_EBG_$($kind)Phase + 1) % 4;") $step
 Assert ($stepBody.Contains("s_EBG_$($kind)Samplers.RemoveOrdered(s_EBG_$($kind)Cursor);") -and $stepBody.Contains("s_EBG_$($kind)Cursor++;")) "$step must drop destroyed entries in place and advance past sampled ones"
 foreach ($caller in $sampler.Callers) {
  Get-Ordered (Get-Body $code $caller) @("$($sampler.Sample)();", "$register(this);") "$caller (sample at once, then register)"
 }
 Assert ([regex]::Matches($code, "\b$register\(this\);").Count -eq $sampler.Callers.Count) "$register must be called only from the publish and restore paths"
}
# The cloth blade stops itself when its blade is gone, as its own timer did, and leaves the registry.
$blade = $files['EBG_BayonetState.c']
Assert ((Get-Body $blade 'protected\s+void\s+EBG_SampleClothBladeNetState\s*\(') -match '(?s)if\s*\(!bayonet\)\s*\{\s*m_EBG_FullBlade\s*=\s*null;\s*m_EBG_BladeSampling\s*=\s*false;') 'a cloth blade whose bayonet is gone must stop sampling'
Assert ((Get-Body $blade 'protected\s+static\s+void\s+EBG_StepBladeSamplers\s*\(') -match '(?s)if\s*\(wearer\)\s*wearer\.m_EBG_BladeListed\s*=\s*false;\s*s_EBG_BladeSamplers\.RemoveOrdered\(s_EBG_BladeCursor\);') 'a stopped or destroyed wearer must leave the blade registry'
Assert ((Get-Body $blade 'protected\s+static\s+void\s+EBG_RegisterBladeSampler\s*\(') -match 'wearer\.m_EBG_BladeSampling\s*=\s*true;') 'publishing a cloth blade again must resume its sampling'
# The samplers keep their unchanged compare-and-bump.
Assert ((Get-Body $files['EBG_RHSWeaponRails.c'] 'protected\s+void\s+EBG_SampleRHSRails\s*\(') -match '(?s)if\s*\(!changed\)\s*return;\s*m_EBG_RHSRails\.Copy\(current\);\s*Replication\.BumpMe\(\);') 'the rail sample must stay change-gated'
Assert ((Get-Body $blade 'protected\s+void\s+EBG_SampleBayonetNetState\s*\(') -match 'm_EBG_NetBlood\s*==\s*m_fBloodStainLevel\s*&&\s*EBG_MaterialPacketMatches') 'the bayonet sample must stay change-gated'

# Enforce gotchas in the touched sources: reserved names, RandomFloat, static initializers,
# one-line value returns in the new helpers, ASCII only and unmixed line endings.
foreach ($name in $files.Keys) {
 $path = Join-Path $scripts $name; $code = $files[$name]
 Assert (![regex]::IsMatch($code, '\b(int|float|bool|string|vector|auto|IEntity|World)\s+(owned|Sleep|Wait)\b')) "reserved Enforce name used as a variable in $name"
 Assert (![regex]::IsMatch($code, 'Math\.RandomFloat\b')) "Math.RandomFloat in $name"
 Assert (![regex]::IsMatch($code, 'static\s+(const\s+)?ref\s+[^;=]+=|static\s+const\s+array<')) "static collection initializer in $name"
 $bytes = [IO.File]::ReadAllBytes($path)
 Assert (@($bytes | Where-Object { $_ -gt 127 }).Count -eq 0) "non-ASCII byte in $name"
 $text = [Text.Encoding]::ASCII.GetString($bytes)
 $crlf = [regex]::Matches($text, "`r`n").Count; $lf = [regex]::Matches($text, "`n").Count
 Assert ($crlf -eq 0 -or $crlf -eq $lf) "mixed line endings in $name ($crlf CRLF of $lf lines)"
}
$newHelpers = @(
 @('EBG_SimulationCache.c', 'protected\s+static\s+SCR_ChimeraCharacter\s+EBG_ScanSimulationPartOwner\s*\('),
 @('EBG_OptionalModState.c', 'protected\s+static\s+int\s+ScanFieldIndex\s*\('),
 @('EBG_FullSaveGate.c', 'protected\s+static\s+bool\s+ReadLoadedAddons\s*\('),
 @('EBG_FullSaveGate.c', 'protected\s+static\s+bool\s+CanCaptureForCDF\s*\('),
 @('EBG_RHSWeaponRails.c', 'protected\s+static\s+bool\s+EBG_RailSamplerWorldChanged\s*\('),
 @('EBG_BayonetState.c', 'protected\s+static\s+bool\s+EBG_BayonetSamplerWorldChanged\s*\('),
 @('EBG_BayonetState.c', 'protected\s+static\s+bool\s+EBG_BladeSamplerWorldChanged\s*\(')
)
foreach ($helper in $newHelpers) {
 Assert (![regex]::IsMatch((Get-Body $files[$helper[0]] $helper[1]), '\bif\s*\([^;{}]*\)\s*return\s+[^;\s][^;]*;')) "a conditional value return without braces in $($helper[0]) $($helper[1])"
}
'PASS: unit-caching leaf performance (Simulation part owner index with its debug cross-check, RHS device names, optional field memo, loaded addons once per world, shared rail, bayonet and cloth-blade samplers). Native engines were not launched.'
