"""
Read Gamepad Input from the pi-webrtc IPC Socket
------------------------------------------------
`--enable-gamepad` serves a `gamepad` IPC endpoint on its own socket, carrying the browser's
gamepad as `protocol.InputReport` (see `external/protocol/protos/input.proto`). Each payload
has a big-endian uint32 length in front of it. The endpoint is one-way: the device writes,
consumers read.

Usage:
    1. Install the protobuf runtime and compiler:
        pip install protobuf
        sudo apt install protobuf-compiler

    2. Generate the bindings this script imports:
        protoc -I external/protocol/protos --python_out=examples input.proto common.proto

    3. Start pi-webrtc (`--enable-gamepad` needs `--enable-ipc`):
        /path/to/pi-webrtc --camera=libcamera:0 --fps=30 ... --enable-ipc --enable-gamepad

    4. Run this script on the device, then connect a gamepad in the browser:
        python ./examples/gamepad_socket.py
"""

import argparse
import asyncio
import os
import sys

try:
    from input_pb2 import InputReport
except ImportError:
    sys.exit("Missing input_pb2.py. Generate it first:\n"
             "    protoc -I external/protocol/protos --python_out=examples "
             "input.proto common.proto")

# buttons[N].pressed is bit N, in the W3C "standard" mapping.
BUTTON_NAMES = [
    "A", "B", "X", "Y", "LB", "RB", "LT", "RT",
    "Back", "Start", "L3", "R3", "Up", "Down", "Left", "Right", "Guide",
]


def format_report(report, dropped):
    gamepad = report.gamepad
    pressed = [name for bit, name in enumerate(BUTTON_NAMES) if gamepad.buttons >> bit & 1]
    return (
        f"seq {report.sequence:<8} "
        f"L({gamepad.left_x:+.2f},{gamepad.left_y:+.2f}) "
        f"R({gamepad.right_x:+.2f},{gamepad.right_y:+.2f}) "
        f"LT {gamepad.left_trigger:.2f} RT {gamepad.right_trigger:.2f} "
        f"[{' '.join(pressed)}]"
        f"{'' if dropped == 0 else f' dropped {dropped}'}"
    )


class GamepadSocketClient:
    def __init__(self, socket_path):
        self.socket_path = socket_path
        self.last_sequence = 0
        self.warned_mapping = False

    async def read_reports(self, reader):
        while not reader.at_eof():
            try:
                header = await reader.readexactly(4)
                length = int.from_bytes(header, byteorder="big")
                if length > 1024 * 1024:
                    print(f"\npayload too large: {length} bytes")
                    return
                payload = await reader.readexactly(length)
            except asyncio.IncompleteReadError:
                return  # the device closed the connection

            report = InputReport()
            report.ParseFromString(payload)
            if report.WhichOneof("input") != "gamepad":
                continue

            if not report.gamepad.standard_mapping and not self.warned_mapping:
                print("\nNon-standard mapping: button names may not match this gamepad")
                self.warned_mapping = True

            dropped = max(0, report.sequence - self.last_sequence - 1) if self.last_sequence else 0
            self.last_sequence = report.sequence

            line = format_report(report, dropped)
            if sys.stdout.isatty():
                print(f"\r{line:<110}", end="", flush=True)
            else:
                print(line, flush=True)

    async def run(self):
        waiting = False
        while True:
            if not os.path.exists(self.socket_path):
                if not waiting:
                    print(f"Waiting for {self.socket_path} (is pi-webrtc running with "
                          f"--enable-ipc --enable-gamepad?)")
                    waiting = True
                await asyncio.sleep(1)
                continue

            try:
                reader, writer = await asyncio.open_unix_connection(self.socket_path)
            except OSError as e:
                if not waiting:
                    print(f"Waiting for {self.socket_path}: {e}")
                    waiting = True
                await asyncio.sleep(1)
                continue

            waiting = False
            self.last_sequence = 0
            self.warned_mapping = False
            print(f"Connected to {self.socket_path}")
            try:
                await self.read_reports(reader)
            finally:
                writer.close()
                await writer.wait_closed()
            print("\nDisconnected, reconnecting...")
            await asyncio.sleep(0.5)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    parser.add_argument("--socket-path", default="/tmp/pi-webrtc-gamepad.sock",
                        help="matches pi-webrtc's --gamepad-socket-path")
    args = parser.parse_args()

    try:
        asyncio.run(GamepadSocketClient(args.socket_path).run())
    except KeyboardInterrupt:
        print("Exiting...")
