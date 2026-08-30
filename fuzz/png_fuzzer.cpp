// SPDX-License-Identifier: GPL-3.0-or-later

#include "dragonperch/pack_library.hpp"
#include "png.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>

#include <unistd.h>

/// The atlas image, which is the largest and least structured thing this program reads.
///
/// What is worth being clear about is *what* this fuzzes. Most of the work happens inside
/// libpng, which OSS-Fuzz has run continuously for years -- finding a bug there is not a
/// realistic goal and should not be the reason to run this. The reason is the seventy
/// lines around it, which nobody has fuzzed at all:
///
///   - the size taken from the header and handed to resize(), which without a limit is a
///     four-terabyte allocation from a file of a few dozen bytes;
///   - the row pointers, built from our own arithmetic while libpng writes what
///     png_get_rowbytes says -- any input where those two disagree is a heap overflow, and
///     nothing before this checked they agreed;
///   - the premultiply loop afterwards, walking a buffer sized by one and filled by the
///     other.
///
/// Both of the first two now have a check in front of them, added while writing this. What
/// this target does from here is keep them true.
///
/// Through a file rather than a buffer, because decode_image takes a path and that is the
/// production entry point: an in-memory door opened only for the fuzzer would be a door
/// the real program never walks through. The cost is one small write per iteration.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    // Per process, so `-jobs=N` does not have several fuzzers writing the same file.
    static const std::filesystem::path path =
        std::filesystem::temp_directory_path()
        / ("dragonperch-fuzz-png-" + std::to_string(static_cast<long>(::getpid())));

    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    }

    try {
        const dp::DecodedImage image = dp::wl::decode_image(path);

        // What the renderer is about to be handed. A decode that succeeds and returns a
        // buffer that does not match its own stated size is worse than one that threw:
        // register_atlas reads width * height * 4 bytes from it.
        if (image.size.width <= 0 || image.size.height <= 0) {
            std::abort();
        }

        const std::size_t expected = static_cast<std::size_t>(image.size.width)
                                     * static_cast<std::size_t>(image.size.height) * 4;
        if (image.pixels.size() != expected) {
            std::abort();
        }
    // Swallowing it is the point: a refusal is the documented behaviour, and what this
    // target watches for is everything else.
    // NOLINTNEXTLINE(bugprone-empty-catch)
    } catch (const std::exception&) {
        // A refusal, which is what the header promises for anything it cannot read.
    }

    return 0;
}
