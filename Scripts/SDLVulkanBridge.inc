static bool FP_CommandMatches(VulkanRenderer *renderer, VulkanCommandBuffer *command)
{
    return command && command->common.device && command->common.device->driverData == (SDL_GPURenderer *)renderer &&
        command->renderer == renderer && !command->common.submitted && command->commandPool->threadID == SDL_GetCurrentThreadID();
}

static SDL_FunctionPointer SDLCALL FP_GetProc(void *userdata, const char *name)
{
    VulkanRenderer *renderer = (VulkanRenderer *)userdata;
    if (!name) return NULL;
    if (SDL_strncmp(name, "vkGetPhysicalDevice", 19) == 0) return (SDL_FunctionPointer)vkGetInstanceProcAddr(renderer->instance, name);
    PFN_vkVoidFunction result = renderer->vkGetDeviceProcAddr(renderer->logicalDevice, name);
    if (!result) result = vkGetInstanceProcAddr(renderer->instance, name);
    return (SDL_FunctionPointer)result;
}

static void *SDLCALL FP_Command(void *userdata, SDL_GPUCommandBuffer *buffer)
{
    VulkanCommandBuffer *command = (VulkanCommandBuffer *)buffer;
    if (!FP_CommandMatches((VulkanRenderer *)userdata, command) || command->common.render_pass.in_progress ||
        command->common.compute_pass.in_progress || command->common.copy_pass.in_progress) {
        SDL_SetError("Native Vulkan commands require an active command buffer outside SDL passes");
        return NULL;
    }
    return (void *)command->commandBuffer;
}

static bool SDLCALL FP_BindAcceleration(void *userdata, SDL_GPUCommandBuffer *buffer, SDL_GPURenderPass *pass, Uint64 descriptor_set)
{
    RenderPass *render = (RenderPass *)pass;
    VulkanCommandBuffer *command = (VulkanCommandBuffer *)buffer;
    if (!FP_CommandMatches((VulkanRenderer *)userdata, command) || !render || render->command_buffer != buffer || render != &command->common.render_pass ||
        !render->in_progress || !descriptor_set) {
        return SDL_SetError("Acceleration binding requires a render pass on the owning Vulkan device");
    }
    SDL_COMPILE_TIME_ASSERT(FP_DescriptorSize, sizeof(VkDescriptorSet) == sizeof(Uint64));
    SDL_memcpy(&command->fpAccelerationSet, &descriptor_set, sizeof(descriptor_set));
    command->needNewFragmentUniformOffsets = true;
    return true;
}

static bool SDLCALL FP_Track(void *userdata, SDL_GPUCommandBuffer *buffer, void *payload, FP_SDLVulkanNotify notify)
{
    VulkanRenderer *renderer = (VulkanRenderer *)userdata;
    VulkanCommandBuffer *command = (VulkanCommandBuffer *)buffer;
    if (!FP_CommandMatches(renderer, command) || !payload || !notify) {
        return SDL_SetError("External Vulkan retention requires an active command buffer and a lifecycle callback");
    }
    FPVulkanRetained *retained = SDL_malloc(sizeof(*retained));
    if (!retained) return false;
    retained->payload = payload;
    retained->notify = notify;
    retained->wasSubmitted = false;
    retained->command = command;
    SDL_LockMutex(renderer->submitLock);
    retained->next = command->fpRetained;
    command->fpRetained = retained;
    retained->previousGlobal = NULL;
    retained->nextGlobal = renderer->fpRetained;
    if (renderer->fpRetained) renderer->fpRetained->previousGlobal = retained;
    renderer->fpRetained = retained;
    SDL_UnlockMutex(renderer->submitLock);
    return true;
}

static void FP_MarkSubmitted(VulkanCommandBuffer *command)
{
    for (FPVulkanRetained *retained = command->fpRetained; retained; retained = retained->next) {
        if (!retained->wasSubmitted) {
            retained->wasSubmitted = true;
            retained->notify(retained->payload, FP_SDL_VULKAN_SUBMITTED);
        }
    }
}

static void FP_ReleaseRetained(VulkanRenderer *renderer, VulkanCommandBuffer *command)
{
    FPVulkanRetained *retained = command->fpRetained;
    command->fpRetained = NULL;
    while (retained) {
        FPVulkanRetained *next = retained->next;
        if (retained->previousGlobal) retained->previousGlobal->nextGlobal = retained->nextGlobal;
        else renderer->fpRetained = retained->nextGlobal;
        if (retained->nextGlobal) retained->nextGlobal->previousGlobal = retained->previousGlobal;
        retained->notify(retained->payload, retained->wasSubmitted ? FP_SDL_VULKAN_COMPLETED : FP_SDL_VULKAN_CANCELLED);
        SDL_free(retained);
        retained = next;
    }
}

#define FP_CACHE_LIMIT (64u * 1024u * 1024u)

static Uint32 FP_CacheWord(const Uint8 *value)
{
    return (Uint32)value[0] | (Uint32)value[1] << 8 | (Uint32)value[2] << 16 | (Uint32)value[3] << 24;
}

static void FP_CachePut(Uint8 *value, Uint32 word)
{
    for (unsigned i = 0; i < 4; ++i) value[i] = (Uint8)(word >> (i * 8));
}

static bool FP_ValidCache(const Uint8 *file, size_t size, Uint32 vendor, Uint32 device, const Uint8 *uuid)
{
    if (!file || size < 48 || size > FP_CACHE_LIMIT + 16 || SDL_memcmp(file, "FPVKPC01", 8) != 0) return false;
    const Uint32 bytes = FP_CacheWord(file + 8);
    if (bytes < 32 || bytes > FP_CACHE_LIMIT || bytes + (size_t)16 != size ||
        FP_CacheWord(file + 12) != SDL_crc32(0, file + 16, bytes)) return false;
    const Uint8 *header = file + 16;
    return FP_CacheWord(header) == 32 && FP_CacheWord(header + 4) == 1 &&
        FP_CacheWord(header + 8) == vendor && FP_CacheWord(header + 12) == device && SDL_memcmp(header + 16, uuid, 16) == 0;
}

static bool FP_WantPipelineStats(void)
{
    const char *value = SDL_getenv("FP_GPU_PIPELINE_STATS");
    return value && SDL_strcmp(value, "1") == 0;
}

static void FP_RequestRayQuery(SDL_PropertiesID props, VulkanRenderer *renderer)
{
    if (!SDL_GetBooleanProperty(props, FP_SDL_RAY_QUERY_PROPERTY, false)) return;
    PFN_vkEnumerateInstanceVersion enumerate = (PFN_vkEnumerateInstanceVersion)vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceVersion");
    Uint32 version = VK_API_VERSION_1_0;
    if (enumerate && enumerate(&version) == VK_SUCCESS && version >= VK_API_VERSION_1_2) renderer->fpRayRequested = true;
}

static void FP_QueryRayQuery(VulkanRenderer *renderer, VulkanFeatures *features)
{
    static const char *extensions[] = { VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, VK_KHR_RAY_QUERY_EXTENSION_NAME,
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME };
    if (!renderer->fpRayRequested || renderer->physicalDeviceProperties.properties.apiVersion < VK_API_VERSION_1_2 ||
        renderer->physicalDeviceProperties.properties.limits.maxBoundDescriptorSets < 5) return;
    Uint32 count = 0;
    if (renderer->vkEnumerateDeviceExtensionProperties(renderer->physicalDevice, NULL, &count, NULL) != VK_SUCCESS) return;
    VkExtensionProperties *available = SDL_calloc(count, sizeof(*available));
    if (!available) return;
    bool supported = renderer->vkEnumerateDeviceExtensionProperties(renderer->physicalDevice, NULL, &count, available) == VK_SUCCESS;
    const bool wantStats = FP_WantPipelineStats();
    bool statsAvailable = false;
    for (Uint32 i = 0; i < count; ++i) statsAvailable |= SDL_strcmp(available[i].extensionName, VK_KHR_PIPELINE_EXECUTABLE_PROPERTIES_EXTENSION_NAME) == 0;
    for (Uint32 i = 0; i < SDL_arraysize(extensions); ++i) {
        bool found = false;
        for (Uint32 j = 0; j < count; ++j) found |= SDL_strcmp(available[j].extensionName, extensions[i]) == 0;
        supported &= found;
    }
    SDL_free(available);
    if (!supported || !renderer->vkGetPhysicalDeviceFeatures2) return;
    VkPhysicalDeviceRayQueryFeaturesKHR query = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR };
    VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR statistics = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_EXECUTABLE_PROPERTIES_FEATURES_KHR };
    if (wantStats && statsAvailable) query.pNext = &statistics;
    VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, &query };
    VkPhysicalDeviceVulkan12Features vulkan12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &acceleration };
    VkPhysicalDeviceFeatures2 queried = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &vulkan12 };
    renderer->vkGetPhysicalDeviceFeatures2(renderer->physicalDevice, &queried);
    if (!query.rayQuery || !acceleration.accelerationStructure || !vulkan12.bufferDeviceAddress) return;
    PFN_vkGetPhysicalDeviceProperties2 properties = (PFN_vkGetPhysicalDeviceProperties2)vkGetInstanceProcAddr(renderer->instance, "vkGetPhysicalDeviceProperties2");
    if (!properties) return;
    VkPhysicalDeviceAccelerationStructurePropertiesKHR accelerationProperties = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR };
    VkPhysicalDeviceProperties2 queriedProperties = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &accelerationProperties };
    properties(renderer->physicalDevice, &queriedProperties);
    if (accelerationProperties.maxDescriptorSetAccelerationStructures < 1 || accelerationProperties.maxPerStageDescriptorAccelerationStructures < 1) return;
    Uint32 queueCount = 0;
    renderer->vkGetPhysicalDeviceQueueFamilyProperties(renderer->physicalDevice, &queueCount, NULL);
    VkQueueFamilyProperties *queues = SDL_calloc(queueCount, sizeof(*queues));
    if (!queues) return;
    renderer->vkGetPhysicalDeviceQueueFamilyProperties(renderer->physicalDevice, &queueCount, queues);
    bool compute = renderer->queueFamilyIndex < queueCount && (queues[renderer->queueFamilyIndex].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0;
    if (compute) renderer->fpBridge.timestamp_valid_bits = queues[renderer->queueFamilyIndex].timestampValidBits;
    SDL_free(queues);
    if (!compute) return;
    features->usesCustomVulkanOptions = true;
    features->desiredApiVersion = SDL_max(features->desiredApiVersion, VK_API_VERSION_1_2);
    features->desiredVulkan11DeviceFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
    features->desiredVulkan12DeviceFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features->desiredVulkan13DeviceFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features->desiredVulkan12DeviceFeatures.bufferDeviceAddress = VK_TRUE;
    renderer->fpRayQuery.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
    renderer->fpRayQuery.rayQuery = VK_TRUE;
    renderer->fpAccelerationFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    renderer->fpAccelerationFeatures.accelerationStructure = VK_TRUE;
    renderer->fpAccelerationFeatures.pNext = &renderer->fpRayQuery;
    renderer->fpPipelineStats = wantStats && statsAvailable && statistics.pipelineExecutableInfo;
    if (renderer->fpPipelineStats) {
        renderer->fpPipelineStatsFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_EXECUTABLE_PROPERTIES_FEATURES_KHR;
        renderer->fpPipelineStatsFeatures.pipelineExecutableInfo = VK_TRUE;
        renderer->fpRayQuery.pNext = &renderer->fpPipelineStatsFeatures;
    }
    renderer->fpRayEnabled = true;
}

static void FP_InitializePipelineCache(VulkanRenderer *renderer)
{
    const VkPhysicalDeviceProperties *properties = &renderer->physicalDeviceProperties.properties;
    char uuid[33];
    for (unsigned i = 0; i < 16; ++i) SDL_snprintf(uuid + i * 2, 3, "%02x", properties->pipelineCacheUUID[i]);
    char *folder = SDL_GetPrefPath("FirstPrinciples", "VulkanCache");
    if (folder) SDL_asprintf(&renderer->fpCachePath, "%s%08x-%08x-%08x-%s%s.bin", folder,
        properties->vendorID, properties->deviceID, properties->driverVersion, uuid, renderer->fpPipelineStats ? "-stats" : "");
    SDL_free(folder);
    Uint8 *file = NULL;
    size_t bytes = 0;
    SDL_IOStream *stream = renderer->fpCachePath ? SDL_IOFromFile(renderer->fpCachePath, "rb") : NULL;
    if (stream) {
        const Sint64 length = SDL_GetIOSize(stream);
        if (length >= 48 && length <= FP_CACHE_LIMIT + 16) {
            file = SDL_malloc((size_t)length);
            if (file && SDL_ReadIO(stream, file, (size_t)length) == (size_t)length &&
                FP_ValidCache(file, (size_t)length, properties->vendorID, properties->deviceID, properties->pipelineCacheUUID)) bytes = (size_t)length - 16;
        }
        SDL_CloseIO(stream);
    }
    VkPipelineCacheCreateInfo info = { VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
    info.initialDataSize = bytes;
    info.pInitialData = bytes ? file + 16 : NULL;
    VkResult result = renderer->vkCreatePipelineCache(renderer->logicalDevice, &info, NULL, &renderer->fpPipelineCache);
    if (result != VK_SUCCESS && bytes) {
        bytes = info.initialDataSize = 0;
        info.pInitialData = NULL;
        result = renderer->vkCreatePipelineCache(renderer->logicalDevice, &info, NULL, &renderer->fpPipelineCache);
    }
    if (result != VK_SUCCESS) renderer->fpPipelineCache = VK_NULL_HANDLE;
    SDL_free(file);
    SDL_ClearError();
    if (renderer->fpPipelineCache) SDL_LogInfo(SDL_LOG_CATEGORY_GPU, "Vulkan pipeline cache loaded %zu bytes", bytes);
}

static void FP_DestroyPipelineCache(VulkanRenderer *renderer)
{
    if (!renderer->fpPipelineCache && !renderer->fpCachePath) return;
    if (renderer->fpPipelineCache && renderer->fpCachePath) {
        size_t bytes = 0;
        if (renderer->vkGetPipelineCacheData(renderer->logicalDevice, renderer->fpPipelineCache, &bytes, NULL) == VK_SUCCESS && bytes >= 32 && bytes <= FP_CACHE_LIMIT) {
            Uint8 *file = SDL_malloc(bytes + 16);
            if (file) {
                if (renderer->vkGetPipelineCacheData(renderer->logicalDevice, renderer->fpPipelineCache, &bytes, file + 16) == VK_SUCCESS && bytes >= 32 && bytes <= FP_CACHE_LIMIT) {
                    SDL_memcpy(file, "FPVKPC01", 8);
                    FP_CachePut(file + 8, (Uint32)bytes);
                    FP_CachePut(file + 12, SDL_crc32(0, file + 16, bytes));
                    const VkPhysicalDeviceProperties *properties = &renderer->physicalDeviceProperties.properties;
                    if (FP_ValidCache(file, bytes + 16, properties->vendorID, properties->deviceID, properties->pipelineCacheUUID)) {
                        SDL_Time now = 0;
                        SDL_GetCurrentTime(&now);
                        char *temporary = NULL;
                        SDL_asprintf(&temporary, "%s.%llu-%llu-%p.tmp", renderer->fpCachePath, (unsigned long long)now,
                            (unsigned long long)SDL_GetTicksNS(), (void *)renderer);
                        SDL_IOStream *stream = temporary ? SDL_IOFromFile(temporary, "wbx") : NULL;
                        if (stream) {
                            bool saved = SDL_SaveFile_IO(stream, file, bytes + 16, false);
                            if (!SDL_CloseIO(stream)) saved = false;
                            if (saved && SDL_RenamePath(temporary, renderer->fpCachePath))
                                SDL_LogInfo(SDL_LOG_CATEGORY_GPU, "Vulkan pipeline cache saved %zu bytes", bytes);
                            else SDL_RemovePath(temporary);
                        }
                        SDL_free(temporary);
                    }
                }
                SDL_free(file);
            }
        }
    }
    if (renderer->fpPipelineCache) {
        const Uint64 started = SDL_GetTicksNS();
        SDL_LogDebug(SDL_LOG_CATEGORY_GPU, "Vulkan teardown cacheDestroy begin");
        renderer->vkDestroyPipelineCache(renderer->logicalDevice, renderer->fpPipelineCache, NULL);
        SDL_LogDebug(SDL_LOG_CATEGORY_GPU, "Vulkan teardown cacheDestroy end %.3f ms", (SDL_GetTicksNS() - started) / 1e6);
    }
    renderer->fpPipelineCache = VK_NULL_HANDLE;
    SDL_free(renderer->fpCachePath);
    renderer->fpCachePath = NULL;
    SDL_ClearError();
}

static void FP_ReportPipelineStats(VulkanRenderer *renderer, VkPipeline pipeline, const char *name)
{
    if (!renderer->fpPipelineStats) return;
    PFN_vkGetPipelineExecutablePropertiesKHR properties = (PFN_vkGetPipelineExecutablePropertiesKHR)renderer->vkGetDeviceProcAddr(renderer->logicalDevice, "vkGetPipelineExecutablePropertiesKHR");
    PFN_vkGetPipelineExecutableStatisticsKHR statistics = (PFN_vkGetPipelineExecutableStatisticsKHR)renderer->vkGetDeviceProcAddr(renderer->logicalDevice, "vkGetPipelineExecutableStatisticsKHR");
    if (!properties || !statistics) return;
    VkPipelineInfoKHR pipelineInfo = { VK_STRUCTURE_TYPE_PIPELINE_INFO_KHR };
    pipelineInfo.pipeline = pipeline;
    Uint32 count = 0;
    if (properties(renderer->logicalDevice, &pipelineInfo, &count, NULL) != VK_SUCCESS || count == 0 || count > 64) return;
    const Uint32 capacity = count;
    VkPipelineExecutablePropertiesKHR *executables = SDL_calloc(count, sizeof(*executables));
    if (!executables) return;
    for (Uint32 i = 0; i < count; ++i) executables[i].sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_PROPERTIES_KHR;
    if (properties(renderer->logicalDevice, &pipelineInfo, &count, executables) == VK_SUCCESS && count <= capacity) {
        for (Uint32 i = 0; i < count; ++i) {
            const VkPipelineExecutablePropertiesKHR *executable = &executables[i];
            if (SDL_strnlen(executable->name, sizeof(executable->name)) == sizeof(executable->name) ||
                SDL_strnlen(executable->description, sizeof(executable->description)) == sizeof(executable->description)) continue;
            SDL_LogInfo(SDL_LOG_CATEGORY_GPU, "GPU pipeline executable pipeline=\"%s\" index=%u stages=0x%x subgroup=%u name=\"%s\" description=\"%s\"",
                name, i, executable->stages, executable->subgroupSize, executable->name, executable->description);
            VkPipelineExecutableInfoKHR info = { VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INFO_KHR };
            info.pipeline = pipeline;
            info.executableIndex = i;
            Uint32 statCount = 0;
            if (statistics(renderer->logicalDevice, &info, &statCount, NULL) != VK_SUCCESS || statCount == 0 || statCount > 256) continue;
            const Uint32 statCapacity = statCount;
            VkPipelineExecutableStatisticKHR *values = SDL_calloc(statCount, sizeof(*values));
            if (!values) continue;
            for (Uint32 j = 0; j < statCount; ++j) values[j].sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_STATISTIC_KHR;
            if (statistics(renderer->logicalDevice, &info, &statCount, values) == VK_SUCCESS && statCount <= statCapacity) {
                for (Uint32 j = 0; j < statCount; ++j) {
                    const VkPipelineExecutableStatisticKHR *statistic = &values[j];
                    if (SDL_strnlen(statistic->name, sizeof(statistic->name)) == sizeof(statistic->name) ||
                        SDL_strnlen(statistic->description, sizeof(statistic->description)) == sizeof(statistic->description)) continue;
                    char value[64];
                    switch (statistic->format) {
                    case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_BOOL32_KHR:
                        if (statistic->value.b32 != VK_FALSE && statistic->value.b32 != VK_TRUE) continue;
                        SDL_snprintf(value, sizeof(value), "%s", statistic->value.b32 ? "true" : "false");
                        break;
                    case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_INT64_KHR:
                        SDL_snprintf(value, sizeof(value), "%lld", (long long)statistic->value.i64);
                        break;
                    case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR:
                        SDL_snprintf(value, sizeof(value), "%llu", (unsigned long long)statistic->value.u64);
                        break;
                    case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_FLOAT64_KHR:
                        if (SDL_isnan(statistic->value.f64) || SDL_isinf(statistic->value.f64)) continue;
                        SDL_snprintf(value, sizeof(value), "%.17g", statistic->value.f64);
                        break;
                    default: continue;
                    }
                    SDL_LogInfo(SDL_LOG_CATEGORY_GPU, "GPU pipeline statistic pipeline=\"%s\" executable=%u name=\"%s\" format=%u value=%s description=\"%s\"",
                        name, i, statistic->name, (unsigned)statistic->format, value, statistic->description);
                }
            }
            SDL_free(values);
        }
    }
    SDL_free(executables);
}

static void FP_InitializeBridge(VulkanRenderer *renderer)
{
    if (!renderer->fpRayEnabled) return;
    VkDescriptorSetLayoutBinding binding = { 0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL };
    VkDescriptorSetLayoutCreateInfo info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, NULL, 0, 1, &binding };
    if (renderer->vkCreateDescriptorSetLayout(renderer->logicalDevice, &info, NULL, &renderer->fpAccelerationLayout) != VK_SUCCESS) {
        renderer->fpRayEnabled = false;
        return;
    }
    FP_InitializePipelineCache(renderer);
    renderer->fpBridge.version = FP_SDL_VULKAN_BRIDGE_VERSION;
    renderer->fpBridge.size = sizeof(renderer->fpBridge);
    renderer->fpBridge.userdata = renderer;
    renderer->fpBridge.instance = (void *)renderer->instance;
    renderer->fpBridge.physical_device = (void *)renderer->physicalDevice;
    renderer->fpBridge.device = (void *)renderer->logicalDevice;
    SDL_COMPILE_TIME_ASSERT(FP_LayoutSize, sizeof(VkDescriptorSetLayout) == sizeof(Uint64));
    SDL_memcpy(&renderer->fpBridge.acceleration_layout, &renderer->fpAccelerationLayout, sizeof(Uint64));
    renderer->fpBridge.get_proc = FP_GetProc;
    renderer->fpBridge.command = FP_Command;
    renderer->fpBridge.bind = FP_BindAcceleration;
    renderer->fpBridge.track = FP_Track;
    SDL_SetPointerProperty(renderer->props, FP_SDL_VULKAN_BRIDGE_PROPERTY, &renderer->fpBridge);
    SDL_LogInfo(SDL_LOG_CATEGORY_GPU, "Hardware ray queries enabled on %s: VK_KHR_acceleration_structure VK_KHR_ray_query VK_KHR_deferred_host_operations", renderer->physicalDeviceProperties.properties.deviceName);
}
