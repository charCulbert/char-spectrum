#include "context.h"
#include <cstdio>
#if defined(_WIN32)
#define NOMINMAX // keep std::min usable
#include <windows.h>
#include <dawn/native/DawnNative.h>
#include <string>
#endif

namespace gpu {

#if !defined(__EMSCRIPTEN__)
bool Context::createDevice()
{
    static const wgpu::InstanceFeatureName features[] = {wgpu::InstanceFeatureName::TimedWaitAny};
    wgpu::InstanceDescriptor instanceDescriptor;
    instanceDescriptor.requiredFeatureCount = 1;
    instanceDescriptor.requiredFeatures = features;
#if defined(_WIN32)
    // Dawn's D3D12 backend loads d3dcompiler_47.dll from its search paths, which
    // hold only our plugin's folder by default. Add System32, where Windows has it.
    char system[MAX_PATH] = {};
    const std::string systemPath = std::string(system, GetSystemDirectoryA(system, MAX_PATH)) + "\\";
    std::string_view searchPaths[] = {systemPath};
    dawn::native::DawnInstanceDescriptor dawnDescriptor;
    dawnDescriptor.additionalRuntimeSearchPaths = searchPaths;
    instanceDescriptor.nextInChain = &dawnDescriptor;
#endif
    instance = wgpu::CreateInstance(&instanceDescriptor);
    if (!instance) return false;

    // Real GPUs first; the fallback (software) adapter covers machines and CI
    // runners without one.
    for (bool fallback : {false, true})
    {
        wgpu::RequestAdapterOptions options;
        options.compatibleSurface = surface;
        options.forceFallbackAdapter = fallback;
        instance.WaitAny(instance.RequestAdapter(&options, wgpu::CallbackMode::WaitAnyOnly,
            [](wgpu::RequestAdapterStatus status, wgpu::Adapter result, wgpu::StringView, Context *self) {
                if (status == wgpu::RequestAdapterStatus::Success) self->adapter = std::move(result);
            }, this), UINT64_MAX);
        if (adapter) break;
    }
    if (!adapter) return false;

    wgpu::DeviceDescriptor deviceDescriptor;
#if defined(_WIN32)
    // Dawn prefers the DXC shader compiler, but Windows does not ship its
    // dxcompiler.dll and without it the device fails. FXC, d3dcompiler_47.dll
    // above, needs nothing bundled.
    const char *disabledToggles[] = {"use_dxc"};
    wgpu::DawnTogglesDescriptor toggles;
    toggles.disabledToggles = disabledToggles;
    toggles.disabledToggleCount = 1;
    deviceDescriptor.nextInChain = &toggles;
#endif
    deviceDescriptor.SetUncapturedErrorCallback([](const wgpu::Device &, wgpu::ErrorType, wgpu::StringView message) {
        std::fprintf(stderr, "WebGPU error: %.*s\n", int(message.length), message.data);
    });
    instance.WaitAny(adapter.RequestDevice(&deviceDescriptor, wgpu::CallbackMode::WaitAnyOnly,
        [](wgpu::RequestDeviceStatus status, wgpu::Device result, wgpu::StringView message, Context *self) {
            if (status == wgpu::RequestDeviceStatus::Success) self->device = std::move(result);
            else std::fprintf(stderr, "WebGPU device failed: %.*s\n", int(message.length), message.data);
        }, this), UINT64_MAX);
    if (device)
    {
        wgpu::AdapterInfo info;
        adapter.GetInfo(&info);
        std::fprintf(stderr, "WebGPU device: %.*s (backend %d)\n", int(info.device.length), info.device.data,
                     int(info.backendType));
    }
    return device != nullptr;
}
#endif

void Context::configure(uint32_t newWidth, uint32_t newHeight)
{
    width = newWidth > 0 ? newWidth : 1;
    height = newHeight > 0 ? newHeight : 1;
    if (!surface || !device) return;
    if (format == wgpu::TextureFormat::Undefined)
    {
        wgpu::SurfaceCapabilities capabilities;
        surface.GetCapabilities(adapter, &capabilities);
        format = capabilities.formatCount ? capabilities.formats[0] : wgpu::TextureFormat::BGRA8Unorm;
    }
    wgpu::SurfaceConfiguration config;
    config.device = device;
    config.format = format;
    config.width = width;
    config.height = height;
    config.presentMode = wgpu::PresentMode::Fifo;
    config.alphaMode = wgpu::CompositeAlphaMode::Auto;
    surface.Configure(&config);
}

void Context::frame(View &view, float scale)
{
    if (!surface || !device) return;
    wgpu::SurfaceTexture current;
    surface.GetCurrentTexture(&current);
    if (current.status == wgpu::SurfaceGetCurrentTextureStatus::Outdated ||
        current.status == wgpu::SurfaceGetCurrentTextureStatus::Lost)
    {
        configure(width, height);
        return;
    }
    if (current.status != wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal &&
        current.status != wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal)
        return;

    wgpu::RenderPassColorAttachment attachment;
    attachment.view = current.texture.CreateView();
    attachment.loadOp = wgpu::LoadOp::Clear;
    attachment.storeOp = wgpu::StoreOp::Store;
    attachment.clearValue = {0.07, 0.08, 0.09, 1.0};
    wgpu::RenderPassDescriptor passDescriptor;
    passDescriptor.colorAttachmentCount = 1;
    passDescriptor.colorAttachments = &attachment;

    wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
    wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&passDescriptor);
    view.draw(pass, float(width), float(height), scale);
    pass.End();
    wgpu::CommandBuffer commands = encoder.Finish();
    device.GetQueue().Submit(1, &commands);
#if !defined(__EMSCRIPTEN__)
    surface.Present(); // the browser presents a canvas by itself
    instance.ProcessEvents();
#endif
}

}
