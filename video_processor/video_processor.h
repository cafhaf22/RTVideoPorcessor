#ifndef VIDEO_PROCESSOR_H_
#define VIDEO_PROCESSOR_H_

#include <opencv2/opencv.hpp>
#include "common/common.h"
#include "common/ishared_memory_writer.h"

/**
 * @brief Opens an .mp4 video file, reads its frames, and writes them
 * to a shared memory space, replicating the source video's frame
 * rate in real time until the video ends or SIGINT is received.
 */
class VideoProcessor
{
public:
    /**
     * @brief Constructs a VideoProcessor bound to a shared memory writer.
     * @param writer Destination for the frames read from the video file.
     */
    explicit VideoProcessor(ISharedMemoryWriter& writer);
    VideoProcessor(const VideoProcessor& other) = delete;
    VideoProcessor(VideoProcessor&& other) = delete;
    ~VideoProcessor();

    /**
     * @brief Opens an .mp4 file for reading.
     * @param filepath Path to the video file.
     * @return Status::OK on success, or an error status.
     */
    Status LoadVideoFile(const char* filepath);

    /**
     * @brief Reads and writes frames to shared memory at the video's
     * original frame rate until the video ends or SIGINT is received.
     * @return Status::OK on success, or an error status.
     */
    Status ProcessVideoFrames();


private:
    cv::VideoCapture cap_;
    ISharedMemoryWriter& writer_;

};

#endif // VIDEO_PROCESSOR_H_