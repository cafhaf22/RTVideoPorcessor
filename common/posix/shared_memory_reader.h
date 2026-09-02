#ifndef POSIX_SHARED_MEMORY_READER_H_
#define POSIX_SHARED_MEMORY_READER_H_

#include <string>
#include <semaphore.h>
#include "common/ishared_memory_reader.h"

class PosixSharedMemoryReader : public ISharedMemoryReader {
public:
    explicit PosixSharedMemoryReader(const std::string& shm_name);
    ~PosixSharedMemoryReader() override;

    PosixSharedMemoryReader(const PosixSharedMemoryReader&) = delete;
    PosixSharedMemoryReader& operator=(const PosixSharedMemoryReader&) = delete;
    PosixSharedMemoryReader(PosixSharedMemoryReader&&) = delete;
    PosixSharedMemoryReader& operator=(PosixSharedMemoryReader&&) = delete;

    Status Connect() override;
    Status ReadHeader(vHeader_t& header) override;
    Status ReadFrame(vFrameData_t& out_frame_data,
                    uint8_t* out_pixel_buffer,
                    size_t buffer_size) override;

private:
    uint8_t* GetFrameDataPtr() const;
    uint8_t* GetPixelBufferPtr() const;

    std::string shm_name_;
    std::string sem_name_;
    std::string notify_ready_sem_name_;
    void* map_;
    size_t total_size_;
    bool connected_;
    sem_t* sem_;
    sem_t* notify_ready_sem_;
};

#endif // POSIX_SHARED_MEMORY_READER_H_