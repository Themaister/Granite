add_granite_library(application application.cpp application.hpp)
target_include_directories(application PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(application vulkan vulkan-wsi renderer)

if (ANDROID)
    target_sources(application PRIVATE application_android.cpp)
else()
    target_sources(application PRIVATE application_default.cpp)
endif()