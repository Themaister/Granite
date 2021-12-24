include(FindPkgConfig)
pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET
    libavdevice
    libavformat
    libavcodec
    libswscale
    libavutil)

add_granite_internal_lib(granite-video ffmpeg.cpp ffmpeg.hpp)
target_link_libraries(granite-video
        PUBLIC granite-vulkan
        PRIVATE PkgConfig::LIBAV)
target_include_directories(granite-video PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})