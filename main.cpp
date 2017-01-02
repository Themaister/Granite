#include "util.hpp"
#include "compiler.hpp"
#include "filesystem.hpp"

using namespace Granite;
using namespace std;

int main()
{
	GLSLCompiler compiler;
	vector<uint32_t> spirv;
	compiler.set_source_from_file("/tmp/test.frag");
	compiler.set_stage(Stage::Fragment);
	compiler.compile(spirv);

	auto &fs = Filesystem::get();
	auto entries = fs.walk("/tmp/");
	for (auto &e : entries)
	{
		Filesystem::Stat s;
		uint64_t size = 0;
		if (fs.stat(e.path, s))
			size = s.size;
		LOG("File: %s (type: %d) (size: %zu)\n", e.path.c_str(), static_cast<int>(e.type), size_t(size));
	}
}
