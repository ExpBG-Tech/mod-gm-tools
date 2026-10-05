<#
.SYNOPSIS
Re-titles an approved EXPBG black/antique-gold card (1254x1254 PNG) with new
bottom-title text in the same position, colour, tracking and side rules.
Only System.Drawing is used; no downloads.

.EXAMPLE
pwsh -File tools/art/New-TitleCard.ps1 -Source Crowd_Module_Card.png -Title 'AMBIENT CROWD SOUND' -Out Ambient_Crowd_Sound_Card.png
#>
param(
	[Parameter(Mandatory)][string]$Source,
	[Parameter(Mandatory)][string]$Title,
	[Parameter(Mandatory)][string]$Out,
	[int]$BandTop = 972,
	[int]$BandBottom = 1072,
	[int]$PatchTop = 1110,
	[int]$CapHeight = 68,
	[int]$RuleY = 1024,
	[string]$FontName = 'Bahnschrift SemiBold Condensed',
	[double]$Tracking = 0.16
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$src = [Drawing.Bitmap]::FromFile((Resolve-Path -LiteralPath $Source).Path)
$bmp = New-Object Drawing.Bitmap($src.Width, $src.Height, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g = [Drawing.Graphics]::FromImage($bmp)
$g.DrawImage($src, 0, 0, $src.Width, $src.Height)

# Antique gold of the original title: average of bright warm pixels in the band.
$sumR = 0; $sumG = 0; $sumB = 0; $n = 0
for ($y = $BandTop; $y -lt $BandBottom; $y += 2) {
	for ($x = 0; $x -lt $src.Width; $x += 2) {
		$p = $src.GetPixel($x, $y)
		if ($p.R -gt 170 -and $p.G -gt 120 -and $p.B -lt 110) { $sumR += $p.R; $sumG += $p.G; $sumB += $p.B; $n++ }
	}
}
if ($n -eq 0) { throw 'No title pixels found in the band; adjust -BandTop/-BandBottom.' }
$gold = [Drawing.Color]::FromArgb(255, [int]($sumR / $n), [int]($sumG / $n), [int]($sumB / $n))

# Cover the old title with background texture taken from below the band.
$height = $BandBottom - $BandTop
$patch = New-Object Drawing.Rectangle(0, $PatchTop, $src.Width, $height)
$g.DrawImage($src, (New-Object Drawing.Rectangle(0, $BandTop, $src.Width, $height)), $patch, [Drawing.GraphicsUnit]::Pixel)

$g.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
$g.TextRenderingHint = [Drawing.Text.TextRenderingHint]::AntiAliasGridFit
$family = New-Object Drawing.FontFamily($FontName)
$style = [Drawing.FontStyle]::Regular
# Size the font from the measured cap height of "H" so it matches the approved title.
$probe = New-Object Drawing.Drawing2D.GraphicsPath
$probe.AddString('H', $family, [int]$style, 100, (New-Object Drawing.PointF(0, 0)), [Drawing.StringFormat]::GenericTypographic)
$capRatio = $probe.GetBounds().Height / 100
$probe.Dispose()
$fmt = [Drawing.StringFormat]::GenericTypographic
$chars = $Title.ToCharArray()
$ruleGap = 36; $ruleLength = 96; $margin = 56
$cap = $CapHeight; $track = $Tracking
while ($true) {
	$em = $cap / $capRatio
	$font = New-Object Drawing.Font($family, [single]$em, $style, [Drawing.GraphicsUnit]::Pixel)
	$widths = foreach ($c in $chars) { if ($c -eq ' ') { $em * 0.34 } else { $g.MeasureString([string]$c, $font, 10000, $fmt).Width } }
	$gap = $em * $track
	$total = ($widths | Measure-Object -Sum).Sum + $gap * ($chars.Count - 1)
	if ($total + 2 * ($ruleGap + $ruleLength) -le $src.Width - 2 * $margin) { break }
	if ($track -gt 0.06) { $track -= 0.01 } else { $cap -= 1 }
	$font.Dispose()
}
# Place glyph tops so the caps are centred on the rule line.
$probe = New-Object Drawing.Drawing2D.GraphicsPath
$probe.AddString('H', $family, [int]$style, [single]$em, (New-Object Drawing.PointF(0, 0)), $fmt)
$capTop = $probe.GetBounds().Top
$probe.Dispose()
$top = $RuleY - $cap / 2 - $capTop
$x = ($src.Width - $total) / 2
$brush = New-Object Drawing.SolidBrush($gold)
for ($i = 0; $i -lt $chars.Count; $i++) {
	$g.DrawString([string]$chars[$i], $font, $brush, [single]$x, [single]$top, $fmt)
	$x += $widths[$i] + $gap
}
$left = ($src.Width - $total) / 2
$right = $left + $total
$pen = New-Object Drawing.Pen($gold, 3)
$g.DrawLine($pen, [single]($left - $ruleGap - $ruleLength), [single]$RuleY, [single]($left - $ruleGap), [single]$RuleY)
$g.DrawLine($pen, [single]($right + $ruleGap), [single]$RuleY, [single]($right + $ruleGap + $ruleLength), [single]$RuleY)

$g.Dispose(); $src.Dispose()
$bmp.Save($Out, [Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
"wrote $Out (gold $($gold.R),$($gold.G),$($gold.B), cap $cap px, tracking $track)"
