// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "dragonperch/geometry.hpp"

#include <chrono>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dp {

using Duration = std::chrono::duration<double>;

/// One frame of an animation.
///
/// `anchor` is the offset from the frame's top-left to the pet's feet -- the point that
/// sits on the walkable edge. Carrying it per frame lets frames of different sizes line up
/// without the simulation ever knowing a sprite's dimensions.
struct AnimationFrame {
    PixelRect source{};
    PixelOffset anchor{};
    Duration duration{};
};

class Animation {
public:
    Animation() = default;
    Animation(std::string name, std::vector<AnimationFrame> frames, bool loop);

    [[nodiscard]] std::string_view name() const noexcept { return name_; }
    [[nodiscard]] std::span<const AnimationFrame> frames() const noexcept { return frames_; }
    [[nodiscard]] bool loops() const noexcept { return loop_; }
    [[nodiscard]] Duration total() const noexcept { return total_; }

    /// The frame showing `elapsed` into the animation. Wraps if this animation loops,
    /// otherwise holds on the last frame.
    [[nodiscard]] const AnimationFrame& frame_at(Duration elapsed) const;

private:
    std::string name_;
    std::vector<AnimationFrame> frames_;
    bool loop_ = true;
    Duration total_{};
};

/// A named set of animations plus the atlas they index into.
class SpritePack {
public:
    SpritePack() = default;
    SpritePack(std::string id, std::string display_name, std::string artwork_licence,
               std::string attribution, int atlas_id,
               std::map<std::string, Animation, std::less<>> animations);

    [[nodiscard]] std::string_view id() const noexcept { return id_; }
    [[nodiscard]] std::string_view display_name() const noexcept { return display_name_; }

    /// SPDX id for this pack's artwork. Konqi packs are CC-BY-SA-4.0, not the project's
    /// GPL: sprites are data loaded at runtime, not linked code, so the two coexist.
    [[nodiscard]] std::string_view artwork_licence() const noexcept { return artwork_licence_; }
    [[nodiscard]] std::string_view attribution() const noexcept { return attribution_; }
    [[nodiscard]] int atlas_id() const noexcept { return atlas_id_; }

    void set_atlas_id(int id) noexcept { atlas_id_ = id; }

    /// Throws std::out_of_range if the animation is missing. A pack without "walk" is a
    /// packaging error, not a runtime condition worth degrading gracefully for.
    [[nodiscard]] const Animation& require(std::string_view animation) const;

    [[nodiscard]] bool has(std::string_view animation) const;

private:
    std::string id_;
    std::string display_name_;
    std::string artwork_licence_;
    std::string attribution_;
    int atlas_id_ = -1;

    // std::less<> so a string_view can be looked up without allocating a std::string.
    std::map<std::string, Animation, std::less<>> animations_;
};

} // namespace dp
