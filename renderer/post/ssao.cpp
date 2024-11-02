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

#include "ssao.hpp"
#include "muglm/matrix_helper.hpp"
#include "global_managers.hpp"
#include "common_renderer_data.hpp"
#include "ffx_cacao_impl.h"

namespace Granite
{
struct FFX_CACAO_GraniteContext_Destroyer
{
	void operator()(FFX_CACAO_GraniteContext *ctx) { if (ctx) FFX_CACAO_GraniteDestroyContext(ctx); }
};

struct CACAOState : Util::IntrusivePtrEnabled<CACAOState>
{
	RenderTextureResource *output = nullptr;
	RenderTextureResource *depth = nullptr;
	RenderTextureResource *normal = nullptr;
	FFX_CACAO_GraniteScreenSizeInfo info = {};
	std::unique_ptr<FFX_CACAO_GraniteContext, FFX_CACAO_GraniteContext_Destroyer> context;
};

void setup_ffx_cacao(RenderGraph &graph, const RenderContext &context,
                     const std::string &output, const std::string &input_depth, const std::string &input_normal)
{
	AttachmentInfo info;
	info.format = VK_FORMAT_R8_UNORM;
	info.size_class = SizeClass::InputRelative;
	info.size_relative_name = input_depth;
	info.size_x = 1.0f;
	info.size_y = 1.0f;

	auto ctx = Util::make_handle<CACAOState>();

	auto &ffx = graph.add_pass(output, RENDER_GRAPH_QUEUE_COMPUTE_BIT);
	ctx->output = &ffx.add_storage_texture_output(output, info);
	ctx->depth = &ffx.add_texture_input(input_depth);
	if (!input_normal.empty())
		ctx->normal = &ffx.add_texture_input(input_normal);

	FFX_CACAO_GraniteContext *ffx_context = nullptr;
	FFX_CACAO_GraniteCreateInfo granite_info { &graph.get_device() };
	if (FFX_CACAO_GraniteAllocContext(&ffx_context, &granite_info) != FFX_CACAO_STATUS_OK)
	{
		LOGE("Failed to create CACAO context.\n");
		return;
	}

	ctx->context.reset(ffx_context);

	const FFX_CACAO_Settings settings = {
		/* radius                            */ 0.6f,
		/* shadowMultiplier                  */ 1.0f,
		/* shadowPower                       */ 1.50f,
		/* shadowClamp                       */ 0.98f,
		/* horizonAngleThreshold             */ 0.06f,
		/* fadeOutFrom                       */ 20.0f,
		/* fadeOutTo                         */ 40.0f,
		/* qualityLevel                      */ FFX_CACAO_QUALITY_HIGHEST,
		/* adaptiveQualityLimit              */ 0.75f,
		/* blurPassCount                     */ 2,
		/* sharpness                         */ 0.98f,
		/* temporalSupersamplingAngleOffset  */ 0.0f,
		/* temporalSupersamplingRadiusOffset */ 0.0f,
		/* detailShadowStrength              */ 0.5f,
		/* generateNormals                   */ input_normal.empty() ? FFX_CACAO_TRUE : FFX_CACAO_FALSE,
		/* bilateralSigmaSquared             */ 5.0f,
		/* bilateralSimilarityDistanceSigma  */ 0.1f,
	};

	//settings.generateNormals = FFX_CACAO_TRUE;
	FFX_CACAO_GraniteUpdateSettings(ffx_context, &settings);

	ffx.set_build_render_pass([ctx, &context, &graph](Vulkan::CommandBuffer &cmd) mutable {
		auto *depth_view = &graph.get_physical_texture_resource(*ctx->depth);
		auto *normal_view = ctx->normal ? &graph.get_physical_texture_resource(*ctx->normal) : nullptr;
		auto *output_view = &graph.get_physical_texture_resource(*ctx->output);

		if (depth_view != ctx->info.depthView ||
		    normal_view != ctx->info.normalsView ||
		    output_view != ctx->info.outputView)
		{
			ctx->info.outputView = output_view;
			ctx->info.normalsView = normal_view;
			ctx->info.depthView = depth_view;
			ctx->info.width = depth_view->get_image().get_width();
			ctx->info.height = depth_view->get_image().get_height();
			ctx->info.useDownsampledSsao = false;
			FFX_CACAO_GraniteDestroyScreenSizeDependentResources(ctx->context.get());
			FFX_CACAO_GraniteInitScreenSizeDependentResources(ctx->context.get(), &ctx->info);
		}

		auto &proj = context.get_render_parameters().projection;
		auto &normal_to_view = context.get_render_parameters().view;

		FFX_CACAO_GraniteDraw(ctx->context.get(), cmd,
		                      reinterpret_cast<const FFX_CACAO_Matrix4x4 *>(&proj),
		                      reinterpret_cast<const FFX_CACAO_Matrix4x4 *>(&normal_to_view));
	});
}
}
