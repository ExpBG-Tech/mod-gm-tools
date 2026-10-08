#requires -Version 7.0
param([Parameter(Mandatory)][string]$SourceSnapshot, [switch]$OrchestratorSlotGranted)
$ErrorActionPreference = 'Stop'
if (!$OrchestratorSlotGranted) { throw 'An explicit native-slot handoff is required before running this fixture.' }
$repo = Split-Path -Parent $PSScriptRoot
. "$repo/tools/Workshop-Common.ps1"
$config = & "$repo/tools/Get-LocalConfig.ps1"
$project = & "$repo/tools/Get-ProjectConfig.ps1"
if (Get-Process -Name ArmaReforgerWorkbenchSteamDiag,ArmaReforgerSteam,ArmaReforgerSteamDiag,ArmaReforgerServer,ArmaReforgerServerDiag -ErrorAction SilentlyContinue) { throw 'Native slot is occupied.' }
$source = (Resolve-Path -LiteralPath $SourceSnapshot).Path
if (!(Test-Path -LiteralPath "$source/resourceDatabase.rdb")) { throw 'Build/index the source snapshot first.' }
$run = Join-Path $repo ('build/contracts-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff'))
$addon = Join-Path $run $project.addon.name
New-Item -ItemType Directory -Path $run | Out-Null
Copy-Item -LiteralPath $source -Destination $addon -Recurse
$fixture = Join-Path $addon 'Scripts/Game/EXPGTests'
$plugin = Join-Path $addon 'Scripts/WorkbenchGame/EXPGTests'
New-Item -ItemType Directory -Path $fixture,$plugin | Out-Null
Get-ChildItem -LiteralPath $PSScriptRoot -Filter 'EXPG_*Test.c' -File | Copy-Item -Destination $fixture
Copy-Item -LiteralPath "$PSScriptRoot/EXPG_ContractPlugin.c" -Destination $plugin
$dependencies = Join-Path $run 'dependencies'
& "$repo/tools/Copy-AddonDependencies.ps1" -Destination $dependencies -InstalledAddonsRoot $config.InstalledAddonsRoot -SearchRoots ($config.DependencyAddonsRoots -split ';')
$logs = Join-Path $run 'logs'
New-Item -ItemType Directory -Path $logs | Out-Null
$arguments = @('-disableCrashReporter','-noThrow','-wbModule=ResourceManager','-plugin=EXPG_ContractPlugin','-gproj',(Join-Path $addon $project.addon.project),'-profile',"$run/profile",'-logsDir',$logs,'-addonsDir',$dependencies)
$native = Invoke-PrivateProcess (Join-Path $config.WorkbenchRoot 'ArmaReforgerWorkbenchSteamDiag.exe') $arguments $config.GameRoot "$run/native-output.log" 120
$text = (Get-ChildItem -LiteralPath $logs -Recurse -Filter '*.log' -File | Get-Content -Raw) -join "`n"
$passed = $native.ExitCode -eq 0 -and $text -match '\[EXPG CONTRACT RESULT\] graph=1 editor=1 attributes=1 corridor=1 missingActor=1' -and $text -match '\[EXPG RANDOM CONTRACT RESULT\] rules=1 shuffle=1 packing=1' -and $text -match 'Game destroyed' -and $text -notmatch 'Can.t compile|SCRIPT\s+\(E\)|Virtual Machine Exception|Assertion failed'
@{source=$source;run=$run;passed=$passed;nativeExitCode=$native.ExitCode;worldLoaded=$false;gameplay=$false} | ConvertTo-Json | Set-Content -LiteralPath "$run/result.json"
$text -split "`n" | Where-Object { $_ -match '\[EXPG|SCRIPT\s+\(E\)|Can.t compile' } | Write-Output
# The addon copy is only an input; keep the logs and the receipt.
if (Test-Path -LiteralPath $addon) { Remove-Item -LiteralPath $addon -Recurse -Force -ErrorAction SilentlyContinue }
if (!$passed) { throw "Native contracts failed; inspect $run" }
"PASS: native scalar/graph contracts only; no gameplay proof. Evidence $run"
