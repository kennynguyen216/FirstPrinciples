#version 450
#extension GL_GOOGLE_include_directive : require
#define DIA_TRACE_RESOURCES
#include "DIAShared.glsl"
layout(location = 0) in vec2 InUV;
layout(location = 0) out vec4 OutColor;

void main()
{
	ivec2 pixel = ivec2(clamp(InUV, vec2(0), vec2(0.999999)) * 256.0);
	uint slot = uint(pixel.y * 256 + pixel.x);
	OutColor = DIA.History.z > 0.5 ? texelFetch(SurfaceCache, pixel, 0) : vec4(0);
	uint count = uint(DIA.History.y);
	if (slot >= count) { OutColor = vec4(0); return; }
	uvec4 owner = Surfaces[slot];
	if (owner.x == INVALID_INDEX) { OutColor = vec4(0); return; }
	if ((slot + count - DIA.Frame.z) % count >= DIA.Frame.w) return;
	uint grid = Instances[owner.x].Metadata.w;
	uint cell = owner.w % (grid * grid);
	float root = sqrt((float(cell % grid) + 0.5) / float(grid));
	float v = (float(cell / grid) + 0.5) / float(grid);
	LHit hit = LHit(0u, owner.x, owner.y, 0, vec3(1.0 - root, v * root, (1.0 - v) * root));
	LInstance instance = Instances[owner.x];
	LTriangle triangle = Triangles[owner.y];
	vec3 geometric = normalize(cross(mat3(instance.Transform) * (triangle.Position[1].xyz - triangle.Position[0].xyz), mat3(instance.Transform) * (triangle.Position[2].xyz - triangle.Position[0].xyz)));
	LSurface surface = Surface(hit, owner.w >= grid * grid ? geometric : -geometric);
	uint seed = Seed(uvec2(pixel), 1u);
	vec3 indirect = vec3(0);
	for (uint sampleIndex = 0u; sampleIndex < 4u; ++sampleIndex)
	{
		vec3 direction = CosineDirection(surface.Normal, seed);
		if (dot(direction, surface.GeometricNormal) <= 0.0) continue;
		LHit bounce;
		indirect += Incoming(Offset(surface, direction), direction, bounce, false, seed) * 0.25;
	}
	vec3 outgoing = DiffuseReflectance(surface) * surface.AO * indirect;
	if (surface.Unlit || !surface.Valid) outgoing = vec3(0);
	if (OutColor.a == float(owner.z) && DIA.History.z > 0.5)
	{
		float offset = float((slot + count - DIA.Frame.z) % count);
		float updates = 1.0 + floor((DIA.Emitters.w * min(float(DIA.Frame.w), float(count)) + offset) / float(count));
		outgoing = mix(outgoing, OutColor.rgb, min(1.0 - 1.0 / updates, 0.995));
	}
	OutColor = vec4(max(outgoing, vec3(0)), float(owner.z));
	if (TraceDiagnostics != 0u) OutColor.a = -OutColor.a;
}
