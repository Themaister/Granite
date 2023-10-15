add_granite_offline_tool(slangmosh slangmosh.cpp)
target_link_libraries(slangmosh PRIVATE granite-filesystem granite-compiler granite-rapidjson granite-vulkan)

