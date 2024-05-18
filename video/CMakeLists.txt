include(FindPkgConfig)
pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET
        libavdevice libavformat libavcodec libavutil)

add_granite_internal_lib(granite-video
        ffmpeg_encode.cpp ffmpeg_encode.hpp
        slangmosh_encode_iface.hpp
        ffmpeg_decode.cpp ffmpeg_decode.hpp slangmosh_decode_iface.hpp
        ffmpeg_hw_device.cpp ffmpeg_hw_device.hpp)

target_link_libraries(granite-video
        PUBLIC granite-vulkan
        PRIVATE PkgConfig::LIBAV granite-threading granite-math)
if (GRANITE_AUDIO)
    target_link_libraries(granite-video PRIVATE granite-audio)
endif()
if (GRANITE_FFMPEG_VULKAN)
    target_compile_definitions(granite-video PRIVATE HAVE_FFMPEG_VULKAN)
endif()
target_link_libraries(granite-video PRIVATE pyroenc)
target_include_directories(granite-video PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(granite-video PUBLIC HAVE_GRANITE_FFMPEG)