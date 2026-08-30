// SPDX-License-Identifier: GPL-3.0-or-later
#include "png.hpp"
#include "dragonperch/text.hpp"

#include "win_headers.hpp"

#include <stdexcept>

namespace dp::win {

dp::DecodedImage decode_image(const std::filesystem::path& file)
{
    // WIC rather than a bundled decoder. It is part of Windows, it reads PNG, and asking it
    // for 32bppPBGRA gets the premultiplied layout the renderer wants without a conversion
    // pass of our own -- premultiplying by hand is a classic source of dark fringes around
    // sprite edges.
    //
    // The Linux head will need its own decoder. That is fine: decoding an image is an OS
    // service, and keeping it out of the core is what lets the core stay platform-free.
    ComPtr<IWICImagingFactory2> factory;
    check(CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER,
                           IID_PPV_ARGS(&factory)),
          "CoCreateInstance(WICImagingFactory2)");

    ComPtr<IWICBitmapDecoder> decoder;
    check(factory->CreateDecoderFromFilename(file.c_str(), nullptr, GENERIC_READ,
                                             WICDecodeMetadataCacheOnLoad, &decoder),
          "IWICImagingFactory2::CreateDecoderFromFilename");

    ComPtr<IWICBitmapFrameDecode> frame;
    check(decoder->GetFrame(0, &frame), "IWICBitmapDecoder::GetFrame");

    ComPtr<IWICFormatConverter> converter;
    check(factory->CreateFormatConverter(&converter), "CreateFormatConverter");
    check(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
                                WICBitmapDitherTypeNone, nullptr, 0.0,
                                WICBitmapPaletteTypeMedianCut),
          "IWICFormatConverter::Initialize");

    UINT width = 0;
    UINT height = 0;
    check(converter->GetSize(&width, &height), "IWICFormatConverter::GetSize");

    if (width == 0 || height == 0) {
        throw std::runtime_error(cat(file.string(), " decoded to an empty image"));
    }

    const UINT stride = width * 4;
    dp::DecodedImage image;
    image.size = PixelSize{static_cast<int>(width), static_cast<int>(height)};
    image.pixels.resize(static_cast<std::size_t>(stride) * static_cast<std::size_t>(height));

    check(converter->CopyPixels(nullptr, stride, static_cast<UINT>(image.pixels.size()),
                                reinterpret_cast<BYTE*>(image.pixels.data())),
          "IWICFormatConverter::CopyPixels");

    return image;
}

#ifdef DRAGONPERCH_DIAGNOSTICS

void encode_png(const std::filesystem::path& file, std::span<const std::byte> premultiplied_bgra,
                PixelSize size)
{
    ComPtr<IWICImagingFactory2> factory;
    check(CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER,
                           IID_PPV_ARGS(&factory)),
          "CoCreateInstance(WICImagingFactory2)");

    ComPtr<IWICStream> stream;
    check(factory->CreateStream(&stream), "IWICImagingFactory2::CreateStream");
    check(stream->InitializeFromFilename(file.c_str(), GENERIC_WRITE),
          "IWICStream::InitializeFromFilename");

    ComPtr<IWICBitmapEncoder> encoder;
    check(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder), "CreateEncoder");
    check(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache),
          "IWICBitmapEncoder::Initialize");

    ComPtr<IWICBitmapFrameEncode> frame;
    check(encoder->CreateNewFrame(&frame, nullptr), "CreateNewFrame");
    check(frame->Initialize(nullptr), "IWICBitmapFrameEncode::Initialize");
    check(frame->SetSize(static_cast<UINT>(size.width), static_cast<UINT>(size.height)),
          "SetSize");

    // The encoder is asked for premultiplied BGRA, the same layout the buffer is already
    // in, so nothing is converted and nothing can be lost on the way out.
    GUID format = GUID_WICPixelFormat32bppPBGRA;
    check(frame->SetPixelFormat(&format), "SetPixelFormat");

    check(frame->WritePixels(static_cast<UINT>(size.height), static_cast<UINT>(size.width) * 4,
                             static_cast<UINT>(premultiplied_bgra.size()),
                             reinterpret_cast<BYTE*>(
                                 const_cast<std::byte*>(premultiplied_bgra.data()))),
          "WritePixels");

    check(frame->Commit(), "IWICBitmapFrameEncode::Commit");
    check(encoder->Commit(), "IWICBitmapEncoder::Commit");
}

#endif

} // namespace dp::win
