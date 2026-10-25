#version 450
#extension GL_GOOGLE_include_directive : require
#define DIA_TRACE_RESOURCES
#define DIA_NATIVE_SECONDARY_FOUR
#include "DIAShared.glsl"
layout(location = 0) in vec2 InUV;
layout(location = 0) out vec4 OutDiffuse;
layout(location = 1) out vec4 OutSpecular;
layout(location = 2) out vec4 OutPosition;
layout(location = 3) out vec4 OutNormal;
layout(location = 4) out vec4 OutExact;
layout(location = 5) out vec4 OutReflected;

#include "DIAPrimary.glsl"

void main()
{
	ShadePrimary(InUV, DIA.Settings.z, 4u, 3u, OutDiffuse, OutSpecular, OutPosition, OutNormal, OutExact, OutReflected);
}
