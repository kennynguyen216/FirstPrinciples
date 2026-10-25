#include "Engine/Render/Renderer.h"
#include "Engine/EngineSettings.h"
#include "Engine/Material.h"
#include "Engine/Paths.h"
#include "Engine/Animation/SkeletalModel.h"
#include "Engine/UI/UISystem.h"
#include "Engine/Particles/ParticleSystem.h"
#include "HAL/PlatformMisc.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

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

		SDL_GPUColorTargetBlendState AdditiveBlend()
		{
			SDL_GPUColorTargetBlendState blend = AlphaBlend();
			blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
			blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
			return blend;
		}

		struct ParticleVertex
		{
			Vec3 Position;
			Vec2 UV;
			Vec4 Color;
		};

		// Unit quad in the XY plane facing +Z. UV origin is the top left like images.
		const MeshVertex QuadVertices[4] = {
			{ { -0.5f, 0.5f, 0 }, { 0, 0, 1 }, { 0, 0 }, { 1, 0, 0, 1 } },
			{ { 0.5f, 0.5f, 0 }, { 0, 0, 1 }, { 1, 0 }, { 1, 0, 0, 1 } },
			{ { 0.5f, -0.5f, 0 }, { 0, 0, 1 }, { 1, 1 }, { 1, 0, 0, 1 } },
			{ { -0.5f, -0.5f, 0 }, { 0, 0, 1 }, { 0, 1 }, { 1, 0, 0, 1 } },
		};
		const uint16 QuadIndices[6] = { 0, 3, 2, 0, 2, 1 };

		// Layout matches BlackHole.frag (std140).
		constexpr int32 MaxBlackHoles = 4;
		struct BlackHoleUniforms
		{
			Mat4 ViewProjection;
			Mat4 InverseViewProjection;
			Vec4 CameraForward;
			Vec4 Background;
			Vec4 Params;
			Vec4 Environment;
			Vec4 EnvironmentSize;
			Vec4 Hole[MaxBlackHoles];
			Vec4 Disk[MaxBlackHoles];
			Vec4 DiskLight[MaxBlackHoles];
		};

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
		: m_Device(device), m_TargetFormat(targetFormat), m_Textures(device), m_Meshes(device), m_Culling(device)
	{
		m_QuadVertices = device.CreateBuffer(SDL_GPU_BUFFERUSAGE_VERTEX, QuadVertices, sizeof(QuadVertices));
		m_QuadIndices = device.CreateBuffer(SDL_GPU_BUFFERUSAGE_INDEX, QuadIndices, sizeof(QuadIndices));
		m_BoneUpload.Resize(MaxSkinBones);
		const Vec4 empty;
		m_EmptyEnvironment = device.CreateBuffer(SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ, &empty, sizeof(empty));
		CreatePipelines();
		m_OcclusionPipeline = CreateShadowPipeline(Surface::Mesh, true);
	}

	Renderer::~Renderer()
	{
		SDL_GPUDevice* gpu = m_Device.GetHandle();
		SDL_WaitForGPUIdle(gpu);
		for (auto& row : m_ScenePipelines)
		{
			for (SDL_GPUGraphicsPipeline* pipeline : row)
			{
				if (pipeline)
					SDL_ReleaseGPUGraphicsPipeline(gpu, pipeline);
			}
		}
		for (SDL_GPUGraphicsPipeline* pipeline : { m_OcclusionPipeline, m_PresentPipeline, m_SkyboxPipeline, m_BlackHolePipeline, m_PostPipeline, m_FXAAPipeline, m_BloomPipeline, m_UIPipeline, m_LinePipeline, m_WirePipelines[0], m_WirePipelines[1], m_WirePipelines[2], m_ShadowPipelines[0], m_ShadowPipelines[1], m_ParticlePipelines[0], m_ParticlePipelines[1] })
		{
			if (pipeline)
				SDL_ReleaseGPUGraphicsPipeline(gpu, pipeline);
		}
	}

	bool Renderer::IsValid() const
	{
		for (const auto& row : m_ScenePipelines)
		{
			for (SDL_GPUGraphicsPipeline* pipeline : row)
			{
				if (!pipeline)
					return false;
			}
		}
		return m_PresentPipeline && m_PostPipeline && m_FXAAPipeline && m_BloomPipeline && m_SkyboxPipeline && m_LinePipeline && m_EmptyEnvironment;
	}

	SDL_GPUGraphicsPipeline* Renderer::GetPipeline(const Draw& draw) const
	{
		if (m_Mode == ViewMode::Wireframe && m_WirePipelines[static_cast<int>(draw.Type)])
			return m_WirePipelines[static_cast<int>(draw.Type)];
		return m_ScenePipelines[static_cast<int>(draw.Kind)][static_cast<int>(draw.Type)];
	}

	SDL_GPUGraphicsPipeline* Renderer::CreateMeshPipeline(const char* fragment, uint32 numSamplers, uint32 numUniforms, Surface surface, bool glass, bool wireframe)
	{
		SDL_GPUDevice* gpu = m_Device.GetHandle();
		const bool skinned = surface == Surface::SkinnedMesh;
		SDL_GPUShader* vert = skinned ? m_Device.LoadShader("SkinnedMesh.vert", 0, 2) : m_Device.LoadShader("Mesh.vert", 0, 1);
		SDL_GPUShader* frag = m_Device.LoadShader(fragment, numSamplers, numUniforms, std::string_view(fragment) == "Unlit.frag" ? 0 : 1);
		SDL_GPUGraphicsPipeline* pipeline = nullptr;
		if (vert && frag)
		{
			// Meshes are opaque and write depth. Sprites blend and only test depth so they
			// sort by layer among themselves. Glass writes its final color so it never blends.
			const bool isMesh = surface != Surface::Sprite;
			SDL_GPUColorTargetDescription colorTarget = {};
			colorTarget.format = SceneFormat;
			if (!isMesh && !glass && !wireframe)
				colorTarget.blend_state = AlphaBlend();

			// Skinned meshes add a second buffer with bones and weights.
			SDL_GPUVertexBufferDescription buffers[2] = {};
			buffers[0].slot = 0;
			buffers[0].pitch = sizeof(MeshVertex);
			buffers[0].input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
			buffers[1].slot = 1;
			buffers[1].pitch = sizeof(SkinVertex);
			buffers[1].input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

			SDL_GPUVertexAttribute attributes[6] = {};
			attributes[0] = { 0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(MeshVertex, Position) };
			attributes[1] = { 1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(MeshVertex, Normal) };
			attributes[2] = { 2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(MeshVertex, UV) };
			attributes[3] = { 3, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(MeshVertex, Tangent) };
			attributes[4] = { 4, 1, SDL_GPU_VERTEXELEMENTFORMAT_USHORT4, offsetof(SkinVertex, Bones) };
			attributes[5] = { 5, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(SkinVertex, Weights) };

			SDL_GPUGraphicsPipelineCreateInfo info = {};
			info.vertex_shader = vert;
			info.fragment_shader = frag;
			info.vertex_input_state.vertex_buffer_descriptions = buffers;
			info.vertex_input_state.num_vertex_buffers = skinned ? 2 : 1;
			info.vertex_input_state.vertex_attributes = attributes;
			info.vertex_input_state.num_vertex_attributes = skinned ? 6 : 4;
			info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
			// Wireframe shows back edges too, so it never culls.
			info.rasterizer_state.fill_mode = wireframe ? SDL_GPU_FILLMODE_LINE : SDL_GPU_FILLMODE_FILL;
			info.rasterizer_state.cull_mode = isMesh && !wireframe ? SDL_GPU_CULLMODE_BACK : SDL_GPU_CULLMODE_NONE;
			info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
			info.depth_stencil_state.enable_depth_test = true;
			info.depth_stencil_state.enable_depth_write = isMesh && !glass;
			info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
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

	SDL_GPUGraphicsPipeline* Renderer::CreateFullscreenPipeline(const char* fragment, uint32 samplers, SDL_GPUTextureFormat format)
	{
		SDL_GPUDevice* gpu = m_Device.GetHandle();
		SDL_GPUShader* vert = m_Device.LoadShader("Present.vert", 0, 0);
		SDL_GPUShader* frag = m_Device.LoadShader(fragment, samplers, 1);
		SDL_GPUGraphicsPipeline* pipeline = nullptr;
		if (vert && frag)
		{
			SDL_GPUColorTargetDescription colorTarget = {};
			colorTarget.format = format;
			SDL_GPUGraphicsPipelineCreateInfo info = {};
			info.vertex_shader = vert;
			info.fragment_shader = frag;
			info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
			info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
			info.target_info.color_target_descriptions = &colorTarget;
			info.target_info.num_color_targets = 1;
			pipeline = SDL_CreateGPUGraphicsPipeline(gpu, &info);
			if (!pipeline) FP_LOG_ERROR("Pipeline for %s failed: %s", fragment, SDL_GetError());
		}
		if (vert) SDL_ReleaseGPUShader(gpu, vert);
		if (frag) SDL_ReleaseGPUShader(gpu, frag);
		return pipeline;
	}

	void Renderer::CreatePipelines()
	{
		SDL_GPUDevice* gpu = m_Device.GetHandle();
		for (Surface surface : { Surface::Sprite, Surface::Mesh, Surface::SkinnedMesh })
		{
			const int column = static_cast<int>(surface);
			m_ScenePipelines[static_cast<int>(ShaderKind::Unlit)][column] = CreateMeshPipeline("Unlit.frag", 1, 1, surface, false);
			m_ScenePipelines[static_cast<int>(ShaderKind::Lit)][column] = CreateMeshPipeline("Lit.frag", 16, 2, surface, false);
			m_ScenePipelines[static_cast<int>(ShaderKind::Glass)][column] = CreateMeshPipeline("Glass.frag", 4, 2, surface, true);
			m_WirePipelines[column] = CreateMeshPipeline("Unlit.frag", 1, 1, surface, false, true);
		}
		if (!m_WirePipelines[0] || !m_WirePipelines[1] || !m_WirePipelines[2])
			FP_LOG_WARN("This GPU cannot draw wireframes. The wireframe view mode will show normal shading.");
		m_ShadowPipelines[0] = CreateShadowPipeline(Surface::Mesh);
		m_ShadowPipelines[1] = CreateShadowPipeline(Surface::SkinnedMesh);

		SDL_GPUColorTargetDescription colorTarget = {};
		colorTarget.format = m_TargetFormat;

		SDL_GPUShader* presentVert = m_Device.LoadShader("Present.vert", 0, 0);
		SDL_GPUShader* presentFrag = m_Device.LoadShader("Present.frag", 2, 1);
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

			// UI pages over the finished frame. They come premultiplied.
			SDL_GPUShader* uiFrag = m_Device.LoadShader("UI.frag", 1, 0);
			if (uiFrag)
			{
				SDL_GPUColorTargetDescription uiTarget = colorTarget;
				uiTarget.blend_state.enable_blend = true;
				uiTarget.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
				uiTarget.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
				uiTarget.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
				uiTarget.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
				uiTarget.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
				uiTarget.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
				info.fragment_shader = uiFrag;
				info.target_info.color_target_descriptions = &uiTarget;
				m_UIPipeline = SDL_CreateGPUGraphicsPipeline(gpu, &info);
				if (!m_UIPipeline)
					FP_LOG_WARN("UI pipeline failed, so game UI will not draw: %s", SDL_GetError());
				SDL_ReleaseGPUShader(gpu, uiFrag);
			}
		}

		m_PostPipeline = CreateFullscreenPipeline("Present.frag", 2, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB);
		m_FXAAPipeline = CreateFullscreenPipeline("FXAA.frag", 1, m_TargetFormat);
		m_BloomPipeline = CreateFullscreenPipeline("Bloom.frag", 1, SceneFormat);

		SDL_GPUShader* skyFrag = m_Device.LoadShader("Skybox.frag", 0, 1, 1);
		if (presentVert && skyFrag)
		{
			SDL_GPUColorTargetDescription skyTarget = {};
			skyTarget.format = SceneFormat;
			SDL_GPUGraphicsPipelineCreateInfo info = {};
			info.vertex_shader = presentVert;
			info.fragment_shader = skyFrag;
			info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
			info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
			info.target_info.color_target_descriptions = &skyTarget;
			info.target_info.num_color_targets = 1;
			info.target_info.has_depth_stencil_target = true;
			info.target_info.depth_stencil_format = m_Device.GetDepthFormat();
			m_SkyboxPipeline = SDL_CreateGPUGraphicsPipeline(gpu, &info);
			if (!m_SkyboxPipeline)
				FP_LOG_ERROR("Skybox pipeline failed: %s", SDL_GetError());
		}
		if (skyFrag)
			SDL_ReleaseGPUShader(gpu, skyFrag);

		SDL_GPUShader* blackHoleFrag = m_Device.LoadShader("BlackHole.frag", 2, 1, 1);
		if (presentVert && blackHoleFrag)
		{
			SDL_GPUColorTargetDescription lensTarget = {};
			lensTarget.format = SceneFormat;
			SDL_GPUGraphicsPipelineCreateInfo info = {};
			info.vertex_shader = presentVert;
			info.fragment_shader = blackHoleFrag;
			info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
			info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
			info.target_info.color_target_descriptions = &lensTarget;
			info.target_info.num_color_targets = 1;
			m_BlackHolePipeline = SDL_CreateGPUGraphicsPipeline(gpu, &info);
			if (!m_BlackHolePipeline)
				FP_LOG_WARN("Black hole pipeline failed, so black holes will not bend light: %s", SDL_GetError());
		}
		if (blackHoleFrag)
			SDL_ReleaseGPUShader(gpu, blackHoleFrag);

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

		CreateParticlePipelines();
	}

	void Renderer::CreateParticlePipelines()
	{
		SDL_GPUDevice* gpu = m_Device.GetHandle();
		SDL_GPUShader* vert = m_Device.LoadShader("Particle.vert", 0, 1);
		SDL_GPUShader* frag = m_Device.LoadShader("Particle.frag", 1, 1);
		if (vert && frag)
		{
			SDL_GPUVertexBufferDescription buffer = {};
			buffer.slot = 0;
			buffer.pitch = sizeof(ParticleVertex);
			buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

			SDL_GPUVertexAttribute attributes[3] = {};
			attributes[0] = { 0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(ParticleVertex, Position) };
			attributes[1] = { 1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(ParticleVertex, UV) };
			attributes[2] = { 2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(ParticleVertex, Color) };

			for (int32 additive = 0; additive < 2; ++additive)
			{
				SDL_GPUColorTargetDescription colorTarget = {};
				colorTarget.format = SceneFormat;
				colorTarget.blend_state = additive ? AdditiveBlend() : AlphaBlend();

				SDL_GPUGraphicsPipelineCreateInfo info = {};
				info.vertex_shader = vert;
				info.fragment_shader = frag;
				info.vertex_input_state.vertex_buffer_descriptions = &buffer;
				info.vertex_input_state.num_vertex_buffers = 1;
				info.vertex_input_state.vertex_attributes = attributes;
				info.vertex_input_state.num_vertex_attributes = 3;
				info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
				info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
				info.depth_stencil_state.enable_depth_test = true;
				info.depth_stencil_state.enable_depth_write = false;
				info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
				info.target_info.color_target_descriptions = &colorTarget;
				info.target_info.num_color_targets = 1;
				info.target_info.has_depth_stencil_target = true;
				info.target_info.depth_stencil_format = m_Device.GetDepthFormat();
				m_ParticlePipelines[additive] = SDL_CreateGPUGraphicsPipeline(gpu, &info);
				if (!m_ParticlePipelines[additive])
					FP_LOG_ERROR("Particle pipeline failed: %s", SDL_GetError());
			}
		}
		if (vert)
			SDL_ReleaseGPUShader(gpu, vert);
		if (frag)
			SDL_ReleaseGPUShader(gpu, frag);
	}

	void Renderer::PrepareParticles(SDL_GPUCommandBuffer* cmd, World& world, const RenderView& view)
	{
		m_ParticleBatches.Clear();
		if (!m_ParticlePipelines[0] || !m_ParticlePipelines[1])
			return;

		// Far emitters first so alpha particles blend over what is behind them.
		struct Source
		{
			Entity Value;
			float Depth;
		};
		Array<Source> sources;
		for (auto [entity, emitter, state] : world.View<ParticleEmitterComponent, ParticleStateComponent>().each())
		{
			if (!state.Particles.IsEmpty())
				sources.Add({ entity, Vec3::Dot(world.GetWorldPosition(entity) - view.CameraPosition, view.CameraForward) });
		}
		if (sources.IsEmpty())
			return;
		std::sort(sources.begin(), sources.end(), [](const Source& a, const Source& b) { return a.Depth > b.Depth; });

		Array<ParticleVertex> vertices;
		for (const Source& source : sources)
		{
			const ParticleEmitterComponent& emitter = world.Get<ParticleEmitterComponent>(source.Value);
			const ParticleStateComponent& state = world.Get<ParticleStateComponent>(source.Value);
			const Mat4 placed = emitter.WorldSpace ? Mat4() : world.GetWorldMatrix(source.Value);
			const Vec4 start = ToLinear(emitter.StartColor);
			const Vec4 end = ToLinear(emitter.EndColor);
			const uint32 first = static_cast<uint32>(vertices.Num());
			for (const Particle& particle : state.Particles)
			{
				const float t = Clamp01(particle.Age / particle.Life);
				const float half = Lerp(emitter.StartSize, emitter.EndSize, t) * 0.5f;
				const Vec4 color{ Lerp(start.X, end.X, t), Lerp(start.Y, end.Y, t), Lerp(start.Z, end.Z, t), Lerp(start.W, end.W, t) };
				const Vec3 center = emitter.WorldSpace ? particle.Position : placed.TransformPoint(particle.Position);
				const Vec3 right = view.CameraRight * half;
				const Vec3 up = view.CameraUp * half;
				const ParticleVertex corners[4] = {
					{ center - right + up, { 0, 0 }, color },
					{ center + right + up, { 1, 0 }, color },
					{ center + right - up, { 1, 1 }, color },
					{ center - right - up, { 0, 1 }, color },
				};
				for (int32 index : { 0, 3, 2, 0, 2, 1 })
					vertices.Add(corners[index]);
			}
			GPUTexture* texture = emitter.Texture.IsEmpty() ? nullptr : Texture(emitter.Texture, true, m_Device.GetWhiteTexture());
			m_ParticleBatches.Add({ texture, emitter.Blend != "Alpha", first, static_cast<uint32>(vertices.Num()) - first });
		}

		const uint32 bytes = static_cast<uint32>(vertices.Num()) * sizeof(ParticleVertex);
		if (!m_ParticleVertices || m_ParticleVertices->GetSize() < bytes)
		{
			uint32 capacity = 256 * 1024;
			while (capacity < bytes)
				capacity *= 2;
			m_ParticleVertices = m_Device.CreateBuffer(SDL_GPU_BUFFERUSAGE_VERTEX, nullptr, capacity);
		}
		m_Device.UploadToBuffer(cmd, *m_ParticleVertices, vertices.Data(), bytes);
	}

	void Renderer::DrawParticles(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass, const Mat4& viewProjection)
	{
		if (m_ParticleBatches.IsEmpty())
			return;
		SDL_GPUBufferBinding binding = { m_ParticleVertices->GetHandle(), 0 };
		SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);
		SDL_PushGPUVertexUniformData(cmd, 0, &viewProjection, sizeof(viewProjection));
		SDL_GPUGraphicsPipeline* bound = nullptr;
		for (const ParticleBatch& batch : m_ParticleBatches)
		{
			SDL_GPUGraphicsPipeline* pipeline = m_ParticlePipelines[batch.Additive ? 1 : 0];
			if (pipeline != bound)
			{
				SDL_BindGPUGraphicsPipeline(pass, pipeline);
				bound = pipeline;
			}
			GPUTexture* image = batch.Texture ? batch.Texture : m_Device.GetWhiteTexture();
			SDL_GPUTextureSamplerBinding sampler = { image->GetHandle(), m_Device.GetClampSampler() };
			SDL_BindGPUFragmentSamplers(pass, 0, &sampler, 1);
			const Vec4 look{ batch.Texture ? 1.0f : 0.0f, 0, 0, 0 };
			SDL_PushGPUFragmentUniformData(cmd, 0, &look, sizeof(look));
			SDL_DrawGPUPrimitives(pass, batch.Count, 1, batch.First, 0);
		}
	}

	void Renderer::EnsureTargets(uint32 width, uint32 height)
	{
		if (m_SceneColor && m_SceneCopy && m_Depth && m_SceneColor->GetWidth() == width && m_SceneColor->GetHeight() == height)
			return;
		m_SceneColor = m_Device.CreateRenderTarget(SceneFormat, width, height, false);
		m_SceneCopy = m_Device.CreateRenderTarget(SceneFormat, width, height, false);
		// Sampled so black holes can find the scene along their bent rays.
		m_Depth = m_Device.CreateRenderTarget(m_Device.GetDepthFormat(), width, height, true, true);
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

	void Renderer::PrepareEnvironment(World& world)
	{
		if (world.View<SkyboxComponent>().empty())
			return;
		const SkyboxComponent* sky = nullptr;
		for (Entity entity : world.GetEntities())
		{
			const SkyboxComponent* candidate = world.TryGet<SkyboxComponent>(entity);
			if (candidate && candidate->Enabled)
			{
				sky = candidate;
				break;
			}
		}
		if (!sky || sky->Texture.IsEmpty())
			return;

		const String path = Paths::Content(sky->Texture);
		std::error_code ec;
		const auto modified = std::filesystem::last_write_time(path.CStr(), ec);
		const int64 stamp = ec ? 0 : modified.time_since_epoch().count();
		if (path != m_EnvironmentPath || stamp != m_EnvironmentModified)
		{
			m_EnvironmentPath = path;
			m_EnvironmentModified = stamp;
			m_EnvironmentBuffer.Reset();
			if (m_Environment.Load(path.CStr()))
				m_EnvironmentBuffer = m_Device.CreateBuffer(SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ, m_Environment.Pixels.Data(),
					static_cast<uint32>(m_Environment.Pixels.Num()) * sizeof(Vec4));
			else
				FP_LOG_WARN("Could not load skybox %s. Use a 2:1 panorama up to 8192 pixels wide.", path.CStr());
		}
		if (!m_EnvironmentBuffer)
			return;
		m_Frame.EnvironmentParams = { std::max(sky->Intensity, 0.0f), sky->Rotation * DegToRad, sky->Visible ? 1.0f : 0.0f, 0 };
		m_Frame.EnvironmentSize = { static_cast<float>(m_Environment.Width), static_cast<float>(m_Environment.Height), static_cast<float>(m_Environment.Levels), 0 };
		for (int32 i = 0; i < 9; ++i)
			m_Frame.EnvironmentDiffuse[i] = m_Environment.Diffuse[i];
	}

	void Renderer::DrawSkybox(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass, const RenderView& view)
	{
		if (m_Frame.EnvironmentSize.Z == 0 || m_Frame.EnvironmentParams.Z == 0)
			return;
		struct SkyboxUniforms
		{
			Mat4 InverseViewProjection;
			Vec4 CameraPosition;
			Vec4 CameraForward;
			Vec4 Params;
			Vec4 Size;
		};
		const SkyboxUniforms uniforms{ view.GetViewProjection().Inverse(), m_Frame.CameraPosition, m_Frame.CameraForward,
			m_Frame.EnvironmentParams, m_Frame.EnvironmentSize };
		SDL_BindGPUGraphicsPipeline(pass, m_SkyboxPipeline);
		SDL_PushGPUFragmentUniformData(cmd, 0, &uniforms, sizeof(uniforms));
		SDL_GPUBuffer* buffer = m_EnvironmentBuffer->GetHandle();
		SDL_BindGPUFragmentStorageBuffers(pass, 0, &buffer, 1);
		SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
	}

	void Renderer::DrawBlackHoles(SDL_GPUCommandBuffer* cmd, World& world, const RenderView& view, uint32 width, uint32 height)
	{
		if (!m_BlackHolePipeline || view.Mode == ViewMode::Wireframe || world.View<BlackHoleComponent>().empty())
			return;
		BlackHoleUniforms u = {};
		int32 holes = 0;
		float stars = 0;
		for (auto [entity, transform, hole] : world.View<TransformComponent, BlackHoleComponent>().each())
		{
			if (holes == MaxBlackHoles)
				break;
			const float radius = std::max(hole.SchwarzschildRadius, 0.001f);
			u.Hole[holes] = { transform.Position, radius };
			u.Disk[holes] = { transform.GetRotation().Rotate({ 0, 0, 1 }), hole.AccretionDisk ? std::max(hole.DiskOuterRadius, 3 * radius) : 0 };
			u.DiskLight[holes] = { std::max(hole.DiskTemperature, 1.0f), std::max(hole.DiskBrightness, 0.0f), std::max(hole.SpeedOfLight, 0.001f), 0 };
			stars = std::max(stars, hole.StarBrightness);
			++holes;
		}

		const bool hasSky = m_EnvironmentBuffer && m_Frame.EnvironmentSize.Z > 0 && m_Frame.EnvironmentParams.Z > 0;
		u.ViewProjection = view.GetViewProjection();
		u.InverseViewProjection = u.ViewProjection.Inverse();
		u.CameraForward = { view.CameraForward, view.Orthographic ? 1.0f : 0.0f };
		u.Background = { ToLinear(view.Background).XYZ(), stars };
		u.Params = { static_cast<float>(holes), static_cast<float>(world.GetTime()), 0, 0 };
		u.Environment = { m_Frame.EnvironmentParams.X, m_Frame.EnvironmentParams.Y, hasSky ? 1.0f : 0.0f, 0 };
		u.EnvironmentSize = m_Frame.EnvironmentSize;

		// Rays read the finished scene from a copy and write the lensed image back over it.
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

		SDL_GPUColorTargetInfo output = {};
		output.texture = m_SceneColor->GetHandle();
		output.load_op = SDL_GPU_LOADOP_DONT_CARE;
		output.store_op = SDL_GPU_STOREOP_STORE;
		SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &output, 1, nullptr);
		SDL_BindGPUGraphicsPipeline(pass, m_BlackHolePipeline);
		SDL_PushGPUFragmentUniformData(cmd, 0, &u, sizeof(u));
		// ponytail: every pixel traces its ray, even far from any hole. Add a screen bound if it costs too much
		const SDL_GPUTextureSamplerBinding sources[2] = {
			{ m_SceneCopy->GetHandle(), m_Device.GetClampSampler() },
			{ m_Depth->GetHandle(), m_Device.GetClampSampler() },
		};
		SDL_BindGPUFragmentSamplers(pass, 0, sources, 2);
		SDL_GPUBuffer* environment = hasSky ? m_EnvironmentBuffer->GetHandle() : m_EmptyEnvironment->GetHandle();
		SDL_BindGPUFragmentStorageBuffers(pass, 0, &environment, 1);
		SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
		SDL_EndGPURenderPass(pass);
	}

	void Renderer::BuildFrameUniforms(World& world, const RenderView& view, uint32 width, uint32 height)
	{
		m_Frame = FrameUniforms();
		m_Frame.CameraPosition = { view.CameraPosition, view.Orthographic ? 1.0f : 0.0f };
		m_Frame.CameraForward = { view.CameraForward, 0 };
		m_Frame.CameraRight = { view.CameraRight, 0 };
		m_Frame.CameraUp = { view.CameraUp, 0 };
		m_Frame.ScreenSize = { static_cast<float>(width), static_cast<float>(height), 1.0f / static_cast<float>(width), 1.0f / static_cast<float>(height) };

		Vec3 ambient;
		for (auto [entity, light] : world.View<AmbientLightComponent>().each())
			ambient += ToLinear(light.Color).XYZ() * light.Intensity;
		m_Frame.Ambient = { ambient, 0 };

		int32 count = 0;
		for (int32 type = 0; type < 3; ++type)
		{
			m_ShadowCasters[type] = NullEntity;
			m_ShadowLights[type] = -1;
		}
		for (auto [entity, transform, light] : world.View<TransformComponent, DirectionalLightComponent>().each())
		{
			if (count == MaxLights)
				break;
			if (m_ShadowLights[0] < 0 && light.CastShadows && light.ShadowStrength > 0)
			{
				m_ShadowCasters[0] = entity;
				m_ShadowLights[0] = count;
			}
			m_Frame.LightPosition[count] = { world.GetWorldRotation(entity).Rotate({ 0, 0, -1 }), 0 };
			m_Frame.LightColor[count] = { ToLinear(light.Color).XYZ() * light.Intensity, 0 };
			++count;
		}
		for (auto [entity, transform, light] : world.View<TransformComponent, PointLightComponent>().each())
		{
			if (count == MaxLights)
				break;
			if (m_ShadowLights[2] < 0 && light.CastShadows && light.ShadowStrength > 0)
			{
				m_ShadowCasters[2] = entity;
				m_ShadowLights[2] = count;
			}
			m_Frame.LightPosition[count] = { world.GetWorldPosition(entity), 1 };
			m_Frame.LightColor[count] = { ToLinear(light.Color).XYZ() * light.Intensity, light.Range };
			++count;
		}
		for (auto [entity, transform, light] : world.View<TransformComponent, SpotLightComponent>().each())
		{
			if (count == MaxLights)
				break;
			if (m_ShadowLights[1] < 0 && light.CastShadows && light.ShadowStrength > 0)
			{
				m_ShadowCasters[1] = entity;
				m_ShadowLights[1] = count;
			}
			m_Frame.LightPosition[count] = { world.GetWorldPosition(entity), 2 };
			m_Frame.LightColor[count] = { ToLinear(light.Color).XYZ() * light.Intensity, light.Range };
			m_Frame.LightDirection[count] = { world.GetWorldRotation(entity).Rotate({ 0, 0, -1 }), 0 };
			const float inner = std::fmin(std::fmax(light.InnerAngle, 0.0f), 88.0f);
			const float outer = std::fmin(std::fmax(light.OuterAngle, inner + 0.01f), 89.0f);
			m_Frame.LightCone[count] = { std::cos(inner * DegToRad), std::cos(outer * DegToRad), 0, 0 };
			++count;
		}
		m_Frame.LightCount[0] = count;
	}

	void Renderer::PreloadTextures(World& world)
	{
		Array<TextureCache::Request> requests;
		for (auto [entity, sprite] : world.View<SpriteComponent>().each())
			requests.Add({ sprite.Texture, true });
		auto addMaterial = [&](const String& path) {
			const Material* material = MaterialLibrary::Get().Load(path);
			if (!material)
				return;
			const MaterialData& data = material->GetData();
			requests.Add({ data.BaseColorMap, true });
			requests.Add({ data.EmissiveMap, true });
			for (const String* map : { &data.NormalMap, &data.AmbientOcclusionMap, &data.MetallicMap, &data.RoughnessMap, &data.SpecularMap, &data.SubsurfaceMap })
				requests.Add({ *map, false });
		};
		for (auto [entity, materialComponent] : world.View<MaterialComponent>().each())
		{
			addMaterial(materialComponent.Material);
			for (const String& slot : materialComponent.Slots)
				addMaterial(slot);
		}

		const double start = PlatformMisc::GetTimeSeconds();
		const int32 loaded = m_Textures.Preload(requests);
		if (loaded > 0)
			FP_LOG("Loaded %d textures in %.1f ms", loaded, (PlatformMisc::GetTimeSeconds() - start) * 1000.0);
	}

	GPUTexture* Renderer::Texture(const String& path, bool srgb, GPUTexture* fallback)
	{
		GPUTexture* texture = m_Textures.Get(path, srgb);
		return texture ? texture : fallback;
	}

	void Renderer::DrawObject(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass, World& world, const Draw& draw, const Mat4& viewProjection)
	{
		const SpriteComponent* sprite = draw.Type == Surface::Sprite ? &world.Get<SpriteComponent>(draw.Value) : nullptr;
		const MaterialData defaults;
		const MaterialData& data = draw.Mat ? draw.Mat->GetData() : defaults;

		ObjectUniforms object;
		object.ViewProjection = viewProjection;
		const Mat4 model = world.GetWorldMatrix(draw.Value);
		object.Model = sprite ? model * Mat4::Scale({ sprite->Size, 1 }) : model;
		object.NormalMatrix = object.Model.Inverse().Transposed();
		object.UVTransform = { data.Tiling.X, data.Tiling.Y, data.Offset.X, data.Offset.Y };
		object.Tint = sprite ? ToLinear(sprite->Tint) : Vec4(1, 1, 1, 1);

		MaterialUniforms material = MakeMaterialUniforms(data);
		const bool wireframe = m_Mode == ViewMode::Wireframe && m_WirePipelines[static_cast<int>(draw.Type)];
		if (wireframe)
		{
			// One flat color per surface type. Sprites are tinted blue so they stand out from meshes.
			object.Tint = sprite ? Vec4(0.35f, 0.65f, 1.0f, 1) : Vec4(0.85f, 0.85f, 0.85f, 1);
			material = MaterialUniforms();
			material.Params2.Y = 0; // no alpha cutoff
		}
		SDL_PushGPUVertexUniformData(cmd, 0, &object, sizeof(object));
		SDL_PushGPUFragmentUniformData(cmd, 0, &material, sizeof(material));
		if (draw.Type == Surface::SkinnedMesh)
			PushBones(cmd, draw.Skin);

		if (sprite)
		{
			SDL_GPUBufferBinding vertexBinding = { m_QuadVertices->GetHandle(), 0 };
			SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
			SDL_GPUBufferBinding indexBinding = { m_QuadIndices->GetHandle(), 0 };
			SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_16BIT);
		}
		else
		{
			const SDL_GPUBufferBinding vertexBindings[2] = { { draw.Mesh->Vertices->GetHandle(), 0 }, { draw.Mesh->Skin ? draw.Mesh->Skin->GetHandle() : nullptr, 0 } };
			SDL_BindGPUVertexBuffers(pass, 0, vertexBindings, draw.Type == Surface::SkinnedMesh ? 2 : 1);
			SDL_GPUBufferBinding indexBinding = { draw.Mesh->Indices->GetHandle(), 0 };
			SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
		}

		// A sprite's own texture stands in when the material has no base color map.
		const String& baseMap = sprite && data.BaseColorMap.IsEmpty() ? sprite->Texture : data.BaseColorMap;
		GPUTexture* white = m_Device.GetWhiteTexture();
		SDL_GPUSampler* sampler = m_Device.GetDefaultSampler();

		SDL_GPUTextureSamplerBinding bindings[16] = {};
		uint32 count = 0;
		auto bind = [&](GPUTexture* texture, SDL_GPUSampler* s = nullptr) { bindings[count++] = { texture->GetHandle(), s ? s : sampler }; };

		bind(wireframe || (m_MeshletSettings.Colorize && draw.IndirectFirst >= 0) ? white : Texture(baseMap, true, white));
		if (draw.Kind == ShaderKind::Lit)
		{
			bind(Texture(data.NormalMap, false, m_Device.GetFlatNormalTexture()));
			bind(Texture(data.AmbientOcclusionMap, false, white));
			bind(Texture(data.MetallicMap, false, white));
			bind(Texture(data.RoughnessMap, false, white));
			bind(Texture(data.SpecularMap, false, white));
			bind(Texture(data.EmissiveMap, true, white));
			bind(Texture(data.SubsurfaceMap, false, white));
			for (const auto& map : m_ShadowMaps)
				bind(map.Get(), m_Device.GetShadowSampler());
		}
		else if (draw.Kind == ShaderKind::Glass)
		{
			bind(Texture(data.NormalMap, false, m_Device.GetFlatNormalTexture()));
			bind(Texture(data.RoughnessMap, false, white));
			bind(m_SceneCopy.Get(), m_Device.GetClampSampler());
		}
		if (draw.Kind != ShaderKind::Unlit)
		{
			SDL_GPUBuffer* environment = m_EnvironmentBuffer ? m_EnvironmentBuffer->GetHandle() : m_EmptyEnvironment->GetHandle();
			SDL_BindGPUFragmentStorageBuffers(pass, 0, &environment, 1);
		}
		SDL_BindGPUFragmentSamplers(pass, 0, bindings, count);
		if (draw.IndirectFirst >= 0)
		{
			if (m_MeshletSettings.Colorize)
			{
				material = MaterialUniforms();
				material.Params2.Y = 0;
				SDL_PushGPUFragmentUniformData(cmd, 0, &material, sizeof(material));
				for (uint32 i = 0; i < draw.MeshletCount; ++i)
				{
					uint32 hash = (draw.FirstIndex + i + 1) * 2654435761u ^ (static_cast<uint32>(draw.Value) * 2246822519u);
					object.Tint = { 0.2f + 0.8f * (hash & 255) / 255.0f, 0.2f + 0.8f * ((hash >> 8) & 255) / 255.0f, 0.2f + 0.8f * ((hash >> 16) & 255) / 255.0f, 1 };
					SDL_PushGPUVertexUniformData(cmd, 0, &object, sizeof(object));
					SDL_DrawGPUIndexedPrimitivesIndirect(pass, m_Culling.Commands(), (draw.IndirectFirst + i) * sizeof(SDL_GPUIndexedIndirectDrawCommand), 1);
				}
			}
			else SDL_DrawGPUIndexedPrimitivesIndirect(pass, m_Culling.Commands(), draw.IndirectFirst * sizeof(SDL_GPUIndexedIndirectDrawCommand), draw.MeshletCount);
		}
		else SDL_DrawGPUIndexedPrimitives(pass, sprite ? 6 : draw.IndexCount, 1, sprite ? 0 : draw.FirstIndex, 0, 0);
	}

	void Renderer::PushBones(SDL_GPUCommandBuffer* cmd, const Array<Mat4>* skin)
	{
		// Without a pose every bone is identity, which is the bind pose.
		const int32 count = skin ? (skin->Num() < MaxSkinBones ? skin->Num() : MaxSkinBones) : 0;
		for (int32 i = 0; i < MaxSkinBones; ++i)
			m_BoneUpload[i] = i < count ? (*skin)[i] : Mat4();
		SDL_PushGPUVertexUniformData(cmd, 1, m_BoneUpload.Data(), static_cast<uint32>(MaxSkinBones * sizeof(Mat4)));
	}

	SDL_GPUGraphicsPipeline* Renderer::CreateShadowPipeline(Surface surface, bool occlusion)
	{
		SDL_GPUDevice* gpu = m_Device.GetHandle();
		const bool skinned = surface == Surface::SkinnedMesh;
		SDL_GPUShader* vert = skinned ? m_Device.LoadShader("SkinnedMesh.vert", 0, 2) : m_Device.LoadShader("Mesh.vert", 0, 1);
		SDL_GPUShader* frag = m_Device.LoadShader("Shadow.frag", 0, 0);
		SDL_GPUGraphicsPipeline* pipeline = nullptr;
		if (vert && frag)
		{
			SDL_GPUVertexBufferDescription buffers[2] = {};
			buffers[0] = { 0, sizeof(MeshVertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0 };
			buffers[1] = { 1, sizeof(SkinVertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0 };
			SDL_GPUVertexAttribute attributes[6] = {};
			attributes[0] = { 0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(MeshVertex, Position) };
			attributes[1] = { 1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(MeshVertex, Normal) };
			attributes[2] = { 2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(MeshVertex, UV) };
			attributes[3] = { 3, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(MeshVertex, Tangent) };
			attributes[4] = { 4, 1, SDL_GPU_VERTEXELEMENTFORMAT_USHORT4, offsetof(SkinVertex, Bones) };
			attributes[5] = { 5, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(SkinVertex, Weights) };

			SDL_GPUGraphicsPipelineCreateInfo info = {};
			info.vertex_shader = vert;
			info.fragment_shader = frag;
			info.vertex_input_state.vertex_buffer_descriptions = buffers;
			info.vertex_input_state.num_vertex_buffers = skinned ? 2 : 1;
			info.vertex_input_state.vertex_attributes = attributes;
			info.vertex_input_state.num_vertex_attributes = skinned ? 6 : 4;
			info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
			// Both faces cast so open meshes like planes still shadow. The slope bias keeps
			// lit surfaces from shadowing themselves. The camera occlusion prepass instead
			// matches scene back-face culling and uses unbiased depth.
			info.rasterizer_state.cull_mode = occlusion ? SDL_GPU_CULLMODE_BACK : SDL_GPU_CULLMODE_NONE;
			info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
			info.rasterizer_state.enable_depth_bias = !occlusion;
			info.rasterizer_state.depth_bias_constant_factor = 2;
			info.rasterizer_state.depth_bias_slope_factor = 2;
			info.depth_stencil_state.enable_depth_test = true;
			info.depth_stencil_state.enable_depth_write = true;
			info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
			info.target_info.num_color_targets = 0;
			info.target_info.has_depth_stencil_target = true;
			info.target_info.depth_stencil_format = occlusion ? m_Device.GetDepthFormat() : m_Device.GetShadowFormat();
			pipeline = SDL_CreateGPUGraphicsPipeline(gpu, &info);
			if (!pipeline)
				FP_LOG_WARN("%s depth pipeline failed: %s", occlusion ? "Occlusion" : "Shadow", SDL_GetError());
		}
		if (vert)
			SDL_ReleaseGPUShader(gpu, vert);
		if (frag)
			SDL_ReleaseGPUShader(gpu, frag);
		return pipeline;
	}

	void Renderer::RenderShadows(SDL_GPUCommandBuffer* cmd, World& world, const RenderView& view)
	{
		for (Vec4& params : m_Frame.ShadowParams)
			params = {};
		entt::registry& registry = world.GetRegistry();
		const bool hasCasters = !registry.view<MeshComponent>().empty() || !registry.view<SkeletalMeshComponent>().empty();
		const bool canRender = hasCasters && m_Mode == ViewMode::Lit && m_ShadowPipelines[0] && m_ShadowPipelines[1];
		auto renderMap = [&](int32 slot, int32 type, bool enabled, int32 requestedSize, const ShadowView& shadow,
			float worldTexel, float strength, Vec3 lightPosition, float range) {
			const uint32 size = enabled ? static_cast<uint32>(std::clamp(requestedSize, 64, type == 0 ? 4096 : 2048)) : 16;
			if (!m_ShadowMaps[slot] || m_ShadowMaps[slot]->GetWidth() != size)
			{
				m_ShadowMaps[slot] = m_Device.CreateRenderTarget(m_Device.GetShadowFormat(), size, size, true, true);
				m_ShadowMapCleared[slot] = false;
			}
			if (!m_ShadowMaps[slot] || (!enabled && m_ShadowMapCleared[slot]))
				return;
			SDL_GPUDepthStencilTargetInfo depth = {};
			depth.texture = m_ShadowMaps[slot]->GetHandle();
			depth.clear_depth = 1;
			depth.load_op = SDL_GPU_LOADOP_CLEAR;
			depth.store_op = SDL_GPU_STOREOP_STORE;
			depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
			depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
			SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, nullptr, 0, &depth);
			if (!pass)
				return;
			m_ShadowMapCleared[slot] = true;
			if (!enabled)
			{
				SDL_EndGPURenderPass(pass);
				return;
			}
			m_Frame.ShadowMatrix[slot] = shadow.ToTexture;
			m_Frame.ShadowParams[type] = { std::clamp(strength, 0.0f, 1.0f),
				1.0f / static_cast<float>(size), worldTexel * 1.5f, static_cast<float>(m_ShadowLights[type]) };

			// Draw opaque sections. A range check skips distant meshes for local lights.
			auto cast = [&](Entity entity, const GPUMesh* mesh, int32 pipeline, const Array<Mat4>* skin, float reach) {
				const Mat4 model = world.GetWorldMatrix(entity);
				float largest = 0;
				for (int c = 0; c < 3; ++c)
					largest = std::fmax(largest, Vec3(model.At(c, 0), model.At(c, 1), model.At(c, 2)).Length());
				const Vec3 center = model.TransformPoint((mesh->BoundsMin + mesh->BoundsMax) * 0.5f);
				const float radius = (mesh->BoundsMax - mesh->BoundsMin).Length() * 0.5f * largest * reach;
				if (type == 0 ? !shadow.Touches(center, radius) : (center - lightPosition).Length() > range + radius)
					return;

				ObjectUniforms object;
				object.ViewProjection = shadow.ViewProjection;
				object.Model = model;
				SDL_BindGPUGraphicsPipeline(pass, m_ShadowPipelines[pipeline]);
				SDL_PushGPUVertexUniformData(cmd, 0, &object, sizeof(object));
				if (pipeline == 1)
					PushBones(cmd, skin);
				const SDL_GPUBufferBinding vertices[2] = { { mesh->Vertices->GetHandle(), 0 }, { mesh->Skin ? mesh->Skin->GetHandle() : nullptr, 0 } };
				SDL_BindGPUVertexBuffers(pass, 0, vertices, pipeline == 1 ? 2 : 1);
				const SDL_GPUBufferBinding indices = { mesh->Indices->GetHandle(), 0 };
				SDL_BindGPUIndexBuffer(pass, &indices, SDL_GPU_INDEXELEMENTSIZE_32BIT);

				const MaterialComponent* materials = world.TryGet<MaterialComponent>(entity);
				for (int32 i = 0; i < mesh->Sections.Num(); ++i)
				{
					const Material* material = materials ? MaterialLibrary::Get().Load(materials->GetForSection(i)) : nullptr;
					if (material && material->GetData().Shader == "Glass")
						continue;
					SDL_DrawGPUIndexedPrimitives(pass, mesh->Sections[i].IndexCount, 1, mesh->Sections[i].FirstIndex, 0, 0);
				}
			};
			for (auto [entity, transform, meshComponent] : world.View<TransformComponent, MeshComponent>().each())
			{
				if (const GPUMesh* mesh = m_Meshes.Get(meshComponent.Mesh))
					cast(entity, mesh, 0, nullptr, 1);
			}
			for (auto [entity, transform, skeletal] : world.View<TransformComponent, SkeletalMeshComponent>().each())
			{
				// Animation can reach past the bind pose bounds, so cull them loosely.
				const SkeletalPoseComponent* pose = world.TryGet<SkeletalPoseComponent>(entity);
				if (const GPUMesh* mesh = m_Meshes.GetSkinned(skeletal.Mesh))
					cast(entity, mesh, 1, pose ? &pose->Skin : nullptr, 1.5f);
			}
			SDL_EndGPURenderPass(pass);
		};

		const auto* directional = world.IsValid(m_ShadowCasters[0]) ? world.TryGet<DirectionalLightComponent>(m_ShadowCasters[0]) : nullptr;
		const bool sunEnabled = canRender && directional;
		const int32 sunSize = sunEnabled ? std::clamp(directional->ShadowResolution, 64, 4096) : 16;
		const ShadowView sun = sunEnabled ? FitShadowView(view, world.GetWorldRotation(m_ShadowCasters[0]), directional->ShadowDistance, sunSize) : ShadowView{};
		renderMap(0, 0, sunEnabled, sunSize, sun, sunEnabled ? sun.Radius * 2 / sunSize : 0,
			sunEnabled ? directional->ShadowStrength : 0, {}, 0);

		const auto* spot = world.IsValid(m_ShadowCasters[1]) ? world.TryGet<SpotLightComponent>(m_ShadowCasters[1]) : nullptr;
		const bool spotEnabled = canRender && spot && spot->Range > 0;
		const Vec3 spotPosition = spotEnabled ? world.GetWorldPosition(m_ShadowCasters[1]) : Vec3{};
		const float spotAngle = spotEnabled ? std::clamp(spot->OuterAngle, 1.0f, 89.0f) : 35.0f;
		const ShadowView spotView = spotEnabled ? PerspectiveShadowView(spotPosition, world.GetWorldRotation(m_ShadowCasters[1]), spotAngle, spot->Range) : ShadowView{};
		const int32 spotSize = spotEnabled ? std::clamp(spot->ShadowResolution, 64, 2048) : 16;
		renderMap(1, 1, spotEnabled, spotSize, spotView, spotEnabled ? 2 * spot->Range * std::tan(spotAngle * DegToRad) / spotSize : 0,
			spotEnabled ? spot->ShadowStrength : 0, spotPosition, spotEnabled ? spot->Range : 0);

		const auto* point = world.IsValid(m_ShadowCasters[2]) ? world.TryGet<PointLightComponent>(m_ShadowCasters[2]) : nullptr;
		const bool pointEnabled = canRender && point && point->Range > 0;
		const Vec3 pointPosition = pointEnabled ? world.GetWorldPosition(m_ShadowCasters[2]) : Vec3{};
		const int32 pointSize = pointEnabled ? std::clamp(point->ShadowResolution, 64, 2048) : 16;
		for (int32 face = 0; face < 6; ++face)
		{
			const ShadowView faceView = pointEnabled ? PerspectiveShadowView(pointPosition, PointShadowFaceRotation(face), 46.0f, point->Range) : ShadowView{};
			renderMap(2 + face, 2, pointEnabled, pointSize, faceView, pointEnabled ? 2 * point->Range * std::tan(46.0f * DegToRad) / pointSize : 0,
				pointEnabled ? point->ShadowStrength : 0, pointPosition, pointEnabled ? point->Range : 0);
		}
	}

	Array<GPUTexture*> Renderer::PrepareUI(SDL_GPUCommandBuffer* cmd, World& world, const RenderView& view, uint32 width, uint32 height)
	{
		struct Page
		{
			int32 Order;
			GPUTexture* Texture;
		};
		Array<Page> pages;
		Array<UITexture> kept;
		for (auto [entity, ui, state] : world.View<UIComponent, UIStateComponent>().each())
		{
			state.Drawn = false;
			// A UI on a camera belongs to that camera's view. Others show on every view.
			if (!ui.Visible || !state.Document || !m_UIPipeline || (world.Has<CameraComponent>(entity) && entity != view.Camera))
				continue;
			state.Drawn = true;
			state.Width = width;
			state.Height = height;
			state.Scale = view.PixelScale;

			UIDocumentInstance& document = *state.Document;
			const uint32 key = static_cast<uint32>(entt::to_integral(entity));
			const int32 found = m_UITextures.FindIf([key](const UITexture& t) { return t.Entity == key; });
			UITexture cached = found >= 0 ? m_UITextures[found] : UITexture{ key, nullptr, 0 };
			if (document.GetWidth() == 0 || document.GetHeight() == 0)
				continue;
			if (!cached.Texture || cached.Texture->GetWidth() != document.GetWidth() || cached.Texture->GetHeight() != document.GetHeight())
			{
				cached.Texture = m_Device.CreateDynamicTexture(SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB, document.GetWidth(), document.GetHeight());
				cached.Version = ~0ull;
			}
			if (cached.Texture && cached.Version != document.GetVersion())
			{
				document.ReadPixels([&](const void* pixels, uint32 pageWidth, uint32 pageHeight, uint32 rowBytes) {
					if (pageWidth == cached.Texture->GetWidth() && pageHeight == cached.Texture->GetHeight())
						m_Device.UploadToTexture(cmd, *cached.Texture, pixels, rowBytes);
				});
				cached.Version = document.GetVersion();
			}
			if (cached.Texture)
			{
				pages.Add({ ui.Order, cached.Texture.Get() });
				kept.Add(cached);
			}
		}
		// Pages no longer on screen let go of their textures.
		m_UITextures = kept;
		pages.Sort([](const Page& a, const Page& b) { return a.Order < b.Order; });
		Array<GPUTexture*> textures;
		for (const Page& page : pages)
			textures.Add(page.Texture);
		return textures;
	}

	void Renderer::PrepareMeshlets(SDL_GPUCommandBuffer* cmd, World& world, const RenderView& view, Array<Draw>& draws)
	{
		m_ActiveMeshlets = 0;
		if (!m_MeshletSettings.Enabled || !HasMeshletCulling()) return;
		if (!m_HasCullingView || !m_MeshletSettings.FreezeCamera)
		{
			m_CullingView = view;
			m_HasCullingView = true;
		}
		const uint32 width = m_SceneColor->GetWidth(), height = m_SceneColor->GetHeight();
		if (!m_CullDepth || m_CullDepth->GetWidth() != width || m_CullDepth->GetHeight() != height)
			m_CullDepth = m_Device.CreateRenderTarget(m_Device.GetDepthFormat(), width, height, true, true);
		if (!m_CullDepth) return;
		MeshletSettings settings = m_MeshletSettings;
		settings.Occlusion &= m_Mode != ViewMode::Wireframe;
		const Mat4 clip = m_CullingView.GetViewProjection();
		Array<MeshletCullItem> items;
		for (Draw& draw : draws)
		{
			// Animated bounds need deformation-aware bounds; transparent surfaces keep their ordered path.
			if (draw.Type != Surface::Mesh || draw.Kind == ShaderKind::Glass) continue;
			if (draw.Mat && draw.Mat->GetData().Shader == "Glass") continue;
			const int32 first = items.Num();
			const Mat4 localToClip = clip * world.GetWorldMatrix(draw.Value);
			for (const auto& meshlet : draw.Mesh->Meshlets)
			{
				if (meshlet.FirstIndex < draw.FirstIndex || meshlet.FirstIndex >= draw.FirstIndex + draw.IndexCount) continue;
				items.Add({ localToClip, Vec4(meshlet.BoundsMin, 0), Vec4(meshlet.BoundsMax, 0), meshlet.FirstIndex, meshlet.IndexCount, {} });
			}
			if (items.Num() > first) { draw.IndirectFirst = first; draw.MeshletCount = static_cast<uint32>(items.Num() - first); }
		}
		if (items.IsEmpty()) return;
		// Current-frame depth avoids temporal false occlusion when objects or cameras move.
		SDL_GPUDepthStencilTargetInfo depth{};
		depth.texture = m_CullDepth->GetHandle(); depth.clear_depth = 1;
		depth.load_op = SDL_GPU_LOADOP_CLEAR; depth.store_op = SDL_GPU_STOREOP_STORE;
		depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE; depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
		auto* pass = SDL_BeginGPURenderPass(cmd, nullptr, 0, &depth);
		if (settings.Occlusion)
		{
			SDL_BindGPUGraphicsPipeline(pass, m_OcclusionPipeline);
			for (const Draw& draw : draws)
			{
				if (draw.IndirectFirst < 0) continue;
				// Cutout textures cannot safely occlude without reproducing their alpha test.
				const MaterialData defaults;
				const auto& data = draw.Mat ? draw.Mat->GetData() : defaults;
				if (data.BaseColor.A < data.AlphaCutoff || (!data.BaseColorMap.IsEmpty() && data.AlphaCutoff > 0)) continue;
				ObjectUniforms object;
				object.ViewProjection = clip; object.Model = world.GetWorldMatrix(draw.Value);
				object.NormalMatrix = object.Model.Inverse().Transposed();
				SDL_PushGPUVertexUniformData(cmd, 0, &object, sizeof(object));
				SDL_GPUBufferBinding vertices{ draw.Mesh->Vertices->GetHandle(), 0 }, indices{ draw.Mesh->Indices->GetHandle(), 0 };
				SDL_BindGPUVertexBuffers(pass, 0, &vertices, 1);
				SDL_BindGPUIndexBuffer(pass, &indices, SDL_GPU_INDEXELEMENTSIZE_32BIT);
				SDL_DrawGPUIndexedPrimitives(pass, draw.IndexCount, 1, draw.FirstIndex, 0, 0);
			}
		}
		SDL_EndGPURenderPass(pass);
		if (m_Culling.Dispatch(cmd, m_CullDepth->GetHandle(), width, height, items, settings))
		{
			m_ActiveMeshlets = static_cast<uint32>(items.Num());
			if (settings.Colorize)
				for (Draw& draw : draws) if (draw.IndirectFirst >= 0) draw.Kind = ShaderKind::Unlit;
		}
		else for (Draw& draw : draws) { draw.IndirectFirst = -1; draw.MeshletCount = 0; }
	}

	void Renderer::Render(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* target, uint32 width, uint32 height,
		World& world, const RenderView& view, const DebugLines* lines, const RenderView* cullingView)
	{
		if (!IsValid() || width == 0 || height == 0)
			return;
		m_Mode = view.Mode;
		EnsureTargets(width, height);
		if (!m_SceneColor || !m_SceneCopy || !m_Depth) return;
		BuildFrameUniforms(world, view, width, height);
		PrepareEnvironment(world);
		RenderShadows(cmd, world, view);

		const bool hasLines = lines && !lines->GetVertices().IsEmpty();
		if (hasLines)
			UploadLines(cmd, *lines);
		PrepareParticles(cmd, world, view);

		// Collect everything visible with its shader and surface.
		Array<Draw> draws;
		bool hasGlass = false;
		auto resolve = [&](Entity entity, Surface type, int32 layer, const GPUMesh* mesh, int32 section) {
			const MaterialComponent* materialComponent = world.TryGet<MaterialComponent>(entity);
			const Material* material = materialComponent ? MaterialLibrary::Get().Load(materialComponent->GetForSection(section)) : nullptr;
			// Meshes default to lit so they have shape. Sprites default to unlit so plain 2D games need no lights.
			ShaderKind kind = type != Surface::Sprite ? ShaderKind::Lit : ShaderKind::Unlit;
			if (material)
			{
				const String& shader = material->GetData().Shader;
				kind = shader == "Glass" ? ShaderKind::Glass : shader == "Unlit" ? ShaderKind::Unlit : ShaderKind::Lit;
			}
			// Unlit and wireframe views flatten every material, glass included, to the unlit shader.
			if (m_Mode != ViewMode::Lit)
				kind = ShaderKind::Unlit;
			hasGlass |= kind == ShaderKind::Glass;
			const MeshSection* part = mesh ? &mesh->Sections[section] : nullptr;
			const SkeletalPoseComponent* pose = type == Surface::SkinnedMesh ? world.TryGet<SkeletalPoseComponent>(entity) : nullptr;
			draws.Add({ layer, Vec3::Dot(world.GetWorldPosition(entity) - view.CameraPosition, view.CameraForward), entity, kind, type, material, mesh,
				part ? part->FirstIndex : 0, part ? part->IndexCount : 6, pose ? &pose->Skin : nullptr });
		};
		for (auto [entity, transform, sprite] : world.View<TransformComponent, SpriteComponent>().each())
			resolve(entity, Surface::Sprite, sprite.Layer, nullptr, 0);
		// One draw per section so each can use its own material and shader.
		for (auto [entity, transform, meshComponent] : world.View<TransformComponent, MeshComponent>().each())
		{
			if (const GPUMesh* mesh = m_Meshes.Get(meshComponent.Mesh))
			{
				for (int32 section = 0; section < mesh->Sections.Num(); ++section)
					resolve(entity, Surface::Mesh, 0, mesh, section);
			}
		}
		for (auto [entity, transform, skeletal] : world.View<TransformComponent, SkeletalMeshComponent>().each())
		{
			if (const GPUMesh* mesh = m_Meshes.GetSkinned(skeletal.Mesh))
			{
				for (int32 section = 0; section < mesh->Sections.Num(); ++section)
					resolve(entity, Surface::SkinnedMesh, 0, mesh, section);
			}
		}

		// Opaque meshes front to back, then sprites by layer back to front, then glass back to front.
		auto bucket = [](const Draw& d) { return d.Kind == ShaderKind::Glass ? 2 : d.Type != Surface::Sprite ? 0 : 1; };
		std::stable_sort(draws.begin(), draws.end(), [&](const Draw& a, const Draw& b) {
			const int ba = bucket(a);
			const int bb = bucket(b);
			if (ba != bb)
				return ba < bb;
			if (ba == 0)
				return a.Depth < b.Depth;
			if (ba == 1 && a.Layer != b.Layer)
				return a.Layer < b.Layer;
			return a.Depth > b.Depth;
		});

		PrepareMeshlets(cmd, world, cullingView ? *cullingView : view, draws);
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
			return SDL_BeginGPURenderPass(cmd, &color, 1, &depth);
		};

		auto drawRange = [&](SDL_GPURenderPass* pass, int32 from, int32 to) {
			SDL_GPUGraphicsPipeline* bound = nullptr;
			for (int32 i = from; i < to; ++i)
			{
				SDL_GPUGraphicsPipeline* pipeline = GetPipeline(draws[i]);
				if (pipeline != bound)
				{
					SDL_BindGPUGraphicsPipeline(pass, pipeline);
					bound = pipeline;
				}
				DrawObject(cmd, pass, world, draws[i], viewProjection);
			}
		};

		int32 firstGlass = 0;
		while (firstGlass < draws.Num() && draws[firstGlass].Kind != ShaderKind::Glass)
			++firstGlass;

		SDL_GPURenderPass* pass = beginScenePass(true);
		DrawSkybox(cmd, pass, view);
		drawRange(pass, 0, firstGlass);
		// Particles go before glass so glass refracts them.
		DrawParticles(cmd, pass, viewProjection);
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
			drawRange(pass, firstGlass, draws.Num());
			SDL_EndGPURenderPass(pass);
		}

		DrawBlackHoles(cmd, world, view, width, height);

		const Array<GPUTexture*> pages = PrepareUI(cmd, world, view, width, height);

		PostProcessComponent settings;
		for (Entity entity : world.GetEntities())
		{
			if (const auto* post = world.TryGet<PostProcessComponent>(entity))
			{
				settings = *post;
				break;
			}
		}
		// Keep bloom out of diagnostic view modes.
		if (view.Mode != ViewMode::Lit)
		{
			settings.Bloom = false;
		}
		auto drawFullscreen = [&](SDL_GPUTexture* destination, SDL_GPUGraphicsPipeline* pipeline,
			const SDL_GPUTextureSamplerBinding* sources, uint32 count, const void* uniforms, uint32 bytes) {
			SDL_GPUColorTargetInfo output = {};
			output.texture = destination;
			output.load_op = SDL_GPU_LOADOP_DONT_CARE;
			output.store_op = SDL_GPU_STOREOP_STORE;
			SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &output, 1, nullptr);
			SDL_BindGPUGraphicsPipeline(pass, pipeline);
			SDL_PushGPUFragmentUniformData(cmd, 0, uniforms, bytes);
			SDL_BindGPUFragmentSamplers(pass, 0, sources, count);
			SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
			SDL_EndGPURenderPass(pass);
		};
		GPUTexture* bloom = m_Device.GetBlackTexture();
		if (settings.Bloom && settings.BloomIntensity > 0)
		{
			const uint32 bw = std::max(1u, (width + 3) / 4), bh = std::max(1u, (height + 3) / 4);
			if (!m_Bloom[0] || !m_Bloom[1] || m_Bloom[0]->GetWidth() != bw || m_Bloom[0]->GetHeight() != bh)
				for (auto& texture : m_Bloom) texture = m_Device.CreateRenderTarget(SceneFormat, bw, bh, false);
			if (m_Bloom[0] && m_Bloom[1])
			{
				SDL_GPUTextureSamplerBinding source{ m_SceneColor->GetHandle(), m_Device.GetClampSampler() };
				Vec4 params{ 1.0f / width, 1.0f / height, std::clamp(settings.BloomThreshold, 0.0f, 65504.0f), 0 };
				drawFullscreen(m_Bloom[0]->GetHandle(), m_BloomPipeline, &source, 1, &params, sizeof(params));
				source.texture = m_Bloom[0]->GetHandle();
				params = { 1.0f / bw, 0, 0, 1 };
				drawFullscreen(m_Bloom[1]->GetHandle(), m_BloomPipeline, &source, 1, &params, sizeof(params));
				source.texture = m_Bloom[1]->GetHandle();
				params = { 0, 1.0f / bh, 0, 1 };
				drawFullscreen(m_Bloom[0]->GetHandle(), m_BloomPipeline, &source, 1, &params, sizeof(params));
				bloom = m_Bloom[0].Get();
			}
		}
		else
		{
			for (auto& texture : m_Bloom) texture.Reset();
		}
		if (settings.FXAA && (!m_PostColor || m_PostColor->GetWidth() != width || m_PostColor->GetHeight() != height))
			m_PostColor = m_Device.CreateRenderTarget(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB, width, height, false);
		if (!settings.FXAA) m_PostColor.Reset();
		const bool fxaa = settings.FXAA && m_PostColor;
		const EngineSettings& accessibility = EngineSettings::Get();
		const auto& modes = EngineSettings::ColorBlindModes();
		int32 colorMode = 0;
		for (int32 i = 0; i < modes.Num(); ++i)
			if (accessibility.ColorBlindMode == modes[i]) colorMode = i;
		const Vec4 present[2] = {
			{ view.Exposure, view.Tonemap ? 1.0f : 0.0f, std::clamp(settings.BloomIntensity, 0.0f, 10.0f), static_cast<float>(colorMode) },
			{ std::clamp(accessibility.ColorBlindStrength, 0.0f, 1.0f), 0, 0, 0 }
		};
		SDL_GPUTextureSamplerBinding sources[2] = {
			{ m_SceneColor->GetHandle(), m_Device.GetClampSampler() }, { bloom->GetHandle(), m_Device.GetClampSampler() }
		};
		drawFullscreen(fxaa ? m_PostColor->GetHandle() : target, fxaa ? m_PostPipeline : m_PresentPipeline, sources, 2, present, sizeof(present));
		if (fxaa)
		{
			const Vec4 params{ 1.0f / width, 1.0f / height, 0, 0 };
			const SDL_GPUTextureSamplerBinding source{ m_PostColor->GetHandle(), m_Device.GetClampSampler() };
			drawFullscreen(target, m_FXAAPipeline, &source, 1, &params, sizeof(params));
		}
		if (!hasLines && pages.IsEmpty()) return;

		// UI pages and editor lines stay sharp above the post-processed scene.
		SDL_GPUColorTargetInfo output = {};
		output.texture = target;
		output.load_op = SDL_GPU_LOADOP_LOAD;
		output.store_op = SDL_GPU_STOREOP_STORE;
		pass = SDL_BeginGPURenderPass(cmd, &output, 1, nullptr);

		if (!pages.IsEmpty())
		{
			SDL_BindGPUGraphicsPipeline(pass, m_UIPipeline);
			for (GPUTexture* page : pages)
			{
				SDL_GPUTextureSamplerBinding binding = { page->GetHandle(), m_Device.GetClampSampler() };
				SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
				SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
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
