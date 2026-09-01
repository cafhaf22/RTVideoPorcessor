# Derq Systems Software Engineer Challenge

## Description

A system of 4 independent C++ programs that together process a video,
share it in real time via shared memory, and generate 15-second clips
(9s before / 6s after) when a trigger event is received:

- **Video Processor**: reads a video file (.mp4) and shares its frames
  in real time through shared memory (S), replicating the original
  frame rate of the video.
- **Video Clipper**: reads frames from the shared space (S) and, upon
  receiving a trigger event via socket (E), generates a 15-second .mp4
  clip.
- **Event Notifier**: interactive program that sends an event to the
  Video Clipper every time the user presses 'e'.
- **CPU Watcher**: monitors CPU and memory usage of the Video Clipper
  and the overall system, printing values every second.

All programs shut down gracefully on Ctrl+C (SIGINT).

## Requirements

- Linux (tested on Ubuntu 24.04 "noble")
- CMake 3.28+
- C++17-compliant compiler (tested with GCC 13.3.0)
- OpenCV 4.6 (`sudo apt install libopencv-dev`)
- pthread (included with the system)

## Build

From the project root:

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

This builds all targets: the `posix_shared_memory` library, each
program's executable, and the tests.

### Building a specific target

```bash
cmake --build . --target vprocessor
cmake --build . --target test_shared_memory
```

## Running the programs

### Video Processor

```bash
./video_processor/vprocessor <path_to_video.mp4>
```

The video must be an existing `.mp4` file. The program runs
indefinitely, sharing frames through shared memory
(`/derq_video_shm`), until the video ends or Ctrl+C is received.

### Viewing logs in real time

Programs log their activity via `syslog`. To view them:

```bash
# Real time, while the program is running
journalctl -t VideoProcessor -f

# Last N lines
journalctl -t VideoProcessor -n 50 --no-pager
```

If `journalctl` isn't available or isn't capturing the logs on your system:

```bash
grep VideoProcessor /var/log/syslog | tail -n 50
```

## Running the tests

From the `build` folder:

```bash
ctest --output-on-failure
```

Or directly:

```bash
./test/common/test_shared_memory
```

## Key architecture decisions

*(Section in progress — will be filled in from `doc/design_notes.md`
as the rest of the programs are developed.)*