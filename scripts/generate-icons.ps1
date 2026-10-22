[CmdletBinding()]
param([switch]$RefreshSource)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$directory = Join-Path $root 'resources\icons'
if ($RefreshSource) {
    $revision = 'e4aa508e35286641ec8d6d8c0e37e3df654056a9'
    $url = "https://raw.githubusercontent.com/deepseek-ai/deepseek-harness/$revision/packages/client/ui-chat/src/client/chat/RunningWhaleTail.tsx"
    $component = Invoke-RestMethod -Uri $url
    $match = [regex]::Match($component, "const REST_PATH = '([^']+)'")
    if (-not $match.Success) { throw 'Upstream static REST_PATH was not found.' }
    $export = '<svg xmlns="http://www.w3.org/2000/svg" width="96" height="96" viewBox="0 0 16 16" fill="none" stroke="rgb(82, 161, 243)" stroke-width="1"><path d="' +
        $match.Groups[1].Value + '"/></svg>' + "`n"
    [IO.File]::WriteAllText((Join-Path $directory 'running-whale-tail.svg'),
        $export, [Text.UTF8Encoding]::new($false))
}
$svg = [xml][IO.File]::ReadAllText((Join-Path $directory 'running-whale-tail.svg'))
if ($svg.svg.viewBox -ne '0 0 16 16' -or $svg.svg.stroke -ne 'rgb(82, 161, 243)' -or
    $svg.svg.fill -ne 'none' -or $svg.svg.'stroke-width' -ne '1') {
    throw 'Unexpected icon source geometry or paint; review the renderer before regenerating.'
}
# The source uses only absolute Move/Cubic commands. Fail closed on any new
# command instead of silently dropping unsupported SVG geometry.
$pathData = [string]$svg.svg.path.d
if ($pathData -match '[A-BD-LN-Za-bd-ln-z]') { throw 'Unsupported SVG command.' }
$tokens = [regex]::Matches($pathData, '[MC]|[-+]?(?:\d*\.\d+|\d+)')
$path = [Drawing.Drawing2D.GraphicsPath]::new()
$index = 0
$point = [Drawing.PointF]::new(0, 0)
$culture = [Globalization.CultureInfo]::InvariantCulture
while ($index -lt $tokens.Count) {
    $command = $tokens[$index++].Value
    if ($command -eq 'M') {
        $point = [Drawing.PointF]::new(
            [single]::Parse($tokens[$index++].Value, $culture),
            [single]::Parse($tokens[$index++].Value, $culture))
    } elseif ($command -eq 'C') {
        $values = @()
        for ($n = 0; $n -lt 6; ++$n) {
            $values += [single]::Parse($tokens[$index++].Value, $culture)
        }
        $end = [Drawing.PointF]::new($values[4], $values[5])
        $path.AddBezier($point, [Drawing.PointF]::new($values[0], $values[1]),
            [Drawing.PointF]::new($values[2], $values[3]), $end)
        $point = $end
    } else { throw "Unexpected SVG token: $command" }
}
$sizes = @(16, 20, 24, 32, 40, 48, 64, 128, 256)
$images = @()
try {
    foreach ($size in $sizes) {
        $large = [Drawing.Bitmap]::new($size * 8, $size * 8,
            [Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $graphics = [Drawing.Graphics]::FromImage($large)
        $pen = [Drawing.Pen]::new([Drawing.Color]::FromArgb(255, 82, 161, 243), 1.0)
        $bitmap = [Drawing.Bitmap]::new($size, $size,
            [Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $downsample = [Drawing.Graphics]::FromImage($bitmap)
        $stream = [IO.MemoryStream]::new()
        try {
            $graphics.Clear([Drawing.Color]::Transparent)
            $graphics.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
            $graphics.ScaleTransform($size * 8 / 16.0, $size * 8 / 16.0)
            $graphics.DrawPath($pen, $path)
            $downsample.CompositingMode = [Drawing.Drawing2D.CompositingMode]::SourceCopy
            $downsample.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $downsample.PixelOffsetMode = [Drawing.Drawing2D.PixelOffsetMode]::HighQuality
            $downsample.DrawImage($large, 0, 0, $size, $size)
            # Bicubic filtering can ring in premultiplied RGB. Preserve its
            # alpha coverage while restoring the SVG's single stroke color.
            for ($y = 0; $y -lt $size; ++$y) {
                for ($x = 0; $x -lt $size; ++$x) {
                    $alpha = $bitmap.GetPixel($x, $y).A
                    if ($alpha -gt 0) {
                        $bitmap.SetPixel($x, $y, [Drawing.Color]::FromArgb($alpha, 82, 161, 243))
                    }
                }
            }
            $bitmap.Save($stream, [Drawing.Imaging.ImageFormat]::Png)
            $bytes = $stream.ToArray()
            [IO.File]::WriteAllBytes((Join-Path $directory "app-$size.png"), $bytes)
            $images += ,$bytes
        } finally {
            $stream.Dispose(); $downsample.Dispose(); $bitmap.Dispose()
            $pen.Dispose(); $graphics.Dispose(); $large.Dispose()
        }
    }
    $file = [IO.File]::Create((Join-Path $directory 'app.ico'))
    $writer = [IO.BinaryWriter]::new($file)
    try {
        $writer.Write([uint16]0); $writer.Write([uint16]1); $writer.Write([uint16]$sizes.Count)
        $offset = 6 + 16 * $sizes.Count
        for ($i = 0; $i -lt $sizes.Count; ++$i) {
            $dimension = if ($sizes[$i] -eq 256) { 0 } else { $sizes[$i] }
            $writer.Write([byte]$dimension); $writer.Write([byte]$dimension)
            $writer.Write([byte]0); $writer.Write([byte]0)
            $writer.Write([uint16]1); $writer.Write([uint16]32)
            $writer.Write([uint32]$images[$i].Length); $writer.Write([uint32]$offset)
            $offset += $images[$i].Length
        }
        foreach ($bytes in $images) { $writer.Write([byte[]]$bytes) }
    } finally { $writer.Dispose(); $file.Dispose() }
} finally { $path.Dispose() }
Write-Host 'PASS: static SVG exported to transparent PNGs and multi-size ICO'
