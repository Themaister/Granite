#include "ecs.hpp"

namespace Granite
{
uint32_t ComponentIDMapping::ids;
uint32_t ComponentIDMapping::group_ids;

void EntityDeleter::operator()(Entity *entity)
{
	entity->get_pool()->delete_entity(entity);
}

void EntityPool::reset_groups()
{
	component_to_groups.clear();
	groups.clear();
}
}