#version 450

// Camera facing particle quads, built on the CPU each frame.

layout(set = 1, binding = 0) uniform ParticleData
{
	mat4 ViewProjection;
} Particles;

layout(location = 0) in vec3 InPosition;
layout(location = 1) in vec2 InUV;
layout(location = 2) in vec4 InColor;

layout(location = 0) out vec2 OutUV;
layout(location = 1) out vec4 OutColor;

void main()
{
	OutUV = InUV;
	OutColor = InColor;
	gl_Position = Particles.ViewProjection * vec4(InPosition, 1.0);
}
