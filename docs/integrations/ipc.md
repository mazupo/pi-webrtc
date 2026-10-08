# IPC Messages

Send data both ways between the browser and a program on the device: AI events, sensor values, or remote-control commands. pi-webrtc passes the messages between a WebRTC DataChannel and a Unix socket on the device.

This works with `--use-mqtt` and `--use-livekit`.

## Two channels

With [`--enable-ipc`](../reference/configuration.md#ipc), pi-webrtc opens two extra channels:

| Channel | Use it for |
|:--:|---|
| Lossy | Messages where old data can be dropped, such as live sensor values |
| Reliable | Messages that must arrive, such as commands |

The client chooses the channel for each message. [client-sdk-js](https://github.com/mazupo/client-sdk-js) uses the reliable channel by default.

> [!NOTE]
> With `--use-livekit`, messages go to everyone in the room.
> ![IPC messages in a LiveKit room](https://github.com/user-attachments/assets/cf88cd29-3717-4178-9e6b-f2f3ce9a0270)

## Try it

1. Start pi-webrtc with `--enable-ipc`:

    ```bash
    ./pi-webrtc --camera=libcamera:0 ... --use-mqtt ... --enable-ipc
    ```

2. On the device, run the [unix_socket_client.py](../../examples/unix_socket_client.py) example. It prints every message that goes through pi-webrtc:

    ```bash
    python3 examples/unix_socket_client.py
    ```

3. In the browser, use `onMessage()` to receive messages, and `sendText()` or `sendData()` to send them.

- Code example: [Send and receive IPC messages](https://mazupo.com/docs/client-sdk-js/guides/ipc-messages)
- Web app: [app.mazupo.com/interaction](https://app.mazupo.com/interaction)

The Unix socket is `/tmp/pi-webrtc-ipc.sock` by default. Change it with `--socket-path`.
