# Integrations

Connect pi-webrtc to your own software. Send messages both ways, read a gamepad, show the camera in Home Assistant, or stream from a ROS robot.

- [IPC messages](ipc.md): send data between the browser and a program on the device.
- [Gamepad](gamepad.md): control the device with a gamepad that is connected to the browser.
- [Home Assistant](home-assistant.md): show the camera on a Home Assistant dashboard.
- [ROS](ros.md): stream a ROS 2 image topic, and drive the robot with a gamepad.

All of them use WebRTC DataChannels, except Home Assistant. See [DataChannels](../reference/datachannels.md) for which signaling carries which channel.
