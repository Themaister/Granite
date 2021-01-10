add_granite_internal_lib(granite-application-events
        application_wsi.hpp
        application_wsi.cpp application_wsi_events.hpp
        application_events.hpp)
target_include_directories(granite-application-events PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(granite-application-events
        PRIVATE granite-vulkan granite-event granite-input granite-application-global)
