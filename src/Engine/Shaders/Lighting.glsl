// PBR helpers shared by Lit and Glass. GGX specular, Smith visibility, Schlick fresnel.

const float PI = 3.14159265;

vec3 ViewDirection(vec3 worldPosition)
{
	// Orthographic cameras look along one direction everywhere.
	if (Frame.CameraPosition.w > 0.5)
		return -normalize(Frame.CameraForward.xyz);
	return normalize(Frame.CameraPosition.xyz - worldPosition);
}

// Tangent space normal map to world space.
vec3 ApplyNormalMap(vec3 normal, vec4 tangent, vec3 sampled, float strength)
{
	vec3 n = normalize(normal);
	vec3 t = normalize(tangent.xyz - n * dot(n, tangent.xyz));
	vec3 b = cross(n, t) * tangent.w;
	vec3 local = sampled * 2.0 - 1.0;
	local.xy *= strength;
	return normalize(t * local.x + b * local.y + n * local.z);
}

float DistributionGGX(float nDotH, float roughness)
{
	float a = roughness * roughness;
	float a2 = a * a;
	float d = nDotH * nDotH * (a2 - 1.0) + 1.0;
	return a2 / (PI * d * d);
}

float VisibilitySmith(float nDotV, float nDotL, float roughness)
{
	float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
	float gv = nDotV / (nDotV * (1.0 - k) + k);
	float gl = nDotL / (nDotL * (1.0 - k) + k);
	return gv * gl / max(4.0 * nDotV * nDotL, 1e-4);
}

vec3 FresnelSchlick(float cosTheta, vec3 f0)
{
	return f0 + (1.0 - f0) * pow(1.0 - cosTheta, 5.0);
}

// Direction toward light i and how much of it arrives at the point.
void GetLight(int i, vec3 worldPosition, out vec3 toLight, out vec3 radiance)
{
	vec4 position = Frame.LightPosition[i];
	vec4 color = Frame.LightColor[i];
	if (position.w < 0.5)
	{
		toLight = -normalize(position.xyz);
		radiance = color.rgb;
		return;
	}
	vec3 delta = position.xyz - worldPosition;
	float distance = length(delta);
	toLight = delta / max(distance, 1e-4);
	// Smooth window so the light reaches exactly zero at its range.
	float ratio = distance / max(color.w, 1e-4);
	float window = clamp(1.0 - ratio * ratio * ratio * ratio, 0.0, 1.0);
	radiance = color.rgb * window * window / (distance * distance + 1.0);
	if (position.w > 1.5)
	{
		float cosine = dot(-toLight, normalize(Frame.LightDirection[i].xyz));
		vec2 cone = Frame.LightCone[i].xy;
		radiance *= smoothstep(cone.y, max(cone.x, cone.y + 1e-4), cosine);
	}
}

// Specular highlight for one light, used by glass which has no diffuse.
vec3 SpecularOnly(vec3 n, vec3 v, vec3 l, vec3 radiance, float roughness, vec3 f0)
{
	vec3 h = normalize(v + l);
	float nDotL = max(dot(n, l), 0.0);
	float nDotV = max(dot(n, v), 1e-4);
	float nDotH = max(dot(n, h), 0.0);
	vec3 f = FresnelSchlick(max(dot(h, v), 0.0), f0);
	return DistributionGGX(nDotH, roughness) * VisibilitySmith(nDotV, nDotL, roughness) * f * radiance * nDotL * PI;
}

// Karis' fit of the split sum environment BRDF.
vec3 EnvironmentBRDF(vec3 f0, float roughness, float nDotV)
{
	const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
	const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
	vec4 r = roughness * c0 + c1;
	float a004 = min(r.x * r.x, exp2(-9.28 * nDotV)) * r.x + r.y;
	vec2 ab = vec2(-1.04, 1.04) * a004 + r.zw;
	return f0 * ab.x + ab.y;
}
