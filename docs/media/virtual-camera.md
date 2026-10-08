# Virtual Cameras

Process camera frames in your own program first, for example with OpenCV or an AI model. Then write the result to a virtual camera, and stream it with pi-webrtc.

A virtual camera is a [V4L2 loopback](https://github.com/umlaeute/v4l2loopback) device. Your program writes frames to it, and pi-webrtc reads it like a normal V4L2 camera.

The examples on this page run on a Raspberry Pi. They read the camera with Picamera2.

> [!TIP]
> On a Jetson, run object detection with NVIDIA DeepStream, and stream the result over RTSP. See [DeepStream with object detection](rtsp.md#deepstream-with-object-detection). The [sponsor build](../sponsors.md#sponsor-benefits) can also run detection and tracking inside pi-webrtc, on the GPU.

## 1. Install the packages

```bash
sudo apt install v4l2loopback-dkms python3-opencv python3-picamera2
```

## 2. Create a virtual camera

This creates `/dev/video42`. A high number does not clash with real devices. On a Pi 5, for example, the CSI camera already uses `/dev/video2` to `/dev/video9`.

The first command removes an old virtual camera, if there is one. Then the new settings always apply:

```bash
sudo modprobe -r v4l2loopback
sudo modprobe v4l2loopback devices=1 video_nr=42 card_label=ProcessedCam max_buffers=4 exclusive_caps=1
```

If `modprobe -r` says that the module is in use, stop the programs that use the virtual camera first.

Check that it exists:

```bash
v4l2-ctl --list-devices
```

You should see `ProcessedCam` with `/dev/video42`.

## 3. Write frames to the virtual camera

The [virtual_cam.py](../../examples/virtual_cam.py) example reads the camera, adds a timestamp, and writes YUV 4:2:0 (I420) frames to the virtual camera:

```bash
python3 examples/virtual_cam.py --width 1280 --height 720 --camera-id 0 --virtual-device /dev/video42
```

Leave it running.

To write frames from your own program, copy `set_output_format()` from the example. It sets the size and the format of the virtual camera.

## 4. Stream the virtual camera

In a second terminal, start pi-webrtc with the same size and the `i420` format:

```bash
./pi-webrtc --camera=v4l2:42 --v4l2-format=i420 --width=1280 --height=720 --fps=30 --uid=my-pi --no-audio --use-whep
```

## Object detection with YOLO

The [yolo_cam.py](../../examples/yolo_cam.py) example reads the camera, runs YOLO object detection, and draws a box around each object with OpenCV. It writes the result to the virtual camera from step 2, in place of `virtual_cam.py`.

1. Stop `virtual_cam.py` if it is running. Only one program can use the camera.

2. Create a Python virtual environment, and install the packages in it. `--system-site-packages` lets the environment use Picamera2 from apt. Install `torch` from the CPU index first:

    ```bash
    python3 -m venv --system-site-packages ~/yolo-venv
    source ~/yolo-venv/bin/activate
    pip install torch torchvision --index-url https://download.pytorch.org/whl/cpu
    pip install opencv-python ultralytics
    ```

    The default `torch` for 64-bit Arm also downloads NVIDIA CUDA packages. They are several GB, they fill up `/tmp`, and a Raspberry Pi cannot use them.

3. Run YOLO in the virtual environment. The first run downloads the model:

    ```bash
    python examples/yolo_cam.py --width 1280 --height 720 --camera-id 0 --virtual-device /dev/video42
    ```

4. In a second terminal, stream the result with the same pi-webrtc command as in step 4.

On a Pi 5, YOLO needs about 250 ms for each frame on the CPU. So the result plays at about 4 fps.

ROS 2 users can use the same idea to stream an image topic. See [ROS](../integrations/ros.md).

## Troubleshooting

| Problem | What to do |
| --- | --- |
| pi-webrtc logs `set format(YU12) : Device or resource busy` | No program writes to the virtual camera yet, or another program already reads it. Start your program first, then pi-webrtc. With `exclusive_caps=1`, the virtual camera works as a camera only while a program writes to it. v4l2loopback 0.13 and newer, for example on Raspberry Pi OS Trixie, let only one program read a virtual camera at a time. To show one source in two programs, write it to two virtual cameras. |
| Your program, or pi-webrtc, fails with `Invalid argument` | The virtual camera is stuck. This happens with v4l2loopback 0.12, for example on Ubuntu 22.04, after a program wrote to it without `VIDIOC_STREAMON`. The examples here call it. Stop every program that uses the device, then run the two commands from step 2 again. |
