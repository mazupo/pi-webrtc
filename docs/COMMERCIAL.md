# Commercial Version

`pi-webrtc` is open source under [Apache-2.0](../LICENSE). A few features are developed and
maintained separately and are available under a commercial license.

Throughout this documentation, anything marked with a <sup>[\*](COMMERCIAL.md#licensing)</sup>
links back to this page.

## What's Included

### Object Detection

On-device YOLO inference through TensorRT, with the results drawn onto the outgoing video
stream. Detection runs on a dedicated sub-stream (see [Configuration](CONFIGURATION.md#sub-stream))
so the main stream keeps its full resolution, and frames move between the camera, the
detector, and the encoder as CUDA/EGL handles without a copy back to the CPU.

| Flag | Description |
|---|---|
| `--detector-model` | Path to the TensorRT engine file. |
| `--detector-labels` | Class-name list, one per line. Defaults to the COCO 80 classes. |
| `--detector-confidence` | Minimum confidence threshold, `0.0`–`1.0`. |

### Object Tracking

Multi-object tracking on top of the detector, using NVIDIA's NvMOT library. Both the
correlation-filter tracker (NvDCF) and the re-identification tracker (DeepSORT) are
supported, selected by config file. Clients can toggle the overlay at runtime with the
`TOGGLE_TRACKING` DataChannel command.

| Flag | Description |
|---|---|
| `--tracker-config` | Path to an NvMOT YAML config, e.g. `config/tracker_NvDCF.yml` or `config/tracker_NvDeepSORT.yml`. |

### Multi-camera Capture

Drive several cameras from a single `pi-webrtc` process. Each camera is declared in the
`cameras:` list of a [YAML config file](CONFIGURATION.md#multi-camera), inherits the global
settings, and can override its own resolution, sub-stream, and recording directory — or opt
out of WebRTC or recording entirely.

### Direct LiveKit Connection

The open-source build reaches a LiveKit server through the signaling endpoint it is pointed at.
The commercial build can also connect to one directly, signing its own access token from an API
key/secret pair (`--livekit-secret`) each time it connects — which is what lets a fleet publish
into a LiveKit deployment you run yourself.

| Flag | Description |
|---|---|
| `--livekit-key` | LiveKit API key, paired with `--livekit-secret` to sign a token. |
| `--livekit-secret` | LiveKit API secret, paired with `--livekit-key`. Signs tokens on-device. |

### Cloudflare Realtime SFU

Both builds publish into a [Cloudflare Realtime](https://developers.cloudflare.com/realtime/sfu/)
app, so the fan-out runs on Cloudflare's edge and there is no SFU to host. The open-source build
relays the handshake through the device API, which holds the Realtime credentials. The
commercial build carries the handshake itself: given an App ID and App Secret it drives
Cloudflare's HTTPS API directly — creating the session, offering its tracks, and minting a fresh
session to recover whenever the link drops — so a fleet depends on nothing but Cloudflare. See
[Signaling](SIGNALING.md#cloudflare-realtime) for both flows.

| Flag | Description |
|---|---|
| `--cloudflare-app-id` | Realtime App ID to publish into. |
| `--cloudflare-app-secret` | Realtime App Secret, sent as the bearer token. |
| `--cloudflare-url` | Base URL of the Realtime API. Defaults to `https://rtc.live.cloudflare.com/v1`. |

## Device API

Not a licensed feature. It relays the Cloudflare Realtime handshake and holds the session a
viewer has to pull, so a device needs neither a Cloudflare account nor the handshake logic.
[api.mazupo.com](https://api.mazupo.com) is open for anyone to try — see
[Broadcasting to many viewers](ADVANCED.md#cloudflare-realtime).

| Flag | Description |
|---|---|
| `--api-url` | Base URL of the device API, e.g. `https://api.mazupo.com`. |
| `--api-key` | Bearer token authenticating this device against it. |

## Hardware Requirements

| Feature | Requirement |
|---|---|
| Object detection | NVIDIA Jetson with TensorRT and CUDA |
| Object tracking | NVIDIA Jetson with DeepStream (`libnvds_nvmultiobjecttracker.so`) |
| Multi-camera | Any supported platform |
| Direct LiveKit connection | Any supported platform |
| Cloudflare Realtime SFU | Any supported platform |

Detection and tracking are built against TensorRT, CUDA, and DeepStream, so they are
Jetson-only. They are not available on Raspberry Pi.

## Licensing

Licensing is arranged directly, so that terms can be matched to how the software is actually
deployed — a single device, a fleet, or an integration into a product you ship. Get in touch
and I'll send the current pricing and terms.

Worth mentioning when you write, so the first reply is a useful one:

- Which of the features above you need
- Target hardware and roughly how many devices
- Whether it is for internal use or for a product you distribute

## Contact

For licensing, a dedicated SFU environment, or integration questions:

**tzu.huan.tai@gmail.com**
