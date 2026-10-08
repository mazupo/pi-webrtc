# TURN

Use a TURN server when a peer-to-peer connection fails, for example over 4G or 5G. A TURN server relays the video between the device and the browser.

## When you need TURN

WebRTC first tries to connect the device and the browser directly. A STUN server helps each side find its public address. This works on most home and office networks.

4G and 5G networks often put many users behind one shared public address (carrier-grade NAT). Then a direct connection can fail. You will see this pattern:

- Signaling works: the device log shows that a viewer connected.
- The video never starts, or the connection never leaves "connecting".
- The same setup works on Wi-Fi.

You do **not** need TURN when:

- The device and the viewer are on the same network.
- You use an [SFU](sfu.md). The SFU already relays the stream.
- One side has a public IP address with open ports.

## Run coturn on a server

[coturn](https://github.com/coturn/coturn) is a free TURN server. Run it on a cloud server (VPS) that has a public IP address. It must not sit behind a NAT.

1. Install coturn:

    ```bash
    sudo apt update
    sudo apt install coturn
    ```

2. Edit `/etc/turnserver.conf`. Replace each `<...>` with your own value:

    ```ini
    listening-port=3478
    external-ip=<server-public-ip>
    realm=<your-domain-or-name>
    lt-cred-mech
    user=<username>:<password>
    fingerprint
    min-port=49152
    max-port=65535
    no-cli
    ```

    Use a strong password. Anyone who has it can send traffic through your server.

3. Open these ports in the server's firewall:

    - `3478`, TCP and UDP
    - `49152` to `65535`, UDP. coturn relays the video through these ports.

4. Restart coturn and start it at boot:

    ```bash
    sudo systemctl restart coturn
    sudo systemctl enable coturn
    ```

## Check the server

1. Open the [Trickle ICE](https://webrtc.github.io/samples/src/content/peerconnection/trickle-ice/) test page.
2. Add `turn:<server-public-ip>:3478` with your username and password.
3. Click **Gather candidates**.

You should see a candidate of type `relay`. If not, check the firewall and the password.

## Use it in pi-webrtc

```bash
./pi-webrtc ... \
    --turn-url=turn:<server-public-ip>:3478 \
    --turn-username=<username> \
    --turn-password=<password>
```

Give the same TURN server to the browser too. With [client-sdk-js](https://github.com/mazupo/client-sdk-js), set `turnUrls`, `turnUsername` and `turnPassword`.

pi-webrtc takes a fixed username and password. TURN services that hand out short-lived credentials do not work with it yet.

## If UDP is blocked

Some networks block UDP. coturn also listens on TCP on the same port, so tell pi-webrtc to use TCP:

```bash
--turn-url=turn:<server-public-ip>:3478?transport=tcp
```
