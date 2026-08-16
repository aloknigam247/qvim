# qvim session mirror — wire protocol (v1)

This is the contract between the qvim session mirror (server, issue #49) and the Android companion
app (client, issue #48). It is intentionally minimal for the echo-only skeleton; later slices extend
it **without breaking existing frames**.

qvim owns the session and is the single source of truth. The client is a subscriber that never owns
state. Transport for this milestone is a plaintext WebSocket (`ws://`); encryption is issue #54.

## Framing

Each WebSocket text message is one JSON object with a `type` field. Unknown `type` values MUST be
ignored by the client (forward compatibility). All server→client message frames carry a monotonic,
never-reused `seq` scoped to a `sessionId`.

## Handshake

1. On connect the **server sends `hello` first**.
2. The **client replies with `resume`**, reporting the last `seq` it has applied.
3. The server then streams live events. Server-side replay of `seq > lastSeq` is **reserved for
   #51** and is not implemented in the echo skeleton (the client always sends `lastSeq: 0`).

If a later `hello` carries a different `sessionId` (qvim restarted, `seq` numbering restarts), the
client resets its transcript and `seq` cursor.

## Server → client frames

| type            | fields                                  | meaning                                  |
| --------------- | --------------------------------------- | ---------------------------------------- |
| `hello`         | `protocol` (int), `sessionId` (string)  | greeting; sent first on connect          |
| `message`       | `seq`, `id`, `role`, `text`             | an atomic message block (e.g. user echo) |
| `message.begin` | `seq`, `id`, `role`                     | start of a streamed message              |
| `message.delta` | `seq`, `id`, `text`                     | a chunk appended to `id`                 |
| `message.end`   | `seq`, `id`                             | end of a streamed message                |

`role` is `"user"` or `"assistant"`. The streamed `begin`/`delta`*/`end` shape mirrors qvim's
`ChatModel`, which appends the user block atomically and streams the assistant reply in chunks.

```json
{"type":"hello","protocol":1,"sessionId":"7f3c…"}
{"seq":1,"type":"message","id":"u1","role":"user","text":"hi"}
{"seq":2,"type":"message.begin","id":"a1","role":"assistant"}
{"seq":3,"type":"message.delta","id":"a1","text":"Echo: "}
{"seq":4,"type":"message.delta","id":"a1","text":"hi"}
{"seq":5,"type":"message.end","id":"a1"}
```

## Client → server frames

| type     | fields                        | meaning                                        |
| -------- | ----------------------------- | ---------------------------------------------- |
| `input`  | `text`                        | the user typed a message into the session      |
| `resume` | `lastSeq`, `sessionId` (opt.) | sent after `hello`; highest `seq` applied so far |

```json
{"type":"resume","lastSeq":0,"sessionId":null}
{"type":"input","text":"hi"}
```

## Reserved for later slices

- **#51 resume replay** — server replays `seq > lastSeq` from a bounded log, then goes live. Behaviour
  when `lastSeq` is below the log's retention floor (full resync/snapshot) is defined there.
- **#52 client persistence** — the client persists `lastSeq` across restarts and dedups on reconnect.
- **#50 discovery** — the endpoint is advertised over mDNS instead of being entered by hand.
- **#53 auth** — an `Authorization` header is added on the WebSocket upgrade (single Entra account).
- **#54 encryption** — the scheme becomes `wss://` / an encrypted channel.

Gap detection (a missing `seq` in the live stream) is **reserved for #51**; WebSocket itself preserves
order, so the echo skeleton does not implement gap recovery.
