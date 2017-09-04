add_granite_library(math
        math.hpp math.cpp
        frustum.hpp frustum.cpp
        aabb.cpp aabb.hpp
        render_parameters.hpp
        interpolation.cpp interpolation.hpp
        transforms.cpp transforms.hpp)
target_include_directories(math PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/glm ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(math PUBLIC GLM_FORCE_DEPTH_ZERO_TO_ONE GLM_FORCE_SWIZZLE GLM_ENABLE_EXPERIMENTAL NOMINMAX)
