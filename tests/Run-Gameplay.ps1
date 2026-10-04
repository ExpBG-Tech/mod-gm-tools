#requires -Version 7.0
param(
 [Parameter(Mandatory)][string]$SourceSnapshot,
 [switch]$OrchestratorSlotGranted,
 [ValidateRange(300,600)][int]$TimeoutSeconds = 360
)
$ErrorActionPreference = 'Stop'
if (!$OrchestratorSlotGranted) { throw 'Explicit orchestrator native-slot handoff required. This launches a diagnostic server.' }
$repo = Split-Path -Parent $PSScriptRoot
$config = & "$repo/tools/Get-LocalConfig.ps1"
$project = & "$repo/tools/Get-ProjectConfig.ps1"
$nativeNames = @('ArmaReforgerWorkbenchSteamDiag','ArmaReforgerSteam','ArmaReforgerSteamDiag','ArmaReforgerServer','ArmaReforgerServerDiag')
function Assert-NativeSlot {
 if (Get-Process -Name $nativeNames -ErrorAction SilentlyContinue) { throw 'Native slot occupied; no existing process will be stopped.' }
}
Assert-NativeSlot
$source = (Resolve-Path -LiteralPath $SourceSnapshot).Path
if (!(Test-Path -LiteralPath "$source/resourceDatabase.rdb" -PathType Leaf)) { throw 'Use a built/indexed addon snapshot.' }
$sourceProject = Join-Path $source $project.addon.project
if (!(Test-Path -LiteralPath $sourceProject) -or (Get-Content -LiteralPath $sourceProject -Raw) -notmatch ('GUID\s+"?' + $project.addon.id + '\b')) { throw 'Snapshot must match this project identity.' }
$engine = Join-Path $config.ServerRoot 'ArmaReforgerServerDiag.exe'
if (!(Test-Path -LiteralPath $engine -PathType Leaf)) { throw 'Configure ServerRoot with the native diagnostic server installation.' }
$run = Join-Path $repo ('build/gameplay-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff'))
$addons = Join-Path $run 'addons'
New-Item -ItemType Directory -Path $run | Out-Null
& "$repo/tools/Copy-AddonDependencies.ps1" -Destination $addons -InstalledAddonsRoot $config.InstalledAddonsRoot
Copy-Item -LiteralPath $source -Destination (Join-Path $addons $project.addon.name) -Recurse
$fixture = Join-Path $addons 'EXPG_GameplayFixture'
$logs = Join-Path $run 'logs'
New-Item -ItemType Directory -Path "$fixture/Scripts/Game","$fixture/Worlds/Garrison_Layers",$logs | Out-Null
Copy-Item -LiteralPath "$PSScriptRoot/EXPG_GarrisonGameplay.c" -Destination "$fixture/Scripts/Game"
@'
GameProject {
 ID "EXPG_GameplayFixture"
 GUID "67CC618B744C46A1"
 TITLE "EXPBG Garrison local gameplay fixture"
 Dependencies {
  "58D0FB3206B6F859"
  "F3B7C6FB18AB1F79"
  "FC1402F65B2F4A45"
 }
}
'@ | Set-Content -LiteralPath "$fixture/EXPG_GameplayFixture.gproj" -Encoding utf8NoBOM
@'
SubScene {
 Parent "{BEF094A5F7F3211B}worlds/GameMaster/GM_Eden.ent"
}
'@ | Set-Content -LiteralPath "$fixture/Worlds/Garrison.ent" -Encoding utf8NoBOM
@'
EXPG_GarrisonGameplay GarrisonDriver {
 coords 4773 169 7094
}
'@ | Set-Content -LiteralPath "$fixture/Worlds/Garrison_Layers/default.layer" -Encoding utf8NoBOM
'MetaFileClass { Name "{69C0B9B168C54EAB}Worlds/Garrison.ent" Configurations { ENTResourceClass PC {} ENTResourceClass HEADLESS : PC {} } }' | Set-Content -LiteralPath "$fixture/Worlds/Garrison.ent.meta" -Encoding utf8NoBOM
# Evidence hashes include exact candidate, dependencies and fixture, not just Git HEAD.
@(Get-ChildItem -LiteralPath $addons -Recurse -File | ForEach-Object {
 [ordered]@{path=$_.FullName.Substring($addons.Length + 1);sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}
}) | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath "$run/inputs.json"
$arguments = @('-disableCrashReporter','-addonsDir',$addons,'-addons','67CC618B744C46A1','-profile',"$run/profile",'-logsDir',$logs,'-server','Worlds/Garrison.ent','-worldSystemsConfig','{8DDC2A311929D52F}Configs/Systems/GameMasterSystems.conf','-maxFPS','60')
$start = [Diagnostics.ProcessStartInfo]::new($engine)
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
$start.WorkingDirectory = $config.ServerRoot
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
foreach ($argument in $arguments) { $start.ArgumentList.Add($argument) }
$process = [Diagnostics.Process]::new()
$process.StartInfo = $start
Assert-NativeSlot
if (!$process.Start()) { throw 'Diagnostic server failed to start.' }
$started = $process.StartTime.ToUniversalTime()
$receipt = [ordered]@{source=$source;run=$run;pid=$process.Id;startedUtc=$started.ToString('o');executable=$engine;arguments=$arguments;timeoutSeconds=$TimeoutSeconds;timedOut=$false;ownedProcessStopped=$false;nativeExitCode=$null;passed=$false}
$receipt | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath "$run/run.json"
$stdout = $process.StandardOutput.ReadToEndAsync()
$stderr = $process.StandardError.ReadToEndAsync()
try {
 if (!$process.WaitForExit($TimeoutSeconds * 1000)) {
  $receipt.timedOut = $true
  # Only the process launched above may be stopped. Fail closed on identity drift.
  $current = Get-Process -Id $process.Id -ErrorAction Stop
  if ($current.StartTime.ToUniversalTime().Ticks -ne $started.Ticks -or [IO.Path]::GetFullPath($current.Path) -ine [IO.Path]::GetFullPath($engine)) { throw 'Timeout, but process ownership could not be verified; no process was stopped.' }
  $process.Kill($true)
  $receipt.ownedProcessStopped = $true
  if (!$process.WaitForExit(10000)) { throw 'Owned diagnostic process did not exit after timeout.' }
 }
 $receipt.nativeExitCode = $process.ExitCode
 $output = $stdout.GetAwaiter().GetResult() + "`n" + $stderr.GetAwaiter().GetResult()
 [IO.File]::WriteAllText("$run/native-output.log", $output)
 $text = $output + "`n" + ((Get-ChildItem -LiteralPath $run -Recurse -Filter '*.log' -File | Where-Object Name -ne 'native-output.log' | Get-Content -Raw) -join "`n")
 $terminal = [regex]::Match($text, '\[EXPG GAMEPLAY RESULT\] phase=10 checks=(\d+) failures=0 actors=4 fixedPosts=([1-9]\d*) reason=completed')
 $receipt.passed = !$receipt.timedOut -and $process.ExitCode -eq 0 -and $terminal.Success -and $text -match 'Game destroyed' -and $text -notmatch 'Can.t compile|SCRIPT\s+\(E\)|Virtual Machine Exception|Assertion failed|ENGINE\s+\(F\): Crashed'
 $text -split "`n" | Where-Object { $_ -match '\[EXPG|SCRIPT\s+\(E\)|Can.t compile' } | Write-Output
} finally {
 $receipt | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath "$run/result.json"
}
if (!$receipt.passed) { throw "Gameplay fixture not passed; inspect $run" }
"PASS: bounded native server smoke only. No GM UI, player-distance crossing, combat, multiplayer/JIP or persistence proof. Evidence: $run"
