# Multiple Cameras

Stream several cameras from one device. Run one pi-webrtc process per camera, or use the sponsor build to run all of them in one process.

## One process per camera

Give each process its own `--camera`, `--uid`, `--whep-port` and `--record-path`:

```bash
./pi-webrtc --camera=libcamera:0 --uid=home-front --use-whep --whep-port=8080 --record-path=/home/pi/video/front
./pi-webrtc --camera=libcamera:1 --uid=home-side --use-whep --whep-port=8081 --record-path=/home/pi/video/side
```

Every option works per process. With `--enable-ipc`, also give each process its own `--socket-path`.

## One process for all cameras

<sup>[\*](../sponsors.md#sponsor-benefits)</sup> Sponsor build.

The sponsor build adds a `cameras:` list to the [config file](../reference/configuration.md#config-file). All cameras then run in one process, with one `uid` and one WHEP port. Each camera takes the global settings, and can change them, including its own main and sub-stream and its recording.

```yaml
uid: home-jetson-orin

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

record-path: /home/nx/video
use-whep: true
whep-port: 8080
```

Each camera can turn WebRTC and recording on or off:

| Key | Default | Description |
|---|---|---|
| `alias` | `cam0`, `cam1`, ... | The camera name. It is used for the recording folder, the WHEP path and the WebRTC stream id. Up to 32 letters, digits, `-` or `_`. It must be unique, and it cannot be `sessions`. |
| `webrtc` | `true` | Stream this camera over WebRTC. |
| `record` | `true` | Record this camera. |

Each camera records into its own folder. In this example, the `front` camera records to `/home/nx/video/front/`, and the `side` camera to `/home/nx/video/side/`.

Each camera with `webrtc: true` has its own WHEP path. In this example, `http://<device-ip>:8080/front` plays the front camera. `http://<device-ip>:8080/side` does not exist, because the `side` camera has `webrtc: false`.
