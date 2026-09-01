#ifndef ISHARED_MEMORY_READER_H_
#define ISHARED_MEMORY_READER_H_

#include "common.h"

class ISharedMemoryReader {
public:
    virtual ~ISharedMemoryReader() = default;

    virtual Status Connect() = 0;
    virtual Status ReadHeader(vHeader_t& header) = 0;
    virtual Status ReadFrame(vFrameData_t& out_frame_data, uint8_t* out_pixel_buffer, size_t buffer_size) = 0;
};
#endif // ISHARED_MEMORY_READER_H_