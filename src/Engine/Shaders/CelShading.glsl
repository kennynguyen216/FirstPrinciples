// Shared cel style for every material shader. Quantize brightness, preserving light hue
// and HDR values. Square-root spacing leaves more bands in dim interiors.
bool IsCelShaded()
{
	return Material.Style.x > 0.5;
}

vec3 CelBands(vec3 color)
{
	float brightness = dot(max(color, vec3(0.0)), vec3(0.2126, 0.7152, 0.0722));
	if (brightness <= 1e-5)
		return vec3(0.0);
	float band = floor(sqrt(brightness) * 4.0 + 0.5) / 4.0;
	return color * (band * band / brightness);
}

// A narrow, dark silhouette on curved surfaces without an extra geometry pass.
float CelEdge(float nDotV)
{
	return mix(0.15, 1.0, step(0.18, nDotV));
}
