// SPDX-License-Identifier: GPL-3.0-or-later
#include "dcomp_renderer.hpp"

#include "log.hpp"

#include <format>
#include <vector>

namespace dp::win {

DcompRenderer DcompRenderer::create(HWND hwnd, PixelSize size)
{
    DcompRenderer r;
    r.size_ = size;

    // BGRA support is required for Direct2D interop; without the flag the D2D device
    // creation below fails rather than the D3D one, which is a confusing place to find out.
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifndef NDEBUG
    // Only if the graphics tools optional feature is installed; fall back below if not.
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    constexpr D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };

    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, ARRAYSIZE(levels),
        D3D11_SDK_VERSION, &r.d3d_device_, nullptr, nullptr);

#ifndef NDEBUG
    if (hr == DXGI_ERROR_SDK_COMPONENT_MISSING || hr == DXGI_ERROR_UNSUPPORTED) {
        flags &= ~static_cast<UINT>(D3D11_CREATE_DEVICE_DEBUG);
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels,
                               ARRAYSIZE(levels), D3D11_SDK_VERSION, &r.d3d_device_, nullptr,
                               nullptr);
    }
#endif
    check(hr, "D3D11CreateDevice");

    check(r.d3d_device_.As(&r.dxgi_device_), "ID3D11Device as IDXGIDevice");

#ifndef NDEBUG
    // The debug layer breaks into the debugger on error by default. With no debugger
    // attached that kills the process with STATUS_BREAKPOINT, which surfaces as a bare exit
    // code and no explanation -- the Debug build was exiting 3 while Release exited 0 and
    // nothing said why. Stop it breaking and drain its messages into the log instead, so a
    // validation complaint reads as text rather than as a dead process.
    if (ComPtr<ID3D11InfoQueue> info; SUCCEEDED(r.d3d_device_.As(&info))) {
        info->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, FALSE);
        info->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, FALSE);
        info->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_WARNING, FALSE);
        r.info_queue_ = info;
    }
#endif

    // --- Direct2D on the same device -----------------------------------------
    D2D1_FACTORY_OPTIONS options{};
#ifndef NDEBUG
    // Only when a debugger is actually attached. The Direct2D debug layer breaks into the
    // debugger on its messages, and with nobody there to catch the break the process dies
    // with STATUS_BREAKPOINT -- which is why the Debug build was exiting 3 while Release
    // exited 0, with an empty D3D info queue and no other clue. Under Visual Studio the
    // layer still reports normally.
    if (IsDebuggerPresent() != FALSE) {
        options.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
    }
#endif
    ComPtr<ID2D1Factory3> d2d_factory;
    check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory3),
                            &options, &d2d_factory),
          "D2D1CreateFactory");

    check(d2d_factory->CreateDevice(r.dxgi_device_.Get(), &r.d2d_device_),
          "ID2D1Factory3::CreateDevice");
    check(r.d2d_device_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &r.d2d_context_),
          "ID2D1Device::CreateDeviceContext");

    // --- DirectComposition ---------------------------------------------------
    // Ask for IDCompositionDesktopDevice directly, since binding to an HWND is the only
    // reason this exists. Requesting IDCompositionDevice3 and then querying for the desktop
    // interface looks equivalent and is not: DCompositionCreateDevice3 returns E_NOINTERFACE
    // for IDCompositionDevice3 here, on Windows 11 with the 10.0.28000 SDK.
    HRESULT dcomp = DCompositionCreateDevice3(r.dxgi_device_.Get(),
                                              IID_PPV_ARGS(&r.dcomp_device_));
    if (FAILED(dcomp)) {
        // Device2 predates Device3 and supports the same desktop interface. Keeping the
        // fallback costs three lines and covers older Windows 10 builds.
        dcomp = DCompositionCreateDevice2(r.dxgi_device_.Get(), IID_PPV_ARGS(&r.dcomp_device_));
    }
    check(dcomp, "DCompositionCreateDevice3/2 for IDCompositionDesktopDevice");

    check(r.dcomp_device_->CreateTargetForHwnd(hwnd, TRUE, &r.target_),
          "CreateTargetForHwnd");
    check(r.dcomp_device_->CreateVisual(&r.visual_), "CreateVisual");

    check(r.dcomp_device_->CreateSurface(
              static_cast<UINT>(size.width), static_cast<UINT>(size.height),
              DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED, &r.surface_),
          "CreateSurface");

    check(r.visual_->SetContent(r.surface_.Get()), "IDCompositionVisual2::SetContent");
    check(r.target_->SetRoot(r.visual_.Get()), "IDCompositionTarget::SetRoot");

    return r;
}

void DcompRenderer::draw_frame(const PixelRect& dirty,
                               const std::function<void(ID2D1DeviceContext*)>& draw)
{
    const PixelRect clipped = dirty.intersect(PixelRect{0, 0, size_.width, size_.height});
    if (clipped.empty()) {
        return;
    }

    RECT update{clipped.left(), clipped.top(), clipped.right(), clipped.bottom()};

    ComPtr<IDXGISurface> dxgi_surface;
    POINT offset{};
    check(surface_->BeginDraw(&update, IID_PPV_ARGS(&dxgi_surface), &offset),
          "IDCompositionSurface::BeginDraw");

    const D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0F, 96.0F);

    ComPtr<ID2D1Bitmap1> bitmap;
    check(d2d_context_->CreateBitmapFromDxgiSurface(dxgi_surface.Get(), &props, &bitmap),
          "CreateBitmapFromDxgiSurface");

    d2d_context_->SetTarget(bitmap.Get());
    d2d_context_->BeginDraw();

    // BeginDraw hands back an offset into a surface the compositor may have placed
    // anywhere in an atlas of its own. Drawing in surface coordinates without applying it
    // puts everything in the wrong place, usually off-surface entirely.
    d2d_context_->SetTransform(D2D1::Matrix3x2F::Translation(
        static_cast<float>(offset.x - clipped.left()),
        static_cast<float>(offset.y - clipped.top())));

    d2d_context_->PushAxisAlignedClip(
        D2D1::RectF(static_cast<float>(clipped.left()), static_cast<float>(clipped.top()),
                    static_cast<float>(clipped.right()), static_cast<float>(clipped.bottom())),
        D2D1_ANTIALIAS_MODE_ALIASED);

    d2d_context_->Clear(D2D1::ColorF(0.0F, 0.0F, 0.0F, 0.0F));
    draw(d2d_context_.Get());

    d2d_context_->PopAxisAlignedClip();

    const HRESULT end = d2d_context_->EndDraw();
    d2d_context_->SetTarget(nullptr);
    check(end, "ID2D1DeviceContext::EndDraw");

    check(surface_->EndDraw(), "IDCompositionSurface::EndDraw");
    check(dcomp_device_->Commit(), "IDCompositionDesktopDevice::Commit");
}

void DcompRenderer::drain_debug_messages() const
{
    if (!info_queue_) {
        return;
    }

    const UINT64 count = info_queue_->GetNumStoredMessages();
    for (UINT64 i = 0; i < count; ++i) {
        SIZE_T length = 0;
        if (FAILED(info_queue_->GetMessage(i, nullptr, &length))) {
            continue;
        }

        std::vector<std::byte> storage(length);
        auto* message = reinterpret_cast<D3D11_MESSAGE*>(storage.data());
        if (FAILED(info_queue_->GetMessage(i, message, &length))) {
            continue;
        }

        log_line(std::format("d3d[{}]: {}", static_cast<int>(message->Severity),
                             std::string_view(message->pDescription, message->DescriptionByteLength)));
    }

    info_queue_->ClearStoredMessages();
}

std::wstring DcompRenderer::adapter_description() const
{
    ComPtr<IDXGIAdapter> adapter;
    if (FAILED(dxgi_device_->GetAdapter(&adapter))) {
        return L"(unknown adapter)";
    }

    DXGI_ADAPTER_DESC desc{};
    if (FAILED(adapter->GetDesc(&desc))) {
        return L"(unknown adapter)";
    }

    return desc.Description;
}

} // namespace dp::win
