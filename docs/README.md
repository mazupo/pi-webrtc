# pi-webrtc Documentation

pi-webrtc streams a camera from a Raspberry Pi or an NVIDIA Jetson to a browser over WebRTC. These docs show you how to set it up, how viewers connect, and how to use each feature.

## Start here

| I want to... | Read |
|---|---|
| See video in my browser for the first time | [Getting Started](getting-started/README.md) |
| Use an IP camera or an RTSP stream | [RTSP cameras](media/rtsp.md) |
| Watch from outside my home network | [MQTT](signaling/mqtt.md) |
| Let many people watch at the same time | [SFU](signaling/sfu.md) |
| Look up a command-line option | [Configuration](reference/configuration.md) |

## Sections

- [Getting Started](getting-started/README.md): install pi-webrtc and see video on a Raspberry Pi or a Jetson.
- [Video & Audio](media/README.md): camera types, RTSP input, encoding, and two-way audio.
- [Signaling](signaling/README.md): how viewers connect, with WHEP, MQTT, SFU and TURN.
- [Recording](recording/README.md): save video and snapshots on the device.
- [Integrations](integrations/README.md): send data both ways, gamepads, Home Assistant and ROS.
- [Deployment](deployment/README.md): start on boot, run several cameras, and tune performance.
- [Reference](reference/README.md): every option and the DataChannel protocol.
- [Development](development/README.md): build from source and learn how the code works.
- [Sponsors](sponsors.md): sponsor builds and their extra features.

## For AI tools

The full documentation is also available as plain text:

- [mazupo.com/llms.txt](https://mazupo.com/llms.txt): a list of every page.
- [mazupo.com/llms-full.txt](https://mazupo.com/llms-full.txt): every page in one file.

## Writing these docs

Readers come from all over the world, and many do not speak English as a first language. Please keep the docs simple:

- Use short sentences, with one idea in each sentence.
- Use common words. Do not use idioms.
- Give each step one action, and a command that people can copy.
- Start each page with one or two sentences that say what the page is for.
- Link to another page instead of repeating it.
- In each folder, `README.md` is the overview. The order of its links is the order of the pages on [mazupo.com](https://mazupo.com/docs).
