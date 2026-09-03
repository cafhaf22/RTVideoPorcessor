#ifndef VIDEO_CLIPPER_H_
#define VIDEO_CLIPPER_H_

#include <deque>
#include <vector>
#include <string>
#include <thread>
#include <memory>
#include "common/common.h"
#include "common/ishared_memory_reader.h"

/**
 * @brief Reads video frames from shared memory, maintains a rolling
 * 9-second buffer, and listens for trigger events over a UNIX socket.
 * Each trigger produces its own independent 15-second .mp4 clip
 * (9s before / 6s after the trigger); multiple concurrent/overlapping
 * triggers are supported.
 */
class VideoClipper {
public:
    /**
     * @brief Constructs a VideoClipper bound to a shared memory reader.
     * @param reader Source of the frames shared by VideoProcessor.
     */
    explicit VideoClipper(ISharedMemoryReader& reader);
    ~VideoClipper();
    
    VideoClipper(const VideoClipper& other) = delete;
    VideoClipper& operator=(const VideoClipper&) = delete;
    VideoClipper(VideoClipper&& other) = delete;
    VideoClipper& operator=(VideoClipper&&) = delete;

    /**
     * @brief Connects to shared memory, reads the video header, and
     * starts listening for trigger events on the socket.
     * @param filepath Output directory where generated clips are saved.
     * @return Status::OK on success, or an error status.
     */
    Status Initialize(const char* filepath);

    /**
     * @brief Main loop: reads frames, maintains the 9s buffer, and
     * generates a clip for each trigger once its 6s post-trigger
     * window completes. Runs until SIGINT is received.
     * @return Status::OK on success, or an error status.
     */
    Status ProcessAndClip();

private:

    struct FrameCache {
        vFrameData_t frame_data;
        std::shared_ptr<std::vector<uint8_t>> pixel_data;
    };

    struct PendingClip {
        std::deque<FrameCache> snapshot;
        int64_t trigger_ts;
    };

    ISharedMemoryReader& reader_;
    std::string output_dir_;
    int32_t sock_fd_;
    std::deque<FrameCache> frame_cache_;
    std::vector<PendingClip> pending_clips_;
    vHeader_t header_;
    std::thread listener_thread_;

    /**
     * @brief Writes a sequence of frames to a new .mp4 file named
     * after the trigger's timestamp.
     * @param clip_frames Ordered frames to write (9s before + 6s after).
     * @param trigger_ts Trigger timestamp, used as the output filename.
     * @return Status::OK on success, or an error status.
     */
    Status GenerateVideoClip(const std::deque<FrameCache>& clip_frames, const int64_t trigger_ts) const;

};

#endif // VIDEO_CLIPPER_H_