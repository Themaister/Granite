add_library(math STATIC math.hpp math.cpp frustum.cpp)
target_include_directories(math PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/glm ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(math PRIVATE ${GRANITE_CXX_FLAGS})
