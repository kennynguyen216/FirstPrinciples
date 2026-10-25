#version 450

layout(set = 2, binding = 0) uniform sampler2D Source;
layout(set = 3, binding = 0) uniform BloomData
{
	vec4 Params; // xy texel step, z HDR threshold, w 0 extract / 1 blur
} Bloom;
layout(location = 0) in vec2 InUV;
layout(location = 0) out vec4 OutColor;

vec3 Extract(vec2 uv)
{
	vec3 color = max(texture(Source, uv).rgb, vec3(0.0));
	float peak = max(color.r, max(color.g, color.b));
	return color * max(peak - Bloom.Params.z, 0.0) / max(peak, 0.0001);
}

void main()
{
	vec2 stepUV = Bloom.Params.xy;
	vec3 color;
	if (Bloom.Params.w < 0.5)
	{
		// Threshold before averaging so small emissive surfaces survive downsampling.
		color = (Extract(InUV + stepUV * vec2(-1, -1)) + Extract(InUV + stepUV * vec2(1, -1)) +
			Extract(InUV + stepUV * vec2(-1, 1)) + Extract(InUV + stepUV * vec2(1, 1))) * 0.25;
	}
	else
	{
		// Separable nine-tap Gaussian, paired into five bilinear samples.
		color = texture(Source, InUV).rgb * 0.227027;
		color += (texture(Source, InUV + stepUV * 1.384615).rgb + texture(Source, InUV - stepUV * 1.384615).rgb) * 0.316216;
		color += (texture(Source, InUV + stepUV * 3.230769).rgb + texture(Source, InUV - stepUV * 3.230769).rgb) * 0.070270;
	}
	OutColor = vec4(color, 1.0);
}
