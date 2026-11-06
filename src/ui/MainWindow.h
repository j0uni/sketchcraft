#pragma once
// The main window: menus, toolbars, the 3D view with scene tabs, the tray of panels and the status bar
// with the Measurements box.

#include "edit/Tools.h"
#include "render/Viewport.h"
#include "ui/Document.h"
#include "ui/Panels.h"

#include <QActionGroup>
#include <QMainWindow>
#include <QPointer>

#include <map>

class QDockWidget;
class QLabel;
class QLineEdit;
class QMenu;
class QTabBar;
class QTimer;
class QToolBox;

namespace ui {

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();

    // Opens a file, or a new model from the first template found (or the built-in default).
    void startWith(const QString& path);
    // Draws `frames` frames, saves the window as a PNG and quits.
    void screenshotAndQuit(const QString& file, int frames);

protected:
    void closeEvent(QCloseEvent* e) override;
    bool eventFilter(QObject* watched, QEvent* e) override;

private:
    void buildActions();
    void buildMenus();
    void buildToolbars();
    void buildTray();
    void buildStatusBar();

    void scheduleRebuild();
    void rebuildScene();
    void refreshPanels();
    void refreshTitle();
    void refreshSceneTabs();
    void applyModelCamera();
    void storeViewInModel();

    bool confirmDiscard();
    void newModel(const QString& templatePath);
    void openFile();
    bool save();
    bool saveAs();
    void exportImage();
    void goToScene(skp::ObjId page);
    void addScene();
    void placeComponent(skp::ObjId def);
    void deleteSelection();
    void selectAll();
    void makeGroup();
    void setFaceStyle(int mode);
    void toggleStyle(bool render::Style::*member, bool on);
    void syncStyleActions();
    QStringList templates() const;
    QAction* addAct(const QString& id, const QString& text, const QList<QKeySequence>& keys,
                    const QString& iconId = QString());
    QAction* notYet(const QString& text, const QList<QKeySequence>& keys = {});
    void showPanel(int index);
    void setHidden(bool hide);
    void makeComponent();
    void stepHistory(int step);
    void updateScene();
    void storeView(skp::Camera& cam) const;

    QPointer<Document> doc_;
    QPointer<render::Viewport> view_;
    QPointer<edit::ToolController> tools_;
    QPointer<QTabBar> sceneTabs_;
    QPointer<QLabel> status_, measureLabel_;
    QPointer<QLineEdit> measure_;
    QPointer<QToolBox> tray_;
    QPointer<EntityInfoPanel> entityInfo_;
    QPointer<MaterialsPanel> materials_;
    QPointer<ComponentsPanel> components_;
    QPointer<StylesPanel> styles_;
    QPointer<LayersPanel> layers_;
    QPointer<ScenesPanel> scenes_;
    QPointer<ShadowsPanel> shadows_;

    QPointer<QActionGroup> toolGroup_, faceStyleGroup_;
    std::map<edit::ToolId, QPointer<QAction>> toolActions_;
    std::map<QString, QPointer<QAction>> actions_;
    bool rebuildPending_ = false;
    bool updatingTabs_ = false;
    bool guidesShown_ = true;
    QPointer<QDockWidget> trayDock_, toolSet_;
    QPointer<QMenu> toolbarsMenu_;
    QPointer<QTimer> historyTimer_;
    std::vector<render::View> history_;
    int historyIndex_ = -1;
    bool navigating_ = false;
};

}  // namespace ui
