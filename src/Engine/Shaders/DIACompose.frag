#version 450
#extension GL_GOOGLE_include_directive : require
#include "DIAShared.glsl"
layout(set = 2, binding = 0) uniform sampler2D Diffuse;
layout(set = 2, binding = 1) uniform sampler2D Specular;
layout(set = 2, binding = 2) uniform sampler2D Exact;
layout(location = 0) in vec2 InUV;
layout(location = 0) out vec4 OutColor;

void main()
{
	ivec2 pixel = ivec2(clamp(InUV, vec2(0), vec2(0.999999)) * DIA.Extent.xy);
	vec3 color = texelFetch(Diffuse, pixel, 0).rgb + texelFetch(Specular, pixel, 0).rgb + texelFetch(Exact, pixel, 0).rgb;
	bool valid = !any(isnan(color)) && !any(isinf(color));
	OutColor = vec4(valid ? max(color, vec3(0)) : vec3(1, 0, 1), valid ? 1.0 : 0.0);
}
