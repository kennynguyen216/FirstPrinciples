#version 450
#extension GL_GOOGLE_include_directive : require
#define FP_VERTEX_SHADER
#include "Common.glsl"

// Instance attributes keep base instance offsets portable across backends.

layout(location = 0) in vec3 InPosition;
layout(location = 1) in vec3 InNormal;
layout(location = 2) in vec2 InUV;
layout(location = 3) in vec4 InTangent;

layout(location = 4) in mat4 Model;
layout(location = 8) in mat4 NormalMatrix;
layout(location = 12) in vec4 UVTransform;
layout(location = 13) in vec4 Tint;

layout(location = 0) out vec3 OutWorldPosition;
layout(location = 1) out vec2 OutUV;
layout(location = 2) out vec3 OutNormal;
layout(location = 3) out vec4 OutTangent;
layout(location = 4) out vec4 OutTint;

void main()
{
	vec4 world = Model * vec4(InPosition, 1.0);
	OutWorldPosition = world.xyz;
	OutUV = InUV * UVTransform.xy + UVTransform.zw;
	OutNormal = normalize(mat3(NormalMatrix) * InNormal);
	OutTangent = vec4(normalize(mat3(Model) * InTangent.xyz), InTangent.w);
	OutTint = Tint;
	gl_Position = Object.ViewProjection * world;
}
