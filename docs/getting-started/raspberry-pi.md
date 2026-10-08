# Raspberry Pi

Set up pi-webrtc on a Raspberry Pi and watch the camera in your browser. This guide uses WHEP, so you do not need any server.

## 1. Install Raspberry Pi OS

Use [Raspberry Pi Imager](https://www.raspberrypi.com/software/) to write **Raspberry Pi OS Lite (64-bit)** to the SD card. In the Imager settings, turn on SSH and set up Wi-Fi.

Start the Pi and log in over SSH. Check that the OS is Trixie:

```bash
grep VERSION_CODENAME /etc/os-release
```

It should print `VERSION_CODENAME=trixie`.

## 2. Connect the camera

Connect a CSI camera or a USB camera. You do not need to change `/boot/firmware/config.txt`.

**CSI camera.** Check that the Pi can see it:

```bash
rpicam-hello --list-cameras
```

**USB camera.** Install the V4L2 tools and list the cameras:

```bash
sudo apt install v4l-utils
v4l2-ctl --list-devices
```

Find your camera in the list and note its first `/dev/videoN` device. For example, `/dev/video0` means `--camera=v4l2:0`.

## 3. Install pi-webrtc

Install the libraries that pi-webrtc needs:

```bash
sudo apt update
sudo apt install libmosquitto1 pulseaudio libavformat61 libyaml-cpp0.8
```

Download the latest release and unpack it:

```bash
wget https://github.com/mazupo/pi-webrtc/releases/latest/download/pi-webrtc_raspios-trixie-arm64.tar.gz
tar -xzf pi-webrtc_raspios-trixie-arm64.tar.gz
```

Check that it runs:

```bash
./pi-webrtc -h
```

You should see a list of options. If you see `cannot execute binary file` or a missing library, see [Troubleshooting](troubleshooting.md).

## 4. Start streaming

**CSI camera:**

```bash
./pi-webrtc --camera=libcamera:0 --uid=my-pi --no-audio --use-whep
```

**USB camera.** Replace `0` with your camera number from step 2:

```bash
./pi-webrtc --camera=v4l2:0 --v4l2-format=mjpeg --uid=my-pi --no-audio --use-whep
```

Most USB cameras send `mjpeg`. To see the formats your camera supports, run `v4l2-ctl -d /dev/video0 --list-formats-ext`.

pi-webrtc now waits for viewers on port `8080`. Leave it running.

## 5. Watch in your browser

1. Find the IP address of the Pi: `hostname -I`.
2. On a computer in the same network, open [http://app.mazupo.com/whep](http://app.mazupo.com/whep).
3. Enter `http://<pi-ip>:8080` as the **WHEP URL**, and connect.

Open the player with `http://`, not `https://`. The Pi answers over plain HTTP, and browsers block that from an `https://` page. If the browser asks to access devices on your local network, click **Allow**.

## 6. Make the video sharper

The default video is 640×480 at 30 fps. Stop pi-webrtc with `Ctrl+C`, and start it with a larger size:

```bash
./pi-webrtc --camera=libcamera:0 --width=1280 --height=720 --fps=60 --hw-accel --uid=my-pi --no-audio --use-whep
```

`--hw-accel` uses the hardware video encoder of the Pi 3, Pi 4 and Zero 2. The Pi 5 has no hardware encoder, so pi-webrtc encodes in software there. You can use the flag on every model.

## Next steps

- [Watch from anywhere with MQTT](../signaling/mqtt.md)
- [Let many people watch through an SFU](../signaling/sfu.md)
- [Start pi-webrtc when the Pi boots](../deployment/run-as-service.md)
- [Learn more about cameras](../media/README.md)
