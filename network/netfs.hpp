#pragma once

namespace Granite
{
enum NetFSCommand
{
	NETFS_READ_FILE = 1,
	NETFS_WRITE_FILE = 2,
	NETFS_BEGIN_CHUNK = 3
};

enum NetFSError
{
	NETFS_ERROR_OK = 0,
	NETFS_ERROR_IO = 1
};
}