# Recording

pi-webrtc can save MP4 video files and JPEG snapshots on the device, all the time or only when a viewer asks. Recording uses the full camera resolution, even when WebRTC lowers the streaming resolution.

## Pages

- [Storage](storage.md): record to a USB drive, or to a disk image with a fixed size.
- [Timelapse](timelapse.md): turn snapshots into a timelapse video.

## What and when

Two options control recording. [`--record-type`](../reference/configuration.md#recording) sets *what* to save, and `--record-mode` sets *when*:

| | `--record-type=video` | `--record-type=snapshot` | `--record-type=both` (default) |
|---|---|---|---|
| **`--record-mode=background`** | MP4 files, all the time | JPEGs at a fixed interval | Both |
| **`--record-mode=on-demand`** | MP4 files while a viewer asks | JPEGs while a viewer asks | Both |
| **`--record-mode=both`** (default) | Two recorders, in two directories | | |

Nothing is saved until you set a path. The background recorder needs `--record-path`. The on-demand recorder needs `--record-ondemand-path`, which defaults to `<record-path>/on-demand/`. Both must be absolute paths. If pi-webrtc cannot create the directory, that recorder stays off.

```bash
./pi-webrtc ... --record-path=/mnt/ext_disk/video
```

| | Format |
|:--:|:--:|
| Video | H.264 |
| Audio | AAC |
| Snapshot and thumbnail | JPEG |

## Files on disk

Files are sorted into folders by date and hour, and named after the time they start:

```
/mnt/ext_disk/video/
└── 20260730/
    ├── 09/
    │   ├── 20260730_090000.mp4
    │   ├── 20260730_090000.jpg
    │   ├── 20260730_090100.mp4
    │   └── 20260730_090100.jpg
    └── 10/
        └── ...
```

Every MP4 file has a `.jpg` preview from the same video. Clients use it to draw a thumbnail grid quickly.

- `--file-duration` sets the length of each video file: 60 seconds by default. In `snapshot` mode, it is the time between snapshots.
- `--jpeg-quality` sets the quality of snapshots and thumbnails.

If pi-webrtc stops or the power goes off, the current file still plays, up to about its last second. This needs **FFmpeg 7.1 or newer**.

## Rotation

Every 60 seconds, pi-webrtc checks the free space on the recording disk. While less than **400 MB** is free, it deletes the oldest hour of files. MP4 and `.jpg` files are deleted together, and empty date and hour folders are removed.

So the disk keeps as much history as fits, and recording never stops because the disk is full. To keep recording away from the system disk, give it its own disk. See [Storage](storage.md).

## On-demand recording

With `--record-mode=on-demand` (or `both`), a connected viewer starts and stops recording over the DataChannel. This is useful when you want video of events, not of everything.

| Command | Payload | Response |
|---|---|---|
| `START_RECORDING` | none | `RecordingResponse { is_recording: true, filepath }` |
| `STOP_RECORDING` | none | `RecordingResponse { is_recording: false, filepath }` |

The response contains the path of the file. The client can download it later with `TRANSFER_FILE`.

On-demand files are saved under `--record-ondemand-path`, apart from the background recordings. So rotation and browsing treat them separately.

## Browsing recordings

Clients list and download recordings over the same DataChannel. `QUERY_FILE` returns file details: the path, the length, and a small base64 JPEG thumbnail. `TRANSFER_FILE` sends the file itself, in chunks.

`QueryFileRequest` takes a `type` and a `parameter`:

| Type | Parameter | Returns |
|---|---|---|
| `LATEST_FILE` | none | The newest complete file |
| `BEFORE_FILE` | A file name, for example `20260719_103000.mp4` | Up to 8 files older than that one |
| `BEFORE_TIME` | A time, for example `20260719_103000` | The file at that time, or the one before it |

The `mode` field selects the set of files:

- `RECORDING`: the recordings under `--record-path`, in the date and hour folders above.
- `TIMELAPSE`: the files in `<record-path>/timelapse`, for timelapse videos that you made yourself. See [Timelapse](timelapse.md).

The DataChannel also has `TAKE_SNAPSHOT`, for one JPEG at a chosen quality, and `CONTROL_CAMERA`, to change the image controls while running. See [DataChannels](../reference/datachannels.md).
