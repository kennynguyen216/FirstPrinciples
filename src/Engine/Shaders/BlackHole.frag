#version 450
#extension GL_GOOGLE_include_directive : require

// Gravitational lensing by Schwarzschild black holes. Each pixel's light ray is traced backwards
// from the camera through curved spacetime with the exact null geodesic equation
//     u'' + u = 1.5 Rs u^2,   written in Cartesian form as   a = -1.5 Rs h^2 x / r^5,   h = |x cross v|
// so it bends, wraps around the photon sphere (1.5 Rs) or falls through the horizon (Rs).
// Bent rays find the scene by marching the depth buffer, and the skybox by their escape direction.
// The accretion disk is a thin Novikov-Thorne disk from the innermost stable orbit (3 Rs) out.
// Each ring glows as a blackbody, seen through its Doppler shift and gravitational redshift.

#define MAX_HOLES 4
#define MAX_STEPS 320

#define FP_ENVIRONMENT_BINDING 2
#include "Environment.glsl"

layout(set = 2, binding = 0) uniform sampler2D SceneColor;
layout(set = 2, binding = 1) uniform sampler2D SceneDepth;
layout(set = 3, binding = 0) uniform BlackHoleData
{
	mat4 ViewProjection;
	mat4 InverseViewProjection;
	vec4 CameraForward;        // xyz, w 1 for orthographic
	vec4 Background;           // rgb linear, w star brightness
	vec4 Params;               // x hole count, y time
	vec4 Environment;          // x skybox intensity, y rotation, z 1 when a skybox is loaded
	vec4 EnvironmentSize;      // width, height, level count
	vec4 Hole[MAX_HOLES];      // xyz position, w Schwarzschild radius
	vec4 Disk[MAX_HOLES];      // xyz disk normal, w outer radius, 0 for no disk
	vec4 DiskLight[MAX_HOLES]; // x hottest temperature in kelvin, y brightness, z speed of light
} Lens;
layout(location = 0) in vec2 InUV;
layout(location = 0) out vec4 OutColor;

float Hash(vec3 p)
{
	p = fract(p * 0.3183099 + 0.1);
	p *= 17.0;
	return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float ValueNoise(vec3 p)
{
	vec3 i = floor(p);
	vec3 f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	return mix(mix(mix(Hash(i), Hash(i + vec3(1, 0, 0)), f.x), mix(Hash(i + vec3(0, 1, 0)), Hash(i + vec3(1, 1, 0)), f.x), f.y),
		mix(mix(Hash(i + vec3(0, 0, 1)), Hash(i + vec3(1, 0, 1)), f.x), mix(Hash(i + vec3(0, 1, 1)), Hash(i + vec3(1, 1, 1)), f.x), f.y), f.z);
}

// Planck's law at representative red, green and blue wavelengths, in arbitrary units.
vec3 Blackbody(float temperature)
{
	const vec3 lambda = vec3(0.610, 0.550, 0.465); // micrometres
	vec3 x = min(14387.77 / (lambda * max(temperature, 1.0)), vec3(80.0));
	return 1.0 / (lambda * lambda * lambda * lambda * lambda * (exp(x) - 1.0));
}

vec3 Gravity(vec3 x, vec3 v)
{
	vec3 a = vec3(0.0);
	for (int i = 0; i < int(Lens.Params.x); ++i)
	{
		vec3 d = x - Lens.Hole[i].xyz;
		float r2 = dot(d, d);
		vec3 h = cross(d, v);
		a -= 1.5 * Lens.Hole[i].w * dot(h, h) * d / (r2 * r2 * sqrt(r2));
	}
	return a;
}

vec3 Stars(vec3 direction)
{
	// Stars sit in a 3D grid of cells. Neighbours are checked so the sphere cutting a cell
	// at a slant does not clip a star into a sliver.
	const float cells = 160.0; // per radian
	vec3 base = floor(direction * cells);
	vec3 stars = vec3(0.0);
	for (int i = 0; i < 27; ++i)
	{
		vec3 cell = base + vec3(i % 3, (i / 3) % 3, i / 9) - 1.0;
		float h = Hash(cell);
		if (h < 0.995)
			continue;
		vec3 center = cell + 0.5 + 0.4 * (vec3(Hash(cell + 1.3), Hash(cell + 2.7), Hash(cell + 5.1)) - 0.5);
		float distance = length(normalize(center) - direction) * cells;
		float temperature = mix(3000.0, 15000.0, Hash(cell + 9.1) * Hash(cell + 9.1));
		stars += Blackbody(temperature) / Blackbody(temperature).g * smoothstep(0.35, 0.0, distance) * (h - 0.995) * 600.0;
	}
	return stars;
}

// Light emitted where the ray crosses hole i's disk at p, toward the camera along -v.
// Returns rgb emission and coverage in a.
vec4 DiskEmission(int i, vec3 p, vec3 v)
{
	float rs = Lens.Hole[i].w;
	vec3 n = Lens.Disk[i].xyz;
	vec3 d = p - Lens.Hole[i].xyz;
	float r = length(d);
	float inner = 3.0 * rs;
	float outer = Lens.Disk[i].w;
	if (r < inner || r > outer)
		return vec4(0.0);

	// Novikov-Thorne temperature: T ~ r^-3/4 (1 - sqrt(r_in/r))^1/4, peaking near 1.36 r_in.
	float ratio = inner / r;
	float temperature = Lens.DiskLight[i].x * pow(ratio, 0.75) * pow(1.0 - sqrt(ratio), 0.25) / 0.4878;

	// Circular orbit at angular velocity omega around n. The photon's angular momentum about n is
	// conserved along the ray, so 1 + z = u^t (1 - omega L) covers both Doppler and gravity.
	float m = 0.5 * rs;
	float omega = sqrt(m / (r * r * r));
	float angularMomentum = dot(cross(d, -v), n);
	float g = sqrt(1.0 - 1.5 * rs / r) / (1.0 - omega * angularMomentum);

	// Clumps orbit at their own Kepler speed, so the gas shears into spiral streaks over time.
	vec3 e1 = normalize(abs(n.x) < 0.9 ? cross(n, vec3(1, 0, 0)) : cross(n, vec3(0, 1, 0)));
	vec3 e2 = cross(n, e1);
	float angle = atan(dot(d, e2), dot(d, e1)) - omega * Lens.DiskLight[i].z * Lens.Params.y;
	vec3 q = vec3(cos(angle) * r, sin(angle) * r, r * 0.6) / rs;
	float gas = 0.55 * ValueNoise(q * 0.9) + 0.3 * ValueNoise(q * 2.3 + 4.1) + 0.15 * ValueNoise(q * 6.1 + 9.7);
	float coverage = clamp(gas * 2.4 - 0.35, 0.0, 1.0) * smoothstep(outer, mix(inner, outer, 0.6), r);

	// A blackbody seen through shift g is a blackbody at g T. That carries the beaming too.
	vec3 color = Blackbody(g * temperature) / Blackbody(Lens.DiskLight[i].x).g * Lens.DiskLight[i].y;
	return vec4(color * coverage, coverage);
}

void main()
{
	vec2 ndc = vec2(InUV.x * 2.0 - 1.0, 1.0 - InUV.y * 2.0);
	vec4 nearPoint = Lens.InverseViewProjection * vec4(ndc, 0.0, 1.0);
	vec4 farPoint = Lens.InverseViewProjection * vec4(ndc, 1.0, 1.0);
	vec3 x = nearPoint.xyz / nearPoint.w;
	vec3 v = normalize(farPoint.xyz / farPoint.w - x);
	vec3 forward = Lens.CameraForward.xyz;
	float startDepth = dot(x, forward);
	float farDepth = dot(farPoint.xyz / farPoint.w, forward) - startDepth;
	int holes = int(Lens.Params.x);

	vec3 light = vec3(0.0);
	float transmittance = 1.0;
	float depth = 0.0;
	for (int step = 0; step < MAX_STEPS && transmittance > 0.01; ++step)
	{
		// Steps shrink near a hole and grow with distance, so far rays stay cheap.
		float closest = 1e9;
		for (int i = 0; i < holes; ++i)
		{
			float r = length(x - Lens.Hole[i].xyz);
			if (r < Lens.Hole[i].w)
			{
				OutColor = vec4(light, 1.0); // fell through the horizon
				return;
			}
			closest = min(closest, r - 0.5 * Lens.Hole[i].w);
		}
		float dt = 0.07 * closest / length(v);

		// Leapfrog: half kick, drift, half kick.
		vec3 halfV = v + Gravity(x, v) * (0.5 * dt);
		vec3 next = x + halfV * dt;
		v = halfV + Gravity(next, halfV) * (0.5 * dt);

		for (int i = 0; i < holes; ++i)
		{
			if (Lens.Disk[i].w <= 0.0)
				continue;
			float a = dot(x - Lens.Hole[i].xyz, Lens.Disk[i].xyz);
			float b = dot(next - Lens.Hole[i].xyz, Lens.Disk[i].xyz);
			if (a * b < 0.0)
			{
				vec4 disk = DiskEmission(i, mix(x, next, a / (a - b)), v);
				light += transmittance * disk.rgb;
				transmittance *= 1.0 - disk.a;
			}
		}

		// The scene is hit where the ray passes behind what the depth buffer holds.
		float nextDepth = dot(next, forward) - startDepth;
		vec4 clip = Lens.ViewProjection * vec4(next, 1.0);
		if (clip.w > 0.0)
		{
			vec3 screen = clip.xyz / clip.w;
			vec2 uv = vec2(screen.x * 0.5 + 0.5, 0.5 - screen.y * 0.5);
			if (all(greaterThanEqual(uv, vec2(0.0))) && all(lessThanEqual(uv, vec2(1.0))))
			{
				float stored = texture(SceneDepth, uv).r;
				vec4 surface = Lens.InverseViewProjection * vec4(screen.xy, stored, 1.0);
				float surfaceDepth = dot(surface.xyz / surface.w, forward) - startDepth;
				if (stored < 1.0 && nextDepth >= surfaceDepth && depth <= surfaceDepth + 0.02 * surfaceDepth + 0.01)
				{
					OutColor = vec4(light + transmittance * texture(SceneColor, uv).rgb, 1.0);
					return;
				}
			}
		}
		x = next;
		depth = nextDepth;
		if (depth > farDepth || depth < -farDepth)
			break;
	}

	// Escaped to the sky. A skybox is looked up in the bent direction all the way around. Without one,
	// perspective cameras see whatever background lies that way on screen, and stars fill the rest.
	vec3 direction = normalize(v);
	vec3 sky = Lens.Background.rgb;
	if (Lens.Environment.z > 0.5)
	{
		sky = EnvironmentRadiance(EnvironmentDirection(direction, Lens.Environment.y), 0.0, Lens.EnvironmentSize) * Lens.Environment.x;
	}
	else
	{
		vec4 clip = Lens.ViewProjection * vec4(direction, 0.0);
		if (Lens.CameraForward.w < 0.5 && clip.w > 0.0)
		{
			vec2 uv = vec2(clip.x / clip.w * 0.5 + 0.5, 0.5 - clip.y / clip.w * 0.5);
			if (all(greaterThanEqual(uv, vec2(0.0))) && all(lessThanEqual(uv, vec2(1.0))) && texture(SceneDepth, uv).r >= 1.0)
				sky = texture(SceneColor, uv).rgb;
		}
		sky += Stars(direction) * Lens.Background.w;
	}
	OutColor = vec4(light + transmittance * sky, 1.0);
}
