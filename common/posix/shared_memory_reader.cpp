#include "shared_memory_reader.h"

#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <ctime>
#include <syslog.h>
#include <sys/stat.h>

PosixSharedMemoryReader::PosixSharedMemoryReader(const std::string& shm_name)
    : shm_name_(shm_name), sem_name_("/challenge_sem"), notify_ready_sem_name_("/notify_ready_sem"), 
    map_(nullptr), total_size_(0), connected_(false), sem_(nullptr), 
    notify_ready_sem_(nullptr) {
}

PosixSharedMemoryReader::~PosixSharedMemoryReader() {
    if (map_ != nullptr && map_ != MAP_FAILED) {
        munmap(map_, total_size_);
    }

    if (sem_ != nullptr && sem_ != SEM_FAILED) {
        sem_close(sem_);
    }

    if(notify_ready_sem_ != nullptr && notify_ready_sem_ != SEM_FAILED) {
        sem_close(notify_ready_sem_);
    }
}

Status PosixSharedMemoryReader::Connect() {
    if(connected_) {
        syslog(LOG_WARNING, "Shared memory space is connected");
        return Status::OK;
    }

    int32_t shm_fd = shm_open(shm_name_.c_str(), O_RDONLY, 0666);
    if (shm_fd == -1) {
        syslog(LOG_ERR, "shm_open failed for %s", shm_name_.c_str());
        return Status::SharedMemoryError;
    }

    struct stat shm_stat;
    if (fstat(shm_fd, &shm_stat) == -1) {
        syslog(LOG_ERR, "fstat error");
        close(shm_fd);
        return Status::SharedMemoryError;
    }
    total_size_ = shm_stat.st_size; 

    map_ = mmap(nullptr, total_size_, PROT_READ, MAP_SHARED, shm_fd, 0);
    if(map_ == MAP_FAILED)
    {
        syslog(LOG_ERR, "mmap failed");
        close(shm_fd);
        return Status::SharedMemoryError;
    }
    close(shm_fd);
    
    sem_ = sem_open(sem_name_.c_str(), 0);
    if (sem_ == SEM_FAILED) {
        syslog(LOG_ERR, "sem_open failed for %s", sem_name_.c_str());
        return Status::SharedMemoryError;
    }

    notify_ready_sem_ = sem_open(notify_ready_sem_name_.c_str(), 0);
    if(notify_ready_sem_ == SEM_FAILED) {
        syslog(LOG_ERR, "sem_open failed for %s", notify_ready_sem_name_.c_str());
        return Status::SharedMemoryError;
    }

    connected_ = true;
    syslog(LOG_INFO, "Connected to shared memory space");
    return Status::OK;
}

Status PosixSharedMemoryReader::ReadHeader(vHeader_t& header) {
    if(!connected_) {
        return Status::SharedMemoryError;
    }

    std::memcpy(&header, map_, sizeof(vHeader_t));
    return Status::OK;
}

Status PosixSharedMemoryReader::ReadFrame(vFrameData_t& out_frame_data,
                                        uint8_t* out_pixel_buffer,
                                        size_t buffer_size) {
    if(!connected_) {
        return Status::SharedMemoryError;
    }

    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_nsec += 200000000;  // 200ms 
    if (timeout.tv_nsec >= 1000000000) {
        timeout.tv_sec += 1;
        timeout.tv_nsec -= 1000000000;
    }

    if (sem_timedwait(notify_ready_sem_, &timeout) == -1) {
        return Status::Timeout;
    }

    sem_wait(sem_);
    std::memcpy(&out_frame_data, GetFrameDataPtr(), sizeof(vFrameData_t));
    std::memcpy(out_pixel_buffer, GetPixelBufferPtr(), buffer_size);
    sem_post(sem_);
    return Status::OK;
}

uint8_t* PosixSharedMemoryReader::GetFrameDataPtr() const {
    return sizeof(vHeader_t) + reinterpret_cast<uint8_t*>(map_);
}

uint8_t* PosixSharedMemoryReader::GetPixelBufferPtr() const {
    return sizeof(vFrameData_t) + GetFrameDataPtr();
}