# Set up the Mosquitto broker

On the device which acts as the Mosquitto server.

* Install `mosquitto`
    ```bash
    sudo apt install mosquitto mosquitto-clients
    ```

* Configure the broker in `/etc/mosquitto/mosquitto.conf` likes
    ```apacheconf 
    pid_file /run/mosquitto/mosquitto.pid

    persistence true
    persistence_location /var/lib/mosquitto/

    log_dest file /var/log/mosquitto/mosquitto.log
    password_file /etc/mosquitto/p1.txt
    include_dir /etc/mosquitto/conf.d

    # this will listen for mqtt on tcp
    listener 1883
    allow_anonymous false

    # this will expect websockets connections
    listener 8083
    protocol websockets
    ```

* Restart the service by using the following command
    ```bash
    sudo systemctl restart mosquitto.service
    ```

# Nginx reverse proxy (Optional)
Use HTTP reverse proxy with connection upgrade to WebSocket.
```apacheconf
location /mqtt {
    proxy_pass http://127.0.0.1:8083;
    proxy_http_version 1.1;
    proxy_set_header X-Real-IP $remote_addr;
    proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
    proxy_set_header Host $host;
    proxy_set_header Upgrade $http_upgrade;
    proxy_set_header Connection "Upgrade";
}
```