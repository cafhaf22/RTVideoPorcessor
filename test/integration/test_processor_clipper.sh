#!/bin/bash

set -e

VIDEO_PATH="$1"
OUTPUT_DIR="/tmp/derq_integration_test"
BUILD_DIR="$(dirname "$0")/../../build"

if [ -z "$VIDEO_PATH" ]; then
    echo "Usage: $0 <path_to_test_video.mp4>"
    exit 1
fi

# clean previous runs
rm -rf "$OUTPUT_DIR"
mkdir -p "$OUTPUT_DIR"
rm -f /tmp/event_sock.sock
rm -f /dev/shm/derq_video_shm
rm -f /dev/shm/sem.derq_challenge_sem
rm -f /dev/shm/sem.notify_ready_sem

echo "Starting Video Processor..."
"$BUILD_DIR/video_processor/vprocessor" "$VIDEO_PATH" &
PROCESSOR_PID=$!

sleep 1

echo "Starting Video Clipper..."
"$BUILD_DIR/video_clipper/vclipper" "$OUTPUT_DIR" &
CLIPPER_PID=$!

echo "Waiting for buffer to fill (10s)..."
sleep 10

echo "Sending trigger..."
echo "trigger" | socat - UNIX-CONNECT:/tmp/event_sock.sock

echo "Waiting for clip generation (7s)..."
sleep 7

echo "Stopping processes..."
kill -SIGINT "$CLIPPER_PID" 2>/dev/null || true
kill -SIGINT "$PROCESSOR_PID" 2>/dev/null || true
wait "$CLIPPER_PID" 2>/dev/null || true
wait "$PROCESSOR_PID" 2>/dev/null || true

echo "Checking for generated clip..."
CLIP_COUNT=$(find "$OUTPUT_DIR" -name "*.mp4" | wc -l)

if [ "$CLIP_COUNT" -eq 1 ]; then
    echo "PASS: Clip generated successfully"
    ls -la "$OUTPUT_DIR"
    exit 0
else
    echo "FAIL: Expected 1 clip, found $CLIP_COUNT"
    exit 1
fi