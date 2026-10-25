// Shared layouts. Bindings follow SDL_GPU rules: vertex uniforms in set 1,
// fragment textures in set 2 and fragment uniforms in set 3.

// Each stage declares only its own blocks so Vulkan never sees resources outside SDL's sets.
// Mesh.vert and SkinnedMesh.vert define FP_VERTEX_SHADER before including this file.
#ifdef FP_VERTEX_SHADER

// Per draw data for Mesh.vert.
layout(set = 1, binding = 0) uniform ObjectData
{
	mat4 ViewProjection;
	mat4 Model;
	mat4 NormalMatrix;
	vec4 UVTransform; // xy tiling, zw offset
	vec4 Tint;        // linear
} Object;

#ifdef FP_SKINNED
// Bone matrices for SkinnedMesh.vert, vertex uniform slot 1. Matches MaxSkinBones in C++.
#define MAX_BONES 128
layout(set = 1, binding = 1) uniform BoneData
{
	mat4 Matrices[MAX_BONES];
} Bones;
#endif

#else

// Material values for the base shaders. Colors are linear.
layout(set = 3, binding = 0) uniform MaterialData
{
	vec4 BaseColor;
	vec4 Emissive;   // rgb already multiplied by intensity
	vec4 Subsurface; // rgb color, a strength
	vec4 Params;     // x metallic, y roughness, z specular, w normal strength
	vec4 Params2;    // x ambient occlusion strength, y alpha cutoff, z index of refraction, w thickness
} Material;

// Per frame lighting, fragment uniform slot 1.
#define MAX_LIGHTS 16
layout(set = 3, binding = 1) uniform FrameData
{
	vec4 CameraPosition; // w is 1 for orthographic cameras
	vec4 CameraForward;
	vec4 CameraRight;
	vec4 CameraUp;
	vec4 Ambient;        // rgb already multiplied by intensity
	vec4 ScreenSize;     // xy pixels, zw one over pixels
	ivec4 LightCount;
	vec4 LightPosition[MAX_LIGHTS]; // xyz position, or travel direction. w: 0 directional, 1 point, 2 spot
	vec4 LightColor[MAX_LIGHTS];    // rgb already multiplied by intensity, w range
	vec4 LightDirection[MAX_LIGHTS]; // spot cone axis
	vec4 LightCone[MAX_LIGHTS];      // x cos(inner), y cos(outer)
	mat4 ShadowMatrix[8];            // directional, spot, six point faces
	vec4 ShadowParams[3];            // directional, spot, point: strength, texel, offset, light index
} Frame;

#endif
