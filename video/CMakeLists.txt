include(FindPkgConfig)
pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET
        libavdevice libavformat libavcodec libswscale libavutil)

add_granite_internal_lib(granite-video ffmpeg.cpp ffmpeg.hpp)
target_link_libraries(granite-video
        PUBLIC granite-vulkan
        PRIVATE PkgConfig::LIBAV granite-threading)
if (GRANITE_AUDIO)
    target_link_libraries(granite-video PRIVATE granite-audio)
endif()
target_include_directories(granite-video PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(granite-video PUBLIC HAVE_GRANITE_FFMPEG)