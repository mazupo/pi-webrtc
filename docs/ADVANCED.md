# Advanced Usage

- [Broadcasting a Live Stream to Many Viewers via SFU](#broadcasting-a-live-stream-to-many-viewers-via-sfu)
- [Using the Legacy V4L2 Driver](#using-the-legacy-v4l2-driver)
- [Running as a Linux Service](#running-as-a-linux-service)
- [Two-way Audio Communication](#two-way-audio-communication)
- [DataChannels](#datachannels)
- [Two-way DataChannel Messaging](#two-way-datachannel-messaging)
- [Gamepad Input](#gamepad-input)
- [Stream AI or Any Custom Feed to a Virtual Camera](#stream-ai-or-any-custom-feed-to-a-virtual-camera)
- [WHEP with webrtc-player](#whep-with-webrtc-player)
- [Using the WebRTC Camera in Home Assistant](#using-the-webrtc-camera-in-home-assistant)
- [Jetson: Unthrottling the VIC and NVENC Clocks](#jetson-unthrottling-the-vic-and-nvenc-clocks)
- [Useful Commands](#useful-commands)

# Broadcasting a Live Stream to Many Viewers via SFU

![SFU Cloud Service](https://github.com/user-attachments/assets/e41bcd7b-7c84-4837-88c2-820b20c094d4)

An SFU lets one device stream to many viewers without sending a separate stream to each viewer. The SFU receives the stream from the device and forwards it to the viewers.

Two SFU backends are supported:
- [LiveKit](#livekit): self-hosted or hosted.
- [Cloudflare Realtime](#cloudflare-realtime): managed by Cloudflare.

Both have a free endpoint below for testing.

## LiveKit

### Free Testing Server

| URL | API Key |
| --- | --- |
| `wss://api.mazupo.com` | `APIWnQTs4tmUZvA` |

⚠️ Shared testing server: Limited to 100 concurrent connections, 5,000 minutes, and 50 GB of transfer per month across all users. For a dedicated environment, see [COMMERCIAL.md](COMMERCIAL.md#contact).

### 1. Run on the device

```bash
/path/to/pi-webrtc --camera=libcamera:0 \
    --fps=60 \
    --width=1920 \
    --height=1080 \
    --uid=your-display-name \
    --use-livekit \
    --livekit-url=wss://api.mazupo.com \
    --livekit-key=APIWnQTs4tmUZvA \
    --livekit-room=the-room-name
```

The device connects to LiveKit using `--livekit-url`, `--livekit-key`, `--livekit-room`, and `--uid`.

- `--livekit-url` specifies the SFU address. Use `wss://` for TLS or `ws://` for non-TLS connections.
- `--livekit-key` is the LiveKit API key.
- `--livekit-room` specifies the room to publish to.
- `--uid` is the device's identity in the room.
- [`--livekit-secret`*](COMMERCIAL.md#direct-livekit-connection) lets the device generate a LiveKit access token locally, so no separate token server is required.

Anyone who joins the same room can watch the stream.

With `--enable-ipc`, DataChannel messages are also broadcast to all participants in the room.

### 2. Join the room

- See the [example](https://github.com/mazupo/client-sdk-js/blob/main/docs/EXAMPLES.md#play-through-the-livekit-sfu) in [client-sdk-js](https://github.com/mazupo/client-sdk-js)
- Try it on the Web demo: [https://app.mazupo.com/room](https://app.mazupo.com/room)

## Cloudflare Realtime

### Free Testing Server

| URL | Device API Key | Viewer API Key |
| --- | --- | --- |
| `https://api.mazupo.com` | `81f899b8fab5692b0faa76c3372b7ae6` | `ec0478c67e729b6f429eda1e97829af0` |

⚠️ Shared demo environment: The free Cloudflare quota is shared by all users and stops when the monthly limit is reached. Use a unique `--uid` to avoid conflicts with other users. For a dedicated environment, see [COMMERCIAL.md](COMMERCIAL.md#contact).

### 1. Run on the device

```bash
/path/to/pi-webrtc --camera=libcamera:0 \
    --fps=60 \
    --width=1920 \
    --height=1080 \
    --uid=your-display-name \
    --use-cloudflare \
    --api-url=https://api.mazupo.com \
    --api-key=81f899b8fab5692b0faa76c3372b7ae6
```
The device connects to Cloudflare Realtime through the Mazupo API. Cloudflare generates a new `sessionId` each time the device reconnects, and the backend maps it to the device's `uid`.

- `--api-url` specifies the backend address.
- `--api-key` authenticates the device with the backend.
- `--uid` identifies the device.

Anyone who knows the device's uid can find its stream.

DataChannel/IPC traffic is not supported yet. But `--enable-ipc` still applies to the other signaling services running alongside it.

### 2. Watch streams

- See the [example](https://github.com/mazupo/client-sdk-js/blob/main/docs/EXAMPLES.md#pull-from-the-cloudflare-realtime-sfu) in [client-sdk-js](https://github.com/mazupo/client-sdk-js)
- Try it on the Web demo: [https://app.mazupo.com/cloudflare](https://app.mazupo.com/cloudflare). Enter the URL and the **Viewer API Key** under **Settings → Network**, add a device with the same `uid`, then select your device. The viewer only needs the device's `uid`. 

# Using the Legacy V4L2 Driver

**You probably do not need this.** On Raspberry Pi OS Trixie and later, leaving the default
`camera_auto_detect=1` in place is enough for a USB camera to show up as a V4L2 device, so
`--camera=v4l2:0` works with no configuration at all — see
[Camera and Encoding](CAMERA_AND_ENCODING.md#v4l2).

The steps below are only for driving a **CSI** camera through the legacy driver instead of
libcamera, and they turn that auto-detection off.

1. Edit `/boot/firmware/config.txt`:

    ```ini
    # camera_auto_detect=1  # Default setting
    camera_auto_detect=0    # Turn off default libcamera
    ```

2. Run with `--camera=v4l2:0` for the camera at `/dev/video0`:

    ```bash
    ./pi-webrtc --camera=v4l2:0 \
        --uid=your-custom-uid \
        --v4l2-format=mjpeg \
        --fps=60 \
        --width=1280 \
        --height=720 \
        --hw-accel \
        --no-audio \
        --use-mqtt \
        --mqtt-host=your.mqtt.cloud \
        --mqtt-port=8883 \
        --mqtt-username=hakunamatata \
        --mqtt-password=Wonderful
    ```

> [!CAUTION]
> At 1920x1080 with the legacy V4L2 driver, the hardware decoder firmware may round up to
> 1920x1088 while the ISP/encoder stays at 1920x1080 on the 6.6.31 kernel, which can cause
> memory out-of-range issues. Setting 1920x1088 avoids it.

# Running as a Linux Service

## 1. Set up `pulseaudio` as a system-wide daemon

Skip this step if you run with the `--no-audio` flag.
[[reference]](https://www.freedesktop.org/wiki/Software/PulseAudio/Documentation/User/SystemWide/)

* Install it:
    ```bash
    sudo apt install pulseaudio
    ```
* Create `/etc/systemd/system/pulseaudio.service`:
    ```ini
    [Unit]
    Description= Pulseaudio Daemon
    After=rtkit-daemon.service systemd-udevd.service dbus.service

    [Service]
    Type=simple
    ExecStart=/usr/bin/pulseaudio --system --disallow-exit --disallow-module-loading
    Restart=always
    RestartSec=10

    [Install]
    WantedBy=multi-user.target
    ```
* Stop the client from autospawning its own copy:
    ```bash
    echo 'autospawn = no' | sudo tee -a /etc/pulse/client.conf > /dev/null
    ```
* Grant access, enable, and reboot:
    ```bash
    sudo adduser root pulse-access
    sudo systemctl enable pulseaudio.service
    sudo reboot
    ```

## 2. Run `pi-webrtc` on boot

* Create `/etc/systemd/system/pi-webrtc.service`, adjusting `WorkingDirectory` and `ExecStart`:
    ```ini
    [Unit]
    Description= The p2p camera via webrtc.
    After=network-online.target pulseaudio.service

    [Service]
    Type=simple
    WorkingDirectory=/path/to
    ExecStart=/path/to/pi-webrtc --camera=libcamera:0 --fps=60 --width=1280 --height=720 --uid=your-uid --hw-accel --use-mqtt --mqtt-host=example.s1.eu.hivemq.cloud --mqtt-port=8883 --mqtt-username=hakunamatata --mqtt-password=wonderful
    Restart=always
    RestartSec=10

    [Install]
    WantedBy=multi-user.target
    ```

    > [!TIP]
    > A long `ExecStart` is a good reason to use a
    > [config file](CONFIGURATION.md#config-file) instead:
    > `ExecStart=/path/to/pi-webrtc --config=/path/to/config.yml`

* Enable and start it:
    ```bash
    sudo systemctl daemon-reload
    sudo systemctl enable pi-webrtc.service
    sudo systemctl start pi-webrtc.service
    ```

# Two-way Audio Communication

Remove the `--no-audio` flag to enable two-way audio. If PulseAudio is available, run [`pulseaudio`](#1-run-pulseaudio-as-a-system-wide-daemon) in the background. On systems without PulseAudio, use `--force-alsa`.

Two-way audio works for peer-to-peer connections. Audio sent through an SFU is one-way, from the device to the viewers. Remote audio plays on the default output device. With `--force-alsa`, make sure the ALSA `default` device points to your speaker.

The device needs a microphone and speaker. USB audio devices are the easiest option. For GPIO/I2S devices, see:

- **Microphone** — [wiring and testing an I2S MEMS mic](https://learn.adafruit.com/adafruit-i2s-mems-microphone-breakout/raspberry-pi-wiring-test)
- **Speaker** — [wiring a MAX98357 I2S amp](https://learn.adafruit.com/adafruit-max98357-i2s-class-d-mono-amp/raspberry-pi-wiring)

# DataChannels

`pi-webrtc` uses DataChannels for commands, data transfer, and optional IPC:

| Label | Ordered | Reliability | Carries |
| --- | --- | --- | --- |
| `command` | yes | reliable | Commands and small responses, such as recording status. |
| `stream` | **no** | reliable, unordered | Large data such as snapshots and file transfers. |
| `_lossy` | no | unreliable | IPC messages where old data can be dropped. |
| `_reliable` | yes | reliable | IPC messages that must be delivered. |

The `command` and `stream` are always available. The `_lossy` and `_reliable` channels are enabled with [`--enable-ipc`](CONFIGURATION.md#ipc).

Large transfers use the stream channel so they do not block commands. Multiple transfers can run at the same time.

# Two-way DataChannel Messaging

DataChannels allow the browser and device to exchange AI events, sensor data, and remote control commands. Supported with `--use-mqtt` and `--use-livekit`.

With [`--enable-ipc`](CONFIGURATION.md#ipc), `pi-webrtc` provides two additional channels:

| Channel  | Purpose   |
|:--------:| --------- |
| Lossy    | Messages where old data can be dropped, such as real-time sensor data |
| Reliable | Messages that must be delivered, such as commands |


The client can choose the channel for each message. [client-sdk-js](https://github.com/mazupo/client-sdk-js) defaults to the reliable channel.

> [!NOTE]
> With `--use-livekit`, messages are broadcast to all participants in the room.
> ![image](https://github.com/user-attachments/assets/cf88cd29-3717-4178-9e6b-f2f3ce9a0270)

## Usage:
1. Start `pi-webrtc` with `--enable-ipc`:
    ```bash
    /path/to/pi-webrtc --camera=libcamera:0 --fps=60 ... --enable-ipc
    ```

2. Run the [unix_socket_client.py](../examples/unix_socket_client.py) example on the device:
    ```bash
    python ./examples/unix_socket_client.py
    ```
    It logs everything sent and received through `pi-webrtc`. 

3. On the client side, use `onMessage()`, `sendText()`, or `sendData()` to receive and send messages.

- See [examples](https://github.com/mazupo/client-sdk-js/blob/main/docs/EXAMPLES.md#send-and-receive-ipc-messages) in [client-sdk-js](https://github.com/mazupo/client-sdk-js)
- Try it on Web demo: [http://app.mazupo.com/interaction](http://app.mazupo.com/interaction)

# Gamepad Input

[`--enable-gamepad`](CONFIGURATION.md#ipc) lets a browser gamepad control a process on the device through an IPC endpoint named `gamepad`. It requires [`--enable-ipc`](#two-way-datachannel-messaging).

The gamepad input is forwarded over a lossy DataChannel, so the device always receives the latest input available.

## Button mapping

The browser uses the W3C standard gamepad mapping when available. Buttons pack into one big-endian `uint32`, `buttons[N].pressed` as bit N.

| Bit | Button | Bit | Button | Bit | Button |
| --- | --- | --- | --- | --- | --- |
| 0 | A | 6 | LT | 12 | D-pad up |
| 1 | B | 7 | RT | 13 | D-pad down |
| 2 | X | 8 | Back/View | 14 | D-pad left |
| 3 | Y | 9 | Start/Menu | 15 | D-pad right |
| 4 | LB | 10 | L3 | 16 | Guide |
| 5 | RB | 11 | R3 | | |

## Reading Gamepad Input

The [gamepad_socket.py](../examples/gamepad_socket.py) example reads gamepad input from the device's `gamepad` IPC endpoint. The gamepad socket path can be changed with `--gamepad-socket-path`.

1. Install the protobuf tools:
    ```bash
    pip install protobuf
    sudo apt install protobuf-compiler
    ```

2. Generate the Python bindings:
    ```bash
    protoc -I external/protocol/protos --python_out=examples input.proto common.proto
    ```

3. Start `pi-webrtc` with IPC and gamepad support:
    ```bash
    /path/to/pi-webrtc --camera=libcamera:0 --fps=60 ... --enable-ipc --enable-gamepad
    ```

4. Run the example and connect a gamepad in the browser:
    ```bash
    python ./examples/gamepad_socket.py
    ```

- See the [example](https://github.com/mazupo/client-sdk-js/blob/main/docs/EXAMPLES.md#drive-a-device-with-a-gamepad). The sender polls the browser's
[Gamepad API](https://developer.mozilla.org/en-US/docs/Web/API/Gamepad_API).
- Try it on Web demo: [http://app.mazupo.com/gamepad](http://app.mazupo.com/gamepad)

# Stream AI or Any Custom Feed to a Virtual Camera

To enhance images, run AI recognition, or preprocess frames before streaming, process the camera frames and write the result to a [V4L2 loopback](https://github.com/umlaeute/v4l2loopback) device. `pi-webrtc` can then open it as a normal V4L2 camera.

> [!TIP]
> On Jetson, the [commercial version](COMMERCIAL.md#licensing) runs detection and tracking
> in-process on the GPU instead, with no loopback device and no copy through the CPU.

1. Install the packages:
    ```bash
    sudo apt install v4l2loopback-dkms libopencv-dev python3-opencv python3-picamera2 ffmpeg
    ```

2. Create a virtual device at `/dev/video8`:
    ```bash
    sudo modprobe v4l2loopback devices=1 video_nr=8 card_label=ProcessedCam max_buffers=4 exclusive_caps=1
    ```

3. Create a Python virtual env:
    ```bash
    python -m venv --system-site-packages ~/venv
    ```

4. Activate it and install the packages:
    ```bash
    source ~/venv/bin/activate
    pip install --upgrade pip
    pip install wheel
    pip install rpi-libcamera picamera2 opencv-python
    ```

5. Run the virtual camera, using Libcamera to output YUV420 (I420) to the virtual device. See
   the [virtual_cam.py](../examples/virtual_cam.py) example:
    ```bash
    python virtual_cam.py --width 1280 --height 720 --camera-id 0 --virtual-device /dev/video8
    ```

6. Run `pi-webrtc` against the virtual device with the matching format:
    ```bash
    /path/to/pi-webrtc --camera=v4l2:8 --fps=30 --width=1280 --height=720 --v4l2-format=i420 ...
    ```

> [!TIP]
> **Need the same camera source in several `pi-webrtc` instances?**
> Create multiple virtual cameras from one processed source and stream each independently.
> See [yolo_cam.py](../examples/yolo_cam.py) for writing to multiple loopback devices.

# WHEP with webrtc-player

[Eyevinn/webrtc-player](https://github.com/Eyevinn/webrtc-player) plays a WHEP URL in the browser.

- Run the program:
    ```bash
    /path/to/pi-webrtc --camera=libcamera:0 \
        --uid=home-pi-5 \
        --fps=60 \
        --width=1920 \
        --height=1080 \
        --use-whep \
        --whep-port=8080 \
        --no-audio
    ```

- Open the [demo player](https://tzuhuantai.github.io/webrtc-player/demo/), keep the adapter on
  **WHEP**, and play `http://<device-ip>:8080`, e.g. `http://192.168.4.35:8080`.

The player page is served over `https` while the device answers over `http`. Chrome allows this for private addresses such as `192.168.x.x` once you **Allow** its prompt to access devices on your local network; other browsers may block the request as mixed content.

# Using the WebRTC Camera in Home Assistant

### 1. Prepare the environment

Follow the official [Home Assistant installation guide](https://www.home-assistant.io/installation/).

### 2. Install HACS

HACS lets you install community integrations like WebRTC Camera. Follow the official
[HACS installation guide](https://www.home-assistant.io/blog/2024/08/21/hacs-the-best-way-to-share-community-made-projects/#how-to-install).

![Screenshot 2025-02-03 043025](https://github.com/user-attachments/assets/d28abff7-53d0-43d3-b225-7305c54a800e)

### 3. Install WebRTC Camera via HACS

Go to `Home Assistant` → `HACS` → `Integrations` → search for `WebRTC Camera`, then restart
Home Assistant.

![Screenshot 2025-02-03 043256](https://github.com/user-attachments/assets/17994718-f222-42e7-a0a7-531992bcbb34)

### 4. Add the integration

Go to `Settings` → `Devices & Services` → `Add Integration`.

![Screenshot 2025-02-03 045243](https://github.com/user-attachments/assets/d8ba49e6-7de3-4144-88f7-3f066f4ed393)

### 5. Run `pi-webrtc` with WHEP signaling

```bash
/path/to/pi-webrtc --camera=libcamera:0 \
    --uid=home-pi-4b \
    --fps=30 \
    --width=1280 \
    --height=720 \
    --use-whep \
    --whep-port=8080
```

The stream is exposed on port `8080`, e.g. `http://192.168.4.35:8080`.

### 6. Add the card to a dashboard

Go to `Dashboard` → `Edit Dashboard` → `Add Card` → `WebRTC Camera`.

![Screenshot 2025-02-03 043403](https://github.com/user-attachments/assets/5bc68138-d5a2-481e-a187-0d845aeca463)

Enter the URL in the configuration and save:

```yaml
type: custom:webrtc-camera
url: webrtc:http://192.168.4.35:8080
```

![Screenshot 2025-02-03 043600](https://github.com/user-attachments/assets/87d61efc-7107-41a0-bcb9-12378a904021)

# Jetson: Unthrottling the VIC and NVENC Clocks

On Jetson, the VIC and NVENC engines can run at low clock speeds under the default `tegra_wmark` governor. This can add significant latency to camera copies and hardware encoding.

`jetson_clocks` and `nvpmodel MAXN` do not increase the operating frequency of these multimedia engines.

For example, on an Orin NX at 1080p60, setting both governors to `performance` reduced device-side latency from about **36.5 ms** to **24 ms** in our test.

1. Check the current state:

```bash
for d in /sys/class/devfreq/*vic* /sys/class/devfreq/*nvenc*; do
    echo "$d: $(cat $d/governor) $(cat $d/cur_freq) / $(cat $d/max_freq)"
done
```

2. Apply it for the current boot:

```bash
echo performance | sudo tee /sys/class/devfreq/15340000.vic/governor
echo performance | sudo tee /sys/class/devfreq/154c0000.nvenc/governor
```

## Making it persistent

The governor resets on every boot. Create `/etc/systemd/system/tegra-mm-perf.service`:

```ini
[Unit]
Description=Pin Tegra VIC/NVENC devfreq governors to performance
After=nvargus-daemon.service

[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/bin/sh -c 'for d in /sys/class/devfreq/*vic* /sys/class/devfreq/*nvenc*; do echo performance > "$d/governor"; done'

[Install]
WantedBy=multi-user.target
```

Then enable it:

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now tegra-mm-perf.service
```

The wildcard keeps the service working across Jetson modules with different device addresses.

> [!NOTE]
> `performance` holds each engine at maximum while they are powered, which increases power consumption and heat. If that is too aggressive, you can instead raise the minimum frequency (`echo 614400000 | sudo tee /sys/class/devfreq/15340000.vic/min_freq`), or try the `nvhost_podgov` governor, which reacts to bursty per-frame work better than `tegra_wmark`.

# Useful Commands

| Command | Description |
|--|--|
| `v4l2-ctl --list-devices` | Show available V4L2 devices. |
| `v4l2-ctl -d /dev/video0 --list-formats-ext` | Show supported formats — for cameras and codecs alike. |
| `sudo fdisk -l` | List partition tables, to help set up a USB disk. |
| `vcgencmd get_camera` | Check whether the camera is detected. |
| `sudo tegrastats --interval 500` | Jetson: show per-engine utilisation and clocks, e.g. `VIC 36%@115`. |

To install the latest Mosquitto packages, follow the official
[Readme.txt](https://repo.mosquitto.org/debian/README.txt) for the Eclipse Mosquitto Debian
repository.
