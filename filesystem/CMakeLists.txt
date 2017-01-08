add_library(filesystem STATIC filesystem.cpp filesystem.hpp path.hpp path.cpp fs/os.hpp fs/os.cpp)
target_include_directories(filesystem PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(filesystem PRIVATE ${GRANITE_CXX_FLAGS})
target_link_libraries(filesystem util)