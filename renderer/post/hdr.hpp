#pragma once

#include "render_graph.hpp"

namespace Granite
{
void setup_hdr_postprocess(RenderGraph &graph, const std::string &input, const std::string &output);
}
