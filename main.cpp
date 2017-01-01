#include "compiler.hpp"

using namespace Granite;
using namespace std;

int main()
{
	GLSLCompiler compiler;
	vector<uint32_t> spirv;
	compiler.set_source_from_file("/tmp/test.frag");
	compiler.set_stage(Stage::Fragment);
	compiler.compile(spirv);
}
