#version 450
#extension GL_GOOGLE_include_directive : require
#include "Common.glsl"
#include "Lighting.glsl"

// Base PBR shader. Metallic and roughness workflow with specular, emissive and subsurface.

layout(set = 2, binding = 0) uniform sampler2D BaseColorMap;
layout(set = 2, binding = 1) uniform sampler2D NormalMap;
layout(set = 2, binding = 2) uniform sampler2D AmbientOcclusionMap;
layout(set = 2, binding = 3) uniform sampler2D MetallicMap;
layout(set = 2, binding = 4) uniform sampler2D RoughnessMap;
layout(set = 2, binding = 5) uniform sampler2D SpecularMap;
layout(set = 2, binding = 6) uniform sampler2D EmissiveMap;
layout(set = 2, binding = 7) uniform sampler2D SubsurfaceMap;

layout(location = 0) in vec3 InWorldPosition;
layout(location = 1) in vec2 InUV;
layout(location = 2) in vec3 InNormal;
layout(location = 3) in vec4 InTangent;
layout(location = 4) in vec4 InTint;

layout(location = 0) out vec4 OutColor;

void main()
{
	vec4 base = texture(BaseColorMap, InUV) * Material.BaseColor * InTint;
	if (base.a < Material.Params2.y)
		discard;

	vec3 n = ApplyNormalMap(InNormal, InTangent, texture(NormalMap, InUV).xyz, Material.Params.w);
	vec3 v = ViewDirection(InWorldPosition);
	// Two sided so sprites and planes light the same from both faces.
	if (dot(InNormal, v) < 0.0)
		n = -n;

	float metallic = clamp(Material.Params.x * texture(MetallicMap, InUV).r, 0.0, 1.0);
	float roughness = clamp(Material.Params.y * texture(RoughnessMap, InUV).r, 0.045, 1.0);
	float specular = Material.Params.z * texture(SpecularMap, InUV).r;
	float occlusion = mix(1.0, texture(AmbientOcclusionMap, InUV).r, Material.Params2.x);
	float subsurface = Material.Subsurface.a * texture(SubsurfaceMap, InUV).r;

	// Dielectrics reflect about 4 percent at the default specular of 0.5.
	vec3 f0 = mix(vec3(0.08 * specular), base.rgb, metallic);
	vec3 diffuseColor = base.rgb * (1.0 - metallic);
	float nDotV = max(dot(n, v), 1e-4);

	vec3 color = vec3(0.0);
	for (int i = 0; i < Frame.LightCount.x; ++i)
	{
		vec3 l;
		vec3 radiance;
		GetLight(i, InWorldPosition, l, radiance);

		vec3 h = normalize(v + l);
		float nDotL = max(dot(n, l), 0.0);
		float nDotH = max(dot(n, h), 0.0);
		vec3 f = FresnelSchlick(max(dot(h, v), 0.0), f0);

		vec3 diffuse = (1.0 - f) * diffuseColor;
		vec3 spec = DistributionGGX(nDotH, roughness) * VisibilitySmith(nDotV, nDotL, roughness) * f * PI;
		color += (diffuse + spec) * radiance * nDotL;

		// Subsurface: light wraps past the terminator and shines through thin parts from behind.
		if (subsurface > 0.0)
		{
			float wrapped = clamp((dot(n, l) + 0.5) / 1.5, 0.0, 1.0);
			float through = pow(clamp(dot(v, -normalize(l + n * 0.4)), 0.0, 1.0), 4.0);
			color += Material.Subsurface.rgb * diffuseColor * radiance * subsurface * (max(wrapped - nDotL, 0.0) + through);
		}
	}

	color += Frame.Ambient.rgb * (diffuseColor + EnvironmentBRDF(f0, roughness, nDotV)) * occlusion;
	color += texture(EmissiveMap, InUV).rgb * Material.Emissive.rgb;
	OutColor = vec4(color, base.a);
}
