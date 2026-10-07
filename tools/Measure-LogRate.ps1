#requires -Version 7.0
# Log volume of retained Arma Reforger console.log files (server and client), from the 0.1.15
# performance plan. Groups timestamped lines by their leading [TAG ...] (digits folded to '#'),
# reports lines per minute per tag (top -Top), and flags any tag above -LimitPerMinute after
# the first -WarmupMinutes. Continuation lines count for the tag of the line they continue.
# Also counts '[EBG DEBUGCHECK MISMATCH]' lines, which must stay at zero. Use it after the
# client-server e2e and the soak:
#   pwsh -File tools/Measure-LogRate.ps1 -Path <server>/console.log,<client>/console.log -FailOnFlag
# Portable: reads files only; no engine is launched.
[CmdletBinding(DefaultParameterSetName = 'Path')]
param(
 [Parameter(Mandatory, ParameterSetName = 'Path', Position = 0)][string[]]$Path,
 # Log text instead of files (portable tests).
 [Parameter(Mandatory, ParameterSetName = 'Lines')][AllowEmptyString()][string[]]$Lines,
 [double]$WarmupMinutes = 5,
 [double]$LimitPerMinute = 6,
 [int]$Top = 20,
 # Group by the first word of the tag ([EBG CLEANUP KEEP] -> [EBG]).
 [switch]$ByPrefix,
 # Also flag lines without a [TAG] (grouped by engine category).
 [switch]$IncludeUntagged,
 # Fail when a tag is flagged or a debug-check mismatch was logged.
 [switch]$FailOnFlag,
 # Return one object per tag instead of the text report.
 [switch]$PassThru
)
$ErrorActionPreference = 'Stop'
$stamp = [regex]'^(\d{1,2}):(\d{2}):(\d{2})\.(\d{3})\s+([A-Za-z_]+)\s*(?:\((\w)\))?\s*:\s?(.*)$'
$tagPattern = [regex]'^\[([^\[\]\r\n]{1,80})\]'

function Add-Bucket($Group, [int]$Minute) {
 $count = 0; $null = $Group.Buckets.TryGetValue($Minute, [ref]$count)
 $Group.Buckets[$Minute] = $count + 1
}
function Measure-Log([string]$Name, [Collections.Generic.IEnumerable[string]]$Source) {
 $groups = [ordered]@{}
 $first = $null; $last = $null; $previous = $null; $dayOffset = 0
 $current = $null; $currentSteady = $false; $currentMinute = 0; $total = 0; $mismatches = 0
 $warmup = $WarmupMinutes * 60
 foreach ($line in $Source) {
  $m = $stamp.Match($line)
  if (!$m.Success) {
   # A continuation of the previous record (multi-line print); header lines before it are skipped.
   if ($current -and $line.Trim()) {
    $current.Lines++; $total++
    if ($currentSteady) { $current.SteadyLines++; Add-Bucket $current $currentMinute }
   }
   continue
  }
  $seconds = [int]$m.Groups[1].Value * 3600 + [int]$m.Groups[2].Value * 60 + [int]$m.Groups[3].Value + [int]$m.Groups[4].Value / 1000
  # The clock wraps at midnight.
  if ($null -ne $previous -and $seconds + $dayOffset -lt $previous - 43200) { $dayOffset += 86400 }
  $time = $seconds + $dayOffset
  $previous = $time
  if ($null -eq $first) { $first = $time }
  $last = $time
  $elapsed = $time - $first
  $category = $m.Groups[5].Value; $level = $m.Groups[6].Value; $message = $m.Groups[7].Value
  $tag = $tagPattern.Match($message)
  if ($tag.Success) {
   $key = [regex]::Replace($tag.Groups[1].Value.Trim(), '\d+', '#')
   if ($ByPrefix) { $key = ($key -split '\s+')[0] }
   $key = "[$key]"
   if ($message.StartsWith('[EBG DEBUGCHECK MISMATCH]')) { $mismatches++ }
  } else { $key = "(untagged $category)" }
  if (!$groups.Contains($key)) {
   $groups[$key] = [pscustomobject]@{ Log = $Name; Tag = $key; Tagged = $tag.Success; Records = 0; Lines = 0; SteadyLines = 0
    Warnings = 0; Errors = 0; Buckets = [Collections.Generic.Dictionary[int,int]]::new(); PerMinute = 0.0; SteadyPerMinute = $null; PeakPerMinute = 0; Flagged = $false }
  }
  $current = $groups[$key]
  $current.Records++; $current.Lines++; $total++
  if ($level -eq 'W') { $current.Warnings++ } elseif ($level -eq 'E') { $current.Errors++ }
  $currentSteady = $elapsed -ge $warmup
  $currentMinute = [int][Math]::Floor($elapsed / 60)
  if ($currentSteady) { $current.SteadyLines++; Add-Bucket $current $currentMinute }
 }
 $minutes = 0.0; if ($null -ne $first) { $minutes = ($last - $first) / 60 }
 $steadyMinutes = [Math]::Max(0, $minutes - $WarmupMinutes)
 foreach ($group in $groups.Values) {
  $group.PerMinute = [Math]::Round($group.Lines / [Math]::Max($minutes, 1 / 60), 2)
  if ($steadyMinutes -gt 0) { $group.SteadyPerMinute = [Math]::Round($group.SteadyLines / $steadyMinutes, 2) }
  if ($group.Buckets.Count) { $group.PeakPerMinute = [int]($group.Buckets.Values | Measure-Object -Maximum).Maximum }
  $group.Flagged = ($group.Tagged -or $IncludeUntagged) -and $null -ne $group.SteadyPerMinute -and $group.SteadyPerMinute -gt $LimitPerMinute
 }
 [pscustomobject]@{ Log = $Name; Minutes = [Math]::Round($minutes, 2); SteadyMinutes = [Math]::Round($steadyMinutes, 2); Lines = $total; Mismatches = $mismatches
  Groups = @($groups.Values | Sort-Object -Property @{ Expression = 'Lines'; Descending = $true }, Tag) }
}

$results = @()
if ($PSCmdlet.ParameterSetName -eq 'Lines') { $results += Measure-Log 'lines' ([Collections.Generic.List[string]]$Lines) }
else {
 # 'pwsh -File' passes 'a,b' as one string: split it unless that exact file exists.
 $files = foreach ($entry in $Path) { if ($entry.Contains(',') -and !(Test-Path -LiteralPath $entry)) { $entry -split ',' } else { $entry } }
 foreach ($file in $files) {
  $resolved = (Resolve-Path -LiteralPath $file.Trim()).Path
  $results += Measure-Log $resolved ([IO.File]::ReadLines($resolved))
 }
}

$problems = [Collections.Generic.List[string]]::new()
foreach ($result in $results) {
 foreach ($group in $result.Groups | Where-Object Flagged) { $problems.Add("$($result.Log): $($group.Tag) at $($group.SteadyPerMinute) lines/min after the first $WarmupMinutes min (limit $LimitPerMinute)") }
 if ($result.Mismatches) { $problems.Add("$($result.Log): $($result.Mismatches) [EBG DEBUGCHECK MISMATCH] line(s)") }
}
if ($PassThru) { $results | ForEach-Object { $_.Groups } }
else {
 foreach ($result in $results) {
  $steady = "steady window $($result.SteadyMinutes) min after a $WarmupMinutes min warm-up"
  if ($result.SteadyMinutes -le 0) { $steady = "shorter than the $WarmupMinutes min warm-up; nothing flagged" }
  "== $($result.Log): $($result.Lines) lines over $($result.Minutes) min ($steady); debug-check mismatches: $($result.Mismatches)"
  $result.Groups | Select-Object -First $Top | Format-Table -AutoSize -Property Tag, Lines, Records, PerMinute,
   @{ Name = 'Steady/min'; Expression = { $_.SteadyPerMinute } }, @{ Name = 'Peak/min'; Expression = { $_.PeakPerMinute } }, Warnings, Errors,
   @{ Name = 'Flag'; Expression = { if ($_.Flagged) { "OVER $LimitPerMinute/min" } else { '' } } } | Out-String -Width 200
 }
 if ($problems.Count) { "FLAGGED:`n - " + ($problems -join "`n - ") } else { "PASS: no tag above $LimitPerMinute lines/min after the first $WarmupMinutes min and no debug-check mismatch." }
}
if ($FailOnFlag -and $problems.Count) { throw "Log rate check failed: $($problems.Count) problem(s)." }
