# qvim companion (Android)

A minimal Android client that connects to Microsoft [Agent Host Protocol](https://microsoft.github.io/agent-host-protocol/)
(AHP) servers over a WebSocket and mirrors their chat sessions: it subscribes to every session and
chat on each host, renders the transcript, and streams assistant replies live as they arrive.

It points at a **cptower** multiplexer (`tools/cptower/`) rather than a single AHP host: cptower
discovers every live Copilot AHP host on a machine and exposes them behind one port, and the app
merges all of their sessions into one picker.

## What it does

- Connects to a cptower endpoint (`host:port`, or a full `ws://` / `wss://` URL) you type into the
  top bar.
- Fetches cptower's `GET /hosts` catalog and opens one connection per advertised host at
  `/ws/<port>`. If the endpoint is not a cptower instance (no `/hosts`), it falls back to a single
  direct connection to the endpoint itself.
- Runs the AHP handshake per host — `initialize` → `listSessions` → `subscribe` to each session, then
  to each of its chats — and folds the `chat` action stream through the canonical AHP reducers.
- Merges every host's sessions into one picker, each entry tagged with its host label; selecting one
  scopes the transcript to that host's session.
- Renders the transcript for the selected chat: the user prompt that started each turn, followed by
  the assistant reply assembled from the turn's markdown response parts (streamed deltas included).
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
      model/SessionInfo.kt            # a selectable session, tagged with its cptower host
      model/UiMessage.kt              # one rendered transcript row
      net/  ConnectionState.kt        # Disconnected / Connecting / Connected
            AhpConnection.kt          # OkHttp WebSocket + JSON-RPC driver + AHP state mirror
            HostCatalog.kt            # fetches cptower's GET /hosts catalog
      AhpTranscript.kt                # pure ChatState -> transcript projection (JVM-unit-testable)
      ChatViewModel.kt                # owns one connection per host, merges sessions + transcript
      ui/ChatScreen.kt                # Compose transcript + input + endpoint bar + session picker
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

The app tracks the AHP client's Kotlin toolchain (Kotlin 2.3.21, Compose compiler applied via the
`org.jetbrains.kotlin.plugin.compose` plugin), so the newer-Kotlin AHP artifact is consumed directly
with no metadata-version workarounds.

## Run against cptower

Start the cptower multiplexer on the host machine (see `tools/cptower/README.md`); it listens on
`0.0.0.0:8770` by default and discovers every live Copilot AHP host. Install and launch the app on a
USB-debugging device on the same network:

```ps1
$adb = "$env:ANDROID_HOME\platform-tools\adb.exe"
& $adb install -r app\build\outputs\apk\debug\app-debug.apk
& $adb shell am start -n com.qvim.companion/.MainActivity
```

In the app, set the endpoint to `<HOST-LAN-IP>:8770` and Connect. The session picker lists every
session across all discovered hosts; pick one and its transcript appears and updates live. Typing a
message and Send starts a new turn. Pointing the endpoint straight at a single AHP host (no cptower)
also works via the direct-connection fallback.

## Cleartext note

The app talks plaintext `ws://` for LAN use. Cleartext is enabled **only** in the debug manifest
(`src/debug`) via a network-security config; the release manifest has no such allowance. Use `wss://`
against a TLS-terminating host for encrypted transport.
