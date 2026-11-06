#include "ui/Units.h"

#include "skp/Query.h"

#include <QRegularExpression>

#include <cmath>

namespace ui {

namespace {

constexpr double kMmPerInch = 25.4;

std::optional<std::uint32_t> unitOption(const skp::SkpFile& f, const QString& key) {
    // COptionsManager record: u32, u32 n, then per group: name, (key, type, value)... "" ends a group.
    const skp::Record* r = skp::record(f, f.options);
    if (r == nullptr) return std::nullopt;
    for (std::size_t i = 0; i + 2 < r->fields.size(); ++i) {
        const auto* s = std::get_if<QString>(&r->fields[i]);
        if (s == nullptr || *s != key) continue;
        if (const auto* v = std::get_if<std::uint32_t>(&r->fields[i + 2])) return *v;
    }
    return std::nullopt;
}

QString trimNumber(double v, int decimals) {
    QString s = QString::number(v, 'f', decimals);
    if (s.contains(u'.')) {
        while (s.endsWith(u'0')) s.chop(1);
        if (s.endsWith(u'.')) s.chop(1);
    }
    return s;
}

}  // namespace

UnitFormat modelUnits(const skp::SkpFile& f) {
    const std::uint32_t format = unitOption(f, QStringLiteral("LengthFormat")).value_or(1);
    const std::uint32_t unit = unitOption(f, QStringLiteral("LengthUnit")).value_or(0);
    if (format == 1 || format == 3) return UnitFormat::Architectural;
    switch (unit) {
        case 1: return UnitFormat::Feet;
        case 2: return UnitFormat::Millimeters;
        case 3: return UnitFormat::Centimeters;
        case 4: return UnitFormat::Meters;
        default: return UnitFormat::Inches;
    }
}

QString formatLength(double inches, UnitFormat u) {
    const bool neg = inches < 0;
    const double a = std::abs(inches);
    QString s;
    switch (u) {
        case UnitFormat::Architectural: {
            const long long sixteenths = std::llround(a * 16.0);
            const long long feet = sixteenths / (12 * 16);
            const long long rest = sixteenths % (12 * 16);
            const long long whole = rest / 16;
            long long num = rest % 16, den = 16;
            while (num != 0 && num % 2 == 0) {
                num /= 2;
                den /= 2;
            }
            QString in = QString::number(whole);
            if (num != 0) in = whole != 0 ? QStringLiteral("%1 %2/%3").arg(whole).arg(num).arg(den)
                                          : QStringLiteral("%1/%2").arg(num).arg(den);
            s = feet != 0 ? QStringLiteral("%1' %2\"").arg(feet).arg(in) : in + u'"';
            break;
        }
        case UnitFormat::Inches: s = trimNumber(a, 2) + u'"'; break;
        case UnitFormat::Feet: s = trimNumber(a / 12.0, 2) + u'\''; break;
        case UnitFormat::Millimeters: s = trimNumber(a * kMmPerInch, 0) + QStringLiteral("mm"); break;
        case UnitFormat::Centimeters: s = trimNumber(a * kMmPerInch / 10.0, 1) + QStringLiteral("cm"); break;
        case UnitFormat::Meters: s = trimNumber(a * kMmPerInch / 1000.0, 2) + u'm'; break;
    }
    return neg ? u'-' + s : s;
}

QString formatArea(double sq, UnitFormat u) {
    switch (u) {
        case UnitFormat::Architectural:
        case UnitFormat::Feet: return trimNumber(sq / 144.0, 2) + QStringLiteral(" Feet²");
        case UnitFormat::Inches: return trimNumber(sq, 2) + QStringLiteral(" Inches²");
        case UnitFormat::Millimeters: return trimNumber(sq * kMmPerInch * kMmPerInch, 0) + QStringLiteral(" mm²");
        case UnitFormat::Centimeters: return trimNumber(sq * 6.4516, 1) + QStringLiteral(" cm²");
        case UnitFormat::Meters: return trimNumber(sq * 0.00064516, 2) + QStringLiteral(" m²");
    }
    return {};
}

std::optional<double> parseLength(const QString& textIn, UnitFormat u) {
    QString t = textIn.trimmed().replace(u',', u'.');
    if (t.isEmpty()) return std::nullopt;
    static const QRegularExpression feetInches(
        QStringLiteral(R"(^(-?\d+(?:\.\d*)?)\s*'\s*(?:(\d+(?:\.\d*)?)\s*"?)?$)"));
    if (auto m = feetInches.match(t); m.hasMatch()) {
        const double ft = m.captured(1).toDouble();
        const double in = m.captured(2).isEmpty() ? 0.0 : m.captured(2).toDouble();
        return ft * 12.0 + (ft < 0 ? -in : in);
    }
    static const QRegularExpression withUnit(QStringLiteral(R"(^(-?\d*\.?\d+)\s*(mm|cm|m|"|in|ft)?$)"));
    const auto m = withUnit.match(t);
    if (!m.hasMatch()) return std::nullopt;
    bool ok = false;
    const double v = m.captured(1).toDouble(&ok);
    if (!ok) return std::nullopt;
    const QString unit = m.captured(2);
    if (unit == QLatin1String("mm")) return v / kMmPerInch;
    if (unit == QLatin1String("cm")) return v * 10.0 / kMmPerInch;
    if (unit == QLatin1String("m")) return v * 1000.0 / kMmPerInch;
    if (unit == QLatin1String("ft")) return v * 12.0;
    if (unit == QLatin1String("\"") || unit == QLatin1String("in")) return v;
    switch (u) {
        case UnitFormat::Architectural:
        case UnitFormat::Inches: return v;
        case UnitFormat::Feet: return v * 12.0;
        case UnitFormat::Millimeters: return v / kMmPerInch;
        case UnitFormat::Centimeters: return v * 10.0 / kMmPerInch;
        case UnitFormat::Meters: return v * 1000.0 / kMmPerInch;
    }
    return v;
}

}  // namespace ui
