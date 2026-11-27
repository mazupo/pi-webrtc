# SFU

An SFU (Selective Forwarding Unit) lets many people watch one device. The device sends one stream to the SFU, and the SFU forwards it to every viewer.

![SFU signaling](https://github.com/user-attachments/assets/2329c736-8d98-4148-af01-1966bce9af41)

With MQTT or WHEP, each viewer connects to the device directly. More viewers means more connections, more upload bandwidth and more encoding work on the device. With an SFU, the device does the same work for one viewer or for many.

pi-webrtc supports two SFUs:

- [LiveKit](#livekit): host it yourself, or use LiveKit Cloud.
- [Cloudflare Realtime](#cloudflare-realtime): rooms hosted by the maintainer on Cloudflare.

Both have a free test server below.

![SFU cloud service](https://github.com/user-attachments/assets/e41bcd7b-7c84-4837-88c2-820b20c094d4)

## Video quality

The SFU forwards the stream as it is, so every viewer gets the same quality. Set the highest bitrate on the device with [`--max-bitrate`](../reference/configuration.md#webrtc). When the upload is slow, pi-webrtc lowers the resolution and keeps the frame rate.

## LiveKit

### Free test server

| URL | API key |
| --- | --- |
| `wss://api.mazupo.com` | `APIWnQTs4tmUZvA` |

> [!WARNING]
> This is a shared test server. All users together get 100 connections, 5,000 minutes and 50 GB of transfer per month. For your own environment, run your own LiveKit server, or use the rooms that come with a [Mazupo Cloud plan](https://mazupo.com/pricing).

### 1. Run on the device

```bash
./pi-webrtc --camera=libcamera:0 \
    --fps=30 \
    --width=1920 \
    --height=1080 \
    --uid=your-display-name \
    --use-livekit \
    --livekit-url=wss://api.mazupo.com \
    --livekit-key=APIWnQTs4tmUZvA \
    --livekit-room=the-room-name
```

- `--livekit-url`: the SFU address. Use `wss://` for TLS, or `ws://` without TLS.
- `--livekit-key`: the LiveKit API key.
- `--livekit-room`: the room to publish to.
- `--uid`: the name of the device in the room.
- [`--livekit-secret`](../sponsors.md#sponsor-benefits) (sponsor build): creates the LiveKit access token on the device, so you do not need a token server.

Anyone who joins the same room can watch the stream. With `--enable-ipc`, DataChannel messages also go to everyone in the room.

Viewers in the room can talk. The device plays their voices only with `--enable-ipc`, and then it also gets the other devices' video and audio. See [Two-way audio](../media/audio.md#what-works-where).

### 2. Join the room

- Code example: [Play through the LiveKit SFU](https://mazupo.com/docs/client-sdk-js/guides/livekit)
- Web app: [app.mazupo.com/room](https://app.mazupo.com/room)

## Cloudflare Realtime

The device connects to a worker that the maintainer hosts on Cloudflare. The worker holds the Cloudflare credentials. The device only needs the worker's device public key. Rooms of your own come with a [Mazupo Cloud plan](https://mazupo.com/pricing). Try the free test server below first.

The device streams only while someone watches:

- The device connects to the worker and waits. It does not stream.
- When a viewer joins one of the device's rooms, the device starts streaming. The first picture usually takes 2 to 3 seconds.
- About 30 seconds after the last viewer leaves, the device stops streaming.

The camera stays on the whole time, so recording is not affected.

### Free test server

| URL | Device public key | Viewer public key |
| --- | --- | --- |
| `https://api.mazupo.com/sfu` | `1L4-mUUztDWhzF_ORhPHb4xpQK2Y9bEgeif0pYn7PXI` | `-706d13ForZLHmv6FwARf9ZgEEfO78AattyxWhNoHFo` |

> [!WARNING]
> This is a shared test environment. All users share the free Cloudflare quota. Anyone who knows a room name can watch that room, so use room names and a `--uid` that are hard to guess. For rooms of your own, see the [Mazupo Cloud plans](https://mazupo.com/pricing), or run your own [LiveKit](#livekit) server.

### 1. Run on the device

```bash
./pi-webrtc --camera=libcamera:0 \
    --fps=30 \
    --width=1920 \
    --height=1080 \
    --uid=your-device-name \
    --use-cloudflare \
    --api-url=https://api.mazupo.com/sfu \
    --api-key=1L4-mUUztDWhzF_ORhPHb4xpQK2Y9bEgeif0pYn7PXI \
    --cloudflare-room=your-room
```

- `--api-url`: the worker's API URL. It ends with `/sfu`.
- `--api-key`: the worker's device public key.
- `--uid`: the device ID. Use letters, digits, `.`, `_` and `-`. Every device needs its own ID. A second device with the same `--uid` cannot connect: it logs an error and tries again every 30 seconds.
- `--cloudflare-room`: a room the device joins. Viewers watch a room.

### Rooms

A viewer joins one room and sees every device in it. A device can join up to 4 rooms. Give `--cloudflare-room` once for each room:

```bash
    --cloudflare-room=front-yard \
    --cloudflare-room=cam-1-only
```

To let someone watch only one device, give that device a room of its own, like `cam-1-only` above.

In a [YAML config file](../reference/configuration.md#config-file), write the rooms as a list:

```yaml
use-cloudflare: true
cloudflare-room: [front-yard, cam-1-only]
```

### Log messages

| Message | Meaning |
| --- | --- |
| `Connected to the Cloudflare worker as ... (state sleeping, ...)` | The device is connected and waits for viewers. |
| `Someone is watching` | A viewer joined. The device starts streaming. |
| `Publishing to Cloudflare` | The stream reaches Cloudflare. Viewers can play it. |
| `Nobody is watching. Stopping the Cloudflare stream` | The last viewer left. The device stops streaming. |
| `Playing the voices of 2 viewer(s)` | A viewer started or stopped a microphone. The device now gets this many voices. |
| `Another connection to the Cloudflare worker uses --uid=...` | Two devices use the same `--uid`. Change one of them. Right after a power cut, it can be this device's last run: wait a minute. |
| `no access token from ... 401 key_invalid` | `--api-key` is not the worker's device public key. |

When the connection to the worker drops, the device connects again. The stream keeps going while it reconnects.

### IPC messages

Add [`--enable-ipc`](../reference/configuration.md#ipc) to send messages between the browser and a program on the device. See [IPC messages](../integrations/ipc.md).

- Messages from the device go to every viewer that turned on IPC.
- Only the viewer that controls the device can send to it. Other viewers wait in line.
- When a viewer takes control, the log says `Cloudflare viewer <room>/<viewer> controls this device`. The program on the Unix socket is not told.

### Viewers' voices

Viewers in a room can talk to each other, and the device plays their voices on its speaker:

- Viewers join muted. The device plays only the viewers who unmute.
- A device in several rooms plays the viewers of every room.
- With `--no-audio`, the device plays nothing, and the worker sends it no voices.
- Keep the speaker away from the microphone. Otherwise the viewers hear themselves.

### 2. Watch the stream

- Web app: open [app.mazupo.com/cloudflare](https://app.mazupo.com/cloudflare). Enter the viewer public key and the room.
- Code example: [Pull from the Cloudflare Realtime SFU](https://mazupo.com/docs/client-sdk-js/guides/cloudflare)
- The worker's own viewer page: [api.mazupo.com/sfu/](https://api.mazupo.com/sfu/). Enter the viewer public key and the room.

## Keep device control with MQTT

SFU viewers do not get the built-in `command` and `stream` DataChannels. So snapshots, recording control, camera control and file transfer only work over MQTT. You can turn on `--use-mqtt` together with an SFU: the SFU carries the viewers, and MQTT keeps those commands. See [DataChannels](../reference/datachannels.md).
