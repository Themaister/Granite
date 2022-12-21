/* Copyright (c) 2017-2022 Hans-Kristian Arntzen
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

#include "asset_manager.hpp"
#include "thread_group.hpp"
#include <utility>

namespace Granite
{
AssetManager::AssetManager()
{
	signal = std::make_unique<TaskSignal>();
}

AssetManager::~AssetManager()
{
	signal->wait_until_at_least(timestamp);
	for (auto *a : asset_bank)
		pool.free(a);
}

ImageAssetID AssetManager::register_image_resource(FileHandle file)
{
	auto *info = pool.allocate();
	info->handle = std::move(file);
	info->id.id = id_count++;
	ImageAssetID ret = info->id;
	asset_bank.push_back(info);
	sorted_assets.reserve(asset_bank.size());
	if (iface)
		iface->set_id_bounds(id_count);
	return ret;
}

void AssetManager::update_cost(ImageAssetID id, uint64_t cost)
{
	std::lock_guard<std::mutex> holder{cost_update_lock};
	thread_cost_updates.push_back({ id, cost });
}

void AssetManager::set_asset_instantiator_interface(AssetInstantiatorInterface *iface_)
{
	if (iface)
	{
		signal->wait_until_at_least(timestamp);
		for (uint32_t id = 0; id < id_count; id++)
			iface->release_image_resource({ id });
	}

	for (auto *a : asset_bank)
	{
		a->consumed = 0;
		a->pending_consumed = 0;
		a->last_used = 0;
		total_consumed = 0;
	}

	iface = iface_;
	if (iface)
		iface->set_id_bounds(id_count);
}

void AssetManager::mark_used_resource(ImageAssetID id)
{
	lru_append.push(id);
}

void AssetManager::set_image_budget(uint64_t cost)
{
	image_budget = cost;
}

void AssetManager::set_image_budget_per_iteration(uint64_t cost)
{
	image_budget_per_iteration = cost;
}

bool AssetManager::set_image_residency_priority(ImageAssetID id, int prio)
{
	if (id.id >= asset_bank.size())
		return false;
	asset_bank[id.id]->prio = prio;
	return true;
}

void AssetManager::adjust_update(const CostUpdate &update)
{
	if (update.id.id < asset_bank.size())
	{
		auto *a = asset_bank[update.id.id];
		total_consumed += update.cost - (a->consumed + a->pending_consumed);
		a->consumed = update.cost;
		a->pending_consumed = 0;
	}
}

uint64_t AssetManager::get_current_total_consumed() const
{
	return total_consumed;
}

void AssetManager::iterate(ThreadGroup *group)
{
	if (!iface)
		return;

	// If there is too much pending work going on, skip.
	uint64_t current_count = signal->get_count();
	if (current_count + 3 < timestamp)
	{
		iface->latch_handles();
		return;
	}

	TaskGroupHandle task;
	if (group)
	{
		task = group->create_task();
		task->set_desc("asset-manager-instantiate");
		task->set_fence_counter_signal(signal.get());
		task->set_task_class(TaskClass::Background);
	}
	else
		signal->signal_increment();

	{
		std::lock_guard<std::mutex> holder{cost_update_lock};
		std::swap(cost_updates, thread_cost_updates);
	}

	for (auto &update : cost_updates)
		adjust_update(update);
	cost_updates.clear();

	lru_append.for_each_ranged([this](const ImageAssetID *id, size_t count) {
		for (size_t i = 0; i < count; i++)
			if (id[i].id < asset_bank.size())
				asset_bank[id[i].id]->last_used = timestamp;
	});
	lru_append.clear();

	sorted_assets = asset_bank;
	std::sort(sorted_assets.begin(), sorted_assets.end(), [](const AssetInfo *a, const AssetInfo *b) -> bool {
		// High prios come first since they will be activated.
		// Then we sort by LRU.
		// High consumption should be moved last, so they are candidates to be paged out if we're over budget.
		// High pending consumption should be moved early since we don't want to page out resources that
		// are in the middle of being loaded anyway.
		// Finally, the ID is used as a tie breaker.

		if (a->prio != b->prio)
			return a->prio > b->prio;
		else if (a->last_used != b->last_used)
			return a->last_used > b->last_used;
		else if (a->consumed != b->consumed)
			return a->consumed < b->consumed;
		else if (a->pending_consumed != b->pending_consumed)
			return a->pending_consumed > b->pending_consumed;
		else
			return a->id.id < b->id.id;
	});

	size_t release_index = sorted_assets.size();
	uint64_t activated_cost_this_iteration = 0;
	size_t activate_index = 0;

	// Aim to activate resources as long as we're in budget.
	// Activate in order from highest priority to lowest.
	bool can_activate = true;
	while (can_activate &&
	       total_consumed < image_budget &&
	       activated_cost_this_iteration < image_budget_per_iteration &&
	       activate_index != release_index)
	{
		auto *candidate = sorted_assets[activate_index];
		if (candidate->prio <= 0)
			break;

		// This resource is already active.
		if (candidate->consumed != 0 || candidate->pending_consumed != 0)
		{
			activate_index++;
			continue;
		}

		uint64_t estimate = iface->estimate_cost_image_resource(candidate->id, candidate->handle);

		can_activate = total_consumed + estimate <= image_budget;
		while (!can_activate && activate_index + 1 != release_index)
		{
			auto *release_candidate = sorted_assets[--release_index];
			if (release_candidate->consumed)
			{
				if (group)
					task->enqueue_task([this, release_candidate]() { iface->release_image_resource(release_candidate->id); });
				else
					iface->release_image_resource(release_candidate->id);

				total_consumed -= release_candidate->consumed;
				release_candidate->consumed = 0;
			}
			can_activate = total_consumed + estimate <= image_budget;
		}

		if (can_activate)
		{
			// We're trivially in budget.
			if (group)
				task->enqueue_task([this, candidate]() { iface->instantiate_image_resource(*this, candidate->id, candidate->handle); });
			else
				iface->instantiate_image_resource(*this, candidate->id, candidate->handle);

			candidate->pending_consumed = estimate;
			total_consumed += estimate;
			// Let this run over budget once.
			// Ensures we can make forward progress no matter what the limit is.
			activated_cost_this_iteration += estimate;
			activate_index++;
		}
	}

	// If we're over budget, deactivate resources.
	while (total_consumed > image_budget && release_index != activate_index)
	{
		auto *candidate = sorted_assets[--release_index];
		if (candidate->consumed)
		{
			if (group)
				task->enqueue_task([this, candidate]() { iface->release_image_resource(candidate->id); });
			else
				iface->release_image_resource(candidate->id);

			total_consumed -= candidate->consumed;
			candidate->consumed = 0;
		}
	}

	iface->latch_handles();
	timestamp++;
}
}
