<h1 align="center">pi-webrtc</h1>


<p align="center">
    <a href="https://chromium.googlesource.com/external/webrtc/+/branch-heads/7680"><img src="https://img.shields.io/badge/libwebrtc-m146.7680-red.svg" alt="WebRTC Version"></a>
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
- **Multiple signaling options** — MQTT, WHEP, LiveKit, and Cloudflare Realtime.

## Signaling

| Transport | Use case |
| --------- | -------- |
| **MQTT**  | Peer-to-peer with device control |
| **[WHEP](https://www.ietf.org/archive/id/draft-ietf-wish-whep-04.html)** | Play from any standard WebRTC player |
| **[LiveKit](https://livekit.com)** | Many viewers through an SFU |
| **[Cloudflare Realtime](https://developers.cloudflare.com/realtime/sfu/)** | Many viewers without hosting an SFU |

## Quick Start

Install Dependencies
```bash
sudo apt update
sudo apt install libmosquitto1 pulseaudio libavformat61 libswscale8 libyaml-cpp0.8
```

Download the latest [release](https://github.com/mazupo/pi-webrtc/releases):
```bash
wget https://github.com/mazupo/pi-webrtc/releases/latest/download/pi-webrtc_raspios-trixie-arm64.tar.gz
tar -xzf pi-webrtc_raspios-trixie-arm64.tar.gz
```

Run on Raspberry Pi:

```bash
./pi-webrtc \
    --camera=libcamera:0 \
    --fps=60 \
    --width=1280 \
    --height=720 \
    --use-mqtt \
    --mqtt-host=your.mqtt.cloud \
    --mqtt-port=8883 \
    --mqtt-username=... \
    --mqtt-password=... \
    --uid=your-custom-uid \
    --no-audio \
    --hw-accel # auto-falls back to software if unsupported
```

Open the [demo web](https://app.mazupo.com) and connect using the same MQTT/`uid` settings.

![preview_demo](https://github.com/user-attachments/assets/d472b6e0-8104-4aaf-b02b-9925c5c363d0)

See the [Quick Start](./docs/QUICK_START.md) guide for Raspberry Pi and Jetson.

## Hardware

| Platform | Camera backend | Hardware encoding |
| --- | --- | --- |
| Raspberry Pi* | libcamera | H.264 |
| NVIDIA Jetson* | libargus | H.264 / AV1 |

USB cameras are supported via V4L2 on both platforms.

\* Pi 5 and Jetson Nano without hardware encoder.

## Documentation

📚 **[Full documentation](https://mazupo.com/docs)**

## Support the project

Sponsors help fund continued development and receive access to additional releases, features, and documentation. See [SPONSORS.md](./docs/SPONSORS.md).

## License

Apache-2.0 — see [LICENSE](LICENSE).
