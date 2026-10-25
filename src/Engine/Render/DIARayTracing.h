#pragma once

#include "Core/CoreMinimal.h"
#include <SDL3/SDL_gpu.h>
#include <memory>

namespace FP
{
	class GPUDevice;
	struct RayTriangle;
	struct DIAInstance;
	struct DIAMaterial;
	struct DIATraceTiming
	{
		uint32 Frame = 0;
		double Milliseconds = 0;
	};

	class DIARayTracing
	{
	public:
		explicit DIARayTracing(GPUDevice& device);
		~DIARayTracing();
		bool IsSupported() const;
		bool Update(SDL_GPUCommandBuffer* cmd, const Array<RayTriangle>& triangles, const Array<DIAInstance>& instances,
			const Array<DIAMaterial>& materials, uint64 revision, usize budget);
		bool Bind(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass);
		usize GetMemoryBytes() const;
		uint64 GetBuildCount() const;
		bool BeginTrace(SDL_GPUCommandBuffer* cmd, uint32 frame);
		bool EndTrace(SDL_GPUCommandBuffer* cmd);
		void AbortTrace();
		Array<DIATraceTiming> GetTraceTimings() const;
		const String& GetError() const { return m_Error; }

	private:
		struct State;
		std::unique_ptr<State> m_State;
		String m_Error;
	};
}
