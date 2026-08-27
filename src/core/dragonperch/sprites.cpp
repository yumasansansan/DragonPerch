// SPDX-License-Identifier: GPL-3.0-or-later
#include "dragonperch/sprites.hpp"

#include <cmath>
#include <format>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace dp {
namespace {

const AnimationFrame& empty_frame()
{
    static const AnimationFrame frame{};
    return frame;
}

} // namespace

Animation::Animation(std::string name, std::vector<AnimationFrame> frames, bool loop)
    : name_(std::move(name))
    , frames_(std::move(frames))
    , loop_(loop)
    , total_(std::accumulate(frames_.begin(), frames_.end(), Duration{},
                             [](Duration acc, const AnimationFrame& f) { return acc + f.duration; }))
{
}

const AnimationFrame& Animation::frame_at(Duration elapsed) const
{
    if (frames_.empty()) {
        return empty_frame();
    }

    Duration t = elapsed;
    if (loop_ && total_ > Duration::zero()) {
        // fmod rather than a subtract-loop: a pet idle for an hour would otherwise spin
        // here for thousands of iterations on the frame it resumes.
        t = Duration{std::fmod(elapsed.count(), total_.count())};
        if (t < Duration::zero()) {
            t += total_;
        }
    }

    for (const AnimationFrame& frame : frames_) {
        if (t < frame.duration) {
            return frame;
        }
        t -= frame.duration;
    }

    return frames_.back();
}

SpritePack::SpritePack(std::string id, std::string display_name, std::string artwork_licence,
                       std::string attribution, int atlas_id,
                       std::map<std::string, Animation, std::less<>> animations)
    : id_(std::move(id))
    , display_name_(std::move(display_name))
    , artwork_licence_(std::move(artwork_licence))
    , attribution_(std::move(attribution))
    , atlas_id_(atlas_id)
    , animations_(std::move(animations))
{
}

bool SpritePack::has(std::string_view animation) const
{
    return animations_.find(animation) != animations_.end();
}

const Animation& SpritePack::require(std::string_view animation) const
{
    const auto it = animations_.find(animation);
    if (it == animations_.end()) {
        throw std::out_of_range(
            std::format("sprite pack '{}' has no animation '{}'", id_, animation));
    }
    return it->second;
}

} // namespace dp
