<h1 align="center">pi-webrtc</h1>


<p align="center">
    <a href="https://chromium.googlesource.com/external/webrtc/+/branch-heads/7727"><img src="https://img.shields.io/badge/libwebrtc-m147.7727-red.svg" alt="WebRTC Version"></a>
    <img src="https://img.shields.io/github/downloads/mazupo/pi-webrtc/total.svg?color=yellow" alt="Download">
    <img src="https://img.shields.io/badge/C%2B%2B-20-brightgreen?logo=cplusplus">
    <img src="https://img.shields.io/github/v/release/mazupo/pi-webrtc?color=blue" alt="Release">
    <a href="https://opensource.org/licenses/Apache-2.0"><img src="https://img.shields.io/badge/License-Apache_2.0-purple.svg" alt="License Apache"></a>
</p>

## What is pi-webrtc?

A single binary that streams a camera from a Raspberry Pi or Jetson to a browser over WebRTC, and carries control messages back to the device. It delivers sub-100ms latency on LAN and works remotely over WiFi, LTE, or 5G with no public IP.

<p align="center">
  <img width="600" alt="Raspberry Pi 5 streaming to a browser over WebRTC with gamepad control" src="https://github.com/user-attachments/assets/ee31a2ad-99ad-4be1-9b17-ff5b0522ecde" />
</p>

<p align="center">
  <a href="https://youtu.be/TEWzM436vuc">▶ Watch the full demo</a>
</p>

## Features

- **Low-latency video** — WebRTC with NAT traversal and congestion control.
- **Remote control** — commands and telemetry over WebRTC DataChannels.
- **Hardware acceleration** — H.264/AV1 on supported devices.
- **RTSP input** — add WebRTC and DataChannels to an existing IP camera.

## Signaling

| Signaling | Use case |
| --- | --- |
| **[WHEP](./docs/signaling/whep.md)** | Playback with any standard WebRTC player |
| **[MQTT](./docs/signaling/mqtt.md)** | Peer-to-peer from anywhere, with device control |
| **[SFU](./docs/signaling/sfu.md)** | Many viewers at once via [LiveKit](https://livekit.com) or [Cloudflare Realtime](https://developers.cloudflare.com/realtime/sfu/) |

Several signaling options can run at the same time. See [Signaling](./docs/signaling/README.md).

## Quick Start

On a **Raspberry Pi** with Raspberry Pi OS Trixie (64-bit) and a CSI camera:

```bash
sudo apt update
sudo apt install libmosquitto1 pulseaudio libavformat61 libyaml-cpp0.8

wget https://github.com/mazupo/pi-webrtc/releases/latest/download/pi-webrtc_raspios-trixie-arm64.tar.gz
tar -xzf pi-webrtc_raspios-trixie-arm64.tar.gz

./pi-webrtc --camera=libcamera:0 --uid=my-pi --no-audio --use-whep
```

Open [app.mazupo.com/whep](http://app.mazupo.com/whep), enter `http://<your-pi-ip>:8080` as the **WHEP URL**, and connect.

Step-by-step guides: [Raspberry Pi](./docs/getting-started/raspberry-pi.md) · [NVIDIA Jetson](./docs/getting-started/jetson.md) · [Watch from anywhere with MQTT](./docs/signaling/mqtt.md)

![preview_demo](https://github.com/user-attachments/assets/d472b6e0-8104-4aaf-b02b-9925c5c363d0)

## Hardware

| Platform | Camera backend | Hardware encoding |
| --- | --- | --- |
| Raspberry Pi | libcamera | H.264 on supported models |
| NVIDIA Jetson | libargus | H.264 / AV1 on supported models |

USB cameras (V4L2) and [RTSP streams](./docs/media/rtsp.md) work on both platforms.

## Documentation

📚 **[Full documentation](https://mazupo.com/docs)**, also readable in [docs/](./docs/README.md).

## Support the project

Sponsors help fund continued development and receive access to additional releases, features, and documentation. See [Sponsors](./docs/sponsors.md).

## License

Apache-2.0 — see [LICENSE](LICENSE). Third-party notices: see [NOTICE](NOTICE).
