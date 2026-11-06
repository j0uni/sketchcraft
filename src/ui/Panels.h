#pragma once
// The tray panels: Entity Info, Materials, Components, Styles, Layers, Scenes and Shadows.
// Each reads the open document and changes it through checkpoint()/edited().

#include "ui/Document.h"

#include <QPointer>
#include <QWidget>

#include <functional>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSlider;
class QTreeWidget;

namespace ui {

class EntityInfoPanel : public QWidget {
    Q_OBJECT
public:
    EntityInfoPanel(Document* doc, QWidget* parent);
    void refresh();

private:
    QPointer<Document> doc_;
    QLabel* kind_ = nullptr;
    QComboBox* layer_ = nullptr;
    QLabel* material_ = nullptr;
    QLineEdit* name_ = nullptr;
    QLabel* measure_ = nullptr;
    QCheckBox* hidden_ = nullptr;
    bool updating_ = false;
};

class MaterialsPanel : public QWidget {
    Q_OBJECT
public:
    MaterialsPanel(Document* doc, QWidget* parent);
    void refresh();

private:
    QPointer<Document> doc_;
    QListWidget* list_ = nullptr;
    QLabel* current_ = nullptr;
    bool updating_ = false;
};

class ComponentsPanel : public QWidget {
    Q_OBJECT
public:
    // `place` puts a new instance of a definition into the model.
    ComponentsPanel(Document* doc, std::function<void(skp::ObjId)> place, QWidget* parent);
    void refresh();

private:
    QPointer<Document> doc_;
    std::function<void(skp::ObjId)> place_;
    QListWidget* list_ = nullptr;
};

class StylesPanel : public QWidget {
    Q_OBJECT
public:
    StylesPanel(Document* doc, QWidget* parent);
    void refresh();

private:
    void apply(const std::function<void(render::Style&)>& change);
    QPointer<Document> doc_;
    QComboBox* faceStyle_ = nullptr;
    QCheckBox *edges_ = nullptr, *profiles_ = nullptr, *extension_ = nullptr, *backEdges_ = nullptr;
    QCheckBox *sky_ = nullptr, *ground_ = nullptr, *axes_ = nullptr, *xray_ = nullptr;
    QPushButton *background_ = nullptr, *skyColor_ = nullptr, *groundColor_ = nullptr, *front_ = nullptr,
                *backFace_ = nullptr, *edgeColor_ = nullptr;
    bool updating_ = false;
};

class LayersPanel : public QWidget {
    Q_OBJECT
public:
    LayersPanel(Document* doc, QWidget* parent);
    void refresh();

private:
    QPointer<Document> doc_;
    QTreeWidget* tree_ = nullptr;
    bool updating_ = false;
};

class ScenesPanel : public QWidget {
    Q_OBJECT
public:
    // `go` switches the view to a scene; `add` adds one from the current view.
    ScenesPanel(Document* doc, std::function<void(skp::ObjId)> go, std::function<void()> add, QWidget* parent);
    void refresh();

private:
    QPointer<Document> doc_;
    std::function<void(skp::ObjId)> go_;
    std::function<void()> add_;
    QListWidget* list_ = nullptr;
};

class ShadowsPanel : public QWidget {
    Q_OBJECT
public:
    ShadowsPanel(Document* doc, QWidget* parent);
    void refresh();

private:
    QPointer<Document> doc_;
    QSlider *time_ = nullptr, *date_ = nullptr, *light_ = nullptr, *dark_ = nullptr;
    QLabel *timeLabel_ = nullptr, *dateLabel_ = nullptr, *place_ = nullptr;
    QCheckBox* useSun_ = nullptr;
    bool updating_ = false;
};

// A swatch picture for a material (its texture, or its colour).
QPixmap materialSwatch(const skp::SkpFile& f, skp::Ref material, int size);

}  // namespace ui
