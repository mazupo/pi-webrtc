# Camera

- [Camera Backends](#camera-backends)
  - [Libcamera](#libcamera)
  - [Libargus](#libargus)
  - [V4L2](#v4l2)
  - [RTSP](#rtsp)

## Camera Backends

`--camera` takes a `<backend>:<id>` string or an `rtsp://` URL. Which backends exist depends on
the platform the binary was built for, which CMake detects from `/etc/nv_tegra_release` or
`/usr/bin/raspi-config`.

| Backend | Value | Raspberry Pi | Jetson | Notes |
|---|---|:--:|:--:|---|
| Libcamera | `libcamera:<id>` | ✅ | ❌ | The officially recommended way to read a CSI camera on a Pi. |
| Libargus | `libargus:<id>` | ❌ | ✅ | NVIDIA's CSI camera stack, with EGL output. |
| V4L2 | `v4l2:<id>` | ✅ | ✅ | USB cameras, legacy CSI drivers, and V4L2 loopback devices. |
| RTSP | `rtsp://...` | ✅ | ✅ | IP cameras and RTSP servers such as MediaMTX. |

Asking for a backend the platform does not have is a startup error, e.g. `libcamera:0` on a
Jetson tells you to use `v4l2:<id>` instead.

### Libcamera

Keep the default `camera_auto_detect=1` in `/boot/firmware/config.txt`.

Libcamera only produces `yuv420` here, so `--v4l2-format` is ignored. Because `yuv420` is
uncompressed, the CSI/USB link — not the sensor — is usually what limits resolution and frame
rate. Each MIPI lane carries 1.5 Gbps on the Pi 5 and 1 Gbps on earlier models
[[ref](https://datasheets.raspberrypi.com/rpi5/raspberry-pi-5-product-brief.pdf)]:

| Interface | 1-lane MIPI (older Pi) | 2-lane MIPI (Pi 4) | 4-lane MIPI (Pi 5) | USB 2.0 | USB 3.0 |
|:--:|:--:|:--:|:--:|:--:|:--:|
| Bandwidth | 1 Gbps | 2 Gbps | 6 Gbps | 0.48 Gbps | 5 Gbps |

YUV 4:2:0 needs 12 bits per pixel, so 4Kp60 is `3840 × 2160 × 60 × 12` = 5.56 Gbps:

| Resolution | 4Kp60 | 4Kp30 | 1080p60 | 1080p30 |
|:--:|:--:|:--:|:--:|:--:|
| Bandwidth | 5.56 Gbps | 2.78 Gbps | 1.39 Gbps | 0.70 Gbps |

### Libargus

The CSI camera path on Jetson. Like libcamera it delivers `yuv420` and ignores
`--v4l2-format`, so the same bandwidth arithmetic applies. Frames arrive as EGL images and
stay on the GPU, which is what lets the encoder — and, in the
[sponsor build](SPONSORS.md#sponsor-benefits), the detector — read them without a copy through
the CPU.

```bash
/path/to/pi-webrtc --camera=libargus:0 --fps=30 --width=1920 --height=1080 ...
```

### V4L2

On a modern Raspberry Pi OS, USB cameras are picked up as V4L2 devices with no changes to
`config.txt`. For the legacy CSI driver, see [QUICK_START.md](QUICK_START.md#csi-camera-through-the-legacy-v4l2-driver).

**1. Find the camera index.** List the V4L2 devices to get the `/dev/videoX` node:

```bash
v4l2-ctl --list-devices
```

**2. Check what it supports.** Query the pixel formats, resolutions, and frame rates, replacing
`X` with the index from step 1:

```bash
v4l2-ctl -d /dev/videoX --list-formats-ext
```

**3. Run with those values.** For a camera on `/dev/video2` doing YUYV 720p60:

```bash
/path/to/pi-webrtc --camera=v4l2:2 --v4l2-format=yuyv --fps=60 --width=1280 --height=720 ...
```

### RTSP

Point `--camera` at an RTSP URL to add WebRTC and DataChannels to an existing IP camera or RTSP pipeline:

```bash
/path/to/pi-webrtc --camera=rtsp://user:pass@192.168.1.10:554/stream1 --hw-accel ...
```

`pi-webrtc` supports H.264, H.265, and MJPEG input. The resolution and frame rate come from the RTSP stream, so `--v4l2-format`, `--width`, `--height`, and `--fps` do not apply. 

The stream is decoded and re-encoded for WebRTC, allowing bitrate/resolution adaptation and keyframe requests.

If the camera allows only one RTSP client, use a [MediaMTX](https://github.com/bluenviron/mediamtx) proxy so multiple processes can consume the stream.

> [!TIP]
> For lower CPU usage, prefer H.264 input and use `--hw-accel` when hardware decoding is available.
