add_granite_library(renderer
        mainloop.cpp mainloop.hpp
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
        renderer.hpp renderer.cpp
        material_manager.hpp material_manager.cpp
        animation_system.hpp animation_system.cpp
        scene_loader.cpp scene_loader.hpp)
target_link_libraries(renderer vulkan-backend math importers)
target_include_directories(renderer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(renderer PRIVATE RAPIDJSON_HAS_STDSTRING=1)
target_include_directories(renderer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/rapidjson/include)
