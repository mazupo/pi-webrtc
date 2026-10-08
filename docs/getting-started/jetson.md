# NVIDIA Jetson

Set up pi-webrtc on an NVIDIA Jetson and watch the camera in your browser. This guide uses WHEP, so you do not need any server.

## 1. Check your JetPack version

pi-webrtc supports JetPack 6 (L4T R36). Check your L4T version:

```bash
head -n 1 /etc/nv_tegra_release
```

For example, `# R36 (release), REVISION: 5.2` means L4T `36.5.2`.

## 2. Connect the camera

Connect a CSI camera or a USB camera. Then list the video devices:

```bash
sudo apt install v4l-utils
v4l2-ctl --list-devices
```

A CSI camera shows up as `vi-output, <sensor name>`. A USB camera shows up with its own name. Note the first `/dev/videoN` device of a USB camera. For example, `/dev/video0` means `--camera=v4l2:0`.

## 3. Install pi-webrtc

Install the libraries that pi-webrtc needs:

```bash
sudo apt update
sudo apt install libmosquitto1 pulseaudio libyaml-cpp0.7 libboost-program-options1.74.0
```

JetPack already includes NVIDIA's FFmpeg libraries. Do not install Ubuntu's `libavformat58`, because it conflicts with them.

Download the release for your L4T version and unpack it. The file name is `pi-webrtc_jetson-l4t-<version>.tar.gz`. For L4T 36.5.2:

```bash
wget https://github.com/mazupo/pi-webrtc/releases/latest/download/pi-webrtc_jetson-l4t-36.5.2.tar.gz
tar -xzf pi-webrtc_jetson-l4t-36.5.2.tar.gz
```

If your version is different, find the matching file on the [releases page](https://github.com/mazupo/pi-webrtc/releases).

Check that it runs:

```bash
./pi-webrtc -h
```

You should see a list of options.

## 4. Start streaming

**CSI camera.** Jetson reads CSI cameras with NVIDIA's libargus:

```bash
./pi-webrtc --camera=libargus:0 --uid=my-jetson --no-audio --use-whep --hw-accel
```

**USB camera.** Replace `0` with your camera number from step 2:

```bash
./pi-webrtc --camera=v4l2:0 --v4l2-format=mjpeg --uid=my-jetson --no-audio --use-whep --hw-accel
```

Always set `--camera`. Its default is `libcamera:0`, which does not exist on a Jetson.

pi-webrtc now waits for viewers on port `8080`. Leave it running.

## 5. Watch in your browser

1. Find the IP address of the Jetson: `hostname -I`.
2. On a computer in the same network, open [http://app.mazupo.com/whep](http://app.mazupo.com/whep).
3. Enter `http://<jetson-ip>:8080` as the **WHEP URL**, and connect.

Open the player with `http://`, not `https://`. The Jetson answers over plain HTTP, and browsers block that from an `https://` page. If the browser asks to access devices on your local network, click **Allow**.

## 6. Make the video sharper

The default video is 640×480 at 30 fps. Stop pi-webrtc with `Ctrl+C`, and start it with a larger size:

```bash
./pi-webrtc --camera=libargus:0 --width=1920 --height=1080 --fps=60 --uid=my-jetson --no-audio --use-whep --hw-accel
```

`--hw-accel` uses the NVIDIA hardware encoder. The Jetson Orin Nano has no hardware encoder, so pi-webrtc encodes in software there.

If the delay is higher than you expect, see [Performance](../deployment/performance.md).

## Next steps

- [Watch from anywhere with MQTT](../signaling/mqtt.md)
- [Stream from DeepStream or GStreamer over RTSP](../media/rtsp.md)
- [Start pi-webrtc when the Jetson boots](../deployment/run-as-service.md)
- [Learn more about cameras](../media/README.md)
