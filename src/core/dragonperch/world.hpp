// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "dragonperch/geometry.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dp {

enum class EdgeKind {
    /// Top of a normal application window's frame: the title bar.
    window_top,
    /// Top edge of a panel, taskbar or dock.
    panel_top,
    /// Bottom of an output's usable area. The floor.
    screen_floor,
    /// Top of an output's usable area. Ceiling, for pets that fly.
    screen_ceiling,
};

[[nodiscard]] constexpr std::string_view kind_name(EdgeKind kind) noexcept
{
    switch (kind) {
    case EdgeKind::window_top:
        return "WindowTop";
    case EdgeKind::panel_top:
        return "PanelTop";
    case EdgeKind::screen_floor:
        return "ScreenFloor";
    case EdgeKind::screen_ceiling:
        return "ScreenCeiling";
    }
    return "?";
}

/// A horizontal line segment a pet can stand on.
///
/// Deliberately *not* "a window". This is the whole point of the core/backend split: the
/// simulation never learns what an HWND or a wl_surface is, because every backend flattens
/// whatever it can discover into segments. Adding a platform means writing a flattener,
/// not touching physics.
///
/// `owner_id` is an opaque, backend-assigned stable identity. It must survive across
/// snapshots for the same underlying window, so that a pet standing on a window is carried
/// along when that window is dragged instead of being dropped and re-acquired every frame.
struct WalkableEdge {
    std::int64_t owner_id = 0;
    int y = 0;
    int left = 0;
    int right = 0;
    EdgeKind kind = EdgeKind::window_top;
    int z_order = 0;

    [[nodiscard]] constexpr int width() const noexcept { return right - left; }
    [[nodiscard]] constexpr bool contains_x(int x) const noexcept { return x >= left && x < right; }
};

/// A monitor, in the shared desktop space -- see PixelPoint for what that means.
///
/// `scale` is physical pixels per unit of that space: 1 on Windows, where the space is
/// already physical, and the output's own scale factor on Wayland, where it is not. The
/// core does not lay anything out with it; it is carried so a renderer can pick a sprite
/// size and convert back to whatever its windowing system expects.
struct OutputInfo {
    std::int64_t id = 0;
    PixelRect bounds{};
    PixelRect work_area{};
    double scale = 1.0;
    std::string name;
};

/// An immutable view of everything the simulation is allowed to know about the desktop.
///
/// Edges are sorted by `y` ascending, then `z_order` descending, so "what is directly below
/// this point" is a short scan that stops at the first hit.
class WorldSnapshot {
public:
    WorldSnapshot() = default;

    WorldSnapshot(std::uint64_t version, std::vector<WalkableEdge> edges,
                  std::vector<OutputInfo> outputs);

    [[nodiscard]] std::uint64_t version() const noexcept { return version_; }
    [[nodiscard]] std::span<const WalkableEdge> edges() const noexcept { return edges_; }
    [[nodiscard]] std::span<const OutputInfo> outputs() const noexcept { return outputs_; }

    /// Highest edge strictly below `from` that spans `from.x`: the landing target.
    [[nodiscard]] const WalkableEdge* edge_below(PixelPoint from) const noexcept;

    [[nodiscard]] const WalkableEdge* find_by_owner(std::int64_t owner_id) const noexcept;

    [[nodiscard]] const OutputInfo* output_at(PixelPoint p) const noexcept;

    /// Sorts in place into the order the lookups above assume.
    static void sort(std::vector<WalkableEdge>& edges);

private:
    std::uint64_t version_ = 0;
    std::vector<WalkableEdge> edges_;
    std::vector<OutputInfo> outputs_;
};

/// Supplies and maintains the set of surfaces pets can stand on.
///
/// Implementations must be **event driven, never polling**. On Windows that is
/// `SetWinEventHook`; on X11 a `StructureNotify` selection plus EWMH property watches; on
/// KWin a KWin script pushing over D-Bus. Enumerating every top-level window at 60 Hz
/// shows up in the user's battery life, and a desktop pet that costs measurable power is a
/// desktop pet that gets uninstalled.
///
/// Implementations should also coalesce: dragging a window emits a geometry change per
/// compositor frame, and the simulation reads one snapshot per frame.
class IWorldProvider {
public:
    using ChangedHandler = std::function<void(const WorldSnapshot&)>;

    virtual ~IWorldProvider() = default;

    [[nodiscard]] virtual const WorldSnapshot& current() const = 0;
    virtual void set_changed_handler(ChangedHandler handler) = 0;

    /// Begins watching. **Must be idempotent**: `PetHost::run` calls it unconditionally,
    /// and a head that also wants a snapshot before the loop starts -- to size an overlay,
    /// or to print the world and exit -- will call it too. The Wayland head did not, and
    /// the second call tried to claim a D-Bus name the first already owned.
    virtual void start() = 0;
};

} // namespace dp
