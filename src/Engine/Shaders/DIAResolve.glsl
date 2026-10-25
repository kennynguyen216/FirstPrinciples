#define DIA_TRACE_RESOURCES
#define DIA_STORAGE_BINDING 8
#include "DIAShared.glsl"
#ifdef DIA_RESOLVE_FALLBACK
#include "DIAPrimary.glsl"
layout(early_fragment_tests) in;
#endif
layout(set = 2, binding = 1) uniform sampler2D DenoisedDiffuse;
layout(set = 2, binding = 2) uniform sampler2D DenoisedSpecular;
layout(set = 2, binding = 3) uniform sampler2D RawPosition;
layout(set = 2, binding = 4) uniform sampler2D RawNormal;
layout(set = 2, binding = 5) uniform sampler2D RawDiffuse;
layout(set = 2, binding = 6) uniform sampler2D RawReflected;
layout(set = 2, binding = 7) uniform sampler2D RawSpecular;
layout(location = 0) in vec2 InUV;
layout(location = 0) out vec4 OutColor;

void StoreColor(vec3 color)
{
	bool valid = !any(isnan(color)) && !any(isinf(color)) && TraceDiagnostics == 0u;
	OutColor = vec4(valid ? min(max(color, vec3(0)), vec3(65504)) : vec3(1, 0, 1), valid ? 1 : 0);
}

void main()
{
#ifndef DIA_RESOLVE_FALLBACK
	gl_FragDepth = 0.0;
#endif
	vec2 ndc = vec2(InUV.x * 2.0 - 1.0, 1.0 - InUV.y * 2.0);
	vec4 nearPoint = DIA.InverseViewProjection * vec4(ndc, 0, 1);
	vec4 farPoint = DIA.InverseViewProjection * vec4(ndc, 1, 1);
	vec3 origin = nearPoint.xyz / nearPoint.w;
	vec3 rayEnd = farPoint.xyz / farPoint.w;
	vec3 direction = normalize(rayEnd - origin);
	LHit hit = Trace(origin, direction, length(rayEnd - origin), false);
	if (hit.Kind != 0u)
	{
		StoreColor(hit.Kind == 2u ? DIA.Sky.rgb : vec3(0));
		return;
	}
	LSurface surface = Surface(hit, direction);
	if (!surface.Valid) { StoreColor(vec3(0)); return; }
	if (surface.Unlit) { StoreColor(surface.Albedo + surface.Emission); return; }
	LMaterial material = Materials[Instances[hit.Instance].Metadata.x];
	bool textured = false;
	for (int i = 0; i < 7; ++i) textured = textured || material.Textures[i].y != 0u;
	vec3 diffuse = vec3(0), specular = vec3(0);
	bool hasDiffuse = any(greaterThan(DiffuseReflectance(surface), vec3(0)));
	vec2 weight = vec2(hasDiffuse ? 0 : 1, 0);
	bool mirror = surface.Roughness <= 0.02;
	vec3 reflectedDirection = reflect(direction, surface.Normal);
	LHit reflectedHit = LHit(6u, INVALID_INDEX, INVALID_INDEX, 0, vec3(0));
	vec3 reflectedPosition = vec3(0), reflectedNormal = vec3(0);
	LSurface source;
	bool mirrorReusable = false;
	if (mirror && !textured)
	{
		if (dot(reflectedDirection, surface.GeometricNormal) <= 0.0) weight.y = 1.0;
		else
		{
			reflectedHit = Trace(Offset(surface, reflectedDirection), reflectedDirection, DIA.Settings.x, false);
			mirrorReusable = reflectedHit.Kind == 2u;
			if (reflectedHit.Kind == 0u)
			{
				source = Surface(reflectedHit, reflectedDirection);
				LMaterial sourceMaterial = Materials[Instances[reflectedHit.Instance].Metadata.x];
				mirrorReusable = source.Valid;
				for (int i = 0; i < 8; ++i) mirrorReusable = mirrorReusable && sourceMaterial.Textures[i].y == 0u;
				reflectedPosition = source.Position;
				reflectedNormal = source.GeometricNormal;
			}
		}
	}
	if (!textured)
	{
		vec2 coordinate = InUV * DIA.Extent.xy - 0.5;
		ivec2 base = ivec2(floor(coordinate));
		vec2 blend = fract(coordinate);
		float footprint = max(0.01, length(surface.Position - DIA.Camera.xyz) * 4.0 / DIA.Extent.y);
		float lobe = max(0.001, 1.5 * surface.Roughness * surface.Roughness);
		float reflectedFootprint = max(0.01, (length(surface.Position - DIA.Camera.xyz) + reflectedHit.Distance) * 4.0 / DIA.Extent.y);
		float reflectedAngle = cos(max(0.001, 4.0 / DIA.Extent.y));
		for (int y = 0; y < 2; ++y) for (int x = 0; x < 2; ++x)
		{
			ivec2 pixel = base + ivec2(x, y);
			if (any(lessThan(pixel, ivec2(0))) || any(greaterThanEqual(pixel, ivec2(DIA.Extent.xy)))) continue;
			float sampleWeight = (x == 0 ? 1.0 - blend.x : blend.x) * (y == 0 ? 1.0 - blend.y : blend.y);
			if (sampleWeight <= 0.0 || texelFetch(RawDiffuse, pixel, 0).a != float(hit.Instance + 1u)) continue;
			vec3 delta = texelFetch(RawPosition, pixel, 0).xyz - surface.Position;
			vec4 normal = texelFetch(RawNormal, pixel, 0);
			float alignment = dot(normal.xyz, surface.Normal);
			float planeTolerance = max(0.002, length(delta) * sqrt(max(0.0, 1.0 - alignment * alignment)));
			if (length(delta) > footprint || abs(dot(delta, surface.Normal)) > planeTolerance || alignment < 0.995) continue;
			if (hasDiffuse)
			{
				diffuse += texelFetch(DenoisedDiffuse, pixel, 0).rgb * sampleWeight;
				weight.x += sampleWeight;
			}
			vec4 reflected = texelFetch(RawReflected, pixel, 0);
			vec3 sampleSpecular = texelFetch(DenoisedSpecular, pixel, 0).rgb;
			bool specularCompatible = !mirror && abs(normal.w - surface.Roughness) < 0.01 &&
				alignment >= cos(lobe) && reflected.w >= 0.0;
			float specularWeight = sampleWeight * (mirror ? 1.0 : exp(-4.0 * max(0.0, 1.0 - alignment) / (lobe * lobe)));
			if (mirror && mirrorReusable && normal.w <= 0.02 && alignment >= cos(0.001))
			{
				float sourceIdentity = abs(texelFetch(RawSpecular, pixel, 0).a);
				if (reflectedHit.Kind == 2u)
					specularCompatible = reflected.w == 0.0 && sourceIdentity == 0.0 && dot(reflected.xyz, reflectedDirection) >= reflectedAngle;
				else
				{
					vec3 reflectedDelta = reflected.xyz - reflectedPosition;
					vec3 lowDirection = normalize(reflected.xyz - texelFetch(RawPosition, pixel, 0).xyz);
					specularCompatible = reflected.w == 1.0 && sourceIdentity == float(reflectedHit.Instance + 1u) &&
						length(reflectedDelta) <= reflectedFootprint &&
						abs(dot(reflectedDelta, reflectedNormal)) <= max(0.002, reflectedFootprint * 0.02) &&
						dot(lowDirection, reflectedDirection) >= reflectedAngle;
				}
				vec3 lowView = DIA.Camera.w > 0.5 ? -DIA.CameraForward.xyz : normalize(DIA.Camera.xyz - texelFetch(RawPosition, pixel, 0).xyz);
				vec3 lowFresnel = Fresnel(max(dot(normal.xyz, lowView), 0.0), surface.F0);
				sampleSpecular *= Fresnel(max(dot(surface.Normal, -direction), 0.0), surface.F0) / max(lowFresnel, vec3(1e-8));
			}
			if (specularCompatible)
			{
				specular += sampleSpecular * specularWeight;
				weight.y += specularWeight;
			}
		}
	}
	vec3 exact;
	bool freshExact = false;
	if (weight.x <= 0.0001 || weight.y <= 0.0001)
	{
#ifdef DIA_RESOLVE_FALLBACK
		vec4 freshDiffuse, freshSpecular, freshPosition, freshNormal, exactSample, freshReflected;
		uint channels = (weight.x <= 0.0001 ? 1u : 0u) | (weight.y <= 0.0001 ? 2u : 0u);
		ShadePrimary(InUV, max(hasDiffuse ? 4.0 : 8.0, DIA.Settings.z), 8u, channels, freshDiffuse, freshSpecular, freshPosition, freshNormal, exactSample, freshReflected);
		if (weight.x <= 0.0001) { diffuse = freshDiffuse.rgb; weight.x = 1.0; }
		if (weight.y <= 0.0001) { specular = freshSpecular.rgb; weight.y = 1.0; }
		if ((channels & 2u) != 0u) { exact = exactSample.rgb; freshExact = true; }
#else
		// Depth 1 defers the pixel to DIAResolveFallback. Trace errors stay here so the fallback cannot hide them.
		if (TraceDiagnostics == 0u) { gl_FragDepth = 1.0; OutColor = vec4(0, 0, 0, 1); }
		else StoreColor(vec3(0));
		return;
#endif
	}
	if (!freshExact)
	{
		vec3 punctualDiffuse, punctualSpecular;
		PunctualDirect(surface, -direction, punctualDiffuse, punctualSpecular);
		exact = surface.Emission + punctualDiffuse + punctualSpecular;
		if (mirror && reflectedHit.Kind == 0u)
		{
			vec3 sourceDiffuse, sourceSpecular;
			PunctualDirect(source, -reflectedDirection, sourceDiffuse, sourceSpecular);
			exact += Fresnel(max(dot(surface.Normal, -direction), 0.0), surface.F0) * (source.Emission + sourceDiffuse + sourceSpecular);
		}
	}
	StoreColor(diffuse / weight.x + specular / weight.y + exact);
}
