#include "shared_memory_writer.h"

#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <syslog.h>

PosixSharedMemoryWriter::PosixSharedMemoryWriter(const std::string& shm_name)
    : shm_name_(shm_name), sem_name_("/derq_challenge_sem"), map_(nullptr), 
    total_size_(0), initialized_(false), sem_(nullptr) {
}

PosixSharedMemoryWriter::~PosixSharedMemoryWriter() {
    if (map_ != nullptr && map_ != MAP_FAILED) {
        munmap(map_, total_size_);
    }
    
    if (sem_ != nullptr && sem_ != SEM_FAILED) {
        sem_close(sem_);
    }

    if (initialized_) {
        shm_unlink(shm_name_.c_str());
        sem_unlink(sem_name_.c_str());
    }
}

Status PosixSharedMemoryWriter::Initialize(const vHeader_t& header) {
    if(initialized_)
    {
        syslog(LOG_WARNING, "Shared memory space already exists");
        return Status::OK;
    }
    
    total_size_ = sizeof(vHeader_t) + sizeof(vFrameData_t) + header.frame_size_bytes;
    
    int32_t shm_fd = shm_open(shm_name_.c_str(), O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        syslog(LOG_ERR, "shm_open failed for %s", shm_name_.c_str());
        return Status::SharedMemoryError;
    }

    if (ftruncate(shm_fd, total_size_) == -1) {
        syslog(LOG_ERR, "ftruncate failed");
        close(shm_fd);
        return Status::SharedMemoryError;
    }

    map_ = mmap(nullptr, total_size_, PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if(map_ == MAP_FAILED)
    {
        syslog(LOG_ERR, "mmap failed");
        return Status::SharedMemoryError;
    }
    close(shm_fd);

    // Binary semaphore (initial value 1) protecting the frame_data + pixel
    // buffer section from torn reads/writes between processes.
    sem_ = sem_open(sem_name_.c_str(), O_CREAT, 0666, 1);
    if (sem_ == SEM_FAILED) {
        syslog(LOG_ERR, "sem_open failed for %s", sem_name_.c_str());
        return Status::SharedMemoryError;
    }

    // at this point is not strictly needed to protect the write
    std::memcpy(map_, &header, sizeof(header));

    initialized_ = true;
    syslog(LOG_INFO, "Shared memory space created");

    return Status::OK;
}

Status PosixSharedMemoryWriter::WriteFrame(const vFrameData_t& frame_data,
                                            const uint8_t* pixel_data,
                                            size_t pixel_data_size) {
    if (!initialized_) {
        return Status::SharedMemoryError;
    }
    
    sem_wait(sem_);
    std::memcpy(GetFrameDataPtr(), &frame_data, sizeof(vFrameData_t));
    std::memcpy(GetPixelBufferPtr(), pixel_data, pixel_data_size);
    sem_post(sem_);

    return Status::OK;
}

uint8_t* PosixSharedMemoryWriter::GetFrameDataPtr() const {
    return sizeof(vHeader_t) + reinterpret_cast<uint8_t*>(map_);
}

uint8_t* PosixSharedMemoryWriter::GetPixelBufferPtr() const {
    return sizeof(vFrameData_t) + GetFrameDataPtr();
}