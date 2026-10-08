# Video & Audio

pi-webrtc reads video from a camera or a stream, encodes it, and sends it over WebRTC. This section shows which video sources you can use, how encoding works, and how to add audio.

## Pages

- [CSI cameras](csi-camera.md): ribbon cameras on a Raspberry Pi (libcamera) or a Jetson (libargus).
- [USB cameras](usb-camera.md): any UVC camera, through V4L2.
- [RTSP cameras](rtsp.md): IP cameras, MediaMTX, GStreamer and DeepStream.
- [Virtual cameras](virtual-camera.md): process frames with Python or AI first, then stream them.
- [Encoding](encoding.md): hardware and software encoding.
- [Two-way audio](audio.md): a microphone and a speaker on the device.

## Video sources

`--camera` selects the video source:

| Source | `--camera` | Raspberry Pi | Jetson |
|---|---|:--:|:--:|
| CSI camera | `libcamera:<id>` | ✅ | ❌ |
| CSI camera | `libargus:<id>` | ❌ | ✅ |
| USB camera or other V4L2 device | `v4l2:<id>` | ✅ | ✅ |
| RTSP stream | `rtsp://...` | ✅ | ✅ |

If you ask for a source that the device does not have, pi-webrtc stops with an error. For example, `libcamera:0` does not work on a Jetson.
