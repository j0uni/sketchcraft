// Checks for the .skp reader/writer, model edits, triangulation and texture placement.
// Set SKETCHCRAFT_TEMPLATES to a folder of .skp files to also round-trip those.

#include "geom/Triangulate.h"
#include "render/SceneBuilder.h"
#include "render/Sun.h"
#include "skp/Builder.h"
#include "skp/Query.h"
#include "skp/SkpFile.h"
#include "ui/PluginPath.h"
#include "ui/Units.h"

#include <QDir>
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QTest>

#include <numbers>

using namespace skp;

class SketchTests : public QObject {
    Q_OBJECT

private slots:
    void triangulateSquareWithHole() {
        const std::vector<geom::P2> outer{{0, 0}, {10, 0}, {10, 10}, {0, 10}};
        const std::vector<std::vector<geom::P2>> holes{{{3, 3}, {3, 7}, {7, 7}, {7, 3}}};
        const auto tris = geom::triangulate(outer, holes);
        QCOMPARE(tris.size(), std::size_t{8});
        double area = 0;
        std::vector<geom::P2> pts = outer;
        pts.insert(pts.end(), holes[0].begin(), holes[0].end());
        for (const auto& t : tris) area += geom::signedArea({pts[t[0]], pts[t[1]], pts[t[2]]});
        QVERIFY(std::abs(area - 84.0) < 1e-9);
    }

    void newModelBoxRoundTrip() {
        SkpFile f = newModel();
        QVERIFY(f.root.has_value());
        QVERIFY(layer0(f).has_value());
        const ObjId root = *f.root;
        auto face = addFace(f, root, {{0, 0, 0}, {100, 0, 0}, {100, 50, 0}, {0, 50, 0}}, {}, geom::V3{0, 0, -1});
        QVERIFY(face.has_value());
        QVERIFY(pushPull(f, root, *face, -40));  // pull up: the face looks down
        QCOMPARE(stats(f).faces, std::size_t{6});
        QCOMPARE(stats(f).edges, std::size_t{12});
        const ObjId mat = addMaterial(f, QStringLiteral("Brick"), Color{180, 80, 60, 255});
        paint(f, *face, mat);
        addScene(f, QStringLiteral("Scene 1"), *f.get<Camera>(f.camera));

        auto bytes = writeSkp(f);
        QVERIFY(std::holds_alternative<QByteArray>(bytes));
        auto back = readSkp(std::get<QByteArray>(bytes));
        QVERIFY2(std::holds_alternative<SkpFile>(back),
                 std::holds_alternative<LoadError>(back) ? qPrintable(std::get<LoadError>(back).message) : "");
        const SkpFile& g = std::get<SkpFile>(back);
        QCOMPARE(stats(g).faces, std::size_t{6});
        QCOMPARE(stats(g).edges, std::size_t{12});
        QCOMPARE(stats(g).materials, std::size_t{1});
        QCOMPARE(stats(g).scenes, std::size_t{1});

        // Every face points out of the box.
        const geom::V3 centre{50, 25, 20};
        for (const Object& o : g.objs) {
            if (const auto* fc = std::get_if<Face>(&o)) {
                const geom::Plane pl = geom::Plane::fromSkp(fc->plane);
                QVERIFY2(pl.dist(centre) < 0, "face normal points inwards");
            }
        }
        if (const QString dir = qEnvironmentVariable("SKETCHCRAFT_OUT"); !dir.isEmpty()) {
            QVERIFY(std::holds_alternative<bool>(writeSkpFile(f, dir + QStringLiteral("/box.skp"))));
        }
    }

    void pushPullMovesBoxTop() {
        SkpFile f = newModel();
        const ObjId root = *f.root;
        auto face = addFace(f, root, {{0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {0, 10, 0}}, {}, geom::V3{0, 0, -1});
        QVERIFY(pushPull(f, root, *face, -10));
        // Find the top face and pull it 5 more.
        std::optional<ObjId> top;
        for (ObjId id : rootComponent(f)->ents) {
            if (const auto* fc = f.get<Face>(Ref{id})) {
                const geom::Plane pl = geom::Plane::fromSkp(fc->plane);
                if (pl.n.z > 0.99) top = id;
            }
        }
        QVERIFY(top.has_value());
        QVERIFY(pushPull(f, root, *top, 5));
        QCOMPARE(stats(f).faces, std::size_t{6});
        const auto* fc = f.get<Face>(top);
        QVERIFY(std::abs(geom::Plane::fromSkp(fc->plane).dist({0, 0, 15})) < 1e-9);
    }

    void scenePlanesMatchLoops() {
        const QString dir = qEnvironmentVariable("SKETCHCRAFT_TEMPLATES");
        if (dir.isEmpty()) QSKIP("SKETCHCRAFT_TEMPLATES not set");
        const QStringList files = QDir(dir).entryList({QStringLiteral("*.skp")}, QDir::Files);
        QVERIFY(!files.isEmpty());
        for (const QString& name : files) {
            auto r = readSkpFile(dir + u'/' + name);
            QVERIFY2(std::holds_alternative<SkpFile>(r), qPrintable(name));
            const SkpFile& f = std::get<SkpFile>(r);
            // Stored planes satisfy n·p + d = 0 at the loop corners, and the outer loop winds
            // counter-clockwise about n.
            for (const Object& o : f.objs) {
                const auto* face = std::get_if<Face>(&o);
                if (face == nullptr || face->loops.empty()) continue;
                const geom::Plane pl = geom::Plane::fromSkp(face->plane);
                const Loop3 outer = faceOuter(f, *face);
                for (const geom::V3& p : outer) QVERIFY(std::abs(pl.dist(p)) < 1e-2);
                QVERIFY(geom::newell(outer).dot(pl.n) > 0);
            }
            render::BuildOptions opt;
            opt.style = render::readStyle(f);
            const render::Scene sc = render::buildScene(f, opt);
            QVERIFY2(sc.faceCount == 0 || !sc.bounds.empty(), qPrintable(name));
        }
    }

    void sunIsUpAtNoon() {
        // Boulder, 21 June 2016 12:00 local (UTC-7): the sun is high in the south.
        render::SunInput in;
        in.localSeconds = 1466510400;  // 2016-06-21 12:00 counted as UTC
        in.timeZoneHours = -7;
        in.latitudeDeg = 40.0183;
        in.longitudeDeg = -105.242;
        const geom::V3 s = render::sunDirection(in);
        QVERIFY(s.z > 0.9);
        QVERIFY(s.y < 0);
    }

    void unitsFormatAndParse() {
        QCOMPARE(ui::formatLength(126.0, ui::UnitFormat::Architectural), QStringLiteral("10' 6\""));
        QCOMPARE(ui::formatLength(100.0 / 25.4, ui::UnitFormat::Millimeters), QStringLiteral("100mm"));
        QCOMPARE(ui::parseLength(QStringLiteral("10'6"), ui::UnitFormat::Architectural).value_or(-1), 126.0);
        QCOMPARE(ui::parseLength(QStringLiteral("2m"), ui::UnitFormat::Architectural).value_or(-1),
                 2000.0 / 25.4);
        QCOMPARE(ui::parseLength(QStringLiteral("12"), ui::UnitFormat::Architectural).value_or(-1), 12.0);
        QVERIFY(!ui::parseLength(QStringLiteral("abc"), ui::UnitFormat::Architectural).has_value());
    }
};

int main(int argc, char** argv) {
    ui::addBuildTimePluginPath();
    QGuiApplication app(argc, argv);
    SketchTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "sketchcraft_tests.moc"
