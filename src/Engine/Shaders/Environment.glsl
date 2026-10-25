#ifdef FP_METAL
#undef FP_ENVIRONMENT_BINDING
#ifdef FP_ENVIRONMENT_LIGHTING
#define FP_ENVIRONMENT_BINDING 2
#else
#define FP_ENVIRONMENT_BINDING 1
#endif
#endif

// Storage keeps environment lighting within the sixteen sampler limit.
layout(std430, set = 2, binding = FP_ENVIRONMENT_BINDING) readonly buffer EnvironmentData
{
	vec4 Pixels[];
} Environment;

vec3 EnvironmentDirection(vec3 direction, float rotation)
{
	float c = cos(rotation), s = sin(rotation);
	return vec3(c * direction.x - s * direction.z, direction.y, s * direction.x + c * direction.z);
}

vec3 EnvironmentPixel(ivec2 pixel, ivec2 size, int offset)
{
	pixel.x = (pixel.x % size.x + size.x) % size.x;
	pixel.y = clamp(pixel.y, 0, size.y - 1);
	return Environment.Pixels[offset + pixel.y * size.x + pixel.x].rgb;
}

vec3 EnvironmentLevel(vec3 direction, int level, vec4 dimensions)
{
	ivec2 size = ivec2(dimensions.xy);
	int offset = 0;
	for (int i = 0; i < level; ++i)
	{
		offset += size.x * size.y;
		size = max(size / 2, ivec2(1));
	}
	vec2 uv = vec2(atan(direction.z, direction.x) / 6.28318530718 + 0.5, acos(clamp(direction.y, -1.0, 1.0)) / 3.14159265359);
	vec2 coordinate = uv * vec2(size) - 0.5;
	ivec2 pixel = ivec2(floor(coordinate));
	vec2 f = fract(coordinate);
	return mix(mix(EnvironmentPixel(pixel, size, offset), EnvironmentPixel(pixel + ivec2(1, 0), size, offset), f.x),
		mix(EnvironmentPixel(pixel + ivec2(0, 1), size, offset), EnvironmentPixel(pixel + ivec2(1, 1), size, offset), f.x), f.y);
}

vec3 EnvironmentRadiance(vec3 direction, float roughness, vec4 dimensions)
{
	float level = clamp(roughness, 0.0, 1.0) * (dimensions.z - 1.0);
	int low = int(floor(level));
	int high = min(low + 1, int(dimensions.z) - 1);
	return mix(EnvironmentLevel(direction, low, dimensions), EnvironmentLevel(direction, high, dimensions), fract(level));
}

#ifdef FP_ENVIRONMENT_LIGHTING
vec3 EnvironmentDiffuse(vec3 normal)
{
	vec3 n = EnvironmentDirection(normal, Frame.EnvironmentParams.y);
	vec3 color = Frame.EnvironmentDiffuse[0].rgb * 0.282095
		+ Frame.EnvironmentDiffuse[1].rgb * (0.488603 * n.y)
		+ Frame.EnvironmentDiffuse[2].rgb * (0.488603 * n.z)
		+ Frame.EnvironmentDiffuse[3].rgb * (0.488603 * n.x)
		+ Frame.EnvironmentDiffuse[4].rgb * (1.092548 * n.x * n.y)
		+ Frame.EnvironmentDiffuse[5].rgb * (1.092548 * n.y * n.z)
		+ Frame.EnvironmentDiffuse[6].rgb * (0.315392 * (3.0 * n.z * n.z - 1.0))
		+ Frame.EnvironmentDiffuse[7].rgb * (1.092548 * n.x * n.z)
		+ Frame.EnvironmentDiffuse[8].rgb * (0.546274 * (n.x * n.x - n.y * n.y));
	return max(color, vec3(0.0)) * Frame.EnvironmentParams.x;
}

vec3 EnvironmentReflection(vec3 normal, vec3 view, float roughness)
{
	vec3 direction = EnvironmentDirection(reflect(-view, normal), Frame.EnvironmentParams.y);
	return EnvironmentRadiance(direction, roughness, Frame.EnvironmentSize) * Frame.EnvironmentParams.x;
}
#endif
