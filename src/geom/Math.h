#pragma once
// Small 3D math kit used by the renderer and the editing tools.
// Model units are inches, like SketchUp's internal unit.

#include "skp/Model.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <vector>

namespace geom {

struct V3 {
    double x = 0, y = 0, z = 0;

    constexpr V3() = default;
    constexpr V3(double ax, double ay, double az) : x(ax), y(ay), z(az) {}
    static constexpr V3 from(const skp::Vec3d& a) { return {a[0], a[1], a[2]}; }
    constexpr skp::Vec3d arr() const { return {x, y, z}; }

    constexpr V3 operator+(const V3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr V3 operator-(const V3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr V3 operator*(double s) const { return {x * s, y * s, z * s}; }
    constexpr V3 operator/(double s) const { return {x / s, y / s, z / s}; }
    constexpr V3 operator-() const { return {-x, -y, -z}; }
    V3& operator+=(const V3& o) {
        x += o.x;
        y += o.y;
        z += o.z;
        return *this;
    }
    constexpr double dot(const V3& o) const { return x * o.x + y * o.y + z * o.z; }
    constexpr V3 cross(const V3& o) const { return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x}; }
    double length() const { return std::sqrt(dot(*this)); }
    V3 normalized() const {
        const double l = length();
        return l > 1e-300 ? *this / l : V3{0, 0, 0};
    }
    double dist(const V3& o) const { return (*this - o).length(); }
};

// 4x4 matrix, column-major (m[col*4+row]), for points as columns.
struct M4 {
    std::array<double, 16> m{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

    static M4 identity() { return {}; }
    double& at(int row, int col) { return m[static_cast<std::size_t>(col * 4 + row)]; }
    double at(int row, int col) const { return m[static_cast<std::size_t>(col * 4 + row)]; }

    // From SketchUp's 13 numbers: x axis = t0,t3,t6 ... translation t9..t11, scale t12.
    static M4 fromSkp(const skp::Xform13& t) {
        const double w = std::abs(t[12]) > 1e-12 ? t[12] : 1.0;
        M4 r;
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 3; ++col) r.at(row, col) = t[static_cast<std::size_t>(row * 3 + col)] / w;
            r.at(row, 3) = t[static_cast<std::size_t>(9 + row)] / w;
        }
        return r;
    }
    skp::Xform13 toSkp() const {
        skp::Xform13 t{};
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 3; ++col) t[static_cast<std::size_t>(row * 3 + col)] = at(row, col);
            t[static_cast<std::size_t>(9 + row)] = at(row, 3);
        }
        t[12] = 1.0;
        return t;
    }
    static M4 translate(const V3& d) {
        M4 r;
        r.at(0, 3) = d.x;
        r.at(1, 3) = d.y;
        r.at(2, 3) = d.z;
        return r;
    }
    // Rotation about an axis through c by angle a (radians).
    static M4 rotate(const V3& c, const V3& axis, double a) {
        const V3 k = axis.normalized();
        const double cs = std::cos(a), sn = std::sin(a), t = 1 - cs;
        M4 r;
        r.at(0, 0) = t * k.x * k.x + cs;
        r.at(0, 1) = t * k.x * k.y - sn * k.z;
        r.at(0, 2) = t * k.x * k.z + sn * k.y;
        r.at(1, 0) = t * k.x * k.y + sn * k.z;
        r.at(1, 1) = t * k.y * k.y + cs;
        r.at(1, 2) = t * k.y * k.z - sn * k.x;
        r.at(2, 0) = t * k.x * k.z - sn * k.y;
        r.at(2, 1) = t * k.y * k.z + sn * k.x;
        r.at(2, 2) = t * k.z * k.z + cs;
        return translate(c) * r * translate(-c);
    }
    M4 operator*(const M4& o) const {
        M4 r;
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 4; ++col) {
                double s = 0;
                for (int k = 0; k < 4; ++k) s += at(row, k) * o.at(k, col);
                r.at(row, col) = s;
            }
        }
        return r;
    }
    V3 point(const V3& p) const {
        const double w = at(3, 0) * p.x + at(3, 1) * p.y + at(3, 2) * p.z + at(3, 3);
        const double iw = std::abs(w) > 1e-300 ? 1.0 / w : 1.0;
        return {(at(0, 0) * p.x + at(0, 1) * p.y + at(0, 2) * p.z + at(0, 3)) * iw,
                (at(1, 0) * p.x + at(1, 1) * p.y + at(1, 2) * p.z + at(1, 3)) * iw,
                (at(2, 0) * p.x + at(2, 1) * p.y + at(2, 2) * p.z + at(2, 3)) * iw};
    }
    V3 vector(const V3& v) const {
        return {at(0, 0) * v.x + at(0, 1) * v.y + at(0, 2) * v.z, at(1, 0) * v.x + at(1, 1) * v.y + at(1, 2) * v.z,
                at(2, 0) * v.x + at(2, 1) * v.y + at(2, 2) * v.z};
    }
    double det3() const {
        return at(0, 0) * (at(1, 1) * at(2, 2) - at(1, 2) * at(2, 1)) -
               at(0, 1) * (at(1, 0) * at(2, 2) - at(1, 2) * at(2, 0)) +
               at(0, 2) * (at(1, 0) * at(2, 1) - at(1, 1) * at(2, 0));
    }
    // Normals transform with the inverse transpose of the 3x3 part.
    V3 normal(const V3& n) const {
        const auto inv = inverse();
        if (!inv) return vector(n).normalized();
        const M4& i = *inv;
        return V3{i.at(0, 0) * n.x + i.at(1, 0) * n.y + i.at(2, 0) * n.z,
                  i.at(0, 1) * n.x + i.at(1, 1) * n.y + i.at(2, 1) * n.z,
                  i.at(0, 2) * n.x + i.at(1, 2) * n.y + i.at(2, 2) * n.z}
            .normalized();
    }
    // Inverse of an affine transform (last row 0 0 0 1).
    std::optional<M4> inverse() const {
        const double d = det3();
        if (std::abs(d) < 1e-300) return std::nullopt;
        M4 r;
        r.at(0, 0) = (at(1, 1) * at(2, 2) - at(1, 2) * at(2, 1)) / d;
        r.at(0, 1) = (at(0, 2) * at(2, 1) - at(0, 1) * at(2, 2)) / d;
        r.at(0, 2) = (at(0, 1) * at(1, 2) - at(0, 2) * at(1, 1)) / d;
        r.at(1, 0) = (at(1, 2) * at(2, 0) - at(1, 0) * at(2, 2)) / d;
        r.at(1, 1) = (at(0, 0) * at(2, 2) - at(0, 2) * at(2, 0)) / d;
        r.at(1, 2) = (at(0, 2) * at(1, 0) - at(0, 0) * at(1, 2)) / d;
        r.at(2, 0) = (at(1, 0) * at(2, 1) - at(1, 1) * at(2, 0)) / d;
        r.at(2, 1) = (at(0, 1) * at(2, 0) - at(0, 0) * at(2, 1)) / d;
        r.at(2, 2) = (at(0, 0) * at(1, 1) - at(0, 1) * at(1, 0)) / d;
        const V3 t{at(0, 3), at(1, 3), at(2, 3)};
        const V3 it = r.vector(t);
        r.at(0, 3) = -it.x;
        r.at(1, 3) = -it.y;
        r.at(2, 3) = -it.z;
        return r;
    }
};

// SketchUp plane: n·p + d = 0.
struct Plane {
    V3 n{0, 0, 1};
    double d = 0;
    static Plane fromSkp(const std::array<double, 4>& p) { return {{p[0], p[1], p[2]}, p[3]}; }
    static Plane fromPointNormal(const V3& p, const V3& normal) {
        const V3 nn = normal.normalized();
        return {nn, -nn.dot(p)};
    }
    std::array<double, 4> toSkp() const { return {n.x, n.y, n.z, d}; }
    double dist(const V3& p) const { return n.dot(p) + d; }
    V3 origin() const { return n * -d; }
    V3 project(const V3& p) const { return p - n * dist(p); }
    std::optional<double> ray(const V3& o, const V3& dir) const {
        const double den = n.dot(dir);
        if (std::abs(den) < 1e-12) return std::nullopt;
        return -dist(o) / den;
    }
};

// Newell normal of a polygon (not normalized).
inline V3 newell(const std::vector<V3>& pts) {
    V3 n;
    for (std::size_t i = 0; i < pts.size(); ++i) {
        const V3& a = pts[i];
        const V3& b = pts[(i + 1) % pts.size()];
        n.x += (a.y - b.y) * (a.z + b.z);
        n.y += (a.z - b.z) * (a.x + b.x);
        n.z += (a.x - b.x) * (a.y + b.y);
    }
    return n;
}

// CUnitVector3d::GetArbitraryAxes (libCommonGeometry 0x31e7c): x = normalize(Z × n), or Y × n when
// IsParallelTo(Z) says n is parallel to Z (its tolerance is estimated here).
inline void arbitraryAxes(const V3& n, V3& xa, V3& ya) {
    const V3 nn = n.normalized();
    if (std::abs(nn.z) > 1.0 - 1e-10) {
        xa = V3{0, 1, 0}.cross(nn).normalized();
    } else {
        xa = V3{0, 0, 1}.cross(nn).normalized();
    }
    ya = nn.cross(xa).normalized();
}

struct Box {
    V3 lo{1e300, 1e300, 1e300}, hi{-1e300, -1e300, -1e300};
    bool empty() const { return lo.x > hi.x; }
    void add(const V3& p) {
        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
    }
    V3 center() const { return (lo + hi) * 0.5; }
    double radius() const { return empty() ? 0.0 : (hi - lo).length() * 0.5; }
};

// Closest point on segment ab to p; t in [0,1].
inline V3 closestOnSegment(const V3& p, const V3& a, const V3& b, double* tOut = nullptr) {
    const V3 ab = b - a;
    const double l2 = ab.dot(ab);
    double t = l2 > 1e-300 ? (p - a).dot(ab) / l2 : 0.0;
    t = std::clamp(t, 0.0, 1.0);
    if (tOut != nullptr) *tOut = t;
    return a + ab * t;
}

}  // namespace geom
