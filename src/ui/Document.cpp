#include "ui/Document.h"

#include <QFileInfo>

namespace ui {

namespace {
constexpr std::size_t kUndoLimit = 40;
}

Document::Document(QObject* parent) : QObject(parent) {}

QString Document::displayName() const {
    return path_.isEmpty() ? tr("Untitled") : QFileInfo(path_).completeBaseName();
}

void Document::reset(skp::SkpFile f, const QString& path) {
    model_ = std::move(f);
    path_ = path;
    modified_ = false;
    undo_.clear();
    redo_.clear();
    selection_.clear();
    paint_.reset();
    emit changed();
    emit selectionChanged();
    emit paintMaterialChanged();
    emit fileChanged();
}

std::optional<QString> Document::open(const QString& path) {
    auto r = skp::readSkpFile(path);
    if (auto* e = std::get_if<skp::LoadError>(&r)) return e->message;
    reset(std::move(std::get<skp::SkpFile>(r)), path);
    return std::nullopt;
}

std::optional<QString> Document::save(const QString& path) {
    auto r = skp::writeSkpFile(model_, path);
    if (auto* e = std::get_if<skp::LoadError>(&r)) return e->message;
    path_ = path;
    modified_ = false;
    emit fileChanged();
    return std::nullopt;
}

void Document::checkpoint(const QString& what) {
    undo_.push_back({what, model_});
    if (undo_.size() > kUndoLimit) undo_.pop_front();
    redo_.clear();
}

void Document::edited() {
    modified_ = true;
    // Drop selected ids that no longer belong to the root.
    std::set<skp::ObjId> keep;
    if (const auto* root = model_.get<skp::Component>(model_.root)) {
        for (skp::ObjId id : root->ents) {
            if (selection_.contains(id)) keep.insert(id);
        }
    }
    const bool selChanged = keep != selection_;
    selection_ = std::move(keep);
    emit changed();
    if (selChanged) emit selectionChanged();
    emit fileChanged();
}

void Document::undo() {
    if (undo_.empty()) return;
    redo_.push_back({undo_.back().what, model_});
    model_ = std::move(undo_.back().model);
    undo_.pop_back();
    edited();
}

void Document::redo() {
    if (redo_.empty()) return;
    undo_.push_back({redo_.back().what, model_});
    model_ = std::move(redo_.back().model);
    redo_.pop_back();
    edited();
}

void Document::setStyle(const render::Style& s) {
    render::writeStyle(model_, s);
    modified_ = true;
    emit styleChanged();
    emit fileChanged();
}

void Document::setSelection(std::set<skp::ObjId> s) {
    if (s == selection_) return;
    selection_ = std::move(s);
    emit selectionChanged();
}

void Document::setPaintMaterial(skp::Ref m) {
    paint_ = m;
    emit paintMaterialChanged();
}

}  // namespace ui
