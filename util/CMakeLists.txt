add_granite_internal_lib(granite-util
        logging.hpp logging.cpp
        aligned_alloc.cpp aligned_alloc.hpp
        bitops.hpp
        array_view.hpp
        variant.hpp
        enum_cast.hpp
        hash.hpp
        intrusive.hpp
        intrusive_list.hpp
        object_pool.hpp
        stack_allocator.hpp
        temporary_hashmap.hpp
        read_write_lock.hpp
        async_object_sink.hpp
        unstable_remove_if.hpp
        intrusive_hash_map.hpp
        timer.hpp timer.cpp
        small_vector.hpp
        thread_id.hpp thread_id.cpp
        string_helpers.hpp string_helpers.cpp
        timeline_trace_file.hpp timeline_trace_file.cpp
        thread_name.hpp thread_name.cpp
        cli_parser.cpp cli_parser.hpp
        dynamic_library.cpp dynamic_library.hpp
        generational_handle.hpp
        lru_cache.hpp
        message_queue.hpp message_queue.cpp
        no_init_pod.hpp)
target_include_directories(granite-util PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(granite-util PUBLIC granite-application-global-interface)

if (NOT WIN32)
    target_link_libraries(granite-util PUBLIC dl)
endif()