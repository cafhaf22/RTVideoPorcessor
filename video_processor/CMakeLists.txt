add_executable(vprocessor video_processor.cpp)

target_link_libraries(vprocessor PRIVATE posix_shared_memory ${OpenCV_LIBS})
target_include_directories(vprocessor PRIVATE ${OpenCV_LIBS})