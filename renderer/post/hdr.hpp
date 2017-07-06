#pragma once

#include "render_graph.hpp"

namespace Granite
{
class BloomThresholdPass : public RenderPassImplementation
{
public:
	void build_render_pass(RenderPass &pass, Vulkan::CommandBuffer &cmd) override;
};

class BloomDownsamplePass : public RenderPassImplementation
{
public:
	void build_render_pass(RenderPass &pass, Vulkan::CommandBuffer &cmd) override;
};

class BloomUpsamplePass : public RenderPassImplementation
{
public:
	void build_render_pass(RenderPass &pass, Vulkan::CommandBuffer &cmd) override;
};

class TonemapPass : public RenderPassImplementation
{
public:
	void build_render_pass(RenderPass &pass, Vulkan::CommandBuffer &cmd) override;

	static void setup_hdr_postprocess(RenderGraph &graph, const std::string &input, const std::string &output);
};

class LuminanceAdaptPass : public RenderPassImplementation, public EventHandler
{
public:
	LuminanceAdaptPass();
	void build_render_pass(RenderPass &pass, Vulkan::CommandBuffer &cmd) override;

private:
	float last_frame_time = 0.0f;
	bool on_frame_time(const Event &e);
};
}
