#ifdef DIA_HARDWARE_RT
#extension GL_EXT_ray_query : require
#endif

const float LPI = 3.141592653589793;
const uint INVALID_INDEX = 0xffffffffu;

layout(set = 3, binding = 0) uniform DIAData
{
	mat4 InverseViewProjection;
	mat4 PreviousViewProjection;
	vec4 Camera;
	vec4 Sky;
	vec4 Extent;
	uvec4 Counts;
	uvec4 Frame;
	vec4 Settings;
	vec4 History;
	vec4 Emitters;
	vec4 PreviousCamera;
	vec4 CameraForward;
	vec4 PreviousForward;
} DIA;

#if defined(DIA_TRACE_RESOURCES) || defined(DIA_MATERIAL_BINDING)
struct LInstance { mat4 Transform; mat4 Inverse; uvec4 Geometry; uvec4 Metadata; uvec4 Identity; vec4 Tint; };
struct LMaterial { vec4 BaseColor; vec4 Emission; vec4 Parameters; vec4 Parameters2; vec4 UVTransform; uvec4 Textures[8]; uvec4 Flags; };
#endif

#ifdef DIA_MATERIAL_BINDING
layout(std140, set = 2, binding = DIA_MATERIAL_BINDING) readonly buffer InstanceBuffer { LInstance Instances[]; };
layout(std140, set = 2, binding = DIA_MATERIAL_BINDING + 1) readonly buffer MaterialBuffer { LMaterial Materials[]; };
#endif

#ifdef DIA_TRACE_RESOURCES
#ifdef DIA_HARDWARE_RT
layout(set = 4, binding = 0) uniform accelerationStructureEXT RayScene;
#endif
struct LNode { vec4 Minimum; vec4 Maximum; uvec4 Links; };
struct LTriangle { vec4 Position[3]; vec4 Normal[3]; vec4 Tangent[3]; vec4 UV01; vec4 UV2; uvec4 Source; };
struct LLight { vec4 Position; vec4 ColorRange; vec4 DirectionInner; vec4 OuterRadius; };
struct LEmitter { uvec4 Identity; uvec4 Geometry; vec4 Distribution; };
layout(set = 2, binding = 0) uniform sampler2D SurfaceCache;
#ifndef DIA_STORAGE_BINDING
#define DIA_STORAGE_BINDING 1
#endif
layout(std140, set = 2, binding = DIA_STORAGE_BINDING) readonly buffer NodeBuffer { LNode Nodes[]; };
layout(std140, set = 2, binding = DIA_STORAGE_BINDING + 1) readonly buffer TriangleBuffer { LTriangle Triangles[]; };
layout(std140, set = 2, binding = DIA_STORAGE_BINDING + 2) readonly buffer InstanceBuffer { LInstance Instances[]; };
layout(std140, set = 2, binding = DIA_STORAGE_BINDING + 3) readonly buffer MaterialBuffer { LMaterial Materials[]; };
layout(std140, set = 2, binding = DIA_STORAGE_BINDING + 4) readonly buffer LightBuffer { LLight Lights[]; };
layout(std140, set = 2, binding = DIA_STORAGE_BINDING + 5) readonly buffer TexelBuffer { uvec4 Texels[]; };
layout(std140, set = 2, binding = DIA_STORAGE_BINDING + 6) readonly buffer SurfaceBuffer { uvec4 Surfaces[]; };
layout(std140, set = 2, binding = DIA_STORAGE_BINDING + 7) readonly buffer EmitterBuffer { LEmitter Emitters[]; };

struct LHit
{
	uint Kind;
	uint Instance;
	uint Triangle;
	float Distance;
	vec3 Bary;
};

struct LSurface
{
	vec3 Position;
	vec3 GeometricNormal;
	vec3 Normal;
	vec3 Albedo;
	vec3 Emission;
	vec3 F0;
	float Roughness;
	float Metallic;
	float AO;
	bool Valid;
	bool Unlit;
};

uint TraceDiagnostics = 0u;
#if defined(DIA_HARDWARE_RT) && defined(DIA_RAY_QUERY_STATS)
uvec3 RayStats = uvec3(0);
#endif
#if defined(DIA_HARDWARE_RT) && defined(DIA_COMPARE_RAYS)
uvec3 RayCompare = uvec3(0);
#endif

uint RandomBits(inout uint state)
{
	state = state * 747796405u + 2891336453u;
	uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
	return (word >> 22u) ^ word;
}

float Random(inout uint state) { return (float(RandomBits(state) >> 9) + 0.5) / 8388608.0; }

uint Hash(uint value)
{
	return RandomBits(value);
}

// One stream per pixel, frame and pass. Nested sampling advances this stream.
uint Seed(uvec2 pixel, uint pass)
{
	return Hash(pixel.x ^ Hash(pixel.y ^ Hash(pass ^ Hash(DIA.Frame.x ^ Hash(uint(DIA.History.w))))));
}

vec3 BasisDirection(vec3 n, vec3 local)
{
	vec3 tangent = normalize(cross(abs(n.z) < 0.999 ? vec3(0, 0, 1) : vec3(0, 1, 0), n));
	return tangent * local.x + cross(n, tangent) * local.y + n * local.z;
}

vec3 CosineDirection(vec3 n, inout uint seed)
{
	float r = sqrt(Random(seed));
	float angle = 2.0 * LPI * Random(seed);
	return BasisDirection(n, vec3(r * cos(angle), r * sin(angle), sqrt(max(0.0, 1.0 - r * r))));
}

bool BoundsHit(vec3 minimum, vec3 maximum, vec3 origin, vec3 direction, float limit, out float exitDistance)
{
	float nearDistance = 0.0001;
	exitDistance = limit;
	for (int axis = 0; axis < 3; ++axis)
	{
		if (abs(direction[axis]) < 1e-20)
		{
			if (origin[axis] < minimum[axis] || origin[axis] > maximum[axis]) return false;
			continue;
		}
		vec2 interval = (vec2(minimum[axis], maximum[axis]) - origin[axis]) / direction[axis];
		nearDistance = max(nearDistance, min(interval.x, interval.y));
		exitDistance = min(exitDistance, max(interval.x, interval.y));
		if (exitDistance < nearDistance) return false;
	}
	return true;
}

#ifdef DIA_HARDWARE_RT
bool BoundsPath(uint nodeIndex, uint first, uint count, uint target, vec3 origin, vec3 direction, float maximum, bool emitterOnly)
{
	for (uint depth = 0u; depth < 32u; ++depth)
	{
		LNode node = Nodes[nodeIndex];
		if (emitterOnly && (node.Links.z & 0x80000000u) == 0u) return false;
		float exitDistance;
		if (!BoundsHit(node.Minimum.xyz, node.Maximum.xyz, origin, direction, maximum, exitDistance)) return false;
		if (node.Links.y != 0u) return target >= node.Links.x && target - node.Links.x < node.Links.y;
		uint leftCount = count / 2u;
		if (target < first + leftCount) { ++nodeIndex; count = leftCount; }
		else { nodeIndex = node.Links.z & 0x7fffffffu; first += leftCount; count -= leftCount; }
	}
	return false;
}
#endif

bool TriangleHit(LTriangle triangle, vec3 origin, vec3 direction, float maximum, out float distance, out vec3 bary)
{
	vec3 absolute = abs(direction);
	int zAxis = absolute.x > absolute.y ? (absolute.x > absolute.z ? 0 : 2) : (absolute.y > absolute.z ? 1 : 2);
	int xAxis = (zAxis + 1) % 3, yAxis = (xAxis + 1) % 3;
	if (direction[zAxis] < 0.0) { int swapAxis = xAxis; xAxis = yAxis; yAxis = swapAxis; }
	float sx = -direction[xAxis] / direction[zAxis];
	float sy = -direction[yAxis] / direction[zAxis];
	float sz = 1.0 / direction[zAxis];
	precise vec3 x, y, z;
	for (int i = 0; i < 3; ++i)
	{
		vec3 p = triangle.Position[i].xyz - origin;
		x[i] = p[xAxis] + sx * p[zAxis];
		y[i] = p[yAxis] + sy * p[zAxis];
		z[i] = p[zAxis] * sz;
	}
	precise float a = x[1] * y[2] - y[1] * x[2];
	precise float b = x[2] * y[0] - y[2] * x[0];
	precise float c = x[0] * y[1] - y[0] * x[1];
	if ((a < 0.0 || b < 0.0 || c < 0.0) && (a > 0.0 || b > 0.0 || c > 0.0)) return false;
	float determinant = a + b + c;
	if (determinant == 0.0) return false;
	distance = (a * z[0] + b * z[1] + c * z[2]) / determinant;
	if (isnan(distance) || isinf(distance) || distance < 0.0001 || distance >= maximum) return false;
	bary = vec3(a, b, c) / determinant;
	return true;
}

#ifdef DIA_HARDWARE_RT
#define DIA_SOFTWARE_TRACE TraceSoftware
#else
#define DIA_SOFTWARE_TRACE Trace
#endif
LHit DIA_SOFTWARE_TRACE(vec3 origin, vec3 direction, float maximum, bool segment, bool emitterOnly)
{
	LHit hit = LHit(segment ? 3u : 2u, INVALID_INDEX, INVALID_INDEX, maximum, vec3(0));
	if (any(isnan(origin)) || any(isinf(origin)) || any(isnan(direction)) || any(isinf(direction)) || isnan(maximum) || isinf(maximum) || maximum <= 0.0 || abs(dot(direction, direction) - 1.0) > 0.001)
	{
		TraceDiagnostics |= 4u;
		return LHit(6u, INVALID_INDEX, INVALID_INDEX, 0, vec3(0));
	}
	if (DIA.Counts.y == 0u) return hit;
	uint root = DIA.Counts.x;
	float exitDistance;
	if (!segment && BoundsHit(Nodes[root].Minimum.xyz, Nodes[root].Maximum.xyz, origin, direction, 1e30, exitDistance) && exitDistance > maximum)
		hit.Kind = 4u;
	uint visits = 0u;
	for (uint nodeIndex = root; nodeIndex < DIA.Counts.w;)
	{
		if (++visits > 8192u) { hit.Kind = 5u; TraceDiagnostics |= 2u; return hit; }
		LNode node = Nodes[nodeIndex];
		if (emitterOnly && (node.Links.z & 0x80000000u) == 0u) { nodeIndex = node.Links.w; continue; }
		if (!BoundsHit(node.Minimum.xyz, node.Maximum.xyz, origin, direction, hit.Distance, exitDistance)) { nodeIndex = node.Links.w; continue; }
		++nodeIndex;
		for (uint i = node.Links.x; i < node.Links.x + node.Links.y; ++i)
		{
			LInstance instance = Instances[i];
			if (emitterOnly && Materials[instance.Metadata.x].Flags.z == 0u) continue;
			vec3 localOrigin = (instance.Inverse * vec4(origin, 1)).xyz;
			vec3 localDirection = (instance.Inverse * vec4(direction, 0)).xyz;
			for (uint meshIndex = instance.Geometry.x; meshIndex < instance.Metadata.z;)
			{
				if (++visits > 8192u) { hit.Kind = 5u; TraceDiagnostics |= 2u; return hit; }
				LNode meshNode = Nodes[meshIndex];
				if (!BoundsHit(meshNode.Minimum.xyz, meshNode.Maximum.xyz, localOrigin, localDirection, hit.Distance, exitDistance)) { meshIndex = meshNode.Links.w; continue; }
				++meshIndex;
				for (uint t = meshNode.Links.x; t < meshNode.Links.x + meshNode.Links.y; ++t)
				{
					float distance;
					vec3 bary;
					if (TriangleHit(Triangles[t], localOrigin, localDirection, hit.Distance, distance, bary))
					{
						hit = LHit(0u, i, t, distance, bary);
						if (segment) return hit;
					}
				}
			}
		}
	}
	if (hit.Kind == 4u) TraceDiagnostics |= 1u;
	return hit;
}

#undef DIA_SOFTWARE_TRACE

#ifdef DIA_HARDWARE_RT
#ifdef DIA_COMPARE_RAYS
#define DIA_HARDWARE_TRACE TraceHardware
#else
#define DIA_HARDWARE_TRACE Trace
#endif
LHit DIA_HARDWARE_TRACE(vec3 origin, vec3 direction, float maximum, bool segment, bool emitterOnly)
{
	LHit hit = LHit(segment ? 3u : 2u, INVALID_INDEX, INVALID_INDEX, maximum, vec3(0));
	if (any(isnan(origin)) || any(isinf(origin)) || any(isnan(direction)) || any(isinf(direction)) || isnan(maximum) || isinf(maximum) || maximum <= 0.0 || abs(dot(direction, direction) - 1.0) > 0.001)
	{
		TraceDiagnostics |= 4u;
		return LHit(6u, INVALID_INDEX, INVALID_INDEX, 0, vec3(0));
	}
	if (DIA.Counts.y == 0u) return hit;
	uint root = DIA.Counts.x;
	float exitDistance;
	if (!segment && BoundsHit(Nodes[root].Minimum.xyz, Nodes[root].Maximum.xyz, origin, direction, 1e30, exitDistance) && exitDistance > maximum)
		hit.Kind = 4u;
	float queryMaximum = maximum < 3e38 ? maximum + max(0.0001, maximum * 0.00001) : maximum;
	rayQueryEXT query;
#ifdef DIA_RAY_QUERY_STATS
	++RayStats.x;
#endif
	rayQueryInitializeEXT(query, RayScene, gl_RayFlagsNoOpaqueEXT, emitterOnly ? 2u : 1u, origin, 0.0, direction, queryMaximum);
	uint candidates = 0u;
	while (rayQueryProceedEXT(query))
	{
#ifdef DIA_RAY_QUERY_STATS
		++RayStats.y;
#endif
		if (++candidates > 8192u)
		{
			rayQueryTerminateEXT(query);
#ifdef DIA_RAY_QUERY_STATS
			++RayStats.z;
#endif
			return TraceSoftware(origin, direction, maximum, segment, emitterOnly);
		}
		if (rayQueryGetIntersectionTypeEXT(query, false) != gl_RayQueryCandidateIntersectionAABBEXT)
		{
			rayQueryTerminateEXT(query);
			TraceDiagnostics |= 4u;
			return LHit(6u, INVALID_INDEX, INVALID_INDEX, 0, vec3(0));
		}
		uint index = uint(rayQueryGetIntersectionInstanceCustomIndexEXT(query, false));
		if (index >= DIA.Counts.y)
		{
			rayQueryTerminateEXT(query);
			TraceDiagnostics |= 4u;
			return LHit(6u, INVALID_INDEX, INVALID_INDEX, 0, vec3(0));
		}
		LInstance instance = Instances[index];
		uint primitive = uint(rayQueryGetIntersectionPrimitiveIndexEXT(query, false));
		uint triangle = instance.Geometry.y + primitive;
		if (primitive >= instance.Metadata.y || triangle < instance.Geometry.y || triangle >= uint(Triangles.length()))
		{
			rayQueryTerminateEXT(query);
			TraceDiagnostics |= 4u;
			return LHit(6u, INVALID_INDEX, INVALID_INDEX, 0, vec3(0));
		}
		if (emitterOnly && Materials[instance.Metadata.x].Flags.z == 0u) continue;
		vec3 localOrigin = (instance.Inverse * vec4(origin, 1)).xyz;
		vec3 localDirection = (instance.Inverse * vec4(direction, 0)).xyz;
		float distance;
		vec3 bary;
		if (!TriangleHit(Triangles[triangle], localOrigin, localDirection, maximum, distance, bary)) continue;
		if (segment && (!BoundsPath(root, 0u, DIA.Counts.y, index, origin, direction, maximum, emitterOnly) ||
			!BoundsPath(instance.Geometry.x, instance.Geometry.y, instance.Metadata.y, triangle, localOrigin, localDirection, maximum, false))) continue;
		if (distance < hit.Distance || (distance == hit.Distance && (index < hit.Instance || (index == hit.Instance && triangle < hit.Triangle))))
		{
			hit = LHit(0u, index, triangle, distance, bary);
			if (segment) { rayQueryTerminateEXT(query); return hit; }
		}
	}
	if (hit.Kind == 4u) TraceDiagnostics |= 1u;
	return hit;
}
#undef DIA_HARDWARE_TRACE
#ifdef DIA_COMPARE_RAYS
LHit Trace(vec3 origin, vec3 direction, float maximum, bool segment, bool emitterOnly)
{
	uint before = TraceDiagnostics;
	LHit hardware = TraceHardware(origin, direction, maximum, segment, emitterOnly);
	uint hardwareDiagnostics = TraceDiagnostics;
	TraceDiagnostics = before;
	LHit software = TraceSoftware(origin, direction, maximum, segment, emitterOnly);
	uint softwareDiagnostics = TraceDiagnostics;
	TraceDiagnostics = hardwareDiagnostics;
	++RayCompare.x;
	bool different = hardware.Kind != software.Kind || hardwareDiagnostics != softwareDiagnostics;
	if (!segment && hardware.Kind == 0u && software.Kind == 0u)
	{
		bool sameTriangle = hardware.Instance == software.Instance && hardware.Triangle == software.Triangle;
		different = different || !sameTriangle;
		if (sameTriangle && (floatBitsToUint(hardware.Distance) != floatBitsToUint(software.Distance) ||
			any(notEqual(floatBitsToUint(hardware.Bary), floatBitsToUint(software.Bary))))) ++RayCompare.z;
	}
	if (different) ++RayCompare.y;
	return hardware;
}
#endif
#endif

LHit Trace(vec3 origin, vec3 direction, float maximum, bool segment)
{
	return Trace(origin, direction, maximum, segment, false);
}

vec3 Srgb(vec3 c) { return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), greaterThan(c, vec3(0.04045))); }

vec4 Pixel(uvec4 descriptor, ivec2 coordinate)
{
	ivec2 size = ivec2(descriptor.yz);
	coordinate = (coordinate % size + size) % size;
	uint index = descriptor.x + uint(coordinate.y * size.x + coordinate.x);
	uint packed = Texels[index >> 2u][index & 3u];
	vec4 color = vec4(float(packed & 255u), float((packed >> 8u) & 255u), float((packed >> 16u) & 255u), float(packed >> 24u)) / 255.0;
	if ((descriptor.w & 1u) != 0u) color.rgb = Srgb(color.rgb);
	return color;
}

vec4 SampleMap(uvec4 descriptor, vec2 uv, vec4 fallback)
{
	if (descriptor.y == 0u || descriptor.z == 0u) return fallback;
	vec2 coordinate = fract(uv) * vec2(descriptor.yz) - 0.5;
	ivec2 a = ivec2(floor(coordinate));
	vec2 t = fract(coordinate);
	return mix(mix(Pixel(descriptor, a), Pixel(descriptor, a + ivec2(1, 0)), t.x),
		mix(Pixel(descriptor, a + ivec2(0, 1)), Pixel(descriptor, a + ivec2(1, 1)), t.x), t.y);
}

LSurface Surface(LHit hit, vec3 incoming)
{
	LInstance instance = Instances[hit.Instance];
	LTriangle triangle = Triangles[hit.Triangle];
	LMaterial material = Materials[instance.Metadata.x];
	vec3 b = hit.Bary;
	vec3 position = triangle.Position[0].xyz * b.x + triangle.Position[1].xyz * b.y + triangle.Position[2].xyz * b.z;
	vec3 geometric = normalize(cross(mat3(instance.Transform) * (triangle.Position[1].xyz - triangle.Position[0].xyz),
		mat3(instance.Transform) * (triangle.Position[2].xyz - triangle.Position[0].xyz)));
	vec3 n = normalize(transpose(mat3(instance.Inverse)) * (triangle.Normal[0].xyz * b.x + triangle.Normal[1].xyz * b.y + triangle.Normal[2].xyz * b.z));
	if (dot(n, geometric) < 0.0) n = -n;
	bool front = dot(geometric, -incoming) >= 0.0;
	bool valid = front || material.Flags.x != 0u;
	if (!front && material.Flags.x != 0u) { n = -n; geometric = -geometric; }
	vec2 uv = (triangle.UV01.xy * b.x + triangle.UV01.zw * b.y + triangle.UV2.xy * b.z) * material.UVTransform.xy + material.UVTransform.zw;
	vec4 tangent = triangle.Tangent[0] * b.x + triangle.Tangent[1] * b.y + triangle.Tangent[2] * b.z;
	vec3 t = mat3(instance.Transform) * tangent.xyz;
	t = normalize(t - n * dot(t, n));
	vec3 bitangent = cross(n, t) * tangent.w * sign(determinant(mat3(instance.Transform)));
	vec3 mapped = SampleMap(material.Textures[1], uv, vec4(0.5, 0.5, 1, 1)).xyz * 2.0 - 1.0;
	mapped.xy *= material.Parameters.w;
	n = normalize(t * mapped.x + bitangent * mapped.y + n * mapped.z);
	if (dot(n, geometric) < 0.001) n = geometric;
	vec4 albedo = SampleMap(material.Textures[0], uv, vec4(1)) * material.BaseColor * instance.Tint;
	float metallic = clamp(material.Parameters.x * SampleMap(material.Textures[3], uv, vec4(1)).r, 0.0, 1.0);
	float roughness = clamp(material.Parameters.y * SampleMap(material.Textures[4], uv, vec4(1)).r, 0.0, 1.0);
	float specular = clamp(material.Parameters.z * SampleMap(material.Textures[5], uv, vec4(1)).r, 0.0, 1.0);
	vec3 emission = material.Emission.rgb * SampleMap(material.Textures[6], uv, vec4(1)).rgb;
	return LSurface((instance.Transform * vec4(position, 1)).xyz, geometric, n, albedo.rgb,
		valid ? emission : vec3(0), mix(vec3(0.08 * specular), albedo.rgb, metallic), roughness, metallic,
		mix(1.0, SampleMap(material.Textures[2], uv, vec4(1)).r, clamp(material.Parameters2.x, 0.0, 1.0)), valid, material.Flags.y != 0u);
}

vec3 DiffuseReflectance(LSurface s) { return s.Albedo * (1.0 - s.Metallic) * (vec3(1) - s.F0); }
vec3 Fresnel(float cosine, vec3 f0) { return f0 + (1.0 - f0) * pow(clamp(1.0 - cosine, 0.0, 1.0), 5.0); }

float Distribution(float nh, float roughness)
{
	float a = max(roughness * roughness, 0.0004);
	float a2 = a * a;
	float d = nh * nh * (a2 - 1.0) + 1.0;
	return a2 / max(LPI * d * d, 1e-20);
}

float Masking(float cosine, float roughness)
{
	float a = max(roughness * roughness, 0.0004);
	float a2 = a * a;
	return 2.0 * cosine / max(cosine + sqrt(a2 + (1.0 - a2) * cosine * cosine), 1e-8);
}

float Geometry(float nv, float nl, float roughness)
{
	return Masking(nv, roughness) * Masking(nl, roughness);
}

vec3 Offset(LSurface surface, vec3 direction)
{
	return surface.Position + surface.GeometricNormal * (dot(direction, surface.GeometricNormal) >= 0.0 ? 0.0002 : -0.0002);
}

bool SampleSpecular(LSurface surface, vec3 view, vec2 xi, out vec3 direction, out vec3 weight)
{
	float nv = dot(surface.Normal, view);
	direction = vec3(0);
	weight = vec3(0);
	if (nv <= 0.0) return false;
	if (surface.Roughness <= 0.02)
	{
		direction = reflect(-view, surface.Normal);
		weight = Fresnel(nv, surface.F0);
		return dot(direction, surface.GeometricNormal) > 0.0;
	}
	float a = surface.Roughness * surface.Roughness;
	vec3 tangent = normalize(cross(abs(surface.Normal.z) < 0.999 ? vec3(0, 0, 1) : vec3(0, 1, 0), surface.Normal));
	vec3 bitangent = cross(surface.Normal, tangent);
	vec3 stretched = normalize(vec3(a * dot(view, tangent), a * dot(view, bitangent), nv));
	float lensq = dot(stretched.xy, stretched.xy);
	vec3 axis = lensq > 0.0 ? vec3(-stretched.y, stretched.x, 0) * inversesqrt(lensq) : vec3(1, 0, 0);
	vec3 otherAxis = cross(stretched, axis);
	float radius = sqrt(xi.x);
	float phi = 2.0 * LPI * xi.y;
	vec2 disk = radius * vec2(cos(phi), sin(phi));
	float blend = 0.5 * (1.0 + stretched.z);
	disk.y = (1.0 - blend) * sqrt(max(0.0, 1.0 - disk.x * disk.x)) + blend * disk.y;
	vec3 projected = disk.x * axis + disk.y * otherAxis + sqrt(max(0.0, 1.0 - dot(disk, disk))) * stretched;
	vec3 local = normalize(vec3(a * projected.xy, max(0.0, projected.z)));
	vec3 h = tangent * local.x + bitangent * local.y + surface.Normal * local.z;
	direction = reflect(-view, h);
	float nl = dot(surface.Normal, direction);
	float vh = max(dot(view, h), 0.0);
	weight = Fresnel(vh, surface.F0) * Masking(max(nl, 0.0), surface.Roughness);
	return nl > 0.0 && vh > 0.0 && dot(direction, surface.GeometricNormal) > 0.0;
}

bool SampleSpecular(LSurface surface, vec3 view, inout uint seed, out vec3 direction, out vec3 weight)
{
	vec2 xi = vec2(0);
	if (surface.Roughness > 0.02 && dot(surface.Normal, view) > 0.0)
	{
		xi.x = Random(seed);
		xi.y = Random(seed);
	}
	return SampleSpecular(surface, view, xi, direction, weight);
}

float PowerWeight(float a, float b)
{
	float scale = max(max(a, b), 1e-20);
	a /= scale;
	b /= scale;
	return a * a / max(a * a + b * b, 1e-20);
}

float SpecularDensity(LSurface surface, vec3 view, vec3 direction, bool roulette)
{
	if (surface.Roughness <= 0.02 || dot(direction, surface.GeometricNormal) <= 0.0) return 0.0;
	float nv = dot(surface.Normal, view);
	float nl = dot(surface.Normal, direction);
	vec3 h = normalize(view + direction);
	float nh = max(dot(surface.Normal, h), 0.0);
	float vh = max(dot(view, h), 0.0);
	if (nv <= 0.0 || nl <= 0.0 || nh <= 0.0 || vh <= 0.0) return 0.0;
	float pdf = Distribution(nh, surface.Roughness) * Masking(nv, surface.Roughness) / (4.0 * nv);
	if (roulette)
	{
		vec3 weight = Fresnel(vh, surface.F0) * Masking(nl, surface.Roughness);
		pdf *= clamp(max(weight.r, max(weight.g, weight.b)), 0.0, 1.0);
	}
	return pdf;
}

float EmitterDensity(LHit hit, LSurface source, vec3 origin)
{
	LInstance instance = Instances[hit.Instance];
	LMaterial material = Materials[instance.Metadata.x];
	uint grid = material.Flags.w;
	if (material.Flags.z == 0u || grid == 0u || !source.Valid) return 0.0;
	float r = max(1.0 - hit.Bary.x, 1e-8);
	uvec2 cell = uvec2(clamp(vec2(r * r, hit.Bary.y / r) * float(grid), vec2(0), vec2(grid - 1u)));
	uint selected = material.Flags.z - 1u + (hit.Triangle - instance.Geometry.y) * grid * grid + cell.y * grid + cell.x;
	LEmitter emitter = Emitters[selected];
	float previous = selected == 0u ? 0.0 : Emitters[selected - 1u].Distribution.x;
	float uniformWeight = DIA.Emitters.y > 0.0 ? 0.05 : 1.0;
	float probability = uniformWeight / DIA.Emitters.x;
	if (DIA.Emitters.y > 0.0) probability += (1.0 - uniformWeight) * (emitter.Distribution.x - previous) / DIA.Emitters.y;
	LTriangle triangle = Triangles[hit.Triangle];
	float area = length(cross(mat3(instance.Transform) * (triangle.Position[1].xyz - triangle.Position[0].xyz), mat3(instance.Transform) * (triangle.Position[2].xyz - triangle.Position[0].xyz))) * 0.5;
	vec3 delta = source.Position - origin;
	float cosine = dot(source.GeometricNormal, -normalize(delta));
	if (cosine <= 0.0) return 0.0;
	return probability * float(grid * grid) * dot(delta, delta) / max(area * cosine, 1e-20);
}

float SpecularSourceWeight(LSurface surface, vec3 view, vec3 direction, LHit hit, LSurface source, float samples, bool roulette)
{
	if (surface.Roughness <= 0.02 || DIA.Emitters.x == 0.0) return 1.0;
	return PowerWeight(samples * SpecularDensity(surface, view, direction, roulette), 4.0 * EmitterDensity(hit, source, surface.Position));
}

void PunctualDirect(LSurface surface, vec3 view, out vec3 diffuse, out vec3 specular)
{
	diffuse = vec3(0);
	specular = vec3(0);
	if (!surface.Valid || surface.Unlit || (surface.Roughness <= 0.02 && !any(greaterThan(DiffuseReflectance(surface), vec3(0))))) return;
	for (uint i = 0u; i < DIA.Counts.z; ++i)
	{
		LLight light = Lights[i];
		vec3 direction;
		float maximum = DIA.Settings.x;
		vec3 radiance = light.ColorRange.rgb;
		bool segment = light.Position.w > 0.5;
		if (segment)
		{
			vec3 delta = light.Position.xyz - surface.Position;
			float distance = length(delta);
			direction = delta / max(distance, 1e-8);
			float ratio = distance / max(light.ColorRange.w, 0.0001);
			float window = max(0.0, 1.0 - ratio * ratio * ratio * ratio);
			radiance *= window * window / max(distance * distance, light.OuterRadius.y * light.OuterRadius.y);
			if (light.Position.w > 1.5)
				radiance *= smoothstep(light.OuterRadius.x, light.DirectionInner.w, dot(-direction, light.DirectionInner.xyz));
		}
		else direction = -normalize(light.Position.xyz);
		float nl = max(dot(surface.Normal, direction), 0.0);
		if (nl <= 0.0 || dot(surface.GeometricNormal, direction) <= 0.0 || max(radiance.r, max(radiance.g, radiance.b)) == 0.0) continue;
		vec3 shadowOrigin = Offset(surface, direction);
		vec3 shadowDirection = direction;
		if (segment)
		{
			vec3 shadowDelta = light.Position.xyz - shadowOrigin;
			float shadowDistance = length(shadowDelta);
			shadowDirection = shadowDistance > 0.0001 ? shadowDelta / shadowDistance : direction;
			maximum = max(0.0001, shadowDistance - 0.0001);
		}
		LHit blocker = Trace(shadowOrigin, shadowDirection, maximum, segment);
		if (blocker.Kind != (segment ? 3u : 2u)) continue;
		diffuse += DiffuseReflectance(surface) * radiance * (nl / LPI);
		float nv = max(dot(surface.Normal, view), 0.0001);
		vec3 halfVector = normalize(view + direction);
		float nh = max(dot(surface.Normal, halfVector), 0.0);
		if (surface.Roughness > 0.02)
			specular += radiance * Fresnel(max(dot(view, halfVector), 0.0), surface.F0) * Distribution(nh, surface.Roughness) *
				Geometry(nv, nl, surface.Roughness) / (4.0 * nv);
	}
}

void Direct(LSurface surface, vec3 view, out vec3 diffuse, out vec3 specular, out vec3 punctualDiffuse, out vec3 punctualSpecular, float reflectionSamples, float diffuseSamples, float diffuseDistance, bool roulette, inout uint seed)
{
	PunctualDirect(surface, view, diffuse, specular);
	punctualDiffuse = diffuse;
	punctualSpecular = specular;
	if (!surface.Valid || surface.Unlit) return;
	uint count = uint(DIA.Emitters.x);
	if (count == 0u) return;
	if (surface.Roughness <= 0.02 && !any(greaterThan(DiffuseReflectance(surface), vec3(0))))
	{
		seed = seed * 485992129u + 4177007664u;
		return;
	}
	for (uint areaSample = 0u; areaSample < 4u; ++areaSample)
	{
	uint selected;
	float uniformWeight = DIA.Emitters.y > 0.0 ? 0.05 : 1.0;
	bool uniformSample = Random(seed) < uniformWeight;
	float selection = min((float(areaSample) + Random(seed)) * 0.25, 0.99999994);
	if (uniformSample) selected = min(count - 1u, uint(selection * float(count)));
	else
	{
		float target = selection * DIA.Emitters.y;
		uint low = 0u, high = count - 1u;
		while (low < high)
		{
			uint middle = (low + high) / 2u;
			if (Emitters[middle].Distribution.x > target) high = middle;
			else low = middle + 1u;
		}
		selected = low;
	}
	LEmitter emitter = Emitters[selected];
	float previousCDF = selected == 0u ? 0.0 : Emitters[selected - 1u].Distribution.x;
	float probability = uniformWeight / float(count);
	if (DIA.Emitters.y > 0.0) probability += (1.0 - uniformWeight) * (emitter.Distribution.x - previousCDF) / DIA.Emitters.y;
	float grid = float(emitter.Geometry.w);
	float root = sqrt((float(emitter.Geometry.z & 65535u) + Random(seed)) / grid);
	float v = (float(emitter.Geometry.z >> 16u) + Random(seed)) / grid;
	LHit sampleHit = LHit(0u, emitter.Geometry.x, emitter.Geometry.y, 0, vec3(1.0 - root, v * root, (1.0 - v) * root));
	LInstance instance = Instances[emitter.Geometry.x];
	LTriangle triangle = Triangles[emitter.Geometry.y];
	vec3 samplePosition = (instance.Transform * vec4(triangle.Position[0].xyz * sampleHit.Bary.x + triangle.Position[1].xyz * sampleHit.Bary.y + triangle.Position[2].xyz * sampleHit.Bary.z, 1)).xyz;
	vec3 delta = samplePosition - surface.Position;
	float distance = length(delta);
	if (distance < 0.0004) continue;
	vec3 direction = delta / distance;
	float receiverCosine = max(dot(surface.Normal, direction), 0.0);
	if (receiverCosine <= 0.0 || dot(surface.GeometricNormal, direction) <= 0.0) continue;
	LSurface source = Surface(sampleHit, direction);
	float sourceCosine = max(dot(source.GeometricNormal, -direction), 0.0);
	if (!source.Valid || sourceCosine <= 0.0 || !any(greaterThan(source.Emission, vec3(0)))) continue;
	float worldArea = length(cross(mat3(instance.Transform) * (triangle.Position[1].xyz - triangle.Position[0].xyz), mat3(instance.Transform) * (triangle.Position[2].xyz - triangle.Position[0].xyz))) * 0.5;
	float pdf = probability * grid * grid / max(worldArea, 1e-20);
	vec3 shadowOrigin = Offset(surface, direction);
	vec3 shadowDelta = samplePosition - shadowOrigin;
	float shadowDistance = length(shadowDelta);
	if (Trace(shadowOrigin, shadowDelta / shadowDistance, shadowDistance - 0.0001, true).Kind != 3u) continue;
	float cosineDensity = shadowDistance < diffuseDistance ? diffuseSamples * receiverCosine / LPI : 0.0;
	float diffuseWeight = PowerWeight(4.0 * pdf * distance * distance / sourceCosine, cosineDensity);
	diffuse += diffuseWeight * DiffuseReflectance(surface) * source.Emission * (receiverCosine * sourceCosine / max(4.0 * LPI * distance * distance * pdf, 1e-20));
	if (surface.Roughness > 0.02)
	{
		float nv = max(dot(surface.Normal, view), 0.0001);
		vec3 halfVector = normalize(view + direction);
		float nh = max(dot(surface.Normal, halfVector), 0.0);
		float specularDensity = shadowDistance < DIA.Settings.x ? reflectionSamples * SpecularDensity(surface, view, direction, roulette) : 0.0;
		float mis = PowerWeight(4.0 * pdf * distance * distance / sourceCosine, specularDensity);
		specular += mis * source.Emission * Fresnel(max(dot(view, halfVector), 0.0), surface.F0) * Distribution(nh, surface.Roughness) * Geometry(nv, receiverCosine, surface.Roughness) *
			sourceCosine / max(16.0 * nv * distance * distance * pdf, 1e-20);
	}
	}
}

vec3 DiffuseEmission(LSurface surface, inout uint seed)
{
	vec3 direction = CosineDirection(surface.Normal, seed);
	if (dot(direction, surface.GeometricNormal) <= 0.0) return vec3(0);
	vec3 origin = Offset(surface, direction);
	LHit hit = Trace(origin, direction, 1e30, false, true);
	if (hit.Kind != 0u) return vec3(0);
	LSurface source = Surface(hit, direction);
	if (!source.Valid || !any(greaterThan(source.Emission, vec3(0))) || length(source.Position - surface.Position) < 0.0004) return vec3(0);
	if (Trace(origin, direction, hit.Distance - 0.0001, true).Kind != 3u) return vec3(0);
	float weight = PowerWeight(max(dot(surface.Normal, direction), 0.0) / LPI, 4.0 * EmitterDensity(hit, source, surface.Position));
	return DiffuseReflectance(surface) * source.Emission * weight;
}

vec3 DirectDiffuse(LSurface surface, inout uint seed)
{
	if (!surface.Valid || surface.Unlit || !any(greaterThan(DiffuseReflectance(surface), vec3(0)))) return vec3(0);
	vec3 diffuse, specular, punctualDiffuse, punctualSpecular;
	float diffuseSamples = DIA.Emitters.x > 0.0 ? 1.0 : 0.0;
	Direct(surface, surface.Normal, diffuse, specular, punctualDiffuse, punctualSpecular, 0.0, diffuseSamples, 1e30, false, seed);
	if (diffuseSamples > 0.0) diffuse += DiffuseEmission(surface, seed);
	return diffuse;
}

vec3 CachedIndirect(LHit hit, LSurface surface, inout uint seed)
{
	LInstance instance = Instances[hit.Instance];
	float r = max(1.0 - hit.Bary.x, 1e-8);
	LTriangle triangle = Triangles[hit.Triangle];
	vec3 geometric = normalize(cross(mat3(instance.Transform) * (triangle.Position[1].xyz - triangle.Position[0].xyz), mat3(instance.Transform) * (triangle.Position[2].xyz - triangle.Position[0].xyz)));
	if (!surface.Valid) return vec3(0);
	if (instance.Metadata.w == 0u) return vec3(0);
	uint grid = instance.Metadata.w;
	uint sides = Materials[instance.Metadata.x].Flags.x != 0u ? 2u : 1u;
	uint side = dot(geometric, surface.GeometricNormal) < 0.0 ? 1u : 0u;
	vec2 coordinate = vec2(r * r, hit.Bary.y / r) * float(grid) - 0.5;
	ivec2 base = ivec2(floor(coordinate));
	vec2 blend = fract(coordinate);
	vec3 cachedIndirect = vec3(0);
	float weightSum = 0.0;
	for (int neighbor = 0; neighbor < 4; ++neighbor)
	{
		ivec2 offset = ivec2(neighbor % 2, neighbor / 2);
		ivec2 cell = clamp(base + offset, ivec2(0), ivec2(grid - 1u));
		uint slot = instance.Geometry.z + (triangle.Source.x * sides + side) * grid * grid + uint(cell.y * int(grid) + cell.x);
		vec4 cached = texelFetch(SurfaceCache, ivec2(slot % 256u, slot / 256u), 0);
		if (DIA.History.z < 0.5 || cached.a != float(instance.Geometry.w)) continue;
		float sampleRoot = sqrt((float(cell.x) + 0.5) / float(grid));
		float sampleV = (float(cell.y) + 0.5) / float(grid);
		vec3 sampleBary = vec3(1.0 - sampleRoot, sampleV * sampleRoot, (1.0 - sampleV) * sampleRoot);
		vec3 samplePosition = (instance.Transform * vec4(triangle.Position[0].xyz * sampleBary.x + triangle.Position[1].xyz * sampleBary.y + triangle.Position[2].xyz * sampleBary.z, 1)).xyz;
		vec3 connection = samplePosition - surface.Position;
		float connectionLength = length(connection);
		bool reusable = connectionLength < 0.0004;
		if (!reusable) reusable = Trace(surface.Position + surface.GeometricNormal * 0.0002, connection / connectionLength, connectionLength - 0.0004, true).Kind == 3u;
		if (!reusable) continue;
		float weight = (offset.x == 0 ? 1.0 - blend.x : blend.x) * (offset.y == 0 ? 1.0 - blend.y : blend.y);
		cachedIndirect += cached.rgb * weight;
		weightSum += weight;
	}
	if (weightSum > 0.0) return cachedIndirect / weightSum;
	vec3 fallback = vec3(0);
	for (int sampleIndex = 0; sampleIndex < 4; ++sampleIndex)
	{
		vec3 direction = CosineDirection(surface.Normal, seed);
		if (dot(direction, surface.GeometricNormal) <= 0.0) continue;
		LHit next = Trace(Offset(surface, direction), direction, DIA.Settings.x, false);
		if (next.Kind == 2u) fallback += DIA.Sky.rgb * 0.25;
		else if (next.Kind == 0u)
		{
			LSurface nextSurface = Surface(next, direction);
			vec3 nextDiffuse = DirectDiffuse(nextSurface, seed);
			if (nextSurface.Valid) fallback += ((DIA.Emitters.x > 0.0 ? vec3(0) : nextSurface.Emission) + nextDiffuse) * 0.25;
		}
	}
	return DiffuseReflectance(surface) * surface.AO * fallback;
}

vec3 CachedDiffuse(LHit hit, vec3 incoming, inout uint seed)
{
	if (Instances[hit.Instance].Metadata.w == 0u) return vec3(0);
	LSurface surface = Surface(hit, incoming);
	if (!surface.Valid) return vec3(0);
	return DirectDiffuse(surface, seed) + CachedIndirect(hit, surface, seed);
}

vec3 Incoming(vec3 origin, vec3 direction, out LHit hit, bool includeEmission, inout uint seed, out vec3 exact)
{
	exact = vec3(0);
	hit = Trace(origin, direction, DIA.Settings.x, false);
	if (hit.Kind == 2u) return DIA.Sky.rgb;
	if (hit.Kind != 0u) return vec3(0);
	LSurface surface = Surface(hit, direction);
	if (!surface.Valid) return vec3(0);
	vec3 direct, specular;
	vec3 punctualDiffuse, punctualSpecular;
	bool roulette = !includeEmission;
#ifdef DIA_NATIVE_SECONDARY_FOUR
	float reflectionSamples = includeEmission && !surface.Unlit && surface.Metallic >= 1.0 && surface.Roughness > 0.02 ? 4.0 : 1.0;
#else
	float reflectionSamples = 1.0;
#endif
	float diffuseSamples = DIA.Emitters.x > 0.0 && !surface.Unlit && any(greaterThan(DiffuseReflectance(surface), vec3(0))) ? 1.0 : 0.0;
	Direct(surface, -direction, direct, specular, punctualDiffuse, punctualSpecular, reflectionSamples, diffuseSamples, 1e30, roulette, seed);
	if (diffuseSamples > 0.0) direct += DiffuseEmission(surface, seed);
	vec3 emission = includeEmission || DIA.Emitters.x == 0.0 ? surface.Emission : vec3(0);
	exact = emission + punctualDiffuse + punctualSpecular;
	vec3 reflectedDirection, weight;
	if (reflectionSamples > 1.0)
	{
		for (uint sampleIndex = 0u; sampleIndex < 4u; ++sampleIndex)
		{
			vec2 xi;
			xi.x = Random(seed);
			xi.y = Random(seed);
			xi = min((vec2(sampleIndex & 1u, sampleIndex >> 1u) + xi) * 0.5, vec2(0.99999994));
			if (!SampleSpecular(surface, -direction, xi, reflectedDirection, weight)) continue;
			float probability = roulette ? clamp(max(weight.r, max(weight.g, weight.b)), 0.0, 1.0) : 1.0;
			if (Random(seed) >= probability) continue;
			weight /= probability * 4.0;
			LHit continuation = Trace(Offset(surface, reflectedDirection), reflectedDirection, DIA.Settings.x, false);
			if (continuation.Kind == 2u) specular += DIA.Sky.rgb * weight;
			else if (continuation.Kind == 0u)
			{
				LSurface next = Surface(continuation, reflectedDirection);
				if (next.Valid) specular += (next.Emission * SpecularSourceWeight(surface, -direction, reflectedDirection, continuation, next, 4.0, roulette) + CachedDiffuse(continuation, reflectedDirection, seed)) * weight;
			}
		}
	}
	else
	{
		if (SampleSpecular(surface, -direction, seed, reflectedDirection, weight))
		{
			float probability = roulette ? clamp(max(weight.r, max(weight.g, weight.b)), 0.0, 1.0) : 1.0;
			if (Random(seed) < probability)
			{
				weight /= probability;
				LHit continuation = Trace(Offset(surface, reflectedDirection), reflectedDirection, DIA.Settings.x, false);
				if (continuation.Kind == 2u) specular += DIA.Sky.rgb * weight;
				else if (continuation.Kind == 0u)
				{
					LSurface next = Surface(continuation, reflectedDirection);
					if (next.Valid) specular += (next.Emission * SpecularSourceWeight(surface, -direction, reflectedDirection, continuation, next, 1.0, roulette) + CachedDiffuse(continuation, reflectedDirection, seed)) * weight;
				}
			}
		}
	}
	return emission + (direct + CachedIndirect(hit, surface, seed)) + specular;
}

vec3 Incoming(vec3 origin, vec3 direction, out LHit hit, bool includeEmission, inout uint seed)
{
	vec3 exact;
	return Incoming(origin, direction, hit, includeEmission, seed, exact);
}
#endif
