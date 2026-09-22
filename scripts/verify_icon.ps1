param([string]$Path = (Join-Path $PSScriptRoot "..\resources\icon.ico"))
$bytes = [System.IO.File]::ReadAllBytes($Path)
Write-Output ("Size: {0}" -f $bytes.Length)
$type = [BitConverter]::ToUInt16($bytes, 2)
$count = [BitConverter]::ToUInt16($bytes, 4)
Write-Output ("Type: {0} (1=ICO)" -f $type)
Write-Output ("Frames: {0}" -f $count)
for ($i = 0; $i -lt $count; $i++) {
    $base = 6 + ($i * 16)
    $w = $bytes[$base]; if ($w -eq 0) { $w = 256 }
    $h = $bytes[$base + 1]; if ($h -eq 0) { $h = 256 }
    $bpp = [BitConverter]::ToUInt16($bytes, $base + 6)
    $sz  = [BitConverter]::ToUInt32($bytes, $base + 8)
    $off = [BitConverter]::ToUInt32($bytes, $base + 12)
    $first = $bytes[$off]
    $isPng = ($bytes[$off] -eq 0x89 -and $bytes[$off + 1] -eq 0x50)
    $isDib = ($first -eq 40)
    Write-Output ("Frame {0}: {1}x{2} bpp={3} size={4} off={5} png={6} dib={7}" -f $i, $w, $h, $bpp, $sz, $off, $isPng, $isDib)
}
