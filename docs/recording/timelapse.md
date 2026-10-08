# Timelapse

Turn the snapshots from `--record-type=snapshot` into a timelapse video at 30 fps.

## 1. Make the file list

`ffmpeg -f concat` needs one `file '...'` line per image, in order:

```bash
find /mnt/ext_disk/video/20260509/ -type f -iname "*.jpg" | sort | sed "s/^/file '/; s/$/'/" > file_list.txt
```

## 2. Encode the video

```bash
ffmpeg -f concat -safe 0 -i file_list.txt -r 30 -c:v libx264 -pix_fmt yuv420p timelapse.mp4
```

- `-safe 0` allows absolute paths in the list.
- `-r 30` sets the frame rate of the video.
- `-pix_fmt yuv420p` makes it play in browsers and on phones.

## 3. Share it with clients

Put the video in `<record-path>/timelapse/`. Clients can then list it with `QUERY_FILE` and `mode: TIMELAPSE`. See [Browsing recordings](README.md#browsing-recordings).
