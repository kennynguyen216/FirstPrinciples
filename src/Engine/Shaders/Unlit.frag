#version 450
#extension GL_GOOGLE_include_directive : require
#include "Common.glsl"

// Base shader with no lighting. Base color times tint plus emissive.

layout(set = 2, binding = 0) uniform sampler2D BaseColorMap;

layout(location = 0) in vec3 InWorldPosition;
layout(location = 1) in vec2 InUV;
layout(location = 2) in vec3 InNormal;
layout(location = 3) in vec4 InTangent;
layout(location = 4) in vec4 InTint;

layout(location = 0) out vec4 OutColor;

void main()
{
	vec4 color = texture(BaseColorMap, InUV) * Material.BaseColor * InTint;
	if (color.a < Material.Params2.y)
		discard;
	OutColor = vec4(color.rgb + Material.Emissive.rgb, color.a);
}
