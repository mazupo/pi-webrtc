# Build the [native WebRTC](https://webrtc.github.io/webrtc-org/native-code/development/) lib (`libwebrtc.a`)

## Preparations
- Install Dependencies

    ```bash
    sudo apt install python3 build-essential libncurses5 libx11-dev
    ```

- Install Chromium `depot_tools`

    ``` bash
    sudo apt install git
    git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git
    export PATH=/home/pi/depot_tools:$PATH
    echo 'PATH=/home/pi/depot_tools:$PATH' >> ~/.bashrc
    ```

- Clone WebRTC Source Code

    ```bash
    mkdir webrtc-checkout
    cd ./webrtc-checkout
    fetch --nohooks webrtc
    src/build/linux/sysroot_scripts/install-sysroot.py --arch=arm64
    gclient sync -D
    ```

    To check out the m147 (stable) version:

    ```
    git checkout -b local-7727 branch-heads/7727
    git gc --aggressive
    gclient sync -D --force --reset --with_branch_heads --no-history
    ```

## Compile `libwebrtc.a`

- ### Step 1: Generate Build Configuration
    ``` bash
    gn gen out/test --args='target_os="linux" 
    target_cpu="arm64" 
    is_debug=false 
    rtc_include_tests=false 
    rtc_use_x11=false 
    rtc_use_h264=true 
    rtc_use_pipewire=false 
    use_rtti=true 
    use_glib=false 
    use_custom_libcxx=false 
    rtc_build_tools=false 
    rtc_build_examples=false 
    is_component_build=false 
    is_component_ffmpeg=true 
    clang_use_chrome_plugins=false'
    ```

- ### Step 2: Build

    ```
    ninja -C out/Release64
    ```

- ### Step 3: Extract Header Files

    ```bash
    rsync -amv --include=*/ --include=*.h --include=*.hpp --exclude=* ./ ./webrtc
    ```

- ### Step 4: Transfer Headers and Library to Raspberry Pi

    You can use scp or another method to transfer the header files and libwebrtc.a to your Raspberry Pi. Once transferred, move them to /usr/local:
    ```bash
    # Transfer files via scp
    # scp -r ./webrtc pi@192.168.x.x:/home/pi
    # scp ./out/Release64/obj/libwebrtc.a pi@192.168.x.x:/home/pi

    # Move files to the correct directories
    sudo mv /home/pi/webrtc /usr/local/include
    sudo mv /home/pi/libwebrtc.a /usr/local/lib
    ```

# Reference
* [Version | WebRTC](https://chromiumdash.appspot.com/branches)
* [Building old revisions | WebRTC](https://chromium.googlesource.com/chromium/src.git/+/HEAD/docs/building_old_revisions.md)
* [Using a custom clang binary | WebRTC](https://chromium.googlesource.com/chromium/src/+/master/docs/clang.md#using-a-custom-clang-binary)
* [Trickle ICE](https://webrtc.github.io/samples/src/content/peerconnection/trickle-ice/)
