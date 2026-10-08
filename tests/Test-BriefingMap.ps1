#requires -Version 7.0
# Portable guard for Advanced Briefing Map (live test 2026-10-07: the map on the board
# was turned a quarter turn and showed no roads or buildings; the user asked for the
# Heine "Prop - Projector Screen" instead of the wall map). Live test of 0.1.10 the same
# day: the projector showed a plain white screen. The two render target hand-off (board in
# EBM_Canvas, shown through the image EBM_Screen) never got the canvas texture, so the
# white EBM_Screen image covered the screen's UV rectangle. Checks: the imported model
# closure and its metadata, prefab and model references, the projector and wall-map
# screen UV/turn settings, the single render target direct path (content over the UV
# rectangle, every element turned so the board reads north-up), the opt-in hand-off,
# the upright surface (a local vanilla quad with upright UVs in front of each screen, so
# the game's own map view with roads and buildings is drawn unturned; decoded vertex data
# below), the game map renderer wiring, the [EBM DIAG] trace, attribution and gotchas.
# No engine is launched; render, orientation and roads need a native check.
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
 '01F85A3B7D7C5EB0' = 'Assets/Structures/BuildingsParts/Doors/Door_Barracks_01/Glass_Door_Barracks_92x56.xob'
 '406BC52002B99A15' = 'Assets/Structures/BuildingsParts/Graffiti/Graffiti_01/Graffiti_FIA_V1.xob'
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
Assert ($component -match '\[Attribute\("0"[^\]]*\]\s+protected bool m_bCanvasHandoff;') 'the two render target hand-off (white screen in the 0.1.10 live test) must be opt-in'
Assert ($component -notmatch 'SetSize\(m_wRoot' -and $component -notmatch 'SetPos\(m_wRoot') 'the workspace-stretched layout root must not be sized (GUI error: Position/Size works only when min and max anchor is the same)'
$turn = Get-Body $component 'protected\s+int\s+ScreenTurn\s*\('
Assert ($turn.Contains('Math.AbsFloat(Math.AbsFloat(turn) - 90) < 1') -and $turn.Contains('return -1;') -and $turn.Contains('return 1;') -and $turn.Contains('return 0;')) 'the screen turn must be a quarter turn either way or none'
$create = Get-Body $component 'protected\s+void\s+CreateBoardWidgets\s*\('
Assert ($create.Contains('m_bHandoffActive = m_iScreenTurn != 0 && (m_bCanvasHandoff || EBM_Diagnostics.HandoffForced());')) 'the hand-off runs only when asked for and only on a turned screen'
Assert ($create.Contains('m_wCanvas.RemoveFromHierarchy();') -and $create.Contains('m_wScreen.RemoveFromHierarchy();') -and $create.Contains('m_wSurfaceRT.RemoveFromHierarchy();')) 'unused render targets and the hand-off image must leave the hierarchy'
Assert ($create.Contains('if (!m_bHandoffActive && !m_sSurfaceModel.IsEmpty() && !EBM_Diagnostics.DirectForced())') -and $create.Contains('m_bSurfaceActive = SpawnSurface();') -and $create -match '(?s)else if \(m_bSurfaceActive\)\s+surface = m_wSurfaceRT;') 'the upright surface is the default path; the screen itself is the fallback'
Assert ($create.IndexOf('BindRenderTarget("bind");') -lt $create.IndexOf('BindSurfaceTarget();')) 'the screen and the surface are both bound after layout'
Assert ($create.Contains('m_wContent = workspace.CreateWidgets(CONTENT_LAYOUT, surface);')) 'the board content is created into the render target (direct) or the canvas (hand-off)'
Assert ($create.IndexOf('LayoutScreen();') -gt 0 -and $create.IndexOf('LayoutScreen();') -lt $create.IndexOf('BindRenderTarget("bind");')) 'the render target is bound after the board is laid out'
$bind = Get-Body $component 'protected\s+void\s+BindRenderTarget\s*\('
Assert ($bind.Contains('IEntity owner = GetOwner();') -and $bind.Contains('m_wRT.SetRenderTarget(owner);') -and $bind.Contains('m_BoundObject = owner.GetVObject();')) 'RTTexture0 must be bound to the board entity and remember the mesh object'
$tick = Get-Body $component 'protected\s+void\s+ClientProximityTick\s*\('
Assert ($tick.Contains('m_iRebinds < MAX_REBINDS && owner.GetVObject() != m_BoundObject') -and $tick.Contains('BindRenderTarget("rebind");')) 'a changed mesh object gets the render target again (bounded)'
$layoutScreen = Get-Body $component 'protected\s+void\s+LayoutScreen\s*\('
Assert ($layoutScreen.Contains('m_iRtWidth = Math.Round(spanV);') -and $layoutScreen.Contains('m_iRtHeight = Math.Round(spanU);')) 'a quarter turn must swap the board axes'
Assert ($layoutScreen.Contains('FrameSlot.SetSize(m_wRT, size, size);')) 'the render target must be square (texel aspect of both screens)'
$handoffAt = $layoutScreen.IndexOf('if (m_bHandoffActive)'); $directAt = $layoutScreen.IndexOf('m_iDrawTurn = m_iScreenTurn;'); $imageAt = $layoutScreen.IndexOf('m_wScreen.SetImageTexture(0, m_wCanvas);')
Assert ($handoffAt -ge 0 -and $imageAt -gt $handoffAt -and $imageAt -lt $directAt) 'the canvas hand-off image is only used on the hand-off path'
Assert ($layoutScreen.Contains('m_iDrawTurn = 0;') -and $directAt -gt 0) 'the drawing is turned on the direct path only'
Assert ($layoutScreen.Contains('PlaceFrame(m_wContent, centerX - frameWidth * 0.5, centerY - frameHeight * 0.5, frameWidth, frameHeight);')) 'the direct path content frame must be centred on the UV rectangle'
$toBoard = Get-Body $component 'protected\s+void\s+ToBoard\s*\('
Assert ($toBoard.Contains('boardX = m_iRtWidth * 0.5 + (worldX - m_fCenterX) * m_fScale;') -and $toBoard.Contains('boardY = m_iRtHeight * 0.5 - (worldY - m_fCenterY) * m_fScale;')) 'ToBoard must keep east right and north up on the board'
$toSurface = Get-Body $component 'protected\s+void\s+ToSurface\s*\('
Assert ($toSurface -match '(?s)if \(m_iDrawTurn < 0\)\s+\{\s+surfaceX = boardY;\s+surfaceY = m_iRtWidth - boardX;' -and $toSurface -match '(?s)else if \(m_iDrawTurn > 0\)\s+\{\s+surfaceX = m_iRtHeight - boardY;\s+surfaceY = boardX;') 'ToSurface must turn the board a quarter against the mesh'
Assert ((Get-Body $component 'protected\s+float\s+DrawDegrees\s*\(').Contains('return m_iDrawTurn * 90.0;')) 'the widget turn goes with ToSurface (clockwise-positive degrees)'
# ToSurface (as in the script) must put the board's top corners where each mesh shows its
# screen's top corners (decoded UVs): projector top edge u=UMin, left v=VMax; wall map top u=UMax, left v=VMin.
function Get-Surface([double]$BoardX, [double]$BoardY, [int]$Turn, [double]$Width, [double]$Height) {
 if ($Turn -lt 0) { return @($BoardY, ($Width - $BoardX)) }
 if ($Turn -gt 0) { return @(($Height - $BoardY), $BoardX) }
 @($BoardX, $BoardY)
}
foreach ($case in @(
  @{ Name = 'projector'; Turn = -1; UMin = 0.0505; UMax = 0.6223; VMin = 0.0087; VMax = 0.9912; TopU = 0.0505; LeftV = 0.9912; RightV = 0.0087 },
  @{ Name = 'wall map'; Turn = 1; UMin = 0.0006; UMax = 0.7458; VMin = 0.0007; VMax = 0.9996; TopU = 0.7458; LeftV = 0.0007; RightV = 0.9996 })) {
 $size = 1024
 $width = [math]::Round(($case.VMax - $case.VMin) * $size); $height = [math]::Round(($case.UMax - $case.UMin) * $size)
 $frameLeft = ($case.UMin + $case.UMax) * 0.5 * $size - $height / 2; $frameTop = ($case.VMin + $case.VMax) * 0.5 * $size - $width / 2
 $topLeft = Get-Surface 0 0 $case.Turn $width $height; $topRight = Get-Surface $width 0 $case.Turn $width $height
 Assert ([math]::Abs($frameLeft + $topLeft[0] - $case.TopU * $size) -le 1.5 -and [math]::Abs($frameTop + $topLeft[1] - $case.LeftV * $size) -le 1.5) "$($case.Name): the board's top-left must land on the screen's top-left UV corner"
 Assert ([math]::Abs($frameLeft + $topRight[0] - $case.TopU * $size) -le 1.5 -and [math]::Abs($frameTop + $topRight[1] - $case.RightV * $size) -le 1.5) "$($case.Name): the board's top-right must land on the screen's top-right UV corner"
}
$redraw = Get-Body $component 'protected\s+void\s+Redraw\s*\('
Assert ($redraw.Contains('m_wMap.SetPivot(0.5, 0.5);') -and $redraw.Contains('m_wMap.SetRotation(DrawDegrees());') -and $redraw.Contains('ToSurface(left + width * 0.5, top + height * 0.5, centerX, centerY);')) 'the map image turns about its centre on the turned board'
Assert ($redraw.Contains('TracePath();')) 'each redraw reports a changed drawing path to the trace'
$lines = Get-Body $component 'protected\s+void\s+RedrawLines\s*\('
Assert ($lines.Contains('Math.Atan2(deltaY, deltaX) * Math.RAD2DEG + DrawDegrees()') -and $lines.Contains('ToSurface(startX, startY, surfaceX, surfaceY);')) 'drawn lines are placed and turned with the board'
Assert ((Get-Body $component 'protected\s+void\s+RedrawMarkers\s*\(').Contains('ToSurface(boardX, boardY, surfaceX, surfaceY);') -and (Get-Body $component 'protected\s+void\s+RebuildMarkers\s*\(').Contains('TurnLeaves(markerWidget, DrawDegrees());')) 'markers are placed and turned with the board'
$leaves = Get-Body $component 'protected\s+static\s+void\s+TurnLeaves\s*\('
Assert ($leaves.Contains('image.SetRotation(image.GetRotation() + degrees);') -and $leaves.Contains('text.SetRotation(text.GetRotation() + degrees);')) 'marker icons keep their own rotation plus the board turn'
$title = Get-Body $component 'protected\s+void\s+LayoutTitle\s*\('
Assert ($title.Contains('m_wTitle.SetPivot(0.5, 0.5);') -and $title.Contains('m_wTitle.SetRotation(DrawDegrees());')) 'the title turns with the board'

# Layouts: the render target shell (RTTexture0 with the hand-off image, the optional canvas)
# and the board content in its own layout, created into whichever surface draws it.
$layout = Read-Text (Module-Path 'UI/layouts/EXPBM/EBM_BoardRT.layout')
Assert-References $layout 'EBM_BoardRT.layout'
Assert ($layout -match '(?s)RTTextureWidgetClass "\{[0-9A-F]{16}\}" \{\s+Name "EBM_Canvas"' -and $layout -match '(?s)RTTextureWidgetClass "\{[0-9A-F]{16}\}" \{\s+Name "RTTexture0"' -and $layout -match '(?s)ImageWidgetClass "\{[0-9A-F]{16}\}" \{\s+Name "EBM_Screen"') 'EBM_Canvas and RTTexture0 must be render targets, EBM_Screen an image'
Assert ($layout.IndexOf('Name "RTTexture0"') -lt $layout.IndexOf('Name "EBM_Screen"') -and (Get-Body $layout 'Name "EBM_Canvas"') -notmatch 'WidgetClass') 'EBM_Screen sits in RTTexture0; the canvas stays empty until the hand-off fills it'
Assert ($layout.IndexOf('Name "RTTexture0"') -lt $layout.IndexOf('Name "EBM_Backdrop"') -and $layout.IndexOf('Name "EBM_Backdrop"') -lt $layout.IndexOf('Name "EBM_Screen"') -and $layout -match '(?s)Name "EBM_Backdrop".*?Anchor 0 0 1 1.*?Color 0\.86 0\.85 0\.8 1') 'the screen keeps a paper backdrop under everything (all it shows on the surface path)'
Assert ($layout -match '(?s)RTTextureWidgetClass "\{[0-9A-F]{16}\}" \{\s+Name "EBM_SurfaceRT"' -and (Get-Body $layout 'Name "EBM_SurfaceRT"') -notmatch 'WidgetClass') 'EBM_SurfaceRT is an empty render target filled with the board content on the surface path'
foreach ($name in 'EBM_Content', 'EBM_Paper', 'EBM_Map', 'EBM_Title') { Assert (!$layout.Contains("Name `"$name`"")) "$name belongs to the content layout" }
Assert ($layout -match '(?s)Name "EBM_Root".*?SizeX 1024\s+OffsetRight -1024\s+SizeY 1024\s+OffsetBottom -1024') 'the layout root must default to the square render target'
$contentLayoutPath = 'UI/layouts/EXPBM/EBM_BoardContent.layout'
$contentLayout = Read-Text (Module-Path $contentLayoutPath)
Assert-References $contentLayout 'EBM_BoardContent.layout'
Assert ($component.Contains("CONTENT_LAYOUT = `"{5DBA61BA423517E6}$contentLayoutPath`"") -and $moduleGuids['5DBA61BA423517E6'] -ceq $contentLayoutPath) 'the component must create the content layout by its identity'
Assert ($contentLayout -match '^FrameWidgetClass "\{[0-9A-F]{16}\}" \{\s+Name "EBM_Content"' -and $contentLayout -match '(?s)Name "EBM_Content".*?Clipping True') 'the content root is a clipping frame (turned elements are cut at the UV rectangle)'
$previous = 0
foreach ($name in 'EBM_Paper', 'EBM_Map', 'EBM_EngineMapSlot', 'EBM_Lines', 'EBM_Markers', 'EBM_Title') {
 $at = $contentLayout.IndexOf("Name `"$name`"")
 Assert ($at -gt $previous) "content layout order: paper, map image, game map, lines, markers, title ($name)"
 $previous = $at
}
$titleSlot = [regex]::Match($contentLayout, '(?s)Name "EBM_Title"\s+Slot FrameWidgetSlot "\{[0-9A-F]{16}\}" \{([^}]*)\}')
Assert ($titleSlot.Success -and $titleSlot.Groups[1].Value -match 'SizeX 974' -and $titleSlot.Groups[1].Value -notmatch 'SizeToContent') 'the title has a fixed box so it can turn about its centre'
foreach ($text in $layout, $contentLayout) { Assert ([regex]::Matches($text, '"\{([0-9A-F]{16})\}"').Count -eq @([regex]::Matches($text, '"\{([0-9A-F]{16})\}"') | ForEach-Object { $_.Groups[1].Value } | Select-Object -Unique).Count) 'layout widget IDs must be unique' }

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
Assert ($allowed.Contains('m_iDrawTurn != 0') -and (Get-Body $component 'protected\s+string\s+EngineMapBlocker\s*\(').Contains('return "turned-screen";')) 'a map widget cannot turn: no game map on a turned direct board (the map image stays)'

# Upright surface: the quads were decoded from the base game (XOB9 LOD 0, one submesh, four
# vertices, front by clockwise winding, the convention that reads the decoded tela1 and
# posters correctly). Corners: model x, y, u, v. Front +Z, so seen from the front right is -X.
$surfaces = @{
 projector = @{ Text = $projector; Model = '{01F85A3B7D7C5EB0}Assets/Structures/BuildingsParts/Doors/Door_Barracks_01/Glass_Door_Barracks_92x56.xob'; Size = @(0.92, 0.56); UV = @(0.28439, 0.74439, 0.35912, 0.63912)
  Corners = @(@(-0.46, -0.28, 0.74439, 0.63912), @(0.46, -0.28, 0.28439, 0.63912), @(0.46, 0.28, 0.28439, 0.35912), @(-0.46, 0.28, 0.74439, 0.35912))
  Screen = @{ Center = @(0.0059, 1.2312, -0.0118); Normal = @(1, 0, 0); Up = @(0, 1, 0); Size = @(3.9564, 2.3026) } }
 wall = @{ Text = $board; Model = '{406BC52002B99A15}Assets/Structures/BuildingsParts/Graffiti/Graffiti_01/Graffiti_FIA_V1.xob'; Size = @(0.958094, 0.70362); UV = @(0.67569, 0.99506, 0.04905, 0.28359)
  Corners = @(@(-0.479047, -0.35181, 0.99506, 0.28359), @(0.479047, -0.35181, 0.67569, 0.28359), @(0.479047, 0.35181, 0.67569, 0.04905), @(-0.479047, 0.35181, 0.99506, 0.04905))
  Screen = @{ Center = @(-0.0001, 0.4508, -0.0001); Normal = @(0, 0, 1); Up = @(0, 1, 0); Size = @(1.3858, 1.0396) } }
}
function Get-Cross([double[]]$A, [double[]]$B) { @(($A[1] * $B[2] - $A[2] * $B[1]), ($A[2] * $B[0] - $A[0] * $B[2]), ($A[0] * $B[1] - $A[1] * $B[0])) }
foreach ($name in $surfaces.Keys) {
 $case = $surfaces[$name]; $text = $case.Text
 Assert ($text.Contains("m_sSurfaceModel `"$($case.Model)`"")) "${name}: the upright surface model must be $($case.Model)"
 Assert ((Get-Number $text 'm_fSurfaceUMin') -eq $case.UV[0] -and (Get-Number $text 'm_fSurfaceUMax') -eq $case.UV[1] -and (Get-Number $text 'm_fSurfaceVMin') -eq $case.UV[2] -and (Get-Number $text 'm_fSurfaceVMax') -eq $case.UV[3]) "${name}: surface UV rectangle must match the decoded model"
 Assert ($text -match ('(?m)^\s*m_vSurfaceModelSize ' + [regex]::Escape("$($case.Size[0]) $($case.Size[1]) 0") + '\s*$')) "${name}: surface model size must match the decoded model"
 $screen = $case.Screen
 Assert ($text -match ('(?m)^\s*m_vScreenNormal ' + ($screen.Normal -join ' ') + '\s*$') -and $text -match ('(?m)^\s*m_vScreenUp ' + ($screen.Up -join ' ') + '\s*$')) "${name}: screen front and up must match the decoded screen"
 # Same arithmetic as ComputeSurfaceTransform (unrotated, unscaled owner): axes -right, up, normal.
 $right = Get-Cross $screen.Normal $screen.Up
 $scale = [math]::Min($screen.Size[0] / $case.Size[0], $screen.Size[1] / $case.Size[1])
 foreach ($corner in $case.Corners) {
  $world = 0..2 | ForEach-Object { $screen.Center[$_] - $right[$_] * $corner[0] * $scale + $screen.Up[$_] * $corner[1] * $scale }
  # Screen coordinates seen from the front: x to the right, y down, from the top-left corner of the drawn quad.
  $sx = (0..2 | ForEach-Object { ($world[$_] - $screen.Center[$_]) * $right[$_] } | Measure-Object -Sum).Sum + $case.Size[0] * $scale / 2
  $sy = $case.Size[1] * $scale / 2 - (0..2 | ForEach-Object { ($world[$_] - $screen.Center[$_]) * $screen.Up[$_] } | Measure-Object -Sum).Sum
  $u = $case.UV[0] + ($case.UV[1] - $case.UV[0]) * $sx / ($case.Size[0] * $scale)
  $v = $case.UV[2] + ($case.UV[3] - $case.UV[2]) * $sy / ($case.Size[1] * $scale)
  Assert ([math]::Abs($u - $corner[2]) -lt 0.001 -and [math]::Abs($v - $corner[3]) -lt 0.001) "${name}: the surface must show its UV rectangle upright (u right, v down) seen from the screen's front"
 }
 Assert ($case.Size[0] * $scale -le $screen.Size[0] + 0.0001 -and $case.Size[1] * $scale -le $screen.Size[1] + 0.0001 -and ($case.Size[0] * $scale * $case.Size[1] * $scale) / ($screen.Size[0] * $screen.Size[1]) -gt 0.94) "${name}: the surface must fit the screen and cover most of it"
 Assert ([math]::Abs((($case.UV[1] - $case.UV[0]) / $case.Size[0]) - (($case.UV[3] - $case.UV[2]) / $case.Size[1])) -lt 0.001) "${name}: surface texels must be square (one square render target)"
}
$transform = Get-Body $component 'protected\s+void\s+ComputeSurfaceTransform\s*\('
Assert ($transform.Contains('vector right = CrossProduct(normal, up);') -and $transform.Contains('transform[0] = -right;') -and $transform.Contains('transform[1] = up;') -and $transform.Contains('transform[2] = normal;')) 'the surface quad (front +Z, right -X) must face the screen front with its right on the screen right'
Assert ($transform.Contains('Math.Min(m_vScreenSize[0] / m_vSurfaceModelSize[0], m_vScreenSize[1] / m_vSurfaceModelSize[1])') -and $transform.Contains('* ownerScale')) 'the surface scales uniformly to fit the screen, with the board scale'
Assert ((Get-Body $component 'protected\s+static\s+vector\s+CrossProduct\s*\(').Contains('first[1] * second[2] - first[2] * second[1], first[2] * second[0] - first[0] * second[2], first[0] * second[1] - first[1] * second[0]')) 'CrossProduct must be the standard component formula used in the decoding'
$spawn = Get-Body $component 'protected\s+bool\s+SpawnSurface\s*\('
Assert ($spawn.Contains('GetGame().SpawnEntity(GenericEntity, owner.GetWorld(), params)') -and $spawn.Contains("remap += string.Format(`"`$remap '%1' '%2';`", materials[i], SURFACE_MATERIAL);") -and $spawn.Contains('m_Surface.SetObject(visual, remap);')) 'the surface is a local entity whose every material slot shows the render target'
Assert ($spawn.Contains('return false;') -and $spawn.Contains('Trace("surface-failed"')) 'a surface that cannot be loaded or spawned falls back to the screen itself'
Assert ($component.Contains('static const ResourceName SURFACE_MATERIAL = "{AA3CD43539C7EF56}Assets/EXPBM/EBM_BoardRT.emat";')) 'the surface uses the render target material'
Assert ((Get-Body $component 'protected\s+void\s+BindSurfaceTarget\s*\(').Contains('m_wSurfaceRT.SetRenderTarget(m_Surface);') -and (Get-Body $component 'protected\s+void\s+DeleteSurface\s*\(').Contains('m_wSurfaceRT.RemoveRenderTarget(m_Surface);') -and (Get-Body $component 'protected\s+void\s+DestroyBoardWidgets\s*\(').Contains('DeleteSurface();')) 'the surface render target is bound to the surface and released with it'
Assert ((Get-Body $component 'protected\s+void\s+ClientProximityTick\s*\(').Contains('PlaceSurface(false);')) 'the surface follows a moved, turned or scaled board'
$surfaceLayout = $layoutScreen.Substring($layoutScreen.IndexOf('if (m_bSurfaceActive)'))
Assert ($surfaceLayout.Contains('m_iDrawTurn = 0;') -and $surfaceLayout.Contains('FrameSlot.SetSize(m_wSurfaceRT, surfaceSize, surfaceSize);') -and $surfaceLayout.Contains('PlaceFrame(m_wContent, Math.Min(m_fSurfaceUMin, m_fSurfaceUMax) * surfaceSize, Math.Min(m_fSurfaceVMin, m_fSurfaceVMax) * surfaceSize, m_iRtWidth, m_iRtHeight);')) 'the surface path draws the board unturned over the surface UV rectangle'
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

# Opt-in trace that proves the drawing path on the next live test.
$diagnostics = Read-Text (Module-Path 'Scripts/Game/EXPBM/EBM_Diagnostics.c')
Assert ($diagnostics.Contains('System.GetCLIParam("ebmDiagnostics", value)') -and $diagnostics.Contains('System.GetCLIParam("ebmCanvasHandoff", value)') -and $diagnostics.Contains('[EBM DIAG]')) 'EBM_Diagnostics reads -ebmDiagnostics and -ebmCanvasHandoff and prefixes [EBM DIAG]'
Assert ($component -match '\[Attribute\("0"[^\]]*\]\s+protected bool m_bDebugTrace;') 'per-board trace attribute, off by default'
foreach ($action in 'create', 'raster', 'handoff', 'path', 'destroy', 'surface', 'surface-failed', 'bind-surface') { Assert ($component.Contains("Trace(`"$action`"")) "the trace must report $action" }
Assert ($diagnostics.Contains('System.GetCLIParam("ebmDirect", value)')) 'EBM_Diagnostics reads -ebmDirect (skip the surface for a comparison)'
$tracePath = Get-Body $component 'protected\s+void\s+TracePath\s*\('
Assert ($tracePath.Contains('path = "engine";') -and $tracePath.Contains('string path = "raster";') -and $tracePath.Contains('mode=%2')) 'the trace reports path=engine or path=raster and the drawing mode'
Assert ($bind.Contains('Trace(action, string.Format("entity=%1 object=%2 materials=%3"')) 'the trace must report the render target binding with the mesh object and its materials'
foreach ($script in Get-ChildItem -LiteralPath (Module-Path 'Scripts/Game/EXPBM') -File -Filter '*.c') {
 foreach ($format in [regex]::Matches((Read-Text $script.FullName), 'string\.Format\("([^"]*)"')) { Assert (![regex]::IsMatch($format.Groups[1].Value, '%1[0-9]')) "string.Format takes at most nine arguments: $($script.Name)" }
}

# Attribution: README, Workshop description, notices, credits and the packed comment script.
$readme = Read-Text (Join-Path $repo 'README.md')
$credit = 'Projector Screen model from Structures For GM byHeine by Heine.CRV (Workshop 628EDA2ABC937159, APL-SA)'
Assert ($readme.Contains($credit)) 'README must credit the projector model'
$asset = Read-Text (Join-Path $repo 'tools/workshop-asset.json') | ConvertFrom-Json
Assert (!$asset.description.Contains('Heine')) 'the Workshop listing names nobody (user decision 2026-10-08); the projector credit lives in README and Credits/EBM_ASSET_CREDITS.txt'
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
'PASS: briefing projector closure, references and metadata; upright surface default (decoded vanilla quads shown upright on both screens, unturned board and game map view); direct fallback (content over each screen UV rectangle, every element turned north-up); opt-in canvas hand-off; game map renderer wiring; [EBM DIAG] trace; attribution. Native render, orientation and roads are not verified here.'
