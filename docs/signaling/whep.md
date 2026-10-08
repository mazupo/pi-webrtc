# WHEP

WHEP (WebRTC-HTTP Egress Protocol) lets any WHEP player play the stream from a URL. pi-webrtc serves WHEP itself, so you need no broker and no account.

![WHEP signaling](https://github.com/user-attachments/assets/6b999188-f97c-4bcb-b081-85fa7c63dcaf)

## Start pi-webrtc with WHEP

```bash
./pi-webrtc --camera=libcamera:0 --uid=my-pi --no-audio --use-whep --whep-port=8080
```

The stream URL is `http://<device-ip>:8080`. The default port is `8080`.

## Players

| Player | How to use it |
|---|---|
| [app.mazupo.com/whep](http://app.mazupo.com/whep) | Enter the stream URL as the **WHEP URL**, and connect. |
| [Eyevinn webrtc-player](https://github.com/Eyevinn/webrtc-player) | Open the [demo player](https://tzuhuantai.github.io/webrtc-player/demo/). Keep the adapter on **WHEP**, and play the stream URL. |
| [Home Assistant](../integrations/home-assistant.md) | Add a WebRTC Camera card with the stream URL. |

## HTTP and HTTPS

pi-webrtc serves WHEP over plain HTTP. A player page loaded over `https://` then makes a "mixed content" request:

- Chrome allows it for private addresses, such as `192.168.x.x`, after you click **Allow** on its local network prompt.
- Other browsers may block it. Then open the player over `http://`, for example `http://app.mazupo.com/whep`.

## Limits

- WHEP carries no DataChannel. Snapshots, recording control, file transfer and IPC do not work over WHEP. Use [MQTT](mqtt.md) for them.
- Each viewer gets its own connection and its own encoder on the device. For many viewers, use an [SFU](sfu.md).
