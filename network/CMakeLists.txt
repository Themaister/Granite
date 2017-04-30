add_library(networking STATIC netfs_server.hpp tcp_listener.cpp socket.cpp looper.cpp)
target_compile_options(networking PRIVATE ${GRANITE_CXX_FLAGS})
target_compile_options(networking PRIVATE ${GRANITE_CXX_FLAGS})
target_include_directories(networking PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(netfs-server netfs_server.cpp)
target_link_libraries(netfs-server networking filesystem)
target_compile_options(netfs-server PRIVATE ${GRANITE_CXX_FLAGS})
target_include_directories(netfs-server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})