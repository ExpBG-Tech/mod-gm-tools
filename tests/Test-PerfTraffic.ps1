#requires -Version 7.0
# Portable guard for the WP7 civilian traffic performance fixes (0.1.15 performance plan).
#  civilians-b-01 (a): EAC_TrafficDirector.Hidden remembers one verdict per party for the
#   rest of the Step that reached it (key: Step serial, world time, party, exact point,
#   minimum) and reuses it in CanRemove, DismountHidden and BoardHidden. Outside Step
#   (controller-gap pump, fixtures) every call traces.
#  civilians-b-01 (b): ProveHidden tests every observer's distance before any trace, then
#   traces the nearest observer first and the rest in list order. Same verdict.
#  civilians-a-04 (server half): UpdateHorn returns at once when no party is retained and
#   the polled count is already zero, before Get() and GetActive().
#  civilians-b-08: Controlled() resolves the group and car RplComponent and the car's
#   compartment manager once per entity (re-resolved whenever Group or Car is another
#   entity) and fills a member scratch slot list; CanRemove tests FarFromObservers before
#   HealthyParty; EAC_TrafficRoute.Healthy reuses one lazily created hit-zone list.
# No engine is launched.
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$scripts = Join-Path $repo 'addon/ambient-civilians/Scripts/Game/EXPAC'
function Assert([bool]$Condition, [string]$Message) { if (!$Condition) { throw "FAIL: $Message" } }
# Source without // comments (no traffic file has // inside a string literal; asserted below).
function Read-Code([string]$Name) {
 $path = Join-Path $scripts $Name
 $bytes = [IO.File]::ReadAllBytes($path)
 Assert (@($bytes | Where-Object { $_ -gt 127 }).Count -eq 0) "$Name must stay ASCII"
 $text = [Text.Encoding]::ASCII.GetString($bytes)
 Assert (![regex]::IsMatch($text, '"[^"\n]*//')) "$Name has '//' inside a string; this guard's comment stripping would misread it"
 [regex]::Replace($text, '(?m)//.*$', '')
}
function Get-Body([string]$Text, [string]$Signature) {
 $match = [regex]::Match($Text, $Signature)
 Assert $match.Success "signature not found: $Signature"
 $open = $Text.IndexOf('{', $match.Index + $match.Length); $depth = 0
 for ($i = $open; $i -lt $Text.Length; $i++) {
  if ($Text[$i] -eq '{') { $depth++ } elseif ($Text[$i] -eq '}') { $depth--; if ($depth -eq 0) { return $Text.Substring($open + 1, $i - $open - 1) } }
 }
 throw "FAIL: unbalanced body for $Signature"
}
function Count([string]$Text, [string]$Pattern) { [regex]::Matches($Text, $Pattern).Count }
function Before([string]$Text, [string]$First, [string]$Second) {
 $a = [regex]::Match($Text, $First); $b = [regex]::Match($Text, $Second)
 return ($a.Success -and $b.Success -and $a.Index -lt $b.Index)
}
# Enforce gotchas in the methods this package wrote.
function Assert-Enforce([string]$Body, [string]$Where) {
 Assert (![regex]::IsMatch($Body, '\bif\s*\([^;{}]*\)\s*return\s+[^;\s][^;]*;')) "$Where must keep every value return on its own line"
 Assert (![regex]::IsMatch($Body, 'Math\.RandomFloat\b')) "$Where must not use Math.RandomFloat"
 Assert (![regex]::IsMatch($Body, '\b(int|float|bool|string|vector|auto|IEntity)\s+(owned|Sleep|Wait)\b')) "$Where uses a reserved Enforce name"
}

$director = Read-Code 'EAC_TrafficDirector.c'
$party = Read-Code 'EAC_TrafficParty.c'
$route = Read-Code 'EAC_TrafficRoute.c'

# ---------------------------------------------------------------------------------------
# civilians-b-01 (a): per-Step Hidden memo
# ---------------------------------------------------------------------------------------
Assert ([regex]::IsMatch($director, '(?m)^\s*protected\s+int\s+m_HiddenStep\s*;')) 'the director must keep a Step serial without an initializer'
Assert ([regex]::IsMatch($director, '(?m)^\s*protected\s+bool\s+m_HiddenMemoOpen\s*;')) 'the director must keep the memo-open flag without an initializer'
foreach ($field in 'int\s+HiddenStep\s*;', 'float\s+HiddenTime\s*,\s*HiddenMinimum\s*;', 'vector\s+HiddenAt\s*;', 'bool\s+HiddenVerdict\s*;') {
 Assert ([regex]::IsMatch($party, "(?m)^\s*$field")) "EAC_TrafficParty must carry the memo field: $field"
}
$hidden = Get-Body $director 'protected\s+bool\s+Hidden\s*\(\s*EAC_TrafficParty\s+party\s*,\s*vector\s+position\s*,\s*array<IEntity>\s+observers\s*,\s*float\s+minimum\s*=\s*60\s*\)'
Assert-Enforce $hidden 'Hidden'
Assert ([regex]::IsMatch($hidden, '^\s*if\s*\(\s*!party\s*\|\|\s*!m_HiddenMemoOpen\s*\)\s*\{\s*return\s+ProveHidden\(\s*party\s*,\s*position\s*,\s*observers\s*,\s*minimum\s*\)\s*;\s*\}')) 'Hidden must trace directly outside Step or without a party'
Assert ([regex]::IsMatch($hidden, 'float\s+worldTime\s*=\s*m_World\.GetWorldTime\(\)\s*;')) 'the memo key must include the world time'
$key = [regex]::Match($hidden, 'bool\s+same\s*=\s*([^;]+);')
Assert $key.Success 'the memo key must be built in one expression'
foreach ($term in 'party\.HiddenStep\s*==\s*m_HiddenStep', 'party\.HiddenTime\s*==\s*worldTime', 'party\.HiddenMinimum\s*==\s*minimum') {
 Assert ([regex]::IsMatch($key.Groups[1].Value, $term)) "the memo key must compare: $term"
}
Assert ([regex]::IsMatch($hidden, 'if\s*\(\s*same\s*&&\s*at\[0\]\s*==\s*position\[0\]\s*&&\s*at\[1\]\s*==\s*position\[1\]\s*&&\s*at\[2\]\s*==\s*position\[2\]\s*\)\s*\{\s*return\s+party\.HiddenVerdict\s*;\s*\}')) 'the memo must match the exact point, component by component'
Assert ([regex]::IsMatch($hidden, '(?s)bool\s+verdict\s*=\s*ProveHidden\(\s*party\s*,\s*position\s*,\s*observers\s*,\s*minimum\s*\)\s*;.*party\.HiddenStep\s*=\s*m_HiddenStep\s*;.*party\.HiddenTime\s*=\s*worldTime\s*;.*party\.HiddenMinimum\s*=\s*minimum\s*;.*party\.HiddenAt\s*=\s*position\s*;.*party\.HiddenVerdict\s*=\s*verdict\s*;\s*return\s+verdict\s*;\s*$')) 'a miss must trace, remember the full key and verdict, and return it'
Assert ((Count $director '\bProveHidden\s*\(') -eq 3) 'ProveHidden is the declaration plus the two calls in Hidden'
Assert ((Count $director '\bObserverSees\s*\(') -eq 3) 'ObserverSees is the declaration plus the two calls in ProveHidden'
Assert ((Count $director 'm_World\.TraceMove\s*\(\s*m_HiddenTrace') -eq 1) 'the visibility traces must only be fired by ObserverSees'
Assert ((Count $director '\.HiddenVerdict\s*=(?!=)') -eq 1 -and (Count $director '\.HiddenStep\s*=(?!=)') -eq 1) 'only Hidden may write the memo'
Assert (!(Count $party '\bHidden(Step|Time|Minimum|At|Verdict)\s*=(?!=)')) 'EAC_TrafficParty must never write the director memo itself'
Assert ((Count $director 'm_HiddenMemoOpen\s*=\s*true') -eq 1 -and (Count $director 'm_HiddenMemoOpen\s*=\s*false') -eq 2) 'the memo is opened once (Step) and closed at both Step exits'

$step = Get-Body $director 'void\s+Step\s*\(\s*EAC_AmbientModule\s+module\s*,\s*array<IEntity>\s+observers\s*,\s*float\s+now\s*\)'
Assert ([regex]::IsMatch($step, '^\s*if\s*\(\s*Get\(\)\s*!=\s*this\s*\|\|\s*!module\s*\|\|\s*module\s*!=\s*EAC_AmbientModule\.GetActive\(\)\s*\|\|\s*module\.GetWorld\(\)\s*!=\s*m_World\s*\)\s*return\s*;')) 'Step keeps its guard first'
Assert ([regex]::IsMatch($step, 'if\s*\(\s*m_HiddenStep\s*>=\s*1000000000\s*\)\s*m_HiddenStep\s*=\s*0\s*;\s*m_HiddenStep\+\+\s*;\s*m_HiddenMemoOpen\s*=\s*true\s*;')) 'Step must start a new serial (bounded) and open the memo'
Assert (Before $step 'm_HiddenMemoOpen\s*=\s*true' 'Monitor\s*\(') 'the memo must be open before Monitor runs'
Assert ([regex]::IsMatch($step, 'if\s*\(\s*Advance\(\s*module\s*,\s*party\s*,\s*observers\s*,\s*now\s*\)\s*\)\s*\{\s*m_HiddenMemoOpen\s*=\s*false\s*;\s*return\s*;\s*\}')) 'the Advance exit must close the memo'
Assert ([regex]::IsMatch($step, 'Admit\(\s*module\s*,\s*observers\s*,\s*now\s*\)\s*;\s*m_HiddenMemoOpen\s*=\s*false\s*;\s*$')) 'the Admit exit must close the memo'
Assert ([regex]::IsMatch($step, 'foreach\s*\(\s*EAC_TrafficParty\s+party\s*:\s*m_Parties\s*\)\s*Monitor\(\s*module\s*,\s*party\s*,\s*observers\s*,\s*now\s*\)\s*;')) 'Step must still Monitor every party first'
$drain = Get-Body $director 'protected\s+void\s+DrainParties\s*\(\s*\)'
Assert (!$drain.Contains('m_HiddenMemoOpen') -and !$drain.Contains('m_HiddenStep')) 'the controller-gap pump must never open the memo'
# The callers this memo serves still ask Hidden for the same points.
$canRemove = Get-Body $director 'protected\s+bool\s+CanRemove\s*\('
Assert ([regex]::IsMatch($canRemove, 'if\s*\(\s*party\.Car\s*&&\s*!Hidden\(\s*party\s*,\s*party\.Car\.GetOrigin\(\)\s*,\s*observers\s*\)\s*\)\s*return\s+false\s*;')) 'CanRemove must still prove the car hidden'
Assert ([regex]::IsMatch($canRemove, 'if\s*\(\s*row\.Actor\s*&&\s*!Hidden\(\s*party\s*,\s*row\.Actor\.GetOrigin\(\)\s*,\s*observers\s*\)\s*\)\s*return\s+false\s*;')) 'CanRemove must still prove every crew member hidden'
$monitor = Get-Body $director 'protected\s+void\s+Monitor\s*\('
Assert ([regex]::IsMatch($monitor, 'if\s*\(\s*far\s*&&\s*party\.AlarmUntil\s*<=\s*now\s*&&\s*Hidden\(\s*party\s*,\s*party\.Car\.GetOrigin\(\)\s*,\s*observers\s*\)\s*\)')) 'Monitor must still sample the far car'
Assert ((Get-Body $director 'protected\s+bool\s+BoardHidden\s*\(').Contains('if (!Hidden(party, party.Car.GetOrigin(), observers)) return false;')) 'BoardHidden must still prove the car hidden'
Assert ((Count (Get-Body $director 'protected\s+bool\s+DismountHidden\s*\(') '!Hidden\(\s*party\s*,') -eq 3) 'DismountHidden must still prove the car, every crew member and the dismount point hidden'

# ---------------------------------------------------------------------------------------
# civilians-b-01 (b): distance pass first, nearest observer traced first
# ---------------------------------------------------------------------------------------
$prove = Get-Body $director 'protected\s+bool\s+ProveHidden\s*\(\s*EAC_TrafficParty\s+party\s*,\s*vector\s+position\s*,\s*array<IEntity>\s+observers\s*,\s*float\s+minimum\s*\)'
Assert-Enforce $prove 'ProveHidden'
Assert ([regex]::IsMatch($prove, '^\s*if\s*\(\s*EAC_PedestrianSpawner\.GetObserverReason\(\s*m_World\s*,\s*observers\s*\)\s*!=\s*EAC_ESpawnReason\.NONE\s*\)\s*\{\s*return\s+false\s*;\s*\}')) 'ProveHidden must fail closed on the observer list first'
$pass = [regex]::Match($prove, '(?s)foreach\s*\(\s*int\s+index\s*,\s*IEntity\s+observer\s*:\s*observers\s*\)\s*\{\s*float\s+distance\s*=\s*vector\.Distance\(\s*observer\.GetOrigin\(\)\s*,\s*position\s*\)\s*;\s*if\s*\(\s*distance\s*<\s*minimum\s*\)\s*\{\s*return\s+false\s*;\s*\}\s*if\s*\(\s*nearest\s*<\s*0\s*\|\|\s*distance\s*<\s*nearestDistance\s*\)\s*\{\s*nearest\s*=\s*index\s*;\s*nearestDistance\s*=\s*distance\s*;\s*\}\s*\}')
Assert $pass.Success 'the distance pass must refuse on the old < minimum test and keep the first nearest observer'
Assert ($prove.IndexOf('ObserverSees') -gt $pass.Index + $pass.Length) 'no trace may be fired before the distance pass ends'
Assert ([regex]::IsMatch($prove, 'if\s*\(\s*nearest\s*>=\s*0\s*&&\s*ObserverSees\(\s*party\s*,\s*position\s*,\s*observers\[nearest\]\s*\)\s*\)\s*\{\s*return\s+false\s*;\s*\}')) 'the nearest observer must be traced first'
Assert ([regex]::IsMatch($prove, '(?s)foreach\s*\(\s*int\s+other\s*,\s*IEntity\s+watcher\s*:\s*observers\s*\)\s*\{\s*if\s*\(\s*other\s*==\s*nearest\s*\)\s*continue\s*;\s*if\s*\(\s*ObserverSees\(\s*party\s*,\s*position\s*,\s*watcher\s*\)\s*\)\s*\{\s*return\s+false\s*;\s*\}\s*\}\s*return\s+true\s*;\s*$')) 'the remaining observers must be traced in list order, and only an all-hidden list proves hidden'
$sees = Get-Body $director 'protected\s+bool\s+ObserverSees\s*\(\s*EAC_TrafficParty\s+party\s*,\s*vector\s+position\s*,\s*IEntity\s+observer\s*\)'
Assert-Enforce $sees 'ObserverSees'
foreach ($needle in 'm_HiddenTrace.Start = ChimeraCharacter.Cast(observer).EyePosition();',
  'm_HiddenTrace.Flags = TraceFlags.WORLD | TraceFlags.ENTS | TraceFlags.VISIBILITY | TraceFlags.ANY_CONTACT;',
  'm_HiddenExcluded.Clear(); m_HiddenExcluded.Insert(observer);',
  'IEntity vehicle = CompartmentAccessComponent.GetVehicleIn(observer); if (vehicle) m_HiddenExcluded.Insert(vehicle);',
  'if (party.Car) m_HiddenExcluded.Insert(party.Car);',
  'foreach (EAC_TrafficOccupant row : party.Crew) if (row.Actor) m_HiddenExcluded.Insert(row.Actor);',
  'm_HiddenTrace.ExcludeArray = m_HiddenExcluded;', 'for (int i = 0; i < 5; i++)', 'vector sample = position + "0 2.2 0";',
  'if (party.Car) { vector transform[4]; party.Car.GetWorldTransform(transform); right = transform[0]; forward = transform[2]; }',
  'sample = position + right * (cornerX * 2.8 - 1.4) + forward * (cornerZ * 5.8 - 2.9) + "0 1.2 0";', 'm_HiddenTrace.End = sample;') {
 Assert $sees.Contains($needle) "ObserverSees lost part of the unchanged trace set: $needle"
}
Assert ([regex]::IsMatch($sees, 'if\s*\(\s*m_World\.TraceMove\(\s*m_HiddenTrace\s*,\s*null\s*\)\s*>=\s*1\s*\)\s*\{\s*return\s+true\s*;\s*\}\s*\}\s*return\s+false\s*;\s*$')) 'any unobstructed trace means seen; five blocked traces mean not seen'

# Model: the old interleaved loop and the new two-pass order agree on every list.
$random = [Random]::new(7)
for ($case = 0; $case -lt 20000; $case++) {
 $n = $random.Next(1, 9); $minimum = 60
 $distance = @(1..$n | ForEach-Object { $random.Next(0, 400) }); $seen = @(1..$n | ForEach-Object { $random.Next(0, 4) -eq 0 })
 $old = $true
 for ($i = 0; $i -lt $n; $i++) { if ($distance[$i] -lt $minimum -or $seen[$i]) { $old = $false; break } }
 $new = $true; $nearest = -1
 for ($i = 0; $i -lt $n -and $new; $i++) { if ($distance[$i] -lt $minimum) { $new = $false } elseif ($nearest -lt 0 -or $distance[$i] -lt $distance[$nearest]) { $nearest = $i } }
 if ($new -and $nearest -ge 0 -and $seen[$nearest]) { $new = $false }
 for ($i = 0; $i -lt $n -and $new; $i++) { if ($i -ne $nearest -and $seen[$i]) { $new = $false } }
 Assert ($old -eq $new) "two-pass Hidden model disagrees with the old loop (case $case)"
}

# ---------------------------------------------------------------------------------------
# civilians-a-04 (server half): UpdateHorn idle early-out
# ---------------------------------------------------------------------------------------
$horn = Get-Body $director 'void\s+UpdateHorn\s*\(\s*\)'
Assert ([regex]::IsMatch($horn, '^\s*if\s*\(\s*m_Parties\.IsEmpty\(\)\s*&&\s*m_HornPolled\s*==\s*0\s*\)\s*return\s*;\s*if\s*\(\s*Get\(\)\s*!=\s*this\s*\|\|\s*!EAC_AmbientModule\.GetActive\(\)\s*\)\s*return\s*;\s*m_HornPolled\s*=\s*0\s*;')) 'UpdateHorn must return first when nothing is retained and m_HornPolled is already zero, and keep the old guard and reset after it'
Assert ($horn.Contains('if (party.UpdateHorn() && m_HornRequests < 1000000) m_HornRequests++;')) 'the horn loop must be unchanged'

# ---------------------------------------------------------------------------------------
# civilians-b-08: cheaper Controlled(), CanRemove order, Healthy scratch list
# ---------------------------------------------------------------------------------------
foreach ($field in 'SCR_AIGroup\s+m_ResolvedGroup', 'RplComponent\s+m_GroupRpl', 'IEntity\s+m_ResolvedCar', 'RplComponent\s+m_ResolvedCarRpl', 'BaseCompartmentManagerComponent\s+m_Compartments', 'ref\s+array<BaseCompartmentSlot>\s+m_SlotScratch') {
 Assert ([regex]::IsMatch($party, "(?m)^\s*protected\s+$field\s*;")) "EAC_TrafficParty must declare the protected cache without an initializer: $field"
}
$controlled = Get-Body $party 'bool\s+Controlled\s*\(\s*\)'
Assert ([regex]::IsMatch($controlled, 'if\s*\(\s*m_ResolvedGroup\s*!=\s*Group\s*\)\s*\{\s*m_ResolvedGroup\s*=\s*Group\s*;\s*m_GroupRpl\s*=\s*RplComponent\.Cast\(\s*Group\.FindComponent\(\s*RplComponent\s*\)\s*\)\s*;\s*\}\s*RplComponent\s+groupRpl\s*=\s*m_GroupRpl\s*;')) 'the group RplComponent must be re-resolved whenever Group is another entity'
Assert (Before $controlled '!Group\s*\|\|' 'm_ResolvedGroup\s*!=\s*Group') 'the group cache may only be read after Group is proven non-null'
Assert ([regex]::IsMatch($controlled, 'if\s*\(\s*m_ResolvedCar\s*!=\s*Car\s*\)\s*ResolveCar\(\)\s*;\s*RplComponent\s+rpl\s*=\s*m_ResolvedCarRpl\s*;')) 'the car caches must be re-resolved whenever Car is another entity'
Assert ([regex]::IsMatch($controlled, 'BaseCompartmentManagerComponent\s+manager\s*=\s*m_Compartments\s*;\s*if\s*\(\s*!manager\s*\)\s*return\s+false\s*;')) 'Controlled must read the cached compartment manager and keep the missing-manager refusal'
Assert ([regex]::IsMatch($controlled, 'if\s*\(\s*!m_SlotScratch\s*\)\s*m_SlotScratch\s*=\s*new\s+array<BaseCompartmentSlot>\(\)\s*;\s*array<BaseCompartmentSlot>\s+slots\s*=\s*m_SlotScratch\s*;\s*slots\.Clear\(\)\s*;\s*manager\.GetCompartments\(\s*slots\s*\)\s*;\s*if\s*\(\s*slots\.Count\(\)\s*>\s*16\s*\)\s*return\s+false\s*;')) 'the slot list must be the member scratch, emptied before every fill'
Assert (!(Count $controlled '=\s*\{\s*\}') -and (Count $controlled 'new\s+array<') -eq 1) 'Controlled must not allocate a list per call (only the first-use scratch creation)'
Assert ((Count $controlled 'FindComponent\(') -eq 3) 'Controlled keeps only the group cache resolution and the per-crew RplComponent and AIControlComponent lookups'
foreach ($needle in 'if (!groupRpl || groupRpl.IsProxy() || !groupRpl.IsOwner()) { PlayerTouched = true; return false; }',
  'if (!rpl || rpl.IsProxy() || !rpl.IsOwner()) { PlayerTouched = true; return false; }',
  'if (slot.GetOccupant() && !Owns(slot.GetOccupant())) { PlayerTouched = true; return false; }',
  'if ((row.Joined || control.GetAIAgent().GetParentGroup()) && control.GetAIAgent().GetParentGroup() != Group) { PlayerTouched = true; return false; }') {
 Assert $controlled.Contains($needle) "Controlled lost an unchanged ownership test: $needle"
}
$resolve = Get-Body $party 'protected\s+void\s+ResolveCar\s*\(\s*\)'
Assert ([regex]::IsMatch($resolve, '^\s*m_ResolvedCar\s*=\s*Car\s*;\s*m_ResolvedCarRpl\s*=\s*null\s*;\s*m_Compartments\s*=\s*null\s*;\s*if\s*\(\s*!Car\s*\)\s*return\s*;\s*m_ResolvedCarRpl\s*=\s*RplComponent\.Cast\(\s*Car\.FindComponent\(\s*RplComponent\s*\)\s*\)\s*;\s*m_Compartments\s*=\s*BaseCompartmentManagerComponent\.Cast\(\s*Car\.FindComponent\(\s*BaseCompartmentManagerComponent\s*\)\s*\)\s*;\s*$')) 'ResolveCar must resolve both car caches from the current Car'
Assert ((Count $party 'm_ResolvedCar\s*=') -eq 2 -and (Count $party 'm_Compartments\s*=') -eq 3 -and (Count $party 'm_GroupRpl\s*=') -eq 1) 'only ResolveCar, UnbindCar (clear) and the Controlled group check may write the caches'
$bind = Get-Body $party 'bool\s+BindCar\s*\(\s*\)'
Assert (Before $bind 'ResolveCar\(\)\s*;' 'if\s*\(\s*!Controlled\(\)\s*\)') 'BindCar must resolve the car caches before its Controlled() check'
Assert ([regex]::IsMatch((Get-Body $party 'void\s+UnbindCar\s*\(\s*\)'), 'm_ResolvedCar\s*=\s*null\s*;\s*m_ResolvedCarRpl\s*=\s*null\s*;\s*m_Compartments\s*=\s*null\s*;')) 'UnbindCar must clear the car caches'
Assert-Enforce $resolve 'ResolveCar'

Assert ([regex]::IsMatch($canRemove, 'if\s*\(\s*!FarFromObservers\(\s*party\s*,\s*observers\s*,\s*distance\s*\)\s*\|\|\s*!HealthyParty\(\s*party\s*\)\s*\)\s*\{\s*return\s+false\s*;\s*\}')) 'CanRemove must test FarFromObservers before HealthyParty'
Assert ((Count $canRemove 'HealthyParty\(') -eq 1 -and (Count $canRemove 'FarFromObservers\(') -eq 1) 'CanRemove must call each test exactly once'
Assert (Before $canRemove 'FarFromObservers\(' 'if\s*\(\s*party\.Car\s*&&\s*!Hidden\(') 'both cheap tests must still come before any trace'

Assert ([regex]::IsMatch($route, '(?m)^\s*protected\s+static\s+ref\s+array<HitZone>\s+s_HealthZones\s*;')) 'the hit-zone list must be a protected static ref declared without an initializer (0.1.13 lazy statics)'
$healthy = Get-Body $route 'static\s+bool\s+Healthy\s*\(\s*IEntity\s+entity\s*\)'
Assert-Enforce ($healthy -replace '(?m)^\s*if \(!entity\) return false;\s*$', '' -replace '(?m)^\s*if \(!damage \|\| damage\.IsDestroyed\(\) \|\| damage\.GetHealthScaled\(\) < 0\.999\) return false;\s*$', '') 'Healthy (new lines)'
Assert ([regex]::IsMatch($healthy, 'if\s*\(\s*!s_HealthZones\s*\)\s*s_HealthZones\s*=\s*new\s+array<HitZone>\(\)\s*;\s*array<HitZone>\s+zones\s*=\s*s_HealthZones\s*;\s*zones\.Clear\(\)\s*;\s*damage\.GetAllHitZonesInHierarchy\(\s*zones\s*\)\s*;\s*bool\s+healthy\s*=\s*zones\.Count\(\)\s*<=\s*128\s*;')) 'Healthy must fill the lazily created list after emptying it, and keep the 128-zone limit'
Assert ($healthy.Contains('if (!zone || zone.GetHealth() < zone.GetMaxHealth() - 0.01)')) 'Healthy must keep the exact per-zone test'
Assert ([regex]::IsMatch($healthy, 'zones\.Clear\(\)\s*;\s*return\s+healthy\s*;\s*$')) 'Healthy must empty the list before every answer'
Assert ($healthy.Contains('if (!damage || damage.IsDestroyed() || damage.GetHealthScaled() < 0.999) return false;')) 'the damage-manager gate must be unchanged'
Assert (!(Count $healthy '=\s*\{\s*\}')) 'Healthy must not allocate a list per call'

# ---------------------------------------------------------------------------------------
# Repository guards these files take part in
# ---------------------------------------------------------------------------------------
foreach ($pair in @(@($director, 'EAC_TrafficDirector.c', 5), @($route, 'EAC_TrafficRoute.c', 1), @($party, 'EAC_TrafficParty.c', 0))) {
 Assert ((Count $pair[0] '\bPrint(Format)?\s*\(') -eq $pair[2]) "$($pair[1]) must keep its $($pair[2]) log calls (G4 pin)"
 Assert (!(Count $pair[0] 'static\s+(const\s+)?ref\s+[^;=]+=|static\s+const\s+array<')) "$($pair[1]) must not add a static collection initializer (G5)"
 Assert (!(Count $pair[0] 'SetEventMask\s*\(|\bEOn(Post)?Frame\b|\bEOn(Post)?Simulate\b')) "$($pair[1]) must not add a frame hook (G1)"
}
Assert (!(Count $party 'CallLater\s*\(') -and !(Count $route 'CallLater\s*\(')) 'party and route must not add a timer (G2)'
Assert ((Count $director 'CallLater\s*\(') -eq 1 -and (Count $director 'CallLater\(\s*DrainParties\s*,\s*GAP_INTERVAL_MS\s*,\s*true\s*\)') -eq 1) 'the director keeps only its controller-gap pump timer'
Assert ((Count $director 'Math\.RandomFloat\b') -le 1) 'no new Math.RandomFloat in the director (one pre-existing parked-wait draw)'

"PASS: traffic Hidden memo per Step and distance-first nearest-first proof (model agrees on 20000 lists); UpdateHorn idle early-out; Controlled caches per entity with a scratch slot list; CanRemove distance before health; Healthy reuses one hit-zone list. Native engines were not launched."
