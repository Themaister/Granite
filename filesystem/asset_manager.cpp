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
#include <algorithm>

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

ImageAssetID AssetManager::register_image_resource_nolock(FileHandle file, ImageClass image_class)
{
	auto *info = pool.allocate();
	info->handle = std::move(file);
	info->id.id = id_count++;
	ImageAssetID ret = info->id;
	asset_bank.push_back(info);
	sorted_assets.reserve(asset_bank.size());
	if (iface)
	{
		iface->set_id_bounds(id_count);
		iface->set_image_class(info->id, image_class);
	}
	return ret;
}

void AssetInstantiatorInterface::set_image_class(ImageAssetID, ImageClass)
{
}

ImageAssetID AssetManager::register_image_resource(FileHandle file, ImageClass image_class)
{
	std::lock_guard<std::mutex> holder{asset_bank_lock};
	return register_image_resource_nolock(std::move(file), image_class);
}

ImageAssetID AssetManager::register_image_resource(Filesystem &fs, const std::string &path, ImageClass image_class)
{
	std::lock_guard<std::mutex> holder{asset_bank_lock};

	Util::Hasher h;
	h.string(path);
	if (auto *asset = file_to_assets.find(h.get()))
		return asset->id;

	auto file = fs.open(path);
	if (!file)
		return {};

	auto id = register_image_resource_nolock(std::move(file), image_class);
	file_to_assets.insert_replace(h.get(), asset_bank[id.id]);
	return id;
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
	}
	total_consumed = 0;

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
	std::lock_guard<std::mutex> holder{asset_bank_lock};
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

		// A recently paged in image shouldn't be paged out right away in a situation where we're thrashing,
		// that'd be very dumb.
		a->last_used = timestamp;
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

	std::lock_guard<std::mutex> holder{asset_bank_lock};

	{
		std::lock_guard<std::mutex> holder_cost{cost_update_lock};
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

	// If we're 75% of budget, start garbage collecting non-resident resources ahead of time.
	const uint64_t low_image_budget = (image_budget * 3) / 4;

	const auto should_release = [&]() -> bool {
		if (release_index == activate_index)
			return false;

		if (total_consumed > image_budget)
			return true;
		else if (total_consumed > low_image_budget && sorted_assets[release_index - 1]->prio == 0)
			return true;

		return false;
	};

	// If we're over budget, deactivate resources.
	while (should_release())
	{
		auto *candidate = sorted_assets[--release_index];
		if (candidate->consumed)
		{
			iface->release_image_resource(candidate->id);
			total_consumed -= candidate->consumed;
			candidate->consumed = 0;
			candidate->last_used = 0;
		}
	}

	iface->latch_handles();
	timestamp++;
}
}
