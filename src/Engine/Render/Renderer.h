#pragma once

#include "Engine/Render/DebugLines.h"
#include "Engine/Render/GPUDevice.h"
#include "Engine/Render/MeshData.h"
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
		Mat4 ShadowMatrix; // world to shadow map UV and depth
		Vec4 ShadowParams; // x strength (0 is off), y one texel in UV, z normal offset in world units, w light index
	};

	// Editors pick colors in sRGB. Shaders work in linear.
	Vec4 ToLinear(const Color& color);

	// Draws a World with SDL_GPU into an HDR scene buffer. The first shadowed directional light
	// renders a small depth map of the meshes near the camera first. Opaque meshes go first with depth,
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

		// Records the frame into cmd. The caller submits it.
		void Render(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* target, uint32 width, uint32 height,
			World& world, const RenderView& view, const DebugLines* lines = nullptr);

		// Loads every texture the world's sprites and materials use, decoding them in parallel.
		// Call after loading a scene so the first frame does not stall on image decoding.
		void PreloadTextures(World& world);

		TextureCache& GetTextures() { return m_Textures; }
		MeshLibrary& GetMeshes() { return m_Meshes; }
		GPUDevice& GetDevice() { return m_Device; }

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
		};

		void CreatePipelines();
		SDL_GPUGraphicsPipeline* CreateMeshPipeline(const char* fragment, uint32 numSamplers, uint32 numUniforms, Surface surface, bool glass, bool wireframe = false);
		SDL_GPUGraphicsPipeline* GetPipeline(const Draw& draw) const;
		void EnsureTargets(uint32 width, uint32 height);
		void UploadLines(SDL_GPUCommandBuffer* cmd, const DebugLines& lines);
		void BuildFrameUniforms(World& world, const RenderView& view, uint32 width, uint32 height);
		void DrawObject(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass, World& world, const Draw& draw, const Mat4& viewProjection);
		void PushBones(SDL_GPUCommandBuffer* cmd, const Array<Mat4>* skin);
		SDL_GPUGraphicsPipeline* CreateShadowPipeline(Surface surface);
		void RenderShadows(SDL_GPUCommandBuffer* cmd, World& world, const RenderView& view);
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

		// Indexed by ShaderKind then Surface.
		SDL_GPUGraphicsPipeline* m_ScenePipelines[3][3] = {};

		// Line fill versions of Unlit for the wireframe view mode, indexed by Surface.
		// Optional so a GPU without line fill still renders the other modes.
		SDL_GPUGraphicsPipeline* m_WirePipelines[3] = {};
		ViewMode m_Mode = ViewMode::Lit;
		SDL_GPUGraphicsPipeline* m_PresentPipeline = nullptr;
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
		Array<Mat4> m_BoneUpload; // always MaxSkinBones long, the size the shader reads

		// Depth only pipelines for Mesh and SkinnedMesh. Missing ones just mean no shadows.
		SDL_GPUGraphicsPipeline* m_ShadowPipelines[2] = {};
		RefPtr<GPUTexture> m_ShadowMap;
		bool m_ShadowMapCleared = false;
		Entity m_ShadowCaster = NullEntity; // the light casting shadows this frame
		int32 m_ShadowLight = -1;           // its index in FrameUniforms
	};
}
