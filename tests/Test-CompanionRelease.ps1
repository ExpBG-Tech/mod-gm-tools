#requires -Version 7.0
$ErrorActionPreference = 'Stop'
if (!(& "$PSScriptRoot/../tools/Get-ProjectConfig.ps1").companion) {
 'SKIP: no companion package is configured for this repository.'
 exit 0
}
$repo = Split-Path -Parent $PSScriptRoot
. "$repo/tools/Workshop-Common.ps1"
function Rejects([scriptblock]$Action) {
 $rejected = $false
 try { & $Action | Out-Null } catch { $rejected = $true }
 if (!$rejected) { throw 'Expected release guard rejection.' }
}
$core = & "$repo/tools/Get-ReleaseConfig.ps1"
$cdf = & "$repo/tools/Get-ReleaseConfig.ps1" -Companion
if ($core.addon.id -ne 'F3B7C6FB18AB1F79' -or $cdf.addon.id -ne '8C5A6D9E73B241F0') { throw 'Release target identity changed.' }
if ($cdf.asset.id -ne $cdf.addon.id -or $cdf.versionFile -eq $core.versionFile -or $cdf.tagPrefix -eq $core.tagPrefix) { throw 'Companion release is not independently versioned.' }
if (!$cdf.asset.unlisted -or $cdf.asset.private -or $cdf.asset.license -ne 'Arma Public License Share Alike (APL-SA)') { throw 'Companion visibility/license differs.' }
if ($cdf.addon.dependencies.Count -ne 3 -or $core.addon.dependencies.Count -ne 1) { throw 'Core dependency independence lost.' }
Assert-WorkshopVersion 'cdf-v0.1.0' '0.1.0' 'cdf-v'
Rejects { Assert-WorkshopVersion 'v0.1.0' '0.1.0' 'cdf-v' }
$temp = Join-Path $repo ('.local/companion-release-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path "$temp/artifacts/core" -Force | Out-Null
@{item=$core.addon.id; version='0.1.0'; uploadAttempted=$true} | ConvertTo-Json | Set-Content "$temp/artifacts/core/workshop-receipt.json"
Assert-NoWorkshopUploadAttempt $temp '0.1.0' $cdf.addon.id
Rejects { Assert-NoWorkshopUploadAttempt $temp '0.1.0' $core.addon.id }
# Old receipts without an identity stay fail-closed.
@{version='0.1.0'; uploadAttempted=$true} | ConvertTo-Json | Set-Content "$temp/artifacts/core/workshop-receipt.json"
Rejects { Assert-NoWorkshopUploadAttempt $temp '0.1.0' $cdf.addon.id }
# Exercise immutable companion tags against a real disposable Git history.
New-Item -ItemType Directory -Path "$temp/tools", (Join-Path $temp (Split-Path (Join-Path $cdf.addon.source $cdf.addon.preview))), (Join-Path $temp $core.addon.source) -Force | Out-Null
foreach ($file in 'project.json','workshop-asset.json','workshop-cdf-asset.json') { Copy-Item "$repo/tools/$file" "$temp/tools/$file" }
'preview' | Set-Content (Join-Path $temp (Join-Path $cdf.addon.source $cdf.addon.preview))
'core' | Set-Content (Join-Path $temp "$($core.addon.source)/fixture.c")
'0.1.0' | Set-Content "$temp/CDF_VERSION"
'0.1.24' | Set-Content "$temp/VERSION"
'notes' | Set-Content "$temp/CDF_CHANGELOG.md"
'license' | Set-Content "$temp/LICENSE"
'artifacts/' | Set-Content "$temp/.gitignore"
git -C $temp init -q -b main
git -C $temp config user.name Fixture
git -C $temp config user.email fixture@example.invalid
git -C $temp add .
git -C $temp commit -qm initial
git -C $temp update-ref refs/remotes/origin/main HEAD
$initial = Get-LocalReleaseSource $temp 'cdf-v0.1.0' -Companion
if (!$initial.createTag -or $initial.version -ne '0.1.0') { throw 'Companion did not select its own release version/tag.' }
Rejects { Get-LocalReleaseSource $temp 'v0.1.0' -Companion }
git -C $temp tag cdf-v0.1.0
$existing = Get-LocalReleaseSource $temp 'cdf-v0.1.0' -Companion
if ($existing.createTag) { throw 'Existing companion tag not recognized.' }
'changed core' | Add-Content (Join-Path $temp "$($core.addon.source)/fixture.c")
git -C $temp add .
git -C $temp commit -qm changed-core
git -C $temp update-ref refs/remotes/origin/main HEAD
Rejects { Get-LocalReleaseSource $temp 'cdf-v0.1.0' -Companion }
$name = 'cdf-stage-test-' + [guid]::NewGuid().ToString('N')
$stage = & "$repo/tools/Stage-Release.ps1" -Name $name -Companion | ConvertFrom-Json
if ($stage.version -ne (Get-Content (Join-Path $repo $cdf.versionFile) -Raw).Trim() -or !(Test-Path (Join-Path $stage.stage $cdf.addon.project))) { throw 'Companion staging used core inputs.' }
if (Test-Path (Join-Path (Split-Path $stage.stage) $core.addon.name)) { throw 'Core runtime bundled into companion package.' }
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [IO.Compression.ZipFile]::OpenRead($stage.zip)
try {
 if (@($zip.Entries | Where-Object { $_.FullName -notmatch '^(EXPBG_GM_Optimizer_CDF/|LICENSE$)' }).Count) { throw 'Unexpected companion source archive entry.' }
} finally { $zip.Dispose() }
'PASS: independent companion identity, version/tag, visibility/license, upload-attempt isolation and source archive.'
exit 0
