"""
Gamepad to ROS 2 /cmd_vel
-------------------------
Reads the gamepad snapshots that pi-webrtc writes to its gamepad socket, and publishes the left
stick as geometry_msgs/Twist on /cmd_vel: push up to drive forward, left and right to turn.

The robot only moves while the deadman button (the right bumper, `rb`, by default) is held.
It stops when the button is released, when the gamepad goes quiet, or when pi-webrtc goes away.

Usage:
    1. Start pi-webrtc with gamepad support:
        /path/to/pi-webrtc --camera=v4l2:42 ... --use-mqtt ... --enable-gamepad

    2. Run this node:
        source /opt/ros/humble/setup.bash
        python3 ros2_gamepad_teleop.py --max-linear 0.5 --max-angular 1.0

    3. Connect a gamepad in the browser, hold `rb`, and move the left stick.

Requirements:
    - ROS 2 (Humble or newer)
"""

import json
import time
import signal
import socket
import argparse
import threading
import rclpy
from rclpy.node import Node
from rclpy.signals import SignalHandlerOptions
from geometry_msgs.msg import Twist

STALE_NS = 500_000_000  # Stop when the newest snapshot is older than this.


class GamepadSocket(threading.Thread):
    """Keeps the newest snapshot from pi-webrtc, and reconnects whenever pi-webrtc restarts."""

    def __init__(self, path, logger):
        super().__init__(daemon=True)
        self.path = path
        self.logger = logger
        self.latest = None
        self.lock = threading.Lock()

    def run(self):
        waiting = False
        while True:
            try:
                with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
                    sock.connect(self.path)
                    self.logger.info(f"Connected to {self.path}")
                    waiting = False
                    for line in sock.makefile("r"):
                        with self.lock:
                            self.latest = json.loads(line)
            except OSError as e:
                if not waiting:
                    self.logger.warning(f"Waiting for {self.path} (is pi-webrtc running with --enable-gamepad?): {e}")
                    waiting = True
            with self.lock:
                self.latest = None
            time.sleep(1)

    def snapshot(self):
        with self.lock:
            return self.latest


class GamepadTeleop(Node):
    def __init__(self, args):
        super().__init__("gamepad_teleop")
        self.args = args
        self.publisher = self.create_publisher(Twist, args.topic, 10)
        self.gamepad = GamepadSocket(args.socket, self.get_logger())
        self.gamepad.start()
        self.create_timer(1.0 / args.rate, self.publish)

    def command(self):
        """The Twist for the newest snapshot, or a stop."""
        twist = Twist()
        snapshot = self.gamepad.snapshot()
        if not snapshot or time.monotonic_ns() - snapshot["device_monotonic_ns"] > STALE_NS:
            return twist

        pads = snapshot["gamepads"]
        pad = pads.get(self.args.gamepad) if self.args.gamepad else next(iter(pads.values()), None)
        if pad is None:
            return twist
        if self.args.deadman and self.args.deadman not in pad.get("pressed", []):
            return twist

        # Sticks go from -1 to 1, with +y down. ROS: +x is forward, +z turns left.
        twist.linear.x = -pad["left_y"] * self.args.max_linear
        twist.angular.z = -pad["left_x"] * self.args.max_angular
        return twist

    def publish(self):
        self.publisher.publish(self.command())

    def stop(self):
        self.publisher.publish(Twist())


def main():
    parser = argparse.ArgumentParser(description="Drive a ROS 2 robot with a gamepad through pi-webrtc")
    parser.add_argument("--socket", default="/tmp/pi-webrtc-gamepad.sock", help="pi-webrtc's --gamepad-socket-path")
    parser.add_argument("--topic", default="/cmd_vel", help="geometry_msgs/Twist topic to publish")
    parser.add_argument("--max-linear", type=float, default=0.5, help="Forward speed at full stick, in m/s")
    parser.add_argument("--max-angular", type=float, default=1.0, help="Turn speed at full stick, in rad/s")
    parser.add_argument("--deadman", default="rb", help='Button to hold while driving; "" to drive without one')
    parser.add_argument("--gamepad", default="", help="Gamepad key to follow; empty follows the first one")
    parser.add_argument("--rate", type=float, default=20.0, help="Publish rate, in Hz")
    args, _ = parser.parse_known_args()

    # Without rclpy's own Ctrl+C handler, Ctrl+C raises KeyboardInterrupt and leaves ROS running,
    # so the stop below still reaches the robot. The timer wakes spin() often enough to notice.
    # SIGTERM, from systemctl stop for example, does the same.
    rclpy.init(signal_handler_options=SignalHandlerOptions.NO)
    signal.signal(signal.SIGTERM, signal.default_int_handler)
    node = GamepadTeleop(args)
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.stop()
        node.destroy_node()
        rclpy.try_shutdown()


if __name__ == "__main__":
    main()
