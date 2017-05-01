#pragma once

#include <vector>
#include <arpa/inet.h>
#include <string.h>

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

class ReplyBuilder
{
public:
	void add_u32(uint32_t value)
	{
		buffer.resize(buffer.size() + 4);
		value = htonl(value);
		memcpy(buffer.data() + buffer.size() - 4, &value, 4);
	}

	void add_u64(uint64_t value)
	{
		buffer.resize(buffer.size() + 8);

		uint32_t value0 = uint32_t(value >> 32);
		uint32_t value1 = uint32_t(value >> 0);
		value0 = htonl(value0);
		value1 = htonl(value1);
		memcpy(buffer.data() + buffer.size() - 8, &value0, 4);
		memcpy(buffer.data() + buffer.size() - 4, &value1, 4);
	}

	void add_string(const std::string &str)
	{
		add_u64(str.size());
		buffer.insert(std::end(buffer), reinterpret_cast<const uint8_t *>(str.data()),
		              reinterpret_cast<const uint8_t *>(str.data()) + str.size());
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
	uint32_t offset = 0;
};
}