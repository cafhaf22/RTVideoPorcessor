#include "video_clipper.h"
#include "common/posix/shared_memory_reader.h"

#include <iostream>
#include <syslog.h>
#include <csignal>
#include <atomic>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>
#include <cstring>
#include <unistd.h>
#include <opencv2/opencv.hpp>

namespace {
constexpr const char* kSocketPath = "/tmp/event_sock.sock";
std::atomic<bool> g_should_stop{false};
std::atomic<int16_t> g_pending_triggers{0};

void SignalHandler(int signum) {
    syslog(LOG_INFO, "Received signal: %d", signum);
    g_should_stop.store(true);
}

void ListenForTrigger(int sock_fd) {
    if(listen(sock_fd, 1) == -1) {
        syslog(LOG_ERR, "Error listening on socket: %s", kSocketPath);
        close(sock_fd);
        return;
    }
    else {
        syslog(LOG_INFO, "Socket listening on: %s", kSocketPath);
    }
    struct timeval timeout;
    timeout.tv_sec = 1;  // wait 1 second
    timeout.tv_usec = 0;

    int32_t notifier_fd = - 1;
    while(!g_should_stop.load()) {
        fd_set accept_fds;
        FD_ZERO(&accept_fds);
        FD_SET(sock_fd, &accept_fds);

        timeout.tv_sec = 1;  // wait 1 second
        timeout.tv_usec = 0;

        int ready = select(sock_fd + 1, &accept_fds, nullptr, nullptr, &timeout);
        if(ready > 0 && FD_ISSET(sock_fd, &accept_fds)) {
            notifier_fd = accept(sock_fd, nullptr, nullptr);
            break;
        }
    }

    if (g_should_stop.load()) {
        syslog(LOG_INFO, "Shutting down before accepting a connection");
        close(sock_fd);
        return;
    }

    if(notifier_fd == -1) {
        syslog(LOG_ERR, "Error accepting connection");
        close(sock_fd);
        return;
    }

    syslog(LOG_INFO, "Connection accepted");

    while (!g_should_stop.load()) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(notifier_fd, &read_fds);

        timeout.tv_sec = 1;  // wait 1 second
        timeout.tv_usec = 0;

        int ready = select(notifier_fd + 1, &read_fds, nullptr, nullptr, &timeout);
        if(ready > 0 && FD_ISSET(notifier_fd, &read_fds)) {
            // data is available and recv would block for a small amount of time
            char buffer[256];
            ssize_t bytes_read = recv(notifier_fd, buffer, sizeof(buffer), 0);
            if (bytes_read > 0) {
                g_pending_triggers.fetch_add(1);
                syslog(LOG_INFO, "Trigger received");
            } else {
                break;  // client closed conenction or there wass an error
            }
        }
    }
    close(notifier_fd);
    close(sock_fd);
}
}  // namespace

VideoClipper::VideoClipper(ISharedMemoryReader& reader)
    : reader_(reader), sock_fd_(-1) {
    openlog("VideoClipper", LOG_PID | LOG_NDELAY, LOG_USER);
    syslog(LOG_INFO, "VideoClipper created");
}

VideoClipper::~VideoClipper() {
    if (sock_fd_ != -1) {
        close(sock_fd_);
    }
    unlink(kSocketPath);
    syslog(LOG_INFO, "Exiting video clipper");
}

Status VideoClipper::Initialize(const char* filepath) {
    if(reader_.Connect() != Status::OK) {
        syslog(LOG_ERR, "Error connecting to the shared memory space");
        return Status::SharedMemoryError;
    }

    if(reader_.ReadHeader(header_) != Status::OK) {
        syslog(LOG_ERR, "Error reading the video header");
        return Status::SharedMemoryError;
    }

    sock_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if(int err = errno; sock_fd_ == -1) {
        syslog(LOG_ERR, "Error creating socket, errono: %d", err);
        return Status::Failure; // used for generic errors
    }

    struct sockaddr_un sock_addr{};
    std::memset(&sock_addr, 0, sizeof(sock_addr));
    sock_addr.sun_family = AF_UNIX;
    std::strncpy(sock_addr.sun_path, kSocketPath, sizeof(sock_addr.sun_path) - 1);
    
    if(bind(sock_fd_, reinterpret_cast<struct sockaddr*>(&sock_addr), sizeof(sock_addr)) == -1) {
        int err = errno;
        syslog(LOG_ERR, "Error binding socket, errono: %d", err);
        close(sock_fd_);
        return Status::Failure;
    }
    
    output_dir_ = filepath;
    listener_thread_ = std::thread(ListenForTrigger, sock_fd_);
    return Status::OK;
}

Status VideoClipper::ProcessAndClip() {

    while(!g_should_stop.load()){
        // read the frame
        FrameCache frame_entry;
        frame_entry.pixel_data = std::make_shared<std::vector<uint8_t>>(header_.frame_size_bytes);
        if(reader_.ReadFrame(frame_entry.frame_data, frame_entry.pixel_data->data(), header_.frame_size_bytes) != Status::OK) {
            syslog(LOG_WARNING, "Error reading frame from shared memory space");
            // This could be an error or a timeout
            continue;
        }
        // add the entry to the cached frames deque
        frame_cache_.push_back(frame_entry);


        // once the deque has reached the total of frames for 9 seconds pop the oldest element
        int64_t elapsed_ms = frame_cache_.back().frame_data.timestamp - frame_cache_.front().frame_data.timestamp;
        while(elapsed_ms >= 9000) {
            frame_cache_.pop_front();
            // keep at most 9s of frames in the cache - discard the oldest ones
            // once the window is exceeded
            if(frame_cache_.empty()) {
                break;
            }
            elapsed_ms = frame_cache_.back().frame_data.timestamp - frame_cache_.front().frame_data.timestamp;
        }

        // check for pending triggers and create their own copy of the snapshot pointint at the main frame cache
        int16_t pending = g_pending_triggers.exchange(0); // this will reset g_pending_triggers to 0 
        for(int16_t i = 0; i < pending; i++) {
            PendingClip new_clip;
            new_clip.snapshot = frame_cache_; // cheap copy thanks to shared_ptr
            new_clip.trigger_ts = frame_entry.frame_data.timestamp;
            pending_clips_.push_back(std::move(new_clip)); // move it so the shared_ptr usse count does not increase
        }

        // check every pending clip, if the 6s window after trigger is completed
        // then generate its own video and remove it from the list
        for(auto it = pending_clips_.begin(); it != pending_clips_.end();) {
            int64_t elapsed_since_trigger = frame_entry.frame_data.timestamp - it->trigger_ts;
            if(elapsed_since_trigger >= 6000) {
                // combine the snapshot with the 6s of frames stored after trigger
                std::deque<FrameCache> clip = it->snapshot; // it will increase the use count reference of the shared ptr but it will be out of scope fast
                for(auto& frame: frame_cache_) { // we still check the cache because is our source of true
                    // but only do it for every clip after their trigger timestamp
                    if(frame.frame_data.timestamp > it->trigger_ts) {
                        clip.push_back(frame);
                    }
                }
                 //Create teh video clip
                GenerateVideoClip(clip, it->trigger_ts);
                it = pending_clips_.erase(it);
            }
            else {
                ++it; // after dispatching a clip now increase the iterator
            }
        }
    }

    if (listener_thread_.joinable()) {
        listener_thread_.join();
    }
    return Status::OK;
}

Status VideoClipper::GenerateVideoClip(const std::deque<FrameCache>& clip_frames, const int64_t trigger_ts) const {
    // Check that there is something int the deque
    if(clip_frames.empty()) {
        syslog(LOG_WARNING, "No frames to generate video clip");
        return Status::ErrorProcessingFrame;
    }

    // Create the .mp4 file name
    std::string filename = output_dir_ + "/" + std::to_string(trigger_ts) + ".mp4";

    cv::VideoWriter writer(filename, cv::VideoWriter::fourcc('m', 'p', '4', 'v'),
        header_.fps, cv::Size(header_.width, header_.height));

    if (!writer.isOpened()) {
        syslog(LOG_ERR, "Failed to open video writer for %s", filename.c_str());
        return Status::Failure;
    }

    // write the frames using opencv VideoWriter to create the file
    for (const auto& c_frame : clip_frames) {
        cv::Mat frame(header_.height, header_.width, CV_8UC3,
                        const_cast<uint8_t*>(c_frame.pixel_data->data()));
        writer.write(frame);
    }

    writer.release();
    syslog(LOG_INFO, "Generated video clip: %s (%zu frames)", filename.c_str(), clip_frames.size());
    return Status::OK;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <video_output_path>" << std::endl;
        return 1;
    }

    std::signal(SIGINT, SignalHandler);

    PosixSharedMemoryReader reader("/video_shm");
    VideoClipper clipper(reader);

    if(clipper.Initialize(argv[1]) != Status::OK) {
        std::cerr << "Failed to initialize video clipper" << std::endl;
        return 1;
    }

    if (clipper.ProcessAndClip() != Status::OK) {
        std::cerr << "Error occurred while creating video clip" << std::endl;
        return 1;
    }

    return 0;
}