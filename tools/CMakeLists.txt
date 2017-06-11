add_granite_executable(png2chunky png2chunky.cpp)
target_include_directories(png2chunky PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/gli)
target_link_libraries(png2chunky math stb util)

if (CMAKE_COMPILER_IS_GNUCXX)
    set_source_files_properties(png2chunky.cpp PROPERTIES COMPILE_FLAGS "-Wno-type-limits -Wno-pedantic -Wno-ignored-qualifiers -Wno-unused-parameter")
endif()
