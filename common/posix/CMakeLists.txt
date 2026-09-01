add_library(posix_shared_memory 
            shared_memory_reader.cpp
            shared_memory_writer.cpp)

target_include_directories(posix_shared_memory PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(posix_shared_memory PUBLIC common_interfaces)
