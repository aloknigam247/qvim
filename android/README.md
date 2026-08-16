# qvim companion (Android)

A minimal Android client that mirrors a qvim chat session over a plaintext WebSocket. This is the
skeleton slice (issue #48) of the #29 epic: connect, see the transcript, send input, watch the echo
reply stream back. No discovery, auth, encryption, or resume yet — those are later slices (#50–#54).

## What it does

- Connects to a session mirror endpoint (`ws://host:port`) you type into the top bar.
- Renders the chat transcript: atomic `user` messages and streamed `assistant` replies.
- Sends what you type as an `input` frame; the server echoes `Echo: <text>` back in chunks.

The wire contract lives in [`docs/protocol/session-mirror.md`](../docs/protocol/session-mirror.md).

## Layout

```
android/
  settings.gradle.kts, build.gradle.kts, gradle.properties
  app/
    build.gradle.kts
    src/main/java/com/qvim/companion/
      model/        Protocol.kt, UiMessage.kt      # wire types + decode/encode
      net/          SessionClient.kt, ConnectionFactory.kt  # OkHttp WebSocket
      ChatReducer.kt          # pure frame -> transcript fold (JVM-unit-testable)
      ChatViewModel.kt        # single-collector wiring, immutable StateFlow
      ui/ChatScreen.kt        # Compose transcript + input + endpoint bar
      MainActivity.kt
    src/test/java/com/qvim/companion/   # ProtocolTest, ChatReducerTest (pure JVM)
    src/debug/                # debug-only cleartext network-security config
  scripts/e2e-device.ps1      # on-demand adb-driven device E2E suite
  tools/echo-server/          # Python dev stand-in for the qvim mirror (#49)
```

## Prerequisites (headless — no Android Studio)

- **JDK 17** — set `JAVA_HOME` to it.
- **Android SDK** (cmdline-tools + `platforms;android-34` + `build-tools;34.0.0` + `platform-tools`)
  — set `ANDROID_HOME`, or write `android/local.properties` with `sdk.dir=<path>`.
- **Gradle** — not needed globally; use the committed `./gradlew` wrapper.

Example (PowerShell, matching this repo's dev setup):

```pwsh
$env:JAVA_HOME    = "$env:LOCALAPPDATA\Java\jdk-17"
$env:ANDROID_HOME = "$env:LOCALAPPDATA\Android\Sdk"
```

`local.properties` is git-ignored (it holds a machine-specific absolute path). Create it once:

```pwsh
"sdk.dir=$($env:ANDROID_HOME -replace '\\','\\\\')" | Set-Content android\local.properties
```

## Build & test

From `android/`:

```pwsh
.\gradlew.bat test           # JVM unit tests (Protocol + reducer)
.\gradlew.bat assembleDebug  # -> app/build/outputs/apk/debug/app-debug.apk
```

## Run against the dev echo server

The real session mirror is issue #49; until then, use the Python stand-in. On the PC:

```pwsh
cd android\tools\echo-server
pip install -r requirements.txt
python echo_ws.py            # binds 0.0.0.0:8765
```

Install and launch on a USB-debugging device on the same LAN:

```pwsh
$adb = "$env:ANDROID_HOME\platform-tools\adb.exe"
& $adb install -r app\build\outputs\apk\debug\app-debug.apk
& $adb shell am start -n com.qvim.companion/.MainActivity
```

In the app, set the endpoint to `ws://<PC-LAN-IP>:8765`, type a message, and Send. You should see your
message as a `user` bubble followed by an `assistant` `Echo: <text>` reply. With the server stopped,
sending should surface a disconnected state (negative check).

Drive it headlessly with adb if you like:

```pwsh
& $adb shell input text "hi"
& $adb exec-out screencap -p > shot.png
```

## On-demand device E2E suite

`scripts/e2e-device.ps1` automates the full smoke against a **physically connected device**, driven
entirely through adb. It builds + installs the APK, starts the Python echo server behind
`adb reverse`, launches the app, drives the real UI, and asserts on the actual rendered view
hierarchy via `uiautomator dump` (elements are located by text — never hardcoded pixel coordinates).

> **Local-only gate.** This needs a physical device on USB, so it **cannot run on GitHub-hosted CI**.
> The exit code (0 pass / 1 fail) is for local scripting, not a PR gate. CI-gating output validation
> lives in the JVM unit tests (`ProtocolTest`, `ChatReducerTest`), which pin the protocol frames and
> the `Echo: hi` streaming assembly. Treat this script as a manual smoke, not a proof.

```pwsh
# full run (builds the APK first)
pwsh -NoProfile -File android\scripts\e2e-device.ps1

# reuse an already-built APK
pwsh -NoProfile -File android\scripts\e2e-device.ps1 -SkipBuild
```

It runs two cases and exits non-zero if any assertion fails:

- **Positive** — Connect reaches `Status: Connected`; sending `hi` yields a `you: hi` bubble and a
  streamed `assistant: Echo: hi` bubble.
- **Negative** — with the echo server stopped, a fresh launch + Connect settles on
  `Status: Disconnected` and never shows Connected or an echo.

Prerequisites: an authorized device (`adb devices` shows `device`), the JDK/SDK env vars above, and
the echo-server deps (`pip install -r tools\echo-server\requirements.txt`). The script freezes screen
rotation for the duration and restores it on exit. Pass `-DeviceSerial` when more than one device is
attached, or `-Port` / `-PythonExe` to override defaults.

## Cleartext note

The app talks plaintext `ws://` on purpose for this slice. Cleartext is enabled **only** in the debug
manifest (`src/debug`) via a network-security config; the release manifest has no such allowance.
Encryption (`wss://`) is issue #54.
