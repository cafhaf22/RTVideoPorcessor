#include "video_processor.h"
#include "common/posix/shared_memory_writer.h"

#include <iostream>
#include <filesystem>
#include <syslog.h>
#include <csignal>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <chrono>


namespace fs = std::filesystem;
namespace {
constexpr uint8_t kChannels = 3; // OpenCV VideoCapture decodes in BGR by default
std::atomic<bool> g_should_stop{false};
std::mutex g_cv_mutex;
std::condition_variable g_cv;

void SignalHandler(int signum) {
    syslog(LOG_INFO, "Received signal: %d", signum);
    g_should_stop.store(true);
    g_cv.notify_one();
}
}  // namespace


VideoProcessor::VideoProcessor(ISharedMemoryWriter& writer) : 
    writer_(writer) {
    openlog("VideoProcessor", LOG_PID | LOG_NDELAY, LOG_USER);
    syslog(LOG_INFO, "VideoProcessor created");
}

VideoProcessor::~VideoProcessor() {
    syslog(LOG_INFO, "Exiting video processor");
}

Status VideoProcessor::LoadVideoFile(const char* filepath)
{
    if(!fs::exists(filepath)) {
        syslog(LOG_ERR, "File: %s does not exists", filepath);
        return Status::FileNotFound;
    }

    if(fs::path(filepath).extension() != ".mp4") {
        syslog(LOG_ERR, "Invalid file format");
        return Status::InvalidFormat;
    }

    cap_.open(filepath, cv::CAP_ANY);
    if(!cap_.isOpened()) {
        return Status::Failure;
    }

    return Status::OK;
    
}

Status VideoProcessor::ProcessVideoFrames()
{
    Status status = Status::OK;
    vHeader_t header;
    header.width = static_cast<uint16_t>(cap_.get(cv::CAP_PROP_FRAME_WIDTH));
    header.height = static_cast<uint16_t>(cap_.get(cv::CAP_PROP_FRAME_HEIGHT));
    header.fps = cap_.get(cv::CAP_PROP_FPS);
    header.frame_size_bytes = header.width * header.height * kChannels;

    if(writer_.Initialize(header) != Status::OK) {
        return Status::ErrorProcessingFrame;
    }

    auto frame_interval = std::chrono::duration<double>(1.0 / header.fps);
    auto next_frame_time = std::chrono::steady_clock::now();
    uint32_t frame_number = 0;
    
    while(!g_should_stop.load()) {
        cv::Mat frame;
        if(bool ret = cap_.read(frame); ret == false) {
            int32_t total_frames = static_cast<int32_t>(cap_.get(cv::CAP_PROP_FRAME_COUNT));
            if(frame.empty()) {
                syslog(LOG_INFO, "Video frame processing end: %d/%d", frame_number, total_frames);
            }
            else {
                status = Status::ErrorProcessingFrame;
                syslog(LOG_ERR, "Failing capturing frame: %d/%d", frame_number, total_frames);
            } 
            break;
        }
        // frame read increase counter
        frame_number++;
        // prepare frame data
        vFrameData_t frame_data;
        frame_data.frame_number = frame_number;
        frame_data.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::system_clock::now().time_since_epoch()).count();
        //write frame into shm
        if(writer_.WriteFrame(frame_data, frame.data, frame.total() * frame.elemSize()) != Status::OK) {
            syslog(LOG_ERR, "Failed to write frame %d to shared memory", frame_number);
            status = Status::ErrorProcessingFrame;
            break;
        }
        // update wait time for next frame
        next_frame_time += std::chrono::duration_cast<std::chrono::steady_clock::duration>(frame_interval);
        // wait for next frame or SIGINT happens
        std::unique_lock<std::mutex> lock(g_cv_mutex);
        g_cv.wait_until(lock, next_frame_time, [] { return g_should_stop.load(); });

    }

    return status;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <video_path>" << std::endl;
        return 1;
    }

    std::signal(SIGINT, SignalHandler);

    PosixSharedMemoryWriter writer("/derq_video_shm");
    VideoProcessor processor(writer);

    if (processor.LoadVideoFile(argv[1]) != Status::OK) {
        std::cerr << "Failed to load video file: " << argv[1] << std::endl;
        return 1;
    }

    if (processor.ProcessVideoFrames() != Status::OK) {
        std::cerr << "Error occurred while processing video frames" << std::endl;
        return 1;
    }

    return 0;
}