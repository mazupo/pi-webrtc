"""
Read Gamepad Input from the pi-webrtc IPC Socket
------------------------------------------------
`--enable-gamepad` exposes browser gamepads on a Unix socket as newline-delimited JSON.

Each line is a snapshot of the gamepads currently sending input:

    {"device_monotonic_ns": 81234567890123,
     "gamepads": {"alice": {"sequence": 1234, "left_x": 0.12, ..., "pressed": ["a", "rt"]}}}

This example prints the latest gamepad state.

Usage:
    1. Start pi-webrtc with gamepad support (it turns on --enable-ipc by itself):
        /path/to/pi-webrtc --camera=libcamera:0 --fps=30 ... --enable-gamepad

    2. Run this script on the device, then connect a gamepad in the browser:
        python ./examples/gamepad_socket.py
"""

import argparse
import asyncio
import json
import sys
import time

EMPTY = {"device_monotonic_ns": 0, "gamepads": {}}


class GamepadReader:
    def __init__(self, socket_path):
        self.socket_path = socket_path
        self.latest = EMPTY

    def age_ms(self):
        """How old the newest snapshot is, on the device clock the snapshot is stamped with."""
        return (time.monotonic_ns() - self.latest["device_monotonic_ns"]) / 1e6

    async def run(self):
        waiting = False
        while True:
            try:
                reader, writer = await asyncio.open_unix_connection(self.socket_path)
            except OSError as e:
                if not waiting:
                    print(f"Waiting for {self.socket_path} (is pi-webrtc running with "
                          f"--enable-gamepad?): {e}", flush=True)
                    waiting = True
                await asyncio.sleep(1)
                continue

            waiting = False
            print(f"Connected to {self.socket_path}", flush=True)
            try:
                # Nothing else here: a slow step would let snapshots pile up in asyncio's buffer.
                while line := await reader.readline():
                    self.latest = json.loads(line)
            finally:
                writer.close()
                await writer.wait_closed()

            self.latest = EMPTY
            print("\nDisconnected, reconnecting...", flush=True)
            await asyncio.sleep(0.5)


def format_pad(remote_id, pad):
    return (
        f"{remote_id[:8]} "
        f"L({pad['left_x']:+.2f},{pad['left_y']:+.2f}) "
        f"R({pad['right_x']:+.2f},{pad['right_y']:+.2f}) "
        f"LT {pad['left_trigger']:.2f} RT {pad['right_trigger']:.2f} "
        f"[{' '.join(pad['pressed'])}]"
    )


async def show(reader, stale_ms, hz=30):
    """Stands in for a control loop: runs at its own pace and only reads `reader.latest`."""
    last = None
    while True:
        await asyncio.sleep(1 / hz)
        age = reader.age_ms()
        pads = reader.latest["gamepads"] if age < stale_ms else {}
        if pads:
            line = " | ".join(format_pad(remote_id, pad) for remote_id, pad in pads.items())
        else:
            line = "nobody at the controls"

        if sys.stdout.isatty():
            print(f"\r{line:<110} {age if pads else 0:4.0f} ms", end="", flush=True)
        elif line != last:
            print(line, flush=True)
        last = line


async def main(socket_path, stale_ms):
    reader = GamepadReader(socket_path)
    await asyncio.gather(reader.run(), show(reader, stale_ms))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    parser.add_argument("--socket-path", default="/tmp/pi-webrtc-gamepad.sock",
                        help="matches pi-webrtc's --gamepad-socket-path")
    parser.add_argument("--stale-ms", type=float, default=100,
                        help="treat a snapshot older than this as nobody at the controls")
    args = parser.parse_args()

    try:
        asyncio.run(main(args.socket_path, args.stale_ms))
    except KeyboardInterrupt:
        print("\nExiting...")
