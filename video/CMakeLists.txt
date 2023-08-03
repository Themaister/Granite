add_granite_internal_lib(granite-video
        ffmpeg_encode.cpp ffmpeg_encode.hpp
        ffmpeg_hw_device.cpp ffmpeg_hw_device.hpp)
if (GRANITE_VULKAN_SYSTEM_HANDLES)
    target_sources(granite-video PRIVATE ffmpeg_decode.cpp ffmpeg_decode.hpp)
endif()

# FFmpeg macro uses designated initializer.
target_compile_features(granite-video PRIVATE cxx_std_20)

option(GRANITE_FFMPEG_INSTALL_PREFIX "Override FFmpeg install prefix." "")
if (GRANITE_FFMPEG_INSTALL_PREFIX)
    # For MSVC. It does not play well with Msys2 pkg-configs.
    message("FFmpeg: install prefix ${GRANITE_FFMPEG_INSTALL_PREFIX}.")
    target_include_directories(granite-video PRIVATE ${GRANITE_FFMPEG_INSTALL_PREFIX}/include)
    find_library(AVDEVICE avdevice NAMES libavdevice
        PATHS ${GRANITE_FFMPEG_INSTALL_PREFIX}/lib ${GRANITE_FFMPEG_INSTALL_PREFIX}/bin
        NO_DEFAULT_PATH)
    find_library(AVFORMAT avformat NAMES libavformat
        PATHS ${GRANITE_FFMPEG_INSTALL_PREFIX}/lib ${GRANITE_FFMPEG_INSTALL_PREFIX}/bin
        NO_DEFAULT_PATH)
    find_library(AVCODEC avcodec NAMES libavcodec
        PATHS ${GRANITE_FFMPEG_INSTALL_PREFIX}/lib ${GRANITE_FFMPEG_INSTALL_PREFIX}/bin
        NO_DEFAULT_PATH)
    find_library(AVUTIL avutil NAMES libavutil
        PATHS ${GRANITE_FFMPEG_INSTALL_PREFIX}/lib ${GRANITE_FFMPEG_INSTALL_PREFIX}/bin
        NO_DEFAULT_PATH)
    message("FFmpeg: libavdevice (${AVDEVICE}).")
    message("FFmpeg: libavformat (${AVFORMAT}).")
    message("FFmpeg: libavcodec (${AVCODEC}).")
    message("FFmpeg: libavutil (${AVUTIL}).")
    target_link_libraries(granite-video PRIVATE ${AVDEVICE} ${AVFORMAT} ${AVCODEC} ${AVUTIL})
else()
    include(FindPkgConfig)
    pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET
        libavdevice libavformat libavcodec libavutil)
    target_link_libraries(granite-video PRIVATE PkgConfig::LIBAV)
endif()

target_link_libraries(granite-video
    PUBLIC granite-vulkan
    PRIVATE granite-threading granite-math)
if (GRANITE_AUDIO)
    target_link_libraries(granite-video PRIVATE granite-audio)
endif()
if (GRANITE_FFMPEG_VULKAN)
    target_compile_definitions(granite-video PRIVATE HAVE_FFMPEG_VULKAN)
endif()
if (GRANITE_FFMPEG_VULKAN_ENCODE)
    target_compile_definitions(granite-video PRIVATE HAVE_FFMPEG_VULKAN_ENCODE)
endif()
target_include_directories(granite-video PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(granite-video PUBLIC HAVE_GRANITE_FFMPEG)
