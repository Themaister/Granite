add_granite_library(filesystem-android android.cpp android.hpp)
target_include_directories(filesystem-android PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(filesystem-android util event android)
