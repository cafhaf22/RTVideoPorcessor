cmake_minimum_required(VERSION 3.28)
project(DerqChallenge)

find_package(OpenCV REQUIRED)

add_subdirectory(common)
add_subdirectory(common/posix)
add_subdirectory(video_processor)

