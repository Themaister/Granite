include(FindPkgConfig)

add_granite_library(audio-backend
        audio_interface.cpp audio_interface.hpp
        vorbis_stream.hpp vorbis_stream.cpp)
target_include_directories(audio-backend PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(audio-backend util stb-vorbis filesystem)

if (NOT WIN32)
    pkg_check_modules(PULSEAUDIO libpulse)
    if (PULSEAUDIO_FOUND)
        target_sources(audio-backend PRIVATE audio_pulse.cpp audio_pulse.hpp)
        target_link_libraries(audio-backend ${PULSEAUDIO_LIBRARIES})
        target_include_directories(audio-backend PRIVATE ${PULSEAUDIO_INCLUDEDIR})
        target_compile_definitions(audio-backend PRIVATE AUDIO_HAVE_PULSE=1)
    endif()
endif()