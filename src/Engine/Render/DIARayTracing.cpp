#include "Engine/Render/DIARayTracing.h"
#include "Engine/Render/GPUDevice.h"
#include "Engine/Render/DIA.h"
#include "Engine/Render/SDLVulkanBridge.h"

#if FP_PLATFORM_WINDOWS && defined(FP_VULKAN_RAY_QUERY) && FP_VULKAN_RAY_QUERY
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <mutex>
#include <vector>

namespace FP
{
	namespace
	{
#define FP_RT_FUNCTIONS(X) \
		X(CreateBuffer) X(DestroyBuffer) X(GetBufferMemoryRequirements) X(AllocateMemory) X(FreeMemory) \
		X(BindBufferMemory) X(MapMemory) X(UnmapMemory) X(GetBufferDeviceAddress) \
		X(CreateAccelerationStructureKHR) X(DestroyAccelerationStructureKHR) X(GetAccelerationStructureDeviceAddressKHR) \
		X(GetAccelerationStructureBuildSizesKHR) X(CmdBuildAccelerationStructuresKHR) X(CmdPipelineBarrier) \
		X(CreateDescriptorPool) X(DestroyDescriptorPool) X(AllocateDescriptorSets) X(UpdateDescriptorSets) \
		X(GetPhysicalDeviceMemoryProperties) X(GetPhysicalDeviceProperties2)

#define FP_RT_TIMING_FUNCTIONS(X) \
		X(CreateQueryPool) X(DestroyQueryPool) X(CmdResetQueryPool) X(CmdWriteTimestamp) X(GetQueryPoolResults)

		struct Context
		{
			const FP_SDLVulkanBridge* Bridge = nullptr;
			VkDevice Device = VK_NULL_HANDLE;
			VkPhysicalDevice Physical = VK_NULL_HANDLE;
			VkPhysicalDeviceMemoryProperties Memory = {};
			VkPhysicalDeviceAccelerationStructurePropertiesKHR Limits{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR };
			std::atomic<uint64> Bytes = 0;
			uint64 Budget = 0;
			bool Timing = false;
			double TimestampPeriod = 0;
			std::mutex TimingMutex;
			std::vector<DIATraceTiming> Timings;
#define FP_RT_DECLARE(name) PFN_vk##name name = nullptr;
			FP_RT_FUNCTIONS(FP_RT_DECLARE)
			FP_RT_TIMING_FUNCTIONS(FP_RT_DECLARE)
#undef FP_RT_DECLARE
		};

		bool Result(String& error, const char* operation, VkResult result)
		{
			if (result == VK_SUCCESS) return true;
			error = String::Format("Vulkan ray tracing %s failed (%d)", operation, static_cast<int>(result));
			return false;
		}

		struct Buffer
		{
			std::shared_ptr<Context> Ctx;
			VkBuffer Handle = VK_NULL_HANDLE;
			VkDeviceMemory Memory = VK_NULL_HANDLE;
			VkDeviceAddress Address = 0;
			uint64 Allocation = 0;
			~Buffer()
			{
				if (Handle) Ctx->DestroyBuffer(Ctx->Device, Handle, nullptr);
				if (Memory)
				{
					Ctx->FreeMemory(Ctx->Device, Memory, nullptr);
					Ctx->Bytes.fetch_sub(Allocation);
				}
			}
		};

		std::shared_ptr<Buffer> MakeBuffer(const std::shared_ptr<Context>& ctx, VkDeviceSize size,
			VkBufferUsageFlags usage, bool host, String& error)
		{
			auto buffer = std::make_shared<Buffer>();
			buffer->Ctx = ctx;
			VkBufferCreateInfo info{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
			info.size = std::max<VkDeviceSize>(size, 16);
			info.usage = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
			info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			if (!Result(error, "buffer creation", ctx->CreateBuffer(ctx->Device, &info, nullptr, &buffer->Handle))) return {};
			VkMemoryRequirements requirements;
			ctx->GetBufferMemoryRequirements(ctx->Device, buffer->Handle, &requirements);
			const uint64 live = ctx->Bytes.load();
			if (live > ctx->Budget || requirements.size > ctx->Budget - live)
			{
				error = "Vulkan ray tracing resources exceed the residency budget";
				return {};
			}
			const VkMemoryPropertyFlags required = host ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
			uint32 type = std::numeric_limits<uint32>::max();
			for (uint32 i = 0; i < ctx->Memory.memoryTypeCount; ++i)
				if ((requirements.memoryTypeBits & (1u << i)) && (ctx->Memory.memoryTypes[i].propertyFlags & required) == required)
				{
					type = i;
					if (!host || (ctx->Memory.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) break;
				}
			if (type == std::numeric_limits<uint32>::max()) { error = "Vulkan ray tracing memory type unavailable"; return {}; }
			VkMemoryAllocateFlagsInfo flags{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO };
			flags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
			VkMemoryAllocateInfo allocation{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
			allocation.pNext = &flags;
			allocation.allocationSize = requirements.size;
			allocation.memoryTypeIndex = type;
			if (!Result(error, "memory allocation", ctx->AllocateMemory(ctx->Device, &allocation, nullptr, &buffer->Memory))) return {};
			buffer->Allocation = requirements.size;
			ctx->Bytes.fetch_add(requirements.size);
			if (!Result(error, "buffer memory binding", ctx->BindBufferMemory(ctx->Device, buffer->Handle, buffer->Memory, 0))) return {};
			VkBufferDeviceAddressInfo address{ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
			address.buffer = buffer->Handle;
			buffer->Address = ctx->GetBufferDeviceAddress(ctx->Device, &address);
			if (!buffer->Address) { error = "Vulkan ray tracing buffer has no device address"; return {}; }
			return buffer;
		}

		struct BuildState
		{
			std::atomic_bool Submitted = false;
			std::atomic_bool Abandoned = false;
			SDL_GPUCommandBuffer* Builder = nullptr;
			bool CanUse(SDL_GPUCommandBuffer* cmd) const { return !Abandoned.load() && (Submitted.load() || Builder == cmd); }
		};

		struct Mesh : BuildState
		{
			std::shared_ptr<Context> Ctx;
			std::shared_ptr<Buffer> Storage;
			VkAccelerationStructureKHR Handle = VK_NULL_HANDLE;
			VkDeviceAddress Address = 0;
			uint32 Base = 0;
			uint32 Count = 0;
			uint32 Generation = 0;
			~Mesh() { if (Handle) Ctx->DestroyAccelerationStructureKHR(Ctx->Device, Handle, nullptr); }
		};

		struct Scene : BuildState
		{
			std::shared_ptr<Context> Ctx;
			std::shared_ptr<Buffer> Storage;
			std::vector<std::shared_ptr<Mesh>> Meshes;
			VkAccelerationStructureKHR Handle = VK_NULL_HANDLE;
			VkDescriptorPool Pool = VK_NULL_HANDLE;
			VkDescriptorSet Set = VK_NULL_HANDLE;
			~Scene()
			{
				if (Pool) Ctx->DestroyDescriptorPool(Ctx->Device, Pool, nullptr);
				if (Handle) Ctx->DestroyAccelerationStructureKHR(Ctx->Device, Handle, nullptr);
			}
		};

		struct BuildWork
		{
			std::shared_ptr<Scene> ActiveScene;
			std::vector<std::shared_ptr<Buffer>> Temporary;
			std::vector<std::shared_ptr<Mesh>> NewMeshes;
			bool BuiltScene = false;
		};

		// shortcut: Profiling keeps 8192 samples, raise the cap for longer captures.
		constexpr usize MaxTraceTimings = 8192;
		struct TraceTiming
		{
			std::shared_ptr<Context> Ctx;
			VkQueryPool Pool = VK_NULL_HANDLE;
			uint32 Frame = 0;
			std::atomic_bool Ended = false;
			std::atomic_bool Completed = false;
			~TraceTiming()
			{
				if (!Pool) return;
				uint64 ticks[2] = {};
				if (Ended.load() && Completed.load() && Ctx->GetQueryPoolResults(Ctx->Device, Pool, 0, 2, sizeof(ticks), ticks, sizeof(uint64), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS)
				{
					const uint32 bits = Ctx->Bridge->timestamp_valid_bits;
					const uint64 mask = bits >= 64 ? std::numeric_limits<uint64>::max() : (uint64(1) << bits) - 1;
					const double milliseconds = static_cast<double>((ticks[1] - ticks[0]) & mask) * Ctx->TimestampPeriod / 1e6;
					std::lock_guard lock(Ctx->TimingMutex);
					if (Ctx->Timings.size() < MaxTraceTimings) Ctx->Timings.push_back({ Frame, milliseconds });
				}
				Ctx->DestroyQueryPool(Ctx->Device, Pool, nullptr);
			}
		};

		void SDLCALL ReleaseTiming(void* payload, Uint32 event)
		{
			auto* timing = static_cast<std::shared_ptr<TraceTiming>*>(payload);
			if (event == FP_SDL_VULKAN_SUBMITTED) return;
			if (event == FP_SDL_VULKAN_COMPLETED) (*timing)->Completed = true;
			if (event == FP_SDL_VULKAN_COMPLETED || event == FP_SDL_VULKAN_CANCELLED) delete timing;
		}

		void SDLCALL ReleaseWork(void* payload, Uint32 event)
		{
			auto* retained = static_cast<std::shared_ptr<BuildWork>*>(payload);
			auto& work = **retained;
			if (event == FP_SDL_VULKAN_SUBMITTED)
			{
				if (work.BuiltScene) work.ActiveScene->Submitted = true;
				for (const auto& mesh : work.NewMeshes) mesh->Submitted = true;
				return;
			}
			if (event == FP_SDL_VULKAN_CANCELLED)
			{
				if (work.BuiltScene) work.ActiveScene->Abandoned = true;
				for (const auto& mesh : work.NewMeshes) mesh->Abandoned = true;
			}
			if (event == FP_SDL_VULKAN_COMPLETED || event == FP_SDL_VULKAN_CANCELLED) delete retained;
		}

		bool Track(const Context& ctx, SDL_GPUCommandBuffer* cmd, const std::shared_ptr<BuildWork>& work, String& error)
		{
			auto* payload = new std::shared_ptr<BuildWork>(work);
			if (ctx.Bridge->track(ctx.Bridge->userdata, cmd, payload, ReleaseWork)) return true;
			delete payload;
			error = String::Format("Could not retain Vulkan ray tracing resources: %s", SDL_GetError());
			return false;
		}

		bool MakeAcceleration(const std::shared_ptr<Context>& ctx, VkAccelerationStructureTypeKHR type, VkDeviceSize size,
			std::shared_ptr<Buffer>& storage, VkAccelerationStructureKHR& handle, String& error)
		{
			storage = MakeBuffer(ctx, size, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, false, error);
			if (!storage) return false;
			VkAccelerationStructureCreateInfoKHR info{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
			info.buffer = storage->Handle;
			info.size = size;
			info.type = type;
			return Result(error, "acceleration structure creation", ctx->CreateAccelerationStructureKHR(ctx->Device, &info, nullptr, &handle));
		}

		void Barrier(const Context& ctx, VkCommandBuffer cmd, VkPipelineStageFlags source, VkPipelineStageFlags destination,
			VkAccessFlags sourceAccess, VkAccessFlags destinationAccess)
		{
			VkMemoryBarrier barrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
			barrier.srcAccessMask = sourceAccess;
			barrier.dstAccessMask = destinationAccess;
			ctx.CmdPipelineBarrier(cmd, source, destination, 0, 1, &barrier, 0, nullptr, 0, nullptr);
		}

		std::vector<uint8> SceneKey(const Array<DIAInstance>& instances, const Array<DIAMaterial>& materials)
		{
			std::vector<uint8> key;
			key.reserve(sizeof(uint32) + static_cast<usize>(instances.Num()) * (sizeof(Mat4) + 3 * sizeof(uint32) + sizeof(bool)));
			auto add = [&](const void* data, usize size) {
				const uint8* bytes = static_cast<const uint8*>(data);
				key.insert(key.end(), bytes, bytes + size);
			};
			const uint32 count = static_cast<uint32>(instances.Num());
			add(&count, sizeof(count));
			for (const auto& instance : instances)
			{
				add(&instance.Transform, sizeof(instance.Transform));
				add(&instance.Geometry[1], sizeof(uint32));
				add(&instance.Metadata[1], sizeof(uint32));
				add(&instance.Identity[1], sizeof(uint32));
				const bool emitter = materials[instance.Metadata[0]].Flags[2] != 0;
				add(&emitter, sizeof(emitter));
			}
			return key;
		}
	}

	struct DIARayTracing::State
	{
		std::shared_ptr<Context> Ctx;
		std::shared_ptr<Scene> Current;
		uint64 Revision = std::numeric_limits<uint64>::max();
		std::vector<uint8> Key;
		uint64 Builds = 0;
		std::shared_ptr<TraceTiming> Timing;
		SDL_GPUCommandBuffer* TimingCommand = nullptr;
	};

	DIARayTracing::DIARayTracing(GPUDevice& device)
	{
		static_assert(RayScene::MaxInstances <= (1u << 24));
		const auto* bridge = static_cast<const FP_SDLVulkanBridge*>(SDL_GetPointerProperty(
			SDL_GetGPUDeviceProperties(device.GetHandle()), FP_SDL_VULKAN_BRIDGE_PROPERTY, nullptr));
		if (!bridge || bridge->version != FP_SDL_VULKAN_BRIDGE_VERSION || bridge->size < sizeof(FP_SDLVulkanBridge) ||
			!bridge->device || !bridge->physical_device || !bridge->acceleration_layout || !bridge->get_proc || !bridge->command || !bridge->bind || !bridge->track)
		{
			m_Error = "SDL Vulkan ray query bridge unavailable";
			return;
		}
		auto ctx = std::make_shared<Context>();
		ctx->Bridge = bridge;
		ctx->Device = static_cast<VkDevice>(bridge->device);
		ctx->Physical = static_cast<VkPhysicalDevice>(bridge->physical_device);
#define FP_RT_LOAD(name) ctx->name = reinterpret_cast<PFN_vk##name>(bridge->get_proc(bridge->userdata, "vk" #name)); if (!ctx->name) { m_Error = "Missing Vulkan ray tracing function vk" #name; return; }
		FP_RT_FUNCTIONS(FP_RT_LOAD)
		FP_RT_TIMING_FUNCTIONS(FP_RT_LOAD)
#undef FP_RT_LOAD
		ctx->GetPhysicalDeviceMemoryProperties(ctx->Physical, &ctx->Memory);
		VkPhysicalDeviceProperties2 properties{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
		properties.pNext = &ctx->Limits;
		ctx->GetPhysicalDeviceProperties2(ctx->Physical, &properties);
		const char* timing = SDL_getenv("FP_DIA_GPU_TIMING");
		ctx->Timing = timing && std::strcmp(timing, "1") == 0 && bridge->timestamp_valid_bits > 0 && properties.properties.limits.timestampPeriod > 0;
		ctx->TimestampPeriod = properties.properties.limits.timestampPeriod;
		if (ctx->Timing) ctx->Timings.reserve(MaxTraceTimings);
		if (ctx->Limits.maxInstanceCount < RayScene::MaxInstances || ctx->Limits.maxGeometryCount < 1 ||
			ctx->Limits.maxPrimitiveCount < RayScene::MaxTriangles || ctx->Limits.maxDescriptorSetAccelerationStructures < 1 ||
			!ctx->Limits.minAccelerationStructureScratchOffsetAlignment)
		{
			m_Error = "Vulkan ray tracing device limits are insufficient";
			return;
		}
		m_State = std::make_unique<State>();
		m_State->Ctx = std::move(ctx);
	}

	DIARayTracing::~DIARayTracing() = default;
	bool DIARayTracing::IsSupported() const { return m_State != nullptr; }
	usize DIARayTracing::GetMemoryBytes() const { return m_State ? static_cast<usize>(m_State->Ctx->Bytes.load()) : 0; }
	uint64 DIARayTracing::GetBuildCount() const { return m_State ? m_State->Builds : 0; }

	bool DIARayTracing::Update(SDL_GPUCommandBuffer* cmd, const Array<RayTriangle>& triangles, const Array<DIAInstance>& instances,
		const Array<DIAMaterial>& materials, uint64 revision, usize budget)
	{
		if (!m_State) return false;
		m_Error.Clear();
		auto ctx = m_State->Ctx;
		ctx->Budget = budget;
		if (ctx->Bytes.load() > budget) { m_Error = "Vulkan ray tracing resources exceed the residency budget"; return false; }
		VkCommandBuffer native = static_cast<VkCommandBuffer>(ctx->Bridge->command(ctx->Bridge->userdata, cmd));
		if (!native) { m_Error = String::Format("Invalid Vulkan ray tracing command buffer: %s", SDL_GetError()); return false; }
		if (instances.Num() > static_cast<int32>(RayScene::MaxInstances)) { m_Error = "Vulkan ray tracing instance limit exceeded"; return false; }
		for (const auto& instance : instances)
			if (!IsFinite(instance.Transform) || instance.Metadata[0] >= static_cast<uint32>(materials.Num()) ||
				instance.Geometry[1] > static_cast<uint32>(triangles.Num()) ||
				instance.Metadata[1] > static_cast<uint32>(triangles.Num()) - instance.Geometry[1] ||
				instance.Metadata[1] > RayScene::MaxTriangles)
			{ m_Error = "Invalid Vulkan ray tracing instance geometry"; return false; }
		if (m_State->Current && m_State->Current->Abandoned.load()) m_State->Current.reset();
		const bool sameRevision = m_State->Revision == revision && m_State->Current;
		std::vector<uint8> key = sameRevision ? std::vector<uint8>() : SceneKey(instances, materials);
		if (m_State->Current && (sameRevision || key == m_State->Key) && m_State->Current->CanUse(cmd))
		{
			m_State->Revision = revision;
			return true;
		}

		auto scene = std::make_shared<Scene>();
		scene->Ctx = ctx;
		scene->Builder = cmd;
		auto work = std::make_shared<BuildWork>();
		work->ActiveScene = scene;
		work->BuiltScene = true;
		struct Build
		{
			VkAccelerationStructureGeometryKHR Geometry{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
			VkAccelerationStructureBuildGeometryInfoKHR Info{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
			VkAccelerationStructureBuildRangeInfoKHR Range = {};
		};
		std::vector<Build> builds;
		builds.reserve(static_cast<usize>(instances.Num()) + 1);
		std::vector<VkAccelerationStructureInstanceKHR> packed(std::max(1, instances.Num()));
		for (auto& instance : packed)
			for (int axis = 0; axis < 3; ++axis) instance.transform.matrix[axis][axis] = 1;
		VkDeviceSize scratchSize = 0;
		for (int32 i = 0; i < instances.Num(); ++i)
		{
			const auto& instance = instances[i];
			if (!instance.Metadata[1]) continue;
			std::shared_ptr<Mesh> mesh;
			auto find = [&](const std::vector<std::shared_ptr<Mesh>>& meshes) {
				for (const auto& candidate : meshes)
					if (candidate->Base == instance.Geometry[1] && candidate->Count == instance.Metadata[1] &&
						candidate->Generation == instance.Identity[1] && candidate->CanUse(cmd)) return candidate;
				return std::shared_ptr<Mesh>();
			};
			mesh = find(scene->Meshes);
			if (!mesh && m_State->Current) mesh = find(m_State->Current->Meshes);
			if (!mesh)
			{
				mesh = std::make_shared<Mesh>();
				mesh->Ctx = ctx;
				mesh->Builder = cmd;
				mesh->Base = instance.Geometry[1];
				mesh->Count = instance.Metadata[1];
				mesh->Generation = instance.Identity[1];
				static_assert(sizeof(VkAabbPositionsKHR) == 24);
				auto input = MakeBuffer(ctx, static_cast<VkDeviceSize>(mesh->Count) * sizeof(VkAabbPositionsKHR),
					VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, true, m_Error);
				if (!input) return false;
				void* mapped;
				if (!Result(m_Error, "AABB mapping", ctx->MapMemory(ctx->Device, input->Memory, 0, VK_WHOLE_SIZE, 0, &mapped))) return false;
				auto* boxes = static_cast<VkAabbPositionsKHR*>(mapped);
				bool finite = true;
				for (uint32 t = 0; t < mesh->Count; ++t)
				{
					const auto& triangle = triangles[static_cast<int32>(mesh->Base + t)];
					double minimum[3], maximum[3], longest = 0, magnitude = 0;
					for (int axis = 0; axis < 3; ++axis)
					{
						minimum[axis] = std::numeric_limits<double>::max();
						maximum[axis] = -std::numeric_limits<double>::max();
						for (int v = 0; v < 3; ++v)
						{
							const double coordinate = axis == 0 ? triangle.Position[v].X : axis == 1 ? triangle.Position[v].Y : triangle.Position[v].Z;
							finite = finite && std::isfinite(coordinate);
							minimum[axis] = std::min(minimum[axis], coordinate);
							maximum[axis] = std::max(maximum[axis], coordinate);
							magnitude = std::max(magnitude, std::abs(coordinate));
						}
					}
					for (int v = 0; v < 3; ++v)
					{
						const Vec4& a = triangle.Position[v];
						const Vec4& b = triangle.Position[(v + 1) % 3];
						const double x = static_cast<double>(a.X) - b.X, y = static_cast<double>(a.Y) - b.Y, z = static_cast<double>(a.Z) - b.Z;
						longest = std::max(longest, std::sqrt(x * x + y * y + z * z));
					}
					const double pad = std::max(1e-4 * longest, 1e-5 * (1 + magnitude));
					float lower[3], upper[3];
					for (int axis = 0; axis < 3; ++axis)
					{
						lower[axis] = std::nextafter(static_cast<float>(minimum[axis] - pad), -std::numeric_limits<float>::infinity());
						upper[axis] = std::nextafter(static_cast<float>(maximum[axis] + pad), std::numeric_limits<float>::infinity());
						finite = finite && std::isfinite(lower[axis]) && std::isfinite(upper[axis]) && lower[axis] < upper[axis];
					}
					boxes[t] = { lower[0], lower[1], lower[2], upper[0], upper[1], upper[2] };
				}
				ctx->UnmapMemory(ctx->Device, input->Memory);
				if (!finite) { m_Error = "Invalid padded Vulkan ray tracing bounds"; return false; }
				work->Temporary.push_back(input);
				Build build;
				build.Geometry.geometryType = VK_GEOMETRY_TYPE_AABBS_KHR;
				build.Geometry.flags = VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR;
				auto& geometry = build.Geometry.geometry.aabbs;
				geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR;
				geometry.data.deviceAddress = input->Address;
				geometry.stride = sizeof(VkAabbPositionsKHR);
				build.Info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
				build.Info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
				build.Info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
				build.Info.geometryCount = 1;
				build.Info.pGeometries = &build.Geometry;
				build.Range.primitiveCount = mesh->Count;
				VkAccelerationStructureBuildSizesInfoKHR sizes{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
				ctx->GetAccelerationStructureBuildSizesKHR(ctx->Device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
					&build.Info, &build.Range.primitiveCount, &sizes);
				if (!MakeAcceleration(ctx, build.Info.type, sizes.accelerationStructureSize, mesh->Storage, mesh->Handle, m_Error)) return false;
				build.Info.dstAccelerationStructure = mesh->Handle;
				VkAccelerationStructureDeviceAddressInfoKHR address{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };
				address.accelerationStructure = mesh->Handle;
				mesh->Address = ctx->GetAccelerationStructureDeviceAddressKHR(ctx->Device, &address);
				if (!mesh->Address) { m_Error = "Vulkan bottom-level acceleration structure has no device address"; return false; }
				scratchSize = std::max(scratchSize, sizes.buildScratchSize);
				builds.push_back(build);
				work->NewMeshes.push_back(mesh);
			}
			if (std::find(scene->Meshes.begin(), scene->Meshes.end(), mesh) == scene->Meshes.end()) scene->Meshes.push_back(mesh);
			auto& target = packed[static_cast<usize>(i)];
			for (int row = 0; row < 3; ++row) for (int col = 0; col < 4; ++col) target.transform.matrix[row][col] = instance.Transform.At(col, row);
			target.instanceCustomIndex = static_cast<uint32>(i);
			target.mask = 1u | (materials[instance.Metadata[0]].Flags[2] ? 2u : 0u);
			target.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR | VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR;
			target.accelerationStructureReference = mesh->Address;
		}

		auto input = MakeBuffer(ctx, packed.size() * sizeof(VkAccelerationStructureInstanceKHR),
			VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, true, m_Error);
		if (!input) return false;
		void* mapped;
		if (!Result(m_Error, "instance mapping", ctx->MapMemory(ctx->Device, input->Memory, 0, VK_WHOLE_SIZE, 0, &mapped))) return false;
		std::memcpy(mapped, packed.data(), packed.size() * sizeof(VkAccelerationStructureInstanceKHR));
		ctx->UnmapMemory(ctx->Device, input->Memory);
		work->Temporary.push_back(input);
		Build top;
		top.Geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
		top.Geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
		top.Geometry.geometry.instances.data.deviceAddress = input->Address;
		top.Info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
		top.Info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
		top.Info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
		top.Info.geometryCount = 1;
		top.Info.pGeometries = &top.Geometry;
		top.Range.primitiveCount = static_cast<uint32>(packed.size());
		VkAccelerationStructureBuildSizesInfoKHR sizes{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
		ctx->GetAccelerationStructureBuildSizesKHR(ctx->Device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
			&top.Info, &top.Range.primitiveCount, &sizes);
		if (!MakeAcceleration(ctx, top.Info.type, sizes.accelerationStructureSize, scene->Storage, scene->Handle, m_Error)) return false;
		top.Info.dstAccelerationStructure = scene->Handle;
		builds.push_back(top);
		scratchSize = std::max(scratchSize, sizes.buildScratchSize);
		const VkDeviceSize alignment = ctx->Limits.minAccelerationStructureScratchOffsetAlignment;
		auto scratch = MakeBuffer(ctx, scratchSize + alignment - 1, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false, m_Error);
		if (!scratch) return false;
		const VkDeviceAddress scratchAddress = (scratch->Address + alignment - 1) / alignment * alignment;
		work->Temporary.push_back(scratch);

		VkDescriptorPoolSize poolSize{ VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1 };
		VkDescriptorPoolCreateInfo pool{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
		pool.maxSets = 1;
		pool.poolSizeCount = 1;
		pool.pPoolSizes = &poolSize;
		if (!Result(m_Error, "descriptor pool creation", ctx->CreateDescriptorPool(ctx->Device, &pool, nullptr, &scene->Pool))) return false;
		VkDescriptorSetLayout layout{};
		static_assert(sizeof(layout) <= sizeof(ctx->Bridge->acceleration_layout));
		std::memcpy(&layout, &ctx->Bridge->acceleration_layout, sizeof(layout));
		VkDescriptorSetAllocateInfo set{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
		set.descriptorPool = scene->Pool;
		set.descriptorSetCount = 1;
		set.pSetLayouts = &layout;
		if (!Result(m_Error, "descriptor allocation", ctx->AllocateDescriptorSets(ctx->Device, &set, &scene->Set))) return false;
		VkWriteDescriptorSetAccelerationStructureKHR acceleration{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR };
		acceleration.accelerationStructureCount = 1;
		acceleration.pAccelerationStructures = &scene->Handle;
		VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		write.pNext = &acceleration;
		write.dstSet = scene->Set;
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
		ctx->UpdateDescriptorSets(ctx->Device, 1, &write, 0, nullptr);
		if (!Track(*ctx, cmd, work, m_Error)) return false;

		Barrier(*ctx, native, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
			VK_ACCESS_HOST_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
		for (auto& build : builds)
		{
			Barrier(*ctx, native, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
				VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR, VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR);
			build.Info.pGeometries = &build.Geometry;
			build.Info.scratchData.deviceAddress = scratchAddress;
			const auto* range = &build.Range;
			ctx->CmdBuildAccelerationStructuresKHR(native, 1, &build.Info, &range);
		}
		Barrier(*ctx, native, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR, VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR);
		m_State->Current = std::move(scene);
		m_State->Revision = revision;
		if (!sameRevision) m_State->Key = std::move(key);
		++m_State->Builds;
		return true;
	}

	bool DIARayTracing::Bind(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass)
	{
		if (!m_State || !m_State->Current || !m_State->Current->CanUse(cmd))
		{ m_Error = "Vulkan ray tracing scene is unsubmitted or abandoned"; return false; }
		auto work = std::make_shared<BuildWork>();
		work->ActiveScene = m_State->Current;
		if (!Track(*m_State->Ctx, cmd, work, m_Error)) return false;
		Uint64 set = 0;
		static_assert(sizeof(m_State->Current->Set) <= sizeof(set));
		std::memcpy(&set, &m_State->Current->Set, sizeof(m_State->Current->Set));
		if (m_State->Ctx->Bridge->bind(m_State->Ctx->Bridge->userdata, cmd, pass, set)) return true;
		m_Error = String::Format("Could not bind Vulkan ray tracing scene: %s", SDL_GetError());
		return false;
	}

	bool DIARayTracing::BeginTrace(SDL_GPUCommandBuffer* cmd, uint32 frame)
	{
		if (!m_State || !m_State->Ctx->Timing) return true;
		auto ctx = m_State->Ctx;
		{
			std::lock_guard lock(ctx->TimingMutex);
			if (ctx->Timings.size() >= MaxTraceTimings) return true;
		}
		if (m_State->Timing) { m_Error = "Unclosed Vulkan trace timing interval"; return false; }
		VkCommandBuffer native = static_cast<VkCommandBuffer>(ctx->Bridge->command(ctx->Bridge->userdata, cmd));
		if (!native) { m_Error = "Invalid Vulkan trace timing command buffer"; return false; }
		auto timing = std::make_shared<TraceTiming>();
		timing->Ctx = ctx;
		timing->Frame = frame;
		VkQueryPoolCreateInfo info{ VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
		info.queryType = VK_QUERY_TYPE_TIMESTAMP;
		info.queryCount = 2;
		if (!Result(m_Error, "timestamp pool creation", ctx->CreateQueryPool(ctx->Device, &info, nullptr, &timing->Pool))) return false;
		auto* payload = new std::shared_ptr<TraceTiming>(timing);
		if (!ctx->Bridge->track(ctx->Bridge->userdata, cmd, payload, ReleaseTiming))
		{
			delete payload;
			m_Error = "Could not retain Vulkan trace timing queries";
			return false;
		}
		ctx->CmdResetQueryPool(native, timing->Pool, 0, 2);
		ctx->CmdWriteTimestamp(native, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timing->Pool, 0);
		m_State->Timing = std::move(timing);
		m_State->TimingCommand = cmd;
		return true;
	}

	bool DIARayTracing::EndTrace(SDL_GPUCommandBuffer* cmd)
	{
		if (!m_State || !m_State->Ctx->Timing || !m_State->Timing) return true;
		if (m_State->TimingCommand != cmd) { m_Error = "Vulkan trace timing command buffer mismatch"; return false; }
		auto ctx = m_State->Ctx;
		VkCommandBuffer native = static_cast<VkCommandBuffer>(ctx->Bridge->command(ctx->Bridge->userdata, cmd));
		if (!native) { m_Error = "Invalid Vulkan trace timing end command buffer"; return false; }
		ctx->CmdWriteTimestamp(native, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_State->Timing->Pool, 1);
		m_State->Timing->Ended = true;
		m_State->Timing.reset();
		m_State->TimingCommand = nullptr;
		return true;
	}

	void DIARayTracing::AbortTrace()
	{
		if (!m_State) return;
		m_State->Timing.reset();
		m_State->TimingCommand = nullptr;
	}

	Array<DIATraceTiming> DIARayTracing::GetTraceTimings() const
	{
		Array<DIATraceTiming> timings;
		if (!m_State) return timings;
		std::lock_guard lock(m_State->Ctx->TimingMutex);
		for (const auto& timing : m_State->Ctx->Timings) timings.Add(timing);
		return timings;
	}
#undef FP_RT_FUNCTIONS
#undef FP_RT_TIMING_FUNCTIONS
}

#else

namespace FP
{
	struct DIARayTracing::State {};
	DIARayTracing::DIARayTracing(GPUDevice&) { m_Error = "Vulkan ray tracing was not built for this platform"; }
	DIARayTracing::~DIARayTracing() = default;
	bool DIARayTracing::IsSupported() const { return false; }
	bool DIARayTracing::Update(SDL_GPUCommandBuffer*, const Array<RayTriangle>&, const Array<DIAInstance>&,
		const Array<DIAMaterial>&, uint64, usize) { return false; }
	bool DIARayTracing::Bind(SDL_GPUCommandBuffer*, SDL_GPURenderPass*) { return false; }
	usize DIARayTracing::GetMemoryBytes() const { return 0; }
	uint64 DIARayTracing::GetBuildCount() const { return 0; }
	bool DIARayTracing::BeginTrace(SDL_GPUCommandBuffer*, uint32) { return true; }
	bool DIARayTracing::EndTrace(SDL_GPUCommandBuffer*) { return true; }
	void DIARayTracing::AbortTrace() {}
	Array<DIATraceTiming> DIARayTracing::GetTraceTimings() const { return {}; }
}

#endif
