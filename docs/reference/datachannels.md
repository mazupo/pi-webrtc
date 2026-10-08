# DataChannels

pi-webrtc uses WebRTC DataChannels for commands, file transfers and IPC messages. This page lists the channels, which signaling carries them, and the commands that they carry.

## Channels

| Label | Ordered | Delivery | Carries |
| --- | --- | --- | --- |
| `command` | yes | reliable | Commands and small answers, such as the recording status |
| `stream` | **no** | reliable | Large data, such as snapshots, file lists and files |
| `_lossy` | no | may drop messages | IPC messages where old data can be dropped |
| `_reliable` | yes | reliable | IPC messages that must arrive |

`_lossy` and `_reliable` also need [`--enable-ipc`](configuration.md#ipc). See [IPC messages](../integrations/ipc.md).

Large transfers use `stream`, so they never block commands. Several transfers can run at the same time.

## Which signaling carries which channel

| Label | MQTT | WHEP | LiveKit | Cloudflare |
| --- |:---:|:---:|:---:|:---:|
| `command` | ✅ | ❌ | ❌ | ❌ |
| `stream` | ✅ | ❌ | ❌ | ❌ |
| `_lossy` | ✅ | ❌ | ✅ | ❌ |
| `_reliable` | ✅ | ❌ | ✅ | ❌ |

WHEP and Cloudflare carry no DataChannel at all. SFU viewers never get `command` and `stream`, so snapshots, recording control, camera control and file transfer only work over MQTT. You can turn on `--use-mqtt` next to an SFU to keep them.

## Commands

The messages are Protocol Buffers, defined in [packet.proto](https://github.com/mazupo/protocol/blob/main/protos/packet.proto). [client-sdk-js](https://github.com/mazupo/client-sdk-js) wraps them in methods, so most apps never build them by hand.

| Request | What it does | Answer |
| --- | --- | --- |
| `take_snapshot` | Takes one JPEG at the given quality (0 to 100) | The JPEG, on `stream` |
| `control_camera` | Changes an image control, such as brightness | None |
| `start_recording` | Starts on-demand recording | `recording`, with `is_recording: true` and the file path |
| `stop_recording` | Stops on-demand recording | `recording`, with `is_recording: false` and the file path |
| `query_file` | Lists recordings | The file list, on `stream` |
| `transfer_file` | Downloads a file | The file, on `stream` |
| `toggle_tracking` | Turns the tracking overlay on or off. Jetson [sponsor build](../sponsors.md#sponsor-benefits) only. | `toggle_tracking` |
| `disconnect` | Tells the device that the client is leaving | None |

Every request has a `request_id`. The device puts the same id on its answer, so the client knows which request it answers.

See [Recording](../recording/README.md#browsing-recordings) for the details of `query_file`.
