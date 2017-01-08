#include "util.hpp"
#include "compiler.hpp"
#include "filesystem.hpp"
#include "path.hpp"
#include <unistd.h>

using namespace Granite;
using namespace std;

int main()
{
	GLSLCompiler compiler;
	compiler.set_source_from_file(Filesystem::get(), "/tmp/test.frag");
	compiler.preprocess();
	auto spirv = compiler.compile();

	for (auto &dep : compiler.get_dependencies())
		LOG("Dependency: %s\n", dep.c_str());
	for (auto &dep : compiler.get_variants())
		LOG("Variant: %s\n", dep.first.c_str());
}
