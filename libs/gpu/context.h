#pragma once
#include "view.h"

// One WebGPU device and surface, and the per-frame steps both presenters share.
namespace gpu {

struct Context
{
    wgpu::Instance instance;
    wgpu::Adapter adapter;
    wgpu::Device device;
    wgpu::Surface surface;
    wgpu::TextureFormat format = wgpu::TextureFormat::Undefined;
    uint32_t width = 0, height = 0; // pixels

    // Natively this blocks until the device exists; it returns false without one.
    bool createDevice();
    // Picks a format for the surface and configures it at width x height pixels.
    void configure(uint32_t width, uint32_t height);
    // Clears, lets the view draw, and presents. Reconfigures a lost surface.
    void frame(View &view, float scale);
};

}
