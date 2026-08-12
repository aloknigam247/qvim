<#
.SYNOPSIS
    Deploy-integrity check: assert the trimmed Qt deploy is self-contained and covers every
    QML module the app imports. Deterministic — reads binaries and source, launches nothing.

.DESCRIPTION
    The post-build step in the top-level CMakeLists.txt deploys an EXPLICIT, hand-picked list
    of Qt DLLs + QML modules instead of a blanket Qt6*.dll glob, to keep the cold-start
    footprint small. The risk: a Qt module or QML import added in the future is left out of
    that list and every other test still passes (they load Qt from the full vcpkg tree on
    PATH), so only a real user launch breaks.

    This test guards the list WITHOUT a timing-sensitive launch, using two independent checks
    whose "expected set" is DERIVED from the actual binaries/source — never a hardcoded list
    that would drift in lock-step with the deploy and catch nothing:

      (A) DLL self-containment (closure). For qvim.exe and every deployed plugin/QML-plugin
          DLL, read its PE import table and require every Qt6*.dll it imports to be present at
          the deploy root. If a deployed binary needs a Qt DLL that was not deployed, the app
          would fail to load it at runtime -> fail here instead. Catches missing transitive
          Qt DLLs (e.g. Qt6Qml pulling Qt6QmlMeta) and a new linked Qt module.

      (B) QML module coverage. Scan qml/*.qml for `import Qt...` statements and require each
          imported module's directory (with its qmldir and the backing plugin DLL named in
          that qmldir) to be present in the deploy. Catches a new QML import whose module was
          not added to the deploy — the case a PE-import check alone cannot see, because QML
          modules are loaded by the engine by path, not via the import table.

    System/CRT DLLs (KERNEL32, MSVCP140, api-ms-win-*, dwmapi, ...) and non-Qt vcpkg deps are
    out of scope: they are supplied by the OS / VC runtime / VCPKG_APPLOCAL_DEPS, not by the
    hand-picked Qt list this test guards.

    WHEN THIS TEST FAILS: you do NOT edit this test. Add the missing item to the deploy list
    in the top-level CMakeLists.txt POST_BUILD block —
      - a missing Qt6*.dll        -> add it to the QVIM_QT_RUNTIME_DLLS list;
      - a missing QML module dir  -> add a copy for that module directory alongside the
                                     existing QtQuick / QtQml / QtTest copies.
    The test derives what is required from the binaries + qml source, so it needs no update
    when dependencies change; only the deploy list does.

.PARAMETER DeployDir
    The directory containing the deployed qvim.exe and its Qt payload.

.PARAMETER QmlSourceDir
    The qml/ source directory whose `import` statements define the required QML modules.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$DeployDir,
    [Parameter(Mandatory)][string]$QmlSourceDir
)

$ErrorActionPreference = "Stop"
$failures = [System.Collections.Generic.List[string]]::new()
function Fail($msg) { $script:failures.Add($msg) }

if (-not (Test-Path -LiteralPath $DeployDir))    { Write-Error "deploy dir not found: $DeployDir"; exit 1 }
if (-not (Test-Path -LiteralPath $QmlSourceDir)) { Write-Error "qml source dir not found: $QmlSourceDir"; exit 1 }
$DeployDir = (Resolve-Path -LiteralPath $DeployDir).Path
$exe = Join-Path $DeployDir "qvim.exe"
if (-not (Test-Path -LiteralPath $exe)) { Write-Error "qvim.exe not found in deploy dir: $exe"; exit 1 }

# --- Locate dumpbin (reads PE import tables) ---------------------------------------------
$dumpbin = $null
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vswhere) {
    $vsRoot = & $vswhere -latest -property installationPath 2>$null
    if ($vsRoot) {
        $dumpbin = Get-ChildItem "$vsRoot\VC\Tools\MSVC\*\bin\Hostx64\x64\dumpbin.exe" -EA SilentlyContinue |
                   Select-Object -First 1 -ExpandProperty FullName
    }
}
if (-not $dumpbin) {
    $dumpbin = Get-ChildItem "C:\Program Files*\Microsoft Visual Studio\*\*\VC\Tools\MSVC\*\bin\Hostx64\x64\dumpbin.exe" `
                   -EA SilentlyContinue | Select-Object -First 1 -ExpandProperty FullName
}
if (-not $dumpbin) { Write-Error "dumpbin.exe not found (needed to read PE imports)."; exit 1 }

function Get-DllImports([string]$path) {
    # Parse `dumpbin /dependents` output for lines that are a bare "<name>.dll".
    $out = & $dumpbin /dependents $path 2>$null
    $out | ForEach-Object {
        if ($_ -match '^\s*([A-Za-z0-9_\-\.]+\.dll)\s*$') { $matches[1] }
    }
}

# =========================================================================================
# (A) DLL self-containment: every Qt6*.dll imported by any deployed binary must be deployed.
# =========================================================================================
$deployedDlls = @{}
Get-ChildItem $DeployDir -Recurse -Filter *.dll | ForEach-Object { $deployedDlls[$_.Name] = $true }

$peFiles = @($exe) + (Get-ChildItem $DeployDir -Recurse -Filter *.dll | ForEach-Object { $_.FullName })
$checkedRootQt = [System.Collections.Generic.HashSet[string]]::new()
foreach ($pe in $peFiles) {
    foreach ($dep in (Get-DllImports $pe)) {
        if ($dep -match '^(?i)Qt6.*\.dll$') {
            [void]$checkedRootQt.Add($dep)
            # A required Qt DLL must be present at the deploy ROOT (that is where the loader
            # finds sibling DLLs of the exe / of the plugin DLLs).
            if (-not (Test-Path -LiteralPath (Join-Path $DeployDir $dep))) {
                Fail ("DLL closure: '$([IO.Path]::GetFileName($pe))' imports '$dep' but it is NOT " +
                      "deployed. FIX: add '$($dep -replace '\.dll$','')' to QVIM_QT_RUNTIME_DLLS in CMakeLists.txt.")
            }
        }
    }
}
Write-Host "(A) DLL closure: checked $($checkedRootQt.Count) distinct Qt6 imports across $($peFiles.Count) deployed binaries."

# =========================================================================================
# (B) QML module coverage: every `import Qt...` in qml/ must have its module dir deployed.
# =========================================================================================
$imports = Select-String -Path (Join-Path $QmlSourceDir '*.qml') -Pattern '^\s*import\s+(Qt[\w.]*)' |
           ForEach-Object { $_.Matches[0].Groups[1].Value } | Sort-Object -Unique
Write-Host "(B) QML imports found in qml/: $($imports -join ', ')"
foreach ($mod in $imports) {
    $relDir = $mod -replace '\.', '\'
    $modDir = Join-Path $DeployDir $relDir
    $qmldir = Join-Path $modDir 'qmldir'
    if (-not (Test-Path -LiteralPath $qmldir)) {
        Fail ("QML coverage: import '$mod' has no deployed module dir (expected '$relDir\qmldir'). " +
              "FIX: add a copy for the '$relDir' module to the QML-modules section of the POST_BUILD block in CMakeLists.txt.")
        continue
    }
    # If the qmldir names a backing plugin, its DLL must be deployed alongside.
    $pluginLine = Get-Content $qmldir | Where-Object { $_ -match '^\s*plugin\s+(\S+)' } | Select-Object -First 1
    if ($pluginLine -and $pluginLine -match '^\s*plugin\s+(\S+)') {
        $pluginDll = Join-Path $modDir ("{0}.dll" -f $matches[1])
        if (-not (Test-Path -LiteralPath $pluginDll)) {
            Fail "QML coverage: module '$mod' declares plugin '$($matches[1])' but '$($matches[1]).dll' is not deployed."
        }
    }
}

# --- Report ------------------------------------------------------------------------------
if ($failures.Count -gt 0) {
    Write-Host ""
    Write-Host "DEPLOY-INTEGRITY FAILURES:" -ForegroundColor Red
    $failures | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
    Write-Error "Deploy integrity check failed with $($failures.Count) problem(s). The trimmed Qt deploy list in the top-level CMakeLists.txt POST_BUILD block is missing something the app needs — see each FIX hint above. Do not edit this test."
    exit 1
}
Write-Host "OK: trimmed deploy is self-contained and covers every QML module the app imports."
exit 0
