# Advanced Usage

- [Broadcasting a Live Stream to Many Viewers via SFU](#broadcasting-a-live-stream-to-many-viewers-via-sfu)
- [Using the Legacy V4L2 Driver](#using-the-legacy-v4l2-driver)
- [Running as a Linux Service](#running-as-a-linux-service)
- [Two-way Audio Communication](#two-way-audio-communication)
- [DataChannels](#datachannels)
- [Two-way DataChannel Messaging](#two-way-datachannel-messaging)
- [Stream AI or Any Custom Feed to a Virtual Camera](#stream-ai-or-any-custom-feed-to-a-virtual-camera)
- [WHEP with Nginx Proxy](#whep-with-nginx-proxy)
- [Using the WebRTC Camera in Home Assistant](#using-the-webrtc-camera-in-home-assistant)
- [Jetson: Unthrottling the VIC and NVENC Clocks](#jetson-unthrottling-the-vic-and-nvenc-clocks)
- [Useful Commands](#useful-commands)

# Broadcasting a Live Stream to Many Viewers via SFU

![SFU Cloud Service](https://github.com/user-attachments/assets/e41bcd7b-7c84-4837-88c2-820b20c094d4)

Publishing to an SFU lets the device serve **any number of viewers while only ever encoding and
uploading one stream** — fanning out to the audience is the SFU's job, not the device's.

Two backends are supported. [LiveKit](#livekit) is an SFU server, self-hosted or hosted.
[Cloudflare Realtime](#cloudflare-realtime) has no server at all — the fan-out runs on
Cloudflare's edge. Both have a free endpoint below to try without setting anything up.

## LiveKit

See [Signaling](SIGNALING.md#livekit) for how the connection works.

### Free Testing Server

| URL | API Key |
| --- | --- |
| `wss://api.picamera.live` | `APIWnQTs4tmUZvA` |

⚠️ The testing server allows up to 100 concurrent connections, with a monthly limit of 5,000
minutes and 50 GB of transfer shared across all users. For a dedicated environment, contact
`tzu.huan.tai@gmail.com`.

### 1. Run on the device

```bash
/path/to/pi-webrtc --camera=libcamera:0 \
    --fps=30 \
    --width=1920 \
    --height=1080 \
    --uid=your-display-name \
    --use-livekit \
    --livekit-url=wss://api.picamera.live \
    --livekit-key=APIWnQTs4tmUZvA \
    --livekit-room=the-room-name
```

> `--uid` is the publisher's identity, e.g. `camera123`.
> `--livekit-room` is the room name shared by the publisher and its viewers.

### 2. Join the room

- [`picamera.js`](https://github.com/TzuHuanTai/picamera.js?tab=readme-ov-file#examples)
- Web demo: [https://app.picamera.live/room](https://app.picamera.live/room)

## Cloudflare Realtime

There is no SFU to host and no Cloudflare account to create: the device API relays the
handshake, holding the Realtime credentials on its side. See
[Signaling](SIGNALING.md#cloudflare-realtime) for the exchange.

Cloudflare assigns the session id and changes it on every reconnect, so the device publishes it
under its `--uid`. A viewer only ever needs the uid.

### Free Testing Server

| URL | Device API Key | Viewer API Key |
| --- | --- | --- |
| `https://api.picamera.live` | `81f899b8fab5692b0faa76c3372b7ae6` | `ec0478c67e729b6f429eda1e97829af0` |

⚠️ Shared across all users and capped at the Cloudflare free tier, so it stops serving once the
monthly allowance is used up. Everyone shares these keys and the registry is keyed by `--uid`,
so pick a distinctive one — a common name can be overwritten by another tester. For a dedicated
environment, contact `tzu.huan.tai@gmail.com`.

### 1. Run on the device

```bash
/path/to/pi-webrtc --camera=libcamera:0 \
    --fps=30 \
    --width=1920 \
    --height=1080 \
    --uid=your-display-name \
    --use-cloudflare \
    --api-url=https://api.picamera.live \
    --api-key=81f899b8fab5692b0faa76c3372b7ae6
```

> `--uid` is what a viewer looks the stream up by, e.g. `camera123`.

### 2. Watch

Web demo: [https://app.picamera.live/cloudflare](https://app.picamera.live/cloudflare). Put the
URL and the **viewer** key into *Settings → Network*, then pick the device from the selector.

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
    start_x=1               # Includes additional codecs
    gpu_mem=128             # Adjust based on resolution (256MB for 1080p). Not valid on bookworm.
    ```

    > [!TIP]
    > **Not sure whether to use V4L2 or Libcamera?**
    > V4L2 suits older or generic USB cameras that need no special driver, and most USB
    > cameras are detected as V4L2 devices by default. Camera Module v3 only works with
    > Libcamera. If you're unsure, start with Libcamera.

2. Run with `--camera=v4l2:0` for the camera at `/dev/video0`:

    ```bash
    ./pi-webrtc --camera=v4l2:0 \
        --uid=your-custom-uid \
        --v4l2-format=mjpeg \
        --fps=30 \
        --width=1280 \
        --height=960 \
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

## 1. Run `pulseaudio` as a system-wide daemon

Skip this if you run with `--no-audio`.
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
    ExecStart=/path/to/pi-webrtc --camera=libcamera:0 --fps=30 --width=1280 --height=960 --uid=your-uid --hw-accel --use-mqtt --mqtt-host=example.s1.eu.hivemq.cloud --mqtt-port=8883 --mqtt-username=hakunamatata --mqtt-password=wonderful
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

Run [`pulseaudio`](#1-run-pulseaudio-as-a-system-wide-daemon) in the background and drop the
`--no-audio` flag. On a build without PulseAudio, `--force-alsa` captures through ALSA
instead.

The device needs a microphone and a speaker. A USB mic/speaker is the easy path; for GPIO,
follow the links below.

- **Microphone** — [wiring and testing an I2S MEMS mic](https://learn.adafruit.com/adafruit-i2s-mems-microphone-breakout/raspberry-pi-wiring-test)
- **Speaker** — [wiring a MAX98357 I2S amp](https://learn.adafruit.com/adafruit-max98357-i2s-class-d-mono-amp/raspberry-pi-wiring)

# DataChannels

`pi-webrtc` opens up to four DataChannels towards a browser peer, each with a single job:

| Label | Ordered | Reliability | Carries |
| --- | --- | --- | --- |
| `command` | yes | reliable | Client requests, and small device responses such as the recording state. |
| `stream` | **no** | reliable | Bulk responses, chunked: snapshot JPEGs, file-query results, file transfers. |
| `_lossy` | no | no retransmits | IPC, UDP-like. |
| `_reliable` | yes | reliable | IPC, TCP-like. |

`command` and `stream` are always opened; the two IPC channels only with
[`--enable-ipc`](CONFIGURATION.md#ipc).

How the channels are opened depends on the signaling backend, not on the role. For a
direct browser peer (MQTT, WHEP) all four are negotiated out-of-band on fixed stream ids
**0**, **1**, **2** and **3** in the order above, so both ends can use a channel as soon
as the transport is up. The client creates each one itself, e.g.
`createDataChannel("command", { negotiated: true, id: 0, ordered: true })`.

Over LiveKit the SFU opens its own reserved channels towards the device instead, so
there the IPC pair is negotiated in-band and matched by label.

Bulk content gets its own unordered channel because a large response would otherwise
head-of-line block every command behind it. Each transfer is split into a
`Stream` header, chunks and a trailer sharing one `stream_id`, so several transfers may
interleave freely: asking for a snapshot while a video download is in flight gets an
answer straight away rather than waiting for the download to finish. The enclosing
packet's `type` says how the reassembled body is to be parsed, and its `request_id` which
request it answers.

# Two-way DataChannel Messaging

Carries AI event notifications, sensor readings, or remote control commands between the
browser and the device. Works with both `--use-mqtt` and `--use-livekit`.

[`--enable-ipc`](CONFIGURATION.md#ipc) opens two channels, lossy (UDP-like) and reliable
(TCP-like), and the client chooses per message rather than for the whole connection —
picamera.js takes the mode as an argument to `sendText()` / `sendData()`, defaulting to
reliable. Messages the device sends up from the Unix socket always go over the reliable
channel.

> [!NOTE]
> Over `--use-livekit`, messages are broadcast to every participant in the room.
> ![image](https://github.com/user-attachments/assets/cf88cd29-3717-4178-9e6b-f2f3ce9a0270)

- Start `pi-webrtc` with `--enable-ipc`:
    ```bash
    /path/to/pi-webrtc --camera=libcamera:0 --fps=30 ... --enable-ipc
    ```

- Run the [unix_socket_client.py](../examples/unix_socket_client.py) example on the device:
    ```bash
    python ./examples/unix_socket_client.py
    ```
    It logs everything sent and received through `pi-webrtc`, and keeps sending
    "**ping from client**" over the Unix socket. The socket path is `--socket-path`.

- On the client side, picamera.js exposes `onMessage()` and `sendMessage()`. See
  [the picamera.js examples](https://github.com/TzuHuanTai/picamera.js?tab=readme-ov-file#examples),
  or try it on the [picamera-web](http://app.picamera.live/interaction) interaction page.

# Stream AI or Any Custom Feed to a Virtual Camera

To enhance images, run recognition, or preprocess frames before streaming, read from the real
camera, process the frames, and write the result to a
[V4L2 loopback](https://github.com/umlaeute/v4l2loopback) device that `pi-webrtc` opens as an
ordinary V4L2 camera.

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

# WHEP with Nginx Proxy

Browsers only build WebRTC connections from pages served over `https`, so `pi-webrtc` needs to
be reachable over `https` too. Below is an `nginx.conf` using **DDNS** and **Let's Encrypt**,
assuming `pi-webrtc` runs with `--http-port=8080` and the hostname is `example.ddns.net`.

⚠️ Remember to forward public port 443 to the device.

- Example `nginx.conf`:
    ```nginx
    http {
        gzip on;
        sendfile on;
        tcp_nopush on;
        types_hash_max_size 2048;

        include /etc/nginx/mime.types;
        default_type application/octet-stream;
        ssl_protocols TLSv1 TLSv1.1 TLSv1.2 TLSv1.3;

        access_log /var/log/nginx/access.log;

        server {
            listen *:443 ssl;
            listen [::]:443 ssl;
            server_name example.ddns.net;

            ssl_certificate /etc/letsencrypt/live/example.ddns.net/fullchain.pem;
            ssl_certificate_key /etc/letsencrypt/live/example.ddns.net/privkey.pem;

            location / {
                proxy_pass http://127.0.0.1:8080;
                proxy_http_version 1.1;
                proxy_set_header Host $host;
                proxy_set_header X-Real-IP $remote_addr;
                proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
                proxy_set_header X-Forwarded-Proto $scheme;
            }
        }
    }
    ```

- Run the program:
    ```bash
    /path/to/pi-webrtc --camera=libcamera:0 \
        --uid=home-pi-5 \
        --fps=30 \
        --width=2560 \
        --height=1440 \
        --use-whep \
        --http-port=8080 \
        --no-audio
    ```

- Play `https://example.ddns.net/` in the [eyevinn demo player](https://webrtc.player.eyevinn.technology/)

    ![image](https://github.com/user-attachments/assets/c7052cdb-87fe-4117-8b98-7a970005f98b)

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
    --http-port=8080
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

On Jetson, the VIC (the 2D engine behind every `NvBufSurfTransform` and Argus buffer copy) and
NVENC run under the kernel's **devfreq** governor, independently of the CPU and GPU. The stock
governor is `tegra_wmark`, which scales on job-queue depth. A camera pipeline submits one short
job per frame and then goes idle, so the queue never builds, the watermark never trips, and both
engines sit at their **115.2 MHz floor** — against ceilings of 729.6 MHz (VIC) and 793.6 MHz
(NVENC).

`jetson_clocks` does **not** fix this. It walks `/sys/class/devfreq/*` only to locate the iGPU
and skips every other node, so it covers CPU, GPU, EMC, DLA and PVA but leaves the multimedia
engines alone. `nvpmodel MAXN` only raises the ceiling; it does not raise the operating point.

Measured on an Orin NX at 1080p60 with `--latency-trace`, one viewer:

| Stage | `tegra_wmark` | `performance` |
|---|---|---|
| `argus_copy` (`copyToNvBuffer`, VIC) | 7.00 ms | **2.50 ms** |
| `hw_encode_dwell` (NVENC) | 11.50 ms | **3.00 ms** |
| `sensor->sent` (whole device side) | 36.50 ms | **24.00 ms** |

Per-frame variance drops even more sharply than the median — `hw_encode_dwell` went from a
5.51–14.25 ms spread to 2.86–3.17 ms. That matters twice over, because the receiver sizes its
jitter buffer from arrival variation, so steadier frames shorten the playout delay as well.

Check the current state:

```bash
for d in /sys/class/devfreq/*vic* /sys/class/devfreq/*nvenc*; do
    echo "$d: $(cat $d/governor) $(cat $d/cur_freq) / $(cat $d/max_freq)"
done
```

Apply it for the current boot:

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

The glob keeps the unit working across Jetson modules, whose device addresses differ (they are
`15340000.vic` and `154c0000.nvenc` on Orin NX).

> [!NOTE]
> `performance` holds each engine at maximum whenever it is powered, which raises power draw and
> heat. Both engines are still power-gated when idle, so the cost is modest on an always-streaming
> device. For a middle ground, keep the stock governor and just raise the floor
> (`echo 614400000 | sudo tee /sys/class/devfreq/15340000.vic/min_freq`), or try the
> `nvhost_podgov` governor, which reacts to bursty per-frame work better than `tegra_wmark`.

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
