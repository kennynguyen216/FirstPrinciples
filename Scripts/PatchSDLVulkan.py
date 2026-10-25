import argparse
import hashlib
from pathlib import Path
import shutil


SOURCE_SHA256 = '28b42c4286805a6cff952bd698de29659180132ae881eedd3f6bf24d16583361'
ROOT = Path(__file__).resolve().parents[1]


def patch(source):
    def replace(old, new, count=1):
        nonlocal source
        if source.count(old) != count:
            raise ValueError(f'Unexpected SDL source anchor count: {old[:80]}')
        source = source.replace(old, new)

    def enter(signature, body):
        nonlocal source
        if source.count(signature) != 1:
            raise ValueError(f'Unexpected SDL function count: {signature}')
        start = source.index(signature)
        opening = source.index('\n{\n', start) + 3
        source = source[:opening] + body + source[opening:]

    replace('#include "../SDL_sysgpu.h"', '#include "../SDL_sysgpu.h"\n#include "FP_SDLVulkanBridge.h"')
    replace('typedef struct VulkanCommandBuffer\n', '''typedef struct FPVulkanRetained
{
    void *payload;
    FP_SDLVulkanNotify notify;
    bool wasSubmitted;
    void *command;
    struct FPVulkanRetained *next;
    struct FPVulkanRetained *nextGlobal;
    struct FPVulkanRetained *previousGlobal;
} FPVulkanRetained;

typedef struct VulkanCommandBuffer
''')
    replace('    CommandBufferCommonHeader common;\n    VulkanRenderer *renderer;', '''    CommandBufferCommonHeader common;
    VulkanRenderer *renderer;
    VkDescriptorSet fpAccelerationSet;
    FPVulkanRetained *fpRetained;''')
    replace('    commandBuffer = SDL_malloc(sizeof(VulkanCommandBuffer));', '''    commandBuffer = SDL_malloc(sizeof(VulkanCommandBuffer));
    commandBuffer->fpRetained = NULL;
    commandBuffer->fpAccelerationSet = VK_NULL_HANDLE;''')
    replace('    SDL_PropertiesID props;', '''    SDL_PropertiesID props;
    bool fpRayRequested;
    bool fpRayEnabled;
    bool fpPipelineStats;
    VkPhysicalDeviceAccelerationStructureFeaturesKHR fpAccelerationFeatures;
    VkPhysicalDeviceRayQueryFeaturesKHR fpRayQuery;
    VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR fpPipelineStatsFeatures;
    VkDescriptorSetLayout fpAccelerationLayout;
    FPVulkanRetained *fpRetained;
    VkPipelineCache fpPipelineCache;
    char *fpCachePath;
    FP_SDLVulkanBridge fpBridge;''')
    replace('// Forward declarations', '#include "FP_SDLVulkanBridge.inc"\n\n// Forward declarations')
    replace('        SDL_free(commandBuffer->presentDatas);', '        FP_ReleaseRetained(renderer, commandBuffer);\n        SDL_free(commandBuffer->presentDatas);')
    replace('    SDL_free(renderer->submittedCommandBuffers);', '''    while (renderer->fpRetained) {
        FP_ReleaseRetained(renderer, (VulkanCommandBuffer *)renderer->fpRetained->command);
    }
    SDL_free(renderer->submittedCommandBuffers);''')
    replace('    renderer->vkDestroyDevice(renderer->logicalDevice, NULL);', '''    if (renderer->fpAccelerationLayout) renderer->vkDestroyDescriptorSetLayout(renderer->logicalDevice, renderer->fpAccelerationLayout, NULL);
    FP_DestroyPipelineCache(renderer);
    const Uint64 fpDeviceDestroyStart = SDL_GetTicksNS();
    SDL_LogDebug(SDL_LOG_CATEGORY_GPU, "Vulkan teardown deviceDestroy begin");
    renderer->vkDestroyDevice(renderer->logicalDevice, NULL);
    SDL_LogDebug(SDL_LOG_CATEGORY_GPU, "Vulkan teardown deviceDestroy end %.3f ms", (SDL_GetTicksNS() - fpDeviceDestroyStart) / 1e6);''')
    replace('\n    renderer->vkDestroyInstance(renderer->instance, NULL);', '''
    const Uint64 fpInstanceDestroyStart = SDL_GetTicksNS();
    SDL_LogDebug(SDL_LOG_CATEGORY_GPU, "Vulkan teardown instanceDestroy begin");
    renderer->vkDestroyInstance(renderer->instance, NULL);
    SDL_LogDebug(SDL_LOG_CATEGORY_GPU, "Vulkan teardown instanceDestroy end %.3f ms", (SDL_GetTicksNS() - fpInstanceDestroyStart) / 1e6);''')
    replace('    SDL_free(device);\n    SDL_Vulkan_UnloadLibrary();\n}', '''    SDL_free(device);
    const Uint64 fpLoaderUnloadStart = SDL_GetTicksNS();
    SDL_LogDebug(SDL_LOG_CATEGORY_GPU, "Vulkan teardown loaderUnload begin");
    SDL_Vulkan_UnloadLibrary();
    SDL_LogDebug(SDL_LOG_CATEGORY_GPU, "Vulkan teardown loaderUnload end %.3f ms", (SDL_GetTicksNS() - fpLoaderUnloadStart) / 1e6);
}''')
    replace('''    vulkanResult = renderer->vkCreateGraphicsPipelines(
        renderer->logicalDevice,
        VK_NULL_HANDLE,''', '''    if (renderer->fpPipelineStats) graphicsPipeline->pipeline = VK_NULL_HANDLE;
    vulkanResult = renderer->vkCreateGraphicsPipelines(
        renderer->logicalDevice,
        renderer->fpPipelineCache,''')
    replace('''    vulkanResult = renderer->vkCreateComputePipelines(
        renderer->logicalDevice,
        (VkPipelineCache)VK_NULL_HANDLE,''', '''    vulkanResult = renderer->vkCreateComputePipelines(
        renderer->logicalDevice,
        renderer->fpPipelineCache,''')
    replace('    vkPipelineCreateInfo.flags = 0;', '    vkPipelineCreateInfo.flags = renderer->fpPipelineStats ? VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR : 0;')
    replace('''        &graphicsPipeline->pipeline);

    SDL_stack_free(vertexInputBindingDescriptions);''', '''        &graphicsPipeline->pipeline);
    if (vulkanResult != VK_SUCCESS && (vkPipelineCreateInfo.flags & VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR)) {
        if (graphicsPipeline->pipeline) renderer->vkDestroyPipeline(renderer->logicalDevice, graphicsPipeline->pipeline, NULL);
        graphicsPipeline->pipeline = VK_NULL_HANDLE;
        vkPipelineCreateInfo.flags = 0;
        vulkanResult = renderer->vkCreateGraphicsPipelines(renderer->logicalDevice, renderer->fpPipelineCache,
            1, &vkPipelineCreateInfo, NULL, &graphicsPipeline->pipeline);
    }
    if (vulkanResult == VK_SUCCESS && (vkPipelineCreateInfo.flags & VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR))
        FP_ReportPipelineStats(renderer, graphicsPipeline->pipeline, SDL_GetStringProperty(createinfo->props, SDL_PROP_GPU_GRAPHICSPIPELINE_CREATE_NAME_STRING, "unnamed"));

    SDL_stack_free(vertexInputBindingDescriptions);''')
    replace('    VkDescriptorSetLayout descriptorSetLayouts[4];', '    VkDescriptorSetLayout descriptorSetLayouts[5];')
    replace('    pipelineLayoutCreateInfo.setLayoutCount = 4;', '''    descriptorSetLayouts[4] = renderer->fpAccelerationLayout;
    pipelineLayoutCreateInfo.setLayoutCount = renderer->fpAccelerationLayout ? 5 : 4;''')
    replace('    commandBuffer->needNewVertexUniformOffsets = false;\n    commandBuffer->needNewFragmentUniformOffsets = false;', '''    if (renderer->fpAccelerationLayout && commandBuffer->fpAccelerationSet) {
        renderer->vkCmdBindDescriptorSets(commandBuffer->commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
            resourceLayout->pipelineLayout, 4, 1, &commandBuffer->fpAccelerationSet, 0, NULL);
    }
    commandBuffer->needNewVertexUniformOffsets = false;
    commandBuffer->needNewFragmentUniformOffsets = false;''')
    replace('    VkRenderPass renderPass;\n    VulkanFramebuffer *framebuffer;', '    vulkanCommandBuffer->fpAccelerationSet = VK_NULL_HANDLE;\n    VkRenderPass renderPass;\n    VulkanFramebuffer *framebuffer;')
    replace('    renderer->vkCmdEndRenderPass(\n        vulkanCommandBuffer->commandBuffer);', '    vulkanCommandBuffer->fpAccelerationSet = VK_NULL_HANDLE;\n    renderer->vkCmdEndRenderPass(\n        vulkanCommandBuffer->commandBuffer);')
    replace('    commandBuffer->currentComputePipeline = NULL;', '    SDL_assert(commandBuffer->fpRetained == NULL);\n    commandBuffer->fpAccelerationSet = VK_NULL_HANDLE;\n    commandBuffer->currentComputePipeline = NULL;')
    for stage in ('Render', 'Compute', 'Copy'):
        member = stage.lower() + '_pass'
        enter(f'static void VULKAN_Begin{stage}Pass(\n', f'    ((CommandBufferCommonHeader *)commandBuffer)->{member}.in_progress = true;\n')
        enter(f'static void VULKAN_End{stage}Pass(\n', f'    ((CommandBufferCommonHeader *)commandBuffer)->{member}.in_progress = false;\n')
    replace('    SDL_assert(commandBuffer->fpRetained == NULL);', '''    SDL_assert(commandBuffer->fpRetained == NULL);
    commandBuffer->common.submitted = false;
    commandBuffer->common.render_pass.in_progress = false;
    commandBuffer->common.compute_pass.in_progress = false;
    commandBuffer->common.copy_pass.in_progress = false;''')
    enter('static bool VULKAN_Cancel(\n', '    ((CommandBufferCommonHeader *)commandBuffer)->submitted = true;\n')
    replace('    if (commandBuffer->autoReleaseFence) {', '    FP_ReleaseRetained(renderer, commandBuffer);\n    if (commandBuffer->autoReleaseFence) {')
    replace('''    if (!VULKAN_INTERNAL_EndCommandBuffer(renderer, vulkanCommandBuffer)) {
        SDL_UnlockMutex(renderer->submitLock);''', '''    if (!VULKAN_INTERNAL_EndCommandBuffer(renderer, vulkanCommandBuffer)) {
        FP_ReleaseRetained(renderer, vulkanCommandBuffer);
        SDL_UnlockMutex(renderer->submitLock);''')
    replace('''    if (vulkanCommandBuffer->inFlightFence == NULL) {
        SDL_UnlockMutex(renderer->submitLock);''', '''    if (vulkanCommandBuffer->inFlightFence == NULL) {
        FP_ReleaseRetained(renderer, vulkanCommandBuffer);
        SDL_UnlockMutex(renderer->submitLock);''')
    replace('''    // Present, if applicable
''', '''    FP_MarkSubmitted(vulkanCommandBuffer);
    // Present, if applicable
''')
    replace('    // Handle opt-in device features\n    VULKAN_INTERNAL_AddOptInVulkanOptions(props, renderer, features);', '''    // Handle opt-in device features
    VULKAN_INTERNAL_AddOptInVulkanOptions(props, renderer, features);
    FP_RequestRayQuery(props, renderer);''')
    replace('''                             : VK_MAKE_VERSION(1, 0, 0);
''', '''                             : VK_MAKE_VERSION(1, 0, 0);
    if (renderer->fpRayRequested) appInfo.apiVersion = SDL_max(appInfo.apiVersion, VK_API_VERSION_1_2);
''')
    replace('''    if (!VULKAN_INTERNAL_CreateLogicalDevice(renderer, &features)) {''', '''    FP_QueryRayQuery(renderer, &features);
    if (!VULKAN_INTERNAL_CreateLogicalDevice(renderer, &features)) {''')
    replace('        extensionCount + features->additionalDeviceExtensionCount);', '        extensionCount + features->additionalDeviceExtensionCount + 4);')
    replace('    deviceCreateInfo.enabledExtensionCount = extensionCount;', '''    if (renderer->fpRayEnabled) {
        const char *rayExtensions[] = { VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, VK_KHR_RAY_QUERY_EXTENSION_NAME,
            VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
            renderer->fpPipelineStats ? VK_KHR_PIPELINE_EXECUTABLE_PROPERTIES_EXTENSION_NAME : NULL };
        for (Uint32 ray = 0; ray < SDL_arraysize(rayExtensions); ++ray) {
            if (!rayExtensions[ray]) continue;
            bool found = false;
            for (Uint32 enabled = 0; enabled < extensionCount; ++enabled) found |= SDL_strcmp(deviceExtensions[enabled], rayExtensions[ray]) == 0;
            if (!found) deviceExtensions[extensionCount++] = rayExtensions[ray];
        }
    }
    deviceCreateInfo.enabledExtensionCount = extensionCount;''')
    replace('''    vulkanResult = renderer->vkCreateDevice(
''', '''    if (renderer->fpRayEnabled) {
        VkBaseOutStructure *tail = (VkBaseOutStructure *)&featureList;
        while (tail->pNext) tail = tail->pNext;
        tail->pNext = (VkBaseOutStructure *)&renderer->fpAccelerationFeatures;
    }
    vulkanResult = renderer->vkCreateDevice(
''')
    replace('''    SDL_stack_free((void *)deviceExtensions);
    CHECK_VULKAN_ERROR_AND_RETURN(vulkanResult, vkCreateDevice, 0);''', '''    if (vulkanResult != VK_SUCCESS && renderer->fpPipelineStats) {
        renderer->fpPipelineStats = false;
        renderer->fpRayQuery.pNext = NULL;
        Uint32 output = 0;
        for (Uint32 i = 0; i < deviceCreateInfo.enabledExtensionCount; ++i)
            if (SDL_strcmp(deviceExtensions[i], VK_KHR_PIPELINE_EXECUTABLE_PROPERTIES_EXTENSION_NAME) != 0) deviceExtensions[output++] = deviceExtensions[i];
        deviceCreateInfo.enabledExtensionCount = output;
        vulkanResult = renderer->vkCreateDevice(renderer->physicalDevice, &deviceCreateInfo, NULL, &renderer->logicalDevice);
    }
    SDL_stack_free((void *)deviceExtensions);
    CHECK_VULKAN_ERROR_AND_RETURN(vulkanResult, vkCreateDevice, 0);''')
    replace('''    // FIXME: just move this into this function
    result = (SDL_GPUDevice *)SDL_calloc(1, sizeof(SDL_GPUDevice));''', '''    FP_InitializeBridge(renderer);
    // FIXME: just move this into this function
    result = (SDL_GPUDevice *)SDL_calloc(1, sizeof(SDL_GPUDevice));''')
    return source


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('source', type=Path)
    parser.add_argument('--self-check', action='store_true')
    args = parser.parse_args()
    target = args.source / 'src/gpu/vulkan/SDL_gpu_vulkan.c'
    backup = target.with_suffix('.c.fp-original')
    original = (backup if backup.exists() else target).read_bytes()
    if hashlib.sha256(original).hexdigest() != SOURCE_SHA256:
        raise ValueError('Expected pinned SDL 3.4.18 Vulkan source')
    changed = patch(original.decode('utf-8'))
    assert changed.count('#include "FP_SDLVulkanBridge.inc"') == 1
    assert changed.count('VULKAN_INTERNAL_BindGraphicsDescriptorSets(renderer, vulkanCommandBuffer);') == 4
    assert changed.count('    commandBuffer = SDL_malloc(sizeof(VulkanCommandBuffer));\n    commandBuffer->fpRetained = NULL;\n    commandBuffer->fpAccelerationSet = VK_NULL_HANDLE;') == 1
    if args.self_check:
        assert patch(original.decode('utf-8')) == changed
        print('PASS pinned SDL bridge patch and shared draw hook checks')
        return
    encoded = changed.encode('utf-8')
    stamp = target.with_suffix('.c.fp-patched.sha256')
    current = target.read_bytes()
    if backup.exists() and current != encoded:
        if not stamp.exists() or hashlib.sha256(current).hexdigest() != stamp.read_text().strip():
            raise ValueError('Patched SDL source has unrecognized edits')
    if not backup.exists():
        backup.write_bytes(original)
    target.write_bytes(encoded)
    stamp.write_text(hashlib.sha256(encoded).hexdigest() + '\n', encoding='ascii')
    shutil.copyfile(ROOT / 'src/Engine/Render/SDLVulkanBridge.h', target.parent / 'FP_SDLVulkanBridge.h')
    shutil.copyfile(ROOT / 'Scripts/SDLVulkanBridge.inc', target.parent / 'FP_SDLVulkanBridge.inc')
    print('Patched pinned SDL Vulkan backend')


if __name__ == '__main__':
    main()
