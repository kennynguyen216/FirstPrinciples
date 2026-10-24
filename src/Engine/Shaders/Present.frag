#version 450

// Copies the HDR scene to the output with exposure and tonemapping.
// The output target is sRGB so the hardware does the final encode.

layout(set = 2, binding = 0) uniform sampler2D SceneColor;

layout(set = 3, binding = 0) uniform PresentData
{
	vec4 Params; // x exposure, y tonemap on
} Present;

layout(location = 0) in vec2 InUV;
layout(location = 0) out vec4 OutColor;

// Khronos PBR Neutral. Leaves normal colors alone and only rolls off highlights.
vec3 NeutralTonemap(vec3 color)
{
	const float startCompression = 0.8 - 0.04;
	const float desaturation = 0.15;
	float x = min(color.r, min(color.g, color.b));
	float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
	color -= offset;
	float peak = max(color.r, max(color.g, color.b));
	if (peak < startCompression)
		return color;
	const float d = 1.0 - startCompression;
	float newPeak = 1.0 - d * d / (peak + d - startCompression);
	color *= newPeak / peak;
	float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
	return mix(color, vec3(newPeak), g);
}

void main()
{
	vec3 color = texture(SceneColor, InUV).rgb * Present.Params.x;
	if (Present.Params.y > 0.5)
		color = NeutralTonemap(color);
	OutColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}
