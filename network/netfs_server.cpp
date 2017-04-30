#include "netfs_server.hpp"
#include "filesystem.hpp"
#include <unistd.h>

using namespace Granite;
using namespace std;

struct Reader : public LooperHandler
{
	Reader(unique_ptr<Socket> socket)
	{
		this->socket = move(socket);
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

	auto socket = listener.accept();
	Looper looper;

	auto handler = unique_ptr<Reader>(new Reader(move(socket)));
	auto &sock = *handler->get_socket();
	looper.register_handler(sock, EVENT_IN, move(handler));

	while (looper.wait(-1) >= 0);
}