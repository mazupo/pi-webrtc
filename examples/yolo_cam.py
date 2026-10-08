"""
YOLO Object Detection to a Virtual V4L2 Device
----------------------------------------------
This script reads the Raspberry Pi camera with Picamera2, runs YOLO object detection, draws the
boxes with OpenCV, and writes the result to a virtual V4L2 loopback device. pi-webrtc then
streams it.

It runs only on a Raspberry Pi. On a Jetson, run object detection with DeepStream instead.

Usage:
    1. Create a Python virtual environment that can also use Picamera2 from apt, and install the
       packages in it:
        sudo apt install v4l2loopback-dkms python3-picamera2
        python3 -m venv --system-site-packages ~/yolo-venv
        source ~/yolo-venv/bin/activate
        pip install torch torchvision --index-url https://download.pytorch.org/whl/cpu
        pip install opencv-python ultralytics

       Install torch from the CPU index first. The default torch for 64-bit Arm also downloads
       NVIDIA CUDA packages. They are several GB, they fill up /tmp, and a Raspberry Pi cannot
       use them.

    2. Create the virtual camera. The first command removes an old one, if there is one:
        sudo modprobe -r v4l2loopback
        sudo modprobe v4l2loopback devices=1 video_nr=42 card_label=YoloCam max_buffers=4 exclusive_caps=1

    3. Run the script in the virtual environment. The first run downloads the model:
        python ./examples/yolo_cam.py --width 1280 --height 720 --camera-id 0 --virtual-device /dev/video42

    4. Stream the result:
        /path/to/pi-webrtc --camera=v4l2:42 --v4l2-format=i420 --width=1280 --height=720 ...

Requirements:
    - Raspberry Pi with Camera Module, on a 64-bit OS
    - v4l2loopback kernel module installed
    - Python packages: picamera2 (apt), torch (CPU), opencv-python, ultralytics
"""

import os
import cv2
import fcntl
import struct
import logging
import argparse
from picamera2 import Picamera2
from ultralytics import YOLO

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


def draw_boxes(frame, result, class_names):
    """Draws each detected object's box and label on the frame."""
    for box in result.boxes:
        x1, y1, x2, y2 = map(int, box.xyxy[0])
        label = f"{class_names[int(box.cls[0])]} {float(box.conf[0]):.0%}"

        cv2.rectangle(frame, (x1, y1), (x2, y2), (0, 255, 0), 2)
        (text_w, text_h), _ = cv2.getTextSize(label, cv2.FONT_HERSHEY_SIMPLEX, 0.5, 2)
        cv2.rectangle(frame, (x1, y1 - text_h - 5), (x1 + text_w, y1), (0, 255, 0), -1)
        cv2.putText(
            frame, label, (x1, y1 - 5), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 0, 0), 2
        )


class YoloCameraStreamer:
    def __init__(self, width, height, camera_id, virtual_camera, model):
        self.width = width
        self.height = height
        self.camera_id = camera_id
        self.virtual_camera = virtual_camera
        self.model = YOLO(model)
        self.fd = None
        self.picam2 = None

        self._initialize_camera()
        self._initialize_virtual_device()

    def _initialize_camera(self):
        self.picam2 = Picamera2(self.camera_id)
        # "RGB888" stores each pixel as B, G, R: the order that OpenCV and YOLO expect.
        # queue=False waits for a new frame, so a slow model never works on an old one.
        config = self.picam2.create_video_configuration(
            main={"size": (self.width, self.height), "format": "RGB888"}, queue=False
        )
        self.picam2.configure(config)

    def _initialize_virtual_device(self):
        if not os.path.exists(self.virtual_camera):
            logging.warning(f"Device does not exist: {self.virtual_camera}")
            return

        self.fd = os.open(self.virtual_camera, os.O_RDWR)
        set_output_format(self.fd, self.width, self.height)
        logging.info(f"Set virtual camera: {self.virtual_camera}")

    def _process_frame(self, frame):
        result = self.model(frame, verbose=False)[0]
        draw_boxes(frame, result, self.model.names)
        os.write(self.fd, cv2.cvtColor(frame, cv2.COLOR_BGR2YUV_I420).tobytes())

    def start(self):
        if not self.fd:
            logging.error("Cannot start streaming without virtual device.")
            return

        logging.info(
            f"Detecting objects in camera {self.camera_id} at {self.width}x{self.height}, "
            f"and writing to {self.virtual_camera}"
        )
        self.picam2.start()
        try:
            while True:
                self._process_frame(self.picam2.capture_array("main"))
        except KeyboardInterrupt:
            logging.info("Received KeyboardInterrupt.")
        except OSError as e:
            logging.error(f"Failed to write images: {e}")
        finally:
            self.stop()

    def stop(self):
        if self.picam2:
            self.picam2.stop()
        if self.fd:
            os.close(self.fd)
        logging.info("Streaming stopped.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="YOLO object detection to a virtual camera")
    parser.add_argument("--width", type=int, default=1280, help="Frame width")
    parser.add_argument("--height", type=int, default=720, help="Frame height")
    parser.add_argument("--camera-id", type=int, default=0, help="Camera input ID")
    parser.add_argument(
        "--virtual-device",
        type=str,
        default="/dev/video42",
        help="Virtual video device path",
    )
    parser.add_argument(
        "--model",
        type=str,
        default="yolo11n.pt",
        help="YOLO model file. It is downloaded on the first run if missing",
    )
    args = parser.parse_args()

    streamer = YoloCameraStreamer(
        width=args.width,
        height=args.height,
        camera_id=args.camera_id,
        virtual_camera=args.virtual_device,
        model=args.model,
    )

    streamer.start()
