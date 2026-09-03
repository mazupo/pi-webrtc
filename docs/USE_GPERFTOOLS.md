# Using Google Performance Tools for Performance Testing

This document provides a guide on how to use Google Performance Tools to test performance on a Raspberry Pi and generate performance analysis reports.

## 1. Download and Install Google Performance Tools

```sh
wget https://github.com/gperftools/gperftools/releases/download/gperftools-2.15/gperftools-2.15.tar.gz
tar -xvf ./gperftools-2.15.tar.gz
cd gperftools-2.15
./configure
make
sudo make install
```

The `LD_PRELOAD` environment variable is set correctly to automatically load the performance analysis library when running the program.
```bash
export LD_PRELOAD="/usr/local/lib/libprofiler.so"
```

## 2. Install Necessary Dependencies

The necessary dependencies to generate PDF reports:

```bash
sudo apt-get install ghostscript graphviz
```

## 3. Run the Program and Generate Performance Data
Execute program and output the performance data to a specified file, for example:
```bash
CPUPROFILE=./prof.out HEAPPROFILE=heap CPUPROFILESIGNAL=12 ./pi-webrtc --camera=v4l2:0 --fps=30 --width=1280 --height=960 --v4l2-format=h264 --hw-accel --uid=home-pi-3b
```

Send a signal to the process to start/stop collect performance data:

```bash
killall -12 pi-webrtc
```

## 4. Use pprof Tool to Generate Performance Report
Convert the performance data into a PDF report using the pprof tool:

```bash
pprof /home/pi/IoT/pi-webrtc/build/pi-webrtc prof.out.0 --pdf > prof_0.pdf
pprof --pdf --base=heap.0001.heap ./test_recorder heap.0003.heap > heap_diff.pdf
```

# Reference
* [gperftools](https://github.com/gperftools/gperftools)
