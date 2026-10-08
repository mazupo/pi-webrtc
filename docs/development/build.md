# Build from Source

Build pi-webrtc on the device itself: a Raspberry Pi with Raspberry Pi OS Trixie (64-bit), or a Jetson with JetPack 6.

The CI workflows always have the current steps: [rpi-build.yml](../../.github/workflows/rpi-build.yml) and [jetson-build.yml](../../.github/workflows/jetson-build.yml). If this page and a workflow disagree, follow the workflow.

## 1. Get the source

```bash
git clone --recursive https://github.com/mazupo/pi-webrtc.git
cd pi-webrtc
```

`--recursive` also downloads the submodules. If you cloned without it, run `git submodule update --init`.

## 2. Install the packages

**Raspberry Pi (Trixie):**

```bash
sudo apt update
sudo apt install cmake clang lld build-essential \
    libboost-program-options-dev libyaml-cpp-dev libmosquitto-dev mosquitto-dev \
    libavformat-dev libavcodec-dev libavutil-dev \
    libpulse-dev libasound2-dev libjpeg-dev libcamera-dev
```

**Jetson (JetPack 6):** the clang in Ubuntu 22.04 is too old, so install clang 20 from [apt.llvm.org](https://apt.llvm.org):

```bash
wget https://apt.llvm.org/llvm.sh
chmod +x llvm.sh
sudo ./llvm.sh 20
sudo apt install lld-20 libclang-rt-20-dev
sudo update-alternatives --install /usr/bin/clang clang /usr/bin/clang-20 100
sudo update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-20 100
sudo update-alternatives --install /usr/bin/ld.lld ld.lld /usr/bin/ld.lld-20 100
```

Then install the other packages:

```bash
sudo apt install cmake build-essential nvidia-l4t-jetson-multimedia-api \
    libboost-program-options-dev libyaml-cpp-dev libmosquitto-dev mosquitto-dev \
    libavformat-dev libavcodec-dev libavutil-dev \
    libpulse-dev libasound2-dev libjpeg-dev
```

## 3. Install protoc 33.0

It must match the protobuf version inside libwebrtc:

```bash
curl -sLO https://github.com/protocolbuffers/protobuf/releases/download/v33.0/protoc-33.0-linux-aarch_64.zip
unzip protoc-33.0-linux-aarch_64.zip -d protoc-33.0
sudo cp protoc-33.0/bin/protoc /usr/local/bin/protoc
sudo cp -r protoc-33.0/include/* /usr/local/include/
protoc --version
```

The last command should print `libprotoc 33.0`.

## 4. Install nlohmann/json

```bash
sudo mkdir -p /usr/local/include/nlohmann
sudo curl -L https://raw.githubusercontent.com/nlohmann/json/v3.11.3/single_include/nlohmann/json.hpp \
    -o /usr/local/include/nlohmann/json.hpp
```

## 5. Install libwebrtc

Download the prebuilt `libwebrtc.a` from [libwebrtc-builder](https://github.com/mazupo/libwebrtc-builder/releases). Use the `LIBWEBRTC_VERSION` from the CI workflow:

```bash
LIBWEBRTC_VERSION=7727
wget https://github.com/mazupo/libwebrtc-builder/releases/download/${LIBWEBRTC_VERSION}/libwebrtc-arm64.tar.gz
mkdir -p libwebrtc
tar -xzf libwebrtc-arm64.tar.gz -C libwebrtc

# Remove the headers of an older version, if there are any
sudo rm -rf /usr/local/include/webrtc
sudo mkdir -p /usr/local/include/webrtc
sudo cp -r libwebrtc/include/* /usr/local/include/webrtc/
sudo cp libwebrtc/lib/libwebrtc.a /usr/local/lib/
```

To build libwebrtc yourself, see [libwebrtc-builder](https://github.com/mazupo/libwebrtc-builder#build-process). It lists the exact `gn` arguments.

## 6. Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

The program is `build/pi-webrtc`.

| CMake option | Default | Values |
| --- | --- | --- |
| `-DCMAKE_BUILD_TYPE` | `Debug` | `Debug`, `Release` |
| `-DPLATFORM` | found from the OS | `raspberrypi`, `jetson` |
| `-DBUILD_TEST` | none | Builds one test program instead of pi-webrtc: `whep`, `pulseaudio`, `recorder`, `unix-socket`, `gamepad`, `mqtt`, `livekit`, `openh264`, `v4l2_capturer`, `rtsp_capturer`, `v4l2_encoder`, `v4l2_decoder`, `v4l2_scaler`, `jetson_encoder`, `jetson_decoder`, `jetson_scaler`, `libargus`, `libcamera` |

CMake finds the platform from `/etc/nv_tegra_release` (Jetson) or `/usr/bin/raspi-config` (Raspberry Pi).

## 7. Run it

```bash
./build/pi-webrtc --camera=libcamera:0 --uid=my-pi --no-audio --use-whep
```

Then follow [Getting Started](../getting-started/README.md) from the "Watch in your browser" step.

## Code style

The CI checks the C++ code with clang-format 20 and the [.clang-format](../../.clang-format) file. Format your changes before you open a pull request:

```bash
find src test -name '*.cpp' -o -name '*.h' | xargs clang-format -i
```
