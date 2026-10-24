#pragma once

#include "Engine/Render/DebugLines.h"
#include "Engine/Render/GPUDevice.h"
#include "Engine/Render/RenderView.h"
#include "Engine/Render/TextureCache.h"

namespace FP
{
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
		Vec4 Params2{ 1, 0.01f, 0, 0 };
	};

	// Editors pick colors in sRGB. Shaders work in linear.
	Vec4 ToLinear(const Color& color);

	// Draws a World into a color texture with SDL_GPU. One renderer per target format.
	class Renderer
	{
	public:
		Renderer(GPUDevice& device, SDL_GPUTextureFormat colorFormat);
		~Renderer();

		Renderer(const Renderer&) = delete;
		Renderer& operator=(const Renderer&) = delete;

		bool IsValid() const { return m_SpritePipeline && m_LinePipeline; }

		// Records the frame into cmd. The caller submits it.
		void Render(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* target, uint32 width, uint32 height,
			World& world, const RenderView& view, const DebugLines* lines = nullptr);

		TextureCache& GetTextures() { return m_Textures; }
		GPUDevice& GetDevice() { return m_Device; }

	private:
		void CreatePipelines();
		void EnsureDepth(uint32 width, uint32 height);
		void UploadLines(SDL_GPUCommandBuffer* cmd, const DebugLines& lines);

		GPUDevice& m_Device;
		SDL_GPUTextureFormat m_ColorFormat;
		TextureCache m_Textures;

		SDL_GPUGraphicsPipeline* m_SpritePipeline = nullptr;
		SDL_GPUGraphicsPipeline* m_LinePipeline = nullptr;

		RefPtr<GPUBuffer> m_QuadVertices;
		RefPtr<GPUBuffer> m_QuadIndices;
		RefPtr<GPUBuffer> m_LineVertices;
		RefPtr<GPUTexture> m_Depth;
	};
}
