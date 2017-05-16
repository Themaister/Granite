add_granite_library(application application.cpp application.hpp)
target_include_directories(application PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(application vulkan vulkan-wsi renderer)

if (ANDROID)
    target_sources(application PRIVATE application_android.cpp)

    add_granite_library(android-glue
            android.cpp
            ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c
            ${ANDROID_NDK}/sources/android/cpufeatures/cpu-features.c)

    target_include_directories(android-glue
            PUBLIC
            ${ANDROID_NDK}/sources/android/native_app_glue)

    target_link_libraries(android-glue util)
    target_link_libraries(application android-glue)
else()
    target_sources(application PRIVATE application_default.cpp)
endif()