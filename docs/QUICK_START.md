# Quick Start

Everything needed to get a stream running, from a blank SD card to a picture in the browser.
Check out the [tutorial video](https://youtu.be/g5Npb6DsO-0) for a walkthrough.

- [Requirements](#requirements)
- [MQTT broker](#mqtt-broker)
- [Raspberry Pi setup](#raspberry-pi-setup)
- [Jetson setup](#jetson-setup)
- [Camera setup](#camera-setup)
- [Browser connection](#browser-connection)
- [Common errors](#common-errors)
- [Troubleshooting](#troubleshooting)

## Requirements

| | |
|---|---|
| Device | Raspberry Pi (Zero 2 W and up) or NVIDIA Jetson |
| OS | Raspberry Pi OS **Trixie** 64-bit, or JetPack 6 (L4T R36) |
| Camera | CSI ribbon, or any USB (UVC) camera |
| Signaling | An MQTT broker for remote access — the local WHEP test needs nothing |

Releases are built per OS release, so the package names and the tarball have to match the
system. Check yours with `cat /etc/os-release` on a Pi, or
`head -n 1 /etc/nv_tegra_release` on a Jetson.

## MQTT broker

Signaling is how the device and the browser exchange the SDP and ICE needed to open a WebRTC
connection. With MQTT it works from anywhere, with no public IP and no port forwarding.
Skip this section if you only want the local WHEP test.

Pick one:

- **[HiveMQ Cloud](https://www.hivemq.com)** — free tier, nothing to host. Create a cluster, add a user under *Access Management*, and note the cluster URL.
- **[EMQX](https://www.emqx.com/en)** — free tier as well.
- **[Self-hosted Mosquitto](SETUP_MOSQUITTO.md)** — on your own machine.

Two ports matter, and they are different:

| Who connects | Protocol | HiveMQ Cloud port |
|---|---|---|
| `pi-webrtc` on the device | MQTT over TLS | `8883` |
| The browser | MQTT over WebSocket | `8884` |

Putting `8883` into the browser is the single most common mistake — see
[Common errors](#common-errors).

## Raspberry Pi setup

1. Flash **Raspberry Pi OS Lite (64-bit)** with the
   [Raspberry Pi Imager](https://www.raspberrypi.com/software/), enabling SSH and WiFi in the
   Imager's settings.

2. Install dependencies:
    ```bash
    sudo apt update
    sudo apt install libmosquitto1 pulseaudio libavformat61 libswscale8 libyaml-cpp0.8
    ```

3. Download the latest [release](https://github.com/mazupo/pi-webrtc/releases) and confirm it
   runs:
    ```bash
    wget https://github.com/mazupo/pi-webrtc/releases/latest/download/pi-webrtc_raspios-trixie-arm64.tar.gz
    tar -xzf pi-webrtc_raspios-trixie-arm64.tar.gz
    ./pi-webrtc -h
    ```
    The option list should print. A `cannot execute binary file` or missing-library error
    means the release does not match the OS.

4. Test locally over WHEP, with no broker involved:
    ```bash
    ./pi-webrtc --camera=libcamera:0 --uid=home-pi-5 --no-audio --use-whep
    ```
    Open [app.mazupo.com/whep](http://app.mazupo.com/whep), put `http://<your-pi-ip>:8080`
    in the **WHEP URL** field, and connect. Get the IP with `hostname -I`.

    Load that player over `http`, not `https`. The device answers over plain HTTP, so an
    `https` player page makes the request mixed content, which browsers either block outright
    or gate behind a local-network permission prompt.

5. Once that works, switch to MQTT for remote access:
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
        --hw-accel
    ```
    `--uid` is any name you choose. It namespaces the MQTT topics
    (`{uid}/sdp/...`, `{uid}/ice/...`), so the browser has to use exactly the same value, and
    anyone holding it plus the broker credentials can reach the device.

    The device is ready when the log shows `MQTT connected to broker ...` followed by
    `MQTT service is ready.`, then goes quiet waiting for a viewer. Continue to
    [Browser connection](#browser-connection).

To start it automatically on boot, see
[Running as a Linux Service](ADVANCED.md#running-as-a-linux-service).

## Jetson setup

Tested on JetPack 6 (L4T R36).

1. Install dependencies:
    ```bash
    sudo apt update
    sudo apt install libmosquitto1 pulseaudio libyaml-cpp0.7 libboost-program-options1.74.0
    ```
    JetPack already ships NVIDIA's own `ffmpeg` package, which bundles the `libavformat` and
    `libswscale` shared libraries, so those are not listed here. Do not install Ubuntu's
    `libavformat58` / `libswscale5` on top — they claim the same file paths as NVIDIA's build.

2. Check your L4T version, then download the matching `pi-webrtc_jetson-l4t-<version>.tar.gz`
   from the [releases](https://github.com/mazupo/pi-webrtc/releases):
    ```bash
    head -n 1 /etc/nv_tegra_release   # e.g. "# R36 (release), REVISION: 5.2" → 36.5.2
    tar -xzf pi-webrtc_jetson-l4t-<version>.tar.gz
    ./pi-webrtc -h
    ```

3. Run with a CSI camera through `libargus`. There is no libcamera backend on Jetson; USB
   cameras use `--camera=v4l2:<id>`.
    ```bash
    ./pi-webrtc \
        --camera=libargus:0 \
        --fps=60 \
        --width=1280 \
        --height=720 \
        --use-mqtt \
        --mqtt-host=example.s1.eu.hivemq.cloud \
        --mqtt-port=8883 \
        --mqtt-username=your-username \
        --mqtt-password=your-password \
        --uid=jetson-1 \
        --no-audio \
        --hw-accel
    ```

> [!TIP]
> If latency is higher than expected, see
> [Unthrottling the VIC and NVENC clocks](ADVANCED.md#jetson-unthrottling-the-vic-and-nvenc-clocks).

## Camera setup

`--camera` takes a `<backend>:<id>` string. Asking for a backend the platform does not have is
a startup error.

| Camera | Raspberry Pi | Jetson |
|---|---|---|
| CSI | `libcamera:<id>` | `libargus:<id>` |
| USB | `v4l2:<id>` | `v4l2:<id>` |

### CSI camera on a Raspberry Pi

Keep the default `camera_auto_detect=1` in `/boot/firmware/config.txt`. The `<id>` is the
camera's position, so the only attached camera is `libcamera:0`, and a second one on a Pi 5 is
`libcamera:1`.

libcamera only produces `yuv420`, so `--v4l2-format` is ignored. Resolution and frame rate are
usually limited by the MIPI link rather than the sensor — see
[Camera and Encoding](CAMERA_AND_ENCODING.md#libcamera).

### USB camera

On a modern Raspberry Pi OS and on JetPack, USB cameras appear as V4L2 devices with no
configuration. Find the `/dev/videoX` node and check what it supports:

```bash
v4l2-ctl --list-devices
v4l2-ctl -d /dev/videoX --list-formats-ext
```

Then pass the index and one of the listed formats (`i420`, `yuyv`, `mjpeg`, or `h264`). For a
camera on `/dev/video2` doing YUYV 720p60:

```bash
./pi-webrtc --camera=v4l2:2 --v4l2-format=yuyv --fps=60 --width=1280 --height=720 ...
```

Picking `mjpeg` or `h264` where the camera offers it moves the compression onto the camera and
saves both USB bandwidth and CPU.

### CSI camera through the legacy V4L2 driver

**You probably do not need this.** It is only for driving a **CSI** camera through the legacy
driver instead of libcamera, and it turns USB auto-detection off.

1. Edit `/boot/firmware/config.txt`:

    ```ini
    # camera_auto_detect=1  # Default setting
    camera_auto_detect=0    # Turn off default libcamera
    ```

2. Run with `--camera=v4l2:0` for the camera at `/dev/video0`:

    ```bash
    ./pi-webrtc --camera=v4l2:0 \
        --v4l2-format=mjpeg \
        --fps=60 \
        --width=1280 \
        --height=720 \
        --uid=home-pi-5 \
        --no-audio \
        --hw-accel
    ```

> [!CAUTION]
> At 1920x1080 with the legacy V4L2 driver, the hardware decoder firmware may round up to
> 1920x1088 while the ISP/encoder stays at 1920x1080 on the 6.6.31 kernel, which can cause
> memory out-of-range issues. Setting 1920x1088 avoids it.

## Browser connection

Open the [demo web](https://app.mazupo.com) and fill in:

| Field | Value |
|---|---|
| Host | The broker hostname, the same one given to `--mqtt-host` |
| Port | The broker's **WebSocket** port (HiveMQ Cloud: `8884`), *not* `8883` |
| Username / Password | The same broker credentials |
| uid | Exactly the string passed to `--uid` |

Several browsers can connect to one device at the same time; each generates its own client id.

Other clients: [client-sdk-js](https://github.com/mazupo/client-sdk-js) to build your own,
[picamera-app](https://github.com/TzuHuanTai/picamera-app) on Android, or
[Home Assistant](ADVANCED.md#using-the-webrtc-camera-in-home-assistant) over WHEP.

## Common errors

| What you see | Cause |
|---|---|
| `cannot execute binary file` or a missing `.so` | The release does not match the OS. Re-check `cat /etc/os-release` and download the matching tarball. |
| `E: Unable to locate package libavformat61` | Not on Trixie. The version-suffixed package names differ per OS release. |
| `--uid is required.` | `--uid` is mandatory in every mode, including WHEP. |
| `No signaling service is running.` | No transport was enabled. Pass at least one of `--use-whep`, `--use-mqtt`, `--use-livekit`, or `--use-cloudflare`. |
| `MQTT connect failed: ...` | Wrong host, port, or credentials. The device uses the TLS port (`8883`), not the WebSocket one. |
| Device log is fine, browser shows nothing | Almost always the browser using `8883` instead of the WebSocket port, or a `uid` mismatch. Both sides must match exactly. |
| WHEP test plays nothing | The player page was loaded over `https` while the device answers over `http`. Reload the player as `http://app.mazupo.com/whep`. If the browser forces HTTPS anyway, Chrome will ask for permission to reach devices on the local network — click **Allow**. |
| Requested `libcamera:0` on a Jetson | There is no libcamera backend there. Use `libargus:<id>` for CSI or `v4l2:<id>` for USB. |
| `Hardware encoder/scaler not found; falling back to software` | Expected on a Pi 5, which has no hardware H.264 encoder. Harmless. |

## Troubleshooting

**See every available option.** The full flag list, with defaults, comes from the binary
itself:

```bash
./pi-webrtc -h
```

**List the USB cameras** the system can see, independently of `pi-webrtc`:

```bash
v4l2-ctl --list-devices
```

**Confirm the WHEP port is reachable** from another machine on the LAN. Anything other than a
connection refusal means the device side is serving:

```bash
curl -v http://<your-pi-ip>:8080
```

**Check the broker independently** of `pi-webrtc`, to separate credential problems from
everything else:

```bash
mosquitto_sub -h example.s1.eu.hivemq.cloud -p 8883 --capath /etc/ssl/certs/ \
  -u your-username -P your-password -t 'home-pi-5/#' -v
```

**Reduce the problem.** Drop to `--width=640 --height=480 --fps=30` and add `--no-audio`. If a
smaller stream connects, the issue is bandwidth or encoding rather than signaling.

**Still stuck?** Open an [issue](https://github.com/mazupo/pi-webrtc/issues) with the full
command, the log output, and the device and OS versions.

## Next

- [Configuration](CONFIGURATION.md) — every flag and the YAML config file
- [Camera and Encoding](CAMERA_AND_ENCODING.md) — backends, formats, and bandwidth
- [Signaling](SIGNALING.md) — MQTT, WHEP, and SFU compared
- [Recording](RECORDING.md) — MP4 files and snapshots
- [Advanced Usage](ADVANCED.md) — SFU, two-way audio, DataChannels, running as a service
