# Quick Start

Check out the [tutorial video](https://youtu.be/g5Npb6DsO-0) or follow the steps below.

## Before you start

You need an MQTT broker for signaling: [HiveMQ](https://www.hivemq.com),
[EMQX](https://www.emqx.com/en), or a [self-hosted](SETUP_MOSQUITTO.md) one.
Other signaling options are in [SIGNALING.md](SIGNALING.md).

## Raspberry Pi

1. Flash **Raspberry Pi OS Lite** with [Raspberry Pi Imager](https://www.raspberrypi.com/software/).

2. Install dependencies:
    ```bash
    sudo apt update
    sudo apt install libmosquitto1 pulseaudio libavformat61 libswscale8 libyaml-cpp0.8
    ```

3. Download the latest [release](https://github.com/mazupo/pi-webrtc/releases):
    ```bash
    wget https://github.com/mazupo/pi-webrtc/releases/latest/download/pi-webrtc_raspios-trixie-arm64.tar.gz
    tar -xzf pi-webrtc_raspios-trixie-arm64.tar.gz
    ```

4. Run:
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
        --hw-accel
    ```

5. Open the [demo web](https://app.mazupo.com), enter the same MQTT settings and `uid`, and connect.

## NVIDIA Jetson

Tested on JetPack 6 (L4T R36).

1. Install dependencies:
    ```bash
    sudo apt update
    sudo apt install libmosquitto1 pulseaudio libavformat58 libswscale5 libyaml-cpp0.7 libboost-program-options1.74.0
    ```

2. Check your L4T version, then download the matching `pi-webrtc_jetson-l4t-<version>.tar.gz`
   from the [releases](https://github.com/mazupo/pi-webrtc/releases):
    ```bash
    head -n 1 /etc/nv_tegra_release   # e.g. "# R36 (release), REVISION: 5.2" → 36.5.2
    tar -xzf pi-webrtc_jetson-l4t-<version>.tar.gz
    ```

3. Run with a CSI camera through `libargus` (use `v4l2:<id>` for USB cameras):
    ```bash
    ./pi-webrtc \
        --camera=libargus:0 \
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
        --hw-accel
    ```

4. Open the [demo web](https://app.mazupo.com) and connect as above.

> [!TIP]
> If latency is higher than expected, see
> [Unthrottling the VIC and NVENC clocks](ADVANCED.md#jetson-unthrottling-the-vic-and-nvenc-clocks).

## Next steps

- [Configuration](CONFIGURATION.md) — all flags and YAML config
- [Camera and Encoding](CAMERA_AND_ENCODING.md)
- [Signaling](SIGNALING.md)
- [Recording](RECORDING.md)
