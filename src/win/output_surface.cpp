// SPDX-License-Identifier: GPL-3.0-or-later
#include "output_surface.hpp"

#include "dragonperch/text.hpp"
#include "log.hpp"

#include <utility>

namespace dp::win {

OutputSurface::OutputSurface(GpuDevice& device, OverlayWindow window,
                             ComPtr<IDCompositionTarget> target,
                             ComPtr<IDCompositionVisual2> visual,
                             ComPtr<IDCompositionSurface> surface)
    : device_(&device)
    , window_(std::move(window))
    , target_(std::move(target))
    , visual_(std::move(visual))
    , surface_(std::move(surface))
{
}

OutputSurface OutputSurface::create(GpuDevice& device, const PixelRect& bounds)
{
    OverlayWindow window = OverlayWindow::create(bounds);

    ComPtr<IDCompositionTarget> target;
    check(device.dcomp()->CreateTargetForHwnd(window.handle(), TRUE, &target),
          "CreateTargetForHwnd");

    ComPtr<IDCompositionVisual2> visual;
    check(device.dcomp()->CreateVisual(&visual), "CreateVisual");

    ComPtr<IDCompositionSurface> surface;
    check(device.dcomp()->CreateSurface(static_cast<UINT>(bounds.width),
                                        static_cast<UINT>(bounds.height),
                                        DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED,
                                        &surface),
          "CreateSurface");

    check(visual->SetContent(surface.Get()), "IDCompositionVisual2::SetContent");
    check(target->SetRoot(visual.Get()), "IDCompositionTarget::SetRoot");

    return OutputSurface{device, std::move(window), std::move(target), std::move(visual),
                         std::move(surface)};
}

void OutputSurface::draw(const PixelRect& dirty, const std::function<void(ID2D1DeviceContext*)>& body)
{
    // Everything above this line is in global desktop coordinates; the surface is not.
    PixelRect local =
        dirty.translated(PixelOffset{-bounds().left(), -bounds().top()})
            .intersect(PixelRect{0, 0, bounds().width, bounds().height});

    if (local.empty()) {
        return;
    }

    // The first draw on a new surface has to cover all of it. DirectComposition rejects a
    // partial update -- E_INVALIDARG, "the parameter is incorrect" -- while any of the
    // surface is still undefined, and the rectangle it rejects looks perfectly valid, so
    // the error says nothing about the real cause.
    //
    // A surface that has been hidden asks for the same thing for a different reason; see
    // invalidate().
    if (needs_full_draw_) {
        local = PixelRect{0, 0, bounds().width, bounds().height};
    }

    // What is actually being updated, back in the global coordinates the transform below
    // works in. Not `dirty`: on the first draw `local` was widened to the whole surface,
    // and clipping the clear to `dirty` left the rest of it never written at all.
    //
    // In a release build that leftover happens to be zeroes, so it is invisible and the bug
    // is not. With the D3D debug layer on it is a fill pattern, which showed up as the
    // whole screen tinted green -- fading wherever a pet walked, because that is where
    // damage finally covered it.
    const PixelRect painted = local.translated(PixelOffset{bounds().left(), bounds().top()});

    RECT update{local.left(), local.top(), local.right(), local.bottom()};

    ComPtr<IDXGISurface> dxgi_surface;
    POINT offset{};
    if (const HRESULT hr = surface_->BeginDraw(&update, IID_PPV_ARGS(&dxgi_surface), &offset);
        FAILED(hr)) {
        log_line(cat("BeginDraw failed: surface ", bounds().width, "x", bounds().height,
                     ", dirty global (", dirty.left(), ",", dirty.top(), ")-(", dirty.right(), ",",
                     dirty.bottom(), "), local (", local.left(), ",", local.top(), ")-(",
                     local.right(), ",", local.bottom(), ")"));
        check(hr, "IDCompositionSurface::BeginDraw");
    }

    const D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0F,
        96.0F);

    ID2D1DeviceContext* d2d = device_->d2d();

    ComPtr<ID2D1Bitmap1> bitmap;
    check(d2d->CreateBitmapFromDxgiSurface(dxgi_surface.Get(), &props, &bitmap),
          "CreateBitmapFromDxgiSurface");

    d2d->SetTarget(bitmap.Get());
    d2d->BeginDraw();

    // Two shifts at once, which is easy to get wrong separately.
    //
    // BeginDraw hands back an offset into a surface the compositor may have placed anywhere
    // in an atlas of its own, so drawing at the requested coordinates without applying it
    // puts everything in the wrong place -- usually off-surface entirely.
    //
    // On top of that, callers draw in global desktop coordinates. Subtracting the overlay's
    // origin here means nothing above has to know which monitor it is drawing on.
    d2d->SetTransform(D2D1::Matrix3x2F::Translation(
        static_cast<float>(offset.x - local.left() - bounds().left()),
        static_cast<float>(offset.y - local.top() - bounds().top())));

    d2d->PushAxisAlignedClip(
        D2D1::RectF(static_cast<float>(painted.left()), static_cast<float>(painted.top()),
                    static_cast<float>(painted.right()), static_cast<float>(painted.bottom())),
        D2D1_ANTIALIAS_MODE_ALIASED);

    d2d->Clear(D2D1::ColorF(0.0F, 0.0F, 0.0F, 0.0F));
    body(d2d);

    d2d->PopAxisAlignedClip();

    const HRESULT end = d2d->EndDraw();
    d2d->SetTarget(nullptr);
    check(end, "ID2D1DeviceContext::EndDraw");

    check(surface_->EndDraw(), "IDCompositionSurface::EndDraw");
    needs_full_draw_ = false;
}

} // namespace dp::win
