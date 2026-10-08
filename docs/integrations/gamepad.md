# Gamepad

Control the device with a gamepad that is connected to the browser. pi-webrtc sends the gamepad state to a Unix socket on the device, as simple JSON lines. Your program does not need to know anything about WebRTC.

[`--enable-gamepad`](../reference/configuration.md#ipc) turns this on. It also turns on [IPC](ipc.md).

The browser sends the gamepad state about 60 times per second, over a lossy DataChannel.

## Read the gamepad from Python

The [gamepad_socket.py](../../examples/gamepad_socket.py) example prints every gamepad state that pi-webrtc receives.

1. Start pi-webrtc with gamepad support:

    ```bash
    ./pi-webrtc --camera=libcamera:0 ... --use-mqtt ... --enable-gamepad
    ```

2. Run the example, then connect a gamepad in the browser:

    ```bash
    python3 examples/gamepad_socket.py
    ```

- Code example: [Drive a device with a gamepad](https://mazupo.com/docs/client-sdk-js/guides/gamepad). The sender reads the browser's [Gamepad API](https://developer.mozilla.org/en-US/docs/Web/API/Gamepad_API).
- Web app: [app.mazupo.com/gamepad](https://app.mazupo.com/gamepad)

The socket is `/tmp/pi-webrtc-gamepad.sock` by default. Change it with `--gamepad-socket-path`.

To drive a ROS 2 robot with the gamepad, see [ROS](ros.md#drive-the-robot-with-a-gamepad).

## Snapshots

Each line holds the latest state of every connected gamepad:

```json
{"device_monotonic_ns":81234567890123,"gamepads":{"alice":{"sequence":1234,"timestamp":{"monotonic_ns":5023000000},"left_x":0.12,"left_y":-0.5,"right_x":0.0,"right_y":0.0,"left_trigger":0.0,"right_trigger":0.87,"buttons":129,"pressed":["a","rt"],"standard_mapping":true}}}
```

| Field | Description |
| --- | --- |
| `device_monotonic_ns` | When pi-webrtc made this snapshot, on the device's monotonic clock. Python's `time.monotonic_ns()` reads the same clock. |
| `gamepads` | One entry per sender. Empty when no gamepad is sending. |
| `sequence` | The sender's report counter. A gap means some snapshots were dropped. |
| `timestamp.monotonic_ns` | The browser's timestamp, when it sends one. Do not compare it with `device_monotonic_ns`. |
| `left_x`, `left_y`, `right_x`, `right_y` | Sticks, from -1 to 1. `+y` is down. |
| `left_trigger`, `right_trigger` | From 0 to 1. |
| `buttons` | The button state, as a bit mask. |
| `pressed` | The names of the pressed standard buttons (bits 0 to 16). |
| `standard_mapping` | `false` when the browser does not know the standard layout. Then the button names may not match the real buttons. |

The key of each gamepad is the peer id that pi-webrtc gives each MQTT client, or the LiveKit participant identity. It stays the same across WebRTC reconnects within `--peer-timeout`.

New fields may be added in later releases, so ignore fields that you do not know.

## Button mapping

The browser uses the W3C standard gamepad mapping when it can.

| Bit | Name | Xbox | PlayStation |
| --- | --- | --- | --- |
| 0 | `a` | A | Cross |
| 1 | `b` | B | Circle |
| 2 | `x` | X | Square |
| 3 | `y` | Y | Triangle |
| 4 | `lb` | LB | L1 |
| 5 | `rb` | RB | R1 |
| 6 | `lt` | LT | L2 |
| 7 | `rt` | RT | R2 |
| 8 | `back` | View | Create |
| 9 | `start` | Menu | Options |
| 10 | `l3` | Left stick press | L3 |
| 11 | `r3` | Right stick press | R3 |
| 12 | `dpad_up` | D-pad up | D-pad up |
| 13 | `dpad_down` | D-pad down | D-pad down |
| 14 | `dpad_left` | D-pad left | D-pad left |
| 15 | `dpad_right` | D-pad right | D-pad right |
| 16 | `guide` | Xbox button | PS button |

## Disconnects and old input

- A gamepad is removed as soon as pi-webrtc closes its connection.
- It is also removed when no input arrives from it for 500 ms. For example, the browser tab is in the background, the gamepad is unplugged, or the network is down.
- When nobody is sending, no new snapshot is made. Check the age of the last one, `time.monotonic_ns() - device_monotonic_ns`, against your own limit.
