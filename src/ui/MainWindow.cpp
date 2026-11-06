#include "ui/MainWindow.h"

#include "skp/Builder.h"
#include "skp/Query.h"
#include "ui/Icons.h"

#include <QApplication>
#include <QScreen>
#include <QWindow>
#include <QCloseEvent>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QRegularExpression>
#include <QStatusBar>
#include <QTabBar>
#include <QTimer>
#include <QToolBar>
#include <QToolBox>
#include <QVBoxLayout>

#include <cmath>
#include <numbers>

namespace ui {

using edit::ToolId;

namespace {

constexpr const char* kAppName = "SketchCraft";

render::View viewFromCamera(const skp::Camera& c, double windowAspect) {
    render::View v;
    v.eye = geom::V3::from(c.eye);
    v.target = geom::V3::from(c.target);
    v.up = geom::V3::from(c.up);
    if (v.up.length() < 1e-9) v.up = {0, 0, 1};
    v.perspective = c.perspective;
    // CCamera::GetHeightFieldOfView 0x633fc: the stored angle is the height angle unless flagged otherwise.
    double fov = c.fov > 0.1 && c.fov < 179 ? c.fov : 35.0;
    if (!c.fovIsHeight) {
        const double aspect = c.aspect > 1e-6 ? c.aspect : windowAspect;
        fov = 2.0 * std::atan(std::tan(fov * std::numbers::pi / 360.0) / aspect) * 360.0 / (2 * std::numbers::pi);
    }
    v.fovDeg = fov;
    v.orthoHeight = c.height > 1e-6 ? c.height : 100.0;
    if ((v.target - v.eye).length() < 1e-9) v.target = v.eye + geom::V3{0, 1, 0};
    return v;
}

void viewToCamera(const render::View& v, skp::Camera& c) {
    c.eye = v.eye.arr();
    c.target = v.target.arr();
    c.up = v.up.arr();
    c.perspective = v.perspective;
    c.fov = v.fovDeg;
    c.fovIsHeight = true;
    c.height = v.orthoHeight;
}

}  // namespace

MainWindow::MainWindow() {
    doc_ = new Document(this);
    auto* central = new QWidget(this);
    auto* col = new QVBoxLayout(central);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);
    sceneTabs_ = new QTabBar(central);
    sceneTabs_->setDocumentMode(true);
    sceneTabs_->setExpanding(false);
    sceneTabs_->hide();
    col->addWidget(sceneTabs_);
    view_ = new render::Viewport(central);
    col->addWidget(view_, 1);
    setCentralWidget(central);
    tools_ = new edit::ToolController(view_, doc_, this);
    view_->setInputHandler(tools_);
    view_->installEventFilter(this);

    buildActions();
    buildMenus();
    buildToolbars();
    buildTray();
    buildStatusBar();
    // A unified (native) toolbar next to an OpenGL view leaves unpainted, black areas on screen; widgets
    // also fill their own backgrounds so nothing shows through.
    setUnifiedTitleAndToolBarOnMac(false);
    setAutoFillBackground(true);
    resize(1280, 800);

    connect(doc_, &Document::changed, this, [this] {
        scheduleRebuild();
        refreshPanels();
        refreshSceneTabs();
    });
    connect(doc_, &Document::styleChanged, this, [this] {
        scheduleRebuild();
        if (styles_) styles_->refresh();
        if (shadows_) shadows_->refresh();
        syncStyleActions();
    });
    connect(doc_, &Document::selectionChanged, this, [this] {
        scheduleRebuild();
        if (entityInfo_) entityInfo_->refresh();
    });
    connect(doc_, &Document::paintMaterialChanged, this, [this] {
        if (materials_) materials_->refresh();
    });
    connect(doc_, &Document::fileChanged, this, &MainWindow::refreshTitle);
    connect(view_, &render::Viewport::viewChanged, this, [this] {
        if (view_ && view_->scene().hasFaceMe) scheduleRebuild();
    });
    connect(tools_, &edit::ToolController::status, this, [this](const QString& s) {
        if (status_) status_->setText(s);
    });
    connect(tools_, &edit::ToolController::measurement, this, [this](const QString& label, const QString& value) {
        if (!measureLabel_ || !measure_) return;
        measureLabel_->setText(label.isEmpty() ? tr("Measurements") : label);
        if (!measure_->hasFocus()) measure_->setText(value);
    });
    connect(tools_, &edit::ToolController::toolChanged, this, [this](ToolId t) {
        if (auto it = toolActions_.find(t); it != toolActions_.end() && it->second) it->second->setChecked(true);
    });
    connect(sceneTabs_, &QTabBar::currentChanged, this, [this](int i) {
        if (updatingTabs_ || !doc_ || i < 0) return;
        const auto pages = skp::scenes(doc_->model());
        if (static_cast<std::size_t>(i) < pages.size()) goToScene(pages[static_cast<std::size_t>(i)]);
    });
    tools_->setTool(ToolId::Select);
}

// ------------------------------------------------------------------ actions

void MainWindow::buildStatusBar() {
    auto* bar = statusBar();
    status_ = new QLabel(bar);
    status_->setMinimumWidth(200);
    bar->addWidget(status_, 1);
    measureLabel_ = new QLabel(tr("Measurements"), bar);
    measure_ = new QLineEdit(bar);
    measure_->setFixedWidth(170);
    bar->addPermanentWidget(measureLabel_);
    bar->addPermanentWidget(measure_);
    connect(measure_, &QLineEdit::returnPressed, this, [this] {
        if (!tools_ || !measure_) return;
        if (!tools_->typed(measure_->text())) QApplication::beep();
        if (view_) view_->setFocus();
    });
}

bool MainWindow::eventFilter(QObject* watched, QEvent* e) {
    // Typing a number while drawing goes straight to the Measurements box, as in the original.
    if (watched == view_ && e->type() == QEvent::KeyPress && measure_) {
        auto* k = static_cast<QKeyEvent*>(e);
        static const QRegularExpression starts(QStringLiteral(R"(^[0-9.,'"\-])"));
        if (!k->text().isEmpty() && starts.match(k->text()).hasMatch() && (k->modifiers() & Qt::ControlModifier) == 0) {
            measure_->setFocus();
            measure_->setText(k->text());
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, e);
}

// ------------------------------------------------------------------ model and view

void MainWindow::scheduleRebuild() {
    if (rebuildPending_) return;
    rebuildPending_ = true;
    QTimer::singleShot(0, this, [this] {
        rebuildPending_ = false;
        rebuildScene();
    });
}

void MainWindow::rebuildScene() {
    if (!doc_ || !view_) return;
    render::BuildOptions o;
    o.style = doc_->style();
    o.eye = view_->view().eye;
    o.selection = doc_->selection();
    render::Style st = o.style;
    st.guides = guidesShown_;
    view_->setScene(render::buildScene(doc_->model(), o), st);
}

void MainWindow::storeView(skp::Camera& cam) const {
    if (view_) viewToCamera(view_->view(), cam);
}

void MainWindow::refreshPanels() {
    for (QWidget* w : std::initializer_list<QWidget*>{entityInfo_, materials_, components_, styles_, layers_, scenes_,
                                                      shadows_}) {
        if (w == nullptr) continue;
    }
    if (entityInfo_) entityInfo_->refresh();
    if (materials_) materials_->refresh();
    if (components_) components_->refresh();
    if (styles_) styles_->refresh();
    if (layers_) layers_->refresh();
    if (scenes_) scenes_->refresh();
    if (shadows_) shadows_->refresh();
    syncStyleActions();
}

void MainWindow::refreshTitle() {
    if (!doc_) return;
    setWindowTitle(QStringLiteral("%1%2 - %3").arg(doc_->displayName(), doc_->modified() ? QStringLiteral(" *") : QString(),
                                                   QLatin1String(kAppName)));
    setWindowModified(doc_->modified());
}

void MainWindow::refreshSceneTabs() {
    if (!doc_ || !sceneTabs_) return;
    updatingTabs_ = true;
    while (sceneTabs_->count() > 0) sceneTabs_->removeTab(0);
    for (skp::ObjId p : skp::scenes(doc_->model())) sceneTabs_->addTab(skp::sceneName(doc_->model(), p));
    sceneTabs_->setVisible(sceneTabs_->count() > 0);
    sceneTabs_->setCurrentIndex(-1);
    updatingTabs_ = false;
}

void MainWindow::syncStyleActions() {
    if (!doc_) return;
    const render::Style s = doc_->style();
    auto check = [this](const char* id, bool on) {
        if (auto it = actions_.find(QString::fromLatin1(id)); it != actions_.end() && it->second) {
            const QSignalBlocker block(it->second);
            it->second->setChecked(on);
        }
    };
    check("xray", s.xray);
    check("backedges", s.backEdges);
    check("edges", s.edges);
    check("profiles", s.profiles);
    check("extension", s.extensions);
    check("hiddengeom", s.hiddenGeometry);
    check("axes", s.axes);
    check("wireframe", s.renderMode == render::kWireframe);
    check("hiddenline", s.renderMode == render::kHiddenLine);
    check("shaded", s.renderMode == render::kShaded && !s.textures);
    check("textured", s.renderMode == render::kShaded && s.textures);
    check("monochrome", s.renderMode == render::kMonochrome);
}

void MainWindow::setFaceStyle(int mode) {
    if (!doc_) return;
    render::Style s = doc_->style();
    s.renderMode = mode == 0 ? render::kWireframe : mode == 1 ? render::kHiddenLine : mode == 4 ? render::kMonochrome
                                                                                             : render::kShaded;
    if (mode == 2) s.textures = false;
    if (mode == 3) s.textures = true;
    doc_->setStyle(s);
}

void MainWindow::toggleStyle(bool render::Style::*member, bool on) {
    if (!doc_ || !view_) return;
    render::Style s = doc_->style();
    s.*member = on;
    doc_->setStyle(s);
}

void MainWindow::applyModelCamera() {
    if (!doc_ || !view_) return;
    rebuildScene();
    const double aspect = std::max(1, view_->width()) / static_cast<double>(std::max(1, view_->height()));
    if (const auto* cam = doc_->model().get<skp::Camera>(doc_->model().camera)) {
        view_->setView(viewFromCamera(*cam, aspect));
    } else {
        view_->zoomExtents(false);
    }
}

void MainWindow::storeViewInModel() {
    if (!doc_ || !view_) return;
    skp::SkpFile& f = doc_->model();
    if (auto* cam = f.get<skp::Camera>(f.camera)) {
        viewToCamera(view_->view(), *cam);
    } else {
        skp::Camera c;
        viewToCamera(view_->view(), c);
        f.camera = f.add(skp::Object{c});
    }
}

QStringList MainWindow::templates() const {
    // SketchUp's own templates, when it is installed on this computer.
    QStringList out;
    const QDir apps(QStringLiteral("/Applications"));
    for (const QString& app : apps.entryList({QStringLiteral("SketchUp*")}, QDir::Dirs, QDir::Name | QDir::Reversed)) {
        const QDir t(apps.filePath(app) + QStringLiteral("/SketchUp.app/Contents/Resources/Content/Resources/en-US/Templates"));
        for (const QString& f : t.entryList({QStringLiteral("*.skp")}, QDir::Files, QDir::Name)) out << t.filePath(f);
        if (!out.isEmpty()) break;
    }
    return out;
}

void MainWindow::startWith(const QString& path) {
    if (!path.isEmpty()) {
        if (auto err = doc_->open(path)) {
            QMessageBox::warning(this, tr("Open"), tr("Could not open %1:\n%2").arg(path, *err));
            newModel(QString());
        }
    } else {
        const QStringList t = templates();
        newModel(t.isEmpty() ? QString() : t.front());
    }
    applyModelCamera();
}

bool MainWindow::confirmDiscard() {
    if (!doc_ || !doc_->modified()) return true;
    const auto r = QMessageBox::question(this, tr("Save changes?"),
                                         tr("Save changes to \"%1\" before closing?").arg(doc_->displayName()),
                                         QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (r == QMessageBox::Cancel) return false;
    if (r == QMessageBox::Save) return save();
    return true;
}

void MainWindow::newModel(const QString& templatePath) {
    if (!confirmDiscard()) return;
    if (tools_) tools_->cancel();
    if (!templatePath.isEmpty()) {
        auto r = skp::readSkpFile(templatePath);
        if (auto* f = std::get_if<skp::SkpFile>(&r)) {
            doc_->reset(std::move(*f), QString());
            applyModelCamera();
            return;
        }
    }
    doc_->reset(skp::newModel(), QString());
    applyModelCamera();
}

void MainWindow::openFile() {
    if (!confirmDiscard()) return;
    const QString path = QFileDialog::getOpenFileName(this, tr("Open Model"), QString(), tr("SketchUp models (*.skp)"));
    if (path.isEmpty()) return;
    if (tools_) tools_->cancel();
    if (auto err = doc_->open(path)) {
        QMessageBox::warning(this, tr("Open"), tr("Could not open %1:\n%2").arg(QFileInfo(path).fileName(), *err));
        return;
    }
    applyModelCamera();
}

bool MainWindow::save() {
    if (!doc_) return false;
    if (doc_->path().isEmpty()) return saveAs();
    storeViewInModel();
    if (auto err = doc_->save(doc_->path())) {
        QMessageBox::warning(this, tr("Save"), *err);
        return false;
    }
    statusBar()->showMessage(tr("Saved %1").arg(doc_->path()), 4000);
    return true;
}

bool MainWindow::saveAs() {
    if (!doc_) return false;
    QString path = QFileDialog::getSaveFileName(this, tr("Save Model"), doc_->displayName() + QStringLiteral(".skp"),
                                                tr("SketchUp 2017 model (*.skp)"));
    if (path.isEmpty()) return false;
    if (!path.endsWith(QLatin1String(".skp"), Qt::CaseInsensitive)) path += QStringLiteral(".skp");
    storeViewInModel();
    if (auto err = doc_->save(path)) {
        QMessageBox::warning(this, tr("Save"), *err);
        return false;
    }
    return true;
}

void MainWindow::exportImage() {
    if (!view_) return;
    const QString path = QFileDialog::getSaveFileName(this, tr("Export 2D Graphic"), doc_->displayName() + QStringLiteral(".png"),
                                                      tr("PNG image (*.png);;JPEG image (*.jpg)"));
    if (path.isEmpty()) return;
    if (!view_->grabFramebuffer().save(path)) QMessageBox::warning(this, tr("Export"), tr("Could not write %1").arg(path));
}

void MainWindow::goToScene(skp::ObjId page) {
    if (!doc_ || !view_) return;
    if (auto cam = skp::sceneCamera(doc_->model(), page)) {
        const double aspect = std::max(1, view_->width()) / static_cast<double>(std::max(1, view_->height()));
        view_->setView(viewFromCamera(*cam, aspect), true);
    }
    const auto pages = skp::scenes(doc_->model());
    for (std::size_t i = 0; i < pages.size(); ++i) {
        if (pages[i] == page && sceneTabs_) {
            updatingTabs_ = true;
            sceneTabs_->setCurrentIndex(static_cast<int>(i));
            updatingTabs_ = false;
        }
    }
}

void MainWindow::addScene() {
    if (!doc_ || !view_) return;
    skp::Camera c;
    viewToCamera(view_->view(), c);
    doc_->checkpoint(tr("Add Scene"));
    skp::addScene(doc_->model(), tr("Scene %1").arg(skp::scenes(doc_->model()).size() + 1), c);
    doc_->edited();
}

void MainWindow::placeComponent(skp::ObjId def) {
    if (!doc_ || !view_) return;
    // Put the copy where the view looks at the ground.
    const render::View v = view_->view();
    geom::V3 at = v.target;
    const geom::V3 dir = (v.target - v.eye).normalized();
    if (std::abs(dir.z) > 1e-6) {
        const double t = -v.eye.z / dir.z;
        if (t > 0) at = v.eye + dir * t;
    }
    at.z = 0;
    doc_->checkpoint(tr("Place Component"));
    if (auto id = skp::addInstance(doc_->model(), def, geom::M4::translate(at))) {
        doc_->edited();
        doc_->setSelection({*id});
        if (tools_) tools_->setTool(ToolId::Move);
    } else {
        doc_->edited();
    }
}

void MainWindow::deleteSelection() {
    if (!doc_ || doc_->selection().empty()) return;
    doc_->checkpoint(tr("Erase"));
    skp::erase(doc_->model(), doc_->root(), doc_->selection());
    doc_->setSelection({});
    doc_->edited();
}

void MainWindow::selectAll() {
    if (!doc_) return;
    std::set<skp::ObjId> all;
    if (const auto* root = skp::rootComponent(doc_->model())) all.insert(root->ents.begin(), root->ents.end());
    doc_->setSelection(std::move(all));
}

void MainWindow::makeGroup() {
    if (!doc_ || doc_->selection().empty()) return;
    doc_->checkpoint(tr("Make Group"));
    const auto g = skp::makeGroup(doc_->model(), doc_->selection());
    doc_->edited();
    if (g) doc_->setSelection({*g});
}

void MainWindow::closeEvent(QCloseEvent* e) {
    if (confirmDiscard()) e->accept(); else e->ignore();
}

void MainWindow::screenshotAndQuit(const QString& file, int frames) {
    // Keep drawing until enough frames have been shown, then save the whole window.
    auto* tick = new QTimer(this);
    tick->setInterval(16);
    connect(tick, &QTimer::timeout, this, [this, tick, file, frames] {
        if (!view_) return;
        if (view_->framesDrawn() < frames) {
            view_->update();
            return;
        }
        tick->stop();
        // What the screen really shows (so composition problems are caught), else a fresh render.
        QPixmap shot;
        if (windowHandle() != nullptr && windowHandle()->screen() != nullptr) {
            shot = windowHandle()->screen()->grabWindow(winId());
        }
        if (shot.isNull()) shot = grab();
        if (!shot.save(file)) qWarning("could not save %s", qPrintable(file));
        if (doc_) doc_->reset(doc_->model(), doc_->path());  // nothing to ask about on the way out
        QApplication::quit();
    });
    tick->start();
}

}  // namespace ui
