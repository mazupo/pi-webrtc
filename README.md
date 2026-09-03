<h1 align="center">pi-webrtc</h1>

<p align="center">
Ultra-low latency WebRTC streaming for Raspberry Pi and NVIDIA Jetson over WiFi, LTE, or 5G.
</p>

<p align="center">
    <a href="https://chromium.googlesource.com/external/webrtc/+/branch-heads/7680"><img src="https://img.shields.io/badge/libwebrtc-m146.7680-red.svg" alt="WebRTC Version"></a>
    <img src="https://img.shields.io/github/downloads/mazupo/pi-webrtc/total.svg?color=yellow" alt="Download">
    <img src="https://img.shields.io/badge/C%2B%2B-20-brightgreen?logo=cplusplus">
    <img src="https://img.shields.io/github/v/release/mazupo/pi-webrtc?color=blue" alt="Release">
    <a href="https://opensource.org/licenses/Apache-2.0"><img src="https://img.shields.io/badge/License-Apache_2.0-purple.svg" alt="License Apache"></a>
</p>

<p align="center">
    <img src="docs/pi_5_latency_demo.gif" alt="Raspberry Pi 5 latency demo">
</p>

## Features

- **Hardware encoding on-device** — V4L2 M2M on Raspberry Pi, NVENC on Jetson.
- **~200 ms glass-to-glass**, [~100](https://youtu.be/JgWeKSw_lkM) ms on Jetson.
- **Control travels back** — DataChannels carry messages the other way, and the IPC bridge relays
  them to any process on the device.
- **Runs on cellular** — NAT traversal, congestion control and packet-loss recovery come with
  WebRTC, so the same build works over LTE or 5G.
- **Two platforms, three camera backends** — libcamera, libargus, V4L2.
- **Pluggable signaling** — MQTT, WHEP, LiveKit and Cloudflare Realtime share one interface.

## Hardware Support

| Board | CSI backend | Hardware encode | Status |
| --- | --- | --- | --- |
| Pi Zero 2 W / 3B / 4 | libcamera | V4L2 M2M — H.264 (zero-copy DMABUF) | ✅ Tested |
| Pi 5 | libcamera | — (software OpenH264) | ✅ Tested |
| Jetson Orin NX | libargus | NVENC — H.264 + AV1 | ✅ Tested |
| Jetson Nano / NX / Orin | libargus | NVENC — H.264 | Supported |

USB cameras are supported through V4L2 on both platforms.

## Quick Start

Check out the [tutorial video](https://youtu.be/g5Npb6DsO-0) or follow these steps.

### 1. Flash Raspberry Pi OS

Use [Raspberry Pi Imager](https://www.raspberrypi.com/software/) to flash **Lite OS** to SD card.

### 2. Install Dependencies

```bash
sudo apt update
sudo apt install libmosquitto1 pulseaudio libavformat61 libswscale8 libyaml-cpp0.8
```

### 3. Download Binary

Get the latest [release binary](https://github.com/mazupo/pi-webrtc/releases) .
```bash
wget https://github.com/mazupo/pi-webrtc/releases/latest/download/pi-webrtc_raspios-trixie-arm64.tar.gz
tar -xzf pi-webrtc_raspios-trixie-arm64.tar.gz
```

Jetson builds are published on the same page as `pi-webrtc_jetson-l4t-<version>.tar.gz`.

### 4. MQTT Signaling

Use [HiveMQ](https://www.hivemq.com), [EMQX](https://www.emqx.com/en), or a [self-hosted](docs/SETUP_MOSQUITTO.md) broker.

> [!TIP]
> **MQTT** lets your Pi camera and client exchange WebRTC connection info.
**WHEP** doesn’t need a broker but requires a public hostname.
**LiveKit** needs an SFU server, and serves many viewers from one uplink.
**Cloudflare Realtime** serves just as many with no server of your own — the fan-out runs on Cloudflare's edge.

## Run the App

![preview_demo](https://github.com/user-attachments/assets/d472b6e0-8104-4aaf-b02b-9925c5c363d0)

- Open [picamera-web](https://app.picamera.live)  demo UI — add MQTT settings, and create a `UID`.
- Run the command on your Pi:
    ```bash
    ./pi-webrtc \
        --camera=libcamera:0 \
        --fps=30 \
        --width=1280 \
        --height=960 \
        --use-mqtt \
        --mqtt-host=your.mqtt.cloud \
        --mqtt-port=8883 \
        --mqtt-username=hakunamatata \
        --mqtt-password=Wonderful \
        --uid=your-custom-uid \
        --no-audio \
        --hw-accel
    ```

> [!IMPORTANT]
> Remove `--hw-accel` for Pi 5 or others without hardware encoder.

## Signaling & Integrations

| Transport | Best for | Clients |
| --- | --- | --- |
| **MQTT** | Peer-to-peer, no public hostname needed | [client-sdk-js](https://github.com/mazupo/client-sdk-js) · [picamera-app](https://github.com/TzuHuanTai/picamera-app) |
| **[WHEP](https://www.ietf.org/archive/id/draft-ietf-wish-whep-02.html)** | Playing a URL in any standard WebRTC player | [Home Assistant WebRTC Camera](https://github.com/AlexxIT/WebRTC) · [eyevinn/webrtc-player](https://www.npmjs.com/package/@eyevinn/webrtc-player) |
| **[LiveKit](https://livekit.io)** | Many simultaneous viewers from one uplink | [client-sdk-js](https://github.com/mazupo/client-sdk-js) · [livekit-sdk](https://github.com/livekit/client-sdk-js) |
| **[Cloudflare Realtime](https://developers.cloudflare.com/realtime/sfu/)** | Many simultaneous viewers with nothing to host | [client-sdk-js](https://github.com/mazupo/client-sdk-js) |

Signaling is pluggable — each transport implements the same interface in `src/signaling/`, and
more than one can be enabled at a time.

## Documentation

📚 **Full documentation → [picamera.live/docs](https://picamera.live/docs)**

[Configuration](docs/CONFIGURATION.md) · [Camera and Encoding](docs/CAMERA_AND_ENCODING.md) · [Signaling](docs/SIGNALING.md) · [Recording](docs/RECORDING.md) · [Architecture](docs/ARCHITECTURE.md) · [Building](docs/BUILD.md) · [Advanced usage](docs/ADVANCED.md)

## Commercial

Detection and tracking on Jetson, multi-camera capture, and publishing straight to LiveKit
or Cloudflare Realtime with no relay in between are licensed separately — see
[COMMERCIAL.md](docs/COMMERCIAL.md).

## License

Apache-2.0 — see [LICENSE](LICENSE).
