#include "slang_compiler.hpp"
#include "global_managers_init.hpp"
#include "filesystem.hpp"

int main(int argc, char **argv)
{
	Granite::Global::init(Granite::Global::MANAGER_FEATURE_FILESYSTEM_BIT);
	Granite::SlangCompiler comp(*GRANITE_FILESYSTEM());

	comp.set_source(argv[1]);

	std::string err;
	auto code = comp.compile(err);

	if (code.empty())
	{
		LOGE("Err: %s\n", err.c_str());
		return EXIT_FAILURE;
	}

	if (!GRANITE_FILESYSTEM()->write_buffer_to_file(argv[2], code.data(), code.size() * sizeof(uint32_t)))
	{
		LOGE("Failed to write file to %s\n", argv[2]);
		return EXIT_FAILURE;
	}
}
