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
  clip. Supports multiple concurrent/overlapping triggers, each
  generating its own independent clip.
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
- At least ~1-2GB of free RAM recommended (see note below)

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
named after that trigger's timestamp (in milliseconds). Multiple
triggers can be in progress at once, each producing its own clip.

> **Memory note:** each in-progress clip temporarily holds its own
> snapshot of the last ~9s of video in memory (see Key Architecture
> Decisions). On systems with limited free RAM, closing memory-heavy
> applications (e.g. browsers) before running the full pipeline is
> recommended, especially when testing several triggers in quick
> succession.

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
`journalctl -t VideoProcessor -t VideoClipper -t EventNotifier -t CpuWatcher -f`
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

Full rationale, alternatives considered, and validation details for
every decision below live in [`doc/design_notes.md`](doc/design_notes.md).
Highlights:

- **IPC (shared memory)**: POSIX (`shm_open`/`mmap`) over Boost.Interprocess
  — no cross-platform requirement, fewer dependencies, more control.
  `ISharedMemoryWriter`/`Reader` interfaces (SOLID: DIP + ISP) let each
  side depend only on the operations it actually needs, and allow
  mocking in tests.
- **Synchronization between processes**: two named POSIX semaphores —
  one binary mutex protecting the shared frame data from torn
  reads/writes, and one binary "frame ready" semaphore so
  `VideoClipper` blocks until a genuinely new frame exists, instead of
  polling and reading duplicates (this polling issue caused a real OOM
  crash during testing, since fixed).
- **Clocks**: `steady_clock` for internal loop timing (frame-rate
  pacing, no drift accumulation); `system_clock` for any timestamp
  that must be compared across processes (frame timestamps vs. the
  Event Notifier's trigger time) — a monotonic clock's epoch isn't
  guaranteed comparable between processes.
- **VideoClipper's 9s circular buffer**: a `std::deque` window
  maintained continuously, independent of any trigger. On a trigger,
  its contents are copied (not moved) into a `PendingClip`, so the
  main buffer keeps running uninterrupted for future triggers.
- **Multiple concurrent triggers**: each trigger gets its own
  `PendingClip` (its own 9s snapshot + independent 6s timer), tracked
  in a `std::vector` — not a single "one at a time" flag. Frame pixel
  data uses `std::shared_ptr` so overlapping snapshots reference the
  same underlying buffer instead of duplicating it; the buffer is
  freed automatically once nothing references it anymore.
- **Non-blocking waits with timeouts**: any potentially indefinite
  wait (socket `accept()`/`recv()`, semaphore waits, keyboard input)
  uses a short timeout so `g_should_stop` gets re-checked regularly —
  this is what makes Ctrl+C shutdown clean and fast (<1s) in every
  program, in every tested scenario.
- **Event Notifier key detection**: raw terminal mode (`termios`,
  `VMIN=0`/`VTIME=1`) instead of canonical mode, so `'e'` triggers
  instantly without pressing Enter. `ISIG` is left untouched so
  Ctrl+C still works normally.
- **CPUWatcher**: takes a PID as an argument (not by name) — consistent
  with standard Unix tools like `kill`. Reads directly from `/proc/`;
  CPU% requires two time-accumulator samples a second apart (a
  proportion of two deltas), since `/proc/stat` gives cumulative
  jiffies, not an instantaneous percentage.
- **Known trade-off**: each in-progress clip holds roughly ~250MB of
  frame data in memory (inherent to the 9s lookback requirement, not
  reducible without violating the spec). Under heavy concurrent
  triggers *and* an already memory-constrained system, this can
  contribute to system-wide memory pressure — validated and documented
  in `doc/design_notes.md`.