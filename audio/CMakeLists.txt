include(FindPkgConfig)

add_granite_internal_lib(granite-audio
        audio_interface.cpp audio_interface.hpp
        audio_mixer.cpp audio_mixer.hpp
        audio_resampler.cpp audio_resampler.hpp
        dsp/sinc_resampler.cpp dsp/sinc_resampler.hpp
        dsp/dsp.hpp dsp/dsp.cpp
        dsp/tone_filter.hpp dsp/tone_filter.cpp
        dsp/tone_filter_stream.hpp dsp/tone_filter_stream.cpp
        audio_events.hpp
        dsp/audio_fft_eq.cpp dsp/audio_fft_eq.hpp
        dsp/pole_zero_filter_design.cpp dsp/pole_zero_filter_design.hpp
        vorbis_stream.hpp vorbis_stream.cpp)

target_include_directories(granite-audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(granite-audio PRIVATE granite-stb-vorbis muFFT)
target_compile_definitions(granite-audio PUBLIC HAVE_GRANITE_AUDIO=1)

if (ANDROID)
    target_sources(granite-audio PRIVATE audio_oboe.cpp audio_oboe.hpp)
    target_compile_definitions(granite-audio PUBLIC AUDIO_HAVE_OBOE=1)
    target_link_libraries(granite-audio PRIVATE oboe)
elseif (NOT WIN32)
    pkg_check_modules(PULSEAUDIO libpulse)
    if (PULSEAUDIO_FOUND)
        target_sources(granite-audio PRIVATE audio_pulse.cpp audio_pulse.hpp)
        target_link_libraries(granite-audio PRIVATE ${PULSEAUDIO_LIBRARIES})
        target_include_directories(granite-audio PRIVATE ${PULSEAUDIO_INCLUDEDIR})
        target_compile_definitions(granite-audio PRIVATE AUDIO_HAVE_PULSE=1)
    endif()
elseif (WIN32)
    target_sources(granite-audio PRIVATE audio_wasapi.cpp audio_wasapi.hpp)
    target_compile_definitions(granite-audio PRIVATE AUDIO_HAVE_WASAPI=1)
    target_link_libraries(granite-audio PRIVATE avrt)
endif()

target_link_libraries(granite-audio PUBLIC granite-filesystem granite-math granite-event)
