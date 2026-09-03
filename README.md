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
- `socat` (optional — only needed for manually simulating a trigger
  without the Event Notifier — `sudo apt install socat`)

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
cmake --build . --target vclipper
cmake --build . --target notifier
cmake --build . --target cpuwatcher
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

### Video Clipper

```bash
./video_clipper/vclipper <output_directory>
```

Reads frames from shared memory and listens on a UNIX socket
(`/tmp/event_sock.sock`) for trigger events. Generates a 15-second
`.mp4` clip in `<output_directory>` for each trigger received,
named after the trigger's timestamp (in milliseconds).

### Event Notifier

```bash
./event_notifier/notifier
```

Connects to the Video Clipper's socket and sends a trigger event
every time you press `e` (no need to press Enter). Press Ctrl+C to
quit.

### CPU Watcher

```bash
./cpu_watcher/cpuwatcher <pid_to_monitor>
```

Prints CPU and memory usage of the given process, plus overall
system memory, once per second. Typically pointed at the Video
Clipper's PID (find it with `ps aux | grep vclipper`).

### Running the full system

```bash
# Terminal 1
./video_processor/vprocessor <path_to_video.mp4>

# Terminal 2 (after Terminal 1 has started)
mkdir -p /tmp/clips
./video_clipper/vclipper /tmp/clips

# Terminal 3
ps aux | grep vclipper   # note the PID
./cpu_watcher/cpuwatcher <clipper_pid>

# Terminal 4
./event_notifier/notifier
# press 'e' whenever you want to generate a clip
```

Check `/tmp/clips` for the generated `.mp4` files, and use
`journalctl -t VideoProcessor -t VideoClipper -t EventNotifier -f`
to follow the programs' logs together.

### Manually testing Video Processor + Video Clipper without the Event Notifier

A trigger can be simulated manually with `socat`:

```bash
echo "trigger" | socat - UNIX-CONNECT:/tmp/event_sock.sock
```

### Viewing logs in real time

Programs log their activity via `syslog`. To view them:

```bash
# Real time, while the program is running
journalctl -t VideoProcessor -f

# All four programs together
journalctl -t VideoProcessor -t VideoClipper -t EventNotifier -t CpuWatcher -f

# Last N lines
journalctl -t VideoProcessor -n 50 --no-pager
```

If `journalctl` isn't available or isn't capturing the logs on your system:

```bash
grep VideoProcessor /var/log/syslog | tail -n 50
```

## Running the tests

### Unit tests

From the `build` folder:

```bash
ctest --output-on-failure
```

Or directly:

```bash
./test/common/test_shared_memory
```

### Integration test (Video Processor + Video Clipper)

```bash
./test/integration/test_processor_clipper.sh <path_to_test_video.mp4>
```

Starts both programs, waits for the shared-memory buffer to fill,
sends a trigger via `socat`, and verifies that exactly one `.mp4`
clip was generated before shutting both programs down.

## Key architecture decisions

*(Section in progress — will be filled in from `doc/design_notes.md`.)*