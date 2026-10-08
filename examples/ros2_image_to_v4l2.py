"""
ROS 2 Image Topic to a Virtual V4L2 Camera
------------------------------------------
Subscribes to a sensor_msgs/Image topic and writes every frame to a V4L2 loopback device as
I420, so pi-webrtc can stream the topic like a camera with `--camera=v4l2:<N>`.

Usage:
    1. Create the virtual camera. The first command removes an old one, if there is one:
        sudo modprobe -r v4l2loopback
        sudo modprobe v4l2loopback devices=1 video_nr=42 card_label=RosCam max_buffers=4 exclusive_caps=1

    2. Run this node. It resizes every frame to --width x --height:
        source /opt/ros/humble/setup.bash
        python3 ros2_image_to_v4l2.py --topic /image --device /dev/video42 --width 1280 --height 720

    3. Stream the virtual camera, with the same size:
        /path/to/pi-webrtc --camera=v4l2:42 --v4l2-format=i420 --width=1280 --height=720 ...

Requirements:
    - ROS 2 (Humble or newer) on a 64-bit OS, with OpenCV for Python: sudo apt install python3-opencv
    - v4l2loopback kernel module installed: sudo apt install v4l2loopback-dkms

The image is converted with NumPy rather than cv_bridge: ROS Humble's cv_bridge fails with
NumPy 2, which many machines get from pip.
"""

import os
import cv2
import fcntl
import struct
import argparse
import numpy as np
import rclpy
from rclpy.executors import ExternalShutdownException
from rclpy.node import Node
from rclpy.qos import qos_profile_sensor_data
from sensor_msgs.msg import Image

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


# sensor_msgs/Image encodings this node reads: bytes per pixel, and the conversion to BGR.
ENCODINGS = {
    "bgr8": (3, None),
    "rgb8": (3, cv2.COLOR_RGB2BGR),
    "bgra8": (4, cv2.COLOR_BGRA2BGR),
    "rgba8": (4, cv2.COLOR_RGBA2BGR),
    "mono8": (1, cv2.COLOR_GRAY2BGR),
    "yuv422_yuy2": (2, cv2.COLOR_YUV2BGR_YUY2),
    "yuv422": (2, cv2.COLOR_YUV2BGR_UYVY),
}


def to_bgr(msg):
    """A sensor_msgs/Image as a BGR array. Rows may be padded, so each is cut to the image width."""
    channels, conversion = ENCODINGS[msg.encoding]
    rows = np.frombuffer(msg.data, dtype=np.uint8).reshape(msg.height, msg.step)
    pixels = rows[:, : msg.width * channels].reshape(msg.height, msg.width, channels)
    if conversion is None:
        return pixels
    return cv2.cvtColor(pixels if channels > 1 else pixels[:, :, 0], conversion)


class ImageToV4l2(Node):
    def __init__(self, topic, device, width, height):
        super().__init__("image_to_v4l2")
        self.width = width
        self.height = height
        self.warned = set()

        self.fd = os.open(device, os.O_RDWR)
        set_output_format(self.fd, width, height)

        # Sensor-data QoS keeps only the newest frames, and still receives from a reliable publisher.
        self.create_subscription(Image, topic, self.on_image, qos_profile_sensor_data)
        self.get_logger().info(f"Writing {topic} to {device} as {width}x{height} I420")

    def on_image(self, msg):
        if msg.encoding not in ENCODINGS:
            if msg.encoding not in self.warned:
                self.warned.add(msg.encoding)
                self.get_logger().error(f"Unsupported encoding {msg.encoding}; use one of {', '.join(ENCODINGS)}")
            return

        frame = to_bgr(msg)
        if frame.shape[1] != self.width or frame.shape[0] != self.height:
            frame = cv2.resize(frame, (self.width, self.height))
        try:
            os.write(self.fd, cv2.cvtColor(frame, cv2.COLOR_BGR2YUV_I420).tobytes())
        except OSError as e:
            self.get_logger().error(f"Failed to write a frame: {e}")

    def destroy_node(self):
        os.close(self.fd)
        super().destroy_node()


def main():
    parser = argparse.ArgumentParser(description="Write a ROS 2 image topic to a V4L2 loopback device")
    parser.add_argument("--topic", default="/image", help="sensor_msgs/Image topic to read")
    parser.add_argument("--device", default="/dev/video42", help="V4L2 loopback device to write")
    parser.add_argument("--width", type=int, default=1280, help="Output frame width")
    parser.add_argument("--height", type=int, default=720, help="Output frame height")
    args, _ = parser.parse_known_args()

    rclpy.init()
    node = ImageToV4l2(args.topic, args.device, args.width, args.height)
    try:
        rclpy.spin(node)
    except (KeyboardInterrupt, ExternalShutdownException):
        pass  # Ctrl+C
    finally:
        node.destroy_node()
        rclpy.try_shutdown()


if __name__ == "__main__":
    main()
