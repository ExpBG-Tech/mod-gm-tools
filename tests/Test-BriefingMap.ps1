#requires -Version 7.0
# Portable guard for Advanced Briefing Map (live test 2026-10-07: the map on the board
# was turned a quarter turn and showed no roads or buildings; the user asked for the
# Heine "Prop - Projector Screen" instead of the wall map). Checks: the imported model
# closure and its metadata, prefab and model references, the projector and wall-map
# screen UV/turn settings (the turned canvas must cover each screen's UV rectangle),
# the canvas hand-off layout, the game map renderer wiring, attribution and Enforce
# gotchas. No engine is launched; render, orientation and roads need a native check.
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$addonRoot = Join-Path $repo 'addon'
$module = Join-Path $addonRoot 'advanced-briefing-map'
function Assert([bool]$Condition, [string]$Message) { if (!$Condition) { throw "FAIL: $Message" } }
function Read-Text([string]$Path) { [IO.File]::ReadAllText($Path) }
function Module-Path([string]$Relative) { Join-Path $module $Relative }
function Get-Body([string]$Text, [string]$Signature) {
 $match = [regex]::Match($Text, $Signature)
 Assert $match.Success "signature not found: $Signature"
 $open = $Text.IndexOf('{', $match.Index); $depth = 0
 for ($i = $open; $i -lt $Text.Length; $i++) {
  if ($Text[$i] -eq '{') { $depth++ } elseif ($Text[$i] -eq '}') { $depth--; if ($depth -eq 0) { return $Text.Substring($open + 1, $i - $open - 1) } }
 }
 throw "FAIL: unbalanced body for $Signature"
}
function Get-Number([string]$Text, [string]$Name) {
 $match = [regex]::Match($Text, '(?m)^\s*' + [regex]::Escape($Name) + '\s+(-?[0-9.]+)\s*$')
 Assert $match.Success "$Name is not set"
 [double]::Parse($match.Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture)
}

# Every resource GUID in the pack, from the module metadata. Only the merged shared vanilla
# overrides (Systems.conf and friends) repeat one identity, always with the same path.
$metaGuids = @{}
foreach ($meta in Get-ChildItem -LiteralPath $addonRoot -Recurse -File -Filter '*.meta') {
 $match = [regex]::Match((Read-Text $meta.FullName), 'Name\s+"\{([A-Fa-f0-9]{16})\}([^"\r\n]+)"')
 Assert $match.Success "metadata without a resource GUID: $($meta.FullName)"
 $guid = $match.Groups[1].Value.ToUpperInvariant()
 if ($metaGuids.ContainsKey($guid) -and $metaGuids[$guid].Path -ceq $match.Groups[2].Value -and $match.Groups[2].Value -like 'Configs/*.conf') { continue }
 Assert (!$metaGuids.ContainsKey($guid)) "resource GUID $guid is used by $($meta.FullName) and $($metaGuids[$guid].Meta)"
 $metaGuids[$guid] = [pscustomobject]@{ Meta = $meta.FullName; Path = $match.Groups[2].Value }
}
$moduleGuids = @{}
foreach ($meta in Get-ChildItem -LiteralPath $module -Recurse -File -Filter '*.meta') {
 $match = [regex]::Match((Read-Text $meta.FullName), 'Name\s+"\{([A-Fa-f0-9]{16})\}([^"\r\n]+)"')
 $resource = $meta.FullName.Substring($module.Length + 1, $meta.FullName.Length - $module.Length - 6).Replace('\', '/')
 Assert ($match.Groups[2].Value -ceq $resource) "metadata path differs from its file: $resource"
 Assert (Test-Path -LiteralPath ($meta.FullName.Substring(0, $meta.FullName.Length - 5)) -PathType Leaf) "metadata without its resource: $resource"
 $moduleGuids[$match.Groups[1].Value.ToUpperInvariant()] = $resource
}
# Base-game resources the module may reference (never copied).
$vanilla = @{
 '10C26F96EF7A8CF5' = 'Prefabs/Props/Core/Props_Base.et'
 'DCBC472CDA3EA9B2' = 'Prefabs/Props/Civilian/WallMap_01.et'
 'B193E926C1935A4C' = 'Prefabs/Editor/Components/Default_RplComponent.ct'
 '996046FE206C699A' = 'Prefabs/Editor/Components/Default_SCR_EditableEntityComponent.ct'
 '5EAA7FB0A83F90CF' = 'Common/Materials/Game/plastic.gamemat'
 'E8850FCD9219C411' = 'UI/layouts/Map/MapDrawLine.layout'
 '1B8AC767E06A0ACD' = 'Configs/Map/MapFullscreen.conf'
 'BCD5479864D1A79A' = 'UI/Textures/Map/topographicIcons/icons_topographic_map.imageset'
 'F7E8D4834A3AFF2F' = 'UI/Imagesets/Conflict/conflict-icons-bw.imageset'
 '9AF52DEEF08E7F74' = 'UI/Imagesets/Editor/editor_icons_map.imageset'
 '48D3C3A42E53D202' = 'UI/Textures/Map/lines/lineDashed.edds'
 '3E7733BAC8C831F6' = 'UI/Fonts/RobotoCondensed/RobotoCondensed_Regular.fnt'
}
function Assert-References([string]$Text, [string]$Where) {
 foreach ($reference in [regex]::Matches($Text, '\{([0-9A-F]{16})\}([A-Za-z0-9_./ -]+\.(?:et|ct|xob|emat|edds|layout|conf|gamemat|imageset|fnt))')) {
  $guid = $reference.Groups[1].Value; $path = $reference.Groups[2].Value
  if ($vanilla.ContainsKey($guid)) { Assert ($vanilla[$guid] -ceq $path) "base-game reference $guid has the wrong path in ${Where}: $path"; continue }
  Assert ($moduleGuids.ContainsKey($guid)) "$Where references $guid ($path), which is neither in this module nor an allowed base-game resource"
  Assert ($moduleGuids[$guid] -ceq $path) "$Where references $guid with path $path, but its metadata names $($moduleGuids[$guid])"
 }
}

# Imported projector closure: five resources with metadata, hashes as recorded, references resolving.
$art = 'EBMArt/NewProps/Tela_Projetor'
$expected = @("$art/TelaProjetor.xob", "$art/Data/tela1.emat", "$art/Data/telao.emat", "$art/Data/pln_sunum_perdesi_BaseMap.edds", "$art/Data/pln_sunum_perdesi_Normal.edds")
$shipped = @(Get-ChildItem -LiteralPath (Module-Path 'EBMArt') -Recurse -File | Where-Object Extension -ne '.meta' | ForEach-Object { $_.FullName.Substring($module.Length + 1).Replace('\', '/') } | Sort-Object)
Assert (($shipped -join '|') -ceq (($expected | Sort-Object) -join '|')) "EBMArt must hold exactly the projector closure (found: $($shipped -join ', '))"
Assert (!(Test-Path -LiteralPath (Module-Path "$art/Data/perde_gorsel_diff.edds"))) 'the upstream screen image (a captured video frame) must not be shipped'
$provenance = Read-Text (Join-Path $repo 'docs/licenses/advanced-briefing-map/imported-assets.json') | ConvertFrom-Json
Assert ($provenance.sources[0].workshopId -ceq '628EDA2ABC937159' -and $provenance.sources[0].license -match 'APL-SA') 'provenance must name Structures For GM byHeine (628EDA2ABC937159, APL-SA)'
Assert (@($provenance.assets).Count -eq $expected.Count) 'provenance must list each imported resource once'
foreach ($asset in $provenance.assets) {
 $file = Module-Path $asset.path
 Assert ($asset.path -cin $expected) "provenance lists an unexpected file: $($asset.path)"
 Assert ((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ieq $asset.sha256 -and (Get-Item -LiteralPath $file).Length -eq $asset.bytes) "imported file differs from its provenance record: $($asset.path)"
 Assert ($moduleGuids[$asset.guid] -ceq $asset.path) "provenance GUID differs from the metadata: $($asset.path)"
 Assert (!$vanilla.ContainsKey($asset.guid)) "imported GUID collides with a base-game reference: $($asset.path)"
}
$model = [Text.Encoding]::Latin1.GetString([IO.File]::ReadAllBytes((Module-Path "$art/TelaProjetor.xob")))
Assert ($model.StartsWith('FORM') -and $model.Substring(8, 8) -ceq 'XOB9HEAD') 'TelaProjetor.xob must be a compiled XOB9 model'
Assert-References $model 'TelaProjetor.xob'
Assert ($model.Contains("{787E84A9DAC0DEFC}$art/Data/tela1.emat") -and $model.Contains("{A5B41CA1F3B60085}$art/Data/telao.emat")) 'the model must reference the remapped tela1/telao materials'
Assert (!$model.Contains('Models/NewProps')) 'the model still references upstream Models/ paths'
Assert-References (Read-Text (Module-Path "$art/Data/telao.emat")) 'telao.emat'
Assert ((Read-Text (Module-Path "$art/Data/tela1.emat")) -notmatch '\{[0-9A-F]{16}\}') 'tela1.emat is the EXPBG placeholder and references no texture'

# Prefabs: the projector is the Game Master item; the wall map stays for existing saves.
$projectorPath = 'PrefabsEditable/EXPBM/EBM_BriefingProjector.et'
$boardPath = 'PrefabsEditable/EXPBM/EBM_BriefingBoard.et'
$projector = Read-Text (Module-Path $projectorPath)
$board = Read-Text (Module-Path $boardPath)
Assert-References $projector $projectorPath
Assert-References $board $boardPath
Assert-References (Read-Text (Module-Path 'Configs/Editor/PlaceableEntities/Systems/Systems.conf')) 'Systems.conf'
$systems = Read-Text (Module-Path 'Configs/Editor/PlaceableEntities/Systems/Systems.conf')
Assert ($systems.Contains("{C088CA797526BB5A}$projectorPath") -and !$systems.Contains('EBM_BriefingBoard.et')) 'Systems must offer the projector screen instead of the wall-map board'
Assert ((Read-Text (Module-Path "$boardPath.meta")).Contains("{690232440107FA20}$boardPath")) 'the wall-map board keeps its identity (saves reference it)'
Assert ($projector.Contains("Object `"{795B11AE8F4687D6}$art/TelaProjetor.xob`"")) 'the projector prefab must use the imported model'
Assert ($projector -match '(?s)SourceMaterial "tela1"\s+AssignedMaterial "\{AA3CD43539C7EF56\}Assets/EXPBM/EBM_BoardRT\.emat"') 'the render target material must go on the screen slot tela1'
Assert (!$projector.Contains('Screen01') -and !$projector.Contains('ApproachRadar')) "Heine's prefab (Screen01 slot, approach radar parent) must not be reused"
Assert ($projector -match '(?s)RigidBody "\{[0-9A-F]{16}\}" \{\s+ModelGeometry 1\s+Static 1') 'the projector must collide with its model geometry, static'
Assert ($projector.Contains('EBM_BriefAction') -and $projector.Contains('Name "#EXPBG-BriefingProjector_Name"') -and $projector.Contains('m_bAutoRegister ALWAYS')) 'the projector must carry the brief action and its editable entity name key'
Assert ($projector -match '(?s)Offset 0\.1 1\.23 0\s+\}\s+Radius 2') 'the brief action context must sit at the screen centre with a 2 m radius'
Assert ((Get-Number $projector 'm_fBoardScale') -eq 1 -and $projector -notmatch '(?m)^\s*scale\s') 'the projector keeps its model size (about 4 x 2.3 m)'
Assert ($board.Contains('SourceMaterial "Map_01_Map"') -and $board -match '(?m)^ scale 2$') 'the wall-map board keeps its material slot and scale'

# Screen UV rectangles (decoded from the models) and the turn that makes the canvas upright.
$component = Read-Text (Module-Path 'Scripts/Game/EXPBM/EBM_BriefingBoardComponent.c')
foreach ($case in @(
  @{ Text = $projector; Name = 'projector'; UMin = 0.0505; UMax = 0.6223; VMin = 0.0087; VMax = 0.9912; Rotation = -90 },
  @{ Text = $board; Name = 'wall map'; UMin = 0.0006; UMax = 0.7458; VMin = 0.0007; VMax = 0.9996; Rotation = 90 })) {
 foreach ($field in 'UMin', 'UMax', 'VMin', 'VMax') { Assert ([math]::Abs((Get-Number $case.Text "m_fScreen$field") - $case[$field]) -lt 0.00005) "$($case.Name): m_fScreen$field must be $($case[$field])" }
 Assert ((Get-Number $case.Text 'm_fScreenRotation') -eq $case.Rotation) "$($case.Name): m_fScreenRotation must be $($case.Rotation)"
 # Same arithmetic as LayoutScreen: canvas = UV rectangle turned upright, centred on it.
 $size = 1024
 $width = [math]::Round(($case.VMax - $case.VMin) * $size); $height = [math]::Round(($case.UMax - $case.UMin) * $size)
 $centerX = ($case.UMin + $case.UMax) * 0.5 * $size; $centerY = ($case.VMin + $case.VMax) * 0.5 * $size
 # A quarter turn swaps the canvas axes inside the render target.
 Assert ([math]::Abs(($centerX - $height / 2) - $case.UMin * $size) -le 1 -and [math]::Abs(($centerX + $height / 2) - $case.UMax * $size) -le 1) "$($case.Name): the turned canvas must cover the U span"
 Assert ([math]::Abs(($centerY - $width / 2) - $case.VMin * $size) -le 1 -and [math]::Abs(($centerY + $width / 2) - $case.VMax * $size) -le 1) "$($case.Name): the turned canvas must cover the V span"
}
Assert ($component -match '\[Attribute\("-90"[^\]]*\]\s+protected float m_fScreenRotation;') 'the component default turn is the projector screen'
Assert ($component -match '\[Attribute\("2"[^\]]*\]\s+protected float m_fBoardScale;') 'the default board scale stays 2 for the wall-map board'
Assert ($component -notmatch '\]\s+protected int m_iRtWidth;' -and $component -notmatch '\]\s+protected int m_iRtHeight;') 'canvas size is derived from the UV rectangle, not an attribute'
$layoutScreen = Get-Body $component 'protected\s+void\s+LayoutScreen\s*\('
Assert ($layoutScreen.Contains('m_iRtWidth = Math.Round(spanV);') -and $layoutScreen.Contains('m_iRtHeight = Math.Round(spanU);')) 'a quarter turn must swap the canvas axes'
Assert ($layoutScreen.Contains('m_wScreen.SetImageTexture(0, m_wCanvas);') -and $layoutScreen.Contains('m_wScreen.SetPivot(0.5, 0.5);') -and $layoutScreen.Contains('m_wScreen.SetRotation(m_fScreenRotation);')) 'the canvas must be shown in the render target turned about its centre'
Assert ($layoutScreen.Contains('FrameSlot.SetSize(m_wRoot, size, size);') -and $layoutScreen.Contains('FrameSlot.SetSize(m_wRT, size, size);')) 'the render target must be square (texel aspect of both screens)'
$toBoard = Get-Body $component 'protected\s+void\s+ToBoard\s*\('
Assert ($toBoard.Contains('boardX = m_iRtWidth * 0.5 + (worldX - m_fCenterX) * m_fScale;') -and $toBoard.Contains('boardY = m_iRtHeight * 0.5 - (worldY - m_fCenterY) * m_fScale;')) 'ToBoard must keep east right and north up on the canvas'

# Layout: board content in its own canvas, the render target shows only the turned screen image.
$layout = Read-Text (Module-Path 'UI/layouts/EXPBM/EBM_BoardRT.layout')
Assert-References $layout 'EBM_BoardRT.layout'
$canvasAt = $layout.IndexOf('Name "EBM_Canvas"'); $contentAt = $layout.IndexOf('Name "EBM_Content"'); $rtAt = $layout.IndexOf('Name "RTTexture0"'); $screenAt = $layout.IndexOf('Name "EBM_Screen"')
Assert ($canvasAt -gt 0 -and $canvasAt -lt $contentAt -and $contentAt -lt $rtAt -and $rtAt -lt $screenAt) 'layout order: EBM_Canvas holding EBM_Content, then RTTexture0 holding EBM_Screen'
Assert ($layout -match '(?s)RTTextureWidgetClass "\{[0-9A-F]{16}\}" \{\s+Name "EBM_Canvas"' -and $layout -match '(?s)RTTextureWidgetClass "\{[0-9A-F]{16}\}" \{\s+Name "RTTexture0"' -and $layout -match '(?s)ImageWidgetClass "\{[0-9A-F]{16}\}" \{\s+Name "EBM_Screen"') 'EBM_Canvas and RTTexture0 must be render targets, EBM_Screen an image'
foreach ($name in 'EBM_Paper', 'EBM_Map', 'EBM_EngineMapSlot', 'EBM_Lines', 'EBM_Markers', 'EBM_Title') {
 $at = $layout.IndexOf("Name `"$name`"")
 Assert ($at -gt $contentAt -and $at -lt $rtAt) "$name must be drawn on the canvas"
}
Assert ($layout.IndexOf('Name "EBM_Map"') -lt $layout.IndexOf('Name "EBM_EngineMapSlot"') -and $layout.IndexOf('Name "EBM_EngineMapSlot"') -lt $layout.IndexOf('Name "EBM_Lines"')) 'the game map must lie over the map image and under lines and markers'
Assert ($layout -match '(?s)Name "EBM_Root".*?SizeX 1024\s+OffsetRight -1024\s+SizeY 1024\s+OffsetBottom -1024') 'the layout root must default to the square render target'
Assert ([regex]::Matches($layout, '"\{([0-9A-F]{16})\}"').Count -eq @([regex]::Matches($layout, '"\{([0-9A-F]{16})\}"') | ForEach-Object { $_.Groups[1].Value } | Select-Object -Unique).Count) 'layout widget IDs must be unique'

# Roads, buildings, names: the game's own map renderer, never the player's map flow.
$engineLayoutPath = 'UI/layouts/EXPBM/EBM_EngineMap.layout'
$engineLayout = Read-Text (Module-Path $engineLayoutPath)
Assert-References $engineLayout 'EBM_EngineMap.layout'
Assert ($engineLayout.StartsWith('MapWidgetClass ') -and $engineLayout.Contains('Name "EBM_EngineMap"') -and $engineLayout.Contains('Clear 0') -and $engineLayout.Contains('"Ignore Cursor" 1')) 'the board map widget must be a transparent MapWidget that ignores the cursor'
Assert ($engineLayout.Contains('"{BCD5479864D1A79A}UI/Textures/Map/topographicIcons/icons_topographic_map.imageset" "{F7E8D4834A3AFF2F}UI/Imagesets/Conflict/conflict-icons-bw.imageset" "{9AF52DEEF08E7F74}UI/Imagesets/Editor/editor_icons_map.imageset"')) 'the map widget needs the vanilla Map.layout imagesets in their order (descriptor icon mapping)'
Assert ($engineLayout -notmatch 'Name "MapWidget"') 'the board map widget must not take the vanilla map widget name'
Assert ($component.Contains("ENGINE_MAP_LAYOUT = `"{F4540CBAD6301389}$engineLayoutPath`"") -and $moduleGuids['F4540CBAD6301389'] -ceq $engineLayoutPath) 'the component must create the engine map layout by its identity'
Assert-References $component 'EBM_BriefingBoardComponent.c'
$update = Get-Body $component 'protected\s+void\s+UpdateEngineMap\s*\('
Assert ($update.Contains('EngineMapAllowed()') -and $update.Contains('s_EngineMapOwner != this') -and $update.Contains('ReleaseEngineMap();')) 'only one board per client may drive the native map'
Assert ($update.Contains('mapEntity.ZoomChange(pixelsPerMeter / widgetPixelPerUnit);') -and $update.Contains('mapEntity.PosChange(panX, panY);') -and $update.Contains('mapEntity.SetFrame(') -and $update.Contains('mapEntity.EnableVisualisation(true);') -and $update.Contains('mapEntity.EBM_MarkNativeDirty();')) 'the board must drive zoom, pan, frame and drawing and mark the native view dirty'
Assert ($update.Contains('float pixelsPerMeter = m_fScale * screenWidth / m_iRtWidth;') -and $update.Contains('(mapEntity.GetMapSizeY() - m_fCenterY + offset[2]) * pixelsPerMeter')) 'the game map must use the ToBoard transform (inverse of WorldToScreen)'
Assert ($update.Contains('ENGINE_MAP_RETRIES')) 'the size retry must be bounded'
$allowed = Get-Body $component 'protected\s+bool\s+EngineMapAllowed\s*\('
Assert ($allowed.Contains('mapEntity.IsOpen()')) 'the board must never drive the native map while a map of the player is open'
$create = Get-Body $component 'protected\s+void\s+CreateBoardWidgets\s*\('
$destroy = Get-Body $component 'protected\s+void\s+DestroyBoardWidgets\s*\('
Assert ($create.Contains('GetOnMapInit().Insert(EBM_OnLocalMapInit)') -and $create.Contains('GetOnMapClose().Insert(EBM_OnLocalMapClose)')) 'the board must yield to and return after the player map'
Assert ($destroy.Contains('GetOnMapInit().Remove(EBM_OnLocalMapInit)') -and $destroy.Contains('GetOnMapClose().Remove(EBM_OnLocalMapClose)') -and $destroy.Contains('ReleaseEngineMap();')) 'destroying the board must release the native map and its hooks'
$release = Get-Body $component 'protected\s+void\s+ReleaseEngineMap\s*\('
Assert ($release.Contains('m_wEngineMap.RemoveFromHierarchy();') -and $release.Contains('if (mapEntity && !mapEntity.IsOpen())')) 'releasing removes the board map widget and never switches off an open player map'
Assert ((Get-Body $component 'protected\s+void\s+EBM_OnLocalMapInit\s*\(').Contains('ReleaseEngineMap();')) 'an opening player map takes the native map at once'
$mapEntity = Read-Text (Module-Path 'Scripts/Game/EXPBM/EBM_MapEntity.c')
Assert ($mapEntity.Contains('modded class SCR_MapEntity')) 'EBM_MapEntity.c must mod SCR_MapEntity'
$boardConfig = Get-Body $mapEntity 'MapConfiguration\s+EBM_CreateBoardConfig\s*\('
foreach ($part in 'm_LayersConfig', 'm_MapPropsConfig', 'm_DescriptorVisibilityConfig', 'm_DescriptorDefaultsConfig') { Assert ($boardConfig.Contains("GetObject(`"$part`")")) "the board config must read $part from the map config" }
Assert ($boardConfig.Contains('SetupLayersAndProps(configObject, drawing);') -and $boardConfig -notmatch 'm_aModules|m_aUIComponents') 'only the drawing setup may be taken from the map config (no modules or UI components)'
Assert ($mapEntity -notmatch '\b(OpenMap|CloseMap|SetupMapConfig)\s*\(') 'the board must never open, close or set up the player map'
$open = Get-Body $mapEntity 'override\s+protected\s+void\s+OnMapOpen\s*\('
Assert ($open.Contains('ZoomChange(m_fZoomPPU / pixelPerUnit);') -and $open.Contains('PosChange(m_Workspace.DPIScale(m_iPanX), m_Workspace.DPIScale(m_iPanY));') -and $open.Contains('EBM_ApplyDescriptorTypes(config);')) "an opening map must get its own zoom, pan and icon mapping back"
Assert ($open.IndexOf('m_bEBM_NativeDirty = false;') -lt $open.IndexOf('super.OnMapOpen(config);') -and $open.Contains('super.OnMapOpen(config);')) 'the view is restored before vanilla OnMapOpen continues'

# Attribution: README, Workshop description, notices, credits and the packed comment script.
$readme = Read-Text (Join-Path $repo 'README.md')
$credit = 'Projector Screen model from Structures For GM byHeine by Heine.CRV (Workshop 628EDA2ABC937159, APL-SA)'
Assert ($readme.Contains($credit)) 'README must credit the projector model'
$asset = Read-Text (Join-Path $repo 'tools/workshop-asset.json') | ConvertFrom-Json
Assert ($asset.description.Contains($credit + '.')) 'the Workshop description must credit the projector model'
Assert ($asset.summary.Length -le 200) 'the Workshop summary must stay within 200 characters'
Assert ((Read-Text (Join-Path $repo 'tools/project.json') | ConvertFrom-Json).addon.runtimePaths -ccontains 'EBMArt') 'EBMArt must be a runtime path'
$notices = Read-Text (Join-Path $repo 'docs/licenses/advanced-briefing-map/THIRD_PARTY_NOTICES.md')
Assert ($notices.Contains('628EDA2ABC937159') -and $notices.Contains('Heine.CRV') -and $notices.Contains('APL-SA')) 'third-party notice must name source, author and licence'
$packed = Read-Text (Module-Path 'Scripts/Game/EXPBM/EBM_DistributionNotices.c')
Assert ($packed.TrimStart().StartsWith('/*') -and $packed.TrimEnd().EndsWith('*/') -and $packed.Contains('628EDA2ABC937159') -and $packed.Contains('Heine.CRV')) 'the packed attribution script must be comment-only and name the source'
Assert ((Read-Text (Module-Path 'Credits/EBM_ASSET_CREDITS.txt')).Contains('628EDA2ABC937159')) 'module Credits must name the source'

# Text resources: LF ASCII; Enforce gotchas in the module scripts.
foreach ($file in Get-ChildItem -LiteralPath $module -Recurse -File | Where-Object Extension -in '.c', '.et', '.layout', '.emat', '.conf', '.meta', '.txt') {
 $bytes = [IO.File]::ReadAllBytes($file.FullName)
 Assert (!($bytes -contains 13) -and !($bytes | Where-Object { $_ -gt 127 })) "module text must be LF ASCII: $($file.FullName)"
}
foreach ($script in Get-ChildItem -LiteralPath (Module-Path 'Scripts/Game/EXPBM') -File -Filter '*.c') {
 $text = Read-Text $script.FullName
 Assert (![regex]::IsMatch($text, '\b(int|float|bool|string|vector|auto|IEntity|Widget)\s+(owned|Sleep)\b')) "reserved Enforce name used as a variable in $($script.Name)"
 Assert (![regex]::IsMatch($text, 'Math\.RandomFloat\(\s*0\s*,\s*0\s*\)')) "Math.RandomFloat(0, 0) logs an engine error: $($script.Name)"
 Assert (![regex]::IsMatch($text, '(?m)^\s*(?:static\s+|protected\s+|override\s+)*(?:bool|int|float|vector|string|ResourceName)\s+\w+\s*\([^)]*\)\s*\{[^\r\n]*\bif\b[^\r\n]*\}\s*$')) "one-line non-void method with an if (No return statement): $($script.Name)"
 Assert (![regex]::IsMatch($text, '(?m)^\s*protected\s+(?:ref\s+)?(MapLayer|MapConfiguration|MapWidget|CanvasWidget|ImageWidget|RTTextureWidget)\s+\1\s*;')) "field named like a vanilla type in $($script.Name)"
}
'PASS: briefing projector closure, references and metadata; projector and wall-map screen turns cover their UV rectangles; canvas hand-off layout; game map renderer wiring; attribution. Native render, orientation and roads are not verified here.'
