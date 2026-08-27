// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "dragonperch/geometry.hpp"
#include "win_headers.hpp"

#include <cstddef>
#include <span>
#include <string>

namespace dp::win {

/// The GPU objects that are shared across every overlay: one D3D11 device, one Direct2D
/// device context, one DirectComposition desktop device.
///
/// Shared rather than per-monitor because a Direct2D bitmap belongs to the device that
/// created it. With a device per overlay, a two-monitor setup would need the sprite atlas
/// uploaded twice and drawn from whichever copy matched -- an easy thing to get subtly
/// wrong, for no gain.
///
/// DirectComposition rather than Microsoft.UI.Composition: the Windows App SDK hosting
/// path creates a child HWND that swallows mouse input across everything it covers. See
/// docs/plan.md.
class GpuDevice {
public:
    static GpuDevice create();

    GpuDevice(const GpuDevice&) = delete;
    GpuDevice& operator=(const GpuDevice&) = delete;
    GpuDevice(GpuDevice&&) noexcept = default;
    GpuDevice& operator=(GpuDevice&&) noexcept = default;
    ~GpuDevice() = default;

    [[nodiscard]] ID2D1DeviceContext* d2d() const noexcept { return d2d_context_.Get(); }
    [[nodiscard]] IDCompositionDesktopDevice* dcomp() const noexcept { return dcomp_device_.Get(); }

    /// Uploads premultiplied BGRA, top-down, stride = `size.width * 4`.
    [[nodiscard]] ComPtr<ID2D1Bitmap1> create_bitmap(std::span<const std::byte> premultiplied_bgra,
                                                     PixelSize size) const;

    [[nodiscard]] std::wstring adapter_description() const;

    /// Writes anything the D3D debug layer has to say into the log, and clears it. No-op in
    /// release builds, where the layer is not present.
    void drain_debug_messages() const;

private:
    GpuDevice() = default;

    ComPtr<ID3D11Device> d3d_device_;
    ComPtr<IDXGIDevice> dxgi_device_;
    ComPtr<ID2D1Device> d2d_device_;
    ComPtr<ID2D1DeviceContext> d2d_context_;
    ComPtr<IDCompositionDesktopDevice> dcomp_device_;
    ComPtr<ID3D11InfoQueue> info_queue_;
};

} // namespace dp::win
