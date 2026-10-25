#version 450

layout(set = 2, binding = 0) uniform sampler2D Source;
layout(set = 3, binding = 0) uniform FXAAData
{
	vec4 Params; // xy inverse output size
} FXAA;
layout(location = 0) in vec2 InUV;
layout(location = 0) out vec4 OutColor;

float Luma(vec3 color)
{
	// The sRGB texture decodes on sampling; edge detection needs perceptual contrast.
	return sqrt(max(dot(color, vec3(0.299, 0.587, 0.114)), 0.0));
}

void main()
{
	vec2 pixel = FXAA.Params.xy;
	vec3 center = texture(Source, InUV).rgb;
	float nw = Luma(texture(Source, InUV + pixel * vec2(-1, -1)).rgb);
	float ne = Luma(texture(Source, InUV + pixel * vec2(1, -1)).rgb);
	float sw = Luma(texture(Source, InUV + pixel * vec2(-1, 1)).rgb);
	float se = Luma(texture(Source, InUV + pixel * vec2(1, 1)).rgb);
	float middle = Luma(center);
	float low = min(middle, min(min(nw, ne), min(sw, se)));
	float high = max(middle, max(max(nw, ne), max(sw, se)));
	if (high - low < max(0.0312, high * 0.125))
	{
		OutColor = vec4(center, 1.0);
		return;
	}
	vec2 direction = vec2(-((nw + ne) - (sw + se)), (nw + sw) - (ne + se));
	float reduce = max((nw + ne + sw + se) * (0.25 / 8.0), 1.0 / 128.0);
	direction = clamp(direction / (min(abs(direction.x), abs(direction.y)) + reduce), vec2(-8), vec2(8)) * pixel;
	vec3 a = 0.5 * (texture(Source, InUV + direction * (-1.0 / 6.0)).rgb + texture(Source, InUV + direction * (1.0 / 6.0)).rgb);
	vec3 b = a * 0.5 + 0.25 * (texture(Source, InUV - direction * 0.5).rgb + texture(Source, InUV + direction * 0.5).rgb);
	float lumaB = Luma(b);
	OutColor = vec4(lumaB < low || lumaB > high ? a : b, 1.0);
}
