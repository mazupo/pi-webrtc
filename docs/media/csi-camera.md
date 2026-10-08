# CSI Cameras

A CSI camera connects to the board with a ribbon cable. pi-webrtc reads it with libcamera on a Raspberry Pi, and with libargus on a Jetson.

## Raspberry Pi (libcamera)

Use `--camera=libcamera:<id>`. The first camera is `0`. A Pi 5 with two cameras also has `1`.

```bash
./pi-webrtc --camera=libcamera:0 --width=1280 --height=720 --fps=30 ...
```

Keep the default `camera_auto_detect=1` in `/boot/firmware/config.txt`. List the cameras with:

```bash
rpicam-hello --list-cameras
```

The image controls, such as `--awb`, `--ev` and autofocus, work like in `rpicam-apps`. See [Image controls](../reference/configuration.md#image-controls).

## Jetson (libargus)

Use `--camera=libargus:<id>`:

```bash
./pi-webrtc --camera=libargus:0 --width=1920 --height=1080 --fps=30 --hw-accel ...
```

The frames stay in GPU memory. The encoder reads them there, without a copy through the CPU.

## Sub-stream

A CSI camera can also give a second, smaller stream at the same time, on both a Raspberry Pi and a Jetson. For example, you can record at full size and stream a smaller copy:

```bash
./pi-webrtc --camera=libcamera:0 --width=1280 --height=720 \
  --sub-width=640 --sub-height=360 --webrtc-source=sub --record-source=main ...
```

See [Sub-stream](../reference/configuration.md#sub-stream).

## Bandwidth limits

A CSI camera sends uncompressed YUV 4:2:0 frames. Each pixel needs 12 bits. So the link to the camera often limits the size and frame rate, not the sensor.

Each MIPI lane carries 1.5 Gbps on the Pi 5 and 1 Gbps on older models [[ref](https://datasheets.raspberrypi.com/rpi5/raspberry-pi-5-product-brief.pdf)]:

| Interface | 1-lane MIPI (older Pi) | 2-lane MIPI (Pi 4) | 4-lane MIPI (Pi 5) | USB 2.0 | USB 3.0 |
|:--:|:--:|:--:|:--:|:--:|:--:|
| Bandwidth | 1 Gbps | 2 Gbps | 6 Gbps | 0.48 Gbps | 5 Gbps |

For example, 4K at 60 fps needs `3840 × 2160 × 60 × 12` bits per second, which is 5.56 Gbps:

| Resolution | 4K, 60 fps | 4K, 30 fps | 1080p, 60 fps | 1080p, 30 fps |
|:--:|:--:|:--:|:--:|:--:|
| Bandwidth | 5.56 Gbps | 2.78 Gbps | 1.39 Gbps | 0.70 Gbps |
