// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "dragonperch/geometry.hpp"
#include "win_headers.hpp"

#include <functional>
#include <string>

namespace dp::win {

/// Owns the GPU side: a D3D11 device, a DirectComposition device and target bound to an
/// HWND, and a Direct2D context that draws into the composition surface.
///
/// DirectComposition rather than `Microsoft.UI.Composition`. The Windows App SDK hosting
/// path is `DesktopChildSiteBridge` + `ContentIsland`, which creates a child HWND that
/// swallows mouse input across everything it covers — with a monitor-sized overlay, the
/// whole desktop. Subclassing that child to answer `HTTRANSPARENT` fixed hit testing and
/// changed nothing about the clicks; disabling it did not help either. Content islands are
/// an input-and-output island and we want output only.
///
/// DirectComposition creates no window of its own, so the problem cannot arise. It is also
/// the lower layer: `Windows.UI.Composition` is a WinRT API over the same compositor engine
/// that adds animations, effects and a `DispatcherQueue` requirement, none of which this
/// needs. Not needing a `DispatcherQueue` is worth something on its own — forgetting to
/// pump one is a failure mode with no error message and no pixels.
class DcompRenderer {
public:
    /// Binds to `hwnd` and prepares a composition surface of `size`.
    static DcompRenderer create(HWND hwnd, PixelSize size);

    DcompRenderer(const DcompRenderer&) = delete;
    DcompRenderer& operator=(const DcompRenderer&) = delete;
    DcompRenderer(DcompRenderer&&) noexcept = default;
    DcompRenderer& operator=(DcompRenderer&&) noexcept = default;
    ~DcompRenderer() = default;

    [[nodiscard]] PixelSize size() const noexcept { return size_; }

    /// Opens a drawing session over `dirty`, hands the Direct2D context to `draw`, then
    /// closes and commits.
    ///
    /// `IDCompositionSurface::BeginDraw` takes the dirty rectangle, so damage tracking is
    /// native here: only that region is uploaded, nothing is read back, and the atlas stays
    /// a GPU texture. That is the difference from the `UpdateLayeredWindow` approach, which
    /// has to round-trip every frame through a CPU bitmap.
    void draw_frame(const PixelRect& dirty,
                    const std::function<void(ID2D1DeviceContext*)>& draw);

    /// Describes the device that was actually created, for the diagnostic output.
    [[nodiscard]] std::wstring adapter_description() const;

    /// Writes anything the D3D debug layer has to say into the log, and clears it.
    /// No-op in release builds, where the layer is not present.
    void drain_debug_messages() const;

private:
    DcompRenderer() = default;

    PixelSize size_{};
    ComPtr<ID3D11Device> d3d_device_;
    ComPtr<IDXGIDevice> dxgi_device_;
    ComPtr<ID2D1Device> d2d_device_;
    ComPtr<ID2D1DeviceContext> d2d_context_;
    ComPtr<IDCompositionDesktopDevice> dcomp_device_;
    ComPtr<IDCompositionTarget> target_;
    ComPtr<IDCompositionVisual2> visual_;
    ComPtr<IDCompositionSurface> surface_;
    ComPtr<ID3D11InfoQueue> info_queue_;
};

} // namespace dp::win
