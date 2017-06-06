#include "ui_manager.hpp"

namespace Granite
{
namespace UI
{
UIManager &UIManager::get()
{
	static UIManager manager;
	return manager;
}

UIManager::UIManager()
{
}
}
}