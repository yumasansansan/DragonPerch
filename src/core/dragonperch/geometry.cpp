// SPDX-License-Identifier: GPL-3.0-or-later
#include "dragonperch/geometry.hpp"

namespace dp {

// The geometry types are constexpr and header-only on purpose. These checks run at compile
// time and exist so the half-open convention cannot drift unnoticed; they are cheaper than
// a test binary and impossible to forget to run.
static_assert(PixelRect{0, 0, 10, 10}.contains(PixelPoint{0, 0}));
static_assert(!PixelRect{0, 0, 10, 10}.contains(PixelPoint{10, 0}));
static_assert(PixelRect::from_edges(0, 0, 10, 10) == PixelRect{0, 0, 10, 10});
static_assert(PixelRect{0, 0, 10, 10}.intersect(PixelRect{5, 5, 10, 10}) == PixelRect{5, 5, 5, 5});
static_assert(PixelRect{}.united(PixelRect{2, 2, 3, 3}) == PixelRect{2, 2, 3, 3});
static_assert(!PixelRect{0, 0, 0, 5}.intersects(PixelRect{0, 0, 5, 5}));

} // namespace dp
