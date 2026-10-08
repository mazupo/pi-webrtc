# Signaling

Before video can flow, the browser and the device must exchange connection details (an SDP offer and answer, and ICE candidates). This step is called signaling. pi-webrtc supports three ways to do it, and you can turn on more than one at the same time.

| | What it needs | Good for |
|---|---|---|
| [WHEP](whep.md) | Nothing extra. The device serves it. | Watching on the same network, with a standard player |
| [MQTT](mqtt.md) | An MQTT broker | Watching from anywhere, peer-to-peer, with device control |
| [SFU](sfu.md) | A LiveKit server, or Cloudflare Realtime | Many viewers at the same time |

If none of them is on, pi-webrtc exits, because nobody could reach it. Every option is listed in [Configuration](../reference/configuration.md#signaling).

## Pages

- [WHEP](whep.md): play the stream from a URL. No server needed.
- [MQTT](mqtt.md): connect from anywhere through an MQTT broker.
- [SFU](sfu.md): send one stream and let many people watch.
- [TURN](turn.md): connect when peer-to-peer fails, for example over 4G or 5G.
- [Self-hosted Mosquitto](mosquitto.md): run your own MQTT broker.
