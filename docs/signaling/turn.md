# Install the [coturn](https://github.com/coturn/coturn) (Optional)

If the cellular network is used, the `coturn` is required because the 5G NAT setting by ISP may block p2p. Or try some cloud service that provides TURN server.

1. Install
    ```bash
    sudo apt update
    sudo apt install coturn
    sudo systemctl stop coturn.service
    ```
2. Edit config `sudo nano /etc/turnserver.conf`, uncomment or modify below options
    ```ini
    listening-port=3478
    listening-ip=192.168.x.x
    relay-ip=192.168.x.x
    external-ip=174.127.x.x/192.168.x.x
    #verbose
    lt-cred-mech
    user=webrtc:webrtc
    realm=greenhouse
    no-tls
    no-dtls
    syslog
    no-cli
    ```
3. Set the port `3478` forwarding on the router/modem.
4. Start the service, `sudo systemctl start coturn.service`
