#version 450
#extension GL_GOOGLE_include_directive : require
#include "Common.glsl"
#include "Lighting.glsl"
#include "CelShading.glsl"
#define FP_ENVIRONMENT_BINDING 16
#define FP_ENVIRONMENT_LIGHTING
#include "Environment.glsl"

// Base PBR shader. Metallic and roughness workflow with specular, emissive and subsurface.

layout(set = 2, binding = 0) uniform sampler2D BaseColorMap;
layout(set = 2, binding = 1) uniform sampler2D NormalMap;
layout(set = 2, binding = 2) uniform sampler2D AmbientOcclusionMap;
layout(set = 2, binding = 3) uniform sampler2D MetallicMap;
layout(set = 2, binding = 4) uniform sampler2D RoughnessMap;
layout(set = 2, binding = 5) uniform sampler2D SpecularMap;
layout(set = 2, binding = 6) uniform sampler2D EmissiveMap;
layout(set = 2, binding = 7) uniform sampler2D SubsurfaceMap;
layout(set = 2, binding = 8) uniform sampler2DShadow ShadowMap0;
layout(set = 2, binding = 9) uniform sampler2DShadow ShadowMap1;
layout(set = 2, binding = 10) uniform sampler2DShadow ShadowMap2;
layout(set = 2, binding = 11) uniform sampler2DShadow ShadowMap3;
layout(set = 2, binding = 12) uniform sampler2DShadow ShadowMap4;
layout(set = 2, binding = 13) uniform sampler2DShadow ShadowMap5;
layout(set = 2, binding = 14) uniform sampler2DShadow ShadowMap6;
layout(set = 2, binding = 15) uniform sampler2DShadow ShadowMap7;

layout(location = 0) in vec3 InWorldPosition;
layout(location = 1) in vec2 InUV;
layout(location = 2) in vec3 InNormal;
layout(location = 3) in vec4 InTangent;
layout(location = 4) in vec4 InTint;

layout(location = 0) out vec4 OutColor;

float CompareShadow(int slot, vec3 coordinate)
{
	switch (slot)
	{
	case 0: return texture(ShadowMap0, coordinate);
	case 1: return texture(ShadowMap1, coordinate);
	case 2: return texture(ShadowMap2, coordinate);
	case 3: return texture(ShadowMap3, coordinate);
	case 4: return texture(ShadowMap4, coordinate);
	case 5: return texture(ShadowMap5, coordinate);
	case 6: return texture(ShadowMap6, coordinate);
	default: return texture(ShadowMap7, coordinate);
	}
}

// Returns the fraction of this light that reaches the surface.
float ShadowVisibility(int type, vec3 worldPosition, vec3 normal)
{
	vec4 params = Frame.ShadowParams[type];
	if (params.x <= 0.0)
		return 1.0;
	int slot = type;
	if (type == 2)
	{
		vec3 delta = worldPosition - Frame.LightPosition[int(params.w)].xyz;
		vec3 a = abs(delta);
		if (a.x >= a.y && a.x >= a.z)
			slot = delta.x >= 0.0 ? 2 : 3;
		else if (a.y >= a.z)
			slot = delta.y >= 0.0 ? 4 : 5;
		else
			slot = delta.z >= 0.0 ? 6 : 7;
	}
	vec4 projected = Frame.ShadowMatrix[slot] * vec4(worldPosition + normal * params.z, 1.0);
	if (projected.w <= 0.0)
		return 1.0;
	vec3 s = projected.xyz / projected.w;
	if (s.x <= 0.0 || s.x >= 1.0 || s.y <= 0.0 || s.y >= 1.0 || s.z <= 0.0 || s.z >= 1.0)
		return 1.0;
	float t = params.y;
	float lit = CompareShadow(slot, vec3(s.xy + vec2(-t, -t), s.z))
		+ CompareShadow(slot, vec3(s.xy + vec2(t, -t), s.z))
		+ CompareShadow(slot, vec3(s.xy + vec2(-t, t), s.z))
		+ CompareShadow(slot, vec3(s.xy + vec2(t, t), s.z));
	return mix(1.0, lit * 0.25, params.x);
}

void main()
{
	vec4 base = texture(BaseColorMap, InUV) * Material.BaseColor * InTint;
	if (base.a < Material.Params2.y)
		discard;

	vec3 n = ApplyNormalMap(InNormal, InTangent, texture(NormalMap, InUV).xyz, Material.Params.w);
	vec3 v = ViewDirection(InWorldPosition);
	// Two sided so sprites and planes light the same from both faces.
	vec3 faceNormal = normalize(InNormal);
	if (dot(InNormal, v) < 0.0)
	{
		n = -n;
		faceNormal = -faceNormal;
	}

	float metallic = clamp(Material.Params.x * texture(MetallicMap, InUV).r, 0.0, 1.0);
	float roughness = clamp(Material.Params.y * texture(RoughnessMap, InUV).r, 0.045, 1.0);
	float specular = Material.Params.z * texture(SpecularMap, InUV).r;
	float occlusion = mix(1.0, texture(AmbientOcclusionMap, InUV).r, Material.Params2.x);
	float subsurface = Material.Subsurface.a * texture(SubsurfaceMap, InUV).r;

	// Dielectrics reflect about 4 percent at the default specular of 0.5.
	vec3 f0 = mix(vec3(0.08 * specular), base.rgb, metallic);
	vec3 diffuseColor = base.rgb * (1.0 - metallic);
	float nDotV = max(dot(n, v), 1e-4);

	vec3 diffuseLighting = vec3(0.0);
	vec3 specularLighting = vec3(0.0);
	for (int i = 0; i < Frame.LightCount.x; ++i)
	{
		vec3 l;
		vec3 radiance;
		GetLight(i, InWorldPosition, l, radiance);
		for (int type = 0; type < 3; ++type)
			if (Frame.ShadowParams[type].x > 0.0 && i == int(Frame.ShadowParams[type].w))
				radiance *= ShadowVisibility(type, InWorldPosition, faceNormal);

		vec3 h = normalize(v + l);
		float nDotL = max(dot(n, l), 0.0);
		float nDotH = max(dot(n, h), 0.0);
		vec3 f = FresnelSchlick(max(dot(h, v), 0.0), f0);

		vec3 spec = DistributionGGX(nDotH, roughness) * VisibilitySmith(nDotV, nDotL, roughness) * f * PI;
		diffuseLighting += (1.0 - f) * radiance * nDotL;
		specularLighting += spec * radiance * nDotL;

		// Subsurface: light wraps past the terminator and shines through thin parts from behind.
		if (subsurface > 0.0)
		{
			float wrapped = clamp((dot(n, l) + 0.5) / 1.5, 0.0, 1.0);
			float through = pow(clamp(dot(v, -normalize(l + n * 0.4)), 0.0, 1.0), 4.0);
			diffuseLighting += Material.Subsurface.rgb * radiance * subsurface * (max(wrapped - nDotL, 0.0) + through);
		}
	}

	diffuseLighting += Frame.Ambient.rgb * occlusion;
	specularLighting += Frame.Ambient.rgb * EnvironmentBRDF(f0, roughness, nDotV) * occlusion;
	if (Frame.EnvironmentSize.z > 0.0)
	{
		vec3 diffuse = EnvironmentDiffuse(n) * (1.0 - f0);
		vec3 specularLight = EnvironmentReflection(n, v, roughness) * EnvironmentBRDF(f0, roughness, nDotV);
		diffuseLighting += diffuse * occlusion;
		specularLighting += specularLight * occlusion;
	}
	if (IsCelShaded())
	{
		diffuseLighting = CelBands(diffuseLighting);
		specularLighting = CelBands(specularLighting);
	}
	vec3 color = diffuseColor * diffuseLighting + specularLighting;
	if (IsCelShaded())
		color *= CelEdge(max(dot(faceNormal, v), 0.0));
	color += texture(EmissiveMap, InUV).rgb * Material.Emissive.rgb;
	OutColor = vec4(color, base.a);
}
