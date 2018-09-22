add_granite_library(renderer
        render_queue.hpp render_queue.cpp
        mesh.hpp mesh.cpp
        scene.hpp scene.cpp
        shader_suite.hpp shader_suite.cpp
        render_context.hpp render_context.cpp
        camera.hpp camera.cpp
        material.hpp
        abstract_renderable.hpp
        render_components.hpp
        mesh_util.hpp mesh_util.cpp
        material_util.hpp material_util.cpp
        renderer.hpp renderer.cpp renderer_enums.hpp
        material_manager.hpp material_manager.cpp
        animation_system.hpp animation_system.cpp
        render_graph.cpp render_graph.hpp
        ground.hpp ground.cpp
        ocean.hpp ocean.cpp
        post/hdr.hpp post/hdr.cpp
        post/fxaa.hpp post/fxaa.cpp
        post/smaa.hpp post/smaa.cpp
        post/temporal.hpp post/temporal.cpp
        post/aa.hpp post/aa.cpp
        utils/image_utils.hpp utils/image_utils.cpp
        lights/lights.cpp lights/lights.hpp
        lights/clusterer.cpp lights/clusterer.hpp
        lights/volumetric_fog.cpp lights/volumetric_fog.hpp
        lights/light_info.hpp
        lights/deferred_lights.hpp lights/deferred_lights.cpp
        scene_loader.cpp scene_loader.hpp)

add_subdirectory(fft)
target_link_libraries(renderer
        math
        scene-formats
        vulkan-backend
        application-events
        application-input
        application-wsi
        threading
        rapidjson
        glfft)
target_include_directories(renderer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

