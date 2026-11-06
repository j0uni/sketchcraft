#pragma once
// Field positions inside settings records (MECHANICS.md sections 5-6).

#include "skp/Archive.h"

#include <cstddef>

namespace skp {

void typedValueFields(RecIO& r);

// CRenderingOptions record (fields 0-1 are the entity part).
namespace ro {
inline constexpr std::size_t kRenderMode = 2;       // 0x28
inline constexpr std::size_t kJitter = 5;           // 0x4b
inline constexpr std::size_t kEdgeDisplay = 6;      // 0x30
inline constexpr std::size_t kBackground = 7;       // 0x70
inline constexpr std::size_t kEdgeColor = 8;        // 0x74
inline constexpr std::size_t kHighlight = 9;        // 0x78
inline constexpr std::size_t kConstruction = 10;    // 0x80
inline constexpr std::size_t kTexture = 14;         // 0x2e
inline constexpr std::size_t kEdgeColorMode = 15;   // 0x40
inline constexpr std::size_t kExtensions = 16;      // 0x4c
inline constexpr std::size_t kExtensionLen = 17;    // 0x50
inline constexpr std::size_t kProfiles = 18;        // 0x54
inline constexpr std::size_t kProfileWidth = 19;    // 0x58
inline constexpr std::size_t kDepthCue = 20;        // 0x5c
inline constexpr std::size_t kEndpoints = 22;       // 0x64
inline constexpr std::size_t kHiddenGeometry = 25;  // 0x4a
inline constexpr std::size_t kFaceColorMode = 26;   // 0x44
inline constexpr std::size_t kFaceFront = 27;       // 0x88
inline constexpr std::size_t kFaceBack = 28;        // 0x84
inline constexpr std::size_t kSky = 44;             // 0xac
inline constexpr std::size_t kHorizon = 45;         // 0xb0
inline constexpr std::size_t kGround = 46;          // 0xb4
inline constexpr std::size_t kDrawSky = 47;         // 0xb8
inline constexpr std::size_t kDrawGround = 48;      // 0xb9
inline constexpr std::size_t kGroundTransparency = 50;  // 0xbc
inline constexpr std::size_t kModelTransparency = 3;    // 0x2c (X-ray)
inline constexpr std::size_t kAxes = 40;                // 0x38 (write order in Serialize 0xf6582)
inline constexpr std::size_t kBackEdges = 63;           // 0x6d (written for version 8+ saves)
}  // namespace ro

// CShadowInfo record.
namespace sh {
inline constexpr std::size_t kTime = 2;
inline constexpr std::size_t kCity = 4;
inline constexpr std::size_t kCountry = 5;
inline constexpr std::size_t kLatitude = 7;  // Boulder template: field 7 = 40.02
inline constexpr std::size_t kLongitude = 6;  // field 6 = -105.24
inline constexpr std::size_t kTimeZone = 8;
inline constexpr std::size_t kNorth = 9;  // 3 doubles
inline constexpr std::size_t kLight = 16;
inline constexpr std::size_t kDark = 17;
inline constexpr std::size_t kUseSun = 18;
}  // namespace sh

// CFaceTextureCoords record.
namespace tc {
inline constexpr std::size_t kFront = 3;      // 9 doubles
inline constexpr std::size_t kFrontProj = 12; // 3 doubles
inline constexpr std::size_t kBack = 15;      // 9 doubles
inline constexpr std::size_t kBackProj = 24;  // 3 doubles
}  // namespace tc

}  // namespace skp
