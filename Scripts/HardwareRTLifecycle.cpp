#include "Engine/Render/GPUDevice.h"
#include "Engine/Render/DIA.h"
#include "Engine/Render/DIARayTracing.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
using namespace FP;
namespace
{
void Require(bool value, const char* message, const DIARayTracing* rays = nullptr)
{
    if (!value) throw std::runtime_error(std::string(message) + ": " + (rays ? rays->GetError().CStr() : SDL_GetError()));
}
SDL_GPUCommandBuffer* Acquire(GPUDevice& device)
{
    auto* cmd = SDL_AcquireGPUCommandBuffer(device.GetHandle());
    Require(cmd != nullptr, "Acquire command");
    return cmd;
}
void Drain(GPUDevice& device)
{
    Require(SDL_WaitForGPUIdle(device.GetHandle()), "GPU idle");
}
void Submit(GPUDevice& device, SDL_GPUCommandBuffer* cmd)
{
    auto* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    Require(fence != nullptr, "Submit fence");
    Require(SDL_WaitForGPUFences(device.GetHandle(), true, &fence, 1), "Wait fence");
    SDL_ReleaseGPUFence(device.GetHandle(), fence);
    Drain(device);
}
SDL_GPURenderPass* Pass(SDL_GPUCommandBuffer* cmd, GPUTexture& color)
{
    SDL_GPUColorTargetInfo info = {};
    info.texture = color.GetHandle();
    info.clear_color = { 0, 0, 0, 1 };
    info.load_op = SDL_GPU_LOADOP_CLEAR;
    info.store_op = SDL_GPU_STOREOP_STORE;
    auto* pass = SDL_BeginGPURenderPass(cmd, &info, 1, nullptr);
    Require(pass != nullptr, "Begin pass");
    return pass;
}
int Run()
{
    std::unique_ptr<GPUDevice> device(GPUDevice::Create());
    Require(device != nullptr, "GPU device");
    auto color = device->CreateRenderTarget(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, 32, 32, false);
    Require(bool(color), "Color target");
    DIARayTracing rays(*device);
    Require(rays.IsSupported(), "Hardware context", &rays);
    Array<RayTriangle> triangles;
    RayTriangle triangle;
    triangle.Position[0] = { -1, -1, 0, 1 };
    triangle.Position[1] = { 1, -1, 0, 1 };
    triangle.Position[2] = { 0, 1, 0, 1 };
    triangles.Add(triangle);
    Array<DIAInstance> instances;
    DIAInstance instance;
    instance.Metadata[1] = 1;
    instance.Identity[1] = 1;
    instances.Add(instance);
    Array<DIAMaterial> materials;
    materials.Add(DIAMaterial{});
    constexpr usize budget = 64ull * 1024 * 1024;
    auto update = [&](SDL_GPUCommandBuffer* cmd, uint64 revision) {
        Require(rays.Update(cmd, triangles, instances, materials, revision, budget), "Update AS", &rays);
    };
    auto bind = [&](SDL_GPUCommandBuffer* cmd) {
        auto* pass = Pass(cmd, *color);
        Require(rays.Bind(cmd, pass), "Bind AS on active pass", &rays);
        SDL_EndGPURenderPass(pass);
    };
    Drain(*device);

    auto* cancelled = Acquire(*device);
    update(cancelled, 1);
    Require(rays.GetBuildCount() == 1, "First build count");
    Require(SDL_CancelGPUCommandBuffer(cancelled), "Cancel built command");
    auto* rebuilt = Acquire(*device);
    update(rebuilt, 1);
    Require(rays.GetBuildCount() == 2, "Cancelled build must rebuild");
    bind(rebuilt);
    Submit(*device, rebuilt);
    std::cout << "PASS cancel then rebuild\n";

    instances[0].Transform.At(3, 0) = 1;
    instances[0].Identity[1] = 10;
    auto* unsubmitted = Acquire(*device);
    update(unsubmitted, 2);
    Require(rays.GetBuildCount() == 3, "Unsubmitted build count");
    auto* other = Acquire(*device);
    auto* otherPass = Pass(other, *color);
    Require(!rays.Bind(other, otherPass), "Unsubmitted cross-command bind must reject");
    SDL_EndGPURenderPass(otherPass);
    Require(SDL_CancelGPUCommandBuffer(other), "Cancel unrelated command");
    auto* independent = Acquire(*device);
    update(independent, 2);
    Require(rays.GetBuildCount() == 4, "Unsubmitted cross-command update must rebuild");
    Require(SDL_CancelGPUCommandBuffer(unsubmitted), "Cancel first unsubmitted scene");
    bind(independent);
    Submit(*device, independent);
    std::cout << "PASS unsubmitted cross-command isolation\n";

    auto* reused = Acquire(*device);
    update(reused, 2);
    Require(rays.GetBuildCount() == 4, "Submitted scene should reuse");
    bind(reused);
    Require(SDL_CancelGPUCommandBuffer(reused), "Cancel reused binding");
    auto* stable = Acquire(*device);
    update(stable, 2);
    Require(rays.GetBuildCount() == 4, "Cancelling reuse must not poison valid AS");
    Require(rays.BeginTrace(stable, 17), "Begin timestamp", &rays);
    bind(stable);
    Require(rays.EndTrace(stable), "End timestamp", &rays);
    Submit(*device, stable);
    std::cout << "PASS submitted reuse and cancelled reuse\n";

    auto* aborted = Acquire(*device);
    Require(rays.BeginTrace(aborted, 18), "Begin aborted timestamp", &rays);
    rays.AbortTrace();
    Require(SDL_CancelGPUCommandBuffer(aborted), "Cancel aborted timestamp");
    auto* measured = Acquire(*device);
    Require(rays.BeginTrace(measured, 19), "Begin after abort", &rays);
    bind(measured);
    Require(rays.EndTrace(measured), "End after abort", &rays);
    Submit(*device, measured);
    auto timings = rays.GetTraceTimings();
    Require(timings.Num() == 2 && timings[0].Frame == 17 && timings[1].Frame == 19, "Only completed intervals may be reported");
    std::cout << "PASS timestamp completion and abort recovery\n";

    instances[0].Transform.At(3, 0) = 2;
    auto* earlier = Acquire(*device);
    update(earlier, 3);
    bind(earlier);
    instances[0].Transform.At(3, 0) = 3;
    auto* later = Acquire(*device);
    update(later, 4);
    Require(rays.GetBuildCount() == 6, "Independent changed scenes");
    Submit(*device, later);
    const usize retained = rays.GetMemoryBytes();
    Submit(*device, earlier);
    Require(rays.GetMemoryBytes() < retained, "Earlier bound scene must release after its fence");
    auto* after = Acquire(*device);
    update(after, 4);
    Require(rays.GetBuildCount() == 6, "Latest submitted scene should remain cached");
    bind(after);
    Submit(*device, after);
    std::cout << "PASS bind lifetime and reverse independent submission\n";

    auto* wrongCommand = Acquire(*device);
    auto* passCommand = Acquire(*device);
    auto* wrongPass = Pass(passCommand, *color);
    Require(!rays.Bind(wrongCommand, wrongPass), "Mismatched command/pass must reject");
    SDL_EndGPURenderPass(wrongPass);
    Require(SDL_CancelGPUCommandBuffer(wrongCommand), "Cancel wrong command");
    Require(SDL_CancelGPUCommandBuffer(passCommand), "Cancel pass command");
    auto* final = Acquire(*device);
    update(final, 4);
    Require(rays.GetBuildCount() == 6, "Bad binding must not poison AS");
    bind(final);
    Submit(*device, final);
    std::cout << "PASS mismatched binding\n";

    Array<RayTriangle> emptyTriangles;
    Array<DIAInstance> emptyInstances;
    auto* empty = Acquire(*device);
    Require(rays.Update(empty, emptyTriangles, emptyInstances, materials, 5, budget), "Build empty scene", &rays);
    Require(rays.GetBuildCount() == 7, "Empty TLAS generation");
    bind(empty);
    Submit(*device, empty);
    auto* restored = Acquire(*device);
    update(restored, 6);
    Require(rays.GetBuildCount() == 8, "Recover normal geometry after empty scene");
    bind(restored);
    Submit(*device, restored);
    std::cout << "PASS empty scene and normal recovery\n";

    for (int v = 0; v < 3; ++v) triangles[0].Position[v] = { 1, 1, 1, 1 };
    instances[0].Identity[1] = 2;
    auto* point = Acquire(*device);
    update(point, 7);
    Require(rays.GetBuildCount() == 9, "Point geometry generation");
    bind(point);
    Submit(*device, point);
    for (int v = 0; v < 3; ++v) triangles[0].Position[v] = { 0, static_cast<float>(v), 0, 1 };
    instances[0].Identity[1] = 3;
    auto* line = Acquire(*device);
    update(line, 8);
    Require(rays.GetBuildCount() == 10, "Line geometry generation");
    bind(line);
    Submit(*device, line);
    std::cout << "PASS point and line padded bounds\n";

    const usize beforeInvalid = rays.GetMemoryBytes();
    triangles[0].Position[0].X = std::numeric_limits<float>::quiet_NaN();
    instances[0].Identity[1] = 4;
    auto* invalid = Acquire(*device);
    Require(!rays.Update(invalid, triangles, instances, materials, 9, budget), "Non-finite geometry must reject");
    Require(rays.GetBuildCount() == 10 && rays.GetMemoryBytes() == beforeInvalid, "Invalid geometry must leave no recorded build or leaked allocation");
    Require(SDL_CancelGPUCommandBuffer(invalid), "Cancel rejected geometry command");
    triangles[0] = triangle;
    instances[0].Identity[1] = 5;
    auto* recovered = Acquire(*device);
    update(recovered, 10);
    Require(rays.GetBuildCount() == 11, "Recovery after rejected geometry");
    bind(recovered);
    Submit(*device, recovered);
    std::cout << "PASS non-finite rejection and recovery\n";
    std::cout << "PASS lifecycle, builds=" << rays.GetBuildCount() << " resident=" << rays.GetMemoryBytes() << '\n';
    return 0;
}
}
int main(int, char**)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) { std::cerr << SDL_GetError() << '\n'; return 1; }
    int result = 1;
    try { result = Run(); }
    catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; }
    SDL_Quit();
    return result;
}
