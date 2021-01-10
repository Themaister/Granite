add_granite_internal_lib(granite-renderer
        render_queue.hpp render_queue.cpp
        simple_renderer.hpp simple_renderer.cpp
        mesh.hpp mesh.cpp
        scene.hpp scene.cpp
        scene_renderer.hpp scene_renderer.cpp
        shader_suite.hpp shader_suite.cpp
        render_context.hpp render_context.cpp
        camera.hpp camera.cpp
        material.hpp
        abstract_renderable.hpp
        render_components.hpp
        mesh_util.hpp mesh_util.cpp
        material_util.hpp material_util.cpp
        renderer.hpp renderer.cpp
        flat_renderer.hpp flat_renderer.cpp
        renderer_enums.hpp
        material_manager.hpp material_manager.cpp
        animation_system.hpp animation_system.cpp
        render_graph.cpp render_graph.hpp
        ground.hpp ground.cpp
        post/hdr.hpp post/hdr.cpp
        post/fxaa.hpp post/fxaa.cpp
        post/smaa.hpp post/smaa.cpp
        post/temporal.hpp post/temporal.cpp
        post/aa.hpp post/aa.cpp
        post/ssao.hpp post/ssao.cpp
        utils/image_utils.hpp utils/image_utils.cpp
        lights/lights.cpp lights/lights.hpp
        lights/clusterer.cpp lights/clusterer.hpp
        lights/volumetric_fog.cpp lights/volumetric_fog.hpp
        lights/light_info.hpp
        lights/deferred_lights.hpp lights/deferred_lights.cpp
        formats/scene_formats.hpp formats/scene_formats.cpp
        formats/gltf.hpp formats/gltf.cpp
        scene_loader.cpp scene_loader.hpp
        mesh_manager.cpp mesh_manager.hpp
        sprite.cpp sprite.hpp
        common_renderer_data.cpp common_renderer_data.hpp
        cpu_rasterizer.cpp cpu_rasterizer.hpp
        font.cpp font.hpp
        threaded_scene.cpp threaded_scene.hpp)
target_include_directories(granite-renderer
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/fft ${CMAKE_CURRENT_SOURCE_DIR}/formats)
target_link_libraries(granite-renderer PUBLIC
        granite-ecs
        granite-vulkan
        granite-math
        granite-event
        granite-input
        granite-application-events
        granite-application-global
        granite-threading
        PRIVATE granite-mikktspace meshoptimizer granite-rapidjson granite-stb)

if (GRANITE_VULKAN_SHADER_MANAGER_RUNTIME_COMPILER)
    target_sources(granite-renderer PRIVATE
            ocean.hpp ocean.cpp
            fft/glfft.cpp
            fft/glfft.hpp
            fft/glfft_common.hpp
            fft/glfft_interface.hpp
            fft/glfft_granite_interface.hpp
            fft/glfft_granite_interface.cpp
            fft/glfft_wisdom.cpp
            fft/glfft_wisdom.hpp)
    target_link_libraries(granite-renderer PRIVATE granite-compiler)
endif()
