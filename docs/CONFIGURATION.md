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
| `--camera` | `libcamera:0` | Camera to open, as `<backend>:<id>`. See [Camera and Encoding](CAMERA_AND_ENCODING.md). |
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
| `--sample-rate` | `48000` | Audio sample rate, in Hz. |
| `--no-audio` | `false` | Run without an audio source. |
| `--force-alsa` | `false` | Capture through ALSA instead of PulseAudio. |

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

See [Recording](RECORDING.md) for the directory layout, rotation policy, and the
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
| `--max-bitrate` | `0` | Ceiling in kbps the video sender may be allocated. `0` keeps WebRTC's own default, which is derived from the resolution and is often well below what the link can carry. |
| `--start-bitrate` | `0` | Initial bandwidth estimate in kbps. `0` keeps WebRTC's default of 300, which the estimator then has to ramp up from while every frame is squeezed to fit it. |
| `--min-bitrate` | `0` | Floor in kbps for the bandwidth estimate. `0` keeps WebRTC's default. |
| `--hw-accel` | `false` | Share DMA buffers between decoder, scaler, and encoder to cut CPU usage. See [Camera and Encoding](CAMERA_AND_ENCODING.md#hardware-encoding). |
| `--no-adaptive` | `false` | Disable adaptive resolution scaling, keeping the output resolution fixed regardless of network or device conditions. |
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

Bridges a WebRTC DataChannel to a local Unix socket, so processes on the device can exchange
messages with the browser. See [Advanced Usage](ADVANCED.md#two-way-datachannel-messaging).

| Option | Default | Description |
|---|---|---|
| `--enable-ipc` | `false` | Enable the IPC relay over DataChannels. Opens both a lossy (UDP-like) and a reliable (TCP-like) channel; the client picks one per message. |
| `--socket-path` | `/tmp/pi-webrtc-ipc.sock` | Unix domain socket used to bridge the DataChannel to local applications. |

## Signaling

At least one signaling transport must be enabled or the process exits. See
[Signaling](SIGNALING.md) for the connection flows.

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
| `--http-port` | `8080` | Local HTTP port handling WHEP signaling. |

### LiveKit

| Option | Default | Description |
|---|---|---|
| `--use-livekit` | `false` | Connect to a LiveKit SFU server over WebSocket. |
| `--livekit-url` | | SFU server URL, e.g. `ws://127.0.0.1:7880` or `wss://your-sfu-host.example.com`. The scheme selects TLS; the port defaults to `443` for `wss` and `80` otherwise. **Required** with `--use-livekit`. |
| `--livekit-room` | | Room name to join. **Required** with `--use-livekit`. |
| `--livekit-key` | | API key used to authenticate with the SFU server. |
| `--livekit-secret` <sup>[\*](COMMERCIAL.md#licensing)</sup> | | LiveKit API secret paired with `--livekit-key`. Signs access tokens on-device, which is what lets the commercial build connect to a LiveKit deployment of your own. **Required** with `--use-livekit`. |

### Cloudflare Realtime SFU

The device API relays the handshake and holds the session a viewer has to pull. See [Signaling](SIGNALING.md#cloudflare-realtime) for the exchange and
[Broadcasting to many viewers](ADVANCED.md#cloudflare-realtime) for a worked example.

| Option | Default | Description |
|---|---|---|
| `--use-cloudflare` | `false` | Publish to a Cloudflare Realtime SFU over its HTTPS API. |
| `--api-url` | | Base URL of the device API, e.g. `https://api.picamera.live`. Every Realtime call goes to `<api-url>/sfu/...`, and the session is published to `PUT <api-url>/devices/<uid>/session` on connect and refreshed every 15 minutes. **Required** with `--use-cloudflare`. |
| `--api-key` | | Bearer token authenticating this device against `--api-url`. **Required** with `--api-url`. |
| `--cloudflare-url` <sup>[\*](COMMERCIAL.md#licensing)</sup> | | Base URL of the Realtime API, including the API version path. Defaults to `https://rtc.live.cloudflare.com/v1`; only worth setting when Cloudflare publishes a newer version. |
| `--cloudflare-app-id` <sup>[\*](COMMERCIAL.md#licensing)</sup> | | Realtime App ID to publish into. **Required** with `--use-cloudflare` in the commercial build. |
| `--cloudflare-app-secret` <sup>[\*](COMMERCIAL.md#licensing)</sup> | | Realtime App Secret, sent as the bearer token. **Required** with `--use-cloudflare` in the commercial build. |

The App ID and Secret are what let a device handshake with Cloudflare itself instead of going through the relay, and only the commercial build carries that logic.

## Object Detection and Tracking

Available in the [commercial version](COMMERCIAL.md#licensing) on NVIDIA Jetson.

| Option | Default | Description |
|---|---|---|
| `--detector-model` <sup>[\*](COMMERCIAL.md#licensing)</sup> | | TensorRT engine file for YOLO detection. Empty disables the detector. |
| `--detector-labels` <sup>[\*](COMMERCIAL.md#licensing)</sup> | | Class-name file, one per line. Defaults to the COCO 80 classes. |
| `--detector-confidence` <sup>[\*](COMMERCIAL.md#licensing)</sup> | `0.5` | Minimum detection confidence, `0.0` to `1.0`. |
| `--tracker-config` <sup>[\*](COMMERCIAL.md#licensing)</sup> | | NvMOT YAML config selecting the tracker, e.g. NvDCF or DeepSORT. |

## Config File

`--config` points at a YAML file. Every long-form option is accepted as a key, without the
leading `--`, and boolean flags take `true` / `false`.

```bash
/path/to/pi-webrtc --config=/path/to/config.yml
```

A starting point ships as [`config/config.yml`](../config/config.yml):

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
  exception of the `cameras:` list below.

## Multi-camera

<sup>[\*](COMMERCIAL.md#licensing)</sup> Commercial version.

A `cameras:` sequence replaces the flat `camera:` / `fps:` / `width:` fields and runs several
cameras from one process. Each entry starts from the global settings and overrides only what
it names.

```yaml
cameras:
  - camera: libargus:0
    alias: front
    fps: 60
    width: 1920
    height: 1080
    sub-width: 720
    sub-height: 480
    record-source: main
    webrtc-source: sub
    webrtc: true
    record: true
  - camera: libargus:1
    alias: side
    fps: 60
    width: 1280
    height: 720
    webrtc: false
    record: true
```

Besides the regular per-camera keys, three flags exist only here:

| Key | Default | Description |
|---|---|---|
| `alias` | `cam0`, `cam1`, … | Short name used as the recording subdirectory for this camera. |
| `webrtc` | `true` | Whether this camera is published as a WebRTC track. |
| `record` | `true` | Whether this camera is recorded. |

Each camera writes into its own subdirectory, so with `record-path: /mnt/ext_disk/video/` the
entries above record to `/mnt/ext_disk/video/front/` and `/mnt/ext_disk/video/side/`.

---

# Commercial Version

Options marked <sup>[\*](COMMERCIAL.md#licensing)</sup> above are part of the commercial
build. See [Commercial Version](COMMERCIAL.md) for what is included and how to license it,
or contact **tzu.huan.tai@gmail.com**.
