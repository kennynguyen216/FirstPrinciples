#version 450
#extension GL_GOOGLE_include_directive : require
#include "Common.glsl"
#include "Lighting.glsl"

// Transparent glass. Bends the already drawn scene through the surface normal, tints it,
// blurs it with roughness and adds fresnel reflection and highlights.

layout(set = 2, binding = 0) uniform sampler2D BaseColorMap;
layout(set = 2, binding = 1) uniform sampler2D NormalMap;
layout(set = 2, binding = 2) uniform sampler2D RoughnessMap;
layout(set = 2, binding = 3) uniform sampler2D SceneColor;

layout(location = 0) in vec3 InWorldPosition;
layout(location = 1) in vec2 InUV;
layout(location = 2) in vec3 InNormal;
layout(location = 3) in vec4 InTangent;
layout(location = 4) in vec4 InTint;

layout(location = 0) out vec4 OutColor;

const vec2 BlurTaps[8] = vec2[](
	vec2(0.7071, 0.7071), vec2(-0.7071, 0.7071), vec2(0.7071, -0.7071), vec2(-0.7071, -0.7071),
	vec2(1.0, 0.0), vec2(-1.0, 0.0), vec2(0.0, 1.0), vec2(0.0, -1.0));

void main()
{
	vec4 tint = texture(BaseColorMap, InUV) * Material.BaseColor * InTint;
	vec3 geometryNormal = normalize(InNormal);
	vec3 n = ApplyNormalMap(InNormal, InTangent, texture(NormalMap, InUV).xyz, Material.Params.w);
	vec3 v = ViewDirection(InWorldPosition);
	if (dot(geometryNormal, v) < 0.0)
	{
		geometryNormal = -geometryNormal;
		n = -n;
	}
	float roughness = clamp(Material.Params.y * texture(RoughnessMap, InUV).r, 0.045, 1.0);
	float ior = max(Material.Params2.z, 1.0);
	float thickness = Material.Params2.w;

	// Screen space refraction. The image moves by how far the refracted ray turns away from the
	// view ray, so glass facing the camera stays clear and curves or normal maps bend it.
	vec3 bent = refract(-v, n, 1.0 / ior);
	vec3 turn = bent + v;
	vec2 offset = vec2(dot(turn, Frame.CameraRight.xyz), dot(turn, Frame.CameraUp.xyz)) * thickness * 0.1;
	vec2 screenUV = gl_FragCoord.xy * Frame.ScreenSize.zw + vec2(offset.x, -offset.y);

	vec3 behind = texture(SceneColor, screenUV).rgb;
	float blur = roughness * roughness * 0.03;
	if (blur > 0.0005)
	{
		for (int i = 0; i < 8; ++i)
			behind += texture(SceneColor, screenUV + BlurTaps[i] * blur).rgb;
		behind /= 9.0;
	}

	// Alpha decides how strongly the base color tints what shows through.
	vec3 transmittance = mix(vec3(1.0), tint.rgb, tint.a);

	float f0Scalar = pow((ior - 1.0) / (ior + 1.0), 2.0);
	vec3 f0 = vec3(f0Scalar);
	vec3 fresnel = FresnelSchlick(max(dot(n, v), 0.0), f0);

	vec3 color = behind * transmittance * (1.0 - fresnel);
	color += fresnel * Frame.Ambient.rgb * 4.0;
	for (int i = 0; i < Frame.LightCount.x; ++i)
	{
		vec3 l;
		vec3 radiance;
		GetLight(i, InWorldPosition, l, radiance);
		color += SpecularOnly(n, v, l, radiance, roughness, f0);
	}
	color += Material.Emissive.rgb;
	OutColor = vec4(color, 1.0);
}
