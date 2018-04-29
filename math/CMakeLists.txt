add_granite_library(math
        math.hpp math.cpp
        frustum.hpp frustum.cpp
        aabb.cpp aabb.hpp
        render_parameters.hpp
        interpolation.cpp interpolation.hpp
        muglm/muglm.cpp muglm/muglm.hpp muglm/func.hpp muglm/matrix_helper.hpp
        transforms.cpp transforms.hpp)
target_include_directories(math PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(math PUBLIC NOMINMAX)

add_granite_offline_tool(muglm-test muglm/muglm_test.cpp)
target_link_libraries(muglm-test math)
