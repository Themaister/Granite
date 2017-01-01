#include "util.hpp"
#include <sstream>
#include <fstream>
#include <stdexcept>
using namespace std;

namespace Util
{
string read_file_to_string(const string &path)
{
	ifstream file(path);
	if (!file.good())
	{
		LOG("Failed to open file: %s\n", path.c_str());
		throw runtime_error("file error");
	}
	stringstream stream;
	stream << file.rdbuf();
	return stream.str();
}
}