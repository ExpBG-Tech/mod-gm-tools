<#
.SYNOPSIS
Cooks PNG card art into native .edds textures with Workbench's ResourceManager and
copies the cooked .edds/.meta (and the PNG source) back into a module folder.

The pack has one project (addon/EXPBG_GM_Tools.gproj), so cooking runs against a
copy of an indexed build stage. Existing .meta files are kept, so a re-titled
card keeps its GUID and every reference stays valid; new files get a GUID from
Workbench registration.

.EXAMPLE
pwsh -File tools/art/Import-PackArt.ps1 -SourceSnapshot build/local-.../EXPBG_GM_Tools -Module ambient-sounds `
  -Images @{ 'UI/Textures/EXPBG/EAS_Crowd_Card.png' = '.local/art/Ambient_Crowd_Sound_Card.png' } -OrchestratorSlotGranted

One image per call. For a new card, first copy a sibling card's .edds.meta into the
module with a fresh 16-hex GUID in its Name line; a sibling .edds is used as the stage
placeholder (a meta without its resource hangs headless Workbench).
#>
#requires -Version 7.0
param(
	[Parameter(Mandatory)][string]$SourceSnapshot,
	[Parameter(Mandatory)][string]$Module,
	[Parameter(Mandatory)][hashtable]$Images,
	[switch]$OrchestratorSlotGranted
)
$ErrorActionPreference = 'Stop'
if (!$OrchestratorSlotGranted) { throw 'An explicit native-slot handoff is required before running Workbench.' }
# RebuildResourceFiles returns at once; only the first texture is cooked before Workbench.Exit.
if ($Images.Count -ne 1) { throw 'Pass one image per call: Workbench cooks only the first texture before it exits.' }
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. "$repo/tools/Workshop-Common.ps1"
$config = & "$repo/tools/Get-LocalConfig.ps1"
$project = & "$repo/tools/Get-ProjectConfig.ps1"
if (Get-Process -Name ArmaReforgerWorkbenchSteamDiag,ArmaReforgerSteam,ArmaReforgerSteamDiag,ArmaReforgerServer,ArmaReforgerServerDiag -ErrorAction SilentlyContinue) { throw 'Native slot is occupied.' }
$source = (Resolve-Path -LiteralPath $SourceSnapshot).Path
if (!(Test-Path -LiteralPath "$source/resourceDatabase.rdb")) { throw 'Build/index the source snapshot first.' }
$moduleRoot = Join-Path $repo "addon/$Module"
if (!(Test-Path -LiteralPath $moduleRoot)) { throw "Unknown module folder: $moduleRoot" }

$run = Join-Path $repo ('build/art-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff'))
$addon = Join-Path $run $project.addon.name
New-Item -ItemType Directory -Path $run | Out-Null
Copy-Item -LiteralPath $source -Destination $addon -Recurse

$alias = $project.addon.name
$resources = @()
$placeholders = @{}
foreach ($relative in $Images.Keys) {
	if ($relative -notmatch '(?i)^UI/.+\.png$') { throw "Images must be UI/... .png paths: $relative" }
	$png = (Resolve-Path -LiteralPath (Join-Path $repo $Images[$relative])).Path
	$target = Join-Path $addon $relative
	New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
	Copy-Item -LiteralPath $png -Destination $target -Force
	# A meta prepared in the module (e.g. a new card copied from a sibling card with a
	# fresh GUID) keeps the UI texture settings; otherwise Workbench registration makes one.
	$edds = $relative -replace '(?i)\.png$', '.edds'
	$moduleMeta = Join-Path $moduleRoot "$edds.meta"
	if (Test-Path -LiteralPath $moduleMeta) {
		Copy-Item -LiteralPath $moduleMeta -Destination (Join-Path $addon "$edds.meta") -Force
		# A meta without its resource makes Workbench stop at an error and hang headless.
		# Any sibling .edds is a placeholder; the rebuild replaces it from the PNG (checked below).
		$stageEdds = Join-Path $addon $edds
		if (!(Test-Path -LiteralPath $stageEdds)) {
			$sibling = Get-ChildItem -LiteralPath (Split-Path -Parent $stageEdds) -Filter '*.edds' -File | Select-Object -First 1
			if (!$sibling) { throw "No placeholder texture beside $edds; let Workbench create the meta instead." }
			Copy-Item -LiteralPath $sibling.FullName -Destination $stageEdds
		}
	}
	# Any texture already in the stage (old card or placeholder) must be replaced by the cook.
	$stageTexture = Join-Path $addon $edds
	if (Test-Path -LiteralPath $stageTexture) { $placeholders[$edds] = (Get-FileHash -LiteralPath $stageTexture -Algorithm SHA256).Hash }
	$resources += $relative
}

$pluginDir = Join-Path $addon 'Scripts/WorkbenchGame/EXPBGArt'
New-Item -ItemType Directory -Path $pluginDir -Force | Out-Null
$list = ($resources | ForEach-Object { '"' + $_ + '"' }) -join ', '
# Registering a file that already has a meta waits for a GUI answer and hangs headless;
# registered textures (existing or prepared meta) are only rebuilt.
$registerList = ($resources | ForEach-Object { if (Test-Path -LiteralPath (Join-Path $addon (($_ -replace '(?i)\.png$', '.edds') + '.meta'))) { 'false' } else { 'true' } }) -join ', '
@"
[WorkbenchPluginAttribute(name: "EXPBG pack art import", wbModules: {"ResourceManager"})]
class EXPBG_ArtImportPlugin : WorkbenchPlugin
{
	override void RunCommandline()
	{
		ResourceManager manager = Workbench.GetModule(ResourceManager);
		if (!manager) { Workbench.Exit(1); return; }
		array<string> pngs = { $list };
		array<bool> register = { $registerList };
		array<string> cooked = {};
		foreach (int i, string png : pngs)
		{
			string input;
			if (!Workbench.GetAbsolutePath("`$${alias}:" + png, input)) { Print("[EXPBG ART] missing " + png, LogLevel.ERROR); Workbench.Exit(2); return; }
			if (register[i] && !manager.RegisterResourceFile(input, true)) { Print("[EXPBG ART] register failed " + png, LogLevel.ERROR); Workbench.Exit(3); return; }
			string edds = png.Substring(0, png.Length() - 4) + ".edds";
			cooked.Insert("`$${alias}:" + edds);
			Print("[EXPBG ART] queued " + png + " register=" + register[i].ToString());
		}
		manager.RebuildResourceFiles(cooked, "PC");
		Print("[EXPBG ART] rebuild returned for " + cooked.Count().ToString() + " texture(s)");
		Workbench.Exit(0);
	}
}
"@ | Set-Content -LiteralPath (Join-Path $pluginDir 'EXPBG_ArtImportPlugin.c') -Encoding utf8

$dependencies = Join-Path $run 'dependencies'
& "$repo/tools/Copy-AddonDependencies.ps1" -Destination $dependencies -InstalledAddonsRoot $config.InstalledAddonsRoot
$logs = Join-Path $run 'logs'
New-Item -ItemType Directory -Path $logs | Out-Null
$arguments = @('-disableCrashReporter','-noThrow','-wbModule=ResourceManager','-plugin=EXPBG_ArtImportPlugin','-gproj',(Join-Path $addon $project.addon.project),'-profile',"$run/profile",'-logsDir',$logs,'-addonsDir',$dependencies)
$native = Invoke-PrivateProcess (Join-Path $config.WorkbenchRoot 'ArmaReforgerWorkbenchSteamDiag.exe') $arguments $config.GameRoot "$run/native-output.log" 180
$text = (Get-ChildItem -LiteralPath $logs -Recurse -Filter '*.log' -File | Get-Content -Raw) -join "`n"
$text -split "`n" | Where-Object { $_ -match '\[EXPBG ART\]|SCRIPT\s+\(E\)|Can.t compile' } | Write-Output
if ($native.ExitCode -ne 0 -or $text -notmatch '\[EXPBG ART\] rebuild returned') { throw "Art import failed (exit $($native.ExitCode)); inspect $run" }

$copied = @()
foreach ($relative in $resources) {
	$edds = $relative -replace '(?i)\.png$', '.edds'
	foreach ($file in @($relative, $edds, "$edds.meta")) {
		$from = Join-Path $addon $file
		if (!(Test-Path -LiteralPath $from)) { throw "Workbench did not produce $file; inspect $run" }
		if ($placeholders.ContainsKey($file) -and (Get-FileHash -LiteralPath $from -Algorithm SHA256).Hash -eq $placeholders[$file]) { throw "Workbench did not re-cook $file (bytes unchanged); inspect $run" }
		$to = Join-Path $moduleRoot $file
		New-Item -ItemType Directory -Path (Split-Path -Parent $to) -Force | Out-Null
		Copy-Item -LiteralPath $from -Destination $to -Force
		$copied += $to
	}
	$guid = ([regex]::Match((Get-Content -LiteralPath (Join-Path $addon "$edds.meta") -Raw), 'Name "\{([0-9A-F]{16})\}')).Groups[1].Value
	Write-Output "cooked $edds GUID {$guid}"
}
@{ run = $run; copied = $copied } | ConvertTo-Json | Set-Content -LiteralPath "$run/result.json"
"PASS: cooked $($resources.Count) texture(s); evidence $run"
