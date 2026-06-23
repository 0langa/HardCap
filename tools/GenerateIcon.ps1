$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$output = Join-Path $PSScriptRoot '..\resources\hardcap.ico'
$sizes = @(256, 64, 48, 32, 16)
$images = [System.Collections.Generic.List[byte[]]]::new()

foreach ($size in $sizes) {
    $bitmap = [System.Drawing.Bitmap]::new($size, $size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.Clear([System.Drawing.Color]::Transparent)

    $pad = [float]($size * 0.06)
    $radius = [float]($size * 0.22)
    $path = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $diameter = $radius * 2
    $rect = [System.Drawing.RectangleF]::new($pad, $pad, $size - 2 * $pad, $size - 2 * $pad)
    $path.AddArc($rect.Left, $rect.Top, $diameter, $diameter, 180, 90)
    $path.AddArc($rect.Right - $diameter, $rect.Top, $diameter, $diameter, 270, 90)
    $path.AddArc($rect.Right - $diameter, $rect.Bottom - $diameter, $diameter, $diameter, 0, 90)
    $path.AddArc($rect.Left, $rect.Bottom - $diameter, $diameter, $diameter, 90, 90)
    $path.CloseFigure()

    $background = [System.Drawing.Drawing2D.LinearGradientBrush]::new(
        [System.Drawing.PointF]::new(0, 0), [System.Drawing.PointF]::new($size, $size),
        [System.Drawing.Color]::FromArgb(255, 13, 39, 70), [System.Drawing.Color]::FromArgb(255, 0, 95, 145))
    $graphics.FillPath($background, $path)

    $penWidth = [Math]::Max(2.0, $size * 0.075)
    $pen = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(255, 91, 214, 255), $penWidth)
    $pen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $pen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $arcRect = [System.Drawing.RectangleF]::new($size * 0.22, $size * 0.24, $size * 0.56, $size * 0.56)
    $graphics.DrawArc($pen, $arcRect, 205, 310)
    $graphics.DrawLine($pen, $size * 0.50, $size * 0.51, $size * 0.68, $size * 0.38)

    $stopBrush = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::White)
    $graphics.FillRectangle($stopBrush, $size * 0.35, $size * 0.69, $size * 0.30, $size * 0.08)

    $stream = [System.IO.MemoryStream]::new()
    $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
    $images.Add($stream.ToArray())

    $stream.Dispose(); $stopBrush.Dispose(); $pen.Dispose(); $background.Dispose()
    $path.Dispose(); $graphics.Dispose(); $bitmap.Dispose()
}

$file = [System.IO.File]::Open($output, [System.IO.FileMode]::Create)
$writer = [System.IO.BinaryWriter]::new($file)
$writer.Write([UInt16]0); $writer.Write([UInt16]1); $writer.Write([UInt16]$images.Count)
$offset = 6 + 16 * $images.Count
for ($i = 0; $i -lt $images.Count; $i++) {
    $size = $sizes[$i]
    $writer.Write([byte]$(if ($size -eq 256) { 0 } else { $size }))
    $writer.Write([byte]$(if ($size -eq 256) { 0 } else { $size }))
    $writer.Write([byte]0); $writer.Write([byte]0)
    $writer.Write([UInt16]1); $writer.Write([UInt16]32)
    $writer.Write([UInt32]$images[$i].Length); $writer.Write([UInt32]$offset)
    $offset += $images[$i].Length
}
foreach ($image in $images) { $writer.Write($image) }
$writer.Dispose(); $file.Dispose()

Write-Output $output
