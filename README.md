<h1 align="center">pi-webrtc</h1>


<p align="center">
    <a href="https://chromium.googlesource.com/external/webrtc/+/branch-heads/7727"><img src="https://img.shields.io/badge/libwebrtc-m147.7727-red.svg" alt="WebRTC Version"></a>
    <img src="https://img.shields.io/github/downloads/mazupo/pi-webrtc/total.svg?color=yellow" alt="Download">
    <img src="https://img.shields.io/badge/C%2B%2B-20-brightgreen?logo=cplusplus">
    <img src="https://img.shields.io/github/v/release/mazupo/pi-webrtc?color=blue" alt="Release">
    <a href="https://opensource.org/licenses/Apache-2.0"><img src="https://img.shields.io/badge/License-Apache_2.0-purple.svg" alt="License Apache"></a>
</p>

## What is pi-webrtc?

A single binary that streams a camera from a Raspberry Pi or Jetson to a browser over WebRTC, and carries control messages back to the device. It works over WiFi, LTE, or 5G with no public IP.

## Features

- **Low-latency video** — WebRTC with NAT traversal and congestion control.
- **Remote control** — commands and telemetry over WebRTC DataChannels.
- **Hardware acceleration** — H.264/AV1 on supported devices.

## Signaling

| Signaling | Use case |
| --- | --- |
| **[MQTT](./docs/SIGNALING.md#mqtt)** | Peer-to-peer with device control |
| **[WHEP](./docs/SIGNALING.md#whep)** | Playback with any standard WebRTC player |
| **[SFU](./docs/SIGNALING.md#sfu)** | Simultaneous viewers via [LiveKit](https://livekit.com) or [Cloudflare Realtime](https://developers.cloudflare.com/realtime/sfu/) |

Multiple signaling options can be enabled simultaneously. See [SIGNALING.md](./docs/SIGNALING.md).

## Quick Start

Install the dependencies and download the latest release on **Raspberry Pi OS Trixie (64-bit)**.

```bash
sudo apt update
sudo apt install libmosquitto1 pulseaudio libavformat61 libswscale8 libyaml-cpp0.8

wget https://github.com/mazupo/pi-webrtc/releases/latest/download/pi-webrtc_raspios-trixie-arm64.tar.gz
tar -xzf pi-webrtc_raspios-trixie-arm64.tar.gz
```

### 1. Local Test (WHEP)

Stream camera directly over your local network.

```bash
./pi-webrtc --camera=libcamera:0 --uid=home-pi-5 --no-audio --use-whep
```

Open [app.mazupo.com/whep](http://app.mazupo.com/whep), enter `http://<your-pi-ip>:8080` as the **WHEP URL**, and connect.

### 2. Remote P2P (MQTT)

Use MQTT when you need remote access or device control.

```bash
./pi-webrtc \
    --camera=libcamera:0 \
    --fps=60 \
    --width=1280 \
    --height=720 \
    --use-mqtt \
    --mqtt-host=example.s1.eu.hivemq.cloud \
    --mqtt-port=8883 \
    --mqtt-username=your-username \
    --mqtt-password=your-password \
    --uid=home-pi-5 \
    --no-audio \
    --hw-accel # auto-falls back to software if unsupported
```

Use a free [HiveMQ Cloud](https://www.hivemq.com) instance or your own broker, then open the [demo web](https://app.mazupo.com) with the same MQTT settings and `uid`. See [QUICK_START.md](./docs/QUICK_START.md) for more details.

![preview_demo](https://github.com/user-attachments/assets/d472b6e0-8104-4aaf-b02b-9925c5c363d0)

## Hardware

| Platform | Camera backend | Hardware encoding |
| --- | --- | --- |
| Raspberry Pi | libcamera | H.264 on supported models |
| NVIDIA Jetson | libargus | H.264 / AV1 on supported models |

USB cameras are supported via V4L2 on both platforms.

## Documentation

📚 **[Full documentation](https://mazupo.com/docs)**

## Support the project

Sponsors help fund continued development and receive access to additional releases, features, and documentation. See [SPONSORS.md](./docs/SPONSORS.md).

## License

Apache-2.0 — see [LICENSE](LICENSE). Third-party notices: see [NOTICE](NOTICE).
