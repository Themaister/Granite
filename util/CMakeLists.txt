add_library(util STATIC
        util.hpp
        util.cpp
        enum_cast.hpp
        hashmap.hpp
        intrusive.hpp
        intrusive_list.hpp
        object_pool.hpp
        stack_allocator.hpp
        temporary_hashmap.hpp
        volatile_source.hpp)

target_compile_options(util PRIVATE ${GRANITE_CXX_FLAGS})
target_include_directories(util PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})