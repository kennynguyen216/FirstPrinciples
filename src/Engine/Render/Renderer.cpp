#include "Engine/Render/Renderer.h"
#include "Engine/Material.h"

#include <algorithm>
#include <cmath>

namespace FP
{
	namespace
	{
		// Half floats keep lights brighter than white until the present pass.
		constexpr SDL_GPUTextureFormat SceneFormat = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;

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

		MaterialUniforms MakeMaterialUniforms(const MaterialData& data)
		{
			MaterialUniforms u;
			u.BaseColor = ToLinear(data.BaseColor);
			const Vec4 emissive = ToLinear(data.EmissiveColor);
			u.Emissive = { emissive.XYZ() * data.EmissiveIntensity, 0 };
			u.Subsurface = { ToLinear(data.SubsurfaceColor).XYZ(), data.Subsurface };
			u.Params = { data.Metallic, data.Roughness, data.Specular, data.NormalStrength };
			u.Params2 = { data.AmbientOcclusionStrength, data.AlphaCutoff, data.IndexOfRefraction, data.Thickness };
			return u;
		}
	}

	Vec4 ToLinear(const Color& color)
	{
		return { SrgbToLinear(color.R), SrgbToLinear(color.G), SrgbToLinear(color.B), color.A };
	}

	Renderer::Renderer(GPUDevice& device, SDL_GPUTextureFormat targetFormat)
		: m_Device(device), m_TargetFormat(targetFormat), m_Textures(device)
	{
		m_QuadVertices = device.CreateBuffer(SDL_GPU_BUFFERUSAGE_VERTEX, QuadVertices, sizeof(QuadVertices));
		m_QuadIndices = device.CreateBuffer(SDL_GPU_BUFFERUSAGE_INDEX, QuadIndices, sizeof(QuadIndices));
		CreatePipelines();
	}

	Renderer::~Renderer()
	{
		SDL_GPUDevice* gpu = m_Device.GetHandle();
		SDL_WaitForGPUIdle(gpu);
		for (SDL_GPUGraphicsPipeline* pipeline : { m_UnlitPipeline, m_LitPipeline, m_GlassPipeline, m_PresentPipeline, m_LinePipeline })
		{
			if (pipeline)
				SDL_ReleaseGPUGraphicsPipeline(gpu, pipeline);
		}
	}

	SDL_GPUGraphicsPipeline* Renderer::CreateMeshPipeline(const char* fragment, uint32 numSamplers, uint32 numUniforms, bool blend)
	{
		SDL_GPUDevice* gpu = m_Device.GetHandle();
		SDL_GPUShader* vert = m_Device.LoadShader("Mesh.vert", 0, 1);
		SDL_GPUShader* frag = m_Device.LoadShader(fragment, numSamplers, numUniforms);
		SDL_GPUGraphicsPipeline* pipeline = nullptr;
		if (vert && frag)
		{
			SDL_GPUColorTargetDescription colorTarget = {};
			colorTarget.format = SceneFormat;
			if (blend)
				colorTarget.blend_state = AlphaBlend();

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
			info.vertex_shader = vert;
			info.fragment_shader = frag;
			info.vertex_input_state.vertex_buffer_descriptions = &buffer;
			info.vertex_input_state.num_vertex_buffers = 1;
			info.vertex_input_state.vertex_attributes = attributes;
			info.vertex_input_state.num_vertex_attributes = 4;
			info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
			info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
			info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
			info.target_info.color_target_descriptions = &colorTarget;
			info.target_info.num_color_targets = 1;
			info.target_info.has_depth_stencil_target = true;
			info.target_info.depth_stencil_format = m_Device.GetDepthFormat();
			pipeline = SDL_CreateGPUGraphicsPipeline(gpu, &info);
			if (!pipeline)
				FP_LOG_ERROR("Pipeline for %s failed: %s", fragment, SDL_GetError());
		}
		if (vert)
			SDL_ReleaseGPUShader(gpu, vert);
		if (frag)
			SDL_ReleaseGPUShader(gpu, frag);
		return pipeline;
	}

	void Renderer::CreatePipelines()
	{
		SDL_GPUDevice* gpu = m_Device.GetHandle();
		m_UnlitPipeline = CreateMeshPipeline("Unlit.frag", 1, 1, true);
		m_LitPipeline = CreateMeshPipeline("Lit.frag", 8, 2, true);
		m_GlassPipeline = CreateMeshPipeline("Glass.frag", 4, 2, false);

		SDL_GPUColorTargetDescription colorTarget = {};
		colorTarget.format = m_TargetFormat;

		SDL_GPUShader* presentVert = m_Device.LoadShader("Present.vert", 0, 0);
		SDL_GPUShader* presentFrag = m_Device.LoadShader("Present.frag", 1, 1);
		if (presentVert && presentFrag)
		{
			SDL_GPUGraphicsPipelineCreateInfo info = {};
			info.vertex_shader = presentVert;
			info.fragment_shader = presentFrag;
			info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
			info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
			info.target_info.color_target_descriptions = &colorTarget;
			info.target_info.num_color_targets = 1;
			m_PresentPipeline = SDL_CreateGPUGraphicsPipeline(gpu, &info);
			if (!m_PresentPipeline)
				FP_LOG_ERROR("Present pipeline failed: %s", SDL_GetError());
		}

		SDL_GPUShader* lineVert = m_Device.LoadShader("Line.vert", 0, 1);
		SDL_GPUShader* lineFrag = m_Device.LoadShader("Line.frag", 0, 0);
		if (lineVert && lineFrag)
		{
			colorTarget.blend_state = AlphaBlend();

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
			info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
			info.target_info.color_target_descriptions = &colorTarget;
			info.target_info.num_color_targets = 1;
			m_LinePipeline = SDL_CreateGPUGraphicsPipeline(gpu, &info);
			if (!m_LinePipeline)
				FP_LOG_ERROR("Line pipeline failed: %s", SDL_GetError());
		}

		for (SDL_GPUShader* shader : { presentVert, presentFrag, lineVert, lineFrag })
		{
			if (shader)
				SDL_ReleaseGPUShader(gpu, shader);
		}
	}

	void Renderer::EnsureTargets(uint32 width, uint32 height)
	{
		if (m_SceneColor && m_SceneColor->GetWidth() == width && m_SceneColor->GetHeight() == height)
			return;
		m_SceneColor = m_Device.CreateRenderTarget(SceneFormat, width, height, false);
		m_SceneCopy = m_Device.CreateRenderTarget(SceneFormat, width, height, false);
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

	void Renderer::BuildFrameUniforms(World& world, const RenderView& view, uint32 width, uint32 height)
	{
		m_Frame = FrameUniforms();
		m_Frame.CameraPosition = { view.CameraPosition, view.Orthographic ? 1.0f : 0.0f };
		m_Frame.CameraForward = { view.CameraForward, 0 };
		m_Frame.ScreenSize = { static_cast<float>(width), static_cast<float>(height), 1.0f / static_cast<float>(width), 1.0f / static_cast<float>(height) };

		Vec3 ambient;
		for (auto [entity, light] : world.View<AmbientLightComponent>().each())
			ambient += ToLinear(light.Color).XYZ() * light.Intensity;
		m_Frame.Ambient = { ambient, 0 };

		int32 count = 0;
		for (auto [entity, transform, light] : world.View<TransformComponent, DirectionalLightComponent>().each())
		{
			if (count == MaxLights)
				break;
			m_Frame.LightPosition[count] = { transform.GetForward(), 0 };
			m_Frame.LightColor[count] = { ToLinear(light.Color).XYZ() * light.Intensity, 0 };
			++count;
		}
		for (auto [entity, transform, light] : world.View<TransformComponent, PointLightComponent>().each())
		{
			if (count == MaxLights)
				break;
			m_Frame.LightPosition[count] = { transform.Position, 1 };
			m_Frame.LightColor[count] = { ToLinear(light.Color).XYZ() * light.Intensity, light.Range };
			++count;
		}
		m_Frame.LightCount[0] = count;
	}

	GPUTexture* Renderer::Texture(const String& path, bool srgb, GPUTexture* fallback)
	{
		GPUTexture* texture = m_Textures.Get(path, srgb);
		return texture ? texture : fallback;
	}

	void Renderer::DrawSprite(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass, World& world, const Draw& draw, const Mat4& viewProjection)
	{
		const TransformComponent& transform = world.Get<TransformComponent>(draw.Value);
		const SpriteComponent& sprite = world.Get<SpriteComponent>(draw.Value);
		const MaterialData defaults;
		const MaterialData& data = draw.Mat ? draw.Mat->GetData() : defaults;

		ObjectUniforms object;
		object.ViewProjection = viewProjection;
		object.Model = transform.GetMatrix() * Mat4::Scale({ sprite.Size, 1 });
		object.NormalMatrix = object.Model.Inverse().Transposed();
		object.UVTransform = { data.Tiling.X, data.Tiling.Y, data.Offset.X, data.Offset.Y };
		object.Tint = ToLinear(sprite.Tint);
		SDL_PushGPUVertexUniformData(cmd, 0, &object, sizeof(object));

		const MaterialUniforms material = MakeMaterialUniforms(data);
		SDL_PushGPUFragmentUniformData(cmd, 0, &material, sizeof(material));

		// The sprite's own texture stands in when the material has no base color map.
		const String& baseMap = data.BaseColorMap.IsEmpty() ? sprite.Texture : data.BaseColorMap;
		GPUTexture* white = m_Device.GetWhiteTexture();
		SDL_GPUSampler* sampler = m_Device.GetDefaultSampler();

		SDL_GPUTextureSamplerBinding bindings[8] = {};
		uint32 count = 0;
		auto bind = [&](GPUTexture* texture, SDL_GPUSampler* s = nullptr) { bindings[count++] = { texture->GetHandle(), s ? s : sampler }; };

		bind(Texture(baseMap, true, white));
		if (draw.Kind == ShaderKind::Lit)
		{
			bind(Texture(data.NormalMap, false, m_Device.GetFlatNormalTexture()));
			bind(Texture(data.AmbientOcclusionMap, false, white));
			bind(Texture(data.MetallicMap, false, white));
			bind(Texture(data.RoughnessMap, false, white));
			bind(Texture(data.SpecularMap, false, white));
			bind(Texture(data.EmissiveMap, true, white));
			bind(Texture(data.SubsurfaceMap, false, white));
		}
		else if (draw.Kind == ShaderKind::Glass)
		{
			bind(Texture(data.NormalMap, false, m_Device.GetFlatNormalTexture()));
			bind(Texture(data.RoughnessMap, false, white));
			bind(m_SceneCopy.Get(), m_Device.GetClampSampler());
		}
		SDL_BindGPUFragmentSamplers(pass, 0, bindings, count);
		SDL_DrawGPUIndexedPrimitives(pass, 6, 1, 0, 0, 0);
	}

	void Renderer::Render(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* target, uint32 width, uint32 height,
		World& world, const RenderView& view, const DebugLines* lines)
	{
		if (!IsValid() || width == 0 || height == 0)
			return;
		EnsureTargets(width, height);
		BuildFrameUniforms(world, view, width, height);

		const bool hasLines = lines && !lines->GetVertices().IsEmpty();
		if (hasLines)
			UploadLines(cmd, *lines);

		// Sort by layer, then back to front along the view. Glass draws last so it can see everything.
		Array<Draw> draws;
		bool hasGlass = false;
		for (auto [entity, transform, sprite] : world.View<TransformComponent, SpriteComponent>().each())
		{
			const MaterialComponent* materialComponent = world.TryGet<MaterialComponent>(entity);
			const Material* material = materialComponent ? MaterialLibrary::Get().Load(materialComponent->Material) : nullptr;
			ShaderKind kind = ShaderKind::Unlit;
			if (material)
			{
				const String& shader = material->GetData().Shader;
				kind = shader == "Glass" ? ShaderKind::Glass : shader == "Unlit" ? ShaderKind::Unlit : ShaderKind::Lit;
			}
			hasGlass |= kind == ShaderKind::Glass;
			draws.Add({ sprite.Layer, Vec3::Dot(transform.Position - view.CameraPosition, view.CameraForward), entity, kind, material });
		}
		std::stable_sort(draws.begin(), draws.end(), [](const Draw& a, const Draw& b) {
			const bool aGlass = a.Kind == ShaderKind::Glass;
			const bool bGlass = b.Kind == ShaderKind::Glass;
			if (aGlass != bGlass)
				return bGlass;
			return a.Layer != b.Layer ? a.Layer < b.Layer : a.Depth > b.Depth;
		});

		const Mat4 viewProjection = view.GetViewProjection();
		SDL_PushGPUFragmentUniformData(cmd, 1, &m_Frame, sizeof(m_Frame));

		auto beginScenePass = [&](bool clear) {
			const Vec4 background = ToLinear(view.Background);
			SDL_GPUColorTargetInfo color = {};
			color.texture = m_SceneColor->GetHandle();
			color.clear_color = { background.X, background.Y, background.Z, 1 };
			color.load_op = clear ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
			color.store_op = SDL_GPU_STOREOP_STORE;

			SDL_GPUDepthStencilTargetInfo depth = {};
			depth.texture = m_Depth->GetHandle();
			depth.clear_depth = 1;
			depth.load_op = clear ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
			depth.store_op = SDL_GPU_STOREOP_STORE;
			depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
			depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;

			SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &color, 1, &depth);
			SDL_GPUBufferBinding vertexBinding = { m_QuadVertices->GetHandle(), 0 };
			SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
			SDL_GPUBufferBinding indexBinding = { m_QuadIndices->GetHandle(), 0 };
			SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_16BIT);
			return pass;
		};

		// Scene pass.
		SDL_GPURenderPass* pass = beginScenePass(true);
		SDL_GPUGraphicsPipeline* bound = nullptr;
		int32 index = 0;
		for (; index < draws.Num() && draws[index].Kind != ShaderKind::Glass; ++index)
		{
			SDL_GPUGraphicsPipeline* pipeline = draws[index].Kind == ShaderKind::Lit ? m_LitPipeline : m_UnlitPipeline;
			if (pipeline != bound)
			{
				SDL_BindGPUGraphicsPipeline(pass, pipeline);
				bound = pipeline;
			}
			DrawSprite(cmd, pass, world, draws[index], viewProjection);
		}
		SDL_EndGPURenderPass(pass);

		// Glass pass. Each glass object sees the scene as it was before any glass was drawn.
		if (hasGlass)
		{
			SDL_GPUBlitInfo blit = {};
			blit.source.texture = m_SceneColor->GetHandle();
			blit.source.w = width;
			blit.source.h = height;
			blit.destination.texture = m_SceneCopy->GetHandle();
			blit.destination.w = width;
			blit.destination.h = height;
			blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
			blit.filter = SDL_GPU_FILTER_NEAREST;
			SDL_BlitGPUTexture(cmd, &blit);

			pass = beginScenePass(false);
			SDL_BindGPUGraphicsPipeline(pass, m_GlassPipeline);
			for (; index < draws.Num(); ++index)
				DrawSprite(cmd, pass, world, draws[index], viewProjection);
			SDL_EndGPURenderPass(pass);
		}

		// Present pass with editor lines on top.
		SDL_GPUColorTargetInfo output = {};
		output.texture = target;
		output.load_op = SDL_GPU_LOADOP_DONT_CARE;
		output.store_op = SDL_GPU_STOREOP_STORE;
		pass = SDL_BeginGPURenderPass(cmd, &output, 1, nullptr);

		SDL_BindGPUGraphicsPipeline(pass, m_PresentPipeline);
		const Vec4 present{ view.Exposure, view.Tonemap ? 1.0f : 0.0f, 0, 0 };
		SDL_PushGPUFragmentUniformData(cmd, 0, &present, sizeof(present));
		SDL_GPUTextureSamplerBinding scene = { m_SceneColor->GetHandle(), m_Device.GetClampSampler() };
		SDL_BindGPUFragmentSamplers(pass, 0, &scene, 1);
		SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);

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
