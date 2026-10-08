# Performance

Measure where the delay comes from, and make the device faster.

## Measure the delay

`--latency-trace` measures each frame from the sensor to WebRTC. It prints the median (p50), p95 and maximum time of each step every few seconds. It works in release builds.

```bash
./pi-webrtc ... --latency-trace --latency-trace-interval=5
```

## General tips

- Use `--hw-accel`. pi-webrtc then uses the hardware decoder, scaler and encoder when the board has them, and moves frames without copies.
- For a USB camera, use `mjpeg` or `h264` instead of an uncompressed format. See [USB cameras](../media/usb-camera.md).
- Ask for the size and frame rate that you need, not the largest one. Check the [bandwidth limits](../media/csi-camera.md#bandwidth-limits) of CSI cameras.

## Jetson: speed up the VIC and NVENC clocks

On a Jetson, the VIC (image copies) and NVENC (encoding) engines can run at low clock speeds under the default `tegra_wmark` governor. This can add a lot of delay. `jetson_clocks` and `nvpmodel MAXN` do not raise the speed of these engines.

For example, on an Orin NX at 1080p and 60 fps, the `performance` governor cut the delay on the device from about **36.5 ms** to **24 ms** in our test.

1. Check the current state:

    ```bash
    for d in /sys/class/devfreq/*vic* /sys/class/devfreq/*nvenc*; do
        echo "$d: $(cat $d/governor) $(cat $d/cur_freq) / $(cat $d/max_freq)"
    done
    ```

2. Use the `performance` governor until the next boot:

    ```bash
    echo performance | sudo tee /sys/class/devfreq/15340000.vic/governor
    echo performance | sudo tee /sys/class/devfreq/154c0000.nvenc/governor
    ```

### Keep it after a reboot

The governor resets at every boot. Create `/etc/systemd/system/tegra-mm-perf.service`:

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

The `*vic*` and `*nvenc*` patterns make the service work on Jetson modules with different device addresses.

> [!NOTE]
> `performance` keeps each engine at its highest speed while it is on. This uses more power and makes more heat. If that is too much, raise only the lowest speed instead (`echo 614400000 | sudo tee /sys/class/devfreq/15340000.vic/min_freq`). You can also try the `nvhost_podgov` governor, which reacts better to short bursts of work than `tegra_wmark`.
