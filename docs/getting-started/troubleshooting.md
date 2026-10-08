# Troubleshooting

Fixes for the most common problems. If you are still stuck, [open an issue](https://github.com/mazupo/pi-webrtc/issues). Include the full command, the log, and your device and OS versions.

## Common errors

| What you see | What to do |
|---|---|
| `cannot execute binary file`, or a missing `.so` library | The release does not match your OS. Check your OS with `cat /etc/os-release` (Pi) or `head -n 1 /etc/nv_tegra_release` (Jetson). Then download the matching file. |
| `E: Unable to locate package libavformat61` | Your Pi does not run Trixie. Package names change with each OS release. |
| `--uid is required.` | Add `--uid=<any-name>`. Every mode needs it. |
| `No signaling service is running.` | Turn on at least one of `--use-whep`, `--use-mqtt`, `--use-livekit` or `--use-cloudflare`. |
| The WHEP player shows nothing | Open the player with `http://`, not `https://`. If the browser asks to access devices on your local network, click **Allow**. |
| A USB camera shows no picture, or a broken picture | Use a format that the camera supports. List the formats with `v4l2-ctl -d /dev/videoN --list-formats-ext`, then set `--v4l2-format`, for example `--v4l2-format=mjpeg`. |
| `libcamera:0` fails on a Jetson | A Jetson has no libcamera. Use `libargus:<id>` for a CSI camera, or `v4l2:<id>` for a USB camera. |
| `No hardware encoder found; WebRTC uses software encoding.` | This is normal on a Pi 5 and a Jetson Orin Nano. They have no hardware video encoder. |
| `No hardware scaler found; scaling frames in software.` | This is normal on a Pi 5. |
| `MQTT connect failed: ...` | Check the broker host, port, username and password. The device uses the MQTT port with TLS (`8883` on EMQX Serverless), not the WebSocket port. |
| With MQTT, the device log looks fine, but the browser shows nothing | The browser must use the WebSocket port and path (`8084` and `/mqtt` on EMQX Serverless). It must also use exactly the same `uid` as the device. |

## Checks

**See every option.** The binary prints every flag and its default value:

```bash
./pi-webrtc -h
```

**List the cameras.** For a CSI camera on a Pi:

```bash
rpicam-hello --list-cameras
```

For a USB camera, and its formats:

```bash
v4l2-ctl --list-devices
v4l2-ctl -d /dev/video0 --list-formats-ext
```

**Check the WHEP port.** Run this on another computer in the same network. Any answer other than "connection refused" means that pi-webrtc is serving:

```bash
curl -v http://<device-ip>:8080
```

**Check the MQTT broker without pi-webrtc.** This separates login problems from everything else. Install the tools with `sudo apt install mosquitto-clients`:

```bash
mosquitto_sub -h xxxxxxxx.ala.us-east-1.emqxsl.com -p 8883 --capath /etc/ssl/certs/ \
  -u your-username -P your-password -t 'my-pi/#' -v
```

**Try a smaller stream.** Use `--width=640 --height=480 --fps=30 --no-audio`. If a smaller stream works, the problem is bandwidth or CPU, not the connection.

**Jetson load and clocks.** This shows how busy each engine is, for example `VIC 36%@115`:

```bash
sudo tegrastats --interval 500
```
