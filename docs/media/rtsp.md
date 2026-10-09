# RTSP Cameras

Stream an IP camera, or any RTSP stream, through pi-webrtc. Run pi-webrtc on a Raspberry Pi or a Jetson near the camera, and point `--camera` at an `rtsp://` URL.

```mermaid
graph LR
    A[IP camera] -->|RTSP| P[pi-webrtc]
    B[IP camera] -->|RTSP| M[MediaMTX] -->|RTSP| Q[pi-webrtc]
    C[GStreamer or DeepStream] -->|RTSP| N[MediaMTX] -->|RTSP| R[pi-webrtc]
```

## Why use pi-webrtc for an RTSP camera

MediaMTX can also send RTSP to a browser over WebRTC. It sends the camera's stream as it is. pi-webrtc adds three things:

- **Adaptive video.** pi-webrtc decodes and encodes the video again. So it can lower the bitrate and the resolution to fit each viewer and each network. This saves upload data on 4G, 5G and cloud links.
- **Two-way data.** Send commands and sensor data both ways over [DataChannels](../reference/datachannels.md).
- **Remote access.** Reach the camera from anywhere over [MQTT](../signaling/mqtt.md), or send it to many viewers through an [SFU](../signaling/sfu.md).

The cost is one more decode and encode on the device. If you need none of the three, MediaMTX's own WebRTC output is enough.

## 1. Check the RTSP URL

Use `ffprobe` (from the `ffmpeg` package) to check that the URL works:

```bash
ffprobe -rtsp_transport tcp rtsp://user:pass@192.168.1.10:554/stream1
```

It should list a `Video:` stream with `h264`, `hevc` or `mjpeg`. If the password has special characters, such as `@` or `#`, URL-encode them. For example, `@` becomes `%40`.

## 2. Start pi-webrtc

```bash
./pi-webrtc --camera=rtsp://user:pass@192.168.1.10:554/stream1 --hw-accel --uid=front-door --no-audio --use-whep
```

Then watch it like any other camera. See [Getting Started](../getting-started/README.md#choose-your-device) for the player. `rtsps://` URLs work too.

## What pi-webrtc does with the stream

- It reads the stream over TCP.
- It uses the video only. It ignores the audio of the RTSP stream. Use `--no-audio`, or a microphone on the device (see [Two-way audio](audio.md)).
- The size and the frame rate come from the stream. `--width`, `--height` and `--fps` have no effect.
- It waits for the stream when it starts, and it reconnects when the stream drops. It tries again every second.
- The first frame shows after the first keyframe.
- If the stream comes back with a different size or codec, pi-webrtc exits. Run it as a [service](../deployment/run-as-service.md) with `Restart=always`, so it starts again.
- RTSP input has no [sub-stream](../reference/configuration.md#sub-stream).

## Decoding

| Device | Hardware decoding (`--hw-accel`) | Software decoding |
| --- | --- | --- |
| Raspberry Pi 3 / 4 / Zero 2 | H.264 and MJPEG, up to 1920×1088 | H.265, and larger streams |
| Raspberry Pi 5 | None | All formats |
| NVIDIA Jetson | H.264, H.265 and MJPEG | Only when the hardware decoder is not available |

A Pi 5 decodes 1080p H.264 in about 6 ms per frame on one CPU core.

## Camera settings

- On a Pi 3, Pi 4 or Zero 2, use H.264. The hardware there does not decode H.265.
- Set the keyframe interval (GOP, or "I-frame interval") to 1 or 2 seconds. Viewers see the first frame only after a keyframe. If the camera has a separate IDR interval, set it the same. A Jetson decoder can start only from an IDR frame.
- If you only need a small picture, use the camera's second, smaller stream. It is less work to decode.

## Put MediaMTX in between

Many cameras allow only one or two RTSP clients. [MediaMTX](https://github.com/bluenviron/mediamtx) reads the camera once, and lets pi-webrtc, a recorder and other programs all read from it.

1. Download MediaMTX for arm64, and unpack it. Check the [releases page](https://github.com/bluenviron/mediamtx/releases) for a newer version:

    ```bash
    wget https://github.com/bluenviron/mediamtx/releases/download/v1.21.1/mediamtx_v1.21.1_linux_arm64.tar.gz
    mkdir -p mediamtx
    tar -xzf mediamtx_v1.21.1_linux_arm64.tar.gz -C mediamtx
    ```

2. Open `mediamtx/mediamtx.yml`. Find `paths:` near the end, and add your camera below it:

    ```yaml
    paths:
      cam1:
        source: rtsp://user:pass@192.168.1.10:554/stream1
    ```

3. Start MediaMTX:

    ```bash
    cd mediamtx
    ./mediamtx
    ```

4. In a second terminal, start pi-webrtc with the MediaMTX URL:

    ```bash
    ./pi-webrtc --camera=rtsp://127.0.0.1:8554/cam1 --hw-accel --uid=cam1 --no-audio --use-whep
    ```

## Test with a test pattern

With MediaMTX running, publish a test pattern with `ffmpeg`:

```bash
ffmpeg -re -f lavfi -i testsrc2=size=1280x720:rate=30 \
    -c:v libx264 -preset ultrafast -tune zerolatency -g 30 -pix_fmt yuv420p \
    -f rtsp rtsp://127.0.0.1:8554/test
```

Then read it with pi-webrtc:

```bash
./pi-webrtc --camera=rtsp://127.0.0.1:8554/test --uid=test --no-audio --use-whep
```

## Jetson: GStreamer and DeepStream

Install the RTSP plugins once. `gstreamer1.0-rtsp` gives you `rtspclientsink`, and `deepstream-app` needs `libgstrtspserver-1.0-0`:

```bash
sudo apt install gstreamer1.0-rtsp libgstrtspserver-1.0-0
```

The commands below publish to MediaMTX, so start MediaMTX first.

> [!IMPORTANT]
> If you connect over SSH with X11 forwarding (`ssh -X`, or MobaXterm), run `unset DISPLAY` first. Otherwise the camera fails with `Failed to create FrameConsumer`.

### A CSI camera with the hardware encoder

```bash
gst-launch-1.0 -e nvarguscamerasrc sensor-id=0 \
    ! "video/x-raw(memory:NVMM),width=1920,height=1080,framerate=30/1" \
    ! nvv4l2h264enc bitrate=8000000 iframeinterval=30 idrinterval=30 insert-sps-pps=true \
    ! h264parse \
    ! rtspclientsink location=rtsp://127.0.0.1:8554/cam protocols=tcp
```

Then:

```bash
./pi-webrtc --camera=rtsp://127.0.0.1:8554/cam --hw-accel --uid=jetson-cam --no-audio --use-whep
```

Without `gstreamer1.0-rtsp`, you can pipe the H.264 stream into `ffmpeg` instead:

```bash
gst-launch-1.0 -q nvarguscamerasrc sensor-id=0 \
    ! "video/x-raw(memory:NVMM),width=1920,height=1080,framerate=30/1" \
    ! nvv4l2h264enc bitrate=8000000 iframeinterval=30 idrinterval=30 insert-sps-pps=true \
    ! h264parse ! video/x-h264,stream-format=byte-stream ! fdsink fd=1 \
  | ffmpeg -hide_banner -loglevel error -f h264 -framerate 30 -i - \
    -c copy -f rtsp -rtsp_transport tcp rtsp://127.0.0.1:8554/cam
```

### DeepStream with object detection

This pipeline runs NVIDIA's sample detector (cars, people, bicycles and road signs), and draws the boxes on the video:

```bash
gst-launch-1.0 -e \
    nvarguscamerasrc sensor-id=0 \
    ! "video/x-raw(memory:NVMM),width=1920,height=1080,framerate=30/1" \
    ! m.sink_0 nvstreammux name=m batch-size=1 width=1920 height=1080 live-source=1 \
    ! nvinfer config-file-path=/opt/nvidia/deepstream/deepstream/samples/configs/deepstream-app/config_infer_primary.txt batch-size=1 \
    ! nvvideoconvert ! nvdsosd \
    ! nvvideoconvert ! "video/x-raw(memory:NVMM),format=NV12" \
    ! nvv4l2h264enc bitrate=8000000 iframeinterval=30 idrinterval=30 insert-sps-pps=true \
    ! h264parse \
    ! rtspclientsink location=rtsp://127.0.0.1:8554/ds protocols=tcp
```

The first start builds a TensorRT engine. This takes about 2 minutes on an Orin NX. DeepStream saves the engine next to the model, under `/opt/nvidia/deepstream`. If your user cannot write there, it builds the engine again at every start.

Then:

```bash
./pi-webrtc --camera=rtsp://127.0.0.1:8554/ds --hw-accel --uid=jetson-ds --no-audio --use-whep
```

### deepstream-app

`deepstream-app` has its own RTSP output, so you do not need MediaMTX. The sample configs turn this output off, so copy a sample and change it.

1. Copy the sample configs and models to your home folder. DeepStream can then save the TensorRT engine next to the model, so only the first start is slow:

    ```bash
    mkdir -p ~/deepstream
    cp -r /opt/nvidia/deepstream/deepstream/samples/configs /opt/nvidia/deepstream/deepstream/samples/models ~/deepstream/
    cd ~/deepstream/configs/deepstream-app
    ```

2. In the CSI camera sample, turn off the display output (`[sink0]`) and turn on the RTSP output (`[sink2]`):

    ```bash
    sed -i -e '/^\[sink0\]/,/^\[/ s/^enable=1/enable=0/' \
           -e '/^\[sink2\]/,/^\[/ s/^enable=0/enable=1/' source1_csi_dec_infer_resnet_int8.txt
    ```

3. Stop MediaMTX if it is running. DeepStream's RTSP output also uses port `8554`. Then start deepstream-app. Run `unset DISPLAY` first. Over SSH with X11 forwarding, for example in MobaXterm, the camera fails without it:

    ```bash
    unset DISPLAY
    deepstream-app -c source1_csi_dec_infer_resnet_int8.txt
    ```

    It prints `Launched RTSP Streaming at rtsp://localhost:8554/ds-test`. The first start then builds the TensorRT engine, which takes about 2 minutes. The RTSP output does not answer during this time. Wait until `**PERF:` lines show the frame rate. Later starts take a few seconds.

    If deepstream-app stops with `Could not get EGL display connection` and `App run failed`, `DISPLAY` was still set.

4. In a second terminal, read the stream with pi-webrtc:

    ```bash
    ./pi-webrtc --camera=rtsp://127.0.0.1:8554/ds-test --hw-accel --uid=jetson-ds --no-audio --use-whep
    ```

If you start pi-webrtc before the `**PERF:` lines, it logs `Could not open rtsp://...; retrying every second.` Leave it running. It connects when the stream starts. The first picture can then take about 5 seconds, because pi-webrtc waits for a keyframe.

The samples whose names start with `source30` or `source4` read video files from `samples/streams`. To use them, copy that folder to `~/deepstream` too.

> [!TIP]
> On a Jetson, the [sponsor build](../sponsors.md#sponsor-benefits) runs detection and tracking inside pi-webrtc. Then the video is encoded only once.

## Log messages

| Log | What it means |
| --- | --- |
| `Could not open rtsp://...: ...; retrying every second.` | pi-webrtc cannot reach the stream yet. Check the URL, the user, the password and the network. |
| `Lost the RTSP stream (...); reconnecting.` | The stream stopped. pi-webrtc keeps trying. |
| `The RTSP stream changed from ... to ...; restart to follow it.` | The size or the codec changed. pi-webrtc exits. Let systemd start it again. |
| `The RTSP stream uses a codec other than H264, H265 or MJPEG.` | Change the codec in the camera settings. |
| `No V4L2 hardware decoder at /dev/video10.` | Normal on a Pi 5. It decodes in software. |
| `The H264 decoder has not found a frame to start from yet; it waits for the next IDR frame.` | On a Jetson. The stream has few IDR frames, so the first picture comes late. pi-webrtc keeps waiting. Set the IDR interval to 1 or 2 seconds. |
