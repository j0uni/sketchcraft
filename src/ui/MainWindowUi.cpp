// Menus, shortcuts, toolbars, the Large Tool Set palette and the tray.
// The shortcuts are the original's defaults: single keys from Resources/Shortcuts.plist, ⌘ keys from the
// key equivalents in English.lproj/MainMenu.nib (see MECHANICS.md section 10). On macOS Qt::CTRL is ⌘.

#include "skp/Builder.h"
#include "skp/Query.h"
#include "ui/Icons.h"
#include "ui/MainWindow.h"

#include <QDockWidget>
#include <QFileInfo>
#include <QGridLayout>
#include <QInputDialog>
#include <QMenuBar>
#include <QMessageBox>
#include <QTimer>
#include <QToolBar>
#include <QToolBox>
#include <QLineEdit>
#include <QTabBar>
#include <QToolButton>

namespace ui {

using edit::ToolId;

namespace {
constexpr const char* kAppName = "SketchCraft";
constexpr int kHistoryDelayMs = 400;  // a view counts as a stop for Previous/Next after this pause (estimated)
}  // namespace

QAction* MainWindow::addAct(const QString& id, const QString& text, const QList<QKeySequence>& keys,
                            const QString& iconId) {
    auto* a = new QAction(text, this);
    if (!keys.isEmpty()) a->setShortcuts(keys);
    if (!iconId.isEmpty()) a->setIcon(icon(iconId));
    if (!keys.isEmpty()) a->setToolTip(QStringLiteral("%1 (%2)").arg(text, keys.front().toString(QKeySequence::NativeText)));
    actions_[id] = a;
    return a;
}

QAction* MainWindow::notYet(const QString& text, const QList<QKeySequence>& keys) {
    auto* a = new QAction(text, this);
    a->setShortcuts(keys);
    a->setEnabled(false);
    a->setToolTip(tr("Not available in this version"));
    return a;
}

void MainWindow::buildActions() {
    auto on = [this](QAction* a, auto&& fn) { connect(a, &QAction::triggered, this, fn); };
    const auto cmd = [](int key) { return QKeySequence(Qt::CTRL | key); };
    const auto cmdShift = [](int key) { return QKeySequence(Qt::CTRL | Qt::SHIFT | key); };

    // File.
    on(addAct(QStringLiteral("new"), tr("New"), {QKeySequence::New}, QStringLiteral("new")), [this] {
        const QStringList t = templates();
        newModel(t.isEmpty() ? QString() : t.front());
    });
    on(addAct(QStringLiteral("open"), tr("Open…"), {QKeySequence::Open}, QStringLiteral("open")), [this] { openFile(); });
    on(addAct(QStringLiteral("close"), tr("Close"), {cmd(Qt::Key_W)}), [this] { close(); });
    on(addAct(QStringLiteral("save"), tr("Save"), {QKeySequence::Save}, QStringLiteral("save")), [this] { save(); });
    on(addAct(QStringLiteral("saveas"), tr("Save As…"), {cmdShift(Qt::Key_S)}), [this] { saveAs(); });
    on(addAct(QStringLiteral("export"), tr("2D Graphic…"), {}), [this] { exportImage(); });
    on(addAct(QStringLiteral("quit"), tr("Quit"), {QKeySequence::Quit}), [this] { close(); });

    // Edit.
    on(addAct(QStringLiteral("undo"), tr("Undo"), {QKeySequence::Undo}, QStringLiteral("undo")), [this] {
        if (tools_) tools_->cancel();
        if (doc_) doc_->undo();
    });
    on(addAct(QStringLiteral("redo"), tr("Redo"), {cmdShift(Qt::Key_Z)}, QStringLiteral("redo")), [this] {
        if (doc_) doc_->redo();
    });
    on(addAct(QStringLiteral("delete"), tr("Delete"), {QKeySequence(Qt::Key_Backspace), QKeySequence(Qt::Key_Delete)}),
       [this] { deleteSelection(); });
    on(addAct(QStringLiteral("selectall"), tr("Select All"), {cmd(Qt::Key_A)}), [this] { selectAll(); });
    on(addAct(QStringLiteral("selectnone"), tr("Select None"), {cmdShift(Qt::Key_A)}), [this] {
        if (doc_) doc_->setSelection({});
    });
    on(addAct(QStringLiteral("hide"), tr("Hide"), {cmd(Qt::Key_E)}), [this] { setHidden(true); });
    on(addAct(QStringLiteral("unhideall"), tr("All"), {cmdShift(Qt::Key_E)}), [this] { setHidden(false); });
    on(addAct(QStringLiteral("component"), tr("Make Component…"), {QKeySequence(Qt::Key_G), cmdShift(Qt::Key_G)},
              QStringLiteral("component")),
       [this] { makeComponent(); });
    on(addAct(QStringLiteral("group"), tr("Make Group"), {cmd(Qt::Key_G)}), [this] { makeGroup(); });

    // Tools: single keys from Shortcuts.plist plus the menu's ⌘ keys.
    toolGroup_ = new QActionGroup(this);
    struct T {
        ToolId id;
        const char* icon;
        QList<QKeySequence> keys;
    };
    const QList<T> tools{
        {ToolId::Select, "select", {QKeySequence(Qt::Key_Space)}},
        {ToolId::Eraser, "eraser", {QKeySequence(Qt::Key_E)}},
        {ToolId::Paint, "paint", {QKeySequence(Qt::Key_B)}},
        {ToolId::Line, "line", {QKeySequence(Qt::Key_L), cmd(Qt::Key_L)}},
        {ToolId::Arc, "arc", {QKeySequence(Qt::Key_A), cmd(Qt::Key_J)}},
        {ToolId::Rectangle, "rectangle", {QKeySequence(Qt::Key_R), cmd(Qt::Key_K)}},
        {ToolId::Circle, "circle", {QKeySequence(Qt::Key_C)}},
        {ToolId::Polygon, "polygon", {cmd(Qt::Key_Semicolon)}},
        {ToolId::PushPull, "pushpull", {QKeySequence(Qt::Key_P), cmd(Qt::Key_Equal)}},
        {ToolId::Offset, "offset", {QKeySequence(Qt::Key_F), cmd(Qt::Key_Minus)}},
        {ToolId::Move, "move", {QKeySequence(Qt::Key_M), cmd(Qt::Key_0)}},
        {ToolId::Rotate, "rotate", {QKeySequence(Qt::Key_Q), cmd(Qt::Key_8)}},
        {ToolId::Scale, "scale", {QKeySequence(Qt::Key_S), cmd(Qt::Key_9)}},
        {ToolId::Tape, "tape", {QKeySequence(Qt::Key_T), cmd(Qt::Key_D)}},
        {ToolId::Orbit, "orbit", {QKeySequence(Qt::Key_O), cmd(Qt::Key_B)}},
        {ToolId::Pan, "pan", {QKeySequence(Qt::Key_H), cmd(Qt::Key_R)}},
        {ToolId::Zoom, "zoom", {QKeySequence(Qt::Key_Z), cmd(Qt::Key_Backslash)}},
        {ToolId::ZoomWindow, "zoomwindow", {cmd(Qt::Key_BracketRight)}},
        {ToolId::FieldOfView, "fov", {}},
        {ToolId::PositionCamera, "positioncamera", {}},
        {ToolId::Walk, "walk", {cmd(Qt::Key_Slash)}},
        {ToolId::LookAround, "lookaround", {cmd(Qt::Key_Period)}},
    };
    for (const T& t : tools) {
        QAction* a = addAct(QStringLiteral("tool-%1").arg(QString::fromLatin1(t.icon)), edit::toolName(t.id), t.keys,
                            QString::fromLatin1(t.icon));
        a->setCheckable(true);
        toolGroup_->addAction(a);
        const ToolId id = t.id;
        on(a, [this, id] {
            if (tools_) tools_->setTool(id);
            if (id == ToolId::Paint) showPanel(1);  // the original opens Materials with the Paint Bucket
        });
        toolActions_[t.id] = a;
    }
    on(addAct(QStringLiteral("zoomext"), tr("Zoom Extents"), {QKeySequence(Qt::SHIFT | Qt::Key_Z), cmd(Qt::Key_BracketLeft)},
              QStringLiteral("zoomext")),
       [this] {
           if (view_) view_->zoomExtents();
       });
    on(addAct(QStringLiteral("previous"), tr("Previous"), {}, QStringLiteral("previous")), [this] { stepHistory(-1); });
    on(addAct(QStringLiteral("next"), tr("Next"), {}, QStringLiteral("next")), [this] { stepHistory(+1); });

    // Standard views, ⌘1 … ⌘7.
    struct V {
        const char* id;
        const char* text;
        render::StandardView sv;
        int key;
    };
    const QList<V> views{{"top", "Top", render::StandardView::Top, Qt::Key_1},
                         {"bottom", "Bottom", render::StandardView::Bottom, Qt::Key_2},
                         {"front", "Front", render::StandardView::Front, Qt::Key_3},
                         {"back", "Back", render::StandardView::Back, Qt::Key_4},
                         {"left", "Left", render::StandardView::Left, Qt::Key_5},
                         {"right", "Right", render::StandardView::Right, Qt::Key_6},
                         {"iso", "Iso", render::StandardView::Iso, Qt::Key_7}};
    for (const V& v : views) {
        const render::StandardView sv = v.sv;
        const QString iconId = QString::fromLatin1(v.id) == QLatin1String("bottom") ? QStringLiteral("top")
                                                                                     : QString::fromLatin1(v.id);
        on(addAct(QString::fromLatin1(v.id), tr(v.text), {cmd(v.key)}, iconId), [this, sv] {
            if (view_) view_->standardView(sv);
        });
    }
    auto* projection = new QActionGroup(this);
    QAction* parallel = addAct(QStringLiteral("parallel"), tr("Parallel Projection"), {});
    QAction* persp = addAct(QStringLiteral("perspective"), tr("Perspective"), {}, QStringLiteral("perspective"));
    for (QAction* a : {parallel, persp}) {
        a->setCheckable(true);
        projection->addAction(a);
    }
    persp->setChecked(true);
    on(parallel, [this] {
        if (view_) view_->setPerspective(false);
    });
    on(persp, [this] {
        if (view_) view_->setPerspective(true);
    });
    connect(view_, &render::Viewport::viewChanged, persp, [this, persp, parallel] {
        if (!view_) return;
        (view_->view().perspective ? persp : parallel)->setChecked(true);
    });

    // Face styles and display switches.
    faceStyleGroup_ = new QActionGroup(this);
    const QList<std::pair<const char*, const char*>> faces{{"wireframe", "Wireframe"},
                                                           {"hiddenline", "Hidden Line"},
                                                           {"shaded", "Shaded"},
                                                           {"textured", "Shaded with Textures"},
                                                           {"monochrome", "Monochrome"}};
    for (int i = 0; i < faces.size(); ++i) {
        QAction* a = addAct(QString::fromLatin1(faces[i].first), tr(faces[i].second), {}, QString::fromLatin1(faces[i].first));
        a->setCheckable(true);
        faceStyleGroup_->addAction(a);
        on(a, [this, i] { setFaceStyle(i); });
    }
    auto toggle = [&](const QString& id, const QString& text, const QList<QKeySequence>& keys, const QString& iconId,
                      bool render::Style::*member) {
        QAction* a = addAct(id, text, keys, iconId);
        a->setCheckable(true);
        on(a, [this, member](bool b) { toggleStyle(member, b); });
    };
    toggle(QStringLiteral("xray"), tr("X-ray"), {}, QStringLiteral("xray"), &render::Style::xray);
    toggle(QStringLiteral("backedges"), tr("Back Edges"), {QKeySequence(Qt::Key_K)}, QStringLiteral("backedges"),
           &render::Style::backEdges);
    toggle(QStringLiteral("edges"), tr("Edges"), {}, QString(), &render::Style::edges);
    toggle(QStringLiteral("profiles"), tr("Profiles"), {}, QString(), &render::Style::profiles);
    toggle(QStringLiteral("extension"), tr("Extension"), {}, QString(), &render::Style::extensions);
    toggle(QStringLiteral("hiddengeom"), tr("Hidden Geometry"), {}, QString(), &render::Style::hiddenGeometry);
    toggle(QStringLiteral("axes"), tr("Axes"), {}, QString(), &render::Style::axes);
    QAction* guides = addAct(QStringLiteral("guides"), tr("Guides"), {});
    guides->setCheckable(true);
    guides->setChecked(true);
    on(guides, [this](bool b) {
        if (!view_) return;
        render::Style s = view_->style();
        s.guides = b;
        view_->setStyle(s);
        guidesShown_ = b;
    });
    on(addAct(QStringLiteral("addscene"), tr("Add Scene"), {QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Equal),
                                                            QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Plus)},
              QStringLiteral("scene")),
       [this] { addScene(); });
    on(addAct(QStringLiteral("updatescene"), tr("Update Scene"), {QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Minus)}),
       [this] { updateScene(); });
    on(addAct(QStringLiteral("about"), tr("About %1").arg(QLatin1String(kAppName)), {}), [this] {
        QMessageBox::about(this, tr("About %1").arg(QLatin1String(kAppName)),
                           tr("%1 opens, draws, edits and saves 3D models in the .skp format used by SketchUp 2017 "
                              "and earlier.")
                               .arg(QLatin1String(kAppName)));
    });

    // Previous / Next remember where the camera came to rest.
    historyTimer_ = new QTimer(this);
    historyTimer_->setSingleShot(true);
    historyTimer_->setInterval(kHistoryDelayMs);
    connect(historyTimer_, &QTimer::timeout, this, [this] {
        if (!view_) return;
        if (navigating_) {
            navigating_ = false;
            return;
        }
        if (historyIndex_ + 1 < static_cast<int>(history_.size())) history_.resize(static_cast<std::size_t>(historyIndex_ + 1));
        history_.push_back(view_->view());
        if (history_.size() > 50) history_.erase(history_.begin());
        historyIndex_ = static_cast<int>(history_.size()) - 1;
    });
    connect(view_, &render::Viewport::viewChanged, historyTimer_, [this] {
        if (historyTimer_) historyTimer_->start();
    });
}

void MainWindow::buildMenus() {
    auto a = [this](const char* id) { return actions_.at(QString::fromLatin1(id)).data(); };
    auto t = [this](ToolId id) { return toolActions_.at(id).data(); };

    QMenu* file = menuBar()->addMenu(tr("File"));
    file->addAction(a("new"));
    QMenu* fromTemplate = file->addMenu(tr("New from Template"));
    connect(fromTemplate, &QMenu::aboutToShow, this, [this, fromTemplate] {
        fromTemplate->clear();
        fromTemplate->addAction(tr("Empty (built-in)"), this, [this] { newModel(QString()); });
        for (const QString& path : templates()) {
            fromTemplate->addAction(QFileInfo(path).completeBaseName(), this, [this, path] { newModel(path); });
        }
    });
    file->addAction(a("open"));
    file->addSeparator();
    file->addAction(a("close"));
    file->addAction(a("save"));
    file->addAction(a("saveas"));
    file->addSeparator();
    file->addMenu(tr("Export"))->addAction(a("export"));
    file->addSeparator();
    file->addAction(a("quit"));

    QMenu* edit = menuBar()->addMenu(tr("Edit"));
    edit->addAction(a("undo"));
    edit->addAction(a("redo"));
    edit->addSeparator();
    edit->addAction(notYet(tr("Cut"), {QKeySequence::Cut}));
    edit->addAction(notYet(tr("Copy"), {QKeySequence::Copy}));
    edit->addAction(notYet(tr("Paste"), {QKeySequence::Paste}));
    edit->addAction(a("delete"));
    edit->addSeparator();
    edit->addAction(a("selectall"));
    edit->addAction(a("selectnone"));
    edit->addSeparator();
    edit->addAction(a("hide"));
    edit->addMenu(tr("Unhide"))->addAction(a("unhideall"));
    edit->addSeparator();
    edit->addAction(a("component"));
    edit->addAction(a("group"));
    connect(edit, &QMenu::aboutToShow, this, [this] {
        if (!doc_) return;
        actions_.at(QStringLiteral("undo"))->setEnabled(doc_->canUndo());
        actions_.at(QStringLiteral("undo"))->setText(doc_->canUndo() ? tr("Undo %1").arg(doc_->undoText()) : tr("Undo"));
        actions_.at(QStringLiteral("redo"))->setEnabled(doc_->canRedo());
        actions_.at(QStringLiteral("redo"))->setText(doc_->canRedo() ? tr("Redo %1").arg(doc_->redoText()) : tr("Redo"));
    });
    connect(edit, &QMenu::aboutToHide, this, [this] {
        actions_.at(QStringLiteral("undo"))->setEnabled(true);
        actions_.at(QStringLiteral("redo"))->setEnabled(true);
    });

    QMenu* view = menuBar()->addMenu(tr("View"));
    toolbarsMenu_ = view->addMenu(tr("Tool Palettes"));
    view->addSeparator();
    view->addAction(a("hiddengeom"));
    view->addAction(a("guides"));
    view->addAction(a("axes"));
    view->addSeparator();
    QMenu* edgeStyle = view->addMenu(tr("Edge Style"));
    for (const char* id : {"edges", "backedges", "profiles", "extension"}) edgeStyle->addAction(a(id));
    QMenu* faceStyle = view->addMenu(tr("Face Style"));
    faceStyle->addAction(a("xray"));
    faceStyle->addSeparator();
    for (const char* id : {"wireframe", "hiddenline", "shaded", "textured", "monochrome"}) faceStyle->addAction(a(id));
    QMenu* animation = view->addMenu(tr("Animation"));
    animation->addAction(a("addscene"));
    animation->addAction(a("updatescene"));

    QMenu* camera = menuBar()->addMenu(tr("Camera"));
    camera->addAction(a("previous"));
    camera->addAction(a("next"));
    QMenu* standard = camera->addMenu(tr("Standard Views"));
    for (const char* id : {"top", "bottom", "front", "back", "left", "right", "iso"}) standard->addAction(a(id));
    camera->addSeparator();
    camera->addAction(a("parallel"));
    camera->addAction(a("perspective"));
    camera->addAction(notYet(tr("Two-Point Perspective")));
    camera->addSeparator();
    camera->addAction(notYet(tr("Match New Photo…")));
    camera->addSeparator();
    camera->addAction(t(ToolId::Orbit));
    camera->addAction(t(ToolId::Pan));
    camera->addAction(t(ToolId::Zoom));
    camera->addAction(t(ToolId::FieldOfView));
    camera->addAction(t(ToolId::ZoomWindow));
    camera->addAction(a("zoomext"));
    camera->addSeparator();
    camera->addAction(t(ToolId::PositionCamera));
    camera->addAction(t(ToolId::Walk));
    camera->addAction(t(ToolId::LookAround));

    QMenu* draw = menuBar()->addMenu(tr("Draw"));
    QMenu* lines = draw->addMenu(tr("Lines"));
    lines->addAction(t(ToolId::Line));
    lines->addAction(notYet(tr("Freehand")));
    draw->addMenu(tr("Arcs"))->addAction(t(ToolId::Arc));
    QMenu* shapes = draw->addMenu(tr("Shapes"));
    for (ToolId id : {ToolId::Rectangle, ToolId::Circle, ToolId::Polygon}) shapes->addAction(t(id));

    QMenu* tools = menuBar()->addMenu(tr("Tools"));
    for (ToolId id : {ToolId::Select, ToolId::Eraser, ToolId::Paint}) tools->addAction(t(id));
    tools->addSeparator();
    for (ToolId id : {ToolId::Move, ToolId::Rotate, ToolId::Scale}) tools->addAction(t(id));
    tools->addSeparator();
    tools->addAction(t(ToolId::PushPull));
    tools->addAction(notYet(tr("Follow Me")));
    tools->addAction(t(ToolId::Offset));
    tools->addSeparator();
    tools->addAction(notYet(tr("Outer Shell")));
    tools->addSeparator();
    tools->addAction(t(ToolId::Tape));
    tools->addAction(notYet(tr("Protractor")));
    tools->addAction(notYet(tr("Axes")));
    tools->addSeparator();
    for (const QString& s : {tr("Dimensions"), tr("Text"), tr("3D Text")}) tools->addAction(notYet(s));
    tools->addSeparator();
    tools->addAction(notYet(tr("Section Plane"), {QKeySequence(Qt::CTRL | Qt::Key_Y)}));

    QMenu* window = menuBar()->addMenu(tr("Window"));
    const QList<std::pair<QString, QKeySequence>> panels{{tr("Entity Info"), QKeySequence(Qt::CTRL | Qt::Key_I)},
                                                         {tr("Materials"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C)},
                                                         {tr("Components"), {}},
                                                         {tr("Styles"), {}},
                                                         {tr("Layers"), {}},
                                                         {tr("Scenes"), {}},
                                                         {tr("Shadows"), {}}};
    for (int i = 0; i < panels.size(); ++i) {
        QAction* pa = window->addAction(panels[i].first, this, [this, i] { showPanel(i); });
        pa->setShortcut(panels[i].second);
    }
    QMenu* help = menuBar()->addMenu(tr("Help"));
    help->addAction(a("about"));
}

void MainWindow::buildToolbars() {
    auto a = [this](const char* id) { return actions_.at(QString::fromLatin1(id)).data(); };
    auto t = [this](ToolId id) { return toolActions_.at(id).data(); };

    // The original's Getting Started toolbar, minus the online services.
    QToolBar* main = addToolBar(tr("Getting Started"));
    main->setObjectName(QStringLiteral("GettingStarted"));
    main->setIconSize(QSize(24, 24));
    main->setMovable(false);
    for (ToolId id : {ToolId::Select, ToolId::Eraser, ToolId::Line, ToolId::Arc, ToolId::Rectangle, ToolId::PushPull,
                      ToolId::Offset, ToolId::Move, ToolId::Rotate, ToolId::Scale, ToolId::Tape, ToolId::Paint,
                      ToolId::Orbit, ToolId::Pan, ToolId::Zoom}) {
        main->addAction(t(id));
    }
    main->addAction(a("zoomext"));
    main->addAction(a("addscene"));

    QToolBar* views = addToolBar(tr("Views"));
    views->setObjectName(QStringLiteral("Views"));
    views->setIconSize(QSize(24, 24));
    for (const char* id : {"iso", "top", "front", "right", "back", "left"}) views->addAction(a(id));
    QToolBar* styles = addToolBar(tr("Styles"));
    styles->setObjectName(QStringLiteral("Styles"));
    styles->setIconSize(QSize(24, 24));
    for (const char* id : {"xray", "backedges", "wireframe", "hiddenline", "shaded", "textured", "monochrome"}) {
        styles->addAction(a(id));
    }
    views->hide();
    styles->hide();

    // The Large Tool Set palette: two columns of tools, floating beside the view as in the original.
    auto* dock = new QDockWidget(tr("Large Tool Set"), this);
    dock->setObjectName(QStringLiteral("LargeToolSet"));
    dock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    auto* grid = new QWidget(dock);
    grid->setAutoFillBackground(true);
    auto* layout = new QGridLayout(grid);
    layout->setContentsMargins(3, 3, 3, 3);
    layout->setSpacing(1);
    const QList<std::pair<QAction*, QAction*>> pairs{
        {t(ToolId::Select), a("component")},  {t(ToolId::Paint), t(ToolId::Eraser)},
        {t(ToolId::Line), t(ToolId::Arc)},     {t(ToolId::Rectangle), t(ToolId::Circle)},
        {t(ToolId::Polygon), t(ToolId::Offset)}, {t(ToolId::Move), t(ToolId::PushPull)},
        {t(ToolId::Rotate), t(ToolId::Scale)}, {t(ToolId::Tape), t(ToolId::FieldOfView)},
        {t(ToolId::Orbit), t(ToolId::Pan)},    {t(ToolId::Zoom), t(ToolId::ZoomWindow)},
        {a("zoomext"), a("previous")},          {t(ToolId::PositionCamera), t(ToolId::Walk)},
        {t(ToolId::LookAround), a("next")}};
    for (int row = 0; row < pairs.size(); ++row) {
        for (int col = 0; col < 2; ++col) {
            auto* b = new QToolButton(grid);
            b->setDefaultAction(col == 0 ? pairs[row].first : pairs[row].second);
            b->setIconSize(QSize(24, 24));
            b->setAutoRaise(true);
            b->setToolButtonStyle(Qt::ToolButtonIconOnly);
            layout->addWidget(b, row, col);
        }
    }
    layout->setRowStretch(static_cast<int>(pairs.size()), 1);  // keep the buttons together at the top
    dock->setWidget(grid);
    addDockWidget(Qt::LeftDockWidgetArea, dock);
    toolSet_ = dock;

    toolbarsMenu_->addAction(dock->toggleViewAction());
    toolbarsMenu_->addAction(views->toggleViewAction());
    toolbarsMenu_->addAction(styles->toggleViewAction());
}

void MainWindow::buildTray() {
    // The Mac original shows its inspectors (Entity Info, Materials, ...) only when opened from the Window
    // menu; here they share one tray that starts hidden.
    auto* dock = new QDockWidget(tr("Default Tray"), this);
    dock->setObjectName(QStringLiteral("DefaultTray"));
    dock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    tray_ = new QToolBox(dock);
    tray_->setAutoFillBackground(true);
    entityInfo_ = new EntityInfoPanel(doc_, tray_);
    materials_ = new MaterialsPanel(doc_, tray_);
    components_ = new ComponentsPanel(doc_, [this](skp::ObjId d) { placeComponent(d); }, tray_);
    styles_ = new StylesPanel(doc_, tray_);
    layers_ = new LayersPanel(doc_, tray_);
    scenes_ = new ScenesPanel(doc_, [this](skp::ObjId p) { goToScene(p); }, [this] { addScene(); }, tray_);
    shadows_ = new ShadowsPanel(doc_, tray_);
    tray_->addItem(entityInfo_, tr("Entity Info"));
    tray_->addItem(materials_, tr("Materials"));
    tray_->addItem(components_, tr("Components"));
    tray_->addItem(styles_, tr("Styles"));
    tray_->addItem(layers_, tr("Layers"));
    tray_->addItem(scenes_, tr("Scenes"));
    tray_->addItem(shadows_, tr("Shadows"));
    tray_->setMinimumWidth(260);
    dock->setWidget(tray_);
    addDockWidget(Qt::RightDockWidgetArea, dock);
    dock->hide();
    trayDock_ = dock;
}

void MainWindow::showPanel(int index) {
    if (!tray_ || !trayDock_) return;
    trayDock_->show();
    trayDock_->raise();
    tray_->setCurrentIndex(index);
}

void MainWindow::setHidden(bool hide) {
    if (!doc_) return;
    std::set<skp::ObjId> ids = doc_->selection();
    if (!hide) {
        if (const auto* root = skp::rootComponent(doc_->model())) ids.insert(root->ents.begin(), root->ents.end());
    }
    if (ids.empty()) return;
    doc_->checkpoint(hide ? tr("Hide") : tr("Unhide All"));
    for (skp::ObjId id : ids) {
        std::visit(
            [hide](auto& x) {
                using X = std::decay_t<decltype(x)>;
                if constexpr (std::is_same_v<X, skp::Face> || std::is_same_v<X, skp::Edge> ||
                              std::is_same_v<X, skp::Instance>)
                    x.de.flags[skp::kHidden] = hide;
            },
            doc_->model().objs.at(id));
    }
    if (hide) doc_->setSelection({});
    doc_->edited();
}

void MainWindow::makeComponent() {
    if (!doc_ || doc_->selection().empty()) return;
    bool ok = false;
    const QString name =
        QInputDialog::getText(this, tr("Create Component"), tr("Definition name:"), QLineEdit::Normal,
                              tr("Component#%1").arg(skp::definitions(doc_->model()).size() + 1), &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    doc_->checkpoint(tr("Make Component"));
    const auto c = skp::makeComponent(doc_->model(), doc_->selection(), name.trimmed());
    doc_->edited();
    if (c) doc_->setSelection({*c});
}

void MainWindow::stepHistory(int step) {
    const int target = historyIndex_ + step;
    if (!view_ || target < 0 || target >= static_cast<int>(history_.size())) return;
    historyIndex_ = target;
    navigating_ = true;
    view_->setView(history_[static_cast<std::size_t>(target)], true);
}

void MainWindow::updateScene() {
    if (!doc_ || !view_ || !sceneTabs_) return;
    const auto pages = skp::scenes(doc_->model());
    const int i = sceneTabs_->currentIndex();
    if (i < 0 || static_cast<std::size_t>(i) >= pages.size()) return;
    const skp::Record* page = skp::record(doc_->model(), skp::Ref{pages[static_cast<std::size_t>(i)]});
    if (page == nullptr || page->fields.size() < 6) return;
    const auto* camRef = std::get_if<skp::Ref>(&page->fields[5]);
    if (camRef == nullptr || !*camRef) return;
    doc_->checkpoint(tr("Update Scene"));
    if (auto* cam = doc_->model().get<skp::Camera>(*camRef)) storeView(*cam);
    doc_->edited();
}

}  // namespace ui
