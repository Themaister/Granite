/* Copyright (c) 2017 Hans-Kristian Arntzen
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "os.hpp"
#include "../path.hpp"
#include "util.hpp"
#include <stdexcept>

using namespace std;

namespace Granite
{
OSFilesystem::OSFilesystem(const std::string &base)
	: base(base)
{
}

OSFilesystem::~OSFilesystem()
{
}

unique_ptr<File> OSFilesystem::open(const std::string &path, FileMode mode)
{
	try
	{
		unique_ptr<File> file(new StdioFile(Path::join(base, path), mode));
		return file;
	}
	catch (const std::exception &e)
	{
		LOGE("OSFilesystem::open(): %s\n", e.what());
		return {};
	}
}

void OSFilesystem::poll_notifications()
{
}

void OSFilesystem::uninstall_notification(FileNotifyHandle)
{
}

FileNotifyHandle OSFilesystem::install_notification(const string &,
                                                    function<void (const FileNotifyInfo &)>)
{
    return -1;
}

vector<ListEntry> OSFilesystem::list(const string &)
{
    return {};
}

bool OSFilesystem::stat(const std::string &, FileStat &)
{
    return false;
}

int OSFilesystem::get_notification_fd() const
{
    return -1;
}

}
