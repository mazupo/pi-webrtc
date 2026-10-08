# Sponsors

`pi-webrtc` is open source under [Apache-2.0](../LICENSE). Options marked with
<sup>[\*](#sponsor-benefits)</sup> in the docs are available to sponsors.

## Why sponsor

Sponsorship supports the time spent keeping `pi-webrtc` working: new libwebrtc releases, new Raspberry Pi and Jetson OS versions, and fixes reported from real deployments.

## Sponsor benefits

Sponsors get access to sponsor-only releases with additional features:

| Feature | Platform | Description |
|---|---|---|
| Object detection | Jetson | YOLO on TensorRT, drawn onto the stream. See [flags](reference/configuration.md#object-detection-and-tracking). |
| Object tracking | Jetson (DeepStream) | NvDCF or DeepSORT via NvMOT, toggled at runtime. |
| Single-process multi-camera | All | All cameras in one process and one [YAML config](deployment/multi-camera.md#one-process-for-all-cameras), sharing one `uid` and WHEP port. Running one process per camera, see [multiple cameras](deployment/multi-camera.md). |
| Direct LiveKit | All | Signs LiveKit tokens on-device, no token server needed. |
| Direct Cloudflare Realtime | All | Talks to Cloudflare directly, no device API relay. |

## Tiers

| Tier | Includes |
|---|---|
| **Raspberry Pi** | Sponsor-only releases for Raspberry Pi and the private `sponsor-pi` repository. |
| **NVIDIA Jetson** | Sponsor-only releases for Jetson, including detection and tracking, and the private `sponsor-jetson` repository. |
| **All access** | All sponsor-only releases, all private sponsor repositories, and priority issue response. |

Sponsors are invited to the private repositories automatically. See [GitHub Sponsors](https://github.com/sponsors/mazupo).

## Commercial / OEM notes

For products you ship, larger fleets, dedicated SFU environments, or integration work, commercial terms are arranged separately. Contact **tzu.huan.tai@gmail.com**.
