#version 450
#extension GL_GOOGLE_include_directive : require
#include "Common.glsl"

// Used by every sprite and mesh. Sprites are unit quads scaled by their size.

layout(location = 0) in vec3 InPosition;
layout(location = 1) in vec3 InNormal;
layout(location = 2) in vec2 InUV;
layout(location = 3) in vec4 InTangent;

layout(location = 0) out vec3 OutWorldPosition;
layout(location = 1) out vec2 OutUV;
layout(location = 2) out vec3 OutNormal;
layout(location = 3) out vec4 OutTangent;
layout(location = 4) out vec4 OutTint;

void main()
{
	vec4 world = Object.Model * vec4(InPosition, 1.0);
	OutWorldPosition = world.xyz;
	OutUV = InUV * Object.UVTransform.xy + Object.UVTransform.zw;
	OutNormal = normalize(mat3(Object.NormalMatrix) * InNormal);
	OutTangent = vec4(normalize(mat3(Object.Model) * InTangent.xyz), InTangent.w);
	OutTint = Object.Tint;
	gl_Position = Object.ViewProjection * world;
}
