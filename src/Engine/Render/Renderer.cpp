#include "Engine/Render/Renderer.h"

#include <algorithm>
#include <cmath>

namespace FP
{
	namespace
	{
		float SrgbToLinear(float c)
		{
			return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
		}

		SDL_GPUColorTargetBlendState AlphaBlend()
		{
			SDL_GPUColorTargetBlendState blend = {};
			blend.enable_blend = true;
			blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
			blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
			blend.color_blend_op = SDL_GPU_BLENDOP_ADD;
			blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
			blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
			blend.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
			return blend;
		}

		// Unit quad in the XY plane facing +Z. UV origin is the top left like images.
		const MeshVertex QuadVertices[4] = {
			{ { -0.5f, 0.5f, 0 }, { 0, 0, 1 }, { 0, 0 }, { 1, 0, 0, 1 } },
			{ { 0.5f, 0.5f, 0 }, { 0, 0, 1 }, { 1, 0 }, { 1, 0, 0, 1 } },
			{ { 0.5f, -0.5f, 0 }, { 0, 0, 1 }, { 1, 1 }, { 1, 0, 0, 1 } },
			{ { -0.5f, -0.5f, 0 }, { 0, 0, 1 }, { 0, 1 }, { 1, 0, 0, 1 } },
		};
		const uint16 QuadIndices[6] = { 0, 3, 2, 0, 2, 1 };
	}

	Vec4 ToLinear(const Color& color)
	{
		return { SrgbToLinear(color.R), SrgbToLinear(color.G), SrgbToLinear(color.B), color.A };
	}

	Renderer::Renderer(GPUDevice& device, SDL_GPUTextureFormat colorFormat)
		: m_Device(device), m_ColorFormat(colorFormat), m_Textures(device)
	{
		m_QuadVertices = device.CreateBuffer(SDL_GPU_BUFFERUSAGE_VERTEX, QuadVertices, sizeof(QuadVertices));
		m_QuadIndices = device.CreateBuffer(SDL_GPU_BUFFERUSAGE_INDEX, QuadIndices, sizeof(QuadIndices));
		CreatePipelines();
	}

	Renderer::~Renderer()
	{
		SDL_GPUDevice* gpu = m_Device.GetHandle();
		SDL_WaitForGPUIdle(gpu);
		if (m_SpritePipeline)
			SDL_ReleaseGPUGraphicsPipeline(gpu, m_SpritePipeline);
		if (m_LinePipeline)
			SDL_ReleaseGPUGraphicsPipeline(gpu, m_LinePipeline);
	}

	void Renderer::CreatePipelines()
	{
		SDL_GPUDevice* gpu = m_Device.GetHandle();
		SDL_GPUShader* meshVert = m_Device.LoadShader("Mesh.vert", 0, 1);
		SDL_GPUShader* unlitFrag = m_Device.LoadShader("Unlit.frag", 1, 1);
		SDL_GPUShader* lineVert = m_Device.LoadShader("Line.vert", 0, 1);
		SDL_GPUShader* lineFrag = m_Device.LoadShader("Line.frag", 0, 0);

		SDL_GPUColorTargetDescription colorTarget = {};
		colorTarget.format = m_ColorFormat;
		colorTarget.blend_state = AlphaBlend();

		if (meshVert && unlitFrag)
		{
			SDL_GPUVertexBufferDescription buffer = {};
			buffer.slot = 0;
			buffer.pitch = sizeof(MeshVertex);
			buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

			SDL_GPUVertexAttribute attributes[4] = {};
			attributes[0] = { 0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(MeshVertex, Position) };
			attributes[1] = { 1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(MeshVertex, Normal) };
			attributes[2] = { 2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(MeshVertex, UV) };
			attributes[3] = { 3, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(MeshVertex, Tangent) };

			SDL_GPUGraphicsPipelineCreateInfo info = {};
			info.vertex_shader = meshVert;
			info.fragment_shader = unlitFrag;
			info.vertex_input_state.vertex_buffer_descriptions = &buffer;
			info.vertex_input_state.num_vertex_buffers = 1;
			info.vertex_input_state.vertex_attributes = attributes;
			info.vertex_input_state.num_vertex_attributes = 4;
			info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
			info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
			info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
			info.depth_stencil_state.enable_depth_test = false;
			info.depth_stencil_state.enable_depth_write = false;
			info.target_info.color_target_descriptions = &colorTarget;
			info.target_info.num_color_targets = 1;
			info.target_info.has_depth_stencil_target = true;
			info.target_info.depth_stencil_format = m_Device.GetDepthFormat();
			m_SpritePipeline = SDL_CreateGPUGraphicsPipeline(gpu, &info);
			if (!m_SpritePipeline)
				FP_LOG_ERROR("Sprite pipeline failed: %s", SDL_GetError());
		}

		if (lineVert && lineFrag)
		{
			SDL_GPUVertexBufferDescription buffer = {};
			buffer.slot = 0;
			buffer.pitch = sizeof(LineVertex);
			buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

			SDL_GPUVertexAttribute attributes[2] = {};
			attributes[0] = { 0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(LineVertex, Position) };
			attributes[1] = { 1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(LineVertex, Color) };

			SDL_GPUGraphicsPipelineCreateInfo info = {};
			info.vertex_shader = lineVert;
			info.fragment_shader = lineFrag;
			info.vertex_input_state.vertex_buffer_descriptions = &buffer;
			info.vertex_input_state.num_vertex_buffers = 1;
			info.vertex_input_state.vertex_attributes = attributes;
			info.vertex_input_state.num_vertex_attributes = 2;
			info.primitive_type = SDL_GPU_PRIMITIVETYPE_LINELIST;
			info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
			info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
			info.target_info.color_target_descriptions = &colorTarget;
			info.target_info.num_color_targets = 1;
			info.target_info.has_depth_stencil_target = true;
			info.target_info.depth_stencil_format = m_Device.GetDepthFormat();
			m_LinePipeline = SDL_CreateGPUGraphicsPipeline(gpu, &info);
			if (!m_LinePipeline)
				FP_LOG_ERROR("Line pipeline failed: %s", SDL_GetError());
		}

		// Pipelines keep what they need so the shaders can go.
		for (SDL_GPUShader* shader : { meshVert, unlitFrag, lineVert, lineFrag })
		{
			if (shader)
				SDL_ReleaseGPUShader(gpu, shader);
		}
	}

	void Renderer::EnsureDepth(uint32 width, uint32 height)
	{
		if (m_Depth && m_Depth->GetWidth() == width && m_Depth->GetHeight() == height)
			return;
		m_Depth = m_Device.CreateRenderTarget(m_Device.GetDepthFormat(), width, height, true);
	}

	void Renderer::UploadLines(SDL_GPUCommandBuffer* cmd, const DebugLines& lines)
	{
		const uint32 bytes = static_cast<uint32>(lines.GetVertices().Num()) * sizeof(LineVertex);
		if (!m_LineVertices || m_LineVertices->GetSize() < bytes)
		{
			uint32 capacity = 64 * 1024;
			while (capacity < bytes)
				capacity *= 2;
			m_LineVertices = m_Device.CreateBuffer(SDL_GPU_BUFFERUSAGE_VERTEX, nullptr, capacity);
		}
		m_Device.UploadToBuffer(cmd, *m_LineVertices, lines.GetVertices().Data(), bytes);
	}

	void Renderer::Render(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* target, uint32 width, uint32 height,
		World& world, const RenderView& view, const DebugLines* lines)
	{
		if (!IsValid() || width == 0 || height == 0)
			return;
		EnsureDepth(width, height);

		const bool hasLines = lines && !lines->GetVertices().IsEmpty();
		if (hasLines)
			UploadLines(cmd, *lines);

		// Sprites draw back to front by layer, then by depth along the view.
		struct SpriteDraw
		{
			int32 Layer;
			float Depth;
			Entity Value;
		};
		Array<SpriteDraw> sprites;
		for (auto [entity, transform, sprite] : world.View<TransformComponent, SpriteComponent>().each())
			sprites.Add({ sprite.Layer, Vec3::Dot(transform.Position - view.CameraPosition, view.CameraForward), entity });
		std::stable_sort(sprites.begin(), sprites.end(), [](const SpriteDraw& a, const SpriteDraw& b) {
			return a.Layer != b.Layer ? a.Layer < b.Layer : a.Depth > b.Depth;
		});

		const Vec4 background = ToLinear(view.Background);
		SDL_GPUColorTargetInfo color = {};
		color.texture = target;
		color.clear_color = { background.X, background.Y, background.Z, 1 };
		color.load_op = SDL_GPU_LOADOP_CLEAR;
		color.store_op = SDL_GPU_STOREOP_STORE;

		SDL_GPUDepthStencilTargetInfo depth = {};
		depth.texture = m_Depth->GetHandle();
		depth.clear_depth = 1;
		depth.load_op = SDL_GPU_LOADOP_CLEAR;
		depth.store_op = SDL_GPU_STOREOP_DONT_CARE;
		depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
		depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;

		SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &color, 1, &depth);
		const Mat4 viewProjection = view.GetViewProjection();

		if (!sprites.IsEmpty())
		{
			SDL_BindGPUGraphicsPipeline(pass, m_SpritePipeline);
			SDL_GPUBufferBinding vertexBinding = { m_QuadVertices->GetHandle(), 0 };
			SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
			SDL_GPUBufferBinding indexBinding = { m_QuadIndices->GetHandle(), 0 };
			SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_16BIT);

			for (const SpriteDraw& draw : sprites)
			{
				const TransformComponent& transform = world.Get<TransformComponent>(draw.Value);
				const SpriteComponent& sprite = world.Get<SpriteComponent>(draw.Value);

				ObjectUniforms object;
				object.ViewProjection = viewProjection;
				object.Model = transform.GetMatrix() * Mat4::Scale({ sprite.Size, 1 });
				object.NormalMatrix = object.Model.Inverse().Transposed();
				object.Tint = ToLinear(sprite.Tint);
				SDL_PushGPUVertexUniformData(cmd, 0, &object, sizeof(object));

				MaterialUniforms material;
				SDL_PushGPUFragmentUniformData(cmd, 0, &material, sizeof(material));

				GPUTexture* texture = m_Textures.Get(sprite.Texture, true);
				SDL_GPUTextureSamplerBinding binding = { (texture ? texture : m_Device.GetWhiteTexture())->GetHandle(), m_Device.GetDefaultSampler() };
				SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);

				SDL_DrawGPUIndexedPrimitives(pass, 6, 1, 0, 0, 0);
			}
		}

		if (hasLines)
		{
			SDL_BindGPUGraphicsPipeline(pass, m_LinePipeline);
			SDL_GPUBufferBinding binding = { m_LineVertices->GetHandle(), 0 };
			SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);
			SDL_PushGPUVertexUniformData(cmd, 0, &viewProjection, sizeof(viewProjection));
			SDL_DrawGPUPrimitives(pass, static_cast<uint32>(lines->GetVertices().Num()), 1, 0, 0);
		}

		SDL_EndGPURenderPass(pass);
	}
}
