# Home Assistant

Show the camera on a Home Assistant dashboard. Home Assistant plays the stream over [WHEP](../signaling/whep.md) with the WebRTC Camera integration.

## 1. Install Home Assistant

Follow the official [installation guide](https://www.home-assistant.io/installation/).

## 2. Install HACS

HACS installs community integrations, such as WebRTC Camera. Follow the official [HACS guide](https://www.home-assistant.io/blog/2024/08/21/hacs-the-best-way-to-share-community-made-projects/#how-to-install).

![Installing HACS](https://github.com/user-attachments/assets/d28abff7-53d0-43d3-b225-7305c54a800e)

## 3. Install WebRTC Camera

Go to `Home Assistant` → `HACS` → `Integrations`, and search for `WebRTC Camera`. Install it, then restart Home Assistant.

![WebRTC Camera in HACS](https://github.com/user-attachments/assets/17994718-f222-42e7-a0a7-531992bcbb34)

## 4. Add the integration

Go to `Settings` → `Devices & Services` → `Add Integration`, and add WebRTC Camera.

![Adding the integration](https://github.com/user-attachments/assets/d8ba49e6-7de3-4144-88f7-3f066f4ed393)

## 5. Start pi-webrtc with WHEP

```bash
./pi-webrtc --camera=libcamera:0 \
    --uid=home-pi-4b \
    --fps=30 \
    --width=1280 \
    --height=720 \
    --use-whep \
    --whep-port=8080
```

The stream is now at `http://<device-ip>:8080`, for example `http://192.168.4.35:8080`.

## 6. Add the card to a dashboard

Go to `Dashboard` → `Edit Dashboard` → `Add Card` → `WebRTC Camera`.

![Adding the card](https://github.com/user-attachments/assets/5bc68138-d5a2-481e-a187-0d845aeca463)

Enter the stream URL, and save:

```yaml
type: custom:webrtc-camera
url: webrtc:http://192.168.4.35:8080
```

![The camera on a dashboard](https://github.com/user-attachments/assets/87d61efc-7107-41a0-bcb9-12378a904021)
