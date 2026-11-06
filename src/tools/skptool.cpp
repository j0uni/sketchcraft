// Command-line checker for the .skp reader/writer (not part of the app's UI).
//   skptool info <file>            counts of what the file holds
//   skptool roundtrip <file...>    read, save, read back and compare
//   skptool resave <in> <out>      read and save again
//   skptool record <file> <class>  print the fields of the first record of that class

#include "render/SceneBuilder.h"
#include "skp/Query.h"
#include "skp/SkpFile.h"

#include <QCoreApplication>
#include <QFile>
#include <QTextStream>

#include <iostream>

using namespace skp;

namespace {

QTextStream& out() {
    static QTextStream s(stdout);
    return s;
}

QString fieldText(const Field& f) {
    return std::visit(
        [](const auto& v) -> QString {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, bool>) return v ? QStringLiteral("B(true)") : QStringLiteral("B(false)");
            else if constexpr (std::is_same_v<T, Color>)
                return QStringLiteral("C(%1,%2,%3,%4)").arg(v.r).arg(v.g).arg(v.b).arg(v.a);
            else if constexpr (std::is_same_v<T, QString>) return QStringLiteral("S(\"%1\")").arg(v);
            else if constexpr (std::is_same_v<T, Ref>) return v ? QStringLiteral("R(#%1)").arg(*v) : QStringLiteral("R(null)");
            else if constexpr (std::is_same_v<T, QByteArray>) return QStringLiteral("Bytes(%1)").arg(v.size());
            else if constexpr (std::is_same_v<T, double> || std::is_same_v<T, float>) return QStringLiteral("F(%1)").arg(v);
            else return QStringLiteral("U(%1)").arg(static_cast<qulonglong>(v));
        },
        f);
}

void printStats(const QString& name, const SkpFile& f) {
    const Stats s = stats(f);
    out() << name << ": version " << f.appVersion << " objects " << s.objects << " faces " << s.faces << " edges "
          << s.edges << " instances " << s.instances << " groups " << s.groups << " definitions " << s.definitions
          << " materials " << s.materials << " layers " << s.layers << " scenes " << s.scenes << "\n";
}

int roundtrip(const QStringList& files) {
    int bad = 0;
    for (const QString& path : files) {
        auto r = readSkpFile(path);
        if (auto* e = std::get_if<LoadError>(&r)) {
            out() << "READ FAIL " << path << ": " << e->message << "\n";
            ++bad;
            continue;
        }
        SkpFile& f = std::get<SkpFile>(r);
        const Stats a = stats(f);
        auto bytes = writeSkp(f);
        if (auto* e = std::get_if<LoadError>(&bytes)) {
            out() << "SAVE FAIL " << path << ": " << e->message << "\n";
            ++bad;
            continue;
        }
        auto r2 = readSkp(std::get<QByteArray>(bytes));
        if (auto* e = std::get_if<LoadError>(&r2)) {
            out() << "REREAD FAIL " << path << ": " << e->message << "\n";
            ++bad;
            continue;
        }
        const Stats b = stats(std::get<SkpFile>(r2));
        const bool same = a.faces == b.faces && a.edges == b.edges && a.instances == b.instances &&
                          a.groups == b.groups && a.definitions == b.definitions && a.materials == b.materials &&
                          a.layers == b.layers && a.scenes == b.scenes;
        out() << (same ? "ok   " : "DIFF ") << path << " (" << a.faces << " faces, " << a.edges << " edges)\n";
        if (!same) ++bad;
    }
    out() << files.size() - bad << "/" << files.size() << " files round-trip\n";
    return bad == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() < 3) {
        std::cerr << "usage: skptool info|roundtrip|resave|record ...\n";
        return 2;
    }
    const QString cmd = args[1];
    if (cmd == QLatin1String("roundtrip")) {
        return roundtrip(args.mid(2));
    }
    auto r = readSkpFile(args[2]);
    if (auto* e = std::get_if<LoadError>(&r)) {
        out() << "READ FAIL: " << e->message << "\n";
        return 1;
    }
    SkpFile& f = std::get<SkpFile>(r);
    if (cmd == QLatin1String("info")) {
        printStats(args[2], f);
        for (ObjId m : materials(f)) {
            if (const auto* mat = f.get<Material>(m)) {
                out() << "  material " << mat->name << " " << mat->color.r << "," << mat->color.g << ","
                      << mat->color.b << " alpha " << materialRgba(*mat).a << (mat->texture ? " textured" : "")
                      << "\n";
            }
        }
        for (ObjId l : layers(f)) {
            if (const auto* lay = f.get<Layer>(l)) out() << "  layer " << lay->name << (lay->hidden ? " hidden" : "") << "\n";
        }
        return 0;
    }
    if (cmd == QLatin1String("uv")) {
        // Same listing as checks/uvcheck/uv.c prints from SketchUp's library.
        auto faces = [&](const Component* c, const QString& where) {
            if (c == nullptr) return;
            int index = 0;
            for (ObjId id : c->ents) {
                const auto* face = f.get<Face>(Ref{id});
                if (face == nullptr) continue;
                const int i = index++;
                const auto* m = f.get<Material>(face->de.material);
                const auto* t = m ? f.get<Texture>(m->texture) : nullptr;
                if (t == nullptr || face->loops.empty()) continue;
                const auto pts = loopPoints(f, face->loops.front());
                for (std::size_t k = 0; k < pts.size() && k < 3; ++k) {
                    const auto uv = render::textureUV(f, *face, *t, geom::V3::from(pts[k]), true, true);
                    out() << QStringLiteral("%1 %2 %3 %4 %5 -> %6 %7\n")
                                 .arg(where)
                                 .arg(i)
                                 .arg(pts[k][0], 0, 'f', 4)
                                 .arg(pts[k][1], 0, 'f', 4)
                                 .arg(pts[k][2], 0, 'f', 4)
                                 .arg(uv[0], 0, 'f', 5)
                                 .arg(uv[1], 0, 'f', 5);
                }
            }
        };
        faces(rootComponent(f), QStringLiteral("root"));
        const auto defs = definitions(f);
        for (std::size_t d = 0; d < defs.size(); ++d) faces(componentOf(f, defs[d]), QStringLiteral("def%1").arg(d));
        return 0;
    }
    if (cmd == QLatin1String("camera")) {
        if (const auto* c = f.get<Camera>(f.camera)) {
            out() << "eye " << c->eye[0] << " " << c->eye[1] << " " << c->eye[2] << " target " << c->target[0] << " "
                  << c->target[1] << " " << c->target[2] << " up " << c->up[0] << " " << c->up[1] << " " << c->up[2]
                  << " persp " << c->perspective << " fov " << c->fov << " height " << c->height << " aspect "
                  << c->aspect << " fovIsHeight " << c->fovIsHeight << "\n";
        }
        for (std::size_t i = 0; i < f.objs.size(); ++i) {
            if (const auto* l = std::get_if<Loop>(&f.objs[i])) {
                out() << "loop flags " << l->b0 << " " << l->b1 << "\n";
                break;
            }
        }
        for (ObjId l : layers(f)) {
            if (const auto* lay = f.get<Layer>(l)) {
                const auto* m = f.get<Material>(lay->material);
                out() << "layer " << lay->name << " u " << lay->u << " colour "
                      << (m ? QStringLiteral("%1,%2,%3 kind %4").arg(m->color.r).arg(m->color.g).arg(m->color.b).arg(m->kind)
                            : QStringLiteral("-"))
                      << "\n";
            }
        }
        return 0;
    }
    if (cmd == QLatin1String("resave") && args.size() >= 4) {
        auto w = writeSkpFile(f, args[3]);
        if (auto* e = std::get_if<LoadError>(&w)) {
            out() << "SAVE FAIL: " << e->message << "\n";
            return 1;
        }
        out() << "saved " << args[3] << "\n";
        return 0;
    }
    if (cmd == QLatin1String("record") && args.size() >= 4) {
        const std::string cls = args[3].toStdString();
        for (std::size_t i = 0; i < f.objs.size(); ++i) {
            const auto* rec = std::get_if<Record>(&f.objs[i]);
            if (rec == nullptr || rec->cls != cls) continue;
            out() << "#" << i << " " << args[3] << "\n";
            for (std::size_t k = 0; k < rec->fields.size(); ++k) out() << "  " << k << " " << fieldText(rec->fields[k]) << "\n";
            return 0;
        }
        out() << "no " << args[3] << "\n";
        return 1;
    }
    std::cerr << "unknown command\n";
    return 2;
}
