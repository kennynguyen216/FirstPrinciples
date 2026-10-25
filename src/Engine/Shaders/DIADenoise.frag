#version 450
#extension GL_GOOGLE_include_directive : require
#define DIA_MATERIAL_BINDING 8
#include "DIAShared.glsl"
layout(set = 2, binding = 0) uniform sampler2D InputDiffuse;
layout(set = 2, binding = 1) uniform sampler2D InputSpecular;
layout(set = 2, binding = 2) uniform sampler2D Position;
layout(set = 2, binding = 3) uniform sampler2D Normal;
layout(set = 2, binding = 4) uniform sampler2D Counts;
layout(set = 2, binding = 5) uniform sampler2D Reflected;
layout(set = 2, binding = 6) uniform sampler2D Identity;
layout(set = 2, binding = 7) uniform sampler2D RawSpecular;
layout(location = 0) in vec2 InUV;
layout(location = 0) out vec4 OutDiffuse;
layout(location = 1) out vec4 OutSpecular;

const vec3 LumaWeights = vec3(0.2126, 0.7152, 0.0722);
const float Kernel[3] = float[](0.375, 0.25, 0.0625);
const float LumaSigma = 4.0;

float Luma(vec3 color) { return dot(color, LumaWeights); }

bool Boundary(ivec2 pixel, float identity, ivec2 extent)
{
	for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x)
	{
		ivec2 neighbor = pixel + ivec2(x, y);
		if (any(lessThan(neighbor, ivec2(0))) || any(greaterThanEqual(neighbor, extent))) continue;
		if (texelFetch(Identity, neighbor, 0).a != identity) return true;
	}
	return false;
}

float GeometryWeight(vec3 position, vec3 normal, vec3 otherPosition, vec3 otherNormal)
{
	vec3 delta = otherPosition - position;
	float elevation = abs(dot(delta, normal)) / (length(delta) + 0.000001);
	return pow(max(dot(normal, otherNormal), 0.0), 128.0) * exp(-8.0 * elevation);
}

float LobeWeight(vec3 normal, vec3 otherNormal, float lobe)
{
	return exp(-4.0 * max(0.0, 1.0 - dot(normal, otherNormal)) / max(lobe * lobe, 0.00000001));
}

float Footprint(vec3 position, vec3 normal)
{
	vec2 ndc = vec2(InUV.x * 2.0 - 1.0, 1.0 - InUV.y * 2.0);
	vec4 nearPoint = DIA.InverseViewProjection * vec4(ndc, 0, 1);
	vec4 farPoint = DIA.InverseViewProjection * vec4(ndc, 1, 1);
	vec3 rayOrigin = nearPoint.xyz / nearPoint.w;
	vec3 rayEnd = farPoint.xyz / farPoint.w;
	vec3 ray = rayEnd - rayOrigin;
	float travel = dot(position - rayOrigin, ray) / dot(ray, ray);
	float planeDenominator = dot(normal, ray);
	float footprint = 0.0;
	if (abs(planeDenominator) > 0.000001)
	{
		for (int axis = 0; axis < 2; ++axis)
		{
			vec4 differential = DIA.InverseViewProjection[axis] * (axis == 0 ? 2.0 / DIA.Extent.x : -2.0 / DIA.Extent.y);
			vec3 nearDelta = (differential.xyz - rayOrigin * differential.w) / nearPoint.w;
			vec3 farDelta = (differential.xyz - rayEnd * differential.w) / farPoint.w;
			vec3 offset = mix(nearDelta, farDelta, travel);
			footprint = max(footprint, length(offset - ray * (dot(normal, offset) / planeDenominator)));
		}
	}
	return footprint;
}

bool MirrorReceiver(vec3 delta, vec3 normal, vec3 otherNormal)
{
	float tolerance = max(0.00001, min(0.002, 0.02 * length(delta)));
	return !any(isnan(delta)) && !any(isinf(delta)) && !any(isnan(normal)) && !any(isinf(normal)) &&
		!any(isnan(otherNormal)) && !any(isinf(otherNormal)) && dot(normal, otherNormal) >= cos(0.001) &&
		abs(dot(delta, normal)) <= tolerance && abs(dot(delta, otherNormal)) <= tolerance;
}

bool MirrorPlane(ivec2 pixel, ivec2 extent, float receiver, float source, out vec3 sourceNormal)
{
	sourceNormal = vec3(0);
	if (any(lessThan(pixel, ivec2(1))) || any(greaterThanEqual(pixel, extent - 1))) return false;
	vec4 normal = texelFetch(Normal, pixel, 0);
	vec3 position = texelFetch(Position, pixel, 0).xyz;
	vec4 reflected = texelFetch(Reflected, pixel, 0);
	if (normal.w > 0.02 || reflected.w != 1.0 || texelFetch(Identity, pixel, 0).a != receiver ||
		texelFetch(RawSpecular, pixel, 0).a != source) return false;
	ivec2 offsets[4] = ivec2[](ivec2(-1, 0), ivec2(1, 0), ivec2(0, -1), ivec2(0, 1));
	vec3 points[4];
	for (int i = 0; i < 4; ++i)
	{
		ivec2 tap = pixel + offsets[i];
		vec4 otherNormal = texelFetch(Normal, tap, 0);
		vec4 otherReflected = texelFetch(Reflected, tap, 0);
		vec3 delta = texelFetch(Position, tap, 0).xyz - position;
		if (texelFetch(Identity, tap, 0).a != receiver || texelFetch(RawSpecular, tap, 0).a != source ||
			otherReflected.w != 1.0 || otherNormal.w > 0.02 || !MirrorReceiver(delta, normal.xyz, otherNormal.xyz)) return false;
		points[i] = otherReflected.xyz - reflected.xyz;
	}
	vec3 dx = points[1] - points[0], dy = points[3] - points[2];
	vec3 area = cross(dx, dy);
	if (any(isnan(area)) || any(isinf(area))) return false;
	if (dot(area, area) <= max(1e-20, 0.000001 * dot(dx, dx) * dot(dy, dy))) return false;
	sourceNormal = normalize(area);
	if (any(isnan(sourceNormal)) || any(isinf(sourceNormal))) return false;
	float tolerance = max(0.00001, min(0.002, 0.02 * max(length(dx), length(dy))));
	for (int i = 0; i < 4; ++i)
		if (any(isnan(points[i])) || any(isinf(points[i])) || abs(dot(points[i], sourceNormal)) > tolerance) return false;
	return true;
}

bool MirrorFootprint(vec3 position, vec3 normal, vec3 source, vec3 sourceNormal, out vec3 dx, out vec3 dy)
{
	vec3 virtualPoint = source - 2.0 * normal * dot(source - position, normal);
	vec3 virtualNormal = reflect(sourceNormal, normal);
	vec2 ndc = vec2(InUV.x * 2.0 - 1.0, 1.0 - InUV.y * 2.0);
	vec4 nearPoint = DIA.InverseViewProjection * vec4(ndc, 0, 1);
	vec4 farPoint = DIA.InverseViewProjection * vec4(ndc, 1, 1);
	vec3 origin = nearPoint.xyz / nearPoint.w, end = farPoint.xyz / farPoint.w;
	vec3 ray = end - origin;
	float denominator = dot(virtualNormal, ray);
	dx = dy = vec3(0);
	if (abs(denominator) <= 0.000001 * length(ray)) return false;
	float travel = dot(virtualPoint - origin, virtualNormal) / denominator;
	for (int axis = 0; axis < 2; ++axis)
	{
		vec4 differential = DIA.InverseViewProjection[axis] * (axis == 0 ? 2.0 / DIA.Extent.x : -2.0 / DIA.Extent.y);
		vec3 nearDelta = (differential.xyz - origin * differential.w) / nearPoint.w;
		vec3 farDelta = (differential.xyz - end * differential.w) / farPoint.w;
		vec3 offset = mix(nearDelta, farDelta, travel);
		vec3 tangent = reflect(offset - ray * (dot(virtualNormal, offset) / denominator), normal);
		if (any(isnan(tangent)) || any(isinf(tangent))) return false;
		if (axis == 0) dx = tangent;
		else dy = tangent;
	}
	return dot(cross(dx, dy), cross(dx, dy)) > max(1e-20, 0.000001 * dot(dx, dx) * dot(dy, dy));
}

vec3 MirrorFresnel(ivec2 pixel, vec3 f0)
{
	vec3 view = DIA.Camera.w > 0.5 ? -DIA.CameraForward.xyz : normalize(DIA.Camera.xyz - texelFetch(Position, pixel, 0).xyz);
	float cosine = clamp(dot(texelFetch(Normal, pixel, 0).xyz, view), 0.0, 1.0);
	return f0 + (1.0 - f0) * pow(1.0 - cosine, 5.0);
}

vec3 FilterMirror(ivec2 pixel, ivec2 extent, float receiver, vec3 color)
{
	float source = texelFetch(RawSpecular, pixel, 0).a;
	if (!(receiver >= 1.0 && receiver <= float(DIA.Counts.y) && source >= 1.0 && source <= float(DIA.Counts.y))) return color;
	LMaterial sourceMaterial = Materials[Instances[uint(source) - 1u].Metadata.x];
	if (sourceMaterial.Parameters.y < 0.5) return color;
	vec3 sourceNormal;
	if (!MirrorPlane(pixel, extent, receiver, source, sourceNormal)) return color;
	vec3 normal = texelFetch(Normal, pixel, 0).xyz;
	vec3 position = texelFetch(Position, pixel, 0).xyz;
	vec3 reflected = texelFetch(Reflected, pixel, 0).xyz;
	vec3 dx, dy;
	if (!MirrorFootprint(position, normal, reflected, sourceNormal, dx, dy)) return color;
	vec3 metric = vec3(dot(dx, dx), dot(dx, dy), dot(dy, dy));
	float determinant = metric.x * metric.z - metric.y * metric.y;
	if (determinant <= 0.0 || isnan(determinant) || isinf(determinant)) return color;
	float planeTolerance = max(0.00001, min(0.002, 0.02 * max(length(dx), length(dy))));
	LInstance instance = Instances[uint(receiver) - 1u];
	LMaterial material = Materials[instance.Metadata.x];
	vec3 f0 = mix(vec3(0.08 * clamp(material.Parameters.z, 0.0, 1.0)),
		material.BaseColor.rgb * instance.Tint.rgb, clamp(material.Parameters.x, 0.0, 1.0));
	vec3 fresnel = MirrorFresnel(pixel, f0);
	vec3 colors[9];
	float weights[9], variances[9], confidence[9];
	float sum = 0.0, squares = 0.0, total = 0.0;
	for (int i = 0; i < 9; ++i)
	{
		ivec2 offset = ivec2(i % 3 - 1, i / 3 - 1), tap = pixel + offset;
		weights[i] = 0.0;
		colors[i] = color;
		variances[i] = confidence[i] = 0.0;
		vec3 tapPlane;
		if (!MirrorPlane(tap, extent, receiver, source, tapPlane)) continue;
		vec3 tapNormal = texelFetch(Normal, tap, 0).xyz;
		vec3 receiverDelta = texelFetch(Position, tap, 0).xyz - position;
		vec3 delta = texelFetch(Reflected, tap, 0).xyz - reflected;
		vec2 projection = vec2(dot(delta, dx), dot(delta, dy));
		vec2 coordinate = vec2(metric.z * projection.x - metric.y * projection.y,
			metric.x * projection.y - metric.y * projection.x) / determinant;
		vec3 ratio = MirrorFresnel(tap, f0) / max(fresnel, vec3(1e-8));
		if (!MirrorReceiver(receiverDelta, normal, tapNormal) || abs(dot(sourceNormal, tapPlane)) < 0.995 ||
			abs(dot(delta, sourceNormal)) > planeTolerance || abs(dot(delta, tapPlane)) > planeTolerance ||
			dot(coordinate, coordinate) > 2.25 || any(lessThan(ratio, vec3(1.0 / 1.02))) || any(greaterThan(ratio, vec3(1.02)))) continue;
		colors[i] = texelFetch(InputSpecular, tap, 0).rgb;
		if (any(isnan(colors[i])) || any(isinf(colors[i]))) continue;
		weights[i] = Kernel[abs(offset.x)] * Kernel[abs(offset.y)];
		float light = Luma(colors[i]);
		vec4 counts = texelFetch(Counts, tap, 0);
		variances[i] = max(counts.w - light * light, 0.0) / max(counts.y, 1.0);
		confidence[i] = clamp((counts.y - 1.0) / 3.0, 0.0, 1.0);
		sum += weights[i] * light;
		squares += weights[i] * light * light;
		total += weights[i];
	}
	if (total <= weights[4]) return color;
	float spatial = max(squares / total - (sum / total) * (sum / total), 0.0);
	float centerVariance = mix(spatial, variances[4], confidence[4]);
	vec3 filtered = vec3(0);
	float weightSum = 0.0;
	for (int i = 0; i < 9; ++i)
	{
		float variance = mix(spatial, variances[i], confidence[i]);
		float sigma = LumaSigma * sqrt(max(centerVariance + variance, 0.0)) + 0.000001;
		float weight = weights[i] * exp(-abs(Luma(colors[i]) - Luma(color)) / sigma);
		filtered += colors[i] * weight;
		weightSum += weight;
	}
	return weightSum > 0.0 ? filtered / weightSum : color;
}

void main()
{
	ivec2 extent = ivec2(DIA.Extent.xy);
	ivec2 pixel = ivec2(clamp(InUV, vec2(0), vec2(0.999999)) * DIA.Extent.xy);
	int stepWidth = max(1, int(DIA.Emitters.z + 0.5));
	bool first = DIA.Emitters.w < 0.5;
	vec4 diffuse = texelFetch(InputDiffuse, pixel, 0);
	vec4 specular = texelFetch(InputSpecular, pixel, 0);
	float identity = texelFetch(Identity, pixel, 0).a;
	OutDiffuse = vec4(diffuse.rgb, first ? 0.0 : diffuse.a);
	OutSpecular = vec4(specular.rgb, first ? 0.0 : specular.a);
	if (DIA.Settings.w >= 4.0 || identity <= 0.0)
	{
		OutDiffuse.a = 0.0;
		OutSpecular.a = 0.0;
		return;
	}
	bool preserveDiffuse = !first && Boundary(pixel, identity, extent);
	vec4 position = texelFetch(Position, pixel, 0);
	vec4 normal = texelFetch(Normal, pixel, 0);
	vec4 counts = texelFetch(Counts, pixel, 0);
	float reflectedKind = texelFetch(Reflected, pixel, 0).w;
	float roughness = normal.w;
	if (DIA.Settings.w == 0.0 && roughness <= 0.02 && identity >= 1.0 && identity <= float(DIA.Counts.y))
	{
		if (first) OutSpecular.rgb = FilterMirror(pixel, extent, identity, specular.rgb);
		if (Materials[Instances[uint(identity) - 1u].Metadata.x].Parameters.x >= 1.0)
		{
			OutDiffuse.a = OutSpecular.a = 0.0;
			return;
		}
	}
	float lobe = 1.5 * roughness * roughness;
	bool filterSpecular = roughness > 0.02 && reflectedKind >= 0.0;
	if (!filterSpecular) OutSpecular.a = 0.0;
	vec2 luma = vec2(Luma(diffuse.rgb), Luma(specular.rgb));
	vec2 variance = vec2(diffuse.a, specular.a);
	if (first)
	{
		vec2 sampleVariance = vec2(0), reliableWeight = vec2(0);
		vec2 spatialWeight = vec2(0), spatialSum = vec2(0), spatialSquares = vec2(0), taps = vec2(0);
		for (int y = -2; y <= 2; ++y) for (int x = -2; x <= 2; ++x)
		{
			ivec2 tap = pixel + ivec2(x, y);
			if (any(lessThan(tap, ivec2(0))) || any(greaterThanEqual(tap, extent))) continue;
			if (texelFetch(Identity, tap, 0).a != identity) continue;
			vec4 tapNormal = texelFetch(Normal, tap, 0);
			float geometry = GeometryWeight(position.xyz, normal.xyz, texelFetch(Position, tap, 0).xyz, tapNormal.xyz);
			vec2 weight = vec2(geometry, 0.0);
			if (filterSpecular && abs(tapNormal.w - roughness) < 0.02 && texelFetch(Reflected, tap, 0).w >= 0.0)
				weight.y = geometry * LobeWeight(normal.xyz, tapNormal.xyz, lobe);
			vec4 tapCounts = texelFetch(Counts, tap, 0);
			vec2 tapLuma = vec2(Luma(texelFetch(InputDiffuse, tap, 0).rgb), Luma(texelFetch(InputSpecular, tap, 0).rgb));
			if (any(isnan(tapLuma)) || any(isinf(tapLuma))) continue;
			spatialWeight += weight;
			spatialSum += weight * tapLuma;
			spatialSquares += weight * tapLuma * tapLuma;
			taps += vec2(greaterThan(weight, vec2(0.5)));
			vec2 reliability = weight * clamp((tapCounts.xy - 1.0) / 3.0, vec2(0), vec2(1));
			sampleVariance += reliability * max(tapCounts.zw - tapLuma * tapLuma, vec2(0));
			reliableWeight += reliability;
		}
		vec2 spatialMean = spatialSum / max(spatialWeight, vec2(0.000001));
		vec2 spatial = max(spatialSquares / max(spatialWeight, vec2(0.000001)) - spatialMean * spatialMean, vec2(0));
		spatial = mix(spatial, max(spatial, luma * luma), vec2(lessThan(taps, vec2(4))));
		vec2 temporal = sampleVariance / max(reliableWeight, vec2(0.000001)) / max(counts.xy, vec2(1));
		vec2 confidence = clamp((counts.xy - 1.0) / 3.0, vec2(0), vec2(1)) * clamp(reliableWeight, vec2(0), vec2(1));
		variance = mix(spatial, temporal, confidence);
	}
	float hitDistance = reflectedKind == 1.0 ? position.w : DIA.Settings.x;
	float radius = hitDistance * lobe / max(Footprint(position.xyz, normal.xyz), 0.000001) + 1.0;
	vec3 diffuseSum = vec3(0), specularSum = vec3(0);
	vec2 weightSum = vec2(0), varianceSum = vec2(0);
	for (int y = -2; y <= 2; ++y) for (int x = -2; x <= 2; ++x)
	{
		ivec2 tap = pixel + ivec2(x, y) * stepWidth;
		if (any(lessThan(tap, ivec2(0))) || any(greaterThanEqual(tap, extent))) continue;
		bool center = x == 0 && y == 0;
		if (!center && texelFetch(Identity, tap, 0).a != identity) continue;
		vec4 tapDiffuse = center ? diffuse : texelFetch(InputDiffuse, tap, 0);
		vec4 tapSpecular = center ? specular : texelFetch(InputSpecular, tap, 0);
		if (!center && (any(isnan(tapDiffuse.rgb)) || any(isinf(tapDiffuse.rgb)) || any(isnan(tapSpecular.rgb)) || any(isinf(tapSpecular.rgb)))) continue;
		vec4 tapNormal = center ? normal : texelFetch(Normal, tap, 0);
		float kernel = Kernel[abs(x)] * Kernel[abs(y)];
		float geometry = center ? 1.0 : GeometryWeight(position.xyz, normal.xyz, texelFetch(Position, tap, 0).xyz, tapNormal.xyz);
		vec2 tapVariance = first ? variance : vec2(tapDiffuse.a, tapSpecular.a);
		vec2 sigma = LumaSigma * sqrt(max(variance + tapVariance, vec2(0))) + 0.000001;
		float diffuseWeight = kernel * geometry * exp(-abs(Luma(tapDiffuse.rgb) - luma.x) / sigma.x);
		if (!first && !center && Boundary(tap, identity, extent)) diffuseWeight = 0.0;
		diffuseSum += tapDiffuse.rgb * diffuseWeight;
		weightSum.x += diffuseWeight;
		varianceSum.x += diffuseWeight * diffuseWeight * tapVariance.x;
		if (filterSpecular && abs(tapNormal.w - roughness) < 0.02 && (center || texelFetch(Reflected, tap, 0).w >= 0.0))
		{
			float distance2 = float(x * x + y * y) * float(stepWidth * stepWidth);
			float specularWeight = kernel * geometry * LobeWeight(normal.xyz, tapNormal.xyz, lobe) *
				exp(-distance2 / (2.0 * radius * radius)) * exp(-abs(Luma(tapSpecular.rgb) - luma.y) / sigma.y);
			specularSum += tapSpecular.rgb * specularWeight;
			weightSum.y += specularWeight;
			varianceSum.y += specularWeight * specularWeight * tapVariance.y;
		}
	}
	if (!preserveDiffuse) OutDiffuse = vec4(diffuseSum / weightSum.x, varianceSum.x / (weightSum.x * weightSum.x));
	if (filterSpecular) OutSpecular = vec4(specularSum / weightSum.y, varianceSum.y / (weightSum.y * weightSum.y));
}
