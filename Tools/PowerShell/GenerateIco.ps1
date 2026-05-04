param(
    [Parameter(Mandatory = $true)]
    [string]$InputPath,

    [Parameter(Mandatory = $true)]
    [string]$OutputPath
)

$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.Drawing

$resolvedInputPath = [System.IO.Path]::GetFullPath($InputPath)
$resolvedOutputPath = [System.IO.Path]::GetFullPath($OutputPath)
$outputDirectory = [System.IO.Path]::GetDirectoryName($resolvedOutputPath)

if (-not [System.IO.File]::Exists($resolvedInputPath)) {
    throw "Input image was not found: $resolvedInputPath"
}

if (-not [string]::IsNullOrEmpty($outputDirectory)) {
    [System.IO.Directory]::CreateDirectory($outputDirectory) | Out-Null
}

$sizes = @(16, 32, 48, 256)
$source = [System.Drawing.Image]::FromFile($resolvedInputPath)

try {
    $images = @()

    foreach ($size in $sizes) {
        $bitmap = New-Object System.Drawing.Bitmap $size, $size

        try {
            $graphics = [System.Drawing.Graphics]::FromImage($bitmap)

            try {
                $graphics.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
                $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
                $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
                $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
                $graphics.Clear([System.Drawing.Color]::Transparent)
                $graphics.DrawImage($source, 0, 0, $size, $size)
            }
            finally {
                $graphics.Dispose()
            }

            $memory = New-Object System.IO.MemoryStream
            $bitmap.Save($memory, [System.Drawing.Imaging.ImageFormat]::Png)
            $images += [pscustomobject]@{ Size = $size; Data = $memory.ToArray() }
            $memory.Dispose()
        }
        finally {
            $bitmap.Dispose()
        }
    }

    $stream = [System.IO.File]::Create($resolvedOutputPath)
    $writer = New-Object System.IO.BinaryWriter $stream

    try {
        $writer.Write([uint16]0)
        $writer.Write([uint16]1)
        $writer.Write([uint16]$images.Count)

        $offset = 6 + (16 * $images.Count)
        foreach ($image in $images) {
            if ($image.Size -ge 256) {
                $entrySize = 0
            }
            else {
                $entrySize = $image.Size
            }

            $writer.Write([byte]$entrySize)
            $writer.Write([byte]$entrySize)
            $writer.Write([byte]0)
            $writer.Write([byte]0)
            $writer.Write([uint16]1)
            $writer.Write([uint16]32)
            $writer.Write([uint32]$image.Data.Length)
            $writer.Write([uint32]$offset)
            $offset += $image.Data.Length
        }

        foreach ($image in $images) {
            $writer.Write($image.Data)
        }
    }
    finally {
        $writer.Dispose()
        $stream.Dispose()
    }
}
finally {
    $source.Dispose()
}
