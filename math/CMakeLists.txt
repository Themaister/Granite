add_granite_internal_lib(granite-math
        math.hpp math.cpp
        frustum.hpp frustum.cpp
        aabb.cpp aabb.hpp
        render_parameters.hpp
        interpolation.cpp interpolation.hpp
        muglm/muglm.cpp muglm/muglm.hpp
        muglm/muglm_impl.hpp muglm/matrix_helper.hpp
        transforms.cpp transforms.hpp
        simd.hpp simd_headers.hpp)

target_include_directories(granite-math PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})