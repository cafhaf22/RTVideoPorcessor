#ifndef POSIX_SHARED_MEMORY_READER_H_
#define POSIX_SHARED_MEMORY_READER_H_

#include <string>
#include "common/ishared_memory_reader.h"

class PosixSharedMemoryReader : public ISharedMemoryReader {
public:
    PosixSharedMemoryReader(const std::string& shm_name);
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
    void* map_;
    size_t total_size_;
    bool connected_;
};

#endif // POSIX_SHARED_MEMORY_READER_H_