#pragma once

#include "Engine/Render/DebugLines.h"
#include "Engine/Render/GPUDevice.h"
#include "Engine/Render/RenderView.h"
#include "Engine/Render/TextureCache.h"

namespace FP
{
	class Material;

	// Vertex layout shared by sprites and meshes. Matches Mesh.vert.
	struct MeshVertex
	{
		Vec3 Position;
		Vec3 Normal;
		Vec2 UV;
		Vec4 Tangent; // w is the bitangent sign
	};

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
		Vec4 Ambient;
		Vec4 ScreenSize;
		int32 LightCount[4] = {};
		Vec4 LightPosition[MaxLights];
		Vec4 LightColor[MaxLights];
	};

	// Editors pick colors in sRGB. Shaders work in linear.
	Vec4 ToLinear(const Color& color);

	// Draws a World with SDL_GPU. Opaque and alpha blended objects go into an HDR scene buffer,
	// glass then refracts a copy of it, and a present pass tonemaps into the target.
	class Renderer
	{
	public:
		Renderer(GPUDevice& device, SDL_GPUTextureFormat targetFormat);
		~Renderer();

		Renderer(const Renderer&) = delete;
		Renderer& operator=(const Renderer&) = delete;

		bool IsValid() const { return m_UnlitPipeline && m_LitPipeline && m_GlassPipeline && m_PresentPipeline && m_LinePipeline; }

		// Records the frame into cmd. The caller submits it.
		void Render(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* target, uint32 width, uint32 height,
			World& world, const RenderView& view, const DebugLines* lines = nullptr);

		TextureCache& GetTextures() { return m_Textures; }
		GPUDevice& GetDevice() { return m_Device; }

	private:
		enum class ShaderKind : uint8
		{
			Unlit,
			Lit,
			Glass,
		};

		struct Draw
		{
			int32 Layer;
			float Depth;
			Entity Value;
			ShaderKind Kind;
			const Material* Mat;
		};

		void CreatePipelines();
		SDL_GPUGraphicsPipeline* CreateMeshPipeline(const char* fragment, uint32 numSamplers, uint32 numUniforms, bool blend);
		void EnsureTargets(uint32 width, uint32 height);
		void UploadLines(SDL_GPUCommandBuffer* cmd, const DebugLines& lines);
		void BuildFrameUniforms(World& world, const RenderView& view, uint32 width, uint32 height);
		void DrawSprite(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass, World& world, const Draw& draw, const Mat4& viewProjection);
		GPUTexture* Texture(const String& path, bool srgb, GPUTexture* fallback);

		GPUDevice& m_Device;
		SDL_GPUTextureFormat m_TargetFormat;
		TextureCache m_Textures;
		FrameUniforms m_Frame;

		SDL_GPUGraphicsPipeline* m_UnlitPipeline = nullptr;
		SDL_GPUGraphicsPipeline* m_LitPipeline = nullptr;
		SDL_GPUGraphicsPipeline* m_GlassPipeline = nullptr;
		SDL_GPUGraphicsPipeline* m_PresentPipeline = nullptr;
		SDL_GPUGraphicsPipeline* m_LinePipeline = nullptr;

		RefPtr<GPUBuffer> m_QuadVertices;
		RefPtr<GPUBuffer> m_QuadIndices;
		RefPtr<GPUBuffer> m_LineVertices;
		RefPtr<GPUTexture> m_SceneColor;
		RefPtr<GPUTexture> m_SceneCopy;
		RefPtr<GPUTexture> m_Depth;
	};
}
