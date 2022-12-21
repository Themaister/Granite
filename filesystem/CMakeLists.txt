add_granite_internal_lib(granite-filesystem
        volatile_source.hpp
        filesystem.hpp filesystem.cpp
        asset_manager.cpp asset_manager.hpp)

if (WIN32)
    target_sources(granite-filesystem PRIVATE windows/os_filesystem.cpp windows/os_filesystem.hpp)
    target_include_directories(granite-filesystem PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/windows)
elseif (ANDROID)
    target_sources(granite-filesystem PRIVATE linux/os_filesystem.cpp linux/os_filesystem.hpp)
    target_sources(granite-filesystem PRIVATE android/android.cpp android/android.hpp)
    target_include_directories(granite-filesystem PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/linux)
    target_include_directories(granite-filesystem PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/android)
else()
    target_sources(granite-filesystem PRIVATE linux/os_filesystem.cpp linux/os_filesystem.hpp)
    target_include_directories(granite-filesystem PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/linux)
endif()

target_include_directories(granite-filesystem PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(granite-filesystem PUBLIC granite-util granite-path granite-application-global PRIVATE granite-threading)

if (GRANITE_SHIPPING)
    target_compile_definitions(granite-filesystem PRIVATE GRANITE_SHIPPING)
else()
    target_compile_definitions(granite-filesystem PRIVATE GRANITE_DEFAULT_BUILTIN_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/../assets\")
    target_compile_definitions(granite-filesystem PRIVATE GRANITE_DEFAULT_CACHE_DIRECTORY=\"${CMAKE_BINARY_DIR}/cache\")
endif()
