# Build the project

## Preparation
1. Download the prebuilt `libwebrtc.a` from [libwebrtc-builder](https://github.com/mazupo/libwebrtc-builder/releases) and install it to `/usr/local`. To compile it yourself instead, follow [BUILD_WEBRTC](BUILD_WEBRTC.md).
    ```bash
    LIBWEBRTC_VERSION=7727
    wget https://github.com/mazupo/libwebrtc-builder/releases/download/${LIBWEBRTC_VERSION}/libwebrtc-arm64.tar.gz
    mkdir -p libwebrtc
    tar -xzf libwebrtc-arm64.tar.gz -C libwebrtc

    # Remove the headers of a previously installed version, if any
    sudo rm -rf /usr/local/include/webrtc
    sudo mkdir -p /usr/local/include/webrtc
    sudo cp -r libwebrtc/include/* /usr/local/include/webrtc/
    sudo cp libwebrtc/lib/libwebrtc.a /usr/local/lib/
    ```
2. Prepare the MQTT development library.
    * Follow [BUILD_MOSQUITTO](BUILD_MOSQUITTO.md) to compile `mosquitto`.
    * Install the lib from official repo [[tutorial](https://repo.mosquitto.org/debian/README.txt)]. (recommended)
3. Install essential packages
    ```bash
    sudo apt install cmake clang clang-format lld mosquitto-dev libboost-program-options-dev libavformat-dev libavcodec-dev libavutil-dev libswscale-dev libpulse-dev libasound2-dev libjpeg-dev libcamera-dev libmosquitto-dev
    ```
4. Install clang-20 and lld-20 (or newer versions). Set them as default using `update-alternatives`:
    ```bash
    wget https://apt.llvm.org/llvm.sh
    chmod +x llvm.sh
    sudo ./llvm.sh 20
    sudo apt install lld-20 clang-format-20
    sudo update-alternatives --install /usr/bin/clang clang /usr/bin/clang-20 100
    sudo update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-20 100
    sudo update-alternatives --install /usr/bin/ld.lld ld.lld /usr/bin/ld.lld-20 100
    sudo update-alternatives --install /usr/bin/clang-format clang-format /usr/bin/clang-format-20 100
    ```
5. Install Protobuf compiler (protoc) v33.0 to match the protobuf bundled in libwebrtc.a.
    ```bash
    # Download pre-built protoc v33.0 (adjust the URL for your architecture)
    # For arm64:
    curl -sLO https://github.com/protocolbuffers/protobuf/releases/download/v33.0/protoc-33.0-linux-aarch_64.zip

    unzip protoc-33.0-linux-aarch_64.zip -d protoc-33.0

    # Install protoc binary and well-known proto includes
    sudo cp protoc-33.0/bin/protoc /usr/local/bin/protoc
    sudo cp -r protoc-33.0/include/* /usr/local/include/

    # Verify installation
    protoc --version  # should print "libprotoc 33.0"
    ```
6. Copy the [nlohmann/json.hpp](https://github.com/nlohmann/json/blob/develop/single_include/nlohmann/json.hpp) to `/usr/local/include`
    ```bash
    sudo mkdir -p /usr/local/include/nlohmann
    sudo curl -L https://raw.githubusercontent.com/nlohmann/json/develop/single_include/nlohmann/json.hpp -o /usr/local/include/nlohmann/json.hpp
    ```

## Compile and run

| <div style="width:200px">Command line</div> | Default     | Options      |
| --------------------------------------------| ----------- | ------------ |
| -DPLATFORM         | raspberrypi            | jetson, raspberrypi        |
| -DBUILD_TEST       |                        | whep, recorder, mqtt, v4l2_capture, v4l2_encoder, v4l2_decoder, v4l2_scaler, unix-socket, libcamera, libargus |
| -DCMAKE_BUILD_TYPE | Debug                  | Debug, Release             |

Build on raspberry pi and it'll output a `pi-webrtc` file in `/build`.
```bash
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j
```

Run `pi-webrtc` to start the service.
```bash
./pi-webrtc --camera=libcamera:0 --fps=30 --width=1280 --height=720 --use-mqtt --mqtt-host=<hostname> --mqtt-port=1883 --mqtt-username=<username> --mqtt-password=<password> --hw-accel
```
