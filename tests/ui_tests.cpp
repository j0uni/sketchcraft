// Drives the real window with mouse clicks and typed values: draw a rectangle, push/pull it into a box,
// undo and redo, select and erase, save and read the file back.

#include "render/Viewport.h"
#include "skp/Builder.h"
#include "skp/Query.h"
#include "ui/Document.h"
#include "ui/MainWindow.h"
#include "ui/PluginPath.h"

#include <QAction>
#include <QApplication>
#include <QLineEdit>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTest>

using namespace skp;

namespace {

QAction* actionNamed(QWidget& w, const QString& text) {
    for (QAction* a : w.findChildren<QAction*>()) {
        if (a->text() == text) return a;
    }
    return nullptr;
}

std::size_t rootFaces(const SkpFile& f) {
    std::size_t n = 0;
    if (const auto* r = rootComponent(f)) {
        for (ObjId id : r->ents) n += f.get<Face>(Ref{id}) != nullptr ? 1 : 0;
    }
    return n;
}

}  // namespace

class UiTests : public QObject {
    Q_OBJECT

private slots:
    void drawBoxWithTools() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        SkpFile empty = newModel();
        const QString start = dir.filePath(QStringLiteral("start.skp"));
        QVERIFY(std::holds_alternative<bool>(writeSkpFile(empty, start)));

        ui::MainWindow w;
        w.resize(1100, 760);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        w.startWith(start);
        auto* doc = w.findChild<ui::Document*>();
        auto* view = w.findChild<render::Viewport*>();
        auto* measure = w.statusBar()->findChild<QLineEdit*>();
        QVERIFY(doc != nullptr && view != nullptr && measure != nullptr);
        render::View v;
        v.eye = {300, -400, 350};
        v.target = {50, 25, 0};
        view->setView(v);
        QTest::qWait(100);

        // Rectangle: two clicks on the ground.
        QAction* rect = actionNamed(w, QStringLiteral("Rectangle"));
        QVERIFY(rect != nullptr);
        rect->trigger();
        const QPoint p0 = view->project({0, 0, 0})->toPoint();
        const QPoint p1 = view->project({100, 50, 0})->toPoint();
        QTest::mouseMove(view, p0);
        QTest::mouseClick(view, Qt::LeftButton, {}, p0);
        QTest::mouseMove(view, p1);
        QTest::mouseClick(view, Qt::LeftButton, {}, p1);
        QTest::qWait(50);
        QCOMPARE(rootFaces(doc->model()), std::size_t{1});

        // Push/pull: press on the face, type 40, Enter.
        actionNamed(w, QStringLiteral("Push/Pull"))->trigger();
        const QPoint mid = view->project({50, 25, 0})->toPoint();
        QTest::mouseMove(view, mid);
        QTest::mouseClick(view, Qt::LeftButton, {}, mid);
        measure->setText(QStringLiteral("40"));
        QMetaObject::invokeMethod(measure, "returnPressed");
        QTest::qWait(50);
        QCOMPARE(rootFaces(doc->model()), std::size_t{6});

        // Undo and redo.
        doc->undo();
        QCOMPARE(rootFaces(doc->model()), std::size_t{1});
        doc->redo();
        QCOMPARE(rootFaces(doc->model()), std::size_t{6});

        // Select a face by clicking it, then erase with the Delete command.
        actionNamed(w, QStringLiteral("Select"))->trigger();
        QTest::qWait(50);
        const auto hit = view->pick(QPointF(mid), false);
        QVERIFY(hit.has_value());
        QTest::mouseClick(view, Qt::LeftButton, {}, mid);
        QCOMPARE(doc->selection().size(), std::size_t{1});
        QAction* del = actionNamed(w, QStringLiteral("Delete"));
        QVERIFY(del != nullptr);
        del->trigger();
        QCOMPARE(rootFaces(doc->model()), std::size_t{5});
        doc->undo();
        QCOMPARE(rootFaces(doc->model()), std::size_t{6});

        // Offset the top face inward by a typed 5".
        actionNamed(w, QStringLiteral("Offset"))->trigger();
        const QPoint top = view->project({50, 25, 40})->toPoint();
        QTest::mouseMove(view, top);
        QTest::mouseClick(view, Qt::LeftButton, {}, top);
        measure->setText(QStringLiteral("5"));
        QMetaObject::invokeMethod(measure, "returnPressed");
        QCOMPARE(rootFaces(doc->model()), std::size_t{7});
        doc->undo();

        // Rotate everything 90 degrees about the origin: the corner (100,0,0) moves to (0,100,0).
        actionNamed(w, QStringLiteral("Select All"))->trigger();
        actionNamed(w, QStringLiteral("Rotate"))->trigger();
        const QPoint origin = view->project({0, 0, 0})->toPoint();
        const QPoint ref = view->project({100, 0, 0})->toPoint();
        QTest::mouseMove(view, origin);
        QTest::mouseClick(view, Qt::LeftButton, {}, origin);
        QTest::mouseMove(view, ref);
        QTest::mouseClick(view, Qt::LeftButton, {}, ref);
        measure->setText(QStringLiteral("90"));
        QMetaObject::invokeMethod(measure, "returnPressed");
        bool turned = false;
        for (const Object& o : doc->model().objs) {
            if (const auto* vx = std::get_if<Vertex>(&o)) {
                turned = turned || (std::abs(vx->pos[0]) < 1e-6 && std::abs(vx->pos[1] - 100) < 0.5 && std::abs(vx->pos[2]) < 1e-6);
            }
        }
        QVERIFY(turned);
        doc->undo();

        // Orbiting keeps the camera upright (the original re-levels after every step).
        view->beginOrbit(QPointF(view->width() / 2.0, view->height() / 2.0));
        const auto before = view->view().eye;
        view->orbitBy(60, 0);
        view->orbitBy(0, 40);
        view->endOrbit();
        QVERIFY(view->view().eye.dist(before) > 1);
        QVERIFY(view->view().up.z > 0.5);

        // The original's single-key shortcuts.
        actionNamed(w, QStringLiteral("Rectangle"))->setChecked(false);
        QAction* rectKey = actionNamed(w, QStringLiteral("Rectangle"));
        QVERIFY(rectKey->shortcuts().contains(QKeySequence(Qt::Key_R)));
        QVERIFY(actionNamed(w, QStringLiteral("Push/Pull"))->shortcuts().contains(QKeySequence(Qt::Key_P)));
        QVERIFY(actionNamed(w, QStringLiteral("Zoom Extents"))->shortcuts().contains(QKeySequence(Qt::SHIFT | Qt::Key_Z)));

        // Save and read back.
        const QString out = dir.filePath(QStringLiteral("box.skp"));
        QVERIFY(!doc->save(out).has_value());
        auto back = readSkpFile(out);
        QVERIFY(std::holds_alternative<SkpFile>(back));
        QCOMPARE(rootFaces(std::get<SkpFile>(back)), std::size_t{6});
        if (const QString keep = qEnvironmentVariable("SKETCHCRAFT_OUT"); !keep.isEmpty()) {
            QFile::remove(keep + QStringLiteral("/tool-box.skp"));
            QFile::copy(out, keep + QStringLiteral("/tool-box.skp"));
        }
        QVERIFY(view->framesDrawn() > 0);
    }
};

int main(int argc, char** argv) {
    ui::addBuildTimePluginPath();
    QApplication app(argc, argv);
    UiTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "ui_tests.moc"
