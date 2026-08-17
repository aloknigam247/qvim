<#
.SYNOPSIS
    On-demand end-to-end smoke test for the qvim companion app against a physically
    connected Android device, driven entirely through adb.

.DESCRIPTION
    Automates the manual smoke: build + install the debug APK, start the Python echo
    server (the stand-in qvim mirror) behind `adb reverse`, launch the app, drive the
    real UI, and assert on the ACTUAL rendered view hierarchy via `uiautomator dump`
    (never hardcoded pixel coordinates).

    Cases:
      1. Positive  - connect succeeds, sending "hi" produces a "you: hi" bubble and a
                     streamed "assistant: Echo: hi" bubble.
      2. Negative  - with the echo server stopped, a fresh launch + Connect settles on
                     "Status: Disconnected" and never shows Connected or an echo.

    Exit code 0 = all assertions passed, 1 = at least one failed (or setup error).

    LOCAL-ONLY GATE: this requires a physical device on USB and therefore CANNOT run on
    GitHub-hosted CI runners. The exit code is for local scripting (e.g. a pre-push hook),
    not a PR gate. CI-gating output validation lives in the JVM unit tests (ProtocolTest,
    ChatReducerTest), which pin the protocol frames and the "Echo: hi" streaming assembly.

.PARAMETER SkipBuild
    Reuse the already-built app-debug.apk instead of running `gradlew assembleDebug`.

.PARAMETER DeviceSerial
    Target a specific adb device serial (required if more than one is attached).

.PARAMETER Port
    TCP port for the echo server + adb reverse tunnel. Default 8765.

.EXAMPLE
    pwsh -NoProfile -File android\scripts\e2e-device.ps1
    pwsh -NoProfile -File android\scripts\e2e-device.ps1 -SkipBuild
#>
[CmdletBinding()]
param(
    [switch] $SkipBuild,
    [string] $DeviceSerial,
    [int]    $Port = 8765
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

# --- Paths (env override, else the repo's documented headless-toolchain defaults) ---
$AndroidRoot = Split-Path -Parent $PSScriptRoot                 # android/
$JavaHome    = if ($env:JAVA_HOME)    { $env:JAVA_HOME }    else { "$env:LOCALAPPDATA\Java\jdk-17" }
$AndroidHome = if ($env:ANDROID_HOME) { $env:ANDROID_HOME } else { "$env:LOCALAPPDATA\Android\Sdk" }
$Adb         = Join-Path $AndroidHome "platform-tools\adb.exe"
$Apk         = Join-Path $AndroidRoot "app\build\outputs\apk\debug\app-debug.apk"
$EchoDir     = Join-Path $AndroidRoot "tools\echo-server"
$Package     = "com.qvim.companion"
$Activity    = "$Package/.MainActivity"

$RunId       = [guid]::NewGuid().ToString("N").Substring(0, 8)
$DeviceUiXml = "/sdcard/qvim-ui.xml"

if (-not (Test-Path $Adb)) { throw "adb not found at $Adb. Set ANDROID_HOME." }

# --- Assertion tracking ---
$script:Failures = New-Object System.Collections.Generic.List[string]
function Assert-True([bool] $Condition, [string] $Message) {
    if ($Condition) {
        Write-Host "  [PASS] $Message" -ForegroundColor Green
    } else {
        Write-Host "  [FAIL] $Message" -ForegroundColor Red
        $script:Failures.Add($Message)
    }
}

# --- adb helpers (bind to a single target so a second device can't race in) ---
$AdbTarget = @()
function Invoke-Adb { & $Adb @AdbTarget @args }

function Get-UiDump {
    # uiautomator dump is occasionally flaky ("null root node"); retry a few times.
    for ($i = 0; $i -lt 5; $i++) {
        try {
            Invoke-Adb shell uiautomator dump $DeviceUiXml *> $null
            $raw = (Invoke-Adb shell cat $DeviceUiXml) -join "`n"
            if ($raw -match '<hierarchy') { return [xml]$raw }
        } catch { }
        Start-Sleep -Milliseconds 400
    }
    throw "uiautomator dump failed after retries."
}

function Get-NodeCenter {
    # Returns @{X;Y;Text} for the first node whose text/content-desc matches, else $null.
    param([xml] $Doc, [string] $Text, [switch] $Contains)
    foreach ($n in $Doc.SelectNodes('//node')) {
        $t = "$($n.text)"; $d = "$($n.'content-desc')"
        $hit = if ($Contains) { $t.Contains($Text) -or $d.Contains($Text) }
               else           { $t -eq $Text -or $d -eq $Text }
        if ($hit -and $n.bounds -match '\[(\d+),(\d+)\]\[(\d+),(\d+)\]') {
            return @{
                X    = [int](([int]$Matches[1] + [int]$Matches[3]) / 2)
                Y    = [int](([int]$Matches[2] + [int]$Matches[4]) / 2)
                Text = $t
            }
        }
    }
    return $null
}

function Test-UiHasText {
    param([string] $Text, [switch] $Contains)
    $null -ne (Get-NodeCenter -Doc (Get-UiDump) -Text $Text -Contains:$Contains)
}

function Wait-ForText {
    param([string] $Text, [int] $TimeoutSec = 15, [switch] $Contains)
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        if (Test-UiHasText -Text $Text -Contains:$Contains) { return $true }
        Start-Sleep -Milliseconds 500
    }
    return $false
}

function Tap-Text {
    param([string] $Text, [switch] $Contains)
    $node = Get-NodeCenter -Doc (Get-UiDump) -Text $Text -Contains:$Contains
    if (-not $node) { throw "Could not locate UI element with text '$Text' to tap." }
    Invoke-Adb shell input tap $node.X $node.Y | Out-Null
}

function Test-AppForeground {
    (Invoke-Adb shell "dumpsys activity activities" | Select-String -SimpleMatch "topResumedActivity=").ToString().Contains($Package)
}

function Hide-ImeIfShown {
    # Only press BACK when the IME is actually up: with the IME shown, the first BACK
    # dismisses the keyboard (does NOT pop the activity). Pressing BACK with no IME
    # would exit the app to the launcher.
    $imeShown = Invoke-Adb shell dumpsys input_method | Select-String -SimpleMatch "mInputShown=true"
    if ($imeShown) {
        Invoke-Adb shell input keyevent 4 | Out-Null
        Start-Sleep -Milliseconds 500
    }
}

function Seed-Endpoint {
    param([string] $Url)
    $xml = "<?xml version='1.0' encoding='utf-8' standalone='yes' ?>`n<map>`n    <string name=`"endpoint`">$Url</string>`n</map>`n"
    $tmp = Join-Path $env:TEMP "qvim-prefs-$RunId.xml"
    [IO.File]::WriteAllText($tmp, $xml)
    $devTmp = "/data/local/tmp/qvim-prefs-$RunId.xml"
    Invoke-Adb push $tmp $devTmp | Out-Null
    Invoke-Adb shell "run-as $Package mkdir -p /data/data/$Package/shared_prefs" | Out-Null
    Invoke-Adb shell "run-as $Package cp $devTmp /data/data/$Package/shared_prefs/qvim.xml" | Out-Null
    Invoke-Adb shell rm -f $devTmp | Out-Null
    Remove-Item $tmp -ErrorAction SilentlyContinue
}

function Restart-App {
    param([string] $Endpoint)
    Invoke-Adb shell am force-stop $Package | Out-Null
    Seed-Endpoint -Url $Endpoint
    Invoke-Adb shell monkey -p $Package -c android.intent.category.LAUNCHER 1 *> $null
    for ($i = 0; $i -lt 20; $i++) {
        Start-Sleep -Milliseconds 500
        if (Test-AppForeground) { return }
    }
    throw "App $Package did not reach the foreground after launch."
}

# --- Cleanup state (restored in finally) ---
$echoProc          = $null
$origAutoRotate    = $null
$reverseAdded      = $false

try {
    # 0. Device selection.
    $devices = @((Invoke-Adb devices) | Select-String -Pattern "\tdevice$" | ForEach-Object { ($_ -split "\t")[0] })
    if ($DeviceSerial) {
        if ($devices -notcontains $DeviceSerial) { throw "Device '$DeviceSerial' not found among: $($devices -join ', ')" }
        $AdbTarget = @("-s", $DeviceSerial)
    } elseif ($devices.Count -eq 0) {
        throw "No authorized device. Connect a phone with USB debugging enabled and accept the RSA prompt (adb devices)."
    } elseif ($devices.Count -gt 1) {
        throw "Multiple devices attached ($($devices -join ', ')). Pass -DeviceSerial to choose one."
    } else {
        $AdbTarget = @("-s", $devices[0])
    }
    Write-Host "Device: $($AdbTarget[1])" -ForegroundColor Cyan

    # 1. Build (unless skipped).
    if (-not $SkipBuild) {
        Write-Host "Building debug APK..." -ForegroundColor Cyan
        $env:JAVA_HOME = $JavaHome
        Push-Location $AndroidRoot
        try { & .\gradlew.bat --no-daemon assembleDebug | Out-Host }
        finally { Pop-Location }
        if ($LASTEXITCODE -ne 0) { throw "gradlew assembleDebug failed." }
    }
    if (-not (Test-Path $Apk)) { throw "APK not found at $Apk (run without -SkipBuild first)." }

    # 2. Install.
    Write-Host "Installing APK..." -ForegroundColor Cyan
    Invoke-Adb install -r $Apk | Out-Host

    # 3. Echo-server preflight + reverse tunnel.
    & python -c "import websockets" 2>$null
    if ($LASTEXITCODE -ne 0) { throw "Python 'websockets' not installed. Run: python -m pip install -r `"$EchoDir\requirements.txt`"" }
    Invoke-Adb reverse "tcp:$Port" "tcp:$Port" | Out-Null
    $reverseAdded = $true

    # 4. Freeze rotation so layout can't shift between a dump and the tap it computes.
    $origAutoRotate = (Invoke-Adb shell settings get system accelerometer_rotation).Trim()
    Invoke-Adb shell settings put system accelerometer_rotation 0 | Out-Null

    $endpoint = "ws://127.0.0.1:$Port"

    # ---------------------------------------------------------------------------
    Write-Host "`n=== Case 1: positive (server up) ===" -ForegroundColor Yellow
    # ---------------------------------------------------------------------------
    Write-Host "Starting echo server..." -ForegroundColor Cyan
    $echoLog = Join-Path $env:TEMP "qvim-echo-$RunId.log"
    $echoProc = Start-Process -FilePath "python" -ArgumentList "echo_ws.py" `
        -WorkingDirectory $EchoDir -PassThru -NoNewWindow `
        -RedirectStandardOutput $echoLog -RedirectStandardError "$echoLog.err"
    $up = $false
    for ($i = 0; $i -lt 20; $i++) {
        if (Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue) { $up = $true; break }
        Start-Sleep -Milliseconds 300
    }
    if (-not $up) { throw "Echo server did not start listening on port $Port. See $echoLog" }

    Restart-App -Endpoint $endpoint
    Assert-True (Test-UiHasText -Text "ws://127.0.0.1:$Port" -Contains) "endpoint pre-seeded to $endpoint"

    Tap-Text -Text "Connect"
    Assert-True (Wait-ForText -Text "Status: Connected" -TimeoutSec 15) "WebSocket connects (Status: Connected)"

    Tap-Text -Text "Message"          # focus the input field
    Start-Sleep -Milliseconds 400
    Invoke-Adb shell input text "hi" | Out-Null
    Start-Sleep -Milliseconds 400
    Hide-ImeIfShown
    Assert-True (Test-AppForeground) "app still foreground after text entry"
    Tap-Text -Text "Send"

    Assert-True (Wait-ForText -Text "hi" -TimeoutSec 10)         "user message bubble shows 'hi'"
    Assert-True (Wait-ForText -Text "Echo: hi" -TimeoutSec 10)   "assistant echo assembled ('Echo: hi')"

    # ---------------------------------------------------------------------------
    Write-Host "`n=== Case 2: negative (server down) ===" -ForegroundColor Yellow
    # ---------------------------------------------------------------------------
    Write-Host "Stopping echo server..." -ForegroundColor Cyan
    if ($echoProc -and -not $echoProc.HasExited) { Stop-Process -Id $echoProc.Id -Force }
    $echoProc = $null
    for ($i = 0; $i -lt 20; $i++) {
        if (-not (Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue)) { break }
        Start-Sleep -Milliseconds 300
    }

    Restart-App -Endpoint $endpoint      # fresh process => empty transcript
    Assert-True (-not (Test-UiHasText -Text "Echo: hi" -Contains)) "fresh launch has no stale echo bubble"

    Tap-Text -Text "Connect"
    Start-Sleep -Seconds 6               # let the refused connection surface via onFailure
    $doc = Get-UiDump
    Assert-True ($null -ne (Get-NodeCenter -Doc $doc -Text "Status: Disconnected")) "settles on Status: Disconnected with no server"
    Assert-True ($null -eq (Get-NodeCenter -Doc $doc -Text "Status: Connected"))    "never reports Connected with no server"
    Assert-True ($null -eq (Get-NodeCenter -Doc $doc -Text "Echo: hi" -Contains))   "no echo bubble appears with no server"
}
finally {
    Write-Host "`nCleaning up..." -ForegroundColor Cyan
    if ($echoProc -and -not $echoProc.HasExited) { Stop-Process -Id $echoProc.Id -Force -ErrorAction SilentlyContinue }
    if ($AdbTarget.Count -gt 0) {
        if ($reverseAdded) { Invoke-Adb reverse --remove "tcp:$Port" 2>$null | Out-Null }
        Invoke-Adb shell rm -f $DeviceUiXml 2>$null | Out-Null
        if ($origAutoRotate -match '^\d+$') {
            Invoke-Adb shell settings put system accelerometer_rotation $origAutoRotate 2>$null | Out-Null
        }
    }
    Get-ChildItem (Join-Path $env:TEMP "qvim-echo-$RunId.log*") -ErrorAction SilentlyContinue | Remove-Item -ErrorAction SilentlyContinue
}

# --- Verdict ---
Write-Host ""
if ($script:Failures.Count -eq 0) {
    Write-Host "E2E PASSED - all assertions green." -ForegroundColor Green
    exit 0
} else {
    Write-Host "E2E FAILED - $($script:Failures.Count) assertion(s):" -ForegroundColor Red
    $script:Failures | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
    exit 1
}
