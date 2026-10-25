#pragma once

#include "Engine/Render/GPUDevice.h"
#include "Engine/Math.h"

namespace FP
{
	Array<Vec4> CaptureLinear(GPUDevice& device, GPUTexture& source);
	bool SaveLinearPFM(const char* path, const Array<Vec4>& pixels, uint32 width, uint32 height);
	bool SaveLinearRGBA(const char* path, const Array<Vec4>& pixels, uint32 width, uint32 height);
}
