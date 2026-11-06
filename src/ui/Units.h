#pragma once
// Length display and typed lengths for the Measurements box. Model lengths are inches.

#include "skp/SkpFile.h"

#include <QString>

#include <optional>

namespace ui {

enum class UnitFormat { Architectural, Inches, Feet, Millimeters, Centimeters, Meters };

// From the model's UnitsOptions (LengthFormat 0 decimal / 1 architectural, LengthUnit 0 in, 1 ft, 2 mm,
// 3 cm, 4 m), as stored in COptionsManager.
UnitFormat modelUnits(const skp::SkpFile& f);

QString formatLength(double inches, UnitFormat u);
QString formatArea(double squareInches, UnitFormat u);
// Accepts 10' 6", 10'6, 6", 2m, 250mm, 12.5cm, or a bare number in the model's unit.
std::optional<double> parseLength(const QString& text, UnitFormat u);

}  // namespace ui
