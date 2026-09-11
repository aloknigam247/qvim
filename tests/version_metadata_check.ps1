param(
    [Parameter(Mandatory)][string]$ExePath,
    [Parameter(Mandatory)][string]$ExpectedVersion
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $ExePath)) {
    throw "qvim.exe not found at '$ExePath'."
}

$vi = (Get-Item $ExePath).VersionInfo
$fileVersion = $vi.FileVersion
$productVersion = $vi.ProductVersion
$raw = $vi.FileVersionRaw.ToString()
$expectedRaw = "$ExpectedVersion.0"

$errors = @()
if ($fileVersion -ne $ExpectedVersion) {
    $errors += "FileVersion '$fileVersion' != PROJECT_VERSION '$ExpectedVersion'"
}
if ($productVersion -ne $ExpectedVersion) {
    $errors += "ProductVersion '$productVersion' != PROJECT_VERSION '$ExpectedVersion'"
}
if ($raw -ne $expectedRaw) {
    $errors += "FileVersionRaw '$raw' != expected '$expectedRaw'"
}

if ($errors.Count -gt 0) {
    throw ($errors -join "; ")
}

Write-Host "version metadata OK: FileVersion=$fileVersion ProductVersion=$productVersion FileVersionRaw=$raw"
