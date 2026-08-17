#!/usr/bin/env python3
"""Dev echo server for the qvim companion app.

A stand-in for the real qvim session mirror, used to exercise the Android client
end-to-end before the C++ server exists. It speaks the wire protocol in
docs/protocol/session-protocol.md and mimics qvim's ChatModel: on each `input` it
appends an atomic `user` message, then streams an `assistant` "Echo: <text>" reply
via message.begin/delta*/end with a monotonically increasing seq.

This is a dev fixture, not the deliverable. Run it on the PC, then point the phone
at ws://<PC-LAN-IP>:8765.

    pip install -r requirements.txt
    python echo_ws.py            # binds 0.0.0.0:8765
"""
import asyncio
import json
import uuid

import websockets

HOST = "0.0.0.0"
PORT = 8765
ECHO_CHUNKS = 3


def chunkify(s: str, parts: int):
    if not s:
        return []
    n = max(1, parts)
    size = -(-len(s) // n)  # ceil division
    return [s[i:i + size] for i in range(0, len(s), size)]


async def handler(ws):
    session_id = str(uuid.uuid4())
    seq = 0

    def nxt():
        nonlocal seq
        seq += 1
        return seq

    # Server greets first.
    await ws.send(json.dumps({"type": "hello", "protocol": 1, "sessionId": session_id}))

    async for raw in ws:
        try:
            frame = json.loads(raw)
        except json.JSONDecodeError:
            continue
        if frame.get("type") != "input":
            continue  # resume/other frames are accepted and ignored in this fixture

        text = frame.get("text", "")

        # Atomic user block.
        uid = "u" + uuid.uuid4().hex[:8]
        await ws.send(json.dumps(
            {"seq": nxt(), "type": "message", "id": uid, "role": "user", "text": text}))

        # Streamed assistant echo.
        aid = "a" + uuid.uuid4().hex[:8]
        await ws.send(json.dumps(
            {"seq": nxt(), "type": "message.begin", "id": aid, "role": "assistant"}))
        for part in chunkify("Echo: " + text, ECHO_CHUNKS):
            await ws.send(json.dumps(
                {"seq": nxt(), "type": "message.delta", "id": aid, "text": part}))
            await asyncio.sleep(0.05)
        await ws.send(json.dumps({"seq": nxt(), "type": "message.end", "id": aid}))


async def main():
    print(f"echo server listening on ws://{HOST}:{PORT}")
    async with websockets.serve(handler, HOST, PORT):
        await asyncio.Future()  # run forever


if __name__ == "__main__":
    asyncio.run(main())
