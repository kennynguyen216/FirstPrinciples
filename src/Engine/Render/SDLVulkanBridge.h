#pragma once

#include <SDL3/SDL_gpu.h>

#define FP_SDL_VULKAN_BRIDGE_VERSION 2u
#define FP_SDL_VULKAN_BRIDGE_PROPERTY "FP.gpu.vulkan.bridge"
#define FP_SDL_RAY_QUERY_PROPERTY "FP.gpu.ray_query"

#define FP_SDL_VULKAN_SUBMITTED 1u
#define FP_SDL_VULKAN_COMPLETED 2u
#define FP_SDL_VULKAN_CANCELLED 3u

typedef void (SDLCALL *FP_SDLVulkanNotify)(void *payload, Uint32 event);

typedef struct FP_SDLVulkanBridge
{
    Uint32 version;
    Uint32 size;
    void *userdata;
    void *instance;
    void *physical_device;
    void *device;
    Uint64 acceleration_layout;
    SDL_FunctionPointer (SDLCALL *get_proc)(void *userdata, const char *name);
    void *(SDLCALL *command)(void *userdata, SDL_GPUCommandBuffer *command_buffer);
    bool (SDLCALL *bind)(void *userdata, SDL_GPUCommandBuffer *command_buffer, SDL_GPURenderPass *render_pass, Uint64 descriptor_set);
    bool (SDLCALL *track)(void *userdata, SDL_GPUCommandBuffer *command_buffer, void *payload, FP_SDLVulkanNotify notify);
    Uint32 timestamp_valid_bits;
} FP_SDLVulkanBridge;
