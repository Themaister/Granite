add_granite_library(util
        util.hpp util.cpp
        array_view.hpp
        variant.hpp
        enum_cast.hpp
        hash.hpp
        intrusive.hpp
        intrusive_list.hpp
        object_pool.hpp
        stack_allocator.hpp
        temporary_hashmap.hpp
        volatile_source.hpp
        read_write_lock.hpp
        thread_safe_cache.hpp
        async_object_sink.hpp
        unstable_remove_if.hpp
        intrusive_hash_map.hpp
        cli_parser.cpp cli_parser.hpp
        dynamic_library.cpp dynamic_library.hpp
        timer.hpp timer.cpp)

target_include_directories(util PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if (NOT WIN32)
    target_link_libraries(util dl)
endif()
