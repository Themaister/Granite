add_granite_internal_lib(granite-ui
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
target_include_directories(granite-ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(granite-ui PUBLIC granite-util granite-renderer PRIVATE granite-stb)