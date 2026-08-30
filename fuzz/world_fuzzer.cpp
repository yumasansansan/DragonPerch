// SPDX-License-Identifier: GPL-3.0-or-later

#include "dragonperch/edge_builder.hpp"
#include "dragonperch/world.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace {

/// Numbers out of the fuzzer's bytes, bounded to what a desktop could hold.
///
/// Bounded on purpose rather than for safety: the boundary that takes untrusted numbers is
/// the KWin report, and it refuses anything larger before this code ever sees it. What is
/// being looked for here is a wrong answer, not an overflow, so the inputs are the ones the
/// program really gets and the assertions can be strict.
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

constexpr int minimum_width = 64;

} // namespace

/// The geometry the whole program stands on: occlusion clipping, the sort, and the lookup
/// the physics asks every frame.
///
/// Not an input boundary -- every number here has already been through one -- which is why
/// this asserts properties rather than watching for a crash. A wrong answer from
/// edge_below does not crash anything; it puts a pet on the window behind the one it was
/// standing over, which is a bug somebody has to notice by eye. These are the invariants
/// the rest of the core is written against.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    Bytes bytes{data, size};

    std::vector<dp::WindowCandidate> candidates;
    while (!bytes.done() && candidates.size() < 32) {
        const int x = bytes.number(-2000, 2000);
        const int y = bytes.number(-2000, 2000);
        candidates.push_back(dp::WindowCandidate{
            .id = static_cast<std::int64_t>(candidates.size()) + 1,
            .frame = dp::PixelRect{x, y, bytes.number(0, 2000), bytes.number(0, 2000)},
            .z = bytes.number(-100, 100),
            .kind = dp::EdgeKind::window_top,
        });
    }

    std::vector<dp::WalkableEdge> edges;
    dp::append_window_edges(candidates, minimum_width, edges);

    // Every edge that survives is wide enough to stand on. Anything narrower is a sliver of
    // a window mostly hidden behind another, and putting a pet on one is the thing the
    // occlusion pass exists to prevent.
    for (const dp::WalkableEdge& edge : edges) {
        if (edge.width() < minimum_width) {
            std::abort();
        }
    }

    dp::WorldSnapshot::sort(edges);

    // The order the lookups assume: by y ascending, then front to back. edge_below stops at
    // the first hit, so an unsorted list does not fail loudly -- it quietly answers with the
    // wrong window.
    for (std::size_t i = 1; i < edges.size(); ++i) {
        const dp::WalkableEdge& before = edges[i - 1];
        const dp::WalkableEdge& after = edges[i];
        if (after.y < before.y || (after.y == before.y && after.z_order > before.z_order)) {
            std::abort();
        }
    }

    const dp::WorldSnapshot snapshot{1, edges, {}};

    // And the lookup itself, asked from a handful of places and checked against the whole
    // list rather than against itself.
    for (int i = 0; i < 8 && !bytes.done(); ++i) {
        const dp::PixelPoint from{bytes.number(-2000, 2000), bytes.number(-2000, 2000)};
        const dp::WalkableEdge* found = snapshot.edge_below(from);

        if (found != nullptr) {
            // What it returned must be an edge that is really below and really under foot.
            if (found->y <= from.y || !found->contains_x(from.x)) {
                std::abort();
            }
        }

        // And it must be the *highest* such edge: nothing else below the point and above
        // the answer may span it. That is the difference between landing on the title bar
        // in front and landing on the one behind it.
        for (const dp::WalkableEdge& edge : snapshot.edges()) {
            if (edge.y <= from.y || !edge.contains_x(from.x)) {
                continue;
            }
            if (found == nullptr || edge.y < found->y) {
                std::abort();
            }
        }
    }

    return 0;
}
