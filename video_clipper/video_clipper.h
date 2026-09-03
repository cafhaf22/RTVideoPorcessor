#ifndef VIDEO_CLIPPER_H_
#define VIDEO_CLIPPER_H_

#include <deque>
#include <vector>
#include <string>
#include <thread>
#include <memory>
#include "common/common.h"
#include "common/ishared_memory_reader.h"


class VideoClipper {
public:
    explicit VideoClipper(ISharedMemoryReader& reader);
    ~VideoClipper();
    
    VideoClipper(const VideoClipper& other) = delete;
    VideoClipper& operator=(const VideoClipper&) = delete;
    VideoClipper(VideoClipper&& other) = delete;
    VideoClipper& operator=(VideoClipper&&) = delete;

    Status Initialize(const char* filepath);
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

    Status GenerateVideoClip(const std::deque<FrameCache>& clip_frames, const int64_t trigger_ts) const;

};

#endif // VIDEO_CLIPPER_H_