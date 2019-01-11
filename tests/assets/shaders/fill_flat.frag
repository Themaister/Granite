#version 450

#if OUTPUT_COMPONENTS == 1
layout(location = 0) out float FragColor;
void main() { FragColor = 0.2; }
#elif OUTPUT_COMPONENTS == 2
layout(location = 0) out vec2 FragColor;
void main() { FragColor = vec2(0.2); }
#elif OUTPUT_COMPONENTS == 3
layout(location = 0) out vec3 FragColor;
void main() { FragColor = vec3(0.2); }
#elif OUTPUT_COMPONENTS == 4
layout(location = 0) out vec4 FragColor;
void main() { FragColor = vec4(0.2); }
#else
#error "Invalid number of components."
#endif
