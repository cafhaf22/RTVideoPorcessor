#ifndef VIDEO_PROCESSOR_H_
#define VIDEO_PROCESSOR_H_

#include <cstdint>
#include <opencv2/opencv.hpp>
#include "common/common.h"
#include "common/ishared_memory_writer.h"

/**
 * @brief This clas is in charge of open a video file in format mp4,
 * read the frames in the video and write them to a shared memory space in /dev/shm/derq_challenge
 * while replicates the video frame rate unitl a SIGINT is received 
 */
class VideoProcessor
{
public:
    explicit VideoProcessor(ISharedMemoryWriter& writer);
    VideoProcessor(const VideoProcessor& other) = delete;
    VideoProcessor(VideoProcessor&& other) = delete;
    ~VideoProcessor();

    Status LoadVideoFile(const char* filepath);
    Status ProcessVideoFrames();


private:
    cv::VideoCapture cap_;
    ISharedMemoryWriter& writer_; // TODO implement Posix writer

};

#endif // VIDEO_PROCESSOR_H_