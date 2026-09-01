#include <gtest/gtest.h>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <sys/mman.h>

#include "common/common.h"
#include "common/ishared_memory_writer.h"
#include "common/ishared_memory_reader.h"
#include "common/posix/shared_memory_writer.h"
#include "common/posix/shared_memory_reader.h"

namespace fs = std::filesystem;

namespace {

constexpr const char* kTestShmName = "/derq_test_shm";
constexpr const uint16_t kChannels = 3;

class PosixSharedMemoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_header_.width = 128;
        test_header_.height = 64;
        test_header_.fps = 30.0;
        test_header_.frame_size_bytes = test_header_.width * test_header_.height * kChannels;
        
        test_frame_.frame_number = 10;
        test_frame_.timestamp = 1788220800; // september 1, 2026 at 00:00:00 UTC
    }

    void TearDown() override {
        if(fs::exists(std::string("/dev/shm") + kTestShmName)) {
            shm_unlink(kTestShmName);
        }
    }

    void CheckSharedHeaderAndFrameData(const vHeader_t& read_header, const vFrameData_t& read_frame) const {
        EXPECT_EQ(test_header_.width, read_header.width);
        EXPECT_EQ(test_header_.height, read_header.height);
        EXPECT_EQ(test_header_.fps, read_header.fps);
        EXPECT_EQ(test_header_.frame_size_bytes, read_header.frame_size_bytes);

        EXPECT_EQ(test_frame_.frame_number, read_frame.frame_number);
        EXPECT_EQ(test_frame_.timestamp, read_frame.timestamp);
    }
    void CheckSharedDataBuffer(const uint8_t* expected_data, const uint8_t* data_read, const size_t read_data_size) const {
        EXPECT_EQ(0, std::memcmp(expected_data, data_read, read_data_size));
    }

    PosixSharedMemoryWriter writer_{kTestShmName};
    PosixSharedMemoryReader reader_{kTestShmName};
    vHeader_t test_header_;
    vFrameData_t test_frame_;
};

TEST_F(PosixSharedMemoryTest, HappyPath_WriteAndReadBackMatches) {
    vHeader_t read_header;
    vFrameData_t read_frame;

    uint8_t* test_data = new uint8_t [test_header_.frame_size_bytes];
    std::memset(test_data, 0x1C, test_header_.frame_size_bytes);

    // write the data into the shared space
    ASSERT_EQ(Status::OK, writer_.Initialize(test_header_));
    EXPECT_EQ(Status::OK, writer_.WriteFrame(test_frame_, test_data, test_header_.frame_size_bytes));
    // read the data from the shared space
    ASSERT_EQ(Status::OK, reader_.Connect());
    EXPECT_EQ(Status::OK, reader_.ReadHeader(read_header));
    uint8_t* read_data = new uint8_t [read_header.frame_size_bytes];
    EXPECT_EQ(Status::OK, reader_.ReadFrame(read_frame, read_data, read_header.frame_size_bytes));
    // Compare what is being write against what is being read
    CheckSharedHeaderAndFrameData(read_header, read_frame);
    CheckSharedDataBuffer(test_data, read_data, read_header.frame_size_bytes);

    delete [] test_data;
    delete [] read_data;
}

TEST_F(PosixSharedMemoryTest, DoubleInitialize_ReturnsOk) {
    EXPECT_EQ(Status::OK, writer_.Initialize(test_header_));
    EXPECT_EQ(Status::OK, writer_.Initialize(test_header_));
}

TEST_F(PosixSharedMemoryTest, ConnectWithoutWriter_ReturnsError) {
    EXPECT_EQ(Status::SharedMemoryError, reader_.Connect());
}

}  // namespace