#pragma once

#include "Engine/Render/DebugLines.h"
#include "Engine/Render/GPUDevice.h"
#include "Engine/Render/EnvironmentMap.h"
#include "Engine/Render/MeshData.h"
#include "Engine/Render/MeshletCulling.h"
#include "Engine/Render/MeshLibrary.h"
#include "Engine/Render/RenderView.h"
#include "Engine/Render/ShadowMap.h"
#include "Engine/Render/TextureCache.h"

namespace FP
{
	class Material;

	// Uniform blocks. Layouts match Common.glsl (std140).
	struct ObjectUniforms
	{
		Mat4 ViewProjection;
		Mat4 Model;
		Mat4 NormalMatrix;
		Vec4 UVTransform{ 1, 1, 0, 0 };
		Vec4 Tint{ 1, 1, 1, 1 };
	};

	struct MaterialUniforms
	{
		Vec4 BaseColor{ 1, 1, 1, 1 };
		Vec4 Emissive{ 0, 0, 0, 0 };
		Vec4 Subsurface{ 0, 0, 0, 0 };
		Vec4 Params{ 0, 0.5f, 0.5f, 1 };
		Vec4 Params2{ 1, 0.01f, 1.5f, 0.5f };
	};

	struct RenderStats
	{
		uint32 SceneDrawCalls = 0;
		uint32 SceneInstances = 0;
		uint32 InstancedDrawCalls = 0;
		uint32 IndirectCommands = 0;
		uint32 ShadowDrawCalls = 0;
		uint32 OcclusionDrawCalls = 0;
	};

	constexpr int32 MaxLights = 16;

	struct FrameUniforms
	{
		Vec4 CameraPosition;
		Vec4 CameraForward;
		Vec4 CameraRight;
		Vec4 CameraUp;
		Vec4 Ambient;
		Vec4 ScreenSize;
		int32 LightCount[4] = {};
		Vec4 LightPosition[MaxLights];
		Vec4 LightColor[MaxLights];
		Vec4 LightDirection[MaxLights]; // spot axis
		Vec4 LightCone[MaxLights]; // x cos(inner), y cos(outer)
		Mat4 ShadowMatrix[8]; // directional, spot, then six point faces
		Vec4 ShadowParams[3]; // x strength, y texel in UV, z normal offset, w light index
		Vec4 EnvironmentParams; // x intensity, y rotation, z visible
		Vec4 EnvironmentSize; // width, height, level count
		Vec4 EnvironmentDiffuse[9];
	};

	// Editors pick colors in sRGB. Shaders work in linear.
	Vec4 ToLinear(const Color& color);

	// Draws a World with SDL_GPU into an HDR scene buffer. The first shadowed light of each type
	// renders depth maps before the scene. Opaque meshes go first with depth,
	// then sprites blend on top by layer, then glass refracts a copy of the scene, and a present
	// pass tonemaps into the target.
	class Renderer
	{
	public:
		Renderer(GPUDevice& device, SDL_GPUTextureFormat targetFormat);
		~Renderer();

		Renderer(const Renderer&) = delete;
		Renderer& operator=(const Renderer&) = delete;

		bool IsValid() const;

		// Records the frame into cmd. The caller submits it. An optional cullingView lets
		// an editor observe the results from a wider or independent viewpoint.
		void Render(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* target, uint32 width, uint32 height,
			World& world, const RenderView& view, const DebugLines* lines = nullptr, const RenderView* cullingView = nullptr);

		// Loads every texture the world's sprites and materials use, decoding them in parallel.
		// Call after loading a scene so the first frame does not stall on image decoding.
		void PreloadTextures(World& world);

		void SetBatchingEnabled(bool enabled) { m_BatchingEnabled = enabled; }
		bool IsBatchingEnabled() const { return m_BatchingEnabled; }
		const RenderStats& GetRenderStats() const { return m_RenderStats; }

		TextureCache& GetTextures() { return m_Textures; }
		MeshLibrary& GetMeshes() { return m_Meshes; }
		GPUDevice& GetDevice() { return m_Device; }
		MeshletSettings& GetMeshletSettings() { return m_MeshletSettings; }
		const MeshletStats& GetMeshletStats() { return m_Culling.ReadStats(); }
		bool HasMeshletCulling() const { return m_Culling.IsValid() && m_OcclusionPipeline; }
		uint32 GetActiveMeshlets() const { return m_ActiveMeshlets; }
		const RenderView* GetFrozenMeshletView() const { return m_MeshletSettings.FreezeCamera && m_HasCullingView ? &m_CullingView : nullptr; }

	private:
		enum class ShaderKind : uint8
		{
			Unlit,
			Lit,
			Glass,
		};

		// Sprites blend and sort by layer. Meshes write depth and cull back faces.
		// Skinned meshes are meshes that also follow bones.
		enum class Surface : uint8
		{
			Sprite,
			Mesh,
			SkinnedMesh,
		};

		struct Draw
		{
			int32 Layer;
			float Depth;
			Entity Value;
			ShaderKind Kind;
			Surface Type;
			const Material* Mat;
			const GPUMesh* Mesh;
			uint32 FirstIndex;
			uint32 IndexCount;
			const Array<Mat4>* Skin; // bone matrices for skinned meshes, null for the bind pose
			int32 IndirectFirst = -1;
			uint32 MeshletCount = 0;
			int32 InstanceIndex = -1;
		};

		void CreatePipelines();
		SDL_GPUGraphicsPipeline* CreateFullscreenPipeline(const char* fragment, uint32 samplers, SDL_GPUTextureFormat format);
		SDL_GPUGraphicsPipeline* CreateMeshPipeline(const char* fragment, uint32 numSamplers, uint32 numUniforms, Surface surface, bool glass, bool wireframe = false, bool instanced = false);
		SDL_GPUGraphicsPipeline* GetPipeline(const Draw& draw) const;
		SDL_GPUGraphicsPipeline* GetInstancedPipeline(const Draw& draw) const;
		ObjectUniforms MakeObjectUniforms(World& world, const Draw& draw, const Mat4& viewProjection) const;
		void PrepareInstances(SDL_GPUCommandBuffer* cmd, World& world, Array<Draw>& draws);
		bool CanBatch(World& world, const Draw& a, const Draw& b) const;
		void EnsureTargets(uint32 width, uint32 height);
		void UploadLines(SDL_GPUCommandBuffer* cmd, const DebugLines& lines);
		void PrepareEnvironment(World& world);
		void DrawSkybox(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass, const RenderView& view);
		void BuildFrameUniforms(World& world, const RenderView& view, uint32 width, uint32 height);
		void DrawObject(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass, World& world, const Draw& draw, const Mat4& viewProjection, uint32 instances = 1, uint32 commands = 0);
		void PushBones(SDL_GPUCommandBuffer* cmd, const Array<Mat4>* skin);
		SDL_GPUGraphicsPipeline* CreateShadowPipeline(Surface surface, bool occlusion = false, bool instanced = false);
		void PrepareMeshlets(SDL_GPUCommandBuffer* cmd, World& world, const RenderView& view, Array<Draw>& draws);
		void RenderShadows(SDL_GPUCommandBuffer* cmd, World& world, const RenderView& view, const Array<Draw>& draws);
		void DrawDepth(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass, World& world, const Draw& draw,
			const Mat4& viewProjection, SDL_GPUGraphicsPipeline* pipeline, bool instanced, uint32 instances);
		bool CanBatchDepth(const Draw& a, const Draw& b) const;
		void CreateParticlePipelines();
		// Builds camera facing quads for every live particle and uploads them before the scene pass.
		void PrepareParticles(SDL_GPUCommandBuffer* cmd, World& world, const RenderView& view);
		void DrawParticles(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass, const Mat4& viewProjection);
		// Uploads the UI pages on screen and returns their textures in draw order.
		Array<GPUTexture*> PrepareUI(SDL_GPUCommandBuffer* cmd, World& world, const RenderView& view, uint32 width, uint32 height);
		GPUTexture* Texture(const String& path, bool srgb, GPUTexture* fallback);

		GPUDevice& m_Device;
		SDL_GPUTextureFormat m_TargetFormat;
		TextureCache m_Textures;
		MeshLibrary m_Meshes;
		FrameUniforms m_Frame;
		MeshletCulling m_Culling;
		MeshletSettings m_MeshletSettings;
		RenderView m_CullingView;
		bool m_HasCullingView = false;
		uint32 m_ActiveMeshlets = 0;
		SDL_GPUGraphicsPipeline* m_OcclusionPipeline = nullptr;
		SDL_GPUGraphicsPipeline* m_InstancedOcclusionPipeline = nullptr;
		SDL_GPUGraphicsPipeline* m_InstancedShadowPipeline = nullptr;
		RefPtr<GPUTexture> m_CullDepth;

		// Indexed by ShaderKind then Surface.
		SDL_GPUGraphicsPipeline* m_ScenePipelines[3][3] = {};
		SDL_GPUGraphicsPipeline* m_InstancedPipelines[3][2] = {};
		SDL_GPUGraphicsPipeline* m_InstancedWirePipelines[2] = {};
		RefPtr<GPUBuffer> m_Instances;
		bool m_BatchingEnabled = true;
		RenderStats m_RenderStats;

		// Line fill versions of Unlit for the wireframe view mode, indexed by Surface.
		// Optional so a GPU without line fill still renders the other modes.
		SDL_GPUGraphicsPipeline* m_WirePipelines[3] = {};
		ViewMode m_Mode = ViewMode::Lit;
		SDL_GPUGraphicsPipeline* m_PresentPipeline = nullptr;
		SDL_GPUGraphicsPipeline* m_PostPipeline = nullptr;
		SDL_GPUGraphicsPipeline* m_FXAAPipeline = nullptr;
		SDL_GPUGraphicsPipeline* m_BloomPipeline = nullptr;
		SDL_GPUGraphicsPipeline* m_SkyboxPipeline = nullptr;
		EnvironmentMap m_Environment;
		RefPtr<GPUBuffer> m_EnvironmentBuffer;
		RefPtr<GPUBuffer> m_EmptyEnvironment;
		String m_EnvironmentPath;
		int64 m_EnvironmentModified = 0;
		SDL_GPUGraphicsPipeline* m_UIPipeline = nullptr; // optional, no UI without it

		// One texture per UI page, refilled when the page's version changes.
		struct UITexture
		{
			uint32 Entity = 0;
			RefPtr<GPUTexture> Texture;
			uint64 Version = 0;
		};
		Array<UITexture> m_UITextures;
		SDL_GPUGraphicsPipeline* m_LinePipeline = nullptr;

		// Particles test depth without writing it. Indexed by blend: alpha then additive.
		// Optional, so a missing shader only loses particles.
		SDL_GPUGraphicsPipeline* m_ParticlePipelines[2] = {};
		struct ParticleBatch
		{
			GPUTexture* Texture; // null draws the soft dot
			bool Additive;
			uint32 First;
			uint32 Count;
		};
		Array<ParticleBatch> m_ParticleBatches;
		RefPtr<GPUBuffer> m_ParticleVertices;

		RefPtr<GPUBuffer> m_QuadVertices;
		RefPtr<GPUBuffer> m_QuadIndices;
		RefPtr<GPUBuffer> m_LineVertices;
		RefPtr<GPUTexture> m_SceneColor;
		RefPtr<GPUTexture> m_SceneCopy;
		RefPtr<GPUTexture> m_Depth;
		RefPtr<GPUTexture> m_PostColor;
		RefPtr<GPUTexture> m_Bloom[2];
		Array<Mat4> m_BoneUpload; // always MaxSkinBones long, the size the shader reads

		// Depth only pipelines for Mesh and SkinnedMesh. Missing ones just mean no shadows.
		SDL_GPUGraphicsPipeline* m_ShadowPipelines[2] = {};
		RefPtr<GPUTexture> m_ShadowMaps[8];
		bool m_ShadowMapCleared[8] = {};
		Entity m_ShadowCasters[3] = { NullEntity, NullEntity, NullEntity };
		int32 m_ShadowLights[3] = { -1, -1, -1 };
	};
}
