// SPDX-License-Identifier: GPL-3.0-or-later
#include "dragonperch/text.hpp"
#include "png.hpp"

#include <cstdio>
#include <memory>
#include <stdexcept>
#include <vector>

#include <png.h>

namespace dp::wl {
namespace {

struct FileCloser {
    void operator()(std::FILE* file) const noexcept
    {
        if (file != nullptr) {
            std::fclose(file);
        }
    }
};

/// libpng's error path is a longjmp, which will not run C++ destructors. Everything that
/// owns anything therefore has to live outside the scope the longjmp lands in -- which is
/// what this is for: the read structs are freed here whichever way control leaves.
struct PngReader {
    png_structp png = nullptr;
    png_infop info = nullptr;

    ~PngReader()
    {
        if (png != nullptr) {
            png_destroy_read_struct(&png, info != nullptr ? &info : nullptr, nullptr);
        }
    }
};

} // namespace

dp::DecodedImage decode_image(const std::filesystem::path& file)
{
    const std::unique_ptr<std::FILE, FileCloser> handle{std::fopen(file.c_str(), "rb")};
    if (!handle) {
        throw std::runtime_error(cat("cannot open ", file.string()));
    }

    PngReader reader;
    reader.png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (reader.png == nullptr) {
        throw std::runtime_error("png_create_read_struct failed");
    }

    reader.info = png_create_info_struct(reader.png);
    if (reader.info == nullptr) {
        throw std::runtime_error("png_create_info_struct failed");
    }

    dp::DecodedImage image;
    std::vector<png_bytep> rows;

    // NOLINTNEXTLINE(cert-err52-cpp) -- libpng has no other error mechanism.
    if (setjmp(png_jmpbuf(reader.png)) != 0) {
        throw std::runtime_error(cat(file.string(), " is not a PNG this can read"));
    }

    png_init_io(reader.png, handle.get());
    png_read_info(reader.png, reader.info);

    // Normalise everything to 8-bit RGBA before reading a single row: palettes, greyscale,
    // 16-bit channels and tRNS chunks all become the one layout, so the loop below has one
    // case rather than six.
    png_set_expand(reader.png);
    png_set_strip_16(reader.png);
    png_set_gray_to_rgb(reader.png);
    png_set_filler(reader.png, 0xFF, PNG_FILLER_AFTER);
    png_set_interlace_handling(reader.png);
    png_read_update_info(reader.png, reader.info);

    const auto width = static_cast<int>(png_get_image_width(reader.png, reader.info));
    const auto height = static_cast<int>(png_get_image_height(reader.png, reader.info));
    if (width <= 0 || height <= 0) {
        throw std::runtime_error(cat(file.string(), " has no pixels"));
    }

    image.size = PixelSize{width, height};
    image.pixels.resize(static_cast<std::size_t>(width) * height * 4);

    rows.resize(static_cast<std::size_t>(height));
    for (int y = 0; y < height; ++y) {
        rows[static_cast<std::size_t>(y)] =
            reinterpret_cast<png_bytep>(image.pixels.data()) + static_cast<std::size_t>(y) * width * 4;
    }

    png_read_image(reader.png, rows.data());

    // In place, RGBA straight to premultiplied BGRA. PNG stores straight alpha; the
    // renderers all blend premultiplied, and multiplying here rather than per frame means
    // it happens once per atlas rather than once per pixel per frame.
    auto* bytes = reinterpret_cast<unsigned char*>(image.pixels.data());
    for (std::size_t i = 0; i < image.pixels.size(); i += 4) {
        const unsigned int r = bytes[i];
        const unsigned int g = bytes[i + 1];
        const unsigned int b = bytes[i + 2];
        const unsigned int a = bytes[i + 3];

        bytes[i] = static_cast<unsigned char>((b * a + 127) / 255);
        bytes[i + 1] = static_cast<unsigned char>((g * a + 127) / 255);
        bytes[i + 2] = static_cast<unsigned char>((r * a + 127) / 255);
        bytes[i + 3] = static_cast<unsigned char>(a);
    }

    return image;
}

} // namespace dp::wl
