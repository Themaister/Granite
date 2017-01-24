add_library(renderer STATIC mainloop.cpp mainloop.hpp event.cpp event.hpp)
target_link_libraries(renderer vulkan math)
target_include_directories(renderer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(renderer PRIVATE ${GRANITE_CXX_FLAGS})