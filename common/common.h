#ifndef COMMON_H_
#define COMMON_H_
#include <cstdint>

// TODO move to common/
enum class Status {
    OK,
    Failure,
    FileNotFound,
    InvalidFormat,
    SharedMemoryError,
    ErrorProcessingFrame,
};

struct vHeader_t {
    uint16_t width;
    uint16_t height;
    uint8_t fps;
    uint32_t frame_size_bytes;
};

struct vFrameData_t {
    int64_t timestamp;
    uint32_t frame_number; // for debugging
};

#endif // COMMON_H_