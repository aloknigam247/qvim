# Generates resources/icon.ico — multi-resolution classic DIB ICO.
# Frames: 16/32/48/64/128/256. Bold uppercase "Q" white on #0F4C5C teal.
# Classic DIB payloads (not PNG); Qt qico plugin in this vcpkg build does not
# decode PNG-payload ICOs.

[CmdletBinding()]
param(
    [string]$OutPath = (Join-Path $PSScriptRoot "..\resources\icon.ico")
)

Add-Type -AssemblyName System.Drawing

$sizes  = @(16, 32, 48, 64, 128, 256)
$bg     = [System.Drawing.Color]::FromArgb(255, 0x0F, 0x4C, 0x5C)
$fg     = [System.Drawing.Color]::White

function New-IconBitmap {
    param([int]$Size)

    $bmp = New-Object System.Drawing.Bitmap $Size, $Size, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g   = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode     = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic

    $bgBrush = New-Object System.Drawing.SolidBrush $bg
    $g.FillRectangle($bgBrush, 0, 0, $Size, $Size)
    $bgBrush.Dispose()

    $fontSize = [Math]::Max(6, [int]($Size * 0.66))
    $font = $null
    foreach ($family in @("Segoe UI", "Arial", "Tahoma")) {
        try {
            $font = New-Object System.Drawing.Font $family, $fontSize, ([System.Drawing.FontStyle]::Bold), ([System.Drawing.GraphicsUnit]::Pixel)
            break
        } catch { }
    }
    if (-not $font) {
        $font = New-Object System.Drawing.Font ([System.Drawing.FontFamily]::GenericSansSerif), $fontSize, ([System.Drawing.FontStyle]::Bold), ([System.Drawing.GraphicsUnit]::Pixel)
    }

    $fmt = New-Object System.Drawing.StringFormat
    $fmt.Alignment     = [System.Drawing.StringAlignment]::Center
    $fmt.LineAlignment = [System.Drawing.StringAlignment]::Center

    $rect = New-Object System.Drawing.RectangleF 0, 0, $Size, $Size
    $fgBrush = New-Object System.Drawing.SolidBrush $fg
    $g.DrawString("Q", $font, $fgBrush, $rect, $fmt)
    $fgBrush.Dispose()
    $font.Dispose()
    $fmt.Dispose()
    $g.Dispose()

    return $bmp
}

function Get-DibBytes {
    param([System.Drawing.Bitmap]$Bitmap)

    $w = $Bitmap.Width
    $h = $Bitmap.Height

    # BITMAPINFOHEADER: 40 bytes. biHeight is doubled (XOR + AND mask convention).
    $hdr = New-Object byte[] 40
    [BitConverter]::GetBytes([int]40).CopyTo($hdr, 0)         # biSize
    [BitConverter]::GetBytes([int]$w).CopyTo($hdr, 4)         # biWidth
    [BitConverter]::GetBytes([int]($h * 2)).CopyTo($hdr, 8)   # biHeight (XOR+AND)
    [BitConverter]::GetBytes([uint16]1).CopyTo($hdr, 12)      # biPlanes
    [BitConverter]::GetBytes([uint16]32).CopyTo($hdr, 14)     # biBitCount
    [BitConverter]::GetBytes([int]0).CopyTo($hdr, 16)         # biCompression = BI_RGB
    [BitConverter]::GetBytes([int]0).CopyTo($hdr, 20)         # biSizeImage
    [BitConverter]::GetBytes([int]0).CopyTo($hdr, 24)         # biXPelsPerMeter
    [BitConverter]::GetBytes([int]0).CopyTo($hdr, 28)         # biYPelsPerMeter
    [BitConverter]::GetBytes([int]0).CopyTo($hdr, 32)         # biClrUsed
    [BitConverter]::GetBytes([int]0).CopyTo($hdr, 36)         # biClrImportant

    # XOR (BGRA) pixel mask — rows bottom-up, no row padding for 32bpp.
    $xor = New-Object byte[] ($w * $h * 4)
    $rect = New-Object System.Drawing.Rectangle 0, 0, $w, $h
    $data = $Bitmap.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    try {
        $stride = $data.Stride
        $scan0  = $data.Scan0
        $rowLen = $w * 4
        $rowBuf = New-Object byte[] $rowLen
        for ($y = 0; $y -lt $h; $y++) {
            $srcRow = [IntPtr]::Add($scan0, $y * $stride)
            [System.Runtime.InteropServices.Marshal]::Copy($srcRow, $rowBuf, 0, $rowLen)
            $destY = $h - 1 - $y
            [Array]::Copy($rowBuf, 0, $xor, $destY * $rowLen, $rowLen)
        }
    } finally {
        $Bitmap.UnlockBits($data)
    }

    # AND mask: 1bpp, rows bottom-up, padded to 4-byte boundary. All zero
    # (opaque) since alpha lives in XOR. Some old Win32 paths still read it.
    $andRowBytes = [Math]::Ceiling($w / 8.0)
    $andRowBytes = ([int]$andRowBytes + 3) -band -bnot 3
    $and = New-Object byte[] ($andRowBytes * $h)

    $ms = New-Object System.IO.MemoryStream
    $ms.Write($hdr, 0, $hdr.Length)
    $ms.Write($xor, 0, $xor.Length)
    $ms.Write($and, 0, $and.Length)
    return $ms.ToArray()
}

$frames = @()
foreach ($s in $sizes) {
    $bmp = New-IconBitmap -Size $s
    $dib = Get-DibBytes -Bitmap $bmp
    $bmp.Dispose()
    $frames += [pscustomobject]@{ Size = $s; Data = $dib }
}

# ICO file layout:
#   ICONDIR    (6 bytes)
#   ICONDIRENTRY[n] (16 bytes each)
#   image payloads (DIB blobs)

$ms = New-Object System.IO.MemoryStream
$bw = New-Object System.IO.BinaryWriter $ms

# ICONDIR
$bw.Write([uint16]0)                      # reserved
$bw.Write([uint16]1)                      # type = ICO
$bw.Write([uint16]$frames.Count)

$entrySize = 16
$offset    = 6 + ($frames.Count * $entrySize)

foreach ($f in $frames) {
    $width  = if ($f.Size -ge 256) { 0 } else { $f.Size }
    $height = if ($f.Size -ge 256) { 0 } else { $f.Size }
    $bw.Write([byte]$width)
    $bw.Write([byte]$height)
    $bw.Write([byte]0)                    # color count (0 for >=8bpp)
    $bw.Write([byte]0)                    # reserved
    $bw.Write([uint16]1)                  # planes
    $bw.Write([uint16]32)                 # bit count
    $bw.Write([uint32]$f.Data.Length)     # bytes in res
    $bw.Write([uint32]$offset)            # image offset
    $offset += $f.Data.Length
}

foreach ($f in $frames) {
    $bw.Write($f.Data, 0, $f.Data.Length)
}

$bw.Flush()
$bytes = $ms.ToArray()
$bw.Dispose()
$ms.Dispose()

$resolved = [System.IO.Path]::GetFullPath($OutPath)
$dir = [System.IO.Path]::GetDirectoryName($resolved)
if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
[System.IO.File]::WriteAllBytes($resolved, $bytes)

Write-Output "Wrote $resolved ($($bytes.Length) bytes, $($frames.Count) frames)"
