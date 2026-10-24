#version 450

// Editor and debug lines.

layout(set = 1, binding = 0) uniform LineData
{
	mat4 ViewProjection;
} Lines;

layout(location = 0) in vec3 InPosition;
layout(location = 1) in vec4 InColor;

layout(location = 0) out vec4 OutColor;

void main()
{
	OutColor = InColor;
	gl_Position = Lines.ViewProjection * vec4(InPosition, 1.0);
}
