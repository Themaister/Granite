#version 450
precision highp float;

layout(location = 0) in highp vec2 vUV;

#ifdef HAVE_TARGET_0
layout(location = 0) out vec4 FragColor0;
layout(set = 0, binding = 0) uniform sampler2D uSampler0;
#endif

#ifdef HAVE_TARGET_1
layout(location = 1) out vec4 FragColor1;
layout(set = 0, binding = 1) uniform sampler2D uSampler1;
#endif

#ifdef HAVE_TARGET_2
layout(location = 2) out vec4 FragColor2;
layout(set = 0, binding = 2) uniform sampler2D uSampler2;
#endif

#ifdef HAVE_TARGET_3
layout(location = 3) out vec4 FragColor3;
layout(set = 0, binding = 3) uniform sampler2D uSampler3;
#endif

void main()
{
#ifdef HAVE_TARGET_0
    FragColor0 = textureLod(uSampler0, vUV, 0.0);
#endif
#ifdef HAVE_TARGET_1
    FragColor1 = textureLod(uSampler1, vUV, 0.0);
#endif
#ifdef HAVE_TARGET_2
    FragColor2 = textureLod(uSampler2, vUV, 0.0);
#endif
#ifdef HAVE_TARGET_3
    FragColor3 = textureLod(uSampler3, vUV, 0.0);
#endif
}
