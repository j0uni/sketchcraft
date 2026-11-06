#include "geom/Triangulate.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace geom {

namespace {

double cross(const P2& o, const P2& a, const P2& b) {
    return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0]);
}

bool inTriangle(const P2& p, const P2& a, const P2& b, const P2& c) {
    const double c1 = cross(a, b, p), c2 = cross(b, c, p), c3 = cross(c, a, p);
    return c1 >= -1e-12 && c2 >= -1e-12 && c3 >= -1e-12;
}

bool segmentsCross(const P2& p1, const P2& p2, const P2& q1, const P2& q2) {
    const double d1 = cross(q1, q2, p1), d2 = cross(q1, q2, p2);
    const double d3 = cross(p1, p2, q1), d4 = cross(p1, p2, q2);
    return ((d1 > 1e-12 && d2 < -1e-12) || (d1 < -1e-12 && d2 > 1e-12)) &&
           ((d3 > 1e-12 && d4 < -1e-12) || (d3 < -1e-12 && d4 > 1e-12));
}

bool samePoint(const P2& a, const P2& b) {
    return a[0] == b[0] && a[1] == b[1];
}

using Ring = std::vector<std::size_t>;

// No edge of the ring or of the holes still to merge crosses segment a-b.
bool visible(const std::vector<P2>& pts, std::size_t a, std::size_t b, const Ring& ring,
             const std::vector<Ring>& pending) {
    auto blocked = [&](const Ring& r) {
        for (std::size_t i = 0; i < r.size(); ++i) {
            const std::size_t e0 = r[i], e1 = r[(i + 1) % r.size()];
            if (e0 == a || e1 == a || e0 == b || e1 == b) continue;
            if (segmentsCross(pts.at(a), pts.at(b), pts.at(e0), pts.at(e1))) return true;
        }
        return false;
    };
    if (blocked(ring)) return false;
    return std::none_of(pending.begin(), pending.end(), blocked);
}

std::vector<Tri> earClip(const std::vector<P2>& pts, Ring ring) {
    std::vector<Tri> out;
    std::size_t guard = 0;
    while (ring.size() > 3 && guard < 200000) {
        ++guard;
        const std::size_t n = ring.size();
        bool clipped = false;
        for (std::size_t i = 0; i < n; ++i) {
            const std::size_t ia = ring[(i + n - 1) % n], ib = ring[i], ic = ring[(i + 1) % n];
            const P2 &a = pts.at(ia), &b = pts.at(ib), &c = pts.at(ic);
            if (cross(a, b, c) <= 1e-14) continue;  // reflex or flat corner
            bool empty = true;
            for (std::size_t k = 0; k < n && empty; ++k) {
                const std::size_t ik = ring[k];
                if (ik == ia || ik == ib || ik == ic) continue;
                const P2& p = pts.at(ik);
                if (samePoint(p, a) || samePoint(p, b) || samePoint(p, c)) continue;  // bridge copies
                if (inTriangle(p, a, b, c)) empty = false;
            }
            if (!empty) continue;
            out.push_back({ia, ib, ic});
            ring.erase(ring.begin() + static_cast<std::ptrdiff_t>(i));
            clipped = true;
            break;
        }
        if (!clipped) {
            // Degenerate input: drop the flattest corner and carry on.
            std::size_t best = 0;
            double bestArea = std::numeric_limits<double>::max();
            for (std::size_t i = 0; i < n; ++i) {
                const double ar =
                    std::abs(cross(pts.at(ring[(i + n - 1) % n]), pts.at(ring[i]), pts.at(ring[(i + 1) % n])));
                if (ar < bestArea) {
                    bestArea = ar;
                    best = i;
                }
            }
            const std::size_t ia = ring[(best + n - 1) % n], ib = ring[best], ic = ring[(best + 1) % n];
            if (cross(pts.at(ia), pts.at(ib), pts.at(ic)) > 0) out.push_back({ia, ib, ic});
            ring.erase(ring.begin() + static_cast<std::ptrdiff_t>(best));
        }
    }
    if (ring.size() == 3 && cross(pts.at(ring[0]), pts.at(ring[1]), pts.at(ring[2])) > 0) {
        out.push_back({ring[0], ring[1], ring[2]});
    }
    return out;
}

}  // namespace

double signedArea(const std::vector<P2>& ring) {
    double a = 0;
    for (std::size_t i = 0; i < ring.size(); ++i) {
        const P2& p = ring[i];
        const P2& q = ring[(i + 1) % ring.size()];
        a += p[0] * q[1] - q[0] * p[1];
    }
    return a * 0.5;
}

std::vector<Tri> triangulate(const std::vector<P2>& outer, const std::vector<std::vector<P2>>& holes) {
    if (outer.size() < 3) return {};
    std::vector<P2> pts = outer;
    Ring ring(outer.size());
    std::iota(ring.begin(), ring.end(), std::size_t{0});
    if (signedArea(outer) < 0) std::reverse(ring.begin(), ring.end());

    std::vector<Ring> hs;
    for (const auto& h : holes) {
        const std::size_t base = pts.size();
        pts.insert(pts.end(), h.begin(), h.end());
        if (h.size() < 3) continue;
        Ring r(h.size());
        std::iota(r.begin(), r.end(), base);
        if (signedArea(h) > 0) std::reverse(r.begin(), r.end());  // holes wind clockwise
        hs.push_back(std::move(r));
    }
    auto maxX = [&](const Ring& r) {
        double m = -std::numeric_limits<double>::max();
        for (std::size_t i : r) m = std::max(m, pts.at(i)[0]);
        return m;
    };
    std::sort(hs.begin(), hs.end(), [&](const Ring& a, const Ring& b) { return maxX(a) > maxX(b); });

    // Bridge each hole (right-most first) to the nearest ring corner it can see.
    while (!hs.empty()) {
        Ring h = std::move(hs.front());
        hs.erase(hs.begin());
        std::size_t hm = 0;
        for (std::size_t i = 1; i < h.size(); ++i) {
            if (pts.at(h[i])[0] > pts.at(h[hm])[0]) hm = i;
        }
        const std::size_t m = h[hm];
        Ring order(ring.size());
        std::iota(order.begin(), order.end(), std::size_t{0});
        auto d2 = [&](std::size_t ri) {
            const P2& p = pts.at(ring[ri]);
            const P2& q = pts.at(m);
            return (p[0] - q[0]) * (p[0] - q[0]) + (p[1] - q[1]) * (p[1] - q[1]);
        };
        std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return d2(a) < d2(b); });
        std::vector<Ring> others = hs;
        others.push_back(h);
        std::size_t pick = order.front();
        for (std::size_t ri : order) {
            if (visible(pts, ring[ri], m, ring, others)) {
                pick = ri;
                break;
            }
        }
        Ring merged;
        merged.reserve(ring.size() + h.size() + 2);
        merged.insert(merged.end(), ring.begin(), ring.begin() + static_cast<std::ptrdiff_t>(pick) + 1);
        for (std::size_t k = 0; k <= h.size(); ++k) merged.push_back(h[(hm + k) % h.size()]);
        merged.push_back(ring[pick]);
        merged.insert(merged.end(), ring.begin() + static_cast<std::ptrdiff_t>(pick) + 1, ring.end());
        ring = std::move(merged);
    }
    return earClip(pts, std::move(ring));
}

}  // namespace geom
