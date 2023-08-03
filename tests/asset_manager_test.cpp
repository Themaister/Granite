#include "asset_manager.hpp"
#include "filesystem.hpp"
#include "logging.hpp"

using namespace Granite;

struct ActivationInterface final : AssetInstantiatorImagesInterface
{
	uint64_t estimate_cost_image_resource(ImageAssetID, File &mapping) override
	{
		return mapping.get_size();
	}

	void instantiate_image_resource(AssetManagerImages &manager, TaskGroup *, ImageAssetID id, File &mapping) override
	{
		LOGI("Instantiating ID: %u\n", id.id);
		manager.update_cost(id, mapping.get_size());
	}

	void release_image_resource(ImageAssetID id) override
	{
		LOGI("Releasing ID: %u\n", id.id);
	}

	void set_id_bounds(uint32_t bound_) override
	{
		bound = bound_;
		LOGI("ID bound: %u\n", bound);
	}

	void latch_handles() override
	{
	}

	uint32_t bound = 0;
};

int main()
{
	Filesystem fs;
	AssetManagerImages manager;
	ActivationInterface iface;
	fs.register_protocol("tmp", std::make_unique<ScratchFilesystem>());

	{ auto a = fs.open_writeonly_mapping("tmp://a", 1); }
	{ auto b = fs.open_writeonly_mapping("tmp://b", 2); }
	{ auto c = fs.open_writeonly_mapping("tmp://c", 4); }
	{ auto d = fs.open_writeonly_mapping("tmp://d", 8); }
	{ auto e = fs.open_writeonly_mapping("tmp://e", 16); }

	auto a = fs.open("tmp://a");
	auto b = fs.open("tmp://b");
	auto c = fs.open("tmp://c");
	auto d = fs.open("tmp://d");
	auto e = fs.open("tmp://e");

	auto id_a = manager.register_image_resource(std::move(a), ImageClass::Zeroable);
	auto id_b = manager.register_image_resource(std::move(b), ImageClass::Zeroable);
	auto id_c = manager.register_image_resource(std::move(c), ImageClass::Zeroable);
	auto id_d = manager.register_image_resource(std::move(d), ImageClass::Zeroable);
	manager.set_asset_instantiator_interface(&iface);
	auto id_e = manager.register_image_resource(std::move(e), ImageClass::Zeroable);

	manager.set_image_budget(25);
	manager.set_image_budget_per_iteration(5);

	manager.set_image_residency_priority(id_a, 1);
	manager.set_image_residency_priority(id_b, 1);
	manager.set_image_residency_priority(id_c, 1);
	manager.set_image_residency_priority(id_d, 1);
	manager.set_image_residency_priority(id_e, 2);
	manager.iterate(nullptr);
	LOGI("Cost: %u\n", unsigned(manager.get_current_total_consumed()));
	manager.iterate(nullptr);
	LOGI("Cost: %u\n", unsigned(manager.get_current_total_consumed()));
	manager.set_image_residency_priority(id_e, 0);
	manager.iterate(nullptr);
	LOGI("Cost: %u\n", unsigned(manager.get_current_total_consumed()));
	manager.set_image_budget(10);
	manager.iterate(nullptr);
	LOGI("Cost: %u\n", unsigned(manager.get_current_total_consumed()));
}