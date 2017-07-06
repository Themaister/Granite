#pragma once

#include "math.hpp"
#include "gli/texture.hpp"

namespace Util
{
using namespace glm;

unsigned num_miplevels(unsigned width, unsigned height);
vec4 skybox_to_fog_color(const gli::texture &cube);
void filter_tiling_artifacts(gli::texture &texture, unsigned leve, const gli::image &input);
}