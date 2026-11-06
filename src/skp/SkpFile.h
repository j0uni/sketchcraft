#pragma once
// A whole .skp model: header fields plus the object arena (MECHANICS.md section 2).

#include "skp/Model.h"

#include <QByteArray>
#include <QString>

#include <array>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace skp {

struct SkpFile {
    std::vector<Object> objs;
    QString header;
    QString appVersion;
    Guid guid{};
    std::uint32_t time = 0;
    std::vector<std::pair<std::string, std::uint32_t>> versionMap;
    std::uint32_t ua = 2, ub = 0x4b0, license = 0;
    std::uint64_t nextPid = 1;
    Ref thumbnail;
    bool thumbFlag = false;
    Ref behavior;
    QString description;
    Ref options, attrs, dib2, camera, rendering;
    std::uint32_t vertexCount = 0, uAfter = 0;
    Ref root, shadow, pages, axes;
    std::array<std::uint32_t, 16> sixteen{};
    Ref dimStyle, textStyle, fonts, background, styles, watermarks;
    std::uint32_t uTail = 0;
    bool bTail = false;
    std::vector<Ref> schemas;
    std::map<std::string, std::uint32_t> loadedVersions;

    // Typed access helpers (nullptr when the slot is empty or of another type).
    template <class T>
    T* get(Ref r) {
        return (r && *r < objs.size()) ? std::get_if<T>(&objs[*r]) : nullptr;
    }
    template <class T>
    const T* get(Ref r) const {
        return (r && *r < objs.size()) ? std::get_if<T>(&objs[*r]) : nullptr;
    }
    ObjId add(Object o) {
        objs.push_back(std::move(o));
        return objs.size() - 1;
    }
    std::uint64_t takePid() { return nextPid++; }
};

struct LoadError {
    QString message;
};

using LoadResult = std::variant<SkpFile, LoadError>;

LoadResult readSkp(const QByteArray& data);
LoadResult readSkpFile(const QString& path);
// Saves in the SketchUp 2017 layout; returns the bytes or an error.
// fillMissing: settings records without fields get zero values (used to make a new model).
std::variant<QByteArray, LoadError> writeSkp(SkpFile& f, bool fillMissing = false);
std::variant<bool, LoadError> writeSkpFile(SkpFile& f, const QString& path);

// Class versions a 2017 save writes.
const std::map<std::string, std::uint32_t>& versions2017();

}  // namespace skp
