add_granite_library(ui-renderer
        font.hpp font.cpp
        flat_renderer.hpp flat_renderer.cpp
        sprite.cpp sprite.hpp
        widget.hpp widget.cpp
        window.hpp window.cpp
        vertical_packing.cpp vertical_packing.hpp
        horizontal_packing.cpp horizontal_packing.hpp
        image_widget.cpp image_widget.hpp
        label.cpp label.hpp
        slider.cpp slider.hpp
        click_button.cpp click_button.hpp
        toggle_button.cpp toggle_button.hpp
        ui_manager.hpp ui_manager.cpp)
target_link_libraries(ui-renderer renderer stb filesystem)
target_include_directories(ui-renderer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
