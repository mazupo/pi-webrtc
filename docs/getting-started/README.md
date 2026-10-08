# Getting Started

Install pi-webrtc and see live video in your browser. It takes about 10 minutes. You only need the device, a camera, and a computer on the same network.

## Choose your device

- [Raspberry Pi](raspberry-pi.md): Raspberry Pi OS Trixie (64-bit) with a CSI or USB camera.
- [NVIDIA Jetson](jetson.md): JetPack 6 with a CSI or USB camera.
- [Troubleshooting](troubleshooting.md): common errors and how to fix them.

Do you already have an IP camera? See [RTSP cameras](../media/rtsp.md).

## What you need

| | |
|---|---|
| Device | Raspberry Pi (Zero 2 W or newer), or an NVIDIA Jetson Orin |
| OS | Raspberry Pi OS Trixie (64-bit), or JetPack 6 (L4T R36) |
| Camera | A CSI ribbon camera or a USB (UVC) camera |
| Network | The device and your computer on the same network |

These guides use WHEP to play the video. WHEP needs no extra server: pi-webrtc serves the stream itself.

## After the first video

| Next step | Read |
|---|---|
| Watch from anywhere | [MQTT](../signaling/mqtt.md) |
| Let many people watch | [SFU](../signaling/sfu.md) |
| Connect over 4G or 5G | [TURN](../signaling/turn.md) |
| Record video on the device | [Recording](../recording/README.md) |
| Start pi-webrtc when the device boots | [Run as a service](../deployment/run-as-service.md) |
