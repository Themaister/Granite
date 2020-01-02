/* Copyright (c) 2017-2020 Hans-Kristian Arntzen
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

#pragma once

#include <vector>
#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif
#include <string.h>
#include <string>

namespace Granite
{
enum NetFSCommand
{
	NETFS_READ_FILE = 1,
	NETFS_LIST = 2,
	NETFS_WALK = 3,
	NETFS_WRITE_FILE = 4,
	NETFS_STAT = 5,
	NETFS_NOTIFICATION = 6,
	NETFS_REGISTER_NOTIFICATION = 7,
	NETFS_UNREGISTER_NOTIFICATION = 8,
	NETFS_BEGIN_CHUNK_REQUEST = 9,
	NETFS_BEGIN_CHUNK_REPLY = 10,
	NETFS_BEGIN_CHUNK_NOTIFICATION = 11
};

enum NetFSError
{
	NETFS_ERROR_OK = 0,
	NETFS_ERROR_IO = 1
};

enum NetFSNotification
{
	NETFS_FILE_DELETED = 1,
	NETFS_FILE_CHANGED = 2,
	NETFS_FILE_CREATED = 3
};

enum NetFSFileType
{
	NETFS_FILE_TYPE_PLAIN = 1,
	NETFS_FILE_TYPE_DIRECTORY = 2,
	NETFS_FILE_TYPE_SPECIAL = 3
};

class ReplyBuilder
{
public:
	size_t add_u32(uint32_t value)
	{
		auto ret = buffer.size();
		buffer.resize(buffer.size() + 4);
		poke_u32(buffer.size() - 4, value);
		return ret;
	}

	size_t add_u64(uint64_t value)
	{
		auto ret = buffer.size();
		buffer.resize(buffer.size() + 8);
		poke_u64(buffer.size() - 8, value);
		return ret;
	}

	void poke_u32(size_t offset_, uint32_t value)
	{
		value = htonl(value);
		memcpy(buffer.data() + offset_, &value, 4);
	}

	void poke_u64(size_t offset_, uint64_t value)
	{
		uint32_t value0 = uint32_t(value >> 32);
		uint32_t value1 = uint32_t(value >> 0);
		value0 = htonl(value0);
		value1 = htonl(value1);
		memcpy(buffer.data() + offset_, &value0, 4);
		memcpy(buffer.data() + offset_ + 4, &value1, 4);
	}

	size_t add_string(const std::string &str)
	{
		auto ret = buffer.size();
		add_u64(str.size());
		buffer.insert(std::end(buffer), reinterpret_cast<const uint8_t *>(str.data()),
		              reinterpret_cast<const uint8_t *>(str.data()) + str.size());
		return ret;
	}

	uint32_t read_u32()
	{
		if (offset + 4 > buffer.size())
			return 0;

		uint32_t v;
		memcpy(&v, buffer.data() + offset, 4);
		offset += 4;
		return ntohl(v);
	}

	uint64_t read_u64()
	{
		if (offset + 8 > buffer.size())
			return 0;

		uint32_t v0;
		uint32_t v1;
		memcpy(&v0, buffer.data() + offset, 4);
		memcpy(&v1, buffer.data() + offset + 4, 4);
		offset += 8;
		return (uint64_t(ntohl(v0)) << 32) | ntohl(v1);
	}

	std::string read_string()
	{
		uint64_t len = read_u64();
		if (offset + len > buffer.size())
			return {};

		auto ret = std::string(reinterpret_cast<const char *>(buffer.data() + offset),
		                       reinterpret_cast<const char *>(buffer.data() + offset + len));
		offset += len;
		return ret;
	}

	std::string read_string_implicit_count()
	{
		auto ret = std::string(reinterpret_cast<const char *>(buffer.data() + offset),
		                       reinterpret_cast<const char *>(buffer.data() + buffer.size()));

		offset = buffer.size();
		return ret;
	}

	void add_buffer(const std::vector<uint8_t> &other)
	{
		buffer.insert(std::end(buffer), std::begin(other), std::end(other));
	}

	std::vector<uint8_t> &get_buffer()
	{
		return buffer;
	}

	std::vector<uint8_t> &&consume_buffer()
	{
		return move(buffer);
	}

	void begin(size_t size = 0)
	{
		if (size == 0)
			buffer.clear();
		else
			buffer.resize(size);

		offset = 0;
	}

private:
	std::vector<uint8_t> buffer;
	size_t offset = 0;
};
}