# Configuration

Every option can be given either as a command-line flag or as a key in a
[YAML config file](#config-file). Command-line arguments always win over the file.

The camera control options are compatible with the official `rpicam-apps`, so the
[Raspberry Pi camera control documentation](https://www.raspberrypi.com/documentation/computers/camera_software.html#camera-control-options)
applies here too.

- [Camera and Video Input](#camera-and-video-input)
- [Sub-stream](#sub-stream)
- [Audio](#audio)
- [Image Controls](#image-controls)
- [Recording](#recording)
- [WebRTC](#webrtc)
- [IPC](#ipc)
- [Signaling](#signaling)
- [Object Detection and Tracking](#object-detection-and-tracking)
- [Config File](#config-file)
- [Multi-camera](#multi-camera)

## Camera and Video Input

| Option | Default | Description |
|---|---|---|
| `-h`, `--help` | | Display the help message. |
| `--camera` | `libcamera:0` | Camera to open, as `<backend>:<id>` or an `rtsp://` URL. See [Video & Audio](../media/README.md). |
| `--v4l2-format` | `i420` | Input format of a V4L2 camera: `i420`, `yuyv`, `mjpeg`, `h264`. Ignored by other backends. |
| `--uid` | | Unique id identifying this device. **Required.** |
| `--fps` | `30` | Camera frames per second. |
| `--width` | `640` | Camera frame width. |
| `--height` | `480` | Camera frame height. |
| `--rotation` | `0` | Rotation angle: `0`, `90`, `180`, `270`. |

## Sub-stream

A second, usually smaller, capture stream from the same camera. Live streaming and recording
can each be pointed at either stream, so you can (for example) record at full resolution
while streaming a downscaled copy, or feed a small stream to the detector while the viewer
still gets the full picture.

The sub-stream is off unless both `--sub-width` and `--sub-height` are set. If either
exceeds the main stream's dimensions it is clamped to the main stream.

CSI cameras have a sub-stream: `libcamera` on a Raspberry Pi and `libargus` on a Jetson.
USB cameras (`v4l2`) and RTSP sources do not, so both consumers read the main stream.

On a Raspberry Pi, use a sub-stream width that is a multiple of 64, such as `640` or `1280`.

| Option | Default | Description |
|---|---|---|
| `--sub-width` | `0` | Sub-stream frame width. `0` disables the sub-stream. |
| `--sub-height` | `0` | Sub-stream frame height. `0` disables the sub-stream. |
| `--record-source` | `main` | Which capture stream the recorder consumes: `main` or `sub`. |
| `--webrtc-source` | `main` | Which capture stream WebRTC publishes: `main` or `sub`. |

> [!NOTE]
> `--record-source` and `--webrtc-source` fall back to `main` when no sub-stream is
> configured. They select which stream a consumer reads from, not whether it runs — use
> the per-camera `record` and `webrtc` flags to turn a consumer off.

## Audio

| Option | Default | Description |
|---|---|---|
| `--sample-rate` | `48000` | Microphone sample rate, in Hz. |
| `--no-audio` | `false` | Run without an audio source. |
| `--force-alsa` | `false` | Capture and play audio through ALSA instead of PulseAudio. |

## Image Controls

These are available on builds with libcamera support. They have no effect on a V4L2 or
libargus camera.

| Option | Default | Description |
|---|---|---|
| `--sharpness` | `1.0` | Sharpness, `0.0` to `15.99`. |
| `--contrast` | `1.0` | Contrast, `0.0` to `15.99`. |
| `--brightness` | `0.0` | Brightness, `-1.0` to `1.0`. |
| `--saturation` | `1.0` | Saturation, `0.0` to `15.99`. |
| `--ev` | `0.0` | Exposure value compensation, `-10.0` to `10.0`. |
| `--shutter` | `0` | Manual shutter speed in microseconds (`0` = auto). Accepts unit suffixes, e.g. `20ms`. |
| `--gain` | `0.0` | Manual analog gain (`0` = auto). |
| `--metering` | `centre` | Metering mode: `centre`, `spot`, `average`, `matrix`, `custom`. |
| `--exposure` | `normal` | Exposure mode: `normal`, `sport`, `short`, `long`, `custom`. |
| `--awb` | `auto` | AWB mode: `auto`, `normal`, `incandescent`, `tungsten`, `fluorescent`, `indoor`, `daylight`, `cloudy`, `custom`. |
| `--awbgains` | `0,0` | Custom AWB gains as `red,blue`, e.g. `1.2,1.5`. Used with `--awb=custom`. |
| `--denoise` | `auto` | Denoise mode: `off`, `cdn_off`, `cdn_fast`, `cdn_hq`, `auto`. |
| `--tuning-file` | `-` | Camera tuning file. `-` keeps libcamera's default behaviour. |
| `--autofocus-mode` | `default` | Autofocus mode: `default`, `manual`, `auto`, `continuous`. |
| `--autofocus-range` | `normal` | Autofocus range: `normal`, `macro`, `full`. |
| `--autofocus-speed` | `normal` | Autofocus speed: `normal`, `fast`. |
| `--autofocus-window` | `0,0,0,0` | Autofocus window as `x,y,width,height`, e.g. `0.3,0.3,0.4,0.4`. All zeros uses the full frame. |
| `--lens-position` | | Fixed focus position. `0` is infinity, `default` is the hyperfocal distance. Leave unset to keep libcamera's behaviour. |

## Recording

See [Recording](../recording/README.md) for the directory layout, rotation policy, and the
DataChannel commands that drive on-demand capture.

| Option | Default | Description |
|---|---|---|
| `--record-type` | `both` | What to record: `video` for MP4 files, `snapshot` for periodic JPEGs, or `both`. |
| `--record-mode` | `both` | When to record: `background` for continuous capture, `on-demand` for DataChannel-triggered capture, or `both`. |
| `--record-path` | | Absolute path for background recordings. The background recorder does not start if this is empty or unwritable. |
| `--record-ondemand-path` | | Absolute path for on-demand recordings. Falls back to `<record-path>/on-demand/`. |
| `--file-duration` | `60` | Length in seconds of each video file, or the interval between snapshots. |
| `--jpeg-quality` | `30` | Quality of snapshots and thumbnails, `0` to `100`. |

> [!IMPORTANT]
> `--record-mode` used to select `video` / `snapshot` / `both`. That meaning moved to
> `--record-type`, and `--record-mode` now selects when recording happens. Rename any
> existing `--record-mode=video` or `--record-mode=snapshot` to `--record-type=`.

## WebRTC

| Option | Default | Description |
|---|---|---|
| `--peer-timeout` | `60` | Connection timeout in seconds after receiving a remote offer. |
| `--max-bitrate` | `0` | Maximum video bitrate (kbps). `0`: **0.08 bpp** with adaptive scaling (~10 Mbps at 1080p60, min. 2.5 Mbps), or **2.5 Mbps** with `--no-adaptive`. |
| `--start-bitrate` | `0` | Initial bandwidth estimate (kbps). `0`: **1 Mbps** with adaptive scaling, or **300 kbps** with `--no-adaptive`. Below 500 kbps, sources above VGA may be downscaled permanently. |
| `--min-bitrate` | `0` | Floor in kbps for the bandwidth estimate. `0` keeps WebRTC's default. |
| `--hw-accel` | `false` | Share DMA buffers between decoder, scaler, and encoder to cut CPU usage. See [Encoding](../media/encoding.md#hardware-encoding). |
| `--no-adaptive` | `false` | Disable adaptive resolution scaling, keeping the output resolution fixed regardless of network or device conditions. |
| `--scalability-mode` | | Temporal layers for the software video encoders, e.g. `L1T2` or `L1T3`. Not supported with `--hw-accel`. Empty keeps WebRTC's default (`L1T1`). |
| `--latency-trace` | `false` | Measure per-frame latency from the sensor timestamp through capture, scaling, encoding and the handoff to WebRTC, then print p50/p95/max per stage. Works in release builds. |
| `--latency-trace-interval` | `5` | Seconds between `--latency-trace` summaries. |
| `--stun-url` | `stun:stun.l.google.com:19302` | STUN server URL. Must start with `stun:`. |
| `--turn-url` | | TURN server URL, e.g. `turn:example.com:3478?transport=tcp`. Must start with `turn:`. |
| `--turn-username` | | TURN username. |
| `--turn-password` | | TURN password. |

> [!NOTE]
> WebRTC may lower the streaming `fps`, `width`, or `height` when the network or the device
> is under pressure. Recording always uses the configured resolution regardless of these
> adjustments.

## IPC

Bridges WebRTC DataChannels to local Unix sockets. See [IPC messages](../integrations/ipc.md) and [Gamepad](../integrations/gamepad.md).

| Option | Default | Description |
|---|---|---|
| `--enable-ipc` | `false` | Enable IPC over WebRTC DataChannels. |
| `--socket-path` | `/tmp/pi-webrtc-ipc.sock` | Unix socket for IPC messages. |
| `--enable-gamepad` | `false` | Enable browser gamepad input. Implies `--enable-ipc`. |
| `--gamepad-socket-path` | `/tmp/pi-webrtc-gamepad.sock` | Unix socket for gamepad input. |

## Signaling

At least one signaling transport must be enabled or the process exits. See
[Signaling](../signaling/README.md) for the connection flows.

### MQTT

| Option | Default | Description |
|---|---|---|
| `--use-mqtt` | `false` | Exchange SDP and ICE candidates over MQTT. |
| `--mqtt-host` | `localhost` | MQTT broker host. **Required** with `--use-mqtt`. |
| `--mqtt-port` | `1883` | MQTT broker port. |
| `--mqtt-username` | | MQTT username. |
| `--mqtt-password` | | MQTT password. |

### WHEP

| Option | Default | Description |
|---|---|---|
| `--use-whep` | `false` | Serve WHEP (WebRTC-HTTP Egress Protocol) for SDP and ICE exchange. |
| `--whep-port` | `8080` | Local HTTP port serving WHEP signaling. |

### LiveKit

| Option | Default | Description |
|---|---|---|
| `--use-livekit` | `false` | Connect to a LiveKit SFU server over WebSocket. |
| `--livekit-url` | | SFU server URL, e.g. `ws://127.0.0.1:7880` or `wss://your-sfu-host.example.com`. The scheme selects TLS; the port defaults to `443` for `wss` and `80` otherwise. **Required** with `--use-livekit`. |
| `--livekit-room` | | Room name to join. **Required** with `--use-livekit`. |
| `--livekit-key` | | API key used to authenticate with the SFU server. |
| `--livekit-secret` <sup>[\*](../sponsors.md#sponsor-benefits)</sup> | | LiveKit API secret paired with `--livekit-key`. Signs access tokens on-device, which is what lets the sponsor build connect to a LiveKit deployment of your own. **Required** with `--use-livekit`. |

### Cloudflare Realtime SFU

The device API relays the handshake and holds the session a viewer has to pull. See
[SFU](../signaling/sfu.md#cloudflare-realtime) for a worked example.

| Option | Default | Description |
|---|---|---|
| `--use-cloudflare` | `false` | Publish to a Cloudflare Realtime SFU over its HTTPS API. |
| `--api-url` | | Base URL of the device API, e.g. `https://api.mazupo.com`. Every Realtime call goes to `<api-url>/sfu/...`, and the session is published to `PUT <api-url>/devices/<uid>/session` on connect and refreshed every 15 minutes. **Required** with `--use-cloudflare`. |
| `--api-key` | | Bearer token authenticating this device against `--api-url`. **Required** with `--api-url`. |
| `--cloudflare-url` <sup>[\*](../sponsors.md#sponsor-benefits)</sup> | | Base URL of the Realtime API, including the API version path. Defaults to `https://rtc.live.cloudflare.com/v1`; only worth setting when Cloudflare publishes a newer version. |
| `--cloudflare-app-id` <sup>[\*](../sponsors.md#sponsor-benefits)</sup> | | Realtime App ID to publish into. **Required** with `--use-cloudflare` in the sponsor build. |
| `--cloudflare-app-secret` <sup>[\*](../sponsors.md#sponsor-benefits)</sup> | | Realtime App Secret, sent as the bearer token. **Required** with `--use-cloudflare` in the sponsor build. |

The App ID and Secret are what let a device handshake with Cloudflare itself instead of going through the relay, and only the sponsor build carries that logic.

## Object Detection and Tracking

Available in the [sponsor build](../sponsors.md#sponsor-benefits) on NVIDIA Jetson.

| Option | Default | Description |
|---|---|---|
| `--detector-model` <sup>[\*](../sponsors.md#sponsor-benefits)</sup> | | TensorRT engine file for YOLO detection. Empty disables the detector. |
| `--detector-labels` <sup>[\*](../sponsors.md#sponsor-benefits)</sup> | | Class-name file, one per line. Defaults to the COCO 80 classes. |
| `--detector-confidence` <sup>[\*](../sponsors.md#sponsor-benefits)</sup> | `0.5` | Minimum detection confidence, `0.0` to `1.0`. |
| `--tracker-config` <sup>[\*](../sponsors.md#sponsor-benefits)</sup> | | NvMOT YAML config selecting the tracker, e.g. NvDCF or DeepSORT. |

## Config File

`--config` points at a YAML file. Every long-form option is accepted as a key, without the
leading `--`, and boolean flags take `true` / `false`.

```bash
/path/to/pi-webrtc --config=/path/to/config.yml
```

A starting point ships as [`config/config.yml`](../../config/config.yml):

```yaml
# ── Video input ──
camera: libcamera:0
fps: 60
width: 1920
height: 1080

# ── Hardware / encoding ──
hw-accel: true # Set to false on Raspberry Pi 5, which has no hardware encoder
no-adaptive: false

# ── Audio ──
no-audio: true

# ── Device identity ──
uid: your-device-uid

# ── MQTT signaling ──
use-mqtt: true
mqtt-host: your-mqtt-broker.example.com
mqtt-port: 8883
mqtt-username: your-mqtt-username
mqtt-password: your-mqtt-password

# ── IPC ──
enable-ipc: true

# ── Recording ──
record-path: /path/to/recording/output
```

Things worth knowing:

- **Command-line arguments take priority.** A flag on the command line overrides the same key
  in the file, which makes the file a good place for defaults you occasionally override.
- **Unknown keys are ignored** rather than treated as errors, so a config file can carry
  comments-as-keys or settings for a newer version without breaking an older binary.
- **Only scalar values are read.** Nested mappings and sequences are skipped, with the single
  exception of the sponsor build's `cameras:` list (see [Multiple cameras](../deployment/multi-camera.md)).

## Multi-camera

Run one process per camera, or, with the sponsor build, all cameras in one process with a
`cameras:` list. See [Multiple cameras](../deployment/multi-camera.md).

---

# Sponsor Build

Options marked <sup>[\*](../sponsors.md#sponsor-benefits)</sup> above are part of the sponsor
build. See [Sponsors](../sponsors.md) for what is included.
