#ifndef POSIX_SHARED_MEMORY_WRITER_H_
#define POSIX_SHARED_MEMORY_WRITER_H_

#include <string>
#include <semaphore.h>
#include "common/ishared_memory_writer.h"

class PosixSharedMemoryWriter : public ISharedMemoryWriter{
public:
    explicit PosixSharedMemoryWriter(const std::string& shm_name);
    ~PosixSharedMemoryWriter() override;

    PosixSharedMemoryWriter(const PosixSharedMemoryWriter&) = delete;
    PosixSharedMemoryWriter& operator=(const PosixSharedMemoryWriter&) = delete;
    PosixSharedMemoryWriter(PosixSharedMemoryWriter&&) = delete;
    PosixSharedMemoryWriter& operator=(PosixSharedMemoryWriter&&) = delete;

    Status Initialize(const vHeader_t& header) override;
    Status WriteFrame(const vFrameData_t& frame_data,
                    const uint8_t* pixel_data,
                    size_t pixel_data_size) override;
private:
    uint8_t* GetFrameDataPtr() const;
    uint8_t* GetPixelBufferPtr() const;
    
    std::string shm_name_;
    std::string sem_name_;
    std::string notify_ready_sem_name_;
    void* map_;
    size_t total_size_;
    bool initialized_;
    sem_t* sem_;
    sem_t* notify_ready_sem_;
};

#endif // POSIX_SHARED_MEMORY_WRITER_H_