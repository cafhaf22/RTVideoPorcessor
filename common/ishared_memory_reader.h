#ifndef ISHARED_MEMORY_READER_H_
#define ISHARED_MEMORY_READER_H_

#include "common.h"

/**
 * @brief Abstraction over a read-only shared memory channel used to
 * consume video frames published by a writer (e.g. VideoProcessor) in
 * another process.
 */
class ISharedMemoryReader {
public:
    virtual ~ISharedMemoryReader() = default;

    /**
     * @brief Connects to an already-existing shared memory region
     * created by a writer. Must be called once before ReadHeader()
     * or ReadFrame().
     * @return Status::OK on success, or an error status.
     */
    virtual Status Connect() = 0;

    /**
     * @brief Reads the video header (dimensions, fps, frame size).
     * @param header Output parameter filled with the header data.
     * @return Status::OK on success, or an error status.
     */
    virtual Status ReadHeader(vHeader_t& header) = 0;

    /**
     * @brief Blocks until a new frame is available, then reads it.
     * @param out_frame_data Output parameter filled with per-frame metadata.
     * @param out_pixel_buffer Caller-owned buffer to receive pixel bytes.
     * @param buffer_size Size of out_pixel_buffer in bytes.
     * @return Status::OK on success, or an error/timeout status.
     */
    virtual Status ReadFrame(vFrameData_t& out_frame_data, uint8_t* out_pixel_buffer, size_t buffer_size) = 0;
};
#endif // ISHARED_MEMORY_READER_H_