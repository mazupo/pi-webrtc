# Run as a Service

Start pi-webrtc when the device boots, and restart it if it stops. This page uses systemd.

## 1. Run PulseAudio as a system service

Skip this step if you use `--no-audio`. A service has no user session, so PulseAudio must run for the whole system ([reference](https://www.freedesktop.org/wiki/Software/PulseAudio/Documentation/User/SystemWide/)).

1. Install it:

    ```bash
    sudo apt install pulseaudio
    ```

2. Create `/etc/systemd/system/pulseaudio.service`:

    ```ini
    [Unit]
    Description=PulseAudio Daemon
    After=rtkit-daemon.service systemd-udevd.service dbus.service

    [Service]
    Type=simple
    ExecStart=/usr/bin/pulseaudio --system --disallow-exit --disallow-module-loading
    Restart=always
    RestartSec=10

    [Install]
    WantedBy=multi-user.target
    ```

3. Stop clients from starting their own PulseAudio:

    ```bash
    echo 'autospawn = no' | sudo tee -a /etc/pulse/client.conf > /dev/null
    ```

4. Give access, enable the service, and reboot:

    ```bash
    sudo adduser root pulse-access
    sudo systemctl enable pulseaudio.service
    sudo reboot
    ```

## 2. Run pi-webrtc at boot

1. Create `/etc/systemd/system/pi-webrtc.service`. Change `WorkingDirectory` and `ExecStart` to your own path and options:

    ```ini
    [Unit]
    Description=pi-webrtc camera stream
    After=network-online.target pulseaudio.service

    [Service]
    Type=simple
    WorkingDirectory=/path/to
    ExecStart=/path/to/pi-webrtc --config=/path/to/config.yml
    Restart=always
    RestartSec=10

    [Install]
    WantedBy=multi-user.target
    ```

    A [config file](../reference/configuration.md#config-file) keeps `ExecStart` short. You can also put all the options on the `ExecStart` line.

2. Enable and start it:

    ```bash
    sudo systemctl daemon-reload
    sudo systemctl enable pi-webrtc.service
    sudo systemctl start pi-webrtc.service
    ```

3. Check that it runs, and read its log:

    ```bash
    systemctl status pi-webrtc.service
    journalctl -u pi-webrtc.service -f
    ```

`Restart=always` also helps with [RTSP input](../media/rtsp.md): pi-webrtc exits when the RTSP stream changes its size or codec, and systemd starts it again.
