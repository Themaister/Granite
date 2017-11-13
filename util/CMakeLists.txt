add_granite_library(util
        util.hpp util.cpp
        array_view.hpp
        variant.hpp
        enum_cast.hpp
        hashmap.hpp
        intrusive.hpp
        intrusive_list.hpp
        object_pool.hpp
        stack_allocator.hpp
        temporary_hashmap.hpp
        volatile_source.hpp
        unstable_remove_if.hpp
        cli_parser.cpp cli_parser.hpp
        timer.hpp timer.cpp)

target_include_directories(util PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
