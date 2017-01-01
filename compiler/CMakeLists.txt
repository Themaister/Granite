add_library(compiler compiler.cpp compiler.hpp)
target_link_libraries(compiler shaderc)
target_compile_options(compiler PRIVATE ${GRANITE_CXX_FLAGS})
