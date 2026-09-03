#ifndef ISHARED_MEMORY_WRITER_H_
#define ISHARED_MEMORY_WRITER_H_

#include "common.h"

/**
 * @brief Abstraction over a write-only shared memory channel used to
 * publish video frames to a reader (e.g. VideoClipper) in another
 * process. Implementations are responsible for allocation and
 * inter-process synchronization.
 */
class ISharedMemoryWriter {
public:
    virtual ~ISharedMemoryWriter() = default;

    /**
     * @brief Allocates/prepares the shared memory region and writes
     * the video header (dimensions, fps, frame size). Must be called
     * once before any WriteFrame() call.
     * @param header Metadata describing the video stream.
     * @return Status::OK on success, or an error status.
     */
    virtual Status Initialize(const vHeader_t& header) = 0;

    /**
     * @brief Writes a single frame (metadata + pixel data) to the
     * shared memory region, overwriting the previous frame.
     * @param frame_data Per-frame metadata (timestamp, frame number).
     * @param pixel_data Pointer to raw pixel bytes to copy.
     * @param pixel_data_size Size of pixel_data in bytes.
     * @return Status::OK on success, or an error status.
     */
    virtual Status WriteFrame(const vFrameData_t& frame_data, const uint8_t* pixel_data, size_t pixel_data_size) = 0;
};
#endif // ISHARED_MEMORY_WRITER_H_