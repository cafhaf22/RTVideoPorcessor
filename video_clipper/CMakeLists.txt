add_executable(vclipper video_clipper.cpp)

target_link_libraries(vclipper PRIVATE posix_shared_memory ${OpenCV_LIBS})
target_include_directories(vclipper PRIVATE ${OpenCV_INCLUDE_DIRS})