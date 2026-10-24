#version 450

// Fullscreen triangle with no vertex buffer.

layout(location = 0) out vec2 OutUV;

void main()
{
	vec2 position = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2) * 2.0 - 1.0;
	OutUV = vec2(position.x * 0.5 + 0.5, 0.5 - position.y * 0.5);
	gl_Position = vec4(position, 0.0, 1.0);
}
