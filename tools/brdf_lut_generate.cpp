#include "math.hpp"
#include "logging.hpp"
#include "muglm/matrix_helper.hpp"
#include "muglm/muglm_impl.hpp"
#include "memory_mapped_texture.hpp"

using namespace muglm;
using namespace Granite;
using namespace Granite::SceneFormats;

// Shameless copy-pasta from learnopengl.com. :)

static float RadicalInverse_VdC(uint32_t bits)
{
	bits = (bits << 16u) | (bits >> 16u);
	bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
	bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
	bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
	bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
	return float(bits) * 2.3283064365386963e-10f; // / 0x100000000
}

static vec2 Hammersley(uint i, uint N)
{
	return vec2(float(i) / float(N), RadicalInverse_VdC(i));
}

static vec3 ImportanceSampleGGX(vec2 Xi, vec3 N, float roughness)
{
	float a = roughness * roughness;

	float phi = 2.0f * pi<float>() * Xi.x;
	float cosTheta = muglm::sqrt((1.0f - Xi.y) / (1.0f + (a * a - 1.0f) * Xi.y));
	float sinTheta = muglm::sqrt(1.0f - cosTheta * cosTheta);

	// from spherical coordinates to cartesian coordinates
	vec3 H;
	H.x = cos(phi) * sinTheta;
	H.y = sin(phi) * sinTheta;
	H.z = cosTheta;

	// from tangent-space vector to world-space sample vector
	vec3 up        = abs(N.z) < 0.999f ? vec3(0.0f, 0.0f, 1.0f) : vec3(1.0f, 0.0f, 0.0f);
	vec3 tangent   = normalize(cross(up, N));
	vec3 bitangent = cross(N, tangent);

	vec3 sampleVec = tangent * H.x + bitangent * H.y + N * H.z;
	return normalize(sampleVec);
}

static float GeometrySchlickGGX(float NdotV, float roughness)
{
	float a = roughness;
	float k = (a * a) / 2.0f;

	float nom   = NdotV;
	float denom = NdotV * (1.0f - k) + k;

	return nom / denom;
}

static float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
	float NdotV = max(dot(N, V), 0.0f);
	float NdotL = max(dot(N, L), 0.0f);
	float ggx2 = GeometrySchlickGGX(NdotV, roughness);
	float ggx1 = GeometrySchlickGGX(NdotL, roughness);

	return ggx1 * ggx2;
}

static vec2 IntegrateBRDF(float NdotV, float roughness)
{
	vec3 V;
	V.x = muglm::sqrt(1.0f - NdotV * NdotV);
	V.y = 0.0f;
	V.z = NdotV;

	float A = 0.0f;
	float B = 0.0f;

	vec3 N = vec3(0.0f, 0.0f, 1.0f);

	const unsigned SAMPLE_COUNT = 1024u;
	for (unsigned i = 0u; i < SAMPLE_COUNT; i++)
	{
		vec2 Xi = Hammersley(i, SAMPLE_COUNT);
		vec3 H  = ImportanceSampleGGX(Xi, N, roughness);
		vec3 L  = normalize(2.0f * dot(V, H) * H - V);

		float NdotL = max(L.z, 0.0f);
		float NdotH = max(H.z, 0.0f);
		float VdotH = max(dot(V, H), 0.0f);

		if (NdotL > 0.0f)
		{
			float G = GeometrySmith(N, V, L, roughness);
			float G_Vis = (G * VdotH) / (NdotH * NdotV);
			float Fc = pow(1.0f - VdotH, 5.0f);

			A += (1.0f - Fc) * G_Vis;
			B += Fc * G_Vis;
		}
	}
	A /= float(SAMPLE_COUNT);
	B /= float(SAMPLE_COUNT);
	return vec2(A, B);
}

int main(int argc, char *argv[])
{
	if (argc != 2)
	{
		LOGE("Usage: %s <output.gtx>\n", argv[0]);
		return 1;
	}

	const unsigned width = 256;
	const unsigned height = 256;

	MemoryMappedTexture tex;
	tex.set_2d(VK_FORMAT_R16G16_SFLOAT, width, height);
	if (!tex.map_write(argv[1]))
	{
		LOGE("Failed to save image to: %s\n", argv[1]);
		return 1;
	}

	for (unsigned y = 0; y < height; y++)
	{
		for (unsigned x = 0; x < width; x++)
		{
			float NoV = (x + 0.5f) * (1.0f / width);
			float roughness = (y + 0.5f) * (1.0f / height);
			//roughness = roughness * 0.75f + 0.25f;
			*tex.get_layout().data_2d<u16vec2>(x, y) = floatToHalf(IntegrateBRDF(NoV, roughness));
		}
	}
}
