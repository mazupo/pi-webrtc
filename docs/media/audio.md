# Two-way Audio

pi-webrtc can send audio from a microphone on the device, and play audio from the viewer on a speaker.

## Turn audio on

Audio is on when you do not pass `--no-audio`. pi-webrtc uses PulseAudio by default:

```bash
./pi-webrtc --camera=libcamera:0 --uid=my-pi --use-whep ...
```

On a system without PulseAudio, add `--force-alsa`. Then make sure that the ALSA `default` device is your speaker.

To run pi-webrtc as a service with audio, also run PulseAudio as a system service. See [Run as a service](../deployment/run-as-service.md#1-run-pulseaudio-as-a-system-service).

## What works where

- **Peer-to-peer (MQTT, WHEP):** audio goes both ways. The viewer's audio plays on the default output device.
- **SFU (LiveKit, Cloudflare):** audio goes one way, from the device to the viewers.

## Hardware

The device needs a microphone and a speaker. USB audio devices are the easiest choice. For devices on the GPIO pins (I2S), see:

- **Microphone:** [wiring and testing an I2S MEMS microphone](https://learn.adafruit.com/adafruit-i2s-mems-microphone-breakout/raspberry-pi-wiring-test)
- **Speaker:** [wiring a MAX98357 I2S amplifier](https://learn.adafruit.com/adafruit-max98357-i2s-class-d-mono-amp/raspberry-pi-wiring)
