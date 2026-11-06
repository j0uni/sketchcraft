#include "skp/SkpFile.h"

#include "skp/Archive.h"

#include <QDateTime>
#include <QFile>
#include <QSaveFile>

namespace skp {

namespace {

const QString kHeader = QStringLiteral("SketchUp Model");
const QString kWriteVersion = QStringLiteral("{17.0.10349}");

Object makeRecord(const char* cls) {
    return Object{Record{cls, {}}};
}

std::function<Object()> rec(const char* cls) {
    return [cls] { return makeRecord(cls); };
}

// CSketchUpModel::Serialize 0x11563a with the header of CVersionInfo::Serialize 0x137ef6.
void ioModel(Archive& ar, SkpFile& f) {
    ar.string(f.header);
    if (ar.reading() && f.header != kHeader) {
        throw SkpError("this is not a SketchUp model");
    }
    ar.string(f.appVersion);
    if (ar.reading()) {
        const QStringList parts = QString(f.appVersion).remove(u'{').remove(u'}').split(u'.');
        bool ok = parts.size() == 3;
        const int major = ok ? parts[0].toInt(&ok) : 0;
        if (!ok) {
            throw SkpError("unreadable version text in the file header");
        }
        if (major > 17) {
            throw SkpError("this model was saved by a newer SketchUp (" + f.appVersion.toStdString() +
                           "); save it as SketchUp 2017 there first");
        }
    }
    ar.guid(f.guid);
    QString empty;
    ar.string(empty);
    ar.u32(f.time);

    if (ar.reading()) {
        Ref vm;
        ar.ref(vm);
        const auto* m = vm ? std::get_if<VersionMap>(&ar.arena().at(*vm)) : nullptr;
        if (m == nullptr) {
            throw SkpError("the file has no class version table");
        }
        f.versionMap = m->entries;
        std::map<std::string, std::uint32_t> vers(f.versionMap.begin(), f.versionMap.end());
        f.loadedVersions = vers;
        ar.setVersions(std::move(vers));
        ar.arena()[*vm] = std::monostate{};
    } else {
        VersionMap m;
        for (const auto& [k, v] : versions2017()) {
            m.entries.emplace_back(k, v);
        }
        Ref vm = ar.alloc(Object{std::move(m)});
        ar.ref(vm);
        ar.arena()[*vm] = std::monostate{};
    }

    const std::uint32_t mv = ar.ver("CSketchUpModel");
    if (ar.reading() && mv > 26) {
        throw SkpError("model format " + std::to_string(mv) + " is newer than SketchUp 2017");
    }
    if (mv < 4) {
        throw SkpError("models from SketchUp 3 or older are not supported");
    }
    if (mv >= 7) {
        ar.u32(f.ua);
        ar.u32(f.ub);
        if (mv >= 18) ar.u32(f.license);
        if (mv >= 26) ar.u64(f.nextPid);
    }
    ar.ref(f.thumbnail);
    ar.boolean(f.thumbFlag);
    ar.inlineObj(f.behavior, rec("CComponentBehavior"));
    ar.string(f.description);
    if (mv >= 21) {
        ar.inlineObj(f.options, rec("COptionsManager"));
        ar.ref(f.attrs);
        ar.ref(f.dib2);
    }
    ar.ref(f.camera);
    ar.inlineObj(f.rendering, rec("CRenderingOptions"));
    if (!ar.reading()) {
        f.vertexCount = 0;
    }
    ar.u32(f.vertexCount);
    if (ar.reading()) {
        for (std::uint32_t i = 0; i < f.vertexCount && i < 100000000; ++i) {
            Ref x;
            ar.ref(x);
        }
    }
    if (mv >= 17) ar.u32(f.uAfter);
    ar.inlineObj(f.root, [] { return Object{Component{}}; });
    if (mv >= 2) ar.inlineObj(f.shadow, rec("CShadowInfo"));
    if (mv >= 12) {
        ar.inlineObj(f.pages, rec("CPageList"));
    } else if (ar.reading()) {
        Record pl{"CPageList", {}};
        pl.fields.emplace_back(Ref{});
        pl.fields.emplace_back(std::uint64_t{0});
        std::uint32_t n = 0;
        ar.u32(n);
        pl.fields.emplace_back(n);
        for (std::uint32_t i = 0; i < n && i < 100000; ++i) {
            Ref x;
            ar.ref(x);
            pl.fields.emplace_back(x);
        }
        pl.fields.emplace_back(Ref{});
        f.pages = ar.alloc(Object{std::move(pl)});
    }
    if (mv <= 5 && ar.reading()) {
        const std::uint32_t n = ar.legacyU32();
        for (std::uint32_t i = 0; i < n && i < 100000; ++i) {
            QString s;
            ar.string(s);
        }
    }
    ar.inlineObj(f.axes, rec("CSketchCS"));
    for (std::uint32_t& x : f.sixteen) {
        ar.u32(x);
    }
    if (ar.reading() && mv >= 5 && mv <= 20) {
        ar.inlineObj(f.options, rec("COptionsManager"));
    }
    if (mv >= 8) {
        ar.inlineObj(f.dimStyle, rec("CDimensionStyle"));
        if (mv >= 10) ar.inlineObj(f.textStyle, rec("CTextStyle"));
        if (mv >= 9) ar.inlineObj(f.fonts, rec("CFontManager"));
    }
    if (mv == 13 || mv >= 15) {
        ar.ref(f.background);
    }
    if (mv >= 14) {
        ar.inlineObj(f.styles, rec("CSkpStyleManager"));
        if (mv >= 16) {
            ar.inlineObj(f.watermarks, rec("CWatermarkManager"));
            if (mv >= 20) ar.u32(f.uTail);
            if (mv == 19) ar.legacyBool();
            if (mv >= 22) ar.boolean(f.bTail);
            if (mv == 23 && ar.reading()) {
                const std::uint32_t n = ar.legacyU32();
                for (std::uint32_t i = 0; i < n && i < 100000; ++i) ar.legacyRef();
            }
            if (mv >= 24) {
                auto n = static_cast<std::uint32_t>(f.schemas.size());
                ar.u32(n);
                if (ar.reading()) {
                    if (n > 100000) throw SkpError("too many extension schemas");
                    f.schemas.assign(n, std::nullopt);
                }
                for (Ref& s : f.schemas) ar.ref(s);
            }
        }
    }
}

}  // namespace

const std::map<std::string, std::uint32_t>& versions2017() {
    // Version map of a file saved by SketchUp 2017 (CVersionMap::WriteV17VersionMap).
    static const std::map<std::string, std::uint32_t> m{
        {"CArcCurve", 3}, {"CAttribute", 0}, {"CAttributeContainer", 0}, {"CAttributeNamed", 1},
        {"CBackgroundImage", 10}, {"CCamera", 5}, {"CComponent", 11}, {"CComponentBehavior", 5},
        {"CComponentDefinition", 10}, {"CComponentInstance", 5}, {"CConstructionGeometry", 0},
        {"CConstructionLine", 1}, {"CConstructionPoint", 0}, {"CCurve", 4}, {"CDefinitionList", 0},
        {"CDib", 3}, {"CDimension", 1}, {"CDimensionLinear", 6}, {"CDimensionRadial", 2},
        {"CDimensionStyle", 4}, {"CDrawingElement", 9}, {"CEdge", 2}, {"CEdgeUse", 1}, {"CEntity", 5},
        {"CFace", 3}, {"CFaceTextureCoords", 4}, {"CFontManager", 0}, {"CGroup", 1}, {"CImage", 1},
        {"CLayer", 2}, {"CLayerManager", 4}, {"CLoop", 1}, {"CMaterial", 12}, {"CMaterialManager", 4},
        {"CPageList", 1}, {"CPolyline3d", 0}, {"CRelationship", 0}, {"CRelationshipMap", 0},
        {"CRenderingOptions", 36}, {"CSchemaFile", 1}, {"CSchemaFilterFile", 0}, {"CSchemaZipFile", 1},
        {"CSectionPlane", 2}, {"CShadowInfo", 7}, {"CSkFont", 1}, {"CSketchCS", 0}, {"CSketchUpModel", 26},
        {"CSketchUpPage", 1}, {"CSkpStyle", 1}, {"CSkpStyleManager", 2}, {"CText", 9}, {"CTextStyle", 5},
        {"CTexture", 6}, {"CThumbnail", 1}, {"CVertex", 0}, {"CViewPage", 12}, {"CWatermark", 1},
        {"CWatermarkManager", 2},
    };
    return m;
}

LoadResult readSkp(const QByteArray& data) {
    SkpFile f;
    try {
        Archive ar(data, f.objs);
        ioModel(ar, f);
        if (ar.remaining() > 0) {
            return LoadError{QStringLiteral("%1 unexpected bytes after the model").arg(ar.remaining())};
        }
    } catch (const SkpError& e) {
        return LoadError{QString::fromStdString(e.what())};
    } catch (const std::exception& e) {
        return LoadError{QStringLiteral("could not read the model: %1").arg(QString::fromStdString(e.what()))};
    }
    return f;
}

LoadResult readSkpFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return LoadError{QStringLiteral("could not open %1: %2").arg(path, file.errorString())};
    }
    return readSkp(file.readAll());
}

std::variant<QByteArray, LoadError> writeSkp(SkpFile& f, bool fillMissing) {
    // A file written by a 2017 release with the 2017 class table keeps its version text.
    bool keep = f.appVersion.startsWith(QLatin1String("{17."));
    for (const auto& [k, v] : f.versionMap) {
        const auto it = versions2017().find(k);
        keep = keep && it != versions2017().end() && it->second == v;
    }
    const QString savedVersion = f.appVersion;
    if (!keep) {
        f.appVersion = kWriteVersion;
    }
    f.header = kHeader;
    f.ua = 2;
    f.ub = 0x4b0;
    f.time = static_cast<std::uint32_t>(QDateTime::currentSecsSinceEpoch());
    try {
        Archive ar(f.objs);
        ar.setVersions(versions2017());
        ar.setFill(fillMissing);
        ioModel(ar, f);
        f.appVersion = savedVersion;
        return ar.output();
    } catch (const std::exception& e) {
        f.appVersion = savedVersion;
        return LoadError{QStringLiteral("could not save: %1").arg(QString::fromStdString(e.what()))};
    }
}

std::variant<bool, LoadError> writeSkpFile(SkpFile& f, const QString& path) {
    auto bytes = writeSkp(f);
    if (auto* err = std::get_if<LoadError>(&bytes)) {
        return *err;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return LoadError{QStringLiteral("could not write %1: %2").arg(path, file.errorString())};
    }
    file.write(std::get<QByteArray>(bytes));
    if (!file.commit()) {
        return LoadError{QStringLiteral("could not write %1: %2").arg(path, file.errorString())};
    }
    return true;
}

}  // namespace skp
