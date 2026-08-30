Add-Type -AssemblyName System.Drawing

$imageDir = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\docs\images')).Path
$names = @(
    'main-menu',
    'manage-outfits',
    'material-swap',
    'mcm',
    'outfit-studio',
    'quick-outfit-switcher',
    'select-target'
)
$codec = [System.Drawing.Imaging.ImageCodecInfo]::GetImageEncoders() |
    Where-Object { $_.MimeType -eq 'image/jpeg' }

foreach ($name in $names) {
    $srcPath = Join-Path $imageDir ($name + '.png')
    $dstPath = Join-Path $imageDir ($name + '.jpg')
    if (-not (Test-Path -LiteralPath $srcPath -PathType Leaf)) {
        throw "Missing source image: $srcPath"
    }

    $src = [System.Drawing.Image]::FromFile($srcPath)
    try {
        $targetWidth = 1600
        $targetHeight = [int][Math]::Round($src.Height * ($targetWidth / [double]$src.Width))
        $bitmap = [System.Drawing.Bitmap]::new(
            $targetWidth,
            $targetHeight,
            [System.Drawing.Imaging.PixelFormat]::Format24bppRgb
        )
        try {
            $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
            try {
                $graphics.Clear([System.Drawing.Color]::Black)
                $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
                $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
                $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
                $graphics.DrawImage($src, 0, 0, $targetWidth, $targetHeight)
            }
            finally {
                $graphics.Dispose()
            }

            $parameters = [System.Drawing.Imaging.EncoderParameters]::new(1)
            try {
                $parameters.Param[0] = [System.Drawing.Imaging.EncoderParameter]::new(
                    [System.Drawing.Imaging.Encoder]::Quality,
                    [long]85
                )
                $bitmap.Save($dstPath, $codec, $parameters)
            }
            finally {
                $parameters.Dispose()
            }
        }
        finally {
            $bitmap.Dispose()
        }
    }
    finally {
        $src.Dispose()
    }
}

$results = foreach ($name in $names) {
    $srcPath = Join-Path $imageDir ($name + '.png')
    $dstPath = Join-Path $imageDir ($name + '.jpg')
    $image = [System.Drawing.Image]::FromFile($dstPath)
    try {
        [PSCustomObject]@{
            Name = $name
            Width = $image.Width
            Height = $image.Height
            OriginalBytes = (Get-Item -LiteralPath $srcPath).Length
            CompressedBytes = (Get-Item -LiteralPath $dstPath).Length
        }
    }
    finally {
        $image.Dispose()
    }
}

$results | Format-Table -AutoSize
if (($results | Where-Object { $_.CompressedBytes -ge $_.OriginalBytes }).Count -gt 0) {
    throw 'Compression did not reduce every image'
}

foreach ($name in $names) {
    Remove-Item -LiteralPath (Join-Path $imageDir ($name + '.png')) -Force
}
Write-Output 'PNG originals removed after verification.'
