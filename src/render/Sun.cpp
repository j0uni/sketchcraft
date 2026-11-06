#include "render/Sun.h"

#include <cmath>
#include <numbers>

namespace render {

namespace {
constexpr double kDeg = std::numbers::pi / 180.0;
}

SunAngles sunAngles(std::int64_t utcSeconds, double latitudeDeg, double longitudeDeg) {
    // Low-precision solar position (estimated stand-in for CSunPositionFinder::CalcAltitudeAndAzimuth
    // 0x127f77; agrees to about a tenth of a degree).
    const double jd = static_cast<double>(utcSeconds) / 86400.0 + 2440587.5;
    const double n = jd - 2451545.0;
    const double meanLong = std::fmod(280.460 + 0.9856474 * n, 360.0);
    const double anomaly = std::fmod(357.528 + 0.9856003 * n, 360.0) * kDeg;
    const double eclLong = (meanLong + 1.915 * std::sin(anomaly) + 0.020 * std::sin(2 * anomaly)) * kDeg;
    const double obliq = (23.439 - 0.0000004 * n) * kDeg;
    const double ra = std::atan2(std::cos(obliq) * std::sin(eclLong), std::cos(eclLong));
    const double dec = std::asin(std::sin(obliq) * std::sin(eclLong));
    const double gmstHours = std::fmod(18.697374558 + 24.06570982441908 * n, 24.0);
    const double lst = gmstHours * 15.0 * kDeg + longitudeDeg * kDeg;
    const double ha = lst - ra;
    const double lat = latitudeDeg * kDeg;
    const double alt = std::asin(std::sin(lat) * std::sin(dec) + std::cos(lat) * std::cos(dec) * std::cos(ha));
    const double az = std::atan2(-std::sin(ha), std::tan(dec) * std::cos(lat) - std::sin(lat) * std::cos(ha));
    return {alt, az < 0 ? az + 2 * std::numbers::pi : az};
}

geom::V3 sunDirection(const SunInput& in) {
    // CShadowInfo::CalcSunDirection 0x10b3a2: the stored time is local; UTC = time - zone * 3600.
    const auto utc = in.localSeconds - static_cast<std::int64_t>(std::llround(in.timeZoneHours * 3600.0));
    const SunAngles a = sunAngles(utc, in.latitudeDeg, in.longitudeDeg);
    // CSunPositionFinder::CalcSunDirection 0x12837c: (0, cos alt, sin alt) turned clockwise by the azimuth,
    // then by the north angle atan2(-north.x, north.y).
    const double ca = std::cos(a.altitude);
    geom::V3 v{ca * std::sin(a.azimuth), ca * std::cos(a.azimuth), std::sin(a.altitude)};
    const double north = std::atan2(-in.north.x, in.north.y);
    const double c = std::cos(north), s = std::sin(north);
    return geom::V3{v.x * c - v.y * s, v.x * s + v.y * c, v.z}.normalized();
}

}  // namespace render
