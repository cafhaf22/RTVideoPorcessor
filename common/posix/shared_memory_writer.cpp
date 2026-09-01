#include "shared_memory_writer.h"

#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <syslog.h>

PosixSharedMemoryWriter::PosixSharedMemoryWriter(const std::string& shm_name)
    : shm_name_(shm_name), map_(nullptr), total_size_(0), initialized_(false) {
}

PosixSharedMemoryWriter::~PosixSharedMemoryWriter() {
    if (map_ != nullptr && map_ != MAP_FAILED) {
        munmap(map_, total_size_);
    }

    if (initialized_) {
        shm_unlink(shm_name_.c_str());
    }
}

Status PosixSharedMemoryWriter::Initialize(const vHeader_t& header) {
    if(initialized_)
    {
        return Status::SharedMemoryError;
    }
    
    total_size_ = sizeof(vHeader_t) + sizeof(vFrameData_t) + header.frame_size_bytes;
    
    int32_t shm_fd = shm_open(shm_name_.c_str(), O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        syslog(LOG_ERR, "shm_open failed for %s", shm_name_.c_str());
        return Status::SharedMemoryError;
    }

    if (ftruncate(shm_fd, total_size_) == -1) {
        syslog(LOG_ERR, "ftruncate failed");
        return Status::SharedMemoryError;
    }

    map_ = mmap(nullptr, total_size_, PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if(map_ == MAP_FAILED)
    {
        syslog(LOG_ERR, "mmap failed");
        return Status::SharedMemoryError;
    }
    close(shm_fd);

    std::memcpy(map_, &header, sizeof(header));
    initialized_ = true;

    return Status::OK;
}

Status PosixSharedMemoryWriter::WriteFrame(const vFrameData_t& frame_data,
                                            const uint8_t* pixel_data,
                                            size_t pixel_data_size) {
    if (!initialized_) {
        return Status::SharedMemoryError;
    }
    
    std::memcpy(GetFrameDataPtr(), &frame_data, sizeof(vFrameData_t));
    std::memcpy(GetPixelBufferPtr(), pixel_data, pixel_data_size);
    return Status::OK;
}

uint8_t* PosixSharedMemoryWriter::GetFrameDataPtr() const {
    return sizeof(vHeader_t) + reinterpret_cast<uint8_t*>(map_);
}

uint8_t* PosixSharedMemoryWriter::GetPixelBufferPtr() const {
    return sizeof(vFrameData_t) + GetFrameDataPtr();
}