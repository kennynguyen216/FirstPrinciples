#version 450
#extension GL_GOOGLE_include_directive : require

#define FP_ENVIRONMENT_BINDING 0
#include "Environment.glsl"

layout(set = 3, binding = 0) uniform SkyboxData
{
	mat4 InverseViewProjection;
	vec4 CameraPosition;
	vec4 CameraForward;
	vec4 Params;
	vec4 Size;
} Skybox;

layout(location = 0) in vec2 InUV;
layout(location = 0) out vec4 OutColor;

void main()
{
	vec4 point = Skybox.InverseViewProjection * vec4(InUV.x * 2.0 - 1.0, 1.0 - InUV.y * 2.0, 1.0, 1.0);
	vec3 direction = Skybox.CameraPosition.w > 0.5 ? Skybox.CameraForward.xyz : normalize(point.xyz / point.w - Skybox.CameraPosition.xyz);
	direction = EnvironmentDirection(direction, Skybox.Params.y);
	OutColor = vec4(EnvironmentRadiance(direction, 0.0, Skybox.Size) * Skybox.Params.x, 1.0);
}
