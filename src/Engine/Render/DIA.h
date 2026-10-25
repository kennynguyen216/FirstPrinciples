#pragma once

#include "Engine/Render/MeshLibrary.h"
#include "Engine/Material.h"
#include "Engine/Render/DIARayTracing.h"

namespace FP
{
	struct DIAMaterial
	{
		Vec4 BaseColor;
		Vec4 Emission;
		Vec4 Parameters;
		Vec4 Parameters2;
		Vec4 UVTransform;
		uint32 Textures[8][4] = {};
		uint32 Flags[4] = {};
	};

	struct DIALight
	{
		Vec4 Position;
		Vec4 ColorRange;
		Vec4 DirectionInner;
		Vec4 OuterRadius;
	};

	struct DIAInstance
	{
		Mat4 Transform;
		Mat4 Inverse;
		uint32 Geometry[4] = {};
		uint32 Metadata[4] = {};
		uint32 Identity[4] = {};
		Vec4 Tint{ 1, 1, 1, 1 };
	};

	struct DIASurface
	{
		uint32 Owner = 0xffffffffu;
		uint32 Triangle = 0;
		uint32 Generation = 0;
		uint32 Sample = 0;
	};

	struct DIAUniforms
	{
		Mat4 InverseViewProjection;
		Mat4 PreviousViewProjection;
		Vec4 Camera;
		Vec4 Sky;
		Vec4 Extent;
		uint32 Counts[4] = {};
		uint32 Frame[4] = {};
		Vec4 Settings;
		Vec4 History;
		Vec4 Emitters;
		Vec4 PreviousCamera;
		Vec4 CameraForward;
		Vec4 PreviousForward;
	};

	struct DIAEmitter
	{
		uint32 Identity[4] = {};
		uint32 Geometry[4] = {};
		Vec4 Distribution;
	};

	class DIA
	{
	public:
		explicit DIA(GPUDevice& device);
		~DIA();
		bool Render(SDL_GPUCommandBuffer* cmd, World& world, MeshLibrary& meshes, const RenderView& view);
		bool Resolve(SDL_GPUCommandBuffer* cmd, GPUTexture& target);
		GPUTexture* GetOutput() const { return m_Views[m_ActiveView].Output.Get(); }
		const String& GetError() const { return m_Error; }
		const RayScene& GetRayScene() const { return m_Scene; }
		GPUTexture* GetDiagnostic(uint32 index) const { return index < 6 ? m_Raw[index].Get() : nullptr; }
		GPUTexture* GetCacheDiagnostic() const { return m_Cache[(m_Frame - 1) % 2].Get(); }
		GPUTexture* GetHistoryCountsDiagnostic() const { const auto& view = m_Views[m_ActiveView]; return view.Frame ? view.History[(view.Frame - 1) % 2][5].Get() : nullptr; }
		uint32 GetMeshBuildCount() const { return m_MeshBuildCount; }
		uint32 GetSceneUploadCount() const { return m_SceneUploadCount; }
		uint32 GetFrameCount() const { return m_Frame; }
		const char* GetRayBackend() const { return m_UseHardware ? "hardware" : "software"; }
		uint64 GetAccelerationBuildCount() const { return m_Hardware.GetBuildCount(); }
		Array<DIATraceTiming> GetTraceTimings() const { return m_Hardware.GetTraceTimings(); }
		uint32 GetHistoryFrameCount() const { return m_Views[m_ActiveView].Frame; }
		uint32 GetPendingAge() const { return m_PendingAge; }
		double GetUpdateMilliseconds() const { return m_UpdateMilliseconds; }
		uint64 GetSceneRevision() const { return m_SceneRevision; }
		usize GetMemoryBytes() const;
		void SetDebugMode(uint32 mode) { if (m_DebugMode != mode) for (auto& view : m_Views) view.Valid = false; m_DebugMode = mode; }
		Array<Vec4> RenderReference(const RenderView& view, uint32 width, uint32 height, uint32 samples, uint32 seed, uint32 bounces = 8) const;
		Array<Vec4> CaptureGeometry(const RenderView& view, bool reflected = false) const;

	private:
		struct ViewHistory
		{
			uint64 Identity = 0;
			uint64 Epoch = 0;
			uint32 Frame = 0;
			uint32 LastUsed = 0;
			usize DisplayPixels = 0;
			bool Valid = false;
			Vec3 Camera;
			Vec3 Forward;
			Mat4 Projection;
			Mat4 ViewProjection;
			RefPtr<GPUTexture> History[2][6];
			RefPtr<GPUTexture> Output;
		};
		struct MeshAllocation
		{
			String Name;
			RefPtr<RayMesh> Mesh;
			uint32 NodeOffset = 0;
			uint32 TriangleOffset = 0;
			uint32 Generation = 0;
		};
		struct TextureAllocation
		{
			String Path;
			uint32 Offset = 0;
			uint32 Width = 0;
			uint32 Height = 0;
			bool Opaque = true;
		};
		struct SurfaceAllocation
		{
			RayIdentity Identity;
			uint32 Offset = 0;
			uint32 Size = 0;
			uint32 Generation = 0;
			bool Used = false;
			uint32 Grid = 4;
			uint32 Sides = 1;
		};

		bool Extract(World& world, MeshLibrary& meshes, const DIAComponent& settings);
		void Reclaim(World& world, MeshLibrary& meshes);
		bool PackMaterial(const MaterialData& material, const String& spriteTexture, DIAMaterial& packed);
		bool BuildEmitters();
		Vec3 SampleTransportTexture(const uint32* descriptor, Vec2 uv, Vec3 fallback) const;
		bool LoadTexture(const String& path, uint32* descriptor, bool srgb);
		bool Upload(SDL_GPUCommandBuffer* cmd);
		SDL_GPUGraphicsPipeline* CreatePipeline(const char* fragment, uint32 samplers, uint32 buffers, uint32 targets,
			SDL_GPUTextureFormat format = SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT, SDL_GPUCompareOp depthTest = SDL_GPU_COMPAREOP_INVALID, bool depthWrite = false);
		bool RunPass(SDL_GPUCommandBuffer* cmd, SDL_GPUGraphicsPipeline* pipeline, GPUTexture* const* targets, uint32 targetCount,
			GPUTexture* const* sources, uint32 sourceCount, const DIAUniforms& uniforms, GPUTexture* depth = nullptr, bool load = false);
		GPUDevice& m_Device;
		DIARayTracing m_Hardware;
		bool m_UseHardware = false;
		RayScene m_Scene;
		Array<MeshAllocation> m_Meshes;
		Array<TextureAllocation> m_Textures;
		Array<SurfaceAllocation> m_Surfaces;
		Array<RayNode> m_MeshNodes;
		Array<RayTriangle> m_Triangles;
		Array<DIAInstance> m_Instances;
		Array<DIAMaterial> m_Materials;
		Array<DIALight> m_Lights;
		Array<uint32> m_Texels;
		Array<DIASurface> m_SurfaceOwners;
		Array<DIAEmitter> m_Emitters;
		RefPtr<GPUBuffer> m_Buffers[8];
		RefPtr<GPUTexture> m_Cache[2];
		RefPtr<GPUTexture> m_Raw[6];
		RefPtr<GPUTexture> m_Denoised[2][2];
		RefPtr<GPUTexture> m_ResolveMask;
		ViewHistory m_Views[2];
		uint32 m_ActiveView = 0;
		SDL_GPUGraphicsPipeline* m_CachePipeline = nullptr;
		SDL_GPUGraphicsPipeline* m_TracePipeline = nullptr;
		SDL_GPUGraphicsPipeline* m_TraceLitOnePipeline = nullptr;
		SDL_GPUGraphicsPipeline* m_HistoryPipeline = nullptr;
		SDL_GPUGraphicsPipeline* m_DenoisePipeline = nullptr;
		SDL_GPUGraphicsPipeline* m_ComposePipeline = nullptr;
		SDL_GPUGraphicsPipeline* m_ResolvePipeline = nullptr;
		SDL_GPUGraphicsPipeline* m_ResolveFallbackPipeline = nullptr;
		SDL_GPUGraphicsPipeline* m_ResolveFallbackLitPipeline = nullptr;
		DIAUniforms m_Uniforms;
		String m_Error;
		uint64 m_SceneHash = 0;
		uint64 m_SceneRevision = 0;
		uint64 m_UploadedRevision = 0;
		uint32 m_SceneUploadCount = 0;
		uint64 m_WorldGeneration = 0;
		const World* m_World = nullptr;
		uint32 m_Frame = 0;
		uint32 m_CacheCursor = 0;
		uint32 m_CacheAge = 0;
		uint32 m_CacheCount = 0;
		uint32 m_CacheGeneration = 1;
		uint32 m_GeometryGeneration = 0;
		uint32 m_MeshBuildCount = 0;
		uint32 m_PendingAge = 0;
		double m_UpdateMilliseconds = 0;
		uint32 m_DebugMode = 0;
		bool m_GeometryDirty = true;
		bool m_TexturesDirty = true;
		bool m_HistoryValid = false;
		bool m_CacheValid = false;
		uint64 m_EmitterHash = 0;
		float m_EmitterWeight = 0;
		usize m_TextureBudgetBytes = 0;
	};
}
