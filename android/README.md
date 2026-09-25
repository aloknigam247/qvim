# qvim companion (Android)

A minimal Android client that connects to a Microsoft [Agent Host Protocol](https://microsoft.github.io/agent-host-protocol/)
(AHP) server over a WebSocket and mirrors its chat sessions: it subscribes to every session and chat
on the host, renders the transcript, and streams assistant replies live as they arrive.

## What it does

- Connects to an AHP server (`host:port`, or a full `ws://` / `wss://` URL) you type into the top bar.
- Runs the AHP handshake — `initialize` → `listSessions` → `subscribe` to each session, then to each
  of its chats — and folds the `chat` action stream through the canonical AHP reducers.
- Renders the transcript for every chat: the user prompt that started each turn, followed by the
  assistant reply assembled from the turn's markdown response parts (streamed deltas included).
- Sends what you type as a new turn (`chat/turnStarted` dispatched to the default chat).

The wire types, reducers, and JSON-RPC helpers come from the official Kotlin client
(`com.microsoft.agenthostprotocol:agent-host-protocol`); this app supplies only the OkHttp WebSocket
transport, the reactive handshake/subscription driver, and the transcript projection.

## Layout

```
android/
  settings.gradle.kts, build.gradle.kts, gradle.properties
  app/
    build.gradle.kts
    src/main/java/com/qvim/companion/
      model/UiMessage.kt              # one rendered transcript row
      net/  ConnectionState.kt        # Disconnected / Connecting / Connected
            AhpConnection.kt          # OkHttp WebSocket + JSON-RPC driver + AHP state mirror
      AhpTranscript.kt                # pure ChatState -> transcript projection (JVM-unit-testable)
      ChatViewModel.kt                # owns the connection, mirrors its flows
      ui/ChatScreen.kt                # Compose transcript + input + endpoint bar
      MainActivity.kt
    src/test/java/com/qvim/companion/AhpTranscriptTest.kt   # reducer fold + projection (pure JVM)
    src/debug/                        # debug-only cleartext network-security config
```

## Prerequisites (headless — no Android Studio)

- **JDK 17** — set `JAVA_HOME` to it.
- **Android SDK** (cmdline-tools + `platforms;android-34` + `build-tools;34.0.0` + `platform-tools`)
  — set `ANDROID_HOME`, or write `android/local.properties` with `sdk.dir=<path>`.
- **Gradle** — not needed globally; use the committed `./gradlew` wrapper.

Example (PowerShell, matching this repo's dev setup):

```ps1
$env:JAVA_HOME    = "$env:LOCALAPPDATA\Java\jdk-17"
$env:ANDROID_HOME = "$env:LOCALAPPDATA\Android\Sdk"
```

`local.properties` is git-ignored (it holds a machine-specific absolute path). Create it once:

```ps1
"sdk.dir=$($env:ANDROID_HOME -replace '\\','\\\\')" | Set-Content android\local.properties
```

## Build & test

From `android/`:

```ps1
.\gradlew.bat test           # JVM unit tests (reducer fold + transcript projection)
.\gradlew.bat assembleDebug  # -> app/build/outputs/apk/debug/app-debug.apk
```

The AHP client library is published for a newer Kotlin than this module's compiler, so
`app/build.gradle.kts` passes `-Xskip-metadata-version-check` to consume it. This is the standard
escape hatch for depending on a library built with a newer Kotlin toolchain.

## Run against an AHP host

Point the app at any AHP server reachable over the LAN (e.g. the VS Code built-in agent host, or an
open-source host such as `pi-ahp` / `ahpd` — see the
[protocol README](https://github.com/microsoft/agent-host-protocol)). Install and launch on a
USB-debugging device on the same network:

```ps1
$adb = "$env:ANDROID_HOME\platform-tools\adb.exe"
& $adb install -r app\build\outputs\apk\debug\app-debug.apk
& $adb shell am start -n com.qvim.companion/.MainActivity
```

In the app, set the endpoint to `ws://<HOST-LAN-IP>:<PORT>` and Connect. The transcript of every
session/chat on the host appears and updates live; typing a message and Send starts a new turn.

## Cleartext note

The app talks plaintext `ws://` for LAN use. Cleartext is enabled **only** in the debug manifest
(`src/debug`) via a network-security config; the release manifest has no such allowance. Use `wss://`
against a TLS-terminating host for encrypted transport.
