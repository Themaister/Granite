if (NOT GRANITE_LIBAV_PATH)
    set(GRANITE_LIBAV_PATH $ENV{GRANITE_LIBAV_PATH})
endif()

if (GRANITE_LIBAV_PATH)
    message("FFmpeg: Using precompiled path: ${GRANITE_LIBAV_PATH}.")
    add_library(LIBAV INTERFACE)
    add_library(PkgConfig::LIBAV ALIAS LIBAV)
    if (ANDROID)
        message("FFmpeg: ANDROID_ABI: ${ANDROID_ABI}.")
        target_include_directories(LIBAV INTERFACE ${GRANITE_LIBAV_PATH}/include/${ANDROID_ABI})
        target_link_libraries(LIBAV INTERFACE
                ${GRANITE_LIBAV_PATH}/lib/${ANDROID_ABI}/libavcodec.so
                ${GRANITE_LIBAV_PATH}/lib/${ANDROID_ABI}/libavformat.so
                ${GRANITE_LIBAV_PATH}/lib/${ANDROID_ABI}/libavdevice.so
                ${GRANITE_LIBAV_PATH}/lib/${ANDROID_ABI}/libswresample.so
                ${GRANITE_LIBAV_PATH}/lib/${ANDROID_ABI}/libavfilter.so
                ${GRANITE_LIBAV_PATH}/lib/${ANDROID_ABI}/libswscale.so
                ${GRANITE_LIBAV_PATH}/lib/${ANDROID_ABI}/libavutil.so)
    else()
        target_include_directories(LIBAV INTERFACE ${GRANITE_LIBAV_PATH}/include)
        # find_library is being annoying and doesn't want to cooperate for some stupid reason ...
        # pkgconfig is also very annoying with cross-compile setups, so just do it raw.
        if (EXISTS "${GRANITE_LIBAV_PATH}/bin/avcodec.lib")
            message("FFmpeg: Found .lib MSVC import libraries in bin folder.")
            target_link_libraries(LIBAV INTERFACE
                    "${GRANITE_LIBAV_PATH}/bin/avdevice.lib"
                    "${GRANITE_LIBAV_PATH}/bin/avformat.lib"
                    "${GRANITE_LIBAV_PATH}/bin/avcodec.lib"
                    "${GRANITE_LIBAV_PATH}/bin/swresample.lib"
                    "${GRANITE_LIBAV_PATH}/bin/avutil.lib")
        elseif (EXISTS "${GRANITE_LIBAV_PATH}/lib/avcodec.lib")
            message("FFmpeg: Found .lib MSVC import libraries in lib folder.")
            target_link_libraries(LIBAV INTERFACE
                    "${GRANITE_LIBAV_PATH}/lib/avdevice.lib"
                    "${GRANITE_LIBAV_PATH}/lib/avformat.lib"
                    "${GRANITE_LIBAV_PATH}/lib/avcodec.lib"
                    "${GRANITE_LIBAV_PATH}/lib/swresample.lib"
                    "${GRANITE_LIBAV_PATH}/lib/avutil.lib")
        elseif (EXISTS "${GRANITE_LIBAV_PATH}/lib/libavcodec.a")
            message("FFmpeg: Found static .a libraries.")
            target_link_libraries(LIBAV INTERFACE
                    "${GRANITE_LIBAV_PATH}/lib/libavdevice.a"
                    "${GRANITE_LIBAV_PATH}/lib/libavformat.a"
                    "${GRANITE_LIBAV_PATH}/lib/libavcodec.a"
                    "${GRANITE_LIBAV_PATH}/lib/libswresample.a"
                    "${GRANITE_LIBAV_PATH}/lib/libavutil.a")
        endif()
        if (WIN32)
            # avcodec and friends link against these. Needed especially when statically linking.
            target_link_libraries(LIBAV INTERFACE atomic mfuuid ole32 strmiids user32)
        endif()
    endif()
else()
    include(FindPkgConfig)
    pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET libavdevice libavformat libavcodec libavutil)
endif()

add_granite_internal_lib(granite-video
        ffmpeg_encode.cpp ffmpeg_encode.hpp
        scaler.cpp scaler.hpp
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
target_link_libraries(granite-video PRIVATE pyroenc pyrowave)
target_include_directories(granite-video PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(granite-video PUBLIC HAVE_GRANITE_FFMPEG)
