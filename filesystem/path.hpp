#pragma once
#include <string>
#include <utility>

namespace Granite
{
namespace Path
{
std::string join(const std::string &base, const std::string &path);
std::string basedir(const std::string &path);
std::string basename(const std::string &path);
std::pair<std::string, std::string> split(const std::string &path);
std::string relpath(const std::string &base, const std::string &path);
std::string ext(const std::string &path);
}
}