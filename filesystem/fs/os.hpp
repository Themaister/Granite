#pragma once
#include "filesystem.hpp"

namespace Granite
{
class OSFilesystem : public Filesystem
{
public:
	OSFilesystem(const std::string &base);
	~OSFilesystem();
	std::vector<Entry> list(const std::string &path) override;
	std::unique_ptr<File> open(const std::string &path) override;
	bool stat(const std::string &path, Stat &stat) override;
	NotifyHandle install_notification(const std::string &path, std::function<void (const Filesystem::NotifyInfo &)> func) override;
	void uninstall_notification(NotifyHandle handle) override;
private:
	std::string base;
};
}