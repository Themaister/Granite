add_granite_internal_lib(granite-application-global
        global_managers.hpp global_managers.cpp)
target_include_directories(granite-application-global PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(granite-application-global PUBLIC granite-util)

add_granite_internal_lib(granite-application-global-init
        global_managers_init.hpp global_managers_init.cpp)
target_include_directories(granite-application-global-init PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(granite-application-global-init
        PUBLIC granite-application-global
        PRIVATE granite-threading granite-event granite-filesystem)

if (TARGET granite-renderer)
    target_link_libraries(granite-application-global-init PRIVATE granite-renderer granite-ui)
    target_compile_definitions(granite-application-global-init PRIVATE HAVE_GRANITE_RENDERER)
endif()

if (GRANITE_AUDIO)
    target_link_libraries(granite-application-global-init PRIVATE granite-audio)
endif()

if (GRANITE_BULLET)
    target_link_libraries(granite-application-global-init PRIVATE granite-physics)
endif()

add_library(granite-application-global-interface INTERFACE)
target_include_directories(granite-application-global-interface INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})