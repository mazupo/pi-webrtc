# SFU

An SFU (Selective Forwarding Unit) lets many people watch one device. The device sends one stream to the SFU, and the SFU forwards it to every viewer.

![SFU signaling](https://github.com/user-attachments/assets/2329c736-8d98-4148-af01-1966bce9af41)

With MQTT or WHEP, each viewer connects to the device directly. More viewers means more connections, more upload bandwidth and more encoding work on the device. With an SFU, the device does the same work for one viewer or for many.

pi-webrtc supports two SFUs:

- [LiveKit](#livekit): host it yourself, or use LiveKit Cloud.
- [Cloudflare Realtime](#cloudflare-realtime): run by Cloudflare.

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
> This is a shared test server. All users together get 100 connections, 5,000 minutes and 50 GB of transfer per month. For your own environment, see [Sponsors](../sponsors.md#commercial--oem-notes).

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

### 2. Join the room

- Code example: [Play through the LiveKit SFU](https://mazupo.com/docs/client-sdk-js/guides/livekit)
- Web app: [app.mazupo.com/room](https://app.mazupo.com/room)

## Cloudflare Realtime

### Free test server

| URL | Device API key | Viewer API key |
| --- | --- | --- |
| `https://api.mazupo.com` | `81f899b8fab5692b0faa76c3372b7ae6` | `ec0478c67e729b6f429eda1e97829af0` |

> [!WARNING]
> This is a shared test environment. All users share the free Cloudflare quota, and it stops when the monthly limit is reached. Use a unique `--uid`, so you do not clash with other users. For your own environment, see [Sponsors](../sponsors.md#commercial--oem-notes).

### 1. Run on the device

```bash
./pi-webrtc --camera=libcamera:0 \
    --fps=30 \
    --width=1920 \
    --height=1080 \
    --uid=your-display-name \
    --use-cloudflare \
    --api-url=https://api.mazupo.com \
    --api-key=81f899b8fab5692b0faa76c3372b7ae6
```

The device connects to Cloudflare Realtime through the Mazupo API. Cloudflare gives the device a new session each time it reconnects, and the API links that session to the device's `uid`.

- `--api-url`: the API address.
- `--api-key`: the key that lets the device use the API.
- `--uid`: the device name. Anyone who knows it can find the stream.

Cloudflare Realtime does not carry DataChannel or IPC messages yet. `--enable-ipc` still works for the other signaling that you turn on.

### 2. Watch the stream

- Code example: [Pull from the Cloudflare Realtime SFU](https://mazupo.com/docs/client-sdk-js/guides/cloudflare)
- Web app: open [app.mazupo.com/cloudflare](https://app.mazupo.com/cloudflare). Under **Settings → Network**, enter the URL and the **Viewer API key**. Add a device with the same `uid`, then select it.

## Keep device control with MQTT

SFU viewers do not get the built-in `command` and `stream` DataChannels. So snapshots, recording control, camera control and file transfer only work over MQTT. You can turn on `--use-mqtt` together with an SFU: the SFU carries the viewers, and MQTT keeps those commands. See [DataChannels](../reference/datachannels.md).
