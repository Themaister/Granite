add_granite_library(filesystem STATIC filesystem.cpp filesystem.hpp path.hpp path.cpp)
target_include_directories(filesystem PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(filesystem util event)

target_compile_definitions(filesystem PRIVATE GRANITE_DEFAULT_BUILTIN_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/../assets\")
target_compile_definitions(filesystem PRIVATE GRANITE_DEFAULT_CACHE_DIRECTORY=\"${CMAKE_BINARY_DIR}/cache\")

if (WIN32)
    add_subdirectory(windows)
else()
    add_subdirectory(linux)
endif()
add_subdirectory(netfs)
target_link_libraries(filesystem filesystem-os filesystem-netfs)
