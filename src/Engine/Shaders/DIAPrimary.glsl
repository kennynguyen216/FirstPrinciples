#if defined(DIA_HARDWARE_RT) && defined(DIA_COMPARE_RAYS)
#define DIA_DIAGNOSTIC_RGB vec3(RayCompare)
#elif defined(DIA_HARDWARE_RT) && defined(DIA_RAY_QUERY_STATS)
#define DIA_DIAGNOSTIC_RGB vec3(RayStats)
#endif

void ShadePrimary(vec2 uv, float reflectionSamples, uint streamBase, uint channels, out vec4 OutDiffuse, out vec4 OutSpecular, out vec4 OutPosition, out vec4 OutNormal, out vec4 OutExact, out vec4 OutReflected)
{
#ifdef DIA_LIT
	const float mode = 0.0;
#else
	float mode = DIA.Settings.w;
#endif
	vec2 ndc = vec2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
	vec4 nearPoint = DIA.InverseViewProjection * vec4(ndc, 0, 1);
	vec4 farPoint = DIA.InverseViewProjection * vec4(ndc, 1, 1);
	vec3 origin = nearPoint.xyz / nearPoint.w;
	vec3 direction = normalize(farPoint.xyz / farPoint.w - nearPoint.xyz / nearPoint.w);
	LHit hit = Trace(origin, direction, length(farPoint.xyz / farPoint.w - origin), false);
	OutDiffuse = vec4(0);
	OutSpecular = vec4(0);
	OutPosition = vec4(0);
	OutNormal = vec4(0);
	OutExact = vec4(0);
	OutReflected = vec4(0, 0, 0, 2);
	if (hit.Kind != 0u)
	{
		if (hit.Kind == 2u && mode <= 1.0) OutExact.rgb = DIA.Sky.rgb;
		if (mode == 4.0 && hit.Kind != 2u) OutDiffuse.rgb = vec3(1, 0, 1);
#ifdef DIA_DIAGNOSTIC_RGB
		OutExact.rgb = DIA_DIAGNOSTIC_RGB;
#endif
		OutExact.a = float(TraceDiagnostics);
		return;
	}
	LSurface surface = Surface(hit, direction);
	OutDiffuse.a = float(hit.Instance + 1u);
	LMaterial receiverMaterial = Materials[Instances[hit.Instance].Metadata.x];
	bool mappedReceiver = false;
	for (int i = 0; i < 7; ++i) mappedReceiver = mappedReceiver || receiverMaterial.Textures[i].y != 0u;
	if (mappedReceiver) OutDiffuse.a = -OutDiffuse.a;
	OutPosition.xyz = surface.Position;
	OutNormal = vec4(surface.Normal, surface.Roughness);
#ifdef DIA_DIAGNOSTIC_RGB
	if (!surface.Valid) { OutExact.rgb = DIA_DIAGNOSTIC_RGB; return; }
	if (surface.Unlit) { OutExact.rgb = DIA_DIAGNOSTIC_RGB; return; }
#else
	if (!surface.Valid) return;
	if (surface.Unlit) { if (mode <= 1.0) OutExact.rgb = surface.Albedo + surface.Emission; return; }
#endif
	uint directSeed = Seed(uvec2(gl_FragCoord.xy), streamBase);
	uint diffuseSeed = Seed(uvec2(gl_FragCoord.xy), streamBase + 1u);
	uint specularSeed = Seed(uvec2(gl_FragCoord.xy), streamBase + 2u);
	vec3 direct, directSpecular;
	vec3 punctualDiffuse, punctualSpecular;
	vec3 diffuse = vec3(0);
	vec3 diffuseWeight = DiffuseReflectance(surface) * surface.AO;
	bool sampleDiffuse = (channels & 1u) != 0u && any(greaterThan(diffuseWeight, vec3(0)));
	float diffuseSamples = sampleDiffuse && DIA.Emitters.x > 0.0 ? DIA.Settings.y : 0.0;
	Direct(surface, -direction, direct, directSpecular, punctualDiffuse, punctualSpecular, reflectionSamples, diffuseSamples, DIA.Settings.x, false, directSeed);
	if (sampleDiffuse)
	{
		for (uint sampleIndex = 0u; sampleIndex < uint(DIA.Settings.y); ++sampleIndex)
		{
			vec3 sampleDirection = CosineDirection(surface.Normal, diffuseSeed);
			if (dot(sampleDirection, surface.GeometricNormal) <= 0.0) continue;
			LHit bounce;
			diffuse += Incoming(Offset(surface, sampleDirection), sampleDirection, bounce, false, diffuseSeed) / DIA.Settings.y;
			if (diffuseSamples > 0.0 && bounce.Kind == 0u && Materials[Instances[bounce.Instance].Metadata.x].Flags.z != 0u)
			{
				LSurface source = Surface(bounce, sampleDirection);
				if (source.Valid && length(source.Position - surface.Position) >= 0.0004)
				{
					float weight = PowerWeight(diffuseSamples * max(dot(surface.Normal, sampleDirection), 0.0) / LPI, 4.0 * EmitterDensity(bounce, source, surface.Position));
					direct += DiffuseReflectance(surface) * source.Emission * weight / DIA.Settings.y;
				}
			}
		}
	}
	diffuse *= diffuseWeight;
	vec3 reflected = vec3(0), reflectedExact = vec3(0), reflectedEmission = vec3(0);
	bool uncertainReflection = false;
	uint sampleCount = max(1u, uint(reflectionSamples));
	uvec2 strata = uvec2(1u, sampleCount);
	for (uint columns = 2u; columns <= 8u; ++columns)
		if (columns * columns <= sampleCount && sampleCount % columns == 0u) strata = uvec2(columns, sampleCount / columns);
	uint sampleOffset = Seed(uvec2(gl_FragCoord.xy), streamBase + 3u) % sampleCount;
	for (uint sampleIndex = 0u; (channels & 2u) != 0u && sampleIndex < uint(reflectionSamples); ++sampleIndex)
	{
		vec3 sampleDirection;
		vec3 weight;
		bool validSample;
		if (sampleCount > 1u && surface.Roughness > 0.02)
		{
			uint cell = (sampleIndex + sampleOffset) % sampleCount;
			vec2 xi;
			xi.x = Random(specularSeed);
			xi.y = Random(specularSeed);
			xi = min((vec2(cell % strata.x, cell / strata.x) + xi) / vec2(strata), vec2(0.99999994));
			validSample = SampleSpecular(surface, -direction, xi, sampleDirection, weight);
		}
		else if (sampleCount == 1u && streamBase == 4u && !mappedReceiver && surface.Roughness > 0.02 && dot(surface.Normal, -direction) > 0.0)
		{
			uvec2 pixel = uvec2(gl_FragCoord.xy);
			uint cell = (((pixel.x & 1u) + 2u * (pixel.y & 1u)) ^ Seed(pixel / 2u, streamBase + 3u)) & 3u;
			vec2 xi;
			xi.x = Random(specularSeed);
			xi.y = Random(specularSeed);
			xi = min((vec2(cell % 2u, cell / 2u) + xi) * 0.5, vec2(0.99999994));
			validSample = SampleSpecular(surface, -direction, xi, sampleDirection, weight);
		}
		else validSample = SampleSpecular(surface, -direction, specularSeed, sampleDirection, weight);
		if (!validSample) continue;
		LHit reflectedHit;
		vec3 incomingExact;
		vec3 incoming = Incoming(Offset(surface, sampleDirection), sampleDirection, reflectedHit, surface.Roughness <= 0.02, specularSeed, incomingExact);
		if (surface.Roughness > 0.02 && reflectedHit.Kind == 0u && any(greaterThan(Materials[Instances[reflectedHit.Instance].Metadata.x].Emission.rgb, vec3(0))))
		{
			LSurface source = Surface(reflectedHit, sampleDirection);
			incoming += source.Emission * SpecularSourceWeight(surface, -direction, sampleDirection, reflectedHit, source, reflectionSamples, false);
			if (mode == 1.0)
				reflectedEmission += source.Emission * SpecularSourceWeight(surface, -direction, sampleDirection, reflectedHit, source, reflectionSamples, false) * weight / reflectionSamples;
		}
		else if (mode == 1.0 && surface.Roughness <= 0.02 && reflectedHit.Kind == 0u && any(greaterThan(Materials[Instances[reflectedHit.Instance].Metadata.x].Emission.rgb, vec3(0))))
		{
			LSurface source = Surface(reflectedHit, sampleDirection);
			reflectedEmission += source.Emission * weight / reflectionSamples;
		}
		reflected += incoming * weight / reflectionSamples;
		if (surface.Roughness <= 0.02) reflectedExact += incomingExact * weight / reflectionSamples;
		uncertainReflection = uncertainReflection || reflectedHit.Kind >= 4u || reflectedHit.Kind == 1u;
		if (sampleIndex == 0u)
		{
			OutSpecular.a = reflectedHit.Kind == 0u ? float(reflectedHit.Instance + 1u) : 0.0;
			if (reflectedHit.Kind == 0u)
			{
				LMaterial material = Materials[Instances[reflectedHit.Instance].Metadata.x];
				bool mappedSource = false;
				for (int i = 0; i < 7; ++i) mappedSource = mappedSource || material.Textures[i].y != 0u;
				if (any(greaterThan(material.Emission.rgb, vec3(0))) || mappedSource ||
					(material.Flags.y == 0u && material.Parameters.y < 0.18)) OutSpecular.a = -OutSpecular.a;
			}
			if (surface.Roughness <= 0.02 && any(lessThan(surface.F0, vec3(0.02)))) OutSpecular.a = -abs(OutSpecular.a);
			OutPosition.w = reflectedHit.Distance;
			OutReflected = reflectedHit.Kind == 0u ? vec4(Offset(surface, sampleDirection) + sampleDirection * reflectedHit.Distance, 1) : vec4(sampleDirection, reflectedHit.Kind == 2u ? 0 : -1);
		}
	}
	if (uncertainReflection) OutReflected.w = -1;
	OutExact.rgb = surface.Emission + punctualDiffuse + punctualSpecular;
	OutDiffuse.rgb = (channels & 1u) != 0u ? direct - punctualDiffuse + diffuse : vec3(0);
	OutSpecular.rgb = (channels & 2u) != 0u ? directSpecular - punctualSpecular + reflected : vec3(0);
	if (mode == 0.0) { OutExact.rgb += reflectedExact; OutSpecular.rgb -= reflectedExact; }
	if (mode == 1.0) { OutDiffuse.rgb = direct - punctualDiffuse; OutSpecular.rgb = directSpecular - punctualSpecular + reflectedEmission; }
	if (mode == 2.0) { OutExact.rgb = vec3(0); OutDiffuse.rgb = diffuse; OutSpecular.rgb = vec3(0); }
	if (mode == 3.0) { OutExact.rgb = vec3(0); OutDiffuse.rgb = vec3(0); OutSpecular.rgb = reflected; }
	if (mode == 4.0) { OutExact.rgb = vec3(0); OutDiffuse.rgb = vec3(0.05, 0.8, 0.15); OutSpecular.rgb = vec3(0); }
	if (mode == 5.0) { OutExact.rgb = vec3(0); OutDiffuse.rgb = CachedDiffuse(hit, direction, diffuseSeed); OutSpecular.rgb = vec3(0); }
#ifdef DIA_DIAGNOSTIC_RGB
	OutExact.rgb = DIA_DIAGNOSTIC_RGB;
#endif
	OutExact.a = float(TraceDiagnostics);
}

#undef DIA_DIAGNOSTIC_RGB
