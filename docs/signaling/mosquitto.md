# Self-hosted Mosquitto

Run your own MQTT broker instead of a cloud service. [Mosquitto](https://mosquitto.org) is a small and popular MQTT broker for Linux.

pi-webrtc and the browser connect to the broker in different ways:

| Who connects | Protocol | Port in this guide |
|---|---|---|
| pi-webrtc on the device | MQTT | `1883` |
| The browser | MQTT over WebSocket | `8083`, or `443` through a reverse proxy |

## 1. Install Mosquitto

On the server:

```bash
sudo apt update
sudo apt install mosquitto mosquitto-clients
```

## 2. Create a user

```bash
sudo mosquitto_passwd -c /etc/mosquitto/passwd <username>
sudo chown mosquitto:mosquitto /etc/mosquitto/passwd
```

The first command asks for a password.

## 3. Add the listeners

Create `/etc/mosquitto/conf.d/pi-webrtc.conf`:

```apacheconf
# For pi-webrtc
listener 1883

# For browsers
listener 8083
protocol websockets

allow_anonymous false
password_file /etc/mosquitto/passwd
```

Restart Mosquitto:

```bash
sudo systemctl restart mosquitto
```

## 4. Test the broker

In one terminal, subscribe:

```bash
mosquitto_sub -h localhost -p 1883 -u <username> -P <password> -t 'test/#' -v
```

In a second terminal, publish:

```bash
mosquitto_pub -h localhost -p 1883 -u <username> -P <password> -t test/hello -m hi
```

The first terminal should print `test/hello hi`.

## 5. Add TLS for browsers

The [web app](https://app.mazupo.com) uses HTTPS, so the browser can only use a secure WebSocket (`wss://`). Put the broker behind a reverse proxy that has a TLS certificate. For example, with nginx:

```nginx
location /mqtt {
    proxy_pass http://127.0.0.1:8083;
    proxy_http_version 1.1;
    proxy_set_header Upgrade $http_upgrade;
    proxy_set_header Connection "Upgrade";
    proxy_set_header Host $host;
    proxy_set_header X-Real-IP $remote_addr;
    proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
}
```

The browser then connects to port `443` with the path `/mqtt`.

## 6. Connect pi-webrtc

```bash
./pi-webrtc ... \
    --use-mqtt \
    --mqtt-host=<broker-host> \
    --mqtt-port=1883 \
    --mqtt-username=<username> \
    --mqtt-password=<password>
```

If the device connects to the broker over the internet, use TLS on port `8883` instead of `1883`. See the [Mosquitto TLS guide](https://mosquitto.org/man/mosquitto-tls-7.html).

## The client library

pi-webrtc uses the Mosquitto client library from your OS. The release needs `libmosquitto1`, and building from source needs `libmosquitto-dev`. Both come from `apt`, so you do not need to build Mosquitto yourself.
