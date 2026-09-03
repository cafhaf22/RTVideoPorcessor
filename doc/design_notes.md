# Derq Systems Challenge — Architecture & Design Notes

## Shared Memory (S) — Writer/Reader

**IPC choice: POSIX over Boost.Interprocess.** No cross-platform
requirement (Linux only), fewer dependencies, more low-level control
— relevant for a systems/embedded role. Boost would be the right
choice if Windows support were needed.

**Interfaces (`ISharedMemoryWriter`/`ISharedMemoryReader`).** Applies
DIP: `VideoProcessor`/`VideoClipper` depend on the abstraction, not
`PosixSharedMemory` directly — enables mocking for tests and swapping
implementations (e.g. to Boost) without touching consumers. Applies
ISP: two separate interfaces, not one combined `ISharedMemoryManager`
— a Writer has no business exposing `Read()`, and vice versa. OpenCV
itself is *not* abstracted behind an interface: only `VideoProcessor`
uses it internally, so there's no second real consumer that would
justify the extra layer (YAGNI).

**Memory layout**: a single contiguous region —
`[vHeader_t][vFrameData_t][pixel buffer]`. `vHeader_t` (width, height,
fps, frame_size_bytes) is written once by the Writer's `Initialize()`.
`vFrameData_t` + pixel buffer are overwritten on every `WriteFrame()`
call — this is a single-slot "mailbox", not a queue: the Clipper
always reads whatever is most recent, with no history kept at this
layer (history is `VideoClipper`'s own responsibility, see below).
The Reader learns the total region size via `fstat()` on its own
`shm_open()` fd rather than recomputing it — `ftruncate()`'d size by
the Writer is the single source of truth.

**Synchronization — two named POSIX semaphores, not `std::mutex`.**
`std::mutex`/`std::counting_semaphore` only work within one process;
POSIX named semaphores (`sem_open`) live in the kernel and are shared
across processes by name, same pattern as `shm_open`.
- `sem_` (binary, init 1): mutual exclusion around the frame_data +
  pixel buffer section, preventing torn reads/writes.
- `frame_ready_sem_` (binary, init 0): Writer `sem_post()`s it after
  every `WriteFrame()`; Reader `sem_wait()`s on it at the *start* of
  `ReadFrame()`, before taking the mutex. This blocks the Clipper
  until a genuinely new frame exists.
  - **Why this exists**: without it, VideoClipper read in a free
    loop, far faster than the Processor's real frame rate, re-reading
    and caching the *same* frame as duplicates. This caused an actual
    OOM (~2.9GB before the kernel killed the process).
  - **Why binary, not counting**: only one frame slot exists — it's
    overwritten, not queued. A counting semaphore would let the Reader
    `sem_wait()` N times and read the *same* frame N times if the
    Writer posted N times before a single read. Binary correctly
    collapses "multiple writes" to "something is pending, yes/no".
  - Considered pthread_mutex_t with `PTHREAD_PROCESS_SHARED` embedded
    in the shared struct instead of a second semaphore — more init
    complexity for no real benefit here.
  - Encapsulation: the `sem_wait()` for readiness lives *inside*
    `ReadFrame()`, not exposed as a separate `WaitForNewFrame()`
    method — `VideoClipper` shouldn't need to know it's reading a
    synchronized shared resource at all.
- `shm_unlink`/`sem_unlink` responsibility: the creator (Writer) —
  same ownership pattern as file creation in general. The Reader never
  unlinks anything it didn't create.

## Clocks

- `std::chrono::steady_clock`: internal timing within a single
  process (e.g. `VideoProcessor`'s `next_frame_time`, pacing frame
  writes to match the source video's fps without drift — computed as
  an absolute wake time each iteration, not accumulated sleeps).
  Monotonic, immune to NTP jumps, but its epoch is *not* guaranteed
  comparable across processes.
- `std::chrono::system_clock`: any timestamp that must be compared
  *between* processes — `vFrameData_t.timestamp` specifically, since
  `VideoClipper` compares frame timestamps against the Event
  Notifier's trigger time to build the 9s-before/6s-after window.
  Shared Unix epoch across processes on the same system. Clock-jump
  risk accepted as irrelevant given the challenge's short timeframe
  (minutes, not days).
- `CAP_PROP_FRAME_COUNT` reports an *estimated* total (duration × fps
  from container metadata), not the exact decodable frame count —
  confirmed in testing (a 19s/24fps video processed slightly fewer
  frames than reported). Normal OpenCV/FFmpeg behavior, not a bug.

## Signal handling (Ctrl+C)

Every program uses the same pattern: an atomic `g_should_stop` flag
set by a free-function `SignalHandler(int)` (signal handlers can't be
class methods — the OS requires `void(int)`), checked in every
blocking-wait loop with a short timeout so the flag actually gets
re-evaluated:
- `VideoProcessor`: `condition_variable::wait_until` (mutex +
  cv are file-scope globals, not class members — a known
  encapsulation trade-off, see "Known limitations" below).
- `VideoClipper`: `sem_timedwait` (200ms) on frame reads;
  `select()`+timeout (1s) around both the socket's initial `accept()`
  and the subsequent `recv()`.
- `EventNotifier`: non-canonical terminal read with `VMIN=0`/`VTIME=1`
  (100ms), see below.
- `CPUWatcher`: `sleep_until` with 1s cadence, loop condition checked
  each cycle.

Validated on VideoClipper across 3 scenarios (mid-stream, no
connection ever made, after video end) and on EventNotifier/Processor
— all shut down cleanly in <1s from the signal, no manual cleanup
needed. `SIGKILL` bypasses all of this by design (uninterceptable at
the OS level) — accepted as an out-of-scope edge case, recoverable
manually (`stty sane` for a terminal left in raw mode, orphaned
`/dev/shm` or socket files removed manually).

**Reusing a `struct timeval` across multiple `select()` calls is a
bug**: Linux mutates it to reflect remaining time, so without
resetting it every loop iteration, the timeout decays toward a
busy-loop. Fixed by declaring/resetting it inside each iteration.

## VideoClipper — 9s buffer and multi-trigger support

**Buffer**: `std::deque<FrameCache>` (not `std::vector`) — O(1)
`push_back`/`pop_front` at both ends, needed because the window is
time-based (9s), not a fixed element count, and varies with fps. Runs
continuously, independent of any trigger — it's never paused, moved,
or emptied for a trigger's sake, so it stays the single, permanent
source of truth for "the last 9s".

**Multiple concurrent triggers** (the real requirement — "each event
should have its own video generated" implies overlapping triggers,
not just one at a time):
- Each trigger produces its own `PendingClip { snapshot; trigger_ts; }`,
  tracked in `std::vector<PendingClip> pending_clips_` — the count of
  in-flight clips is just `pending_clips_.size()`, no separate counter
  needed.
- `FrameCache::pixel_data` is `std::shared_ptr<std::vector<uint8_t>>`,
  not a plain `std::vector`. Since the main buffer can no longer be
  *moved* into a snapshot (it must keep running for future triggers),
  snapshots are *copied* — but copying a `FrameCache` only copies the
  shared_ptr (pointer + refcount), not the underlying bytes. Multiple
  snapshots covering overlapping 9s windows reference the same pixel
  buffers instead of duplicating them; a buffer is freed automatically
  once nothing references it anymore (RAII via refcounting — no
  possibility of a double-free or a use-after-free by one owner while
  another still needs it, unlike a raw pointer).
- `g_trigger_received` (bool) → `g_pending_triggers`
  (`std::atomic<int>`), incremented (`fetch_add(1)`) per event instead
  of `store(true)` — a bool can only represent "0 or 1 pending",
  silently losing any trigger that arrives while one is already being
  processed.
- Each loop iteration: drain `g_pending_triggers`, creating one new
  `PendingClip` per pending trigger with a snapshot of `frame_cache_`
  at that instant; then iterate *all* `pending_clips_`, generating and
  removing any whose 6s post-trigger window has completed.
- No additional threads used — the concurrency needed here is multiple
  *pending states*, handled by a vector, not hardware parallelism.
  Considered a thread pool initially; rejected because there's no
  ongoing work per pending clip while waiting (just passive time
  passing) — the only genuinely expensive operation is
  `GenerateVideoClip()`'s disk write, which could optionally run on an
  ephemeral thread if multiple clips complete simultaneously (not
  implemented, out of scope for the time available).

**Validated**: 1, 2, and 3 concurrent triggers (including <1s apart)
in a single session with all 4 programs running — correct independent
clips generated each time. Triggers arriving before the 9s buffer is
fully populated produce proportionally shorter clips (best-effort,
same behavior already expected for the single-trigger case) — e.g. a
trigger 2s after Processor start produced a ~12s clip (292 frames at
24fps) instead of 15s.

**Socket (E) thread**: dedicated thread for `accept()`/`recv()`,
separate from the frame-reading loop — blocking `select()`/`recv()`
here doesn't affect the main loop, and keeps socket-handling logic
decoupled from frame-reading logic. `select()` (not `epoll()`) is
sufficient and justified: only one fd (`client_fd`) is ever in the
set, so `select()`'s known limits (`FD_SETSIZE`, O(n) scaling) don't
apply — `epoll()` would be over-engineering for a single fd.

## EventNotifier — key detection

Raw terminal mode (`termios`, `VMIN=0`/`VTIME=1`, 100ms read timeout)
instead of canonical mode (`getline`, `e`+Enter) — switched after
confirming empirically that Ctrl+C needed an extra Enter to unblock
`getline` (inconsistent, implementation-dependent behavior). `ICANON`
and `ECHO` are disabled; `ISIG` is deliberately left untouched so
Ctrl+C still generates SIGINT normally instead of arriving as a plain
input byte. termios setup/teardown lives in the class constructor/
destructor (RAII), not `main()` — keeps terminal configuration an
internal detail of `EventNotifier`.

**Known limitation**: `SIGKILL` bypasses the destructor, leaving the
terminal in raw mode — recoverable with `stty sane`/`reset`. Accepted:
`SIGKILL` is a deliberate, rare action, not a normal shutdown path,
and every program using `termios` has this same exposure.

## CPUWatcher

Takes a PID as a CLI argument — not launched via `fork`/`exec`, not
discovered by scanning `/proc/` for a name. Consistent with standard
Unix tools (`kill`, `strace`) that operate on a given PID; avoids
ambiguity if multiple instances of a monitored program are running.

**Data sources**: `/proc/<pid>/stat` (fields 14/15 = `utime`+`stime`;
field 2, `(comm)`, is skipped since it may contain spaces inside its
parentheses), `/proc/stat` (first line only — the pre-aggregated total
across all cores), `/proc/<pid>/status` (`VmRSS`), `/proc/meminfo`
(`MemTotal`, `MemAvailable`).

**CPU% requires two samples.** `/proc/stat`/`/proc/<pid>/stat` report
cumulative jiffies since boot/process-start, not an instantaneous
value — a percentage requires the *delta* between two samples one
interval apart: `(process_delta / system_delta) × 100`. Since this is
a ratio of two deltas measured over the same real interval,
`sleep_until` drift doesn't distort the math — it only matters for
literally satisfying "printed every 1 second", hence `sleep_until`
(absolute wake time) over `sleep_for` (which would accumulate drift).
Memory values (`VmRSS`, `MemTotal`, `MemAvailable`) are instantaneous
— no delta needed.

A failed `/proc` read in a given cycle logs and `continue`s rather
than aborting — `prev_*_cpu_time` is left unchanged so the next valid
delta is computed against the last known-good sample, not a
corrupted/zero one.

**Validated** against `yes` (single-threaded, CPU-bound): ~8.3% on a
~12-core system (100%/12 ≈ 8.3%, consistent). Validated against a live
`VideoClipper`: RSS growth visible while the 9s buffer fills, CPU
spikes coinciding with clip generation in the logs.

**Known limitation**: doesn't verify the monitored PID is still the
*same* process over time (could be recycled by the kernel if the
original exited) — not checked against `/proc/<pid>/comm`. Accepted
as minor for this challenge's scope.

## Known limitations / not implemented

- **Memory under concurrent load**: each in-progress `PendingClip`
  holds ~250MB of referenced frame data — inherent to the 9s lookback
  requirement, not reducible without violating the spec. On a system
  already near its RAM ceiling from other applications, several
  triggers arriving close together can push total usage high enough
  to cause OS-level swap thrashing (observed: a full system freeze
  with several Chrome tabs open + system RAM at ~15.9/16.3GB used;
  VideoClipper's own logic was unaffected — both clips were generated
  correctly afterward with accurate trigger timestamps preserved in
  their filenames). Re-tested with more free RAM available: no
  freeze. Not something the program can fully prevent; worth closing
  memory-heavy applications before a live demo.
- **VideoProcessor's signal-handling globals** (`g_should_stop`,
  mutex, `condition_variable`) are file-scope, not class members —
  breaks strict encapsulation. Cause: signal handlers must be free
  functions. Cleaner alternative identified but not implemented:
  keep the state as private `VideoProcessor` members and use a single
  global pointer (`VideoProcessor* g_active_processor`) that the
  handler uses to call a public `RequestStop()` method.
- **EventNotifier**: still sends one trigger per `'e'` press with no
  batching/debouncing — rapid presses are all forwarded individually
  (this is intentional and correct per the spec, not a limitation,
  but worth noting since it directly feeds VideoClipper's
  multi-trigger handling).
- **`ProcessAndClip()`** is a long method mixing several
  responsibilities (frame read, buffer maintenance, trigger
  detection, pending-clip management) — a candidate for extraction
  into smaller private methods if time allows.
- **Duplicate signal-handling boilerplate** across all 4 `.cpp` files
  — a candidate for extraction into a shared `common/` utility if
  time allows.