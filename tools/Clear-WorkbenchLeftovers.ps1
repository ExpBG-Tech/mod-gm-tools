#requires -Version 7.0
<#
.SYNOPSIS
One-time cleanup of EXPBG leftovers outside the repositories (dry run unless -Apply).

.DESCRIPTION
Deletes, on this release PC:
- the Workbench folder's old test copies: LocalTest, Release and Staging (September 2026 GM Optimizer
  QA and release staging; nothing in the EXPBG tooling reads them any more);
- old local install backups beside the Workbench addons folder (newest one per addon stays; backups
  of addons that are no longer installed go completely);
- Workbench's temporary Workshop bundles (%TEMP%\Arma Reforger Workbench\Publishing and Build;
  every publication writes a fresh one);
- the release housekeeping archive (ArchiveRoot, e.g. G:\EXPBG-archive): payloads moved there
  before housekeeping deleted them directly; receipts and evidence never left the repositories;
- in each EXPBG repository beside this one: tools/Invoke-Cleanup.ps1 (delete-mode housekeeping)
  and the handoff build-source copies (.local/handoff-*/buildsrc).
Keeps the Workbench addons, profile, logs and .local, every repository's .local/config.json and
e2e folders, and anything else not listed. Links are never followed. Refuses to run while Arma
Reforger, its server or Workbench is open.

.EXAMPLE
pwsh -File tools/Clear-WorkbenchLeftovers.ps1          # list what would go, with sizes
.EXAMPLE
pwsh -File tools/Clear-WorkbenchLeftovers.ps1 -Apply   # delete it
#>
param(
    [switch]$Apply,
    [string]$WorkbenchRoot = (Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'My Games/ArmaReforgerWorkbench'),
    [string]$GitHubRoot = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),
    [string]$ArchiveRoot = ''
)
$ErrorActionPreference = 'Stop'
if (Get-Process -Name ArmaReforgerSteam, ArmaReforgerSteamDiag, ArmaReforgerServer, ArmaReforgerServerDiag, ArmaReforgerWorkbenchSteam, ArmaReforgerWorkbenchSteamDiag -ErrorAction SilentlyContinue) { throw 'Close Arma Reforger, its server and Workbench first.' }
$repos = @('mod-gm-tools', 'mod-cdf-compat', 'mod-ambient-death', 'mod-ambient-radio') | ForEach-Object { Join-Path $GitHubRoot $_ } | Where-Object { Test-Path -LiteralPath (Join-Path $_ '.git') }
if (!$ArchiveRoot) { try { $ArchiveRoot = (Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot '../.local/config.json') | ConvertFrom-Json).ArchiveRoot } catch { } }

function Get-Size([string]$Path) { [long](Get-ChildItem -LiteralPath $Path -Recurse -File -Force -Attributes !ReparsePoint -ErrorAction SilentlyContinue | Measure-Object Length -Sum).Sum }
function Get-Links([string]$Path) { @(Get-ChildItem -LiteralPath $Path -Recurse -Force -Attributes ReparsePoint -ErrorAction SilentlyContinue) }
$targets = [Collections.Generic.List[object]]::new()
function Add-Target([string]$Path, [string]$Why, [switch]$UnlinkFirst) {
    if (!(Test-Path -LiteralPath $Path)) { return }
    $full = [IO.Path]::GetFullPath($Path)
    foreach ($repo in $repos) { if ($full.TrimEnd('\') -eq ([IO.Path]::GetFullPath($repo)).TrimEnd('\')) { throw "Refusing a repository root: $full" } }
    if ((Get-Item -LiteralPath $full -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { Write-Warning "Skipped (is a link): $full"; return }
    $links = (Get-Links $full).Count
    if ($links -and !$UnlinkFirst) { Write-Warning "Skipped (contains a link): $full"; return }
    $targets.Add([pscustomobject]@{ Path = $full; Why = $Why; Bytes = Get-Size $full; Links = $links })
}

# Workbench folder. Their addons folders hold junctions to Workshop downloads: on -Apply the junctions
# themselves are removed first (never followed, their targets stay), then the copies.
foreach ($name in 'LocalTest', 'Release', 'Staging') { Add-Target (Join-Path $WorkbenchRoot $name) 'old Workbench test/staging copies' -UnlinkFirst }
$addons = Join-Path $WorkbenchRoot 'addons'
foreach ($backupRoot in @(Get-ChildItem -LiteralPath $WorkbenchRoot -Directory -Force -ErrorAction SilentlyContinue | Where-Object Name -Match '^\.(.+)-local-backups$')) {
    $addonName = [regex]::Match($backupRoot.Name, '^\.(.+)-local-backups$').Groups[1].Value
    if (!(Test-Path -LiteralPath (Join-Path $addons $addonName))) { Add-Target $backupRoot.FullName "backups of $addonName (no longer installed)"; continue }
    foreach ($old in @(Get-ChildItem -LiteralPath $backupRoot.FullName -Directory -Force | Sort-Object Name -Descending | Select-Object -Skip 1)) { Add-Target $old.FullName "older install backup of $addonName" }
}
# Workbench temporary Workshop bundles
$workbenchTemp = Join-Path ([IO.Path]::GetTempPath()) 'Arma Reforger Workbench'
foreach ($name in 'Publishing', 'Build') {
    $folder = Join-Path $workbenchTemp $name
    if (Test-Path -LiteralPath $folder) { foreach ($entry in Get-ChildItem -LiteralPath $folder -Force) { Add-Target $entry.FullName "temporary Workbench $name bundle" } }
}
# Housekeeping archive: only its per-repository folders, never the archive drive itself.
if ($ArchiveRoot -and (Test-Path -LiteralPath $ArchiveRoot -PathType Container)) {
    $archiveFull = [IO.Path]::GetFullPath($ArchiveRoot).TrimEnd('\')
    if ($archiveFull -eq [IO.Path]::GetPathRoot($archiveFull).TrimEnd('\')) { throw "ArchiveRoot is a drive root: $archiveFull" }
    foreach ($entry in Get-ChildItem -LiteralPath $archiveFull -Directory -Force | Where-Object Name -Match '^mod-') { Add-Target $entry.FullName 'housekeeping archive' }
}
# Repositories: handoff build-source copies
foreach ($repo in $repos) {
    foreach ($buildsrc in @(Get-ChildItem -LiteralPath (Join-Path $repo '.local') -Directory -Force -Filter 'handoff-*' -ErrorAction SilentlyContinue | ForEach-Object { Join-Path $_.FullName 'buildsrc' } | Where-Object { Test-Path -LiteralPath $_ })) {
        Add-Target $buildsrc 'handoff build-source copy (rebuildable from Git)'
    }
}

$total = [long]($targets | Measure-Object Bytes -Sum).Sum
foreach ($target in $targets) { '{0,10:N0} MB  {1}  ({2}{3})' -f ($target.Bytes / 1MB), $target.Path, $target.Why, $(if ($target.Links) { "; $($target.Links) junction(s) unlinked first, their targets kept" }) }
'{0,10:N0} MB  total in {1} item(s) outside the repositories'' run folders' -f ($total / 1MB), $targets.Count
if (!$Apply) {
    'Dry run: nothing deleted. Repository run payloads (see below) are listed by tools/Invoke-Cleanup.ps1 -WhatIf.'
    foreach ($repo in $repos) { "== $(Split-Path -Leaf $repo)"; & (Join-Path $repo 'tools/Invoke-Cleanup.ps1') -RepoRoot $repo -WhatIf }
    'Run again with -Apply to delete.'
    return
}
$freeBefore = (Get-PSDrive -Name C).Free
foreach ($target in $targets) {
    try {
        # Directory.Delete without recursion removes only the junction/symlink, never its target.
        foreach ($link in Get-Links $target.Path) { if ($link.PSIsContainer) { [IO.Directory]::Delete($link.FullName, $false) } else { [IO.File]::Delete($link.FullName) } }
        if ((Get-Links $target.Path).Count) { throw 'a link is still present; nothing deleted' }
        Remove-Item -LiteralPath $target.Path -Recurse -Force
        "deleted $($target.Path)"
    }
    catch { Write-Warning "Not deleted (in use?): $($target.Path): $($_.Exception.Message)" }
}
foreach ($repo in $repos) { "== $(Split-Path -Leaf $repo)"; & (Join-Path $repo 'tools/Invoke-Cleanup.ps1') -RepoRoot $repo -Confirm:$false }
'Free space on C: {0:N1} GB before, {1:N1} GB after.' -f ($freeBefore / 1GB), ((Get-PSDrive -Name C).Free / 1GB)
