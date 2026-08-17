# qvim session protocol (v1)

This is the contract between qvim (server) and the Android companion
app (client). It is intentionally minimal for the echo-only skeleton; later slices extend
it **without breaking existing frames**.

qvim owns the session and is the single source of truth. The client is a subscriber that never owns
state. Transport for this milestone is a plaintext WebSocket (`ws://`).

## Framing

Each WebSocket text message is one JSON object with a `type` field. Unknown `type` values MUST be
ignored by the client (forward compatibility). All server→client message frames carry a monotonic,
never-reused `seq` scoped to a `sessionId`.

## Handshake

1. On connect the **server sends `hello` first**.
2. The **client replies with `resume`**, reporting the last `seq` it has applied.
3. The server then streams live events. In the echo skeleton the client always sends `lastSeq: 0`
   and the server does not replay.

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

`role` is `"user"` or `"assistant"` — it tells the client which side a block belongs to, so the
transcript renders each turn correctly (user input vs. assistant reply) and the reducer attributes
streamed chunks to the right author. The streamed `begin`/`delta`*/`end` shape mirrors qvim's
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
