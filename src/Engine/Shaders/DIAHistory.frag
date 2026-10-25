#version 450
#extension GL_GOOGLE_include_directive : require
#define DIA_MATERIAL_BINDING 11
#include "DIAShared.glsl"
layout(set = 2, binding = 0) uniform sampler2D RawDiffuse;
layout(set = 2, binding = 1) uniform sampler2D RawSpecular;
layout(set = 2, binding = 2) uniform sampler2D RawPosition;
layout(set = 2, binding = 3) uniform sampler2D RawNormal;
layout(set = 2, binding = 4) uniform sampler2D OldDiffuse;
layout(set = 2, binding = 5) uniform sampler2D OldSpecular;
layout(set = 2, binding = 6) uniform sampler2D OldPosition;
layout(set = 2, binding = 7) uniform sampler2D OldNormal;
layout(set = 2, binding = 8) uniform sampler2D RawReflected;
layout(set = 2, binding = 9) uniform sampler2D OldReflected;
layout(set = 2, binding = 10) uniform sampler2D OldCounts;
layout(location = 0) in vec2 InUV;
layout(location = 0) out vec4 OutDiffuse;
layout(location = 1) out vec4 OutSpecular;
layout(location = 2) out vec4 OutPosition;
layout(location = 3) out vec4 OutNormal;
layout(location = 4) out vec4 OutReflected;
layout(location = 5) out vec4 OutCounts;

const float HistoryLimit = 400.0;
const vec3 LumaWeights = vec3(0.2126, 0.7152, 0.0722);
const float FresnelFloor = 0.02 / 0.98;
const float FresnelLag = 0.05;
const float SourceLobe = 0.05;
const float BlurBudget = 2.0;
const float BlurCap = 8.0;

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
			vec3 tangent = offset - ray * (dot(normal, offset) / planeDenominator);
			footprint = max(footprint, length(tangent));
		}
	}
	return footprint;
}

void MirrorHistory()
{
	bool orthographic = DIA.Camera.w > 0.5;
	vec3 position = OutPosition.xyz, normal = OutNormal.xyz;
	vec3 ray = orthographic ? DIA.CameraForward.xyz : normalize(position - DIA.Camera.xyz);
	vec3 image = position + ray * distance(OutReflected.xyz, position);
	vec3 view = orthographic ? DIA.CameraForward.xyz : normalize(image - DIA.Camera.xyz);
	vec3 previousView = orthographic ? DIA.PreviousForward.xyz : normalize(image - DIA.PreviousCamera.xyz);
	float drift = length(view - previousView);
	vec2 grazing = 1.0 - clamp(vec2(dot(normal, -view), dot(normal, -previousView)), 0.0, 1.0);
	vec2 fresnel = FresnelFloor + pow(grazing, vec2(5.0));
	uint source = uint(OutSpecular.a);
	float sourceRoughness = 0.0;
	if (DIA.Settings.w == 0.0 && source >= 1u && source <= DIA.Counts.y)
		sourceRoughness = clamp(Materials[Instances[source - 1u].Metadata.x].Parameters.y, 0.0, 1.0);
	float sourceLobe = max(SourceLobe, 1.5 * sourceRoughness * sourceRoughness);
	float cap = min(sourceLobe / max(drift, 0.000001), FresnelLag / max(abs(log(fresnel.x / fresnel.y)), 0.000001));
	// With an unchanged direction the previous ray to the image also passes through the mirror point.
	vec4 previousClip = DIA.PreviousViewProjection * vec4(drift == 0.0 ? position : image, 1);
	if (previousClip.w <= 0.0) return;
	vec2 uv = vec2(previousClip.x / previousClip.w * 0.5 + 0.5, 0.5 - previousClip.y / previousClip.w * 0.5);
	if (any(lessThan(uv, vec2(0))) || any(greaterThanEqual(uv, vec2(1)))) return;
	vec2 oldCoordinate = uv * DIA.Extent.xy - 0.5;
	oldCoordinate = mix(oldCoordinate, round(oldCoordinate), lessThan(abs(oldCoordinate - round(oldCoordinate)), vec2(0.0001)));
	ivec2 oldBase = ivec2(floor(oldCoordinate));
	vec2 blend = fract(oldCoordinate);
	cap = min(cap, min(BlurBudget * sourceLobe / SourceLobe, BlurCap) / max(dot(blend, 1.0 - blend), 0.000001));
	float tolerance = 1.5 * Footprint(image, view);
	if (!orthographic) tolerance *= max(1.0, distance(image, DIA.PreviousCamera.xyz) / distance(image, DIA.Camera.xyz));
	vec3 historySpecular = vec3(0);
	float historyWeight = 0.0, historyReciprocalCount = 0.0, historyMoment = 0.0;
	for (int y = 0; y < 2; ++y) for (int x = 0; x < 2; ++x)
	{
		float weight = (x == 0 ? 1.0 - blend.x : blend.x) * (y == 0 ? 1.0 - blend.y : blend.y);
		if (weight < 0.0001) continue;
		ivec2 oldPixel = oldBase + ivec2(x, y);
		if (any(lessThan(oldPixel, ivec2(0))) || any(greaterThanEqual(oldPixel, ivec2(DIA.Extent.xy)))) continue;
		vec4 oldSpecular = texelFetch(OldSpecular, oldPixel, 0);
		vec4 oldPosition = texelFetch(OldPosition, oldPixel, 0);
		vec4 oldNormal = texelFetch(OldNormal, oldPixel, 0);
		vec4 oldReflected = texelFetch(OldReflected, oldPixel, 0);
		vec3 delta = oldPosition.xyz - position;
		float normalDot = dot(oldNormal.xyz, normal);
		float planeLimit = max(0.001, length(delta) * sqrt(max(0.0, 1.0 - normalDot * normalDot)));
		if (texelFetch(OldDiffuse, oldPixel, 0).a != OutDiffuse.a || oldSpecular.a != OutSpecular.a || oldReflected.w != 1.0 ||
			oldNormal.w > 0.02 || abs(oldNormal.w - OutNormal.w) >= 0.01 || normalDot < cos(0.001) ||
			abs(dot(delta, normal)) > planeLimit || abs(dot(delta, oldNormal.xyz)) > planeLimit ||
			distance(oldReflected.xyz, OutReflected.xyz) > tolerance) continue;
		vec4 oldCounts = texelFetch(OldCounts, oldPixel, 0);
		historySpecular += oldSpecular.rgb * weight;
		historyMoment += oldCounts.w * weight;
		historyWeight += weight;
		historyReciprocalCount += weight / max(min(clamp(oldCounts.y, 0.0, HistoryLimit), cap), 0.01);
	}
	if (historyWeight <= 0.0) return;
	float count = min(HistoryLimit, historyWeight * min(historyWeight, 1.0) / historyReciprocalCount);
	float retained = count / (count + 1.0);
	OutSpecular.rgb = mix(OutSpecular.rgb, historySpecular / historyWeight, retained);
	OutCounts.y = min(count + 1.0, HistoryLimit);
	OutCounts.w = mix(OutCounts.w, historyMoment / historyWeight, retained);
}

void main()
{
	ivec2 pixel = ivec2(clamp(InUV, vec2(0), vec2(0.999999)) * DIA.Extent.xy);
	OutDiffuse = texelFetch(RawDiffuse, pixel, 0);
	OutSpecular = texelFetch(RawSpecular, pixel, 0);
	OutPosition = texelFetch(RawPosition, pixel, 0);
	OutNormal = texelFetch(RawNormal, pixel, 0);
	OutReflected = texelFetch(RawReflected, pixel, 0);
	float diffuseLuma = dot(OutDiffuse.rgb, LumaWeights);
	float specularLuma = dot(OutSpecular.rgb, LumaWeights);
	OutCounts = OutDiffuse.a == 0.0 ? vec4(0) : vec4(1, 1, diffuseLuma * diffuseLuma, specularLuma * specularLuma);
	if (DIA.Settings.w >= 4.0 || OutDiffuse.a == 0.0 || DIA.History.x < 0.5) return;
	float roughness = OutNormal.w;
	bool virtualImage = roughness <= 0.02 && OutDiffuse.a > 0.0 && OutSpecular.a > 0.0 && OutReflected.w == 1.0;
	if (virtualImage) MirrorHistory();
	vec4 previousClip = DIA.PreviousViewProjection * vec4(OutPosition.xyz, 1);
	if (previousClip.w <= 0.0) return;
	vec2 uv = vec2(previousClip.x / previousClip.w * 0.5 + 0.5, 0.5 - previousClip.y / previousClip.w * 0.5);
	if (any(lessThan(uv, vec2(0))) || any(greaterThanEqual(uv, vec2(1)))) return;
	float footprint = Footprint(OutPosition.xyz, OutNormal.xyz);
	vec2 oldCoordinate = uv * DIA.Extent.xy - 0.5;
	oldCoordinate = mix(oldCoordinate, round(oldCoordinate), lessThan(abs(oldCoordinate - round(oldCoordinate)), vec2(0.0001)));
	ivec2 oldBase = ivec2(floor(oldCoordinate));
	vec2 blend = fract(oldCoordinate);
	vec3 viewDirection = DIA.Camera.w > 0.5 ? DIA.CameraForward.xyz : normalize(OutPosition.xyz - DIA.Camera.xyz);
	vec3 previousSurfaceView = DIA.Camera.w > 0.5 ? DIA.PreviousForward.xyz : normalize(OutPosition.xyz - DIA.PreviousCamera.xyz);
	vec3 viewDrift = viewDirection - previousSurfaceView;
	float lobe = 1.5 * roughness * roughness;
	vec3 historyDiffuse = vec3(0), historySpecular = vec3(0);
	vec3 historyNormal = vec3(0);
	vec2 historyWeight = vec2(0), historyReciprocalCount = vec2(0), historyMoment = vec2(0);
	for (int y = 0; y < 2; ++y) for (int x = 0; x < 2; ++x)
	{
		float weight = (x == 0 ? 1.0 - blend.x : blend.x) * (y == 0 ? 1.0 - blend.y : blend.y);
		if (weight < 0.0001) continue;
		ivec2 oldPixel = oldBase + ivec2(x, y);
		if (any(lessThan(oldPixel, ivec2(0))) || any(greaterThanEqual(oldPixel, ivec2(DIA.Extent.xy)))) continue;
		vec4 oldPosition = texelFetch(OldPosition, oldPixel, 0);
		vec4 oldNormal = texelFetch(OldNormal, oldPixel, 0);
		vec4 oldDiffuse = texelFetch(OldDiffuse, oldPixel, 0);
		float normalDot = dot(oldNormal.xyz, OutNormal.xyz);
		vec3 delta = oldPosition.xyz - OutPosition.xyz;
		if (OutDiffuse.a < 0.0 && length(delta) > 0.00001) continue;
		float planeDistance = dot(delta, OutNormal.xyz);
		float planeLimit = max(0.001, footprint * sqrt(max(0.0, 1.0 - normalDot * normalDot)));
		if (oldDiffuse.a != OutDiffuse.a || normalDot < 0.97 || abs(planeDistance) > planeLimit ||
			abs(dot(delta, oldNormal.xyz)) > planeLimit || length(delta - OutNormal.xyz * planeDistance) > max(0.005, footprint * 1.5)) continue;
		vec4 oldCounts = texelFetch(OldCounts, oldPixel, 0);
		vec2 counts = clamp(oldCounts.xy, vec2(0), vec2(HistoryLimit));
		historyDiffuse += oldDiffuse.rgb * weight;
		historyMoment.x += oldCounts.z * weight;
		historyWeight.x += weight;
		historyReciprocalCount.x += weight / max(counts.x, 1.0);
		if (virtualImage) continue;
		vec4 oldSpecular = texelFetch(OldSpecular, oldPixel, 0);
		vec4 oldReflected = texelFetch(OldReflected, oldPixel, 0);
		vec3 previousDirection = DIA.Camera.w > 0.5 ? DIA.PreviousForward.xyz : normalize(oldPosition.xyz - DIA.PreviousCamera.xyz);
		bool compatible = abs(oldNormal.w - roughness) < 0.01 && OutReflected.w >= 0.0 && oldReflected.w >= 0.0;
		float specularCount = max(counts.y, 1.0);
		if (roughness <= 0.02)
			compatible = compatible && normalDot >= cos(max(0.001, roughness * roughness * 0.5)) &&
				dot(viewDirection, previousDirection) >= cos(max(0.001, roughness * roughness * 0.1)) &&
				OutSpecular.a != 0.0 && oldSpecular.a == OutSpecular.a && OutReflected.w == oldReflected.w &&
				length(OutReflected.xyz - oldReflected.xyz) <= 0.005 && abs(oldPosition.w - OutPosition.w) <= 0.005;
		else
		{
			if (OutReflected.w == 1.0)
				compatible = compatible && abs(OutSpecular.a) >= 1.0 && abs(OutSpecular.a) <= float(DIA.Counts.y) &&
					abs(length(OutReflected.xyz - OutPosition.xyz) - OutPosition.w) <= max(0.001, OutPosition.w * 0.001);
			if (oldReflected.w == 1.0)
				compatible = compatible && abs(oldSpecular.a) >= 1.0 && abs(oldSpecular.a) <= float(DIA.Counts.y) &&
					abs(length(oldReflected.xyz - oldPosition.xyz) - oldPosition.w) <= max(0.001, oldPosition.w * 0.001);
			vec3 normalDelta = OutNormal.xyz - oldNormal.xyz;
			float drift = sqrt(dot(viewDrift, viewDrift) + 4.0 * dot(normalDelta, normalDelta));
			compatible = compatible && drift <= 2.0 * lobe;
		}
		if (!compatible) continue;
		if (roughness > 0.02) historyNormal += oldNormal.xyz * weight;
		historySpecular += oldSpecular.rgb * weight;
		historyMoment.y += oldCounts.w * weight;
		historyWeight.y += weight;
		historyReciprocalCount.y += weight / max(specularCount, 0.01);
	}
	if (historyWeight.x > 0.0)
	{
		float count = min(HistoryLimit, historyWeight.x * min(historyWeight.x, 1.0) / historyReciprocalCount.x);
		float retained = count / (count + 1.0);
		OutDiffuse.rgb = mix(OutDiffuse.rgb, historyDiffuse / historyWeight.x, retained);
		OutCounts.x = min(count + 1.0, HistoryLimit);
		OutCounts.z = mix(OutCounts.z, historyMoment.x / historyWeight.x, retained);
	}
	if (historyWeight.y > 0.0)
	{
		float count = min(HistoryLimit, historyWeight.y * min(historyWeight.y, 1.0) / historyReciprocalCount.y);
		if (roughness > 0.02)
		{
			vec3 normalDelta = OutNormal.xyz - normalize(historyNormal);
			float drift = sqrt(dot(viewDrift, viewDrift) + 4.0 * dot(normalDelta, normalDelta));
			count = min(count, lobe / max(drift, 0.000001));
		}
		float retained = count / (count + 1.0);
		OutSpecular.rgb = mix(OutSpecular.rgb, historySpecular / historyWeight.y, retained);
		OutCounts.y = min(count + 1.0, HistoryLimit);
		OutCounts.w = mix(OutCounts.w, historyMoment.y / historyWeight.y, retained);
	}
}
