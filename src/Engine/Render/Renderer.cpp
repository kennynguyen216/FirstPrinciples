#include "Engine/Render/Renderer.h"
#include "Engine/Material.h"
#include "Engine/Animation/SkeletalModel.h"
#include "HAL/PlatformMisc.h"

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
		: m_Device(device), m_TargetFormat(targetFormat), m_Textures(device), m_Meshes(device)
	{
		m_QuadVertices = device.CreateBuffer(SDL_GPU_BUFFERUSAGE_VERTEX, QuadVertices, sizeof(QuadVertices));
		m_QuadIndices = device.CreateBuffer(SDL_GPU_BUFFERUSAGE_INDEX, QuadIndices, sizeof(QuadIndices));
		m_BoneUpload.Resize(MaxSkinBones);
		CreatePipelines();
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
		for (SDL_GPUGraphicsPipeline* pipeline : { m_PresentPipeline, m_LinePipeline, m_WirePipelines[0], m_WirePipelines[1], m_WirePipelines[2] })
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
		return m_PresentPipeline && m_LinePipeline;
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
		SDL_GPUShader* frag = m_Device.LoadShader(fragment, numSamplers, numUniforms);
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

	void Renderer::CreatePipelines()
	{
		SDL_GPUDevice* gpu = m_Device.GetHandle();
		for (Surface surface : { Surface::Sprite, Surface::Mesh, Surface::SkinnedMesh })
		{
			const int column = static_cast<int>(surface);
			m_ScenePipelines[static_cast<int>(ShaderKind::Unlit)][column] = CreateMeshPipeline("Unlit.frag", 1, 1, surface, false);
			m_ScenePipelines[static_cast<int>(ShaderKind::Lit)][column] = CreateMeshPipeline("Lit.frag", 8, 2, surface, false);
			m_ScenePipelines[static_cast<int>(ShaderKind::Glass)][column] = CreateMeshPipeline("Glass.frag", 4, 2, surface, true);
			m_WirePipelines[column] = CreateMeshPipeline("Unlit.frag", 1, 1, surface, false, true);
		}
		if (!m_WirePipelines[0] || !m_WirePipelines[1] || !m_WirePipelines[2])
			FP_LOG_WARN("This GPU cannot draw wireframes. The wireframe view mode will show normal shading.");

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
		m_Frame.CameraRight = { view.CameraRight, 0 };
		m_Frame.CameraUp = { view.CameraUp, 0 };
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
		const TransformComponent& transform = world.Get<TransformComponent>(draw.Value);
		const SpriteComponent* sprite = draw.Type == Surface::Sprite ? &world.Get<SpriteComponent>(draw.Value) : nullptr;
		const MaterialData defaults;
		const MaterialData& data = draw.Mat ? draw.Mat->GetData() : defaults;

		ObjectUniforms object;
		object.ViewProjection = viewProjection;
		object.Model = sprite ? transform.GetMatrix() * Mat4::Scale({ sprite->Size, 1 }) : transform.GetMatrix();
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
		{
			// Bones past the pose stay as they were, which is fine since no vertex uses them.
			const int32 count = draw.Skin ? (draw.Skin->Num() < MaxSkinBones ? draw.Skin->Num() : MaxSkinBones) : 0;
			for (int32 i = 0; i < MaxSkinBones; ++i)
				m_BoneUpload[i] = i < count ? (*draw.Skin)[i] : Mat4();
			SDL_PushGPUVertexUniformData(cmd, 1, m_BoneUpload.Data(), static_cast<uint32>(MaxSkinBones * sizeof(Mat4)));
		}

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

		SDL_GPUTextureSamplerBinding bindings[8] = {};
		uint32 count = 0;
		auto bind = [&](GPUTexture* texture, SDL_GPUSampler* s = nullptr) { bindings[count++] = { texture->GetHandle(), s ? s : sampler }; };

		bind(wireframe ? white : Texture(baseMap, true, white));
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
		SDL_DrawGPUIndexedPrimitives(pass, sprite ? 6 : draw.IndexCount, 1, sprite ? 0 : draw.FirstIndex, 0, 0);
	}

	void Renderer::Render(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* target, uint32 width, uint32 height,
		World& world, const RenderView& view, const DebugLines* lines)
	{
		if (!IsValid() || width == 0 || height == 0)
			return;
		m_Mode = view.Mode;
		EnsureTargets(width, height);
		BuildFrameUniforms(world, view, width, height);

		const bool hasLines = lines && !lines->GetVertices().IsEmpty();
		if (hasLines)
			UploadLines(cmd, *lines);

		// Collect everything visible with its shader and surface.
		Array<Draw> draws;
		bool hasGlass = false;
		auto resolve = [&](Entity entity, const TransformComponent& transform, Surface type, int32 layer, const GPUMesh* mesh, int32 section) {
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
			draws.Add({ layer, Vec3::Dot(transform.Position - view.CameraPosition, view.CameraForward), entity, kind, type, material, mesh,
				part ? part->FirstIndex : 0, part ? part->IndexCount : 6, pose ? &pose->Skin : nullptr });
		};
		for (auto [entity, transform, sprite] : world.View<TransformComponent, SpriteComponent>().each())
			resolve(entity, transform, Surface::Sprite, sprite.Layer, nullptr, 0);
		// One draw per section so each can use its own material and shader.
		for (auto [entity, transform, meshComponent] : world.View<TransformComponent, MeshComponent>().each())
		{
			if (const GPUMesh* mesh = m_Meshes.Get(meshComponent.Mesh))
			{
				for (int32 section = 0; section < mesh->Sections.Num(); ++section)
					resolve(entity, transform, Surface::Mesh, 0, mesh, section);
			}
		}
		for (auto [entity, transform, skeletal] : world.View<TransformComponent, SkeletalMeshComponent>().each())
		{
			if (const GPUMesh* mesh = m_Meshes.GetSkinned(skeletal.Mesh))
			{
				for (int32 section = 0; section < mesh->Sections.Num(); ++section)
					resolve(entity, transform, Surface::SkinnedMesh, 0, mesh, section);
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
		drawRange(pass, 0, firstGlass);
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
