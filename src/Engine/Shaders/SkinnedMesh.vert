#version 450
#extension GL_GOOGLE_include_directive : require
#define FP_VERTEX_SHADER
#define FP_SKINNED
#include "Common.glsl"

// Mesh.vert for skeletal meshes. Each vertex follows up to four bones before the model transform.

layout(location = 0) in vec3 InPosition;
layout(location = 1) in vec3 InNormal;
layout(location = 2) in vec2 InUV;
layout(location = 3) in vec4 InTangent;
layout(location = 4) in uvec4 InBones;
layout(location = 5) in vec4 InWeights;

layout(location = 0) out vec3 OutWorldPosition;
layout(location = 1) out vec2 OutUV;
layout(location = 2) out vec3 OutNormal;
layout(location = 3) out vec4 OutTangent;
layout(location = 4) out vec4 OutTint;

void main()
{
	mat4 skin = Bones.Matrices[InBones.x] * InWeights.x
		+ Bones.Matrices[InBones.y] * InWeights.y
		+ Bones.Matrices[InBones.z] * InWeights.z
		+ Bones.Matrices[InBones.w] * InWeights.w;

	vec4 world = Object.Model * (skin * vec4(InPosition, 1.0));
	OutWorldPosition = world.xyz;
	OutUV = InUV * Object.UVTransform.xy + Object.UVTransform.zw;
	OutNormal = normalize(mat3(Object.NormalMatrix) * (mat3(skin) * InNormal));
	OutTangent = vec4(normalize(mat3(Object.Model) * (mat3(skin) * InTangent.xyz)), InTangent.w);
	OutTint = Object.Tint;
	gl_Position = Object.ViewProjection * world;
}
