#version 450

layout(set = 2, binding = 0) uniform sampler2D SceneColor;
layout(set = 2, binding = 1) uniform sampler2D LensLight;
layout(set = 2, binding = 2) uniform sampler2D LensWarp;
layout(location = 0) in vec2 InUV;
layout(location = 0) out vec4 OutColor;

void main()
{
	vec3 light = texture(LensLight, InUV).rgb;
	vec3 warp = texture(LensWarp, InUV).xyz;
	vec2 uv = InUV + warp.xy / max(warp.z, 0.00001);
	OutColor = vec4(light + texture(SceneColor, uv).rgb * warp.z, 1.0);
}
