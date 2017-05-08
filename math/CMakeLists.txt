add_granite_library(math
        math.hpp math.cpp
        frustum.hpp frustum.cpp
        aabb.cpp aabb.hpp
        render_parameters.hpp
        transforms.cpp transforms.hpp)
target_include_directories(math PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/glm ${CMAKE_CURRENT_SOURCE_DIR})
