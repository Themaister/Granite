#include "network.hpp"
#include "filesystem.hpp"
#include <unistd.h>

using namespace Granite;
using namespace std;

struct Writer : public LooperHandler
{
	Writer(unique_ptr<Socket> socket)
		: LooperHandler(move(socket))
	{
	}

	bool handle(EventFlags events) override
	{
		if (!(events & EVENT_OUT))
			return false;

		socket->write("hai", 3);
		return true;
	}
};

struct Reader : public LooperHandler
{
	Reader(unique_ptr<Socket> socket)
		: LooperHandler(move(socket))
	{
	}

	bool handle(EventFlags events) override
	{
		if (events & EVENT_IN)
		{
			char buffer[1024];
			auto ret = socket->read(buffer, sizeof(buffer));
			if (ret == 0)
				return true;
			else if (ret == Socket::ErrorWouldBlock)
				return false;
			else if (ret == Socket::ErrorIO)
				return true;
			else
			{
				::write(1, buffer, ret);
				return false;
			}
		}
		else
			return true;
	}
};

int main()
{
	TCPListener listener;
	if (!listener.init(10000))
		return 1;

	auto writer = Socket::connect("127.0.0.1", 10000);
	if (!writer)
		return 1;

	auto socket = listener.accept();
	if (!socket)
		return 1;

	Looper looper;

	auto handler = unique_ptr<Reader>(new Reader(move(socket)));
	looper.register_handler(EVENT_IN, move(handler));
	looper.register_handler(EVENT_OUT, unique_ptr<Writer>(new Writer(move(writer))));

	while (looper.wait(-1) >= 0);
}