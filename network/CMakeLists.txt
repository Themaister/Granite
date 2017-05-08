add_granite_library(networking network.hpp tcp_listener.cpp socket.cpp looper.cpp)
target_include_directories(networking PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_granite_executable(netfs-server netfs_server.cpp)
target_link_libraries(netfs-server networking filesystem)
target_include_directories(netfs-server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_granite_executable(netfs-client netfs_client.cpp)
target_link_libraries(netfs-client networking filesystem)
target_include_directories(netfs-client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
