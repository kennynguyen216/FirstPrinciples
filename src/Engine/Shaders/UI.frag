#version 450

// Draws a UI page over the finished frame. Pages come premultiplied, so the pipeline blends
// with one and one minus source alpha.

layout(set = 2, binding = 0) uniform sampler2D Page;

layout(location = 0) in vec2 InUV;
layout(location = 0) out vec4 OutColor;

void main()
{
	OutColor = texture(Page, InUV);
}
