# Recording

- [What and When](#what-and-when)
- [Files on Disk](#files-on-disk)
- [Rotation](#rotation)
- [On-demand Recording](#on-demand-recording)
- [Browsing Recordings](#browsing-recordings)
- [Storage Setup](#storage-setup)
- [Making a Timelapse](#making-a-timelapse)

## What and When

Recording is controlled by two independent options.
[`--record-type`](CONFIGURATION.md#recording) decides *what* is captured and
`--record-mode` decides *when*:

| | `--record-type=video` | `--record-type=snapshot` | `--record-type=both` (default) |
|---|---|---|---|
| **`--record-mode=background`** | Continuous MP4 files | Periodic JPEGs | Both |
| **`--record-mode=on-demand`** | MP4 files while a client asks | Periodic JPEGs while a client asks | Both |
| **`--record-mode=both`** (default) | Two recorders, writing to separate directories | | |

Nothing is recorded until a path is set. The background recorder needs `--record-path`, and
the on-demand recorder needs `--record-ondemand-path` — which defaults to
`<record-path>/on-demand/` when you leave it out. Both must be absolute paths, and the
recorder stays off if the directory cannot be created.

```bash
/path/to/pi-webrtc ... --record-path=/mnt/ext_disk/video
```

| | Format |
|:--:|:--:|
| Video | H264 |
| Audio | AAC |
| Snapshot / thumbnail | JPEG |

## Files on Disk

Files are bucketed by date and hour, and named for the moment they start:

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

Every MP4 gets a matching `.jpg` preview taken from the video, which is what makes the
thumbnail grid in a client cheap to draw. `--file-duration` sets how long each file runs — 60
seconds by default — and doubles as the interval between snapshots in `snapshot` mode.
`--jpeg-quality` applies to snapshots and thumbnails alike.

If `pi-webrtc` is killed or loses power, the file in progress remains playable up to about its last second. This requires **FFmpeg 7.1 or later**.

## Rotation

A background thread wakes every 60 seconds and checks the free space on the recording volume.
While less than **400 MB** is free it deletes the oldest hour's files, one at a time, until it
is back above the threshold. MP4 and `.jpg` files are removed as a pair, and date and hour
directories are cleaned up once they empty out.

This means the recording volume settles at "as much history as fits" rather than filling up
and stopping. Give it a dedicated disk or image file if you don't want it competing with the
rest of the system — see [Storage Setup](#storage-setup).

## On-demand Recording

With `--record-mode=on-demand` (or `both`), a connected client starts and stops recording over
the DataChannel rather than the device recording continuously. Useful when you want footage of
events, not of everything.

| Command | Payload | Response |
|---|---|---|
| `START_RECORDING` | — | `RecordingResponse { is_recording: true, filepath }` |
| `STOP_RECORDING` | — | `RecordingResponse { is_recording: false, filepath }` |

The response carries the path of the file being written, so the client can request it
afterwards with `TRANSFER_FILE`.

These recordings land under `--record-ondemand-path`, kept separate from the background
recordings so that rotation and browsing treat them independently.

## Browsing Recordings

Clients list and fetch recordings over the same DataChannel. `QUERY_FILE` returns metadata —
path, duration, and a downscaled base64 JPEG thumbnail — and `TRANSFER_FILE` streams the file
itself in chunks.

`QueryFileRequest` takes a `type` and a `parameter`:

| Type | Parameter | Returns |
|---|---|---|
| `LATEST_FILE` | — | The most recent complete file |
| `BEFORE_FILE` | A filename, e.g. `20260719_103000.mp4` | Up to 8 files older than that one |
| `BEFORE_TIME` | A timestamp, e.g. `20260719_103000` | The file covering or preceding that time |

The `mode` field selects which set of files to search:

- `RECORDING` — the recordings under `--record-path`, in the `date/hour` layout above.
- `TIMELAPSE` — a flat `<record-path>/timelapse` directory, for timelapse videos you have
  assembled yourself.

Also available on the DataChannel: `TAKE_SNAPSHOT` for a one-off JPEG at a requested quality,
and `CONTROL_CAMERA` for runtime image controls.

## Storage Setup

### Using a USB drive

1. Find the drive:

    ```bash
    sudo fdisk -l
    ```

2. Mount it at `/mnt/ext_disk` with `autofs`
   ([reference](https://wiki.gentoo.org/wiki/AutoFS)):

    ```bash
    sudo apt-get install autofs
    echo '/- /etc/auto.usb --timeout=5' | sudo tee -a /etc/auto.master > /dev/null
    echo '/mnt/ext_disk -fstype=auto,nofail,nodev,nosuid,noatime,async,umask=000 :/dev/sda1' | sudo tee -a /etc/auto.usb > /dev/null
    sudo systemctl restart autofs
    ```

3. Point `--record-path` at it:

    ```bash
    /path/to/pi-webrtc ... --record-path=/mnt/ext_disk/video
    ```

### Using a virtual disk file

No USB drive needed — cap recording at a fixed size by giving it a disk image.

1. Create a 16 GB image:

    ```bash
    dd if=/dev/zero of=/home/pi/16gb.img bs=1M count=16384
    ```

2. Format it:

    ```bash
    mkfs.ext4 /home/pi/16gb.img
    ```

3. Mount it:

    ```bash
    mkdir -p /home/pi/limited_folder
    sudo mount -o loop /home/pi/16gb.img /home/pi/limited_folder
    ```

4. Make it writable:

    ```bash
    sudo chmod 777 /home/pi/limited_folder
    ```

5. Record into it:

    ```bash
    /path/to/pi-webrtc ... --record-path=/home/pi/limited_folder/
    ```

6. Optionally mount it at boot by adding this to `/etc/fstab`:

    ```
    /home/pi/16gb.img  /home/pi/limited_folder  ext4  loop,noatime  0  2
    ```

> [!CAUTION]
> A mistake in `/etc/fstab` can stop the system booting. Test the mount by hand first.

## Making a Timelapse

Turn the `.jpg` files from `snapshot` mode into a 30 fps MP4.

**1. Build the file list.** `ffmpeg -f concat` wants one `file '...'` per line, in order:

```bash
find /mnt/ext_disk/video/20260509/ -type f -iname "*.jpg" | sort | sed "s/^/file '/; s/$/'/" > file_list.txt
```

**2. Encode it:**

```bash
ffmpeg -f concat -safe 0 -i file_list.txt -r 30 -c:v libx264 -pix_fmt yuv420p timelapse.mp4
```

- `-safe 0` allows absolute paths in the list.
- `-r 30` sets the output frame rate.
- `-pix_fmt yuv420p` keeps it playable in browsers and on phones.

Put the result in `<record-path>/timelapse/` and clients can browse it through
`QUERY_FILE` with `mode: TIMELAPSE`.
