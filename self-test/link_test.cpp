#include "global_managers_init.hpp"

int main()
{
	Granite::Global::init();
	LOGI("Hello there! :)\n");
	Granite::Global::deinit();
	return 0;
}