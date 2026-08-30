// SPDX-License-Identifier: GPL-3.0-or-later

#include "dragonperch/placeholder_pack.hpp"
#include "dragonperch/simulation.hpp"
#include "dragonperch/world.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace {

class Bytes {
public:
    Bytes(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}

    [[nodiscard]] bool done() const { return at_ >= size_; }

    [[nodiscard]] int number(int lowest, int highest)
    {
        const int span = highest - lowest + 1;
        return lowest + static_cast<int>(next16() % static_cast<unsigned>(span));
    }

private:
    [[nodiscard]] unsigned next16()
    {
        unsigned value = 0;
        for (int i = 0; i < 2; ++i) {
            value = (value << 8U) | (at_ < size_ ? data_[at_] : 0U);
            ++at_;
        }
        return value;
    }

    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t at_ = 0;
};

/// Far enough outside any world this builds that reaching it means something ran away.
constexpr int nowhere_near = 1'000'000;

} // namespace

/// The physics, driven by a world that keeps changing underneath it.
///
/// The unit tests here each set up one situation and check one outcome, which is what they
/// are for and also their limit: they only ever look at the situations somebody thought of.
/// Every hard bug this simulation has had was a situation nobody thought of -- a pet
/// stepping off the end of an edge that spans the whole screen and finding nothing below,
/// a monitor arriving mid-frame, a world with edges and no outputs at all.
///
/// So this one shuffles the world under the pets and asserts what must be true whatever
/// happens: nothing becomes a NaN, nothing runs away to infinity, and a pet that claims to
/// be standing on something is standing on something that exists.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    Bytes bytes{data, size};

    const dp::SpritePack pack = dp::placeholder_pack::create(0);

    dp::SimulationOptions options;
    options.walk_speed = static_cast<double>(bytes.number(1, 1000));
    options.idle_interval = dp::Duration{static_cast<double>(bytes.number(0, 3600))};
    dp::Simulation simulation{options};

    for (int i = 0, pets = bytes.number(1, 4); i < pets; ++i) {
        simulation.spawn(pack, dp::PixelPoint{bytes.number(-1000, 1000),
                                              bytes.number(-1000, 1000)});
    }

    std::uint64_t version = 0;
    // Enough worlds that a pet which never stops falling has time to show it. The first
    // version of this stopped at 64, which at the fastest this can fall is just short of
    // the bound below -- so the check was there and could not be reached.
    while (!bytes.done() && version < 256) {
        // A new world, sometimes with no outputs at all -- which is what the Wayland head
        // looks like before the compositor has said anything.
        std::vector<dp::WalkableEdge> edges;
        for (int i = 0, count = bytes.number(0, 8); i < count; ++i) {
            const int left = bytes.number(-1500, 1500);
            edges.push_back(dp::WalkableEdge{
                .owner_id = i + 1,
                .y = bytes.number(-1000, 1000),
                .left = left,
                .right = left + bytes.number(0, 2000),
                .kind = dp::EdgeKind::window_top,
                .z_order = bytes.number(-10, 10),
            });
        }
        dp::WorldSnapshot::sort(edges);

        std::vector<dp::OutputInfo> outputs;
        for (int i = 0, count = bytes.number(0, 2); i < count; ++i) {
            outputs.push_back(dp::OutputInfo{
                .id = i + 1,
                .bounds = dp::PixelRect{bytes.number(-1000, 1000), bytes.number(-1000, 1000),
                                        bytes.number(1, 2000), bytes.number(1, 2000)},
                .work_area = {},
                .scale = 1.0,
                .name = {},
            });
            outputs.back().work_area = outputs.back().bounds;
        }

        simulation.set_world(dp::WorldSnapshot{++version, std::move(edges), outputs});

        // Several steps per world, and a step is allowed to be a long one: a laptop coming
        // back from sleep hands the loop a delta measured in seconds, not milliseconds.
        for (int step = 0, steps = bytes.number(1, 8); step < steps; ++step) {
            simulation.update(dp::Duration{static_cast<double>(bytes.number(0, 2000)) / 1000.0});
        }

        for (const dp::Pet& pet : simulation.pets()) {
            // Velocity is the only floating point number a pet carries, and a NaN in it
            // reaches std::lround, which is undefined behaviour rather than a slow pet.
            if (!std::isfinite(pet.velocity_y())) {
                std::abort();
            }

            // Nothing may run away. A pet that walks or falls forever is an integer that
            // eventually overflows, and the screen is nowhere near this big.
            if (std::abs(pet.position().x) > nowhere_near
                || std::abs(pet.position().y) > nowhere_near) {
                std::abort();
            }

            // And a pet that says it is standing on something must be standing on something
            // that exists. Perches are tracked by owner id across snapshots, so this is the
            // invariant that carrying a pet on a dragged window rests on.
            if (pet.perch().has_value()
                && simulation.world().find_by_owner(*pet.perch()) == nullptr) {
                std::abort();
            }
        }
    }

    return 0;
}
