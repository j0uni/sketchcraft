#pragma once
// Sun position for face shading (MECHANICS.md section 6).

#include "geom/Math.h"

#include <cstdint>

namespace render {

struct SunAngles {
    double altitude = 0;  // radians above the horizon
    double azimuth = 0;   // radians clockwise from north
};

struct SunInput {
    std::int64_t localSeconds = 0;  // CShadowInfo time: local clock time counted like UTC seconds
    double timeZoneHours = 0;
    double latitudeDeg = 0, longitudeDeg = 0;
    geom::V3 north{0, 1, 0};
};

SunAngles sunAngles(std::int64_t utcSeconds, double latitudeDeg, double longitudeDeg);
geom::V3 sunDirection(const SunInput& in);

}  // namespace render
