#pragma once
// Polygon-with-holes triangulation (ear clipping after bridging each hole to the outer ring).

#include <array>
#include <cstddef>
#include <vector>

namespace geom {

using P2 = std::array<double, 2>;
using Tri = std::array<std::size_t, 3>;

// Points are numbered outer first, then each hole in order. Rings may wind either way.
std::vector<Tri> triangulate(const std::vector<P2>& outer, const std::vector<std::vector<P2>>& holes);

double signedArea(const std::vector<P2>& ring);

}  // namespace geom
