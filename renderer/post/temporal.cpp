/* Copyright (c) 2017-2024 Hans-Kristian Arntzen
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

#include "temporal.hpp"
#include "fxaa.hpp"
#include "enum_cast.hpp"
#include "muglm/matrix_helper.hpp"
#include "muglm/muglm_impl.hpp"
#include "simd.hpp"
#include "render_graph.hpp"
#include "render_context.hpp"
#include "os_filesystem.hpp"
#include "global_managers.hpp"
#include "path_utils.hpp"

#include "ffx_fsr2_granite.h"
#include "ffx_fsr2.h"

namespace Granite
{
TemporalJitter::TemporalJitter()
{
	init(Type::None, vec2(0.0f));
}

void TemporalJitter::init_banks()
{
	saved_jittered_view_proj.resize(jitter_count);
	saved_jittered_inv_view_proj.resize(jitter_count);
	saved_view_proj.resize(jitter_count);
	saved_inv_view_proj.resize(jitter_count);
}

void TemporalJitter::init_custom(const vec2 *phases, unsigned phase_count, vec2 backbuffer_resolution)
{
	jitter_table.clear();
	jitter_table.reserve(phase_count);
	for (unsigned i = 0; i < phase_count; i++)
	{
		jitter_table.push_back(translate(2.0f * vec3(phases[i].x / backbuffer_resolution.x,
		                                             phases[i].y / backbuffer_resolution.y, 0.0f)));
	}

	jitter_count = phase_count;
	type = Type::Custom;
	init_banks();
}

void TemporalJitter::init(Type type_, vec2 backbuffer_resolution)
{
	type = type_;

	switch (type)
	{
	case Type::FXAA_2Phase:
		jitter_count = 2;
		phase = 0;
		jitter_table.resize(jitter_count);
		jitter_table[0] = translate(2.0f * vec3(0.5f / backbuffer_resolution.x, 0.0f, 0.0f));
		jitter_table[1] = translate(2.0f * vec3(0.0f, 0.5f / backbuffer_resolution.y, 0.0f));
		break;

	case Type::SMAA_T2X:
		jitter_count = 2;
		phase = 0;
		jitter_table.resize(jitter_count);
		jitter_table[0] = translate(2.0f * vec3(-0.25f / backbuffer_resolution.x, -0.25f / backbuffer_resolution.y, 0.0f));
		jitter_table[1] = translate(2.0f * vec3(+0.25f / backbuffer_resolution.x, +0.25f / backbuffer_resolution.y, 0.0f));
		break;

	case Type::TAA_8Phase:
		jitter_count = 8;
		phase = 0;
		jitter_table.resize(jitter_count);
		jitter_table[0] = translate(0.125f * vec3(-7.0f / backbuffer_resolution.x, +1.0f / backbuffer_resolution.y, 0.0f));
		jitter_table[1] = translate(0.125f * vec3(-5.0f / backbuffer_resolution.x, -5.0f / backbuffer_resolution.y, 0.0f));
		jitter_table[2] = translate(0.125f * vec3(-1.0f / backbuffer_resolution.x, -3.0f / backbuffer_resolution.y, 0.0f));
		jitter_table[3] = translate(0.125f * vec3(+3.0f / backbuffer_resolution.x, -7.0f / backbuffer_resolution.y, 0.0f));
		jitter_table[4] = translate(0.125f * vec3(-5.0f / backbuffer_resolution.x, -1.0f / backbuffer_resolution.y, 0.0f));
		jitter_table[5] = translate(0.125f * vec3(+7.0f / backbuffer_resolution.x, +7.0f / backbuffer_resolution.y, 0.0f));
		jitter_table[6] = translate(0.125f * vec3(+1.0f / backbuffer_resolution.x, +3.0f / backbuffer_resolution.y, 0.0f));
		jitter_table[7] = translate(0.125f * vec3(-3.0f / backbuffer_resolution.x, +5.0f / backbuffer_resolution.y, 0.0f));
		break;

	case Type::TAA_16Phase:
		jitter_count = 16;
		phase = 0;
		jitter_table.resize(jitter_count);
		jitter_table[ 0] = translate(0.125f * vec3(-8.0f / backbuffer_resolution.x, 0.0f / backbuffer_resolution.y, 0.0f));
		jitter_table[ 1] = translate(0.125f * vec3(-6.0f / backbuffer_resolution.x, -4.0f / backbuffer_resolution.y, 0.0f));
		jitter_table[ 2] = translate(0.125f * vec3(-3.0f / backbuffer_resolution.x, -2.0f / backbuffer_resolution.y, 0.0f));
		jitter_table[ 3] = translate(0.125f * vec3(-2.0f / backbuffer_resolution.x, -6.0f / backbuffer_resolution.y, 0.0f));
		jitter_table[ 4] = translate(0.125f * vec3(1.0f / backbuffer_resolution.x, -1.0f / backbuffer_resolution.y, 0.0f));
		jitter_table[ 5] = translate(0.125f * vec3(2.0f / backbuffer_resolution.x, -5.0f / backbuffer_resolution.y, 0.0f));
		jitter_table[ 6] = translate(0.125f * vec3(6.0f / backbuffer_resolution.x, -7.0f / backbuffer_resolution.y, 0.0f));
		jitter_table[ 7] = translate(0.125f * vec3(5.0f / backbuffer_resolution.x, -3.0f / backbuffer_resolution.y, 0.0f));
		jitter_table[ 8] = translate(0.125f * vec3(4.0f / backbuffer_resolution.x, 1.0f / backbuffer_resolution.y, 0.0f));
		jitter_table[ 9] = translate(0.125f * vec3(7.0f / backbuffer_resolution.x, 4.0f / backbuffer_resolution.y, 0.0f));
		jitter_table[10] = translate(0.125f * vec3(3.0f / backbuffer_resolution.x, 5.0f / backbuffer_resolution.y, 0.0f));
		jitter_table[11] = translate(0.125f * vec3(0.0f / backbuffer_resolution.x, 7.0f / backbuffer_resolution.y, 0.0f));
		jitter_table[12] = translate(0.125f * vec3(-1.0f / backbuffer_resolution.x, 3.0f / backbuffer_resolution.y, 0.0f));
		jitter_table[13] = translate(0.125f * vec3(-4.0f / backbuffer_resolution.x, 6.0f / backbuffer_resolution.y, 0.0f));
		jitter_table[14] = translate(0.125f * vec3(-7.0f / backbuffer_resolution.x, 8.0f / backbuffer_resolution.y, 0.0f));
		jitter_table[15] = translate(0.125f * vec3(-5.0f / backbuffer_resolution.x, 2.0f / backbuffer_resolution.y, 0.0f));
		break;

	default:
		jitter_count = 1;
		phase = 0;
		jitter_table.resize(jitter_count);
		jitter_table[0] = mat4(1.0f);
		break;
	}

	init_banks();
}

void TemporalJitter::step(const mat4 &proj, const mat4 &view)
{
	phase++;
	if (phase >= jitter_count)
		phase = 0;

	SIMD::mul(saved_view_proj[get_jitter_phase()], proj, view);
	SIMD::mul(saved_jittered_projection, get_jitter_matrix(), proj);
	SIMD::mul(saved_jittered_view_proj[get_jitter_phase()], get_jitter_matrix(), saved_view_proj[get_jitter_phase()]);

	saved_inv_view_proj[get_jitter_phase()] = inverse(saved_view_proj[get_jitter_phase()]);
	saved_jittered_inv_view_proj[get_jitter_phase()] = inverse(saved_jittered_view_proj[get_jitter_phase()]);
}

unsigned TemporalJitter::get_offset_phase(int frames) const
{
	if (phase >= unsigned(frames))
		return phase - frames;
	else
		return jitter_count - frames;
}

const mat4 &TemporalJitter::get_history_view_proj(int frames) const
{
	return saved_view_proj[get_offset_phase(frames)];
}

const mat4 &TemporalJitter::get_history_inv_view_proj(int frames) const
{
	return saved_inv_view_proj[get_offset_phase(frames)];
}

const mat4 &TemporalJitter::get_history_jittered_view_proj(int frames) const
{
	return saved_jittered_view_proj[get_offset_phase(frames)];
}

const mat4 &TemporalJitter::get_history_jittered_inv_view_proj(int frames) const
{
	return saved_jittered_inv_view_proj[get_offset_phase(frames)];
}

const mat4 &TemporalJitter::get_jitter_matrix() const
{
	return jitter_table[get_jitter_phase()];
}

const mat4 &TemporalJitter::get_jittered_projection() const
{
	return saved_jittered_projection;
}

void TemporalJitter::reset()
{
	phase = 0;
}

unsigned TemporalJitter::get_jitter_phase() const
{
	return phase;
}

void setup_taa_resolve(RenderGraph &graph, TemporalJitter &jitter, float scaling_factor,
                       const std::string &input, const std::string &input_depth,
                       const std::string &input_mv,
                       const std::string &output, TAAQuality quality)
{
	jitter.init(TemporalJitter::Type::TAA_16Phase,
	            vec2(graph.get_backbuffer_dimensions().width,
	                 graph.get_backbuffer_dimensions().height) * scaling_factor);

	AttachmentInfo taa_output;
	taa_output.size_class = SizeClass::InputRelative;
	taa_output.size_relative_name = input;
	taa_output.format = graph.get_device().image_format_is_supported(VK_FORMAT_B10G11R11_UFLOAT_PACK32,
	                                                                 VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) ?
	                    VK_FORMAT_B10G11R11_UFLOAT_PACK32 : VK_FORMAT_R16G16B16A16_SFLOAT;

	AttachmentInfo taa_history = taa_output;
	taa_history.format = VK_FORMAT_R16G16B16A16_SFLOAT;

	auto &resolve = graph.add_pass("taa-resolve", RENDER_GRAPH_QUEUE_GRAPHICS_BIT);
	resolve.add_color_output(output, taa_output);
	resolve.add_color_output(output + "-history", taa_history);
	auto &input_res = resolve.add_texture_input(input);
	auto &input_res_mv = resolve.add_texture_input(input_mv);
	auto &input_depth_res = resolve.add_texture_input(input_depth);
	auto &history = resolve.add_history_input(output + "-history");

	resolve.set_build_render_pass([&, q = Util::ecast(quality)](Vulkan::CommandBuffer &cmd) {
		auto &image = graph.get_physical_texture_resource(input_res);
		auto &image_mv = graph.get_physical_texture_resource(input_res_mv);
		auto &depth = graph.get_physical_texture_resource(input_depth_res);
		auto *prev = graph.get_physical_history_texture_resource(history);

		struct Push
		{
			mat4 reproj;
			vec4 inv_resolution;
		};
		Push push;

		push.reproj =
				translate(vec3(0.5f, 0.5f, 0.0f)) *
				scale(vec3(0.5f, 0.5f, 1.0f)) *
				jitter.get_history_view_proj(1) *
				jitter.get_history_inv_view_proj(0);

		push.inv_resolution = vec4(1.0f / image.get_image().get_create_info().width,
		                           1.0f / image.get_image().get_create_info().height,
		                           image.get_image().get_create_info().width,
		                           image.get_image().get_create_info().height);

		cmd.push_constants(&push, 0, sizeof(push));

		cmd.set_texture(0, 0, image, Vulkan::StockSampler::NearestClamp);
		cmd.set_texture(0, 1, depth, Vulkan::StockSampler::NearestClamp);
		cmd.set_texture(0, 2, image_mv, Vulkan::StockSampler::NearestClamp);
		if (prev)
			cmd.set_texture(0, 3, *prev, Vulkan::StockSampler::LinearClamp);

		Vulkan::CommandBufferUtil::draw_fullscreen_quad(cmd,
		                                                "builtin://shaders/quad.vert",
		                                                "builtin://shaders/post/taa_resolve.frag",
		                                                {
				                                                {"REPROJECTION_HISTORY", prev ? 1 : 0},
				                                                {"TAA_QUALITY",          q}
		                                                });
	});
}

void setup_fxaa_2phase_postprocess(RenderGraph &graph, TemporalJitter &jitter, const std::string &input,
                                   const std::string &input_depth, const std::string &output)
{
	jitter.init(TemporalJitter::Type::FXAA_2Phase,
	            vec2(graph.get_backbuffer_dimensions().width, graph.get_backbuffer_dimensions().height));

	setup_fxaa_postprocess(graph, input, "fxaa-pre", VK_FORMAT_R8G8B8A8_UNORM);
	graph.get_texture_resource("fxaa-pre").get_attachment_info().flags |= ATTACHMENT_INFO_UNORM_SRGB_ALIAS_BIT;

	auto &sharpen = graph.add_pass("fxaa-sharpen", RenderGraph::get_default_post_graphics_queue());
	AttachmentInfo att, backbuffer_att;
	att.size_relative_name = input;
	att.size_class = SizeClass::InputRelative;
	att.format = VK_FORMAT_R8G8B8A8_SRGB;
	backbuffer_att = att;
	backbuffer_att.format = VK_FORMAT_UNDEFINED;

	sharpen.add_color_output(output, backbuffer_att);
	sharpen.add_color_output("fxaa-sharpen", att);
	auto &input_res = sharpen.add_texture_input("fxaa-pre");
	auto &depth_res = sharpen.add_texture_input(input_depth);
	auto &history_res = sharpen.add_history_input("fxaa-sharpen");

	sharpen.set_build_render_pass([&](Vulkan::CommandBuffer &cmd) {
		auto *history = graph.get_physical_history_texture_resource(history_res);
		auto &fxaa = graph.get_physical_texture_resource(input_res);
		auto &depth = graph.get_physical_texture_resource(depth_res);

		struct Push
		{
			mat4 reproj;
			vec2 inv_resolution;
		};
		Push push;

		push.reproj =
				translate(vec3(0.5f, 0.5f, 0.0f)) *
				scale(vec3(0.5f, 0.5f, 1.0f)) *
				jitter.get_history_view_proj(1) *
				jitter.get_history_inv_view_proj(0);

		push.inv_resolution = vec2(1.0f / fxaa.get_image().get_create_info().width,
		                           1.0f / fxaa.get_image().get_create_info().height);

		auto &output_image = graph.get_physical_texture_resource(sharpen.get_color_outputs()[0]->get_physical_index());
		bool srgb = Vulkan::format_is_srgb(output_image.get_format());
		cmd.set_sampler(0, 0, Vulkan::StockSampler::LinearClamp);
		if (srgb)
			cmd.set_srgb_texture(0, 0, fxaa);
		else
			cmd.set_unorm_texture(0, 0, fxaa);

		if (history)
		{
			cmd.set_texture(0, 1, *history, Vulkan::StockSampler::LinearClamp);
			cmd.set_texture(0, 2, depth, Vulkan::StockSampler::NearestClamp);
		}

		cmd.push_constants(&push, 0, sizeof(push));
		Vulkan::CommandBufferUtil::draw_fullscreen_quad(cmd, "builtin://shaders/quad.vert",
		                                                "builtin://shaders/post/aa_sharpen_resolve.frag",
		                                                {{"REPROJECTION_HISTORY", history ? 1 : 0},
		                                                 {"HORIZONTAL",           jitter.get_jitter_phase() == 0 ? 1
		                                                                                                         : 0},
		                                                 {"VERTICAL",             jitter.get_jitter_phase() == 1 ? 1
		                                                                                                         : 0}
		                                                });
	});
}

struct FSR2State : RenderPassInterface
{
	~FSR2State() override;
	RenderGraph *graph = nullptr;
	RenderTextureResource *color = nullptr;
	RenderTextureResource *depth = nullptr;
	RenderTextureResource *mv = nullptr;
	RenderTextureResource *output = nullptr;

	const Vulkan::ImageView *color_view = nullptr;
	const Vulkan::ImageView *depth_view = nullptr;
	const Vulkan::ImageView *mv_view = nullptr;
	const Vulkan::ImageView *output_view = nullptr;
	const TemporalJitter *jitter = nullptr;
	const RenderContext *render_context = nullptr;

	void build_render_pass(Vulkan::CommandBuffer &cmd) override;
	void setup(Vulkan::Device &device) override;
	void enqueue_prepare_render_pass(RenderGraph &graph, TaskComposer &composer) override;

	FfxFsr2ContextDescription desc = {};
	FfxFsr2Context context;
	void *scratch = nullptr;
	int phase_count = 0;
};

FSR2State::~FSR2State()
{
	ffxFsr2ContextDestroy(&context);
	free(scratch);
}

void FSR2State::enqueue_prepare_render_pass(RenderGraph &graph_, TaskComposer &)
{
	color_view = &graph_.get_physical_texture_resource(*color);
	depth_view = &graph_.get_physical_texture_resource(*depth);
	mv_view = &graph_.get_physical_texture_resource(*mv);
	output_view = &graph_.get_physical_texture_resource(*output);
}

void FSR2State::setup(Vulkan::Device &device)
{
	FfxErrorCode code;

	if (GRANITE_FILESYSTEM()->get_protocols().find("fsr2") == GRANITE_FILESYSTEM()->get_protocols().end())
	{
		auto self_dir = Path::basedir(Path::get_executable_path());
		auto fsr2_dir = Path::join(self_dir, "fsr2");
		FileStat s = {};
		if (GRANITE_FILESYSTEM()->stat(fsr2_dir, s) && s.type == PathType::Directory)
		{
			LOGI("Setting up FSR2 shader path: %s.\n", fsr2_dir.c_str());
			GRANITE_FILESYSTEM()->register_protocol("fsr2", std::make_unique<OSFilesystem>(fsr2_dir));
		}
#ifdef GRANITE_FSR2_SHADER_DIR
		else
		{
			LOGI("Setting up FSR2 shader path: %s.\n", GRANITE_FSR2_SHADER_DIR);
			GRANITE_FILESYSTEM()->register_protocol("fsr2", std::make_unique<OSFilesystem>(GRANITE_FSR2_SHADER_DIR));
		}
#endif
	}

	scratch = malloc(ffxFsr2GetScratchMemorySizeGranite());
	code = ffxFsr2GetInterfaceGranite(&desc.callbacks, scratch, ffxFsr2GetScratchMemorySizeGranite());
	if (code != FFX_OK)
	{
		LOGE("Failed to get FSR2 Granite interface (code = %x).\n", code);
		return;
	}

	desc.device = ffxGetDeviceGranite(&device);

	code = ffxFsr2ContextCreate(&context, &desc);
	if (code != FFX_OK)
	{
		LOGE("Failed to create FSR2 context (code = %x).\n", code);
		return;
	}
}

void FSR2State::build_render_pass(Vulkan::CommandBuffer &cmd)
{
	FfxFsr2DispatchDescription dispatch = {};
	dispatch.commandList = ffxGetCommandListGranite(&cmd);
	dispatch.color = ffxGetTextureResourceGranite(&context, &color_view->get_image(), color_view);
	dispatch.depth = ffxGetTextureResourceGranite(&context, &depth_view->get_image(), depth_view);
	dispatch.motionVectors = ffxGetTextureResourceGranite(&context, &mv_view->get_image(), mv_view);
	dispatch.output = ffxGetTextureResourceGranite(&context, &output_view->get_image(), output_view,
	                                               FFX_RESOURCE_STATE_UNORDERED_ACCESS);
	// Our MVs are from old frame to current. Negating should "just werk".
	dispatch.motionVectorScale.x = -float(mv_view->get_view_width());
	dispatch.motionVectorScale.y = -float(mv_view->get_view_height());
	dispatch.renderSize.width = color_view->get_view_width();
	dispatch.renderSize.height = color_view->get_view_height();
	dispatch.enableSharpening = true;
	dispatch.sharpness = 0.5f;
	dispatch.preExposure = 0.0f; // Using AUTO
	dispatch.reset = render_context->get_frame_parameters().discontinuous_camera;
	dispatch.frameTimeDelta = float(render_context->get_frame_parameters().frame_time * 1000.0);
	dispatch.cameraFar = render_context->get_render_parameters().z_far;
	dispatch.cameraNear = render_context->get_render_parameters().z_near;

	// Not sure if this is correct.
	float proj_y_scale = muglm::abs(render_context->get_render_parameters().inv_projection[1][1]);
	float fovY = 2.0f * muglm::atan(proj_y_scale);
	dispatch.cameraFovAngleVertical = fovY;

	ffxFsr2GetJitterOffset(&dispatch.jitterOffset.x, &dispatch.jitterOffset.y,
	                       int(jitter->get_jitter_phase()), phase_count);

	auto code = ffxFsr2ContextDispatch(&context, &dispatch);
	if (code != FFX_OK)
		LOGE("Failed to dispatch context.\n");
}

void setup_fsr2_pass(RenderGraph &graph, TemporalJitter &jitter,
                     const RenderContext &context,
                     float scaling_factor,
                     const std::string &input,
                     const std::string &input_depth,
                     const std::string &input_mv,
                     const std::string &output)
{
	auto fsr2 = Util::make_handle<FSR2State>();

	auto &pass = graph.add_pass("fsr2", RENDER_GRAPH_QUEUE_COMPUTE_BIT);

	fsr2->color = &pass.add_texture_input(input);
	fsr2->depth = &pass.add_texture_input(input_depth);
	fsr2->mv = &pass.add_texture_input(input_mv);
	fsr2->graph = &graph;

	AttachmentInfo info;
	info.size_class = SizeClass::SwapchainRelative;
	info.format = graph.get_resource_dimensions(*fsr2->color).format;
	fsr2->output = &pass.add_storage_texture_output(output, info);

	fsr2->desc.displaySize.width = graph.get_resource_dimensions(*fsr2->output).width;
	fsr2->desc.displaySize.height = graph.get_resource_dimensions(*fsr2->output).height;
	fsr2->desc.maxRenderSize.width = graph.get_resource_dimensions(*fsr2->color).width;
	fsr2->desc.maxRenderSize.height = graph.get_resource_dimensions(*fsr2->color).height;
	fsr2->desc.flags |= FFX_FSR2_ENABLE_AUTO_EXPOSURE | FFX_FSR2_ENABLE_HIGH_DYNAMIC_RANGE;
	fsr2->jitter = &jitter;
	fsr2->render_context = &context;

	int phase_count = ffxFsr2GetJitterPhaseCount(int32_t(fsr2->desc.maxRenderSize.width),
	                                             int32_t(fsr2->desc.displaySize.width));

	fsr2->phase_count = phase_count;

	std::vector<vec2> phase;
	phase.reserve(phase_count);

	for (int i = 0; i < phase_count; i++)
	{
		vec2 offset;
		ffxFsr2GetJitterOffset(&offset.x, &offset.y, i, phase_count);
		phase.push_back(offset);
		// Docs use (pos, neg) offsets here, but that's because DX does Y-flip in window space transform.
		// We don't.
	}

	auto backbuffer_dim = graph.get_backbuffer_dimensions();

	jitter.init_custom(phase.data(), unsigned(phase.size()),
	                   vec2(backbuffer_dim.width, backbuffer_dim.height) * scaling_factor);

	pass.set_render_pass_interface(fsr2);
}
}