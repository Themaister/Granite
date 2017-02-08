#include "os.hpp"
#include "../path.hpp"
#include "util.hpp"
#include <stdexcept>

using namespace std;

namespace Granite
{

OSFilesystem::OSFilesystem(const std::string &)
{
}

OSFilesystem::~OSFilesystem()
{
}

unique_ptr<File> OSFilesystem::open(const std::string &, Mode)
{
    return {};
}

void OSFilesystem::poll_notifications()
{
}

void OSFilesystem::uninstall_notification(Filesystem::NotifyHandle)
{
}

Filesystem::NotifyHandle OSFilesystem::find_notification(const std::string &) const
{
    return -1;
}

Filesystem::NotifyHandle OSFilesystem::install_notification(const string &,
                                                            function<void (const Filesystem::NotifyInfo &)>)
{
    return -1;
}

vector<Filesystem::Entry> OSFilesystem::list(const string &)
{
    return {};
}

bool OSFilesystem::stat(const std::string &, Stat &)
{
    return false;
}

}
