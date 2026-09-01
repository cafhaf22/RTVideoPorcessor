#ifndef ISHARED_MEMORY_WRITER_H_
#define ISHARED_MEMORY_WRITER_H_

#include "common.h"

class ISharedMemoryWriter {
public:
    virtual ~ISharedMemoryWriter() = default;
    
    virtual Status Initialize(const vHeader_t& header) = 0;
    virtual Status WriteFrame(const vFrameData_t& frame_data, const uint8_t* pixel_data, size_t pixel_data_size) = 0;
};
#endif // ISHARED_MEMORY_WRITER_H_