# Sponsors

`pi-webrtc` is open source under [Apache-2.0](../LICENSE). Options marked with
<sup>[\*](#sponsor-benefits)</sup> in the docs are available to sponsors.
Sponsor-only releases are licensed under the EULA included in each package,
not Apache-2.0.

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

## Tiers

Sponsor-only releases come with a monthly sponsorship. One-time sponsorships do not include access.

| Tier | Releases | Use | SFU |
|---|---|---|---|
| **Raspberry Pi** | Raspberry Pi, in the private `sponsor-pi` repository | Non-commercial | Your own |
| **NVIDIA Jetson** | Jetson, including detection and tracking, in the private `sponsor-jetson` repository | Non-commercial | Your own |
| **Commercial** | All sponsor-only releases | Commercial use, including shipping the binary in your products | Your own |
| **Commercial with hosted SFU** | All sponsor-only releases | Same as Commercial | Hosted by the maintainer; traffic billed separately |

Non-commercial means personal, educational or research use, or evaluation. Versions you download while sponsoring stay licensed after you stop. Devices you shipped while on a Commercial tier keep working, but shipping new devices needs an active Commercial tier. Stopping also ends access to new releases and to the hosted SFU.

Monthly sponsors are invited to their tier's private repositories. Prices and sign-up are on [GitHub Sponsors](https://github.com/sponsors/mazupo).

## Commercial / OEM notes

For invoicing outside GitHub Sponsors, larger fleets, dedicated SFU environments, or integration work, commercial terms are arranged separately. Contact **tzu.huan.tai@gmail.com**.
