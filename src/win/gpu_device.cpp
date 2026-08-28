// SPDX-License-Identifier: GPL-3.0-or-later
#include "gpu_device.hpp"

#include "log.hpp"
#include "dragonperch/text.hpp"

#include <cstddef>
#include <string_view>
#include <vector>

namespace dp::win {

GpuDevice GpuDevice::create()
{
    GpuDevice device;

    // BGRA support is required for Direct2D interop. Without the flag it is the D2D device
    // creation below that fails, not this call, which is a confusing place to find out.
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifndef NDEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    constexpr D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };

    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels,
                                   ARRAYSIZE(levels), D3D11_SDK_VERSION, &device.d3d_device_,
                                   nullptr, nullptr);

#ifndef NDEBUG
    if (hr == DXGI_ERROR_SDK_COMPONENT_MISSING || hr == DXGI_ERROR_UNSUPPORTED) {
        // The graphics tools optional feature is not installed. Not worth failing over.
        flags &= ~static_cast<UINT>(D3D11_CREATE_DEVICE_DEBUG);
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels,
                               ARRAYSIZE(levels), D3D11_SDK_VERSION, &device.d3d_device_, nullptr,
                               nullptr);
    }
#endif
    check(hr, "D3D11CreateDevice");
    check(device.d3d_device_.As(&device.dxgi_device_), "ID3D11Device as IDXGIDevice");

#ifndef NDEBUG
    // The debug layer breaks into the debugger on error by default. With no debugger
    // attached that kills the process with STATUS_BREAKPOINT, which surfaces as a bare exit
    // code and no explanation. Stop it breaking and drain its messages into the log instead.
    if (ComPtr<ID3D11InfoQueue> info; SUCCEEDED(device.d3d_device_.As(&info))) {
        info->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, FALSE);
        info->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, FALSE);
        info->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_WARNING, FALSE);
        device.info_queue_ = info;
    }
#endif

    D2D1_FACTORY_OPTIONS options{};
#ifndef NDEBUG
    // Only when a debugger is actually attached. The Direct2D debug layer breaks into the
    // debugger on its own messages, and with nobody there to catch the break the process
    // dies with STATUS_BREAKPOINT.
    if (IsDebuggerPresent() != FALSE) {
        options.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
    }
#endif

    ComPtr<ID2D1Factory3> factory;
    check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory3), &options,
                            &factory),
          "D2D1CreateFactory");

    check(factory->CreateDevice(device.dxgi_device_.Get(), &device.d2d_device_),
          "ID2D1Factory3::CreateDevice");
    check(device.d2d_device_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
                                                  &device.d2d_context_),
          "ID2D1Device::CreateDeviceContext");

    // Ask for IDCompositionDesktopDevice directly, since binding to an HWND is the only
    // reason this exists. Requesting IDCompositionDevice3 and querying for the desktop
    // interface looks equivalent and is not: DCompositionCreateDevice3 returns E_NOINTERFACE
    // for IDCompositionDevice3 here, on Windows 11 with the 10.0.28000 SDK.
    HRESULT dcomp =
        DCompositionCreateDevice3(device.dxgi_device_.Get(), IID_PPV_ARGS(&device.dcomp_device_));
    if (FAILED(dcomp)) {
        dcomp = DCompositionCreateDevice2(device.dxgi_device_.Get(),
                                          IID_PPV_ARGS(&device.dcomp_device_));
    }
    check(dcomp, "DCompositionCreateDevice3/2 for IDCompositionDesktopDevice");

    return device;
}

ComPtr<ID2D1Bitmap1> GpuDevice::create_bitmap(std::span<const std::byte> premultiplied_bgra,
                                              PixelSize size) const
{
    const D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_NONE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0F,
        96.0F);

    const D2D1_SIZE_U pixel_size = D2D1::SizeU(static_cast<UINT32>(size.width),
                                               static_cast<UINT32>(size.height));

    ComPtr<ID2D1Bitmap1> bitmap;
    check(d2d_context_->CreateBitmap(pixel_size, premultiplied_bgra.data(),
                                     static_cast<UINT32>(size.width) * 4U, &props, &bitmap),
          "ID2D1DeviceContext::CreateBitmap");
    return bitmap;
}

std::wstring GpuDevice::adapter_description() const
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

void GpuDevice::drain_debug_messages() const
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

        // DescriptionByteLength counts the terminator, so taking it whole puts a stray
        // NUL in the middle of the log.
        const std::size_t text_length = message->DescriptionByteLength > 0
                                            ? message->DescriptionByteLength - 1
                                            : 0;
        log_line(cat("d3d[", static_cast<int>(message->Severity), "]: ",
                     std::string_view(message->pDescription, text_length)));
    }

    info_queue_->ClearStoredMessages();
}

} // namespace dp::win
