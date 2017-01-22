#include "util.hpp"
#include "compiler.hpp"
#include "filesystem.hpp"
#include "event.hpp"
#include "path.hpp"
#include <unistd.h>
#include <string.h>

using namespace Granite;
using namespace std;

int main()
{
	EventManager manager;

	manager.enqueue<AEvent>(10);

	class Handler : public EventHandler
	{
	public:
		bool handle_a(const Event &e)
		{
			auto &event = e.as<AEvent>();
			fprintf(stderr, "%d\n", event.a);
			return false;
		}

		bool handle_a2(const Event &e)
		{
			auto &event = e.as<AEvent>();
			fprintf(stderr, "%d\n", event.a + 1);
			return true;
		}

		void up(const Event &e)
		{
			fprintf(stderr, "UP %d\n", e.as<BEvent>().b);
		}

		void down(const Event &e)
		{
			fprintf(stderr, "DOWN %d\n", e.as<BEvent>().b);
		}

		void up2(const Event &e)
		{
			fprintf(stderr, "UP2 %d\n", e.as<BEvent>().b);
		}

		void down2(const Event &e)
		{
			fprintf(stderr, "DOWN2 %d\n", e.as<BEvent>().b);
		}
	} handler;

	//manager.register_handler(AEvent::type_id, static_cast<bool (EventHandler::*)(const Event &event)>(&Handler::handle_a), &handler);
	manager.register_handler(AEvent::type_id, &Handler::handle_a, &handler);
	manager.register_handler(AEvent::type_id, &Handler::handle_a2, &handler);
	manager.dispatch();

	manager.enqueue<AEvent>(20);
	manager.unregister_handler(&handler);
	manager.dispatch();

	manager.register_latch_handler(BEvent::type_id, &Handler::up, &Handler::down, &handler);
	auto cookie = manager.enqueue_latched<BEvent>(10, 20);
	manager.dequeue_latched(cookie);
	manager.register_latch_handler(BEvent::type_id, &Handler::up2, &Handler::down2, &handler);

	cookie = manager.enqueue_latched<BEvent>(10, 40);

#if 0
	GLSLCompiler compiler;
	compiler.set_source_from_file(Filesystem::get(), "/tmp/test.frag");
	compiler.preprocess();
	auto spirv = compiler.compile();

	if (spirv.empty())
		LOGE("GLSL: %s\n", compiler.get_error_message().c_str());

	for (auto &dep : compiler.get_dependencies())
		LOGI("Dependency: %s\n", dep.c_str());
	for (auto &dep : compiler.get_variants())
		LOGI("Variant: %s\n", dep.first.c_str());

	auto &fs = Filesystem::get();
	auto file = fs.open("/tmp/foobar", Filesystem::Mode::WriteOnly);
	const string foo = ":D";
	memcpy(file->map_write(foo.size()), foo.data(), foo.size());
#endif
}
