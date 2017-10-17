#version 450
precision highp int;
precision highp float;

layout(set = 2, binding = 0) uniform samplerCube uCube;
layout(location = 0) out vec4 FragColor;
layout(location = 0) in highp vec3 vDirection;

layout(push_constant, std430) uniform Registers
{
    float lod;
} registers;

#define PI 3.1415628

// Shamelessly copypasted from learnopengl.com

void main()
{
    vec3 irradiance = vec3(0.0);
    vec3 dir = normalize(vDirection);
    vec3 up = vec3(0.0, 1.0, 0.0);
    vec3 right = cross(up, dir);
    up = cross(dir, right);

    float sample_delta = 0.025;
    float nr_samples = 0.0;

    for (float phi = 0.0; phi < 2.0 * PI; phi += sample_delta)
    {
        for (float theta = 0.0; theta < 0.5 * PI; theta += sample_delta)
        {
            // spherical to cartesian (in tangent space)
            vec3 tangent_sample = vec3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
            // tangent space to world
            vec3 sample_vec = tangent_sample.x * right + tangent_sample.y * up + tangent_sample.z * dir;

            irradiance += textureLod(uCube, sample_vec, registers.lod).rgb * cos(theta) * sin(theta);
            nr_samples++;
        }
    }

    irradiance = PI * irradiance * (1.0 / float(nr_samples));
    FragColor = vec4(irradiance, 1.0);
}