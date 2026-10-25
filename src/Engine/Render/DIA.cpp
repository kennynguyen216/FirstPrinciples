#include "Engine/Render/DIA.h"
#include "Engine/Paths.h"
#include "Engine/SceneSerializer.h"
#include "Core/Misc/File.h"

#include <stb_image.h>
#include <SDL3/SDL_timer.h>
#include <algorithm>
#include <cstring>
#include <filesystem>

namespace FP
{
	static_assert(sizeof(DIAMaterial) == 224);
	static_assert(sizeof(DIAInstance) == 192);
	static_assert(sizeof(DIALight) == 64);
	static_assert(sizeof(DIAEmitter) == 48);
	static_assert(sizeof(DIAUniforms) == 304);

	namespace
	{
		constexpr uint32 CacheExtent = 256;
		constexpr uint32 MaxTexels = 16 * 1024 * 1024;

		Vec4 Linear(Color c)
		{
			auto convert = [](float v) { return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); };
			return { convert(c.R), convert(c.G), convert(c.B), c.A };
		}

		uint64 Hash(uint64 hash, const void* data, usize size)
		{
			const uint8* bytes = static_cast<const uint8*>(data);
			for (usize i = 0; i < size; ++i) hash = (hash ^ bytes[i]) * 1099511628211ull;
			return hash;
		}
	}

	DIA::DIA(GPUDevice& device) : m_Device(device), m_Hardware(device)
	{
		const char* backend = SDL_getenv("FP_DIA_RAY_BACKEND");
		if (backend && *backend && std::strcmp(backend, "auto") != 0 && std::strcmp(backend, "software") != 0 && std::strcmp(backend, "hardware") != 0)
		{ m_Error = "Unknown DIA ray backend"; return; }
		m_UseHardware = m_Hardware.IsSupported() && backend && std::strcmp(backend, "hardware") == 0;
		if (backend && std::strcmp(backend, "hardware") == 0 && !m_UseHardware)
		{ m_Error = String("Hardware ray queries unavailable: ") + m_Hardware.GetError(); return; }
		FP_LOG("DIA ray backend: %s", GetRayBackend());
		m_Texels.Add(0xffffffffu);
		m_CachePipeline = CreatePipeline(m_UseHardware ? "DIACacheRT.frag" : "DIACache.frag", 1, 8, 1);
		m_HistoryPipeline = CreatePipeline("DIAHistory.frag", 11, 2, 6);
		m_DenoisePipeline = CreatePipeline("DIADenoise.frag", 8, 2, 2);
		m_ComposePipeline = CreatePipeline("DIACompose.frag", 3, 0, 1);
	}

	DIA::~DIA()
	{
		for (SDL_GPUGraphicsPipeline* pipeline : { m_CachePipeline, m_TracePipeline, m_TraceLitOnePipeline, m_HistoryPipeline, m_DenoisePipeline, m_ComposePipeline,
			m_ResolvePipeline, m_ResolveFallbackPipeline, m_ResolveFallbackLitPipeline })
			if (pipeline) SDL_ReleaseGPUGraphicsPipeline(m_Device.GetHandle(), pipeline);
	}

	SDL_GPUGraphicsPipeline* DIA::CreatePipeline(const char* fragment, uint32 samplers, uint32 buffers, uint32 targets, SDL_GPUTextureFormat format,
		SDL_GPUCompareOp depthTest, bool depthWrite)
	{
		const uint64 start = SDL_GetTicksNS();
		SDL_GPUShader* vertex = m_Device.LoadShader("Present.vert", 0, 0);
		SDL_GPUShader* pixel = m_Device.LoadShader(fragment, samplers, 1, buffers);
		SDL_GPUGraphicsPipeline* pipeline = nullptr;
		if (vertex && pixel)
		{
			SDL_GPUColorTargetDescription outputs[8] = {};
			for (uint32 i = 0; i < targets; ++i) outputs[i].format = format;
			SDL_GPUGraphicsPipelineCreateInfo info = {};
			info.vertex_shader = vertex;
			info.fragment_shader = pixel;
			info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
			info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
			info.target_info.color_target_descriptions = outputs;
			info.target_info.num_color_targets = targets;
			if (depthTest != SDL_GPU_COMPAREOP_INVALID)
			{
				info.depth_stencil_state.compare_op = depthTest;
				info.depth_stencil_state.enable_depth_test = true;
				info.depth_stencil_state.enable_depth_write = depthWrite;
				info.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D16_UNORM;
				info.target_info.has_depth_stencil_target = true;
			}
			const char* statistics = SDL_getenv("FP_GPU_PIPELINE_STATS");
			if (statistics && std::strcmp(statistics, "1") == 0)
			{
				info.props = SDL_CreateProperties();
				if (info.props) SDL_SetStringProperty(info.props, SDL_PROP_GPU_GRAPHICSPIPELINE_CREATE_NAME_STRING, fragment);
			}
			pipeline = SDL_CreateGPUGraphicsPipeline(m_Device.GetHandle(), &info);
			if (info.props) SDL_DestroyProperties(info.props);
		}
		if (vertex) SDL_ReleaseGPUShader(m_Device.GetHandle(), vertex);
		if (pixel) SDL_ReleaseGPUShader(m_Device.GetHandle(), pixel);
		if (!pipeline) m_Error = String::Format("DIA pipeline %s: %s", fragment, SDL_GetError());
		const double elapsed = static_cast<double>(SDL_GetTicksNS() - start) / 1e6;
		if (elapsed >= 1000.0) FP_LOG("DIA pipeline %s prepared in %.1f ms", fragment, elapsed);
		return pipeline;
	}

	bool DIA::LoadTexture(const String& reference, uint32* descriptor, bool srgb)
	{
		descriptor[3] = srgb ? 1 : 0;
		if (reference.IsEmpty()) return true;
		const String path = Paths::AssetReference(reference);
		for (const TextureAllocation& texture : m_Textures)
		{
			if (texture.Path != path) continue;
			descriptor[0] = texture.Offset;
			descriptor[1] = texture.Width;
			descriptor[2] = texture.Height;
			if (!texture.Opaque) descriptor[3] |= 2u;
			return true;
		}
		const String full = Paths::Content(Paths::AssetPath(path));
		std::error_code error;
		const auto encodedSize = std::filesystem::file_size(std::filesystem::path(reinterpret_cast<const char8_t*>(full.CStr())), error);
		String encoded;
		if (error || encodedSize > 64 * 1024 * 1024 || !File::ReadAllText(full.CStr(), encoded)) { m_Error = String("DIA texture file is unavailable or exceeds 64 MiB: ") + path; return false; }
		const auto* bytes = reinterpret_cast<const stbi_uc*>(encoded.CStr());
		int width, height, channels;
		if (!stbi_info_from_memory(bytes, encoded.Length(), &width, &height, &channels) || width <= 0 || height <= 0 ||
			width > 8192 || height > 8192 || static_cast<uint64>(width) * height + m_Texels.Num() > MaxTexels)
		{
			m_Error = String("DIA texture is missing, invalid or exceeds its resident budget: ") + path;
			return false;
		}
		stbi_uc* pixels = stbi_load_from_memory(bytes, encoded.Length(), &width, &height, &channels, 4);
		if (!pixels) { m_Error = String("Could not decode DIA texture: ") + path; return false; }
		const uint32 offset = static_cast<uint32>(m_Texels.Num());
		bool opaque = true;
		m_Texels.Reserve(m_Texels.Num() + width * height);
		for (int i = 0; i < width * height; ++i)
		{
			opaque = opaque && pixels[i * 4 + 3] == 255;
			m_Texels.Add(static_cast<uint32>(pixels[i * 4]) | (static_cast<uint32>(pixels[i * 4 + 1]) << 8) |
				(static_cast<uint32>(pixels[i * 4 + 2]) << 16) | (static_cast<uint32>(pixels[i * 4 + 3]) << 24));
		}
		stbi_image_free(pixels);
		m_Textures.Add({ path, offset, static_cast<uint32>(width), static_cast<uint32>(height), opaque });
		descriptor[0] = offset;
		descriptor[1] = static_cast<uint32>(width);
		descriptor[2] = static_cast<uint32>(height);
		if (!opaque) descriptor[3] |= 2u;
		m_TexturesDirty = true;
		return true;
	}

	bool DIA::PackMaterial(const MaterialData& material, const String& spriteTexture, DIAMaterial& packed)
	{
		if (!ValidateMaterial(material)) { m_Error = "Invalid DIA material values"; return false; }
		if (material.Shader != "Lit" || material.Subsurface > 0)
		{
			m_Error = "DIA's opaque profile requires Lit materials without subsurface transport";
			return false;
		}
		if (material.BaseColor.A != 1) { m_Error = "DIA's opaque profile does not admit blended materials"; return false; }
		packed.BaseColor = Linear(material.BaseColor);
		packed.Emission = { Linear(material.EmissiveColor).XYZ() * material.EmissiveIntensity, 0 };
		packed.Parameters = { material.Metallic, material.Roughness, material.Specular, material.NormalStrength };
		packed.Parameters2 = { material.AmbientOcclusionStrength, material.AlphaCutoff, 0, 0 };
		packed.UVTransform = { material.Tiling.X, material.Tiling.Y, material.Offset.X, material.Offset.Y };
		packed.Flags[0] = material.TwoSided ? 1 : 0;
		packed.Flags[1] = material.Shader == "Unlit" ? 1 : 0;
		const String* paths[8] = { material.BaseColorMap.IsEmpty() ? &spriteTexture : &material.BaseColorMap,
			&material.NormalMap, &material.AmbientOcclusionMap, &material.MetallicMap, &material.RoughnessMap,
			&material.SpecularMap, &material.EmissiveMap, &material.SubsurfaceMap };
		for (int i = 0; i < 8; ++i)
			if (!LoadTexture(*paths[i], packed.Textures[i], i == 0 || i == 6)) return false;
		if (packed.Textures[0][3] & 2u) { m_Error = "DIA's opaque profile does not admit alpha-masked textures"; return false; }
		return true;
	}

	void DIA::Reclaim(World& world, MeshLibrary& meshes)
	{
		Array<String> names, textures;
		for (Entity entity : world.GetEntities())
		{
			const auto* mesh = world.TryGet<MeshComponent>(entity);
			const auto* sprite = world.TryGet<SpriteComponent>(entity);
			if (!mesh && !sprite) continue;
			if (mesh) names.AddUnique(Paths::AssetReference(mesh->Mesh));
			if (sprite) { names.AddUnique("Quad"); textures.AddUnique(Paths::AssetReference(sprite->Texture)); }
			if (const auto* component = world.TryGet<MaterialComponent>(entity))
			{
				Array<String> materials = component->Slots;
				materials.Add(component->Material);
				for (const String& path : materials)
				if (const Material* material = MaterialLibrary::Get().Load(path))
				{
					const auto& data = material->GetData();
					for (const String* texture : { &data.BaseColorMap, &data.NormalMap, &data.AmbientOcclusionMap, &data.MetallicMap, &data.RoughnessMap, &data.SpecularMap, &data.EmissiveMap, &data.SubsurfaceMap }) textures.AddUnique(Paths::AssetReference(*texture));
				}
			}
		}
		if (m_Meshes.RemoveAll([&](const MeshAllocation& allocation) {
			if (!names.Contains(allocation.Name)) return true;
			const GPUMesh* mesh = meshes.Get(allocation.Name);
			return !mesh || (mesh->RayGeometry != allocation.Mesh && !mesh->RaySections.Contains(allocation.Mesh));
		}))
		{
			m_MeshNodes.Clear();
			m_Triangles.Clear();
			for (MeshAllocation& allocation : m_Meshes)
			{
				allocation.NodeOffset = static_cast<uint32>(m_MeshNodes.Num());
				allocation.TriangleOffset = static_cast<uint32>(m_Triangles.Num());
				for (RayNode node : allocation.Mesh->Nodes)
				{
					node.First += allocation.TriangleOffset;
					node.Right += allocation.NodeOffset;
					node.Escape += allocation.NodeOffset;
					m_MeshNodes.Add(node);
				}
				m_Triangles.Append(allocation.Mesh->Triangles);
			}
			m_EmitterHash = 0;
			m_GeometryDirty = true;
		}
		meshes.ReleaseUnusedRayGeometry(names);
		if (m_Textures.RemoveAll([&](const TextureAllocation& allocation) { return !textures.Contains(allocation.Path); }))
		{
			Array<uint32> texels{ 0xffffffffu };
			for (TextureAllocation& allocation : m_Textures)
			{
				const uint32 old = allocation.Offset;
				allocation.Offset = static_cast<uint32>(texels.Num());
				for (uint32 pixel = 0; pixel < allocation.Width * allocation.Height; ++pixel) texels.Add(m_Texels[static_cast<int32>(old + pixel)]);
			}
			m_Texels = std::move(texels);
			m_TexturesDirty = true;
			m_EmitterHash = 0;
		}
	}

	bool DIA::Extract(World& world, MeshLibrary& meshes, const DIAComponent& settings)
	{
		if (!SceneSerializer::Validate(world)) { m_Error = "Invalid authored values in the current scene"; return false; }
		if (m_World != &world || m_WorldGeneration != world.GetSceneGeneration())
		{
			m_Surfaces.Clear();
			m_CacheCount = 0;
			m_CacheCursor = 0;
			m_CacheValid = false;
			m_HistoryValid = false;
			for (auto& view : m_Views) view.Valid = false;
			m_World = &world;
			m_WorldGeneration = world.GetSceneGeneration();
		}
		Reclaim(world, meshes);
		Array<RayInstance> instances;
		Array<Vec4> tints;
		m_Materials.Clear();
		m_Lights.Clear();
		for (Entity entity : world.GetEntities())
		{
			const TransformComponent* transform = world.TryGet<TransformComponent>(entity);
			const MeshComponent* entityMesh = world.TryGet<MeshComponent>(entity);
			const SpriteComponent* entitySprite = world.TryGet<SpriteComponent>(entity);
			if (!transform) continue;
			if (const auto* skeletal = world.TryGet<SkeletalMeshComponent>(entity); skeletal && !skeletal->Mesh.IsEmpty())
			{ m_Error = "DIA does not support animated skeletal meshes"; return false; }
			if (!entityMesh && !entitySprite) continue;
			for (uint32 surface = 0; surface < (entityMesh && entitySprite ? 2u : 1u); ++surface)
			{
				const MeshComponent* meshComponent = surface == 0 ? entityMesh : nullptr;
				const SpriteComponent* sprite = meshComponent ? nullptr : entitySprite;
				if (sprite && (sprite->Tint.A != 1 || sprite->Layer != 0)) { m_Error = "DIA sprites require opaque tint and physical depth ordering"; return false; }
				const String name = Paths::AssetReference(meshComponent ? meshComponent->Mesh : String("Quad"));
				GPUMesh* gpuMesh = meshes.Get(name);
				if (!gpuMesh) { m_Error = "DIA mesh unavailable"; return false; }
				if (!gpuMesh->RayGeometry && gpuMesh->RaySections.IsEmpty())
				{
					MeshData data;
					if (!LoadMesh(Paths::AssetPath(name).View(), data)) { m_Error = "DIA mesh source unavailable"; return false; }
					if (MeshFingerprint(data) != gpuMesh->Fingerprint) { m_Error = "Mesh source changed after raster upload; reload the scene"; return false; }
					if (gpuMesh->Sections.Num() <= 1)
					{
						gpuMesh->RayGeometry = RayMesh::Build(data);
						if (!gpuMesh->RayGeometry) { m_Error = "Invalid DIA mesh geometry"; return false; }
						++m_MeshBuildCount;
					}
					else
					{
						if (data.Sections.Num() != gpuMesh->Sections.Num()) { m_Error = "DIA mesh sections changed after raster upload"; return false; }
						Array<RefPtr<RayMesh>> sections;
						for (const MeshSection& section : data.Sections)
						{
							auto geometry = RayMesh::Build(data, section.FirstIndex, section.IndexCount);
							if (!geometry) { m_Error = "Invalid DIA mesh section geometry"; return false; }
							sections.Add(geometry);
							++m_MeshBuildCount;
						}
						gpuMesh->RaySections = std::move(sections);
					}
				}
				for (int32 section = 0; section < std::max(1, gpuMesh->RaySections.Num()); ++section)
				{
					const RefPtr<RayMesh>& geometry = gpuMesh->RaySections.IsEmpty() ? gpuMesh->RayGeometry : gpuMesh->RaySections[section];
					if (geometry->Triangles.IsEmpty()) continue;
					if (instances.Num() == static_cast<int32>(RayScene::MaxInstances)) { m_Error = "DIA instance budget exceeded"; return false; }
					uint32 meshIndex = 0;
					while (meshIndex < static_cast<uint32>(m_Meshes.Num()) && m_Meshes[static_cast<int32>(meshIndex)].Mesh != geometry) ++meshIndex;
					if (meshIndex == static_cast<uint32>(m_Meshes.Num()))
					{
						if (m_Triangles.Num() + geometry->Triangles.Num() > static_cast<int32>(RayScene::MaxTriangles)) { m_Error = "DIA immutable geometry budget exceeded"; return false; }
						const uint32 nodeOffset = static_cast<uint32>(m_MeshNodes.Num());
						const uint32 triangleOffset = static_cast<uint32>(m_Triangles.Num());
						m_Meshes.Add({ name, geometry, nodeOffset, triangleOffset, ++m_GeometryGeneration });
						for (RayNode node : geometry->Nodes)
						{
							node.First += triangleOffset;
							node.Right += nodeOffset;
							node.Escape += nodeOffset;
							m_MeshNodes.Add(node);
						}
						for (const RayTriangle& triangle : geometry->Triangles) m_Triangles.Add(triangle);
						m_GeometryDirty = true;
					}
					const MaterialComponent* materialComponent = world.TryGet<MaterialComponent>(entity);
					const String materialPath = materialComponent ? materialComponent->GetForSection(section) : String();
					const Material* material = MaterialLibrary::Get().Load(materialPath);
					if (!materialPath.IsEmpty() && !material) { m_Error = "DIA material unavailable"; return false; }
					MaterialData defaults;
					DIAMaterial packed;
					if (!PackMaterial(material ? material->GetData() : defaults, sprite ? sprite->Texture : String(), packed)) return false;
					RayInstance instance;
					instance.Identity = { world.GetSceneGeneration(), static_cast<uint32>(entt::to_integral(entity)), m_Meshes[static_cast<int32>(meshIndex)].Generation * 2 + (sprite ? 1u : 0u) };
					instance.Mesh = geometry;
					instance.Transform = world.GetWorldMatrix(entity);
					if (sprite && !meshComponent) instance.Transform = instance.Transform * Mat4::Scale({ sprite->Size, 1 });
					instance.Material = static_cast<uint32>(m_Materials.Num());
					const Vec3 extent = instance.Mesh->Bounds.Max - instance.Mesh->Bounds.Min;
					const Vec3 x = instance.Transform.TransformVector({ extent.X, 0, 0 });
					const Vec3 y = instance.Transform.TransformVector({ 0, extent.Y, 0 });
					const Vec3 z = instance.Transform.TransformVector({ 0, 0, extent.Z });
					const float area = 2 * (Vec3::Cross(x, y).Length() + Vec3::Cross(x, z).Length() + Vec3::Cross(y, z).Length());
					instance.CaptureGrid = area > 2 * std::max(1u, instance.Mesh->SourceTriangleCount) ? 12 : 4;
					instance.CaptureSides = packed.Flags[0] ? 2 : 1;
					if ((packed.Parameters.X == 1 && !packed.Textures[3][1]) || (packed.BaseColor.X == 0 && packed.BaseColor.Y == 0 && packed.BaseColor.Z == 0)) instance.CaptureGrid = 0;
					instances.Add(instance);
					m_Materials.Add(packed);
					tints.Add(sprite && !meshComponent ? Linear(sprite->Tint) : Vec4(1, 1, 1, 1));
				}
			}
		}
		for (SurfaceAllocation& surface : m_Surfaces)
		{
			surface.Used = false;
			for (const RayInstance& instance : instances)
				if (surface.Identity == instance.Identity && instance.CaptureGrid && surface.Grid >= instance.CaptureGrid && surface.Sides == instance.CaptureSides) surface.Used = true;
		}
		m_Surfaces.Sort([](const SurfaceAllocation& a, const SurfaceAllocation& b) { return a.Offset < b.Offset; });
		for (int32 i = 1; i < m_Surfaces.Num();)
		{
			if (!m_Surfaces[i - 1].Used && !m_Surfaces[i].Used && m_Surfaces[i - 1].Offset + m_Surfaces[i - 1].Size == m_Surfaces[i].Offset)
			{
				m_Surfaces[i - 1].Size += m_Surfaces[i].Size;
				m_Surfaces.RemoveAt(i);
			}
			else ++i;
		}
		while (!m_Surfaces.IsEmpty() && !m_Surfaces.Last().Used)
		{
			m_CacheCount = m_Surfaces.Last().Offset;
			m_Surfaces.RemoveAt(m_Surfaces.Num() - 1);
		}
		m_CacheCursor = m_CacheCount ? m_CacheCursor % m_CacheCount : 0;
		for (RayInstance& instance : instances)
		{
			if (!instance.CaptureGrid) continue;
			SurfaceAllocation* allocation = nullptr;
			for (SurfaceAllocation& surface : m_Surfaces)
				if (surface.Identity == instance.Identity && surface.Used) { allocation = &surface; instance.CaptureGrid = surface.Grid; break; }
			const uint32 size = instance.Mesh->SourceTriangleCount * instance.CaptureGrid * instance.CaptureGrid * instance.CaptureSides;
			if (!allocation)
			{
				for (SurfaceAllocation& surface : m_Surfaces)
					if (!surface.Used && surface.Size >= size) { allocation = &surface; break; }
				if (!allocation)
				{
					if (size > CacheExtent * CacheExtent - m_CacheCount) { m_Error = "DIA surface capture budget exceeded"; return false; }
					m_Surfaces.Add({ instance.Identity, m_CacheCount, size, ++m_CacheGeneration, true, instance.CaptureGrid, instance.CaptureSides });
					allocation = &m_Surfaces.Last();
					m_CacheCount += size;
				}
				else
				{
					const uint32 offset = allocation->Offset;
					const uint32 remaining = allocation->Size - size;
					allocation->Size = size;
					allocation->Identity = instance.Identity;
					allocation->Generation = ++m_CacheGeneration;
					allocation->Used = true;
					allocation->Grid = instance.CaptureGrid;
					allocation->Sides = instance.CaptureSides;
					if (remaining)
					{
						const int32 index = static_cast<int32>(allocation - m_Surfaces.Data());
						m_Surfaces.Add({ {}, offset + size, remaining, 0, false });
						allocation = &m_Surfaces[index];
					}
				}
			}
			instance.CachePage = allocation->Offset;
		}
		if (!m_Scene.Publish(std::move(instances))) { m_Error = "Invalid current ray-scene snapshot"; return false; }
		m_Instances.Clear();
		m_SurfaceOwners.Resize(static_cast<int32>(m_CacheCount));
		for (DIASurface& owner : m_SurfaceOwners) owner = {};
		for (const RayInstance& instance : m_Scene.GetInstances())
		{
			const MeshAllocation* found = nullptr;
			for (const MeshAllocation& allocation : m_Meshes) if (allocation.Generation == instance.Identity.Geometry / 2) found = &allocation;
			if (!found) { m_Error = "Current geometry identity does not resolve"; return false; }
			const MeshAllocation& mesh = *found;
			DIAInstance packed;
			packed.Transform = instance.Transform;
			packed.Inverse = instance.Inverse;
			packed.Geometry[0] = mesh.NodeOffset;
			packed.Geometry[1] = mesh.TriangleOffset;
			packed.Geometry[2] = instance.CachePage;
			for (const SurfaceAllocation& surface : m_Surfaces)
				if (surface.Identity == instance.Identity && surface.Used && surface.Offset == instance.CachePage) packed.Geometry[3] = surface.Generation;
			packed.Metadata[0] = instance.Material;
			packed.Metadata[1] = static_cast<uint32>(instance.Mesh->Triangles.Num());
			packed.Metadata[2] = mesh.NodeOffset + static_cast<uint32>(instance.Mesh->Nodes.Num());
			packed.Metadata[3] = instance.CaptureGrid;
			packed.Identity[0] = instance.Identity.Entity;
			packed.Identity[1] = instance.Identity.Geometry;
			packed.Identity[2] = static_cast<uint32>(instance.Identity.Scene);
			packed.Identity[3] = static_cast<uint32>(instance.Identity.Scene >> 32);
			packed.Tint = tints[static_cast<int32>(instance.Material)];
			const uint32 ownerIndex = static_cast<uint32>(m_Instances.Num());
			m_Instances.Add(packed);
			for (uint32 t = 0; t < static_cast<uint32>(instance.Mesh->Triangles.Num()); ++t)
			{
				const uint32 samples = instance.CaptureGrid * instance.CaptureGrid * instance.CaptureSides;
				const uint32 first = instance.CachePage + instance.Mesh->Triangles[static_cast<int32>(t)].Primitive * samples;
				for (uint32 s = 0; s < samples; ++s)
					m_SurfaceOwners[static_cast<int32>(first + s)] = { ownerIndex, mesh.TriangleOffset + t, packed.Geometry[3], s };
			}
		}
		auto addLight = [&](const DIALight& light) {
			if (m_Lights.Num() == 64 || !IsFinite(light.ColorRange.XYZ()) || !IsFinite(light.Position.XYZ()) || !IsFinite(light.DirectionInner.XYZ()) ||
				!std::isfinite(light.ColorRange.W) || !std::isfinite(light.DirectionInner.W) || !std::isfinite(light.OuterRadius.X) ||
				light.ColorRange.X < 0 || light.ColorRange.Y < 0 || light.ColorRange.Z < 0) return false;
			m_Lights.Add(light);
			return true;
		};
		for (auto [entity, transform, light] : world.View<TransformComponent, DirectionalLightComponent>().each())
			if (!addLight({ { world.GetWorldRotation(entity).Rotate({ 0, 0, -1 }), 0 }, { Linear(light.Color).XYZ() * light.Intensity, 0 }, {}, {} })) { m_Error = "Invalid or excessive DIA lights"; return false; }
		for (auto [entity, transform, light] : world.View<TransformComponent, PointLightComponent>().each())
			if (light.Range <= 0 || !addLight({ { world.GetWorldPosition(entity), 1 }, { Linear(light.Color).XYZ() * light.Intensity, light.Range }, {}, { 0, 0.01f, 0, 0 } })) { m_Error = "Invalid point light"; return false; }
		for (auto [entity, transform, light] : world.View<TransformComponent, SpotLightComponent>().each())
			if (light.Range <= 0 || light.InnerAngle < 0 || light.OuterAngle <= light.InnerAngle || light.OuterAngle >= 90 ||
				!addLight({ { world.GetWorldPosition(entity), 2 }, { Linear(light.Color).XYZ() * light.Intensity, light.Range },
					{ world.GetWorldRotation(entity).Rotate({ 0, 0, -1 }), std::cos(light.InnerAngle * DegToRad) }, { std::cos(light.OuterAngle * DegToRad), 0.01f, 0, 0 } })) { m_Error = "Invalid spot light"; return false; }
		uint64 hash = 1469598103934665603ull;
		hash = Hash(hash, m_Instances.Data(), static_cast<usize>(m_Instances.Num()) * sizeof(DIAInstance));
		hash = Hash(hash, m_Materials.Data(), static_cast<usize>(m_Materials.Num()) * sizeof(DIAMaterial));
		hash = Hash(hash, m_Lights.Data(), static_cast<usize>(m_Lights.Num()) * sizeof(DIALight));
		hash = Hash(hash, &settings.SkyColor, sizeof(Color));
		hash = Hash(hash, &settings.SkyIntensity, sizeof(float));
		hash = Hash(hash, &settings.TraceDistance, sizeof(float));
		hash = Hash(hash, &settings.CacheUpdates, sizeof(settings.CacheUpdates));
		if (hash != m_SceneHash) { m_SceneHash = hash; ++m_SceneRevision; m_HistoryValid = false; m_CacheValid = false; for (auto& view : m_Views) view.Valid = false; }
		return BuildEmitters();
	}

	Vec3 DIA::SampleTransportTexture(const uint32* descriptor, Vec2 uv, Vec3 fallback) const
	{
		if (!descriptor[1] || !descriptor[2]) return fallback;
		const int width = static_cast<int>(descriptor[1]), height = static_cast<int>(descriptor[2]);
		auto texel = [&](int x, int y) {
			x = (x % width + width) % width;
			y = (y % height + height) % height;
			const uint32 packed = m_Texels[static_cast<int32>(descriptor[0] + static_cast<uint32>(y * width + x))];
			Vec3 color{ static_cast<float>(packed & 255u) / 255, static_cast<float>((packed >> 8) & 255u) / 255, static_cast<float>((packed >> 16) & 255u) / 255 };
			if (descriptor[3] & 1u)
			{
				auto linear = [](float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); };
				color = { linear(color.X), linear(color.Y), linear(color.Z) };
			}
			return color;
		};
		const float x = (uv.X - std::floor(uv.X)) * width - 0.5f;
		const float y = (uv.Y - std::floor(uv.Y)) * height - 0.5f;
		const int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
		const float a = x - std::floor(x), b = y - std::floor(y);
		return (texel(ix, iy) * (1 - a) + texel(ix + 1, iy) * a) * (1 - b) +
			(texel(ix, iy + 1) * (1 - a) + texel(ix + 1, iy + 1) * a) * b;
	}

	bool DIA::BuildEmitters()
	{
		HashMap<uint64, uint32> owners;
		uint64 signature = Hash(1469598103934665603ull, m_Materials.Data(), static_cast<usize>(m_Materials.Num()) * sizeof(DIAMaterial));
		uint64 identities = 0;
		for (uint32 i = 0; i < static_cast<uint32>(m_Instances.Num()); ++i)
		{
			const DIAInstance& instance = m_Instances[static_cast<int32>(i)];
			owners.Add((static_cast<uint64>(instance.Identity[0]) << 32) | instance.Identity[1], i);
			identities ^= Hash(1469598103934665603ull, instance.Identity, sizeof(instance.Identity));
			const float scale[3] = { std::round(instance.Transform.TransformVector({ 1, 0, 0 }).Length() * 1000) / 1000, std::round(instance.Transform.TransformVector({ 0, 1, 0 }).Length() * 1000) / 1000, std::round(instance.Transform.TransformVector({ 0, 0, 1 }).Length() * 1000) / 1000 };
			identities ^= Hash(Hash(1469598103934665603ull, instance.Identity, sizeof(instance.Identity)), scale, sizeof(scale));
		}
		signature = Hash(signature, &identities, sizeof(identities));
		if (signature == m_EmitterHash)
		{
			for (uint32 i = 0; i < static_cast<uint32>(m_Emitters.Num()); ++i)
			{
				DIAEmitter& emitter = m_Emitters[static_cast<int32>(i)];
				const uint32* owner = owners.Find((static_cast<uint64>(emitter.Identity[0]) << 32) | emitter.Identity[1]);
				if (!owner) { m_Error = "Emitter identity no longer resolves"; return false; }
				emitter.Geometry[0] = *owner;
				DIAMaterial& material = m_Materials[static_cast<int32>(m_Instances[static_cast<int32>(*owner)].Metadata[0])];
				if (!material.Flags[2]) material.Flags[2] = i + 1;
				material.Flags[3] = emitter.Geometry[3];
			}
			return true;
		}
		Array<DIAEmitter> candidate;
		double cumulative = 0;
		for (uint32 i = 0; i < static_cast<uint32>(m_Instances.Num()); ++i)
		{
			const DIAInstance& instance = m_Instances[static_cast<int32>(i)];
			DIAMaterial& material = m_Materials[static_cast<int32>(instance.Metadata[0])];
			if (!IsFinite(material.Emission.XYZ())) { m_Error = "Nonfinite emitter radiance"; return false; }
			if (std::max(material.Emission.X, std::max(material.Emission.Y, material.Emission.Z)) == 0) continue;
			const uint32 grid = material.Textures[6][1] ? std::min(128u, std::max(16u, 2u * std::max(material.Textures[6][1], material.Textures[6][2]))) : 1u;
			if (static_cast<uint64>(instance.Metadata[1]) * grid * grid + candidate.Num() > 262144) { m_Error = "Textured emitter sampling budget exceeded"; return false; }
			material.Flags[2] = static_cast<uint32>(candidate.Num()) + 1;
			material.Flags[3] = grid;
			for (uint32 t = 0; t < instance.Metadata[1]; ++t)
			{
				const RayTriangle& triangle = m_Triangles[static_cast<int32>(instance.Geometry[1] + t)];
				const float area = Vec3::Cross(instance.Transform.TransformVector(triangle.Position[1].XYZ() - triangle.Position[0].XYZ()), instance.Transform.TransformVector(triangle.Position[2].XYZ() - triangle.Position[0].XYZ())).Length() * 0.5f;
				for (uint32 y = 0; y < grid; ++y) for (uint32 x = 0; x < grid; ++x)
				{
					const float root = std::sqrt((static_cast<float>(x) + 0.5f) / grid);
					const float v = (static_cast<float>(y) + 0.5f) / grid;
					const Vec3 bary{ 1 - root, v * root, (1 - v) * root };
					Vec2 uv = Vec2(triangle.UV01.X, triangle.UV01.Y) * bary.X + Vec2(triangle.UV01.Z, triangle.UV01.W) * bary.Y + Vec2(triangle.UV2.X, triangle.UV2.Y) * bary.Z;
					uv = uv * Vec2(material.UVTransform.X, material.UVTransform.Y) + Vec2(material.UVTransform.Z, material.UVTransform.W);
					const Vec3 emission = SampleTransportTexture(material.Textures[6], uv, { 1, 1, 1 }) * material.Emission.XYZ();
					cumulative += static_cast<double>(Vec3::Dot(emission, { 0.2126f, 0.7152f, 0.0722f })) * area / (grid * grid);
					if (!std::isfinite(cumulative) || cumulative > 1e30) { m_Error = "Emitter distribution exceeds finite transport limits"; return false; }
					DIAEmitter emitter;
					std::copy(std::begin(instance.Identity), std::end(instance.Identity), emitter.Identity);
					emitter.Geometry[0] = i;
					emitter.Geometry[1] = instance.Geometry[1] + t;
					emitter.Geometry[2] = x | (y << 16);
					emitter.Geometry[3] = grid;
					emitter.Distribution = { static_cast<float>(cumulative), area, 0, 0 };
					candidate.Add(emitter);
				}
			}
		}
		m_Emitters = std::move(candidate);
		m_EmitterWeight = static_cast<float>(cumulative);
		m_EmitterHash = signature;
		return true;
	}

	bool DIA::Upload(SDL_GPUCommandBuffer* cmd)
	{
		if (m_SceneUploadCount && m_UploadedRevision == m_SceneRevision && !m_GeometryDirty && !m_TexturesDirty) return true;
		Array<RayNode> nodes = m_MeshNodes;
		const uint32 root = static_cast<uint32>(nodes.Num());
		for (RayNode node : m_Scene.GetNodes())
		{
			node.Right += root;
			node.Escape += root;
			nodes.Add(node);
		}
		for (int32 i = nodes.Num() - 1; i >= static_cast<int32>(root); --i)
		{
			RayNode& node = nodes[i];
			uint32 emissive = 0;
			if (node.Count)
			{
				for (uint32 instance = node.First; instance < node.First + node.Count; ++instance)
					if (m_Materials[static_cast<int32>(m_Instances[static_cast<int32>(instance)].Metadata[0])].Flags[2]) emissive = 0x80000000u;
			}
			else emissive = (nodes[i + 1].Right | nodes[static_cast<int32>(node.Right)].Right) & 0x80000000u;
			node.Right |= emissive;
		}
		while (m_Texels.Num() % 4 != 0) m_Texels.Add(0xffffffffu);
		const void* data[8] = { nodes.Data(), m_Triangles.Data(), m_Instances.Data(), m_Materials.Data(), m_Lights.Data(), m_Texels.Data(), m_SurfaceOwners.Data(), m_Emitters.Data() };
		uint32 sizes[8] = { static_cast<uint32>(nodes.Num()) * sizeof(RayNode), static_cast<uint32>(m_Triangles.Num()) * sizeof(RayTriangle),
			static_cast<uint32>(m_Instances.Num()) * sizeof(DIAInstance), static_cast<uint32>(m_Materials.Num()) * sizeof(DIAMaterial),
			static_cast<uint32>(m_Lights.Num()) * sizeof(DIALight), static_cast<uint32>(m_Texels.Num()) * sizeof(uint32), static_cast<uint32>(m_SurfaceOwners.Num()) * sizeof(DIASurface), static_cast<uint32>(m_Emitters.Num()) * sizeof(DIAEmitter) };
		usize projected = GetMemoryBytes() + static_cast<usize>(nodes.Capacity()) * sizeof(RayNode);
		for (int i = 0; i < 8; ++i)
		{
			const uint32 old = m_Buffers[i] ? m_Buffers[i]->GetSize() : 0;
			if (sizes[i] > old) projected += static_cast<usize>(sizes[i] - old) * 4;
		}
		if (projected > 256ull * 1024 * 1024) { m_Error = "DIA frame generation exceeds the residency budget"; return false; }
		const uint32 zero[4] = {};
		for (int i = 0; i < 8; ++i)
		{
			if (i == 1 && m_Buffers[i] && !m_GeometryDirty) continue;
			if (i == 5 && m_Buffers[i] && !m_TexturesDirty) continue;
			const uint32 bytes = std::max(16u, sizes[i]);
			if (!m_Buffers[i] || m_Buffers[i]->GetSize() < bytes || (m_Buffers[i]->GetSize() > bytes * 2ull && m_Buffers[i]->GetSize() > 65536))
				m_Buffers[i] = m_Device.CreateBuffer(SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ, nullptr, bytes);
			if (!m_Buffers[i]) { m_Error = "DIA storage buffer allocation failed"; return false; }
			if (!m_Device.UploadToBuffer(cmd, *m_Buffers[i], sizes[i] ? data[i] : zero, sizes[i] ? sizes[i] : 16, true)) { m_Error = "DIA frame upload failed"; return false; }
		}
		m_Uniforms.Counts[0] = root;
		m_Uniforms.Counts[1] = static_cast<uint32>(m_Instances.Num());
		m_Uniforms.Counts[2] = static_cast<uint32>(m_Lights.Num());
		m_Uniforms.Counts[3] = static_cast<uint32>(nodes.Num());
		m_GeometryDirty = false;
		m_TexturesDirty = false;
		m_UploadedRevision = m_SceneRevision;
		++m_SceneUploadCount;
		return true;
	}

	bool DIA::RunPass(SDL_GPUCommandBuffer* cmd, SDL_GPUGraphicsPipeline* pipeline, GPUTexture* const* targets, uint32 targetCount,
		GPUTexture* const* sources, uint32 sourceCount, const DIAUniforms& uniforms, GPUTexture* depth, bool load)
	{
		SDL_GPUColorTargetInfo outputs[8] = {};
		for (uint32 i = 0; i < targetCount; ++i)
		{
			outputs[i].texture = targets[i]->GetHandle();
			outputs[i].load_op = load ? SDL_GPU_LOADOP_LOAD : SDL_GPU_LOADOP_DONT_CARE;
			outputs[i].store_op = SDL_GPU_STOREOP_STORE;
			outputs[i].cycle = !load;
		}
		SDL_GPUDepthStencilTargetInfo depthTarget = {};
		if (depth)
		{
			depthTarget.texture = depth->GetHandle();
			depthTarget.load_op = load ? SDL_GPU_LOADOP_LOAD : SDL_GPU_LOADOP_DONT_CARE;
			depthTarget.store_op = load ? SDL_GPU_STOREOP_DONT_CARE : SDL_GPU_STOREOP_STORE;
			depthTarget.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
			depthTarget.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
			depthTarget.cycle = !load;
		}
		SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, outputs, targetCount, depth ? &depthTarget : nullptr);
		if (!pass) { m_Error = String("DIA pass could not begin: ") + SDL_GetError(); return false; }
		SDL_BindGPUGraphicsPipeline(pass, pipeline);
		SDL_PushGPUFragmentUniformData(cmd, 0, &uniforms, sizeof(uniforms));
		SDL_GPUTextureSamplerBinding textures[16] = {};
		for (uint32 i = 0; i < sourceCount; ++i) textures[i] = { sources[i]->GetHandle(), m_Device.GetClampSampler() };
		SDL_BindGPUFragmentSamplers(pass, 0, textures, sourceCount);
		if (pipeline == m_CachePipeline || pipeline == m_TracePipeline || pipeline == m_TraceLitOnePipeline ||
			pipeline == m_ResolvePipeline || pipeline == m_ResolveFallbackPipeline || pipeline == m_ResolveFallbackLitPipeline)
		{
			SDL_GPUBuffer* buffers[8];
			for (int i = 0; i < 8; ++i) buffers[i] = m_Buffers[i]->GetHandle();
			SDL_BindGPUFragmentStorageBuffers(pass, 0, buffers, 8);
			if (m_UseHardware && !m_Hardware.Bind(cmd, pass))
			{
				m_Error = m_Hardware.GetError();
				SDL_EndGPURenderPass(pass);
				return false;
			}
		}
		else if (pipeline == m_HistoryPipeline || pipeline == m_DenoisePipeline)
		{
			SDL_GPUBuffer* buffers[] = { m_Buffers[2]->GetHandle(), m_Buffers[3]->GetHandle() };
			SDL_BindGPUFragmentStorageBuffers(pass, 0, buffers, 2);
		}
		SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
		SDL_EndGPURenderPass(pass);
		return true;
	}

	bool DIA::Render(SDL_GPUCommandBuffer* cmd, World& world, MeshLibrary& meshes, const RenderView& view)
	{
		if (!m_CachePipeline || !m_HistoryPipeline || !m_DenoisePipeline || !m_ComposePipeline) return false;
		m_Error.Clear();
		if (!IsFinite(view.GetViewProjection()) || !IsFinite(view.GetViewProjection().Inverse())) { m_Error = "Invalid DIA view matrices"; return false; }
		DIAComponent settings;
		for (auto [entity, component] : world.View<DIAComponent>().each()) if (component.Enabled) { settings = component; break; }
		if (!IsFinite(Vec3(settings.SkyColor.R, settings.SkyColor.G, settings.SkyColor.B)) || settings.SkyColor.R < 0 || settings.SkyColor.G < 0 || settings.SkyColor.B < 0 ||
			!std::isfinite(settings.TraceDistance) || settings.TraceDistance <= 0 || settings.TraceDistance > 128 ||
			!std::isfinite(settings.SkyIntensity) || settings.SkyIntensity < 0 || settings.DiffuseSamples < 1 || settings.DiffuseSamples > 64 ||
			settings.ReflectionSamples < 1 || settings.ReflectionSamples > 64 || settings.CacheUpdates < 1 || settings.CacheUpdates > 65536 ||
			settings.InternalHeight < 32 || settings.InternalHeight > 1080) { m_Error = "Invalid DIA profile settings"; return false; }
		const bool litOne = m_DebugMode == 0 && settings.ReflectionSamples == 1;
		SDL_GPUGraphicsPipeline*& tracePipeline = litOne ? m_TraceLitOnePipeline : m_TracePipeline;
		if (!tracePipeline)
		{
			tracePipeline = CreatePipeline(m_UseHardware ? (litOne ? "DIATraceLitOneRT.frag" : "DIATraceRT.frag") :
				(litOne ? "DIATraceLitOne.frag" : "DIATrace.frag"), 1, 8, 6);
			if (!tracePipeline) return false;
		}
		const uint64 updateStart = SDL_GetTicksNS();
		if (!Extract(world, meshes, settings)) return false;
		m_UpdateMilliseconds = static_cast<double>(SDL_GetTicksNS() - updateStart) / 1e6;
		m_PendingAge = settings.CacheUpdates ? (m_CacheCount + settings.CacheUpdates - 1) / settings.CacheUpdates : 0;
		if (static_cast<uint64>(settings.CacheUpdates) * 30 < m_CacheCount) { m_Error = "Surface update budget cannot meet the 30-frame readiness bound"; return false; }
		m_ActiveView = m_Views[0].Identity == view.ViewIdentity ? 0 : m_Views[1].Identity == view.ViewIdentity ? 1 : m_Views[0].LastUsed <= m_Views[1].LastUsed ? 0 : 1;
		ViewHistory& history = m_Views[m_ActiveView];
		if (history.Identity != view.ViewIdentity) { history.Identity = view.ViewIdentity; history.Valid = false; }
		if (history.Epoch != view.HistoryEpoch || (view.CameraPosition - history.Camera).Length() > 2) history.Valid = false;
		m_Uniforms.PreviousCamera = { history.Camera, view.Orthographic ? 1.0f : 0.0f };
		m_Uniforms.PreviousForward = { history.Forward, 0 };
		m_Uniforms.CameraForward = { view.CameraForward, 0 };
		for (int i = 0; i < 16; ++i) if (std::fabs(view.Projection.M[i] - history.Projection.M[i]) > 1e-5f) history.Valid = false;
		history.Epoch = view.HistoryEpoch;
		history.Camera = view.CameraPosition;
		history.Forward = view.CameraForward;
		history.Projection = view.Projection;
		history.LastUsed = m_Frame + 1;
		const uint32 height = std::min(view.Height, static_cast<uint32>(settings.InternalHeight));
		const uint32 width = std::max(1u, static_cast<uint32>(static_cast<uint64>(view.Width) * height / std::max(1u, view.Height)));
		if (width > 4096 || height == 0) { m_Error = "DIA view extent exceeds its budget"; return false; }
		usize otherView = 0;
		for (const auto& other : m_Views) if (&other != &history && other.Output)
			otherView += static_cast<usize>(other.Output->GetWidth()) * other.Output->GetHeight() * 16 * 13 * 6 + other.DisplayPixels * 8 * 6;
		const usize displayPixels = m_DebugMode == 0 && (view.Width != width || view.Height != height) ? static_cast<usize>(view.Width) * view.Height : 0;
		if (displayPixels && !m_ResolvePipeline)
		{
			m_ResolvePipeline = CreatePipeline(m_UseHardware ? "DIAResolveRT.frag" : "DIAResolve.frag", 8, 8, 1, SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT, SDL_GPU_COMPAREOP_ALWAYS, true);
			if (!m_ResolvePipeline) return false;
		}
		SDL_GPUGraphicsPipeline*& fallbackPipeline = litOne ? m_ResolveFallbackLitPipeline : m_ResolveFallbackPipeline;
		if (displayPixels && !fallbackPipeline)
		{
			fallbackPipeline = CreatePipeline(m_UseHardware ? (litOne ? "DIAResolveFallbackLitRT.frag" : "DIAResolveFallbackRT.frag") :
				(litOne ? "DIAResolveFallbackLit.frag" : "DIAResolveFallback.frag"), 8, 8, 1,
				SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT, SDL_GPU_COMPAREOP_LESS);
			if (!fallbackPipeline) return false;
		}
		const usize maskPixels = std::max(m_ResolveMask ? static_cast<usize>(m_ResolveMask->GetWidth()) * m_ResolveMask->GetHeight() : usize(0), displayPixels);
		const usize textureBudget = std::max(m_TextureBudgetBytes, otherView + static_cast<usize>(width) * height * 16 * 23 * 6 + displayPixels * 8 * 6 + maskPixels * 2 * 6);
		const usize required = GetMemoryBytes() - m_TextureBudgetBytes + textureBudget;
		if (required > 256ull * 1024 * 1024) { m_Error = "DIA lighting resources exceed the 256 MiB residency budget"; return false; }
		m_TextureBudgetBytes = textureBudget;
		history.DisplayPixels = displayPixels;
		if (!m_Raw[0] || m_Raw[0]->GetWidth() != width || m_Raw[0]->GetHeight() != height || !m_Raw[1] || !m_Raw[2] || !m_Raw[3] || !m_Raw[4] || !m_Raw[5])
		{
			for (auto& texture : m_Raw) texture = m_Device.CreateRenderTarget(SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT, width, height, false);
		}
		for (auto& row : m_Denoised) for (auto& texture : row)
			if (!texture || texture->GetWidth() != width || texture->GetHeight() != height)
				texture = m_Device.CreateRenderTarget(SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT, width, height, false);
		if (displayPixels && (!m_ResolveMask || m_ResolveMask->GetWidth() != view.Width || m_ResolveMask->GetHeight() != view.Height))
			m_ResolveMask = m_Device.CreateRenderTarget(SDL_GPU_TEXTUREFORMAT_D16_UNORM, view.Width, view.Height, true);
		bool allocateHistory = !history.Output || history.Output->GetWidth() != width || history.Output->GetHeight() != height;
		for (const auto& row : history.History) for (const auto& texture : row) allocateHistory = allocateHistory || !texture;
		if (allocateHistory)
		{
			for (auto& row : history.History) for (auto& texture : row) texture = m_Device.CreateRenderTarget(SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT, width, height, false);
			history.Output = m_Device.CreateRenderTarget(SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT, width, height, false);
			history.Valid = false;
		}
		m_HistoryValid = history.Valid;
		if (!m_Cache[0] || !m_Cache[1])
			for (auto& texture : m_Cache) texture = m_Device.CreateRenderTarget(SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT, CacheExtent, CacheExtent, false);
		for (const auto& texture : m_Raw) if (!texture) { m_Error = "DIA view allocation failed"; return false; }
		for (const auto& row : m_Denoised) for (const auto& texture : row) if (!texture) { m_Error = "DIA denoiser allocation failed"; return false; }
		for (const auto& row : history.History) for (const auto& texture : row) if (!texture) { m_Error = "DIA history allocation failed"; return false; }
		if (displayPixels && !m_ResolveMask) { m_Error = "DIA resolve mask allocation failed"; return false; }
		if (!history.Output || !m_Cache[0] || !m_Cache[1]) { m_Error = "DIA cache allocation failed"; return false; }
		if (!Upload(cmd)) return false;
		if (m_UseHardware)
		{
			const usize fixedMemory = GetMemoryBytes() - m_Hardware.GetMemoryBytes();
			if (fixedMemory >= 256ull * 1024 * 1024 || !m_Hardware.Update(cmd, m_Triangles, m_Instances, m_Materials,
				m_SceneRevision, 256ull * 1024 * 1024 - fixedMemory))
			{ m_Error = m_Hardware.GetError().IsEmpty() ? String("Hardware ray resources exceed the residency budget") : m_Hardware.GetError(); return false; }
		}
		if (!history.Valid) history.Frame = 0;
		if (!m_CacheValid) m_CacheAge = 0;
		const uint32 current = m_Frame % 2;
		const uint32 previous = 1 - current;
		const uint32 historyCurrent = history.Frame % 2;
		const uint32 historyPrevious = 1 - historyCurrent;
		m_Uniforms.PreviousViewProjection = history.ViewProjection;
		m_Uniforms.InverseViewProjection = view.GetViewProjection().Inverse();
		m_Uniforms.Camera = { view.CameraPosition, view.Orthographic ? 1.0f : 0.0f };
		m_Uniforms.Sky = { Linear(settings.SkyColor).XYZ() * settings.SkyIntensity, 0 };
		m_Uniforms.Extent = { static_cast<float>(width), static_cast<float>(height), 1.0f / width, 1.0f / height };
		m_Uniforms.Frame[0] = m_Frame;
		m_Uniforms.Frame[1] = static_cast<uint32>(m_SceneRevision);
		m_Uniforms.Frame[2] = m_CacheCursor;
		m_Uniforms.Frame[3] = static_cast<uint32>(settings.CacheUpdates);
		m_Uniforms.Settings = { settings.TraceDistance, static_cast<float>(settings.DiffuseSamples), static_cast<float>(settings.ReflectionSamples), static_cast<float>(m_DebugMode) };
		m_Uniforms.History = { m_HistoryValid ? 1.0f : 0.0f, static_cast<float>(m_CacheCount), m_CacheValid ? 1.0f : 0.0f, static_cast<float>(settings.Seed) };
		m_Uniforms.Emitters = { static_cast<float>(m_Emitters.Num()), m_EmitterWeight, static_cast<float>(history.Frame), static_cast<float>(m_CacheAge) };
		GPUTexture* cacheTarget[] = { m_Cache[current].Get() };
		GPUTexture* cacheSource[] = { m_Cache[previous].Get() };
		if (!RunPass(cmd, m_CachePipeline, cacheTarget, 1, cacheSource, 1, m_Uniforms)) return false;
		GPUTexture* rawTargets[6];
		rawTargets[4] = m_Raw[4].Get();
		rawTargets[5] = m_Raw[5].Get();
		GPUTexture* historyTargets[6];
		GPUTexture* historySources[11];
		for (int i = 0; i < 4; ++i)
		{
			rawTargets[i] = m_Raw[i].Get();
			historyTargets[i] = history.History[historyCurrent][i].Get();
			historySources[i] = m_Raw[i].Get();
			historySources[i + 4] = history.History[historyPrevious][i].Get();
		}
		historyTargets[4] = history.History[historyCurrent][4].Get();
		historySources[8] = m_Raw[5].Get();
		historySources[9] = history.History[historyPrevious][4].Get();
		historyTargets[5] = history.History[historyCurrent][5].Get();
		historySources[10] = history.History[historyPrevious][5].Get();
		if (!m_Hardware.BeginTrace(cmd, m_Frame)) { m_Error = m_Hardware.GetError(); return false; }
		if (!RunPass(cmd, tracePipeline, rawTargets, 6, cacheTarget, 1, m_Uniforms))
		{ m_Hardware.AbortTrace(); return false; }
		if (!m_Hardware.EndTrace(cmd)) { m_Hardware.AbortTrace(); m_Error = m_Hardware.GetError(); return false; }
		if (!RunPass(cmd, m_HistoryPipeline, historyTargets, 6, historySources, 11, m_Uniforms)) return false;
		GPUTexture* denoiseSources[] = { historyTargets[0], historyTargets[1], historyTargets[2], historyTargets[3], historyTargets[5], historyTargets[4], historyTargets[0], m_Raw[1].Get() };
		for (uint32 iteration = 0; iteration < 4; ++iteration)
		{
			GPUTexture* denoiseTargets[] = { m_Denoised[iteration % 2][0].Get(), m_Denoised[iteration % 2][1].Get() };
			DIAUniforms uniforms = m_Uniforms;
			uniforms.Emitters.Z = static_cast<float>(iteration < 3 ? 1u << iteration : 1u);
			uniforms.Emitters.W = static_cast<float>(iteration);
			if (!RunPass(cmd, m_DenoisePipeline, denoiseTargets, 2, denoiseSources, 8, uniforms)) return false;
			denoiseSources[0] = denoiseTargets[0];
			denoiseSources[1] = denoiseTargets[1];
		}
		GPUTexture* outputTarget[] = { history.Output.Get() };
		GPUTexture* outputSources[] = { denoiseSources[0], denoiseSources[1], m_Raw[4].Get() };
		if (!RunPass(cmd, m_ComposePipeline, outputTarget, 1, outputSources, 3, m_Uniforms)) return false;
		history.ViewProjection = view.GetViewProjection();
		history.Valid = true;
		++history.Frame;
		m_HistoryValid = true;
		m_CacheValid = true;
		++m_CacheAge;
		m_CacheCursor = m_CacheCount ? (m_CacheCursor + static_cast<uint32>(settings.CacheUpdates)) % m_CacheCount : 0;
		++m_Frame;
		return true;
	}

	bool DIA::Resolve(SDL_GPUCommandBuffer* cmd, GPUTexture& target)
	{
		GPUTexture* targets[] = { &target };
		GPUTexture* sources[] = { m_Cache[(m_Frame - 1) % 2].Get(), m_Denoised[1][0].Get(), m_Denoised[1][1].Get(),
			m_Raw[2].Get(), m_Raw[3].Get(), m_Raw[0].Get(), m_Raw[5].Get(), m_Raw[1].Get() };
		// Depth 1 marks pixels the cheap pass deferred. The fallback shades only those.
		if (!RunPass(cmd, m_ResolvePipeline, targets, 1, sources, 8, m_Uniforms, m_ResolveMask.Get())) return false;
		SDL_GPUGraphicsPipeline* fallbackPipeline = m_Uniforms.Settings.W == 0 && m_Uniforms.Settings.Z == 1 ? m_ResolveFallbackLitPipeline : m_ResolveFallbackPipeline;
		return RunPass(cmd, fallbackPipeline, targets, 1, sources, 8, m_Uniforms, m_ResolveMask.Get(), true);
	}

	usize DIA::GetMemoryBytes() const
	{
		usize bytes = m_Scene.GetMemoryBytes() + m_Hardware.GetMemoryBytes();
		for (const auto& buffer : m_Buffers) if (buffer) bytes += static_cast<usize>(buffer->GetSize()) * 4;
		bytes += static_cast<usize>(m_Texels.Capacity()) * 4 + static_cast<usize>(m_Triangles.Capacity()) * sizeof(RayTriangle) + static_cast<usize>(m_MeshNodes.Capacity()) * sizeof(RayNode) + static_cast<usize>(m_Emitters.Capacity()) * sizeof(DIAEmitter);
		bytes += static_cast<usize>(m_SurfaceOwners.Capacity()) * sizeof(DIASurface) + static_cast<usize>(m_Materials.Capacity()) * sizeof(DIAMaterial) + static_cast<usize>(m_Instances.Capacity()) * sizeof(DIAInstance) + static_cast<usize>(m_Lights.Capacity()) * sizeof(DIALight);
		bytes += m_TextureBudgetBytes;
		if (m_Cache[0]) bytes += CacheExtent * CacheExtent * 16 * 2 * 3;
		return bytes;
	}
}
