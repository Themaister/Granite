#pragma once

#include "math.hpp"

namespace Granite
{
class Camera
{
public:
	mat4 get_projection() const;
	mat4 get_view() const;

private:
};
}