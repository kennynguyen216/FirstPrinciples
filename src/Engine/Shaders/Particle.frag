#version 450

// Particles are their color times the emitter's image, or a soft round dot without one.

layout(set = 2, binding = 0) uniform sampler2D Image;

layout(set = 3, binding = 0) uniform ParticleLook
{
	vec4 Params; // x is 1 to use Image
} Look;

layout(location = 0) in vec2 InUV;
layout(location = 1) in vec4 InColor;
layout(location = 0) out vec4 OutColor;

void main()
{
	vec4 color = InColor;
	if (Look.Params.x > 0.5)
	{
		color *= texture(Image, InUV);
	}
	else
	{
		float edge = clamp(1.0 - length(InUV * 2.0 - 1.0), 0.0, 1.0);
		color.a *= edge * edge;
	}
	OutColor = color;
}
