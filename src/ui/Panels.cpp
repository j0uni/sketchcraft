#include "ui/Panels.h"

#include "skp/Builder.h"
#include "skp/Query.h"
#include "ui/Units.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDateTime>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSlider>
#include <QTimeZone>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace ui {

using skp::ObjId;
using skp::Ref;

QPixmap materialSwatch(const skp::SkpFile& f, Ref material, int size) {
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter g(&pm);
    g.setRenderHint(QPainter::Antialiasing);
    const auto* m = f.get<skp::Material>(material);
    const QRectF r(1, 1, size - 2, size - 2);
    if (m == nullptr) {
        // The default material: white front over blue-grey back.
        const render::Style st = render::readStyle(f);
        g.setBrush(st.faceFront);
        g.setPen(QColor(120, 120, 120));
        g.drawEllipse(r);
        g.setBrush(st.faceBack);
        g.drawPie(r, 225 * 16, 180 * 16);
        return pm;
    }
    QPainterPath clip;
    clip.addEllipse(r);
    g.setClipPath(clip);
    if (auto img = skp::textureImage(f, *m)) {
        g.drawImage(r, img->scaled(size, size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    } else {
        // Checkerboard under see-through colours.
        const QColor c = render::toQColor(skp::materialRgba(*m));
        if (c.alpha() < 255) {
            for (int y = 0; y < size; y += 6) {
                for (int x = 0; x < size; x += 6) g.fillRect(x, y, 6, 6, ((x + y) / 6) % 2 ? Qt::lightGray : Qt::white);
            }
        }
        g.fillRect(r, c);
    }
    g.setClipping(false);
    g.setPen(QColor(120, 120, 120));
    g.setBrush(Qt::NoBrush);
    g.drawEllipse(r);
    return pm;
}

namespace {

QString materialName(const skp::SkpFile& f, Ref m) {
    const auto* mat = f.get<skp::Material>(m);
    return mat ? mat->name : QObject::tr("<Default>");
}

void setButtonColor(QPushButton* b, const QColor& c) {
    QPixmap pm(28, 16);
    pm.fill(c);
    QPainter g(&pm);
    g.setPen(QColor(90, 90, 90));
    g.drawRect(0, 0, 27, 15);
    b->setIcon(QIcon(pm));
    b->setIconSize(pm.size());
}

}  // namespace

// ------------------------------------------------------------------ Entity Info

EntityInfoPanel::EntityInfoPanel(Document* doc, QWidget* parent) : QWidget(parent), doc_(doc) {
    auto* form = new QFormLayout(this);
    kind_ = new QLabel(tr("No Selection"), this);
    QFont bold = kind_->font();
    bold.setBold(true);
    kind_->setFont(bold);
    form->addRow(kind_);
    layer_ = new QComboBox(this);
    form->addRow(tr("Layer:"), layer_);
    material_ = new QLabel(this);
    form->addRow(tr("Material:"), material_);
    name_ = new QLineEdit(this);
    form->addRow(tr("Instance:"), name_);
    measure_ = new QLabel(this);
    form->addRow(tr("Size:"), measure_);
    hidden_ = new QCheckBox(tr("Hidden"), this);
    form->addRow(hidden_);

    connect(layer_, &QComboBox::activated, this, [this](int index) {
        if (!doc_ || updating_ || doc_->selection().empty()) return;
        const auto ls = skp::layers(doc_->model());
        if (index < 0 || static_cast<std::size_t>(index) >= ls.size()) return;
        doc_->checkpoint(tr("Layer"));
        for (ObjId id : doc_->selection()) {
            auto& o = doc_->model().objs.at(id);
            std::visit(
                [&](auto& x) {
                    using T = std::decay_t<decltype(x)>;
                    if constexpr (std::is_same_v<T, skp::Face> || std::is_same_v<T, skp::Edge> ||
                                  std::is_same_v<T, skp::Instance>)
                        x.de.layer = ls[static_cast<std::size_t>(index)];
                },
                o);
        }
        doc_->edited();
    });
    connect(name_, &QLineEdit::editingFinished, this, [this] {
        if (!doc_ || updating_ || doc_->selection().size() != 1) return;
        auto* inst = doc_->model().get<skp::Instance>(Ref{*doc_->selection().begin()});
        if (inst == nullptr || inst->name == name_->text()) return;
        doc_->checkpoint(tr("Rename"));
        inst->name = name_->text();
        doc_->edited();
    });
    connect(hidden_, &QCheckBox::toggled, this, [this](bool on) {
        if (!doc_ || updating_ || doc_->selection().empty()) return;
        doc_->checkpoint(on ? tr("Hide") : tr("Unhide"));
        for (ObjId id : doc_->selection()) {
            std::visit(
                [&](auto& x) {
                    using T = std::decay_t<decltype(x)>;
                    if constexpr (std::is_same_v<T, skp::Face> || std::is_same_v<T, skp::Edge> ||
                                  std::is_same_v<T, skp::Instance>)
                        x.de.flags[skp::kHidden] = on;
                },
                doc_->model().objs.at(id));
        }
        doc_->edited();
    });
    refresh();
}

void EntityInfoPanel::refresh() {
    if (!doc_) return;
    updating_ = true;
    const skp::SkpFile& f = doc_->model();
    const auto& sel = doc_->selection();
    layer_->clear();
    for (ObjId l : skp::layers(f)) {
        if (const auto* lay = f.get<skp::Layer>(Ref{l})) layer_->addItem(lay->name);
    }
    const UnitFormat u = modelUnits(f);
    name_->setEnabled(false);
    name_->clear();
    measure_->clear();
    material_->clear();
    if (sel.empty()) {
        kind_->setText(tr("No Selection"));
        layer_->setEnabled(false);
        hidden_->setEnabled(false);
        hidden_->setChecked(false);
        updating_ = false;
        return;
    }
    layer_->setEnabled(true);
    hidden_->setEnabled(true);
    const ObjId first = *sel.begin();
    const skp::DrawElem* de = nullptr;
    if (sel.size() > 1) {
        kind_->setText(tr("%1 Entities").arg(sel.size()));
    } else if (const auto* face = f.get<skp::Face>(Ref{first})) {
        kind_->setText(tr("Face"));
        de = &face->de;
        measure_->setText(tr("Area %1").arg(formatArea(skp::faceArea(f, *face), u)));
    } else if (const auto* e = f.get<skp::Edge>(Ref{first})) {
        kind_->setText(tr("Edge"));
        de = &e->de;
        const geom::V3 a = geom::V3::from(skp::vertexPos(f, e->v0)), b = geom::V3::from(skp::vertexPos(f, e->v1));
        measure_->setText(tr("Length %1").arg(formatLength(a.dist(b), u)));
    } else if (const auto* inst = f.get<skp::Instance>(Ref{first})) {
        const QString defName = skp::definitionName(f, inst->def);
        kind_->setText(inst->kind == skp::InstanceKind::Group ? tr("Group") : tr("Component (%1)").arg(defName));
        de = &inst->de;
        name_->setEnabled(true);
        name_->setText(inst->name);
    } else {
        kind_->setText(tr("Entity"));
    }
    if (de != nullptr) {
        const auto ls = skp::layers(f);
        for (std::size_t i = 0; i < ls.size(); ++i) {
            if (de->layer == ls[i] || (!de->layer && i == 0)) layer_->setCurrentIndex(static_cast<int>(i));
        }
        material_->setPixmap(QPixmap());
        material_->setText(materialName(f, de->material));
        hidden_->setChecked(de->flags[skp::kHidden]);
    }
    updating_ = false;
}

// ------------------------------------------------------------------ Materials

MaterialsPanel::MaterialsPanel(Document* doc, QWidget* parent) : QWidget(parent), doc_(doc) {
    auto* v = new QVBoxLayout(this);
    current_ = new QLabel(this);
    v->addWidget(current_);
    list_ = new QListWidget(this);
    list_->setViewMode(QListView::IconMode);
    list_->setIconSize(QSize(40, 40));
    list_->setGridSize(QSize(64, 70));
    list_->setResizeMode(QListView::Adjust);
    list_->setMovement(QListView::Static);
    list_->setWordWrap(true);
    v->addWidget(list_, 1);
    auto* rowBox = new QWidget(this);
    auto* row = new QHBoxLayout(rowBox);
    row->setContentsMargins(0, 0, 0, 0);
    auto* create = new QPushButton(tr("Create…"), this);
    auto* edit = new QPushButton(tr("Edit Colour…"), this);
    row->addWidget(create);
    row->addWidget(edit);
    v->addWidget(rowBox);

    connect(list_, &QListWidget::currentRowChanged, this, [this](int row2) {
        if (!doc_ || updating_ || row2 < 0) return;
        const auto mats = skp::materials(doc_->model());
        if (row2 == 0) doc_->setPaintMaterial(std::nullopt);
        else if (static_cast<std::size_t>(row2 - 1) < mats.size()) doc_->setPaintMaterial(mats[static_cast<std::size_t>(row2 - 1)]);
    });
    connect(create, &QPushButton::clicked, this, [this] {
        if (!doc_) return;
        const QColor c = QColorDialog::getColor(QColor(200, 120, 80), this, tr("New Material"));
        if (!c.isValid()) return;
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("New Material"), tr("Name:"), QLineEdit::Normal,
                                                   tr("Material%1").arg(skp::materials(doc_->model()).size() + 1), &ok);
        if (!ok || name.trimmed().isEmpty()) return;
        doc_->checkpoint(tr("New Material"));
        const ObjId id = skp::addMaterial(doc_->model(), name.trimmed(), render::fromQColor(c));
        doc_->edited();
        doc_->setPaintMaterial(id);
    });
    connect(edit, &QPushButton::clicked, this, [this] {
        if (!doc_) return;
        auto* m = doc_->model().get<skp::Material>(doc_->paintMaterial());
        if (m == nullptr) return;
        const QColor c = QColorDialog::getColor(render::toQColor(m->color), this, tr("Edit Material"));
        if (!c.isValid()) return;
        doc_->checkpoint(tr("Edit Material"));
        if (auto* m2 = doc_->model().get<skp::Material>(doc_->paintMaterial())) m2->color = render::fromQColor(c);
        doc_->edited();
    });
    refresh();
}

void MaterialsPanel::refresh() {
    if (!doc_) return;
    updating_ = true;
    const skp::SkpFile& f = doc_->model();
    list_->clear();
    auto add = [&](Ref m) {
        auto* item = new QListWidgetItem(QIcon(materialSwatch(f, m, 40)), materialName(f, m), list_);
        item->setToolTip(materialName(f, m));
    };
    add(std::nullopt);
    int current = 0;
    const auto mats = skp::materials(f);
    for (std::size_t i = 0; i < mats.size(); ++i) {
        add(mats[i]);
        if (doc_->paintMaterial() == mats[i]) current = static_cast<int>(i) + 1;
    }
    list_->setCurrentRow(current);
    current_->setText(tr("Paint with: %1").arg(materialName(f, doc_->paintMaterial())));
    updating_ = false;
}

// ------------------------------------------------------------------ Components

ComponentsPanel::ComponentsPanel(Document* doc, std::function<void(skp::ObjId)> place, QWidget* parent)
    : QWidget(parent), doc_(doc), place_(std::move(place)) {
    auto* v = new QVBoxLayout(this);
    v->addWidget(new QLabel(tr("In Model — double-click to place a copy"), this));
    list_ = new QListWidget(this);
    v->addWidget(list_, 1);
    connect(list_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        if (!doc_ || item == nullptr) return;
        const auto id = static_cast<ObjId>(item->data(Qt::UserRole).toULongLong());
        if (place_) place_(id);
    });
    refresh();
}

void ComponentsPanel::refresh() {
    if (!doc_) return;
    const skp::SkpFile& f = doc_->model();
    list_->clear();
    std::map<ObjId, int> counts;
    for (const skp::Object& o : f.objs) {
        if (const auto* i = std::get_if<skp::Instance>(&o); i != nullptr && i->def) ++counts[*i->def];
    }
    for (ObjId d : skp::definitions(f)) {
        const auto* def = f.get<skp::Definition>(Ref{d});
        if (def == nullptr) continue;
        const bool group = def->name.startsWith(QLatin1String("Group#"));
        if (group) continue;  // groups are not listed, as in the original's Components panel
        auto* item = new QListWidgetItem(tr("%1  (%2)").arg(def->name).arg(counts[d]), list_);
        item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(d));
        item->setToolTip(def->description);
    }
}

// ------------------------------------------------------------------ Styles

StylesPanel::StylesPanel(Document* doc, QWidget* parent) : QWidget(parent), doc_(doc) {
    auto* form = new QFormLayout(this);
    faceStyle_ = new QComboBox(this);
    faceStyle_->addItems({tr("Wireframe"), tr("Hidden Line"), tr("Shaded"), tr("Shaded with Textures"),
                          tr("Monochrome")});
    form->addRow(tr("Face style:"), faceStyle_);
    xray_ = new QCheckBox(tr("X-ray"), this);
    form->addRow(xray_);
    edges_ = new QCheckBox(tr("Edges"), this);
    profiles_ = new QCheckBox(tr("Profiles"), this);
    extension_ = new QCheckBox(tr("Extension"), this);
    backEdges_ = new QCheckBox(tr("Back Edges"), this);
    form->addRow(edges_);
    form->addRow(profiles_);
    form->addRow(extension_);
    form->addRow(backEdges_);
    edgeColor_ = new QPushButton(this);
    form->addRow(tr("Edge colour:"), edgeColor_);
    front_ = new QPushButton(this);
    backFace_ = new QPushButton(this);
    form->addRow(tr("Front colour:"), front_);
    form->addRow(tr("Back colour:"), backFace_);
    background_ = new QPushButton(this);
    form->addRow(tr("Background:"), background_);
    sky_ = new QCheckBox(tr("Sky"), this);
    skyColor_ = new QPushButton(this);
    form->addRow(sky_, skyColor_);
    ground_ = new QCheckBox(tr("Ground"), this);
    groundColor_ = new QPushButton(this);
    form->addRow(ground_, groundColor_);
    axes_ = new QCheckBox(tr("Axes"), this);
    form->addRow(axes_);

    connect(faceStyle_, &QComboBox::activated, this, [this](int i) {
        apply([i](render::Style& s) {
            s.renderMode = i == 0 ? render::kWireframe : i == 1 ? render::kHiddenLine : i == 4 ? render::kMonochrome
                                                                                             : render::kShaded;
            if (i == 2) s.textures = false;
            if (i == 3) s.textures = true;
        });
    });
    auto check = [this](QCheckBox* box, auto member) {
        connect(box, &QCheckBox::toggled, this, [this, member](bool on) { apply([&](render::Style& s) { s.*member = on; }); });
    };
    check(xray_, &render::Style::xray);
    check(edges_, &render::Style::edges);
    check(profiles_, &render::Style::profiles);
    check(extension_, &render::Style::extensions);
    check(backEdges_, &render::Style::backEdges);
    check(sky_, &render::Style::drawSky);
    check(ground_, &render::Style::drawGround);
    check(axes_, &render::Style::axes);
    auto colour = [this](QPushButton* b, auto member, const QString& title) {
        connect(b, &QPushButton::clicked, this, [this, member, title] {
            if (!doc_) return;
            const QColor c = QColorDialog::getColor(doc_->style().*member, this, title);
            if (c.isValid()) apply([&](render::Style& s) { s.*member = c; });
        });
    };
    colour(edgeColor_, &render::Style::edge, tr("Edge Colour"));
    colour(front_, &render::Style::faceFront, tr("Front Face Colour"));
    colour(backFace_, &render::Style::faceBack, tr("Back Face Colour"));
    colour(background_, &render::Style::background, tr("Background Colour"));
    colour(skyColor_, &render::Style::sky, tr("Sky Colour"));
    colour(groundColor_, &render::Style::ground, tr("Ground Colour"));
    refresh();
}

void StylesPanel::apply(const std::function<void(render::Style&)>& change) {
    if (!doc_ || updating_) return;
    render::Style s = doc_->style();
    change(s);
    doc_->setStyle(s);
}

void StylesPanel::refresh() {
    if (!doc_) return;
    updating_ = true;
    const render::Style s = doc_->style();
    int mode = 2;
    if (s.renderMode == render::kWireframe) mode = 0;
    else if (s.renderMode == render::kHiddenLine) mode = 1;
    else if (s.renderMode == render::kMonochrome) mode = 4;
    else if (s.textures) mode = 3;
    faceStyle_->setCurrentIndex(mode);
    xray_->setChecked(s.xray);
    edges_->setChecked(s.edges);
    profiles_->setChecked(s.profiles);
    extension_->setChecked(s.extensions);
    backEdges_->setChecked(s.backEdges);
    sky_->setChecked(s.drawSky);
    ground_->setChecked(s.drawGround);
    axes_->setChecked(s.axes);
    setButtonColor(edgeColor_, s.edge);
    setButtonColor(front_, s.faceFront);
    setButtonColor(backFace_, s.faceBack);
    setButtonColor(background_, s.background);
    setButtonColor(skyColor_, s.sky);
    setButtonColor(groundColor_, s.ground);
    updating_ = false;
}

// ------------------------------------------------------------------ Layers

LayersPanel::LayersPanel(Document* doc, QWidget* parent) : QWidget(parent), doc_(doc) {
    auto* v = new QVBoxLayout(this);
    auto* rowBox = new QWidget(this);
    auto* row = new QHBoxLayout(rowBox);
    row->setContentsMargins(0, 0, 0, 0);
    auto* add = new QPushButton(tr("Add Layer"), this);
    row->addWidget(add);
    row->addStretch(1);
    v->addWidget(rowBox);
    tree_ = new QTreeWidget(this);
    tree_->setHeaderLabels({tr("Name"), tr("Visible"), tr("Colour")});
    tree_->setRootIsDecorated(false);
    tree_->header()->setStretchLastSection(false);
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    v->addWidget(tree_, 1);
    connect(add, &QPushButton::clicked, this, [this] {
        if (!doc_) return;
        doc_->checkpoint(tr("Add Layer"));
        skp::addLayer(doc_->model(), tr("Layer%1").arg(skp::layers(doc_->model()).size()));
        doc_->edited();
    });
    connect(tree_, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int column) {
        if (!doc_ || updating_ || item == nullptr) return;
        const auto id = static_cast<ObjId>(item->data(0, Qt::UserRole).toULongLong());
        auto* l = doc_->model().get<skp::Layer>(Ref{id});
        if (l == nullptr) return;
        if (column == 1) {
            const bool hidden = item->checkState(1) != Qt::Checked;
            if (hidden == l->hidden) return;
            doc_->checkpoint(tr("Layer Visibility"));
            if (auto* l2 = doc_->model().get<skp::Layer>(Ref{id})) l2->hidden = hidden;
            doc_->edited();
        } else if (column == 0 && item->text(0) != l->name && !item->text(0).trimmed().isEmpty()) {
            doc_->checkpoint(tr("Rename Layer"));
            if (auto* l2 = doc_->model().get<skp::Layer>(Ref{id})) l2->name = item->text(0).trimmed();
            doc_->edited();
        }
    });
    refresh();
}

void LayersPanel::refresh() {
    if (!doc_) return;
    updating_ = true;
    const skp::SkpFile& f = doc_->model();
    tree_->clear();
    for (ObjId id : skp::layers(f)) {
        const auto* l = f.get<skp::Layer>(Ref{id});
        if (l == nullptr) continue;
        auto* item = new QTreeWidgetItem(tree_);
        item->setText(0, l->name);
        item->setData(0, Qt::UserRole, QVariant::fromValue<qulonglong>(id));
        item->setCheckState(1, l->hidden ? Qt::Unchecked : Qt::Checked);
        item->setIcon(2, QIcon(materialSwatch(f, l->material, 16)));
        if (l->name != QLatin1String("Layer0")) item->setFlags(item->flags() | Qt::ItemIsEditable);
    }
    updating_ = false;
}

// ------------------------------------------------------------------ Scenes

ScenesPanel::ScenesPanel(Document* doc, std::function<void(skp::ObjId)> go, std::function<void()> add, QWidget* parent)
    : QWidget(parent), doc_(doc), go_(std::move(go)), add_(std::move(add)) {
    auto* v = new QVBoxLayout(this);
    auto* rowBox = new QWidget(this);
    auto* row = new QHBoxLayout(rowBox);
    row->setContentsMargins(0, 0, 0, 0);
    auto* addBtn = new QPushButton(tr("Add Scene"), this);
    row->addWidget(addBtn);
    row->addStretch(1);
    v->addWidget(rowBox);
    list_ = new QListWidget(this);
    v->addWidget(list_, 1);
    connect(addBtn, &QPushButton::clicked, this, [this] {
        if (add_) add_();
    });
    connect(list_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        if (item != nullptr && go_) go_(static_cast<ObjId>(item->data(Qt::UserRole).toULongLong()));
    });
    refresh();
}

void ScenesPanel::refresh() {
    if (!doc_) return;
    list_->clear();
    for (ObjId p : skp::scenes(doc_->model())) {
        auto* item = new QListWidgetItem(skp::sceneName(doc_->model(), p), list_);
        item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(p));
        if (!skp::sceneCamera(doc_->model(), p)) item->setToolTip(tr("This scene does not store a camera."));
    }
}

// ------------------------------------------------------------------ Shadows

ShadowsPanel::ShadowsPanel(Document* doc, QWidget* parent) : QWidget(parent), doc_(doc) {
    auto* form = new QFormLayout(this);
    place_ = new QLabel(this);
    form->addRow(place_);
    time_ = new QSlider(Qt::Horizontal, this);
    time_->setRange(0, 24 * 60 - 1);
    timeLabel_ = new QLabel(this);
    form->addRow(tr("Time:"), time_);
    form->addRow(QString(), timeLabel_);
    date_ = new QSlider(Qt::Horizontal, this);
    date_->setRange(1, 365);
    dateLabel_ = new QLabel(this);
    form->addRow(tr("Date:"), date_);
    form->addRow(QString(), dateLabel_);
    light_ = new QSlider(Qt::Horizontal, this);
    light_->setRange(0, 100);
    dark_ = new QSlider(Qt::Horizontal, this);
    dark_->setRange(0, 100);
    form->addRow(tr("Light:"), light_);
    form->addRow(tr("Dark:"), dark_);
    useSun_ = new QCheckBox(tr("Use sun for shading"), this);
    form->addRow(useSun_);

    auto changed = [this] {
        if (!doc_ || updating_) return;
        render::Style s = doc_->style();
        const QDateTime old = QDateTime::fromSecsSinceEpoch(s.shadowTime, QTimeZone::UTC);
        const QDate d = QDate(old.date().year(), 1, 1).addDays(date_->value() - 1);
        const QDateTime t(d, QTime(time_->value() / 60, time_->value() % 60), QTimeZone::UTC);
        s.shadowTime = static_cast<std::uint32_t>(t.toSecsSinceEpoch());
        s.light = static_cast<std::uint32_t>(light_->value());
        s.dark = static_cast<std::uint32_t>(dark_->value());
        s.useSun = useSun_->isChecked();
        doc_->setStyle(s);
    };
    for (QSlider* sl : {time_, date_, light_, dark_}) connect(sl, &QSlider::valueChanged, this, changed);
    connect(useSun_, &QCheckBox::toggled, this, changed);
    refresh();
}

void ShadowsPanel::refresh() {
    if (!doc_) return;
    updating_ = true;
    const render::Style s = doc_->style();
    const QDateTime t = QDateTime::fromSecsSinceEpoch(s.shadowTime, QTimeZone::UTC);
    time_->setValue(t.time().hour() * 60 + t.time().minute());
    date_->setValue(t.date().dayOfYear());
    timeLabel_->setText(t.time().toString(QStringLiteral("HH:mm")));
    dateLabel_->setText(t.date().toString(QStringLiteral("d MMM")));
    light_->setValue(static_cast<int>(s.light));
    dark_->setValue(static_cast<int>(s.dark));
    useSun_->setChecked(s.useSun);
    const QString city = skp::recString(skp::record(doc_->model(), doc_->model().shadow), 4).value_or(QString());
    place_->setText(tr("%1  (UTC%2%3)")
                        .arg(city.isEmpty() ? tr("Location not set") : city)
                        .arg(s.timeZone < 0 ? QStringLiteral("") : QStringLiteral("+"))
                        .arg(s.timeZone));
    updating_ = false;
}

}  // namespace ui
