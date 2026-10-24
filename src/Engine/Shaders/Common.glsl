// Shared layouts. Bindings follow SDL_GPU rules: vertex uniforms in set 1,
// fragment textures in set 2 and fragment uniforms in set 3.

// Per draw data for Mesh.vert.
layout(set = 1, binding = 0) uniform ObjectData
{
	mat4 ViewProjection;
	mat4 Model;
	mat4 NormalMatrix;
	vec4 UVTransform; // xy tiling, zw offset
	vec4 Tint;        // linear
} Object;

// Material values for the base shaders. Colors are linear.
layout(set = 3, binding = 0) uniform MaterialData
{
	vec4 BaseColor;
	vec4 Emissive;   // rgb already multiplied by intensity
	vec4 Subsurface; // rgb color, a strength
	vec4 Params;     // x metallic, y roughness, z specular, w normal strength
	vec4 Params2;    // x ambient occlusion strength, y alpha cutoff
} Material;
