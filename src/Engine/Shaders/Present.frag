#version 450

// Copies the HDR scene to the output with exposure and tonemapping.
// The output target is sRGB so the hardware does the final encode.

layout(set = 2, binding = 0) uniform sampler2D SceneColor;
layout(set = 2, binding = 1) uniform sampler2D BloomColor;

layout(set = 3, binding = 0) uniform PresentData
{
	vec4 Params; // x exposure, y tonemap on, z bloom intensity, w color vision mode
	vec4 Accessibility; // x correction strength
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

vec3 AssistColorVision(vec3 color)
{
	int mode = int(Present.Params.w);
	if (mode == 0 || Present.Accessibility.x <= 0.0) return color;
	vec3 adjusted;
	if (mode >= 7)
	{
		// shortcut: heuristic hue-to-brightness mapping, add semantic patterns for critical game cues.
		adjusted = vec3(dot(color, vec3(0.6, 0.3, 0.1)));
		if (mode == 8) adjusted = mix(color, adjusted, 0.5);
	}
	else
	{
		int type = (mode - 1) % 3;
		// Machado et al. (2009), linear RGB simulation at severity 1.0 / 0.5.
		// https://www.inf.ufrgs.br/~oliveira/pubs_files/CVD_Simulation/CVD_Simulation.html
		mat3 simulation;
		if (mode == 1) simulation = transpose(mat3(0.152286, 1.052583, -0.204868, 0.114503, 0.786281, 0.099216, -0.003882, -0.048116, 1.051998));
		else if (mode == 2) simulation = transpose(mat3(0.367322, 0.860646, -0.227968, 0.280085, 0.672501, 0.047413, -0.011820, 0.042940, 0.968881));
		else if (mode == 3) simulation = transpose(mat3(1.255528, -0.076749, -0.178779, -0.078411, 0.930809, 0.147602, 0.004733, 0.691367, 0.303900));
		else if (mode == 4) simulation = transpose(mat3(0.458064, 0.679578, -0.137642, 0.092785, 0.846313, 0.060902, -0.007494, -0.016807, 1.024301));
		else if (mode == 5) simulation = transpose(mat3(0.547494, 0.607765, -0.155259, 0.181692, 0.781742, 0.036566, -0.010410, 0.027275, 0.983136));
		else simulation = transpose(mat3(1.017277, 0.027029, -0.044306, -0.006113, 0.958479, 0.047634, 0.006379, 0.248708, 0.744913));
		vec3 error = color - simulation * color;
		// Redistribute lost contrast into the other channels to assist the selected deficiency.
		vec3 correction = type == 0 ? vec3(0, error.g + 0.7 * error.r, error.b + 0.7 * error.r) :
			type == 1 ? vec3(error.r + 0.7 * error.g, 0, error.b + 0.7 * error.g) :
			vec3(error.r + 0.7 * error.b, error.g + 0.7 * error.b, 0);
		adjusted = color + correction;
	}
	return mix(color, clamp(adjusted, 0.0, 1.0), Present.Accessibility.x);
}

void main()
{
	vec3 color = (texture(SceneColor, InUV).rgb + texture(BloomColor, InUV).rgb * Present.Params.z) * Present.Params.x;
	if (Present.Params.y > 0.5)
		color = NeutralTonemap(color);
	OutColor = vec4(AssistColorVision(clamp(color, 0.0, 1.0)), 1.0);
}
