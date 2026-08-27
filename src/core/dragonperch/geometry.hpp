// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <compare>

namespace dp {

/// A point in the one coordinate space the whole program shares: the desktop laid out as
/// the windowing system lays it out, origin at the top-left, Y down.
///
/// **Which unit that is, is the backend's choice** -- the core only requires that one
/// backend uses one unit for everything it reports. Windows uses physical pixels, because
/// a per-monitor-v2 aware process is given them and Windows arranges monitors in them.
/// Wayland uses *logical* units, because it has to: each output carries its own integer
/// scale, so multiplying every output's position by its own scale tears the desktop into
/// overlapping and gapped rectangles. There is no coherent global physical space to use.
///
/// `OutputInfo::scale` carries the factor so a renderer can pick a texture size and convert
/// at the very edge. Nothing above a backend reads it.
struct PixelPoint {
    int x = 0;
    int y = 0;

    friend constexpr auto operator<=>(const PixelPoint&, const PixelPoint&) = default;
};

struct PixelOffset {
    int dx = 0;
    int dy = 0;

    friend constexpr auto operator<=>(const PixelOffset&, const PixelOffset&) = default;
};

struct PixelSize {
    int width = 0;
    int height = 0;

    friend constexpr auto operator<=>(const PixelSize&, const PixelSize&) = default;
};

constexpr PixelPoint operator+(PixelPoint p, PixelOffset d) noexcept
{
    return {p.x + d.dx, p.y + d.dy};
}

constexpr PixelOffset operator-(PixelPoint a, PixelPoint b) noexcept
{
    return {a.x - b.x, a.y - b.y};
}

/// Axis-aligned rectangle in the shared desktop space. Half-open: `left <= x < right`.
struct PixelRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    friend constexpr auto operator<=>(const PixelRect&, const PixelRect&) = default;

    [[nodiscard]] constexpr int left() const noexcept { return x; }
    [[nodiscard]] constexpr int top() const noexcept { return y; }
    [[nodiscard]] constexpr int right() const noexcept { return x + width; }
    [[nodiscard]] constexpr int bottom() const noexcept { return y + height; }

    [[nodiscard]] constexpr PixelPoint top_left() const noexcept { return {x, y}; }
    [[nodiscard]] constexpr PixelSize size() const noexcept { return {width, height}; }
    [[nodiscard]] constexpr bool empty() const noexcept { return width <= 0 || height <= 0; }

    [[nodiscard]] static constexpr PixelRect from_edges(int l, int t, int r, int b) noexcept
    {
        return {l, t, r - l, b - t};
    }

    [[nodiscard]] constexpr bool contains(PixelPoint p) const noexcept
    {
        return p.x >= left() && p.x < right() && p.y >= top() && p.y < bottom();
    }

    [[nodiscard]] constexpr bool intersects(const PixelRect& o) const noexcept
    {
        return o.left() < right() && left() < o.right() && o.top() < bottom() && top() < o.bottom();
    }

    [[nodiscard]] constexpr PixelRect intersect(const PixelRect& o) const noexcept
    {
        return from_edges(std::max(left(), o.left()), std::max(top(), o.top()),
                          std::min(right(), o.right()), std::min(bottom(), o.bottom()));
    }

    [[nodiscard]] constexpr PixelRect united(const PixelRect& o) const noexcept
    {
        if (empty()) {
            return o;
        }
        if (o.empty()) {
            return *this;
        }
        return from_edges(std::min(left(), o.left()), std::min(top(), o.top()),
                          std::max(right(), o.right()), std::max(bottom(), o.bottom()));
    }

    [[nodiscard]] constexpr PixelRect inflated(int dx, int dy) const noexcept
    {
        return {x - dx, y - dy, width + (2 * dx), height + (2 * dy)};
    }

    [[nodiscard]] constexpr PixelRect translated(PixelOffset d) const noexcept
    {
        return {x + d.dx, y + d.dy, width, height};
    }
};

} // namespace dp
