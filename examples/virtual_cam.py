"""
Camera Stream to Virtual V4L2 Device
------------------------------------
This script captures images from the Raspberry Pi camera and streams them
to a virtual V4L2 loopback device using OpenCV.

It runs only on a Raspberry Pi, because it reads the camera with Picamera2. On other devices,
copy set_output_format() into your own program: it is all a loopback writer needs.

Usage:
    1. Install required dependencies:
        sudo apt install v4l2loopback-dkms python3-opencv python3-picamera2

    2. Create the virtual camera. The first command removes an old one, if there is one:
        sudo modprobe -r v4l2loopback
        sudo modprobe v4l2loopback devices=1 video_nr=42 card_label=ProcessedCam max_buffers=4 exclusive_caps=1

    3. Run the script:
        python3 virtual_cam.py

    4. Test the video output with one of these. v4l2loopback 0.13 and newer let only one
       program read a virtual camera at a time:
        /path/to/pi-webrtc --camera=v4l2:42 --v4l2-format=i420 --width=1920 --height=1080 ...   # View the processed feed by WebRTC
        ffplay /dev/video42                                                                    # View the processed feed by ffplay

Requirements:
    - Raspberry Pi with Camera Module, on a 64-bit OS
    - v4l2loopback kernel module installed
"""

import os
import cv2
import time
import fcntl
import struct
import logging
import argparse
from picamera2 import Picamera2, MappedArray

logging.basicConfig(
    level=logging.INFO, format="%(asctime)s - %(levelname)s - %(message)s"
)

# Values from linux/videodev2.h. The structs are packed by hand, so the example needs only the
# Python standard library: the `v4l2` package on PyPI does not run on Python 3.
V4L2_BUF_TYPE_VIDEO_OUTPUT = 2
V4L2_FIELD_NONE = 1
V4L2_PIX_FMT_YUV420 = int.from_bytes(b"YU12", "little")  # I420
VIDIOC_S_FMT = 0xC0D05605  # _IOWR('V', 5, struct v4l2_format): 208 bytes on 64-bit Linux
VIDIOC_STREAMON = 0x40045612  # _IOW('V', 18, int)


def set_output_format(fd, width, height):
    """Tells the loopback device the size and format of the I420 frames written to it."""
    # struct v4l2_pix_format starts with 8 fields of 32 bits. "8I" packs 8 unsigned ints.
    pix = struct.pack(
        "8I",
        width,
        height,
        V4L2_PIX_FMT_YUV420,  # pixelformat
        V4L2_FIELD_NONE,  # field: whole frames, not interlaced
        width,  # bytesperline of the Y plane
        width * height * 3 // 2,  # sizeimage: the bytes in one I420 frame
        0,  # colorspace: driver default
        0,  # priv
    )
    # struct v4l2_format is a 32-bit buffer type, then a 200-byte union that holds the format.
    # "I4x" packs the type and 4 zero bytes, because on 64-bit Linux the union starts at byte 8.
    # pix.ljust(200, b"\0") pads the pixel format with zeros to the size of the union.
    fmt = struct.pack("I4x", V4L2_BUF_TYPE_VIDEO_OUTPUT) + pix.ljust(200, b"\0")
    fcntl.ioctl(fd, VIDIOC_S_FMT, fmt)

    # v4l2loopback 0.12 (Ubuntu 22.04, for example) frees the device for the next run only if
    # the writer called STREAMON. Without it, the next run fails with "Invalid argument" until
    # the module is reloaded. Newer versions refuse this call and do not need it.
    try:
        fcntl.ioctl(fd, VIDIOC_STREAMON, struct.pack("I", V4L2_BUF_TYPE_VIDEO_OUTPUT))
    except OSError:
        pass


class VirtualCameraStreamer:
    def __init__(self, width, height, camera_id, virtual_camera):
        self.width = width
        self.height = height
        self.camera_id = camera_id
        self.virtual_camera = virtual_camera
        self.fd = None
        self.picam2 = None

        self._initialize_camera()
        self._initialize_virtual_device()

    def _initialize_camera(self):
        self.picam2 = Picamera2(self.camera_id)
        config = self.picam2.create_still_configuration(
            main={"size": (self.width, self.height)}, queue=False
        )
        self.picam2.configure(config)

    def _initialize_virtual_device(self):
        if not os.path.exists(self.virtual_camera):
            logging.warning(f"Device is not existing: {self.virtual_camera}")
            return

        self.fd = os.open(self.virtual_camera, os.O_RDWR)
        set_output_format(self.fd, self.width, self.height)
        logging.info(f"Set camera: {self.virtual_camera}")

    def _process_frame(self, request):
        timestamp = time.strftime("%Y-%m-%d %X")
        with MappedArray(request, "main") as m:
            frame = m.array
            cv2.putText(
                frame, timestamp, (10, 30), cv2.FONT_HERSHEY_SIMPLEX, 1, (0, 255, 0), 2
            )
            yuv_frame = cv2.cvtColor(frame, cv2.COLOR_RGB2YUV_I420)

            try:
                os.write(self.fd, yuv_frame.tobytes())
            except (BrokenPipeError, OSError) as e:
                logging.error(f"Failed to write images: {e}")
                self.stop()

    def start(self):
        logging.info(f"Starting streamer with:")
        logging.info(f"  Resolution: {self.width}x{self.height}")
        logging.info(f"  Camera ID: {self.camera_id}")
        logging.info(f"  Output To Virtual Device: {self.virtual_camera}")

        if not self.fd:
            logging.error("Cannot start streaming without virtual device.")
            return

        self.picam2.pre_callback = self._process_frame
        self.picam2.start()

        try:
            while True:
                time.sleep(1)
        except KeyboardInterrupt:
            logging.info("Received KeyboardInterrupt.")
        finally:
            self.stop()

    def stop(self):
        if self.picam2:
            self.picam2.stop()
        if self.fd:
            os.close(self.fd)
        logging.info("Streaming stopped.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Start virtual camera streamer")
    parser.add_argument("--width", type=int, default=1920, help="Frame width")
    parser.add_argument("--height", type=int, default=1080, help="Frame height")
    parser.add_argument("--camera-id", type=int, default=0, help="Camera input ID")
    parser.add_argument(
        "--virtual-device",
        type=str,
        default="/dev/video42",
        help="Virtual video device path",
    )
    args = parser.parse_args()

    streamer = VirtualCameraStreamer(
        width=args.width,
        height=args.height,
        camera_id=args.camera_id,
        virtual_camera=args.virtual_device,
    )

    streamer.start()
