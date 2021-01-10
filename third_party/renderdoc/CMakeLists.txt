add_library(granite-renderdoc-app INTERFACE)
target_include_directories(granite-renderdoc-app INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(granite-renderdoc-app INTERFACE GRANITE_RENDERDOC_CAPTURE)
