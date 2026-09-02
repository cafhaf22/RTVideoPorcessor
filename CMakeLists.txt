cmake_minimum_required(VERSION 3.28)
project(DerqChallenge)

find_package(OpenCV REQUIRED)

include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.16.0.zip
)
FetchContent_MakeAvailable(googletest)
enable_testing()

add_subdirectory(common)
add_subdirectory(common/posix)
add_subdirectory(video_processor)
add_subdirectory(video_clipper)
add_subdirectory(test)
