# USB Cameras

pi-webrtc reads USB (UVC) cameras through V4L2. They work on a Raspberry Pi and on a Jetson, with no extra setup.

## 1. Find the camera

Install the V4L2 tools and list the video devices:

```bash
sudo apt install v4l-utils
v4l2-ctl --list-devices
```

Find your camera and note its first `/dev/videoN` device. For example, `/dev/video2` means `--camera=v4l2:2`.

## 2. Check the formats

List the formats, sizes and frame rates that the camera supports:

```bash
v4l2-ctl -d /dev/video2 --list-formats-ext
```

## 3. Start pi-webrtc

Use a format, size and frame rate from that list. For a camera on `/dev/video2` that sends MJPEG at 1280×720 and 30 fps:

```bash
./pi-webrtc --camera=v4l2:2 --v4l2-format=mjpeg --width=1280 --height=720 --fps=30 ...
```

## Choose a format

| `--v4l2-format` | What the camera sends | When to use it |
|---|---|---|
| `mjpeg` | Compressed JPEG frames | The best choice for most cameras. It uses little USB bandwidth. |
| `h264` | Compressed H.264 | Uses the least USB bandwidth and CPU, if the camera supports it. |
| `yuyv` | Uncompressed frames | Uses a lot of USB bandwidth. Fine for small sizes. |
| `i420` | Uncompressed frames | The default, but few USB cameras support it. |

Always set `--v4l2-format`. Most USB cameras do not support the default, `i420`.

## Other V4L2 devices

V4L2 also covers virtual cameras (V4L2 loopback). See [Virtual cameras](virtual-camera.md).
