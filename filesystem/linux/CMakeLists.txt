add_library(filesystem-os STATIC os.cpp os.hpp)
target_include_directories(filesystem-os PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(filesystem-os PRIVATE ${GRANITE_CXX_FLAGS})
target_link_libraries(filesystem-os util)
