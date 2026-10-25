from pathlib import Path
import shutil
import subprocess

from PatchSDLVulkan import ROOT, patch, SOURCE_SHA256
import hashlib


def main():
    output = ROOT / 'Intermediate/HardwareRT/bridge-unit'
    output.mkdir(parents=True, exist_ok=True)
    original = ROOT / 'Intermediate/HardwareRT/SDL/src/gpu/vulkan/SDL_gpu_vulkan.c.fp-original'
    data = original.read_bytes()
    assert hashlib.sha256(data).hexdigest() == SOURCE_SHA256
    source = patch(data.decode('utf-8'))
    submit = source[source.index('    vulkanResult = renderer->vkQueueSubmit('):source.index('    // Present, if applicable')]
    assert submit.index('CHECK_VULKAN_ERROR_AND_RETURN') < submit.index('FP_MarkSubmitted(vulkanCommandBuffer);')
    assert 'FP_ReleaseRetained' not in submit
    assert source.count('FP_MarkSubmitted(vulkanCommandBuffer);') == 1
    assert source.count('FP_ReleaseRetained(renderer, vulkanCommandBuffer);') == 2
    destroy = source[source.index('static void VULKAN_DestroyDevice('):]
    assert destroy.index('VULKAN_Wait(device->driverData);') < destroy.index('FP_DestroyPipelineCache(renderer);') < destroy.index('renderer->vkDestroyDevice(')
    assert source.count('FP_DestroyPipelineCache(renderer);') == 1
    for kind in ('Graphics', 'Compute'):
        assert f'renderer->vkCreate{kind}Pipelines(\n        renderer->logicalDevice,\n        renderer->fpPipelineCache,' in source
    for label, native in (('deviceDestroy', 'renderer->vkDestroyDevice('), ('instanceDestroy', 'renderer->vkDestroyInstance('), ('loaderUnload', 'SDL_Vulkan_UnloadLibrary();')):
        begin = destroy.index(f'Vulkan teardown {label} begin')
        assert begin < destroy.index(native, begin) < destroy.index(f'Vulkan teardown {label} end')
    assert 'vkPipelineCreateInfo.flags = renderer->fpPipelineStats ? VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR : 0;' in source
    assert 'if (vulkanResult == VK_SUCCESS && (vkPipelineCreateInfo.flags & VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR))\n        FP_ReportPipelineStats' in source
    assert 'renderer->fpPipelineStats ? VK_KHR_PIPELINE_EXECUTABLE_PROPERTIES_EXTENSION_NAME : NULL' in source
    assert '    vkShaderCreateInfo.flags = 0;' in source
    bridge = (ROOT / 'Scripts/SDLVulkanBridge.inc').read_text(encoding='utf-8')
    assert 'if (wantStats && statsAvailable) query.pNext = &statistics;' in bridge
    assert 'renderer->fpPipelineStats = wantStats && statsAvailable && statistics.pipelineExecutableInfo;' in bridge
    assert 'if (!renderer->fpPipelineStats) return;' in bridge
    assert 'count > 64' in bridge and 'statCount > 256' in bridge
    assert 'renderer->fpPipelineStats ? "-stats" : ""' in bridge
    start = source.index('typedef struct FPVulkanRetained\n')
    retained_type = source[start:source.index('typedef struct VulkanCommandBuffer\n', start)]
    start = source.index('    commandBuffer = SDL_malloc(sizeof(VulkanCommandBuffer));')
    allocation = source[start:source.index('    commandBuffer->renderer = renderer;', start)]
    start = source.index('    SDL_assert(commandBuffer->fpRetained == NULL);')
    reset = source[start:source.index('    commandBuffer->currentComputePipeline = NULL;', start)]
    functions = (ROOT / 'Scripts/SDLVulkanBridge.inc').read_text(encoding='utf-8').split('static void FP_RequestRayQuery')[0]
    program = r'''
#undef NDEBUG
#include <SDL3/SDL.h>
#include "SDLVulkanBridge.h"
#include <assert.h>
#include <stdio.h>
typedef void *VkDevice;
typedef void *VkInstance;
typedef Uint64 VkDescriptorSet;
typedef SDL_FunctionPointer PFN_vkVoidFunction;
#define VK_NULL_HANDLE 0
typedef struct SDL_GPURenderer SDL_GPURenderer;
struct SDL_GPUDevice { SDL_GPURenderer *driverData; };
typedef struct Pass { SDL_GPUCommandBuffer *command_buffer; bool in_progress; } Pass;
typedef Pass RenderPass;
typedef struct CommandBufferCommonHeader {
    SDL_GPUDevice *device;
    RenderPass render_pass;
    Pass compute_pass;
    Pass copy_pass;
    bool submitted;
} CommandBufferCommonHeader;
typedef struct VulkanRenderer VulkanRenderer;
typedef struct VulkanCommandPool { SDL_ThreadID threadID; } VulkanCommandPool;
@RETAINED@
typedef struct VulkanCommandBuffer {
    CommandBufferCommonHeader common;
    VulkanRenderer *renderer;
    void *commandBuffer;
    VulkanCommandPool *commandPool;
    VkDescriptorSet fpAccelerationSet;
    FPVulkanRetained *fpRetained;
    bool needNewFragmentUniformOffsets;
} VulkanCommandBuffer;
struct VulkanRenderer {
    VkDevice logicalDevice;
    VkInstance instance;
    PFN_vkVoidFunction (SDLCALL *vkGetDeviceProcAddr)(VkDevice, const char *);
    SDL_Mutex *submitLock;
    FPVulkanRetained *fpRetained;
};
static int device_queries, instance_queries;
static void SDLCALL Dummy(void) {}
static PFN_vkVoidFunction SDLCALL DeviceProc(VkDevice device, const char *name) { ++device_queries; return Dummy; }
static PFN_vkVoidFunction vkGetInstanceProcAddr(VkInstance instance, const char *name) { ++instance_queries; return Dummy; }
static void *PoisonAllocate(size_t size) { void *value = SDL_malloc(size); assert(value); SDL_memset(value, 0xcd, size); return value; }
#define SDL_malloc PoisonAllocate
@FUNCTIONS@
static VulkanCommandBuffer *MakeCommand(VulkanRenderer *renderer, SDL_GPUDevice *device, VulkanCommandPool *pool)
{
    VulkanCommandBuffer *commandBuffer;
@ALLOCATION@
    assert(commandBuffer->fpRetained == NULL && commandBuffer->fpAccelerationSet == VK_NULL_HANDLE);
    commandBuffer->renderer = renderer;
    commandBuffer->commandPool = pool;
    commandBuffer->commandBuffer = (void *)1;
    commandBuffer->common.device = device;
    commandBuffer->common.render_pass.command_buffer = (SDL_GPUCommandBuffer *)commandBuffer;
@RESET@
    return commandBuffer;
}
typedef struct Shared { int references, destroyed; } Shared;
typedef struct Payload { Shared *shared; int releases, submitted, completed, cancelled; } Payload;
static void SDLCALL Notify(void *value, Uint32 event)
{
    Payload *payload = value;
    if (event == FP_SDL_VULKAN_SUBMITTED) {
        assert(payload->submitted++ == 0 && payload->releases == 0);
        return;
    }
    if (event == FP_SDL_VULKAN_COMPLETED) {
        assert(payload->submitted == 1);
        ++payload->completed;
    } else {
        assert(event == FP_SDL_VULKAN_CANCELLED && payload->submitted == 0);
        ++payload->cancelled;
    }
    assert(payload->releases++ == 0);
    if (--payload->shared->references == 0) ++payload->shared->destroyed;
}
int main(void)
{
    VulkanRenderer renderer = {0};
    assert(sizeof(FP_SDLVulkanBridge) == 88);
    assert(FP_SDL_VULKAN_BRIDGE_VERSION == 2);
    SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), "FP_GPU_PIPELINE_STATS");
    assert(!FP_WantPipelineStats());
    const char *modes[] = {"0", "true", "01", ""};
    for (unsigned i = 0; i < SDL_arraysize(modes); ++i) {
        assert(SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "FP_GPU_PIPELINE_STATS", modes[i], true));
        assert(!FP_WantPipelineStats());
    }
    assert(SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "FP_GPU_PIPELINE_STATS", "1", true));
    assert(FP_WantPipelineStats());
    SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), "FP_GPU_PIPELINE_STATS");
    renderer.submitLock = SDL_CreateMutex();
    renderer.vkGetDeviceProcAddr = DeviceProc;
    assert(renderer.submitLock);
    SDL_GPUDevice device = { (SDL_GPURenderer *)&renderer };
    VulkanCommandPool pool = { SDL_GetCurrentThreadID() };
    VulkanCommandBuffer *a = MakeCommand(&renderer, &device, &pool);
    VulkanCommandBuffer *b = MakeCommand(&renderer, &device, &pool);
    Shared shared = {3, 0};
    Payload first = {&shared, 0}, second = {&shared, 0}, third = {&shared, 0};
    assert(FP_Command(&renderer, (SDL_GPUCommandBuffer *)a) == (void *)1);
    a->common.copy_pass.in_progress = true;
    assert(!FP_Command(&renderer, (SDL_GPUCommandBuffer *)a));
    a->common.copy_pass.in_progress = false;
    a->common.render_pass.in_progress = true;
    assert(!FP_Command(&renderer, (SDL_GPUCommandBuffer *)a));
    assert(FP_BindAcceleration(&renderer, (SDL_GPUCommandBuffer *)a, (SDL_GPURenderPass *)&a->common.render_pass, 17));
    assert(a->fpAccelerationSet == 17 && a->needNewFragmentUniformOffsets);
    assert(!FP_BindAcceleration(&renderer, (SDL_GPUCommandBuffer *)b, (SDL_GPURenderPass *)&a->common.render_pass, 17));
    assert(FP_Track(&renderer, (SDL_GPUCommandBuffer *)a, &first, Notify));
    RenderPass foreign = { (SDL_GPUCommandBuffer *)a, true };
    assert(!FP_BindAcceleration(&renderer, (SDL_GPUCommandBuffer *)a, (SDL_GPURenderPass *)&foreign, 17));
    a->common.render_pass.in_progress = false;
    assert(FP_Track(&renderer, (SDL_GPUCommandBuffer *)b, &second, Notify));
    assert(FP_Track(&renderer, (SDL_GPUCommandBuffer *)a, &third, Notify));
    FP_MarkSubmitted(a);
    FP_MarkSubmitted(a);
    assert(first.submitted == 1 && third.submitted == 1 && second.submitted == 0);
    assert(first.releases == 0 && third.releases == 0 && shared.references == 3);
    FP_ReleaseRetained(&renderer, a);
    assert(first.releases == 1 && third.releases == 1 && second.releases == 0);
    assert(first.completed == 1 && third.completed == 1 && first.cancelled == 0);
    assert(shared.references == 1 && shared.destroyed == 0 && !a->fpRetained);
    assert(renderer.fpRetained == b->fpRetained);
    FP_ReleaseRetained(&renderer, a);
    while (renderer.fpRetained) FP_ReleaseRetained(&renderer, (VulkanCommandBuffer *)renderer.fpRetained->command);
    assert(second.releases == 1 && shared.references == 0 && shared.destroyed == 1 && !b->fpRetained);
    assert(second.cancelled == 1 && second.completed == 0);
    a->common.submitted = true;
    assert(!FP_Command(&renderer, (SDL_GPUCommandBuffer *)a));
    assert(!FP_Track(&renderer, (SDL_GPUCommandBuffer *)a, &first, Notify));
    assert(!FP_Track(&renderer, NULL, &first, Notify));
    assert(FP_GetProc(&renderer, "vkGetPhysicalDeviceMemoryProperties") == Dummy);
    assert(device_queries == 0 && instance_queries == 1);
    assert(FP_GetProc(&renderer, "vkCmdBuildAccelerationStructuresKHR") == Dummy);
    assert(device_queries == 1 && instance_queries == 1);
    Uint8 uuid[16] = {1, 2, 3, 4};
    Uint8 valid[64] = {0}, corrupt[64];
    SDL_memcpy(valid, "FPVKPC01", 8);
    FP_CachePut(valid + 8, sizeof(valid) - 16);
    FP_CachePut(valid + 16, 32);
    FP_CachePut(valid + 20, 1);
    FP_CachePut(valid + 24, 0x12345678);
    FP_CachePut(valid + 28, 0x87654321);
    SDL_memcpy(valid + 32, uuid, 16);
    FP_CachePut(valid + 12, SDL_crc32(0, valid + 16, sizeof(valid) - 16));
    assert(FP_ValidCache(valid, sizeof(valid), 0x12345678, 0x87654321, uuid));
    assert(!FP_ValidCache(NULL, sizeof(valid), 0x12345678, 0x87654321, uuid));
    assert(!FP_ValidCache(valid, 47, 0x12345678, 0x87654321, uuid));
    assert(!FP_ValidCache(valid, sizeof(valid) - 1, 0x12345678, 0x87654321, uuid));
    assert(!FP_ValidCache(valid, (size_t)FP_CACHE_LIMIT + 17, 0x12345678, 0x87654321, uuid));
    assert(!FP_ValidCache(valid, sizeof(valid), 9, 0x87654321, uuid));
    assert(!FP_ValidCache(valid, sizeof(valid), 0x12345678, 9, uuid));
    Uint8 other[16] = {0};
    assert(!FP_ValidCache(valid, sizeof(valid), 0x12345678, 0x87654321, other));
    const int offsets[] = {0, 8, 12, 63};
    for (unsigned i = 0; i < SDL_arraysize(offsets); ++i) {
        SDL_memcpy(corrupt, valid, sizeof(valid));
        corrupt[offsets[i]] ^= 1;
        assert(!FP_ValidCache(corrupt, sizeof(corrupt), 0x12345678, 0x87654321, uuid));
    }
    for (unsigned offset = 16; offset <= 20; offset += 4) {
        SDL_memcpy(corrupt, valid, sizeof(valid));
        FP_CachePut(corrupt + offset, 2);
        FP_CachePut(corrupt + 12, SDL_crc32(0, corrupt + 16, sizeof(corrupt) - 16));
        assert(!FP_ValidCache(corrupt, sizeof(corrupt), 0x12345678, 0x87654321, uuid));
    }
    SDL_free(a);
    SDL_free(b);
    SDL_DestroyMutex(renderer.submitLock);
    puts("PASS allocation, command ownership, retention, procedure routing, cache corruption and optional statistics gate");
    return 0;
}
'''
    for name, value in (('RETAINED', retained_type), ('FUNCTIONS', functions), ('ALLOCATION', allocation), ('RESET', reset)):
        program = program.replace('@' + name + '@', value)
    (output / 'test.c').write_text(program, encoding='utf-8')
    cmake = f'''cmake_minimum_required(VERSION 3.16)
project(SDLBridgeRetention C)
add_executable(SDLBridgeRetention test.c)
target_include_directories(SDLBridgeRetention PRIVATE "{ROOT.as_posix()}/ThirdParty/SDL3/include" "{ROOT.as_posix()}/src/Engine/Render")
target_link_libraries(SDLBridgeRetention PRIVATE "{ROOT.as_posix()}/ThirdParty/SDL3/lib/x64/SDL3.lib")
'''
    (output / 'CMakeLists.txt').write_text(cmake, encoding='utf-8')
    build = output / 'build'
    with (output / 'build.log').open('w') as log:
        subprocess.run(['cmake', '-S', str(output), '-B', str(build), '-A', 'x64'], stdout=log, stderr=subprocess.STDOUT, check=True)
        subprocess.run(['cmake', '--build', str(build), '--config', 'Release'], stdout=log, stderr=subprocess.STDOUT, check=True)
    shutil.copyfile(ROOT / 'ThirdParty/SDL3/lib/x64/SDL3.dll', build / 'Release/SDL3.dll')
    subprocess.run([str(build / 'Release/SDLBridgeRetention.exe')], check=True)


if __name__ == '__main__':
    main()
