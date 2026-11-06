#pragma once
// The open model: file path, unsaved changes, undo history, selection and the paint material.

#include "render/Style.h"
#include "skp/SkpFile.h"

#include <QObject>
#include <QString>

#include <deque>
#include <optional>
#include <set>

namespace ui {

class Document : public QObject {
    Q_OBJECT
public:
    explicit Document(QObject* parent = nullptr);

    skp::SkpFile& model() { return model_; }
    const skp::SkpFile& model() const { return model_; }
    const QString& path() const { return path_; }
    QString displayName() const;
    bool modified() const { return modified_; }
    skp::ObjId root() const { return model_.root.value_or(0); }

    // Replaces the model (new or opened). An empty path means "Untitled".
    void reset(skp::SkpFile f, const QString& path);
    std::optional<QString> open(const QString& path);
    std::optional<QString> save(const QString& path);

    // Call before changing the model, then edited() after.
    void checkpoint(const QString& what);
    void edited();
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    QString undoText() const { return undo_.empty() ? QString() : undo_.back().what; }
    QString redoText() const { return redo_.empty() ? QString() : redo_.back().what; }
    void undo();
    void redo();

    render::Style style() const { return render::readStyle(model_); }
    void setStyle(const render::Style& s);

    const std::set<skp::ObjId>& selection() const { return selection_; }
    void setSelection(std::set<skp::ObjId> s);

    skp::Ref paintMaterial() const { return paint_; }
    void setPaintMaterial(skp::Ref m);

signals:
    void changed();           // the model changed (geometry, materials, settings)
    void styleChanged();      // only display settings changed
    void selectionChanged();
    void paintMaterialChanged();
    void fileChanged();       // path or modified flag

private:
    struct Snapshot {
        QString what;
        skp::SkpFile model;
    };
    skp::SkpFile model_;
    QString path_;
    bool modified_ = false;
    std::deque<Snapshot> undo_, redo_;
    std::set<skp::ObjId> selection_;
    skp::Ref paint_;
};

}  // namespace ui
