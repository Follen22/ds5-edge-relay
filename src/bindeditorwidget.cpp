#include "bindeditorwidget.hpp"
#include "gamepadwidget.hpp"
#include "bindstorage.hpp"

#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QScrollArea>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QFrame>

static QString bind_label(const BindStorage::BindRecord& r)
{
    static const QHash<QString, QString> sym = {
        {"Triangle", "△"}, {"Circle", "○"}, {"Cross", "✕"}, {"Square", "□"},
        {"L1", "L1"}, {"R1", "R1"}, {"L2", "L2"}, {"R2", "R2"},
        {"L3", "L3"}, {"R3", "R3"}, {"Options", "Opt"}, {"Create", "Cre"},
        {"PS", "PS"}, {"Touchpad", "Pad"},
        {"DPadUp", "↑"}, {"DPadDown", "↓"}, {"DPadLeft", "←"}, {"DPadRight", "→"},
        {"L4", "L4"}, {"R4", "R4"}, {"LB", "LB"}, {"RB", "RB"},
        {"LFN", "Fn←"}, {"RFN", "Fn→"},
    };
    QString s = sym.value(r.trigger, r.trigger) + "→";
    QStringList acts;
    for (const auto& a : r.actions) acts << sym.value(a, a);
    s += acts.join("+");
    return s;
}

BindEditorWidget::BindEditorWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // ── Header checkbox ───────────────────────────────────────────────────────
    use_bindings_cb_ = new QCheckBox("Использовать бинды", this);
    use_bindings_cb_->setStyleSheet("font-weight: bold; padding: 4px 0; color: #e0e0f0;");
    outer->addWidget(use_bindings_cb_);

    // ── Animated content area ─────────────────────────────────────────────────
    content_ = new QWidget(this);
    content_->setMinimumHeight(0);
    content_->hide();
    auto* cl = new QVBoxLayout(content_);
    cl->setContentsMargins(0, 8, 0, 0);
    cl->setSpacing(8);

    gamepad_ = new GamepadWidget(content_);
    cl->addWidget(gamepad_);

    status_label_ = new QLabel(content_);
    status_label_->setStyleSheet("color: #7878aa; font-size: 11px; background: transparent;");
    status_label_->setAlignment(Qt::AlignCenter);
    cl->addWidget(status_label_);

    auto* btn_row  = new QHBoxLayout();
    set_bind_btn_  = new QPushButton("Задать бинд", content_);
    set_bind_btn_->setStyleSheet(
        "QPushButton { background: transparent; color: #00c9a7; border: 1px solid #00c9a7;"
        "border-radius: 6px; padding: 4px 10px; }"
        "QPushButton:hover { background: #00c9a7; color: #0a0a1a; }"
        "QPushButton:disabled { color: #4a4a6a; border-color: #2a2a45; }");
    apply_btn_     = new QPushButton("Применить",   content_);
    apply_btn_->setEnabled(false);
    apply_btn_->setStyleSheet(
        "QPushButton { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "stop:0 #00c9a7,stop:1 #00a896); color: #0a0a1a; border: none;"
        "border-radius: 6px; padding: 4px 10px; font-weight: bold; }"
        "QPushButton:hover { background: #00e0bb; }"
        "QPushButton:disabled { background: #1e1e35; color: #4a4a6a; }");
    btn_row->addWidget(set_bind_btn_);
    btn_row->addStretch();
    btn_row->addWidget(apply_btn_);
    cl->addLayout(btn_row);

    // ── Bind list (horizontal scroll) ─────────────────────────────────────────
    auto* scroll = new QScrollArea(content_);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFixedHeight(40);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet(
        "QScrollArea { background: #191928; border: 1px solid #2a2a45; border-radius: 8px; }"
        "QScrollArea > QWidget > QWidget { background: #191928; }");

    list_area_   = new QWidget(scroll);
    list_layout_ = new QHBoxLayout(list_area_);
    list_layout_->setContentsMargins(4, 2, 4, 2);
    list_layout_->setSpacing(8);
    scroll->setWidget(list_area_);
    cl->addWidget(scroll);

    outer->addWidget(content_);

    // ── Animation ─────────────────────────────────────────────────────────────
    anim_ = new QPropertyAnimation(content_, "maximumHeight", this);
    anim_->setDuration(200);
    anim_->setEasingCurve(QEasingCurve::InOutCubic);
    connect(anim_, &QPropertyAnimation::valueChanged, this, [this](const QVariant&) {
        if (auto* w = window()) w->resize(w->sizeHint());
    });
    connect(anim_, &QPropertyAnimation::finished, this, [this]() {
        if (!use_bindings_cb_->isChecked())
            content_->hide();
        content_->setMaximumHeight(QWIDGETSIZE_MAX);
        if (auto* w = window()) w->adjustSize();
    });

    // ── Signal connections ────────────────────────────────────────────────────
    connect(use_bindings_cb_, &QCheckBox::toggled,
            this, &BindEditorWidget::on_use_bindings_toggled);
    connect(set_bind_btn_, &QPushButton::clicked,
            this, &BindEditorWidget::on_set_bind_clicked);
    connect(apply_btn_, &QPushButton::clicked,
            this, &BindEditorWidget::on_apply_clicked);
    connect(gamepad_, &GamepadWidget::triggerSelected,
            this, &BindEditorWidget::on_trigger_selected);
    connect(gamepad_, &GamepadWidget::actionToggled,
            this, &BindEditorWidget::on_action_toggled);

    // ── Load saved binds ──────────────────────────────────────────────────────
    binds_ = BindStorage::load();
    rebuild_list();
}

void BindEditorWidget::on_use_bindings_toggled(bool checked)
{
    if (checked) {
        content_->setMinimumHeight(0);
        content_->setMaximumHeight(0);
        content_->show();
        anim_->stop();
        anim_->setStartValue(0);
        anim_->setEndValue(content_->sizeHint().height());
        anim_->start();
    } else {
        if (editor_state_ != EditorState::IDLE)
            set_editor_state(EditorState::IDLE);
        anim_->stop();
        anim_->setStartValue(content_->height());
        anim_->setEndValue(0);
        anim_->start();
    }
}

void BindEditorWidget::on_set_bind_clicked()
{
    pending_trigger_name_.clear();
    pending_trigger_btn_  = {0, 0};
    pending_action_names_.clear();
    gamepad_->clearSelection();
    set_editor_state(EditorState::SELECT_TRIGGER);
}

void BindEditorWidget::on_apply_clicked()
{
    if (pending_trigger_btn_.mask == 0 || pending_action_names_.isEmpty()) return;

    BindStorage::BindRecord r;
    r.enabled = true;
    r.trigger = pending_trigger_name_;
    r.actions = pending_action_names_;
    binds_.append(r);

    save_binds();
    rebuild_list();
    set_editor_state(EditorState::IDLE);
}

void BindEditorWidget::on_trigger_selected(ds5::ButtonBit btn, const QString& name)
{
    pending_trigger_btn_  = btn;
    pending_trigger_name_ = name;
    set_editor_state(EditorState::SELECT_ACTIONS);
}

void BindEditorWidget::on_action_toggled(ds5::ButtonBit /*btn*/,
                                          const QString& name, bool added)
{
    if (added) {
        if (!pending_action_names_.contains(name))
            pending_action_names_.append(name);
    } else {
        pending_action_names_.removeAll(name);
    }
    update_status_text();
    apply_btn_->setEnabled(!running_ && !pending_action_names_.isEmpty());
}

void BindEditorWidget::rebuild_list()
{
    while (list_layout_->count() > 0) {
        auto* item = list_layout_->takeAt(0);
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    if (binds_.isEmpty()) {
        auto* lbl = new QLabel(lang_ru_ ? "Нет биндов" : "No bindings", list_area_);
        lbl->setStyleSheet("color: gray;");
        list_layout_->addWidget(lbl);
    } else {
        for (qsizetype i = 0; i < binds_.size(); ++i) {
            auto* item_w  = new QWidget(list_area_);
            auto* item_l  = new QHBoxLayout(item_w);
            item_l->setContentsMargins(0, 0, 0, 0);
            item_l->setSpacing(2);

            auto* cb = new QCheckBox(bind_label(binds_[i]), item_w);
            cb->setChecked(binds_[i].enabled);
            connect(cb, &QCheckBox::toggled, this, [this, i](bool checked) {
                if (i < binds_.size()) {
                    binds_[i].enabled = checked;
                    save_binds();
                }
            });

            auto* del_btn = new QPushButton("✕", item_w);
            del_btn->setFixedSize(18, 18);
            del_btn->setStyleSheet(
                "QPushButton { color: #4a2a2a; border: none; background: transparent; "
                "font-size: 11px; font-weight: bold; padding: 0; }"
                "QPushButton:hover { color: #e05252; }");
            connect(del_btn, &QPushButton::clicked, this, [this, i]() {
                if (i < binds_.size()) {
                    binds_.removeAt(i);
                    save_binds();
                    rebuild_list();
                }
            });

            item_l->addWidget(del_btn);
            item_l->addWidget(cb);
            list_layout_->addWidget(item_w);
        }
    }
    list_layout_->addStretch();
}

void BindEditorWidget::set_editor_state(EditorState state)
{
    editor_state_ = state;

    switch (state) {
    case EditorState::IDLE:
        gamepad_->setMode(GamepadWidget::Mode::IDLE);
        gamepad_->clearSelection();
        pending_trigger_name_.clear();
        pending_trigger_btn_  = {0, 0};
        pending_action_names_.clear();
        update_status_text();
        break;
    case EditorState::SELECT_TRIGGER:
        gamepad_->setMode(GamepadWidget::Mode::SELECT_TRIGGER);
        update_status_text();
        break;
    case EditorState::SELECT_ACTIONS:
        gamepad_->setMode(GamepadWidget::Mode::SELECT_ACTIONS);
        update_status_text();
        break;
    }

    set_bind_btn_->setEnabled(!running_ && state == EditorState::IDLE);
    apply_btn_->setEnabled(!running_
                           && state == EditorState::SELECT_ACTIONS
                           && !pending_action_names_.isEmpty());
}

void BindEditorWidget::setRunning(bool running)
{
    running_ = running;
    if (running) {
        set_editor_state(EditorState::IDLE);
    } else {
        set_bind_btn_->setEnabled(editor_state_ == EditorState::IDLE);
        apply_btn_->setEnabled(editor_state_ == EditorState::SELECT_ACTIONS
                               && !pending_action_names_.isEmpty());
    }
}

std::vector<ds5::ButtonBinding> BindEditorWidget::activeBindings() const
{
    if (!use_bindings_cb_->isChecked()) return {};

    std::vector<ds5::ButtonBinding> result;
    for (const auto& r : binds_) {
        if (!r.enabled) continue;

        const ds5::ButtonBit src = BindStorage::nameToBtn(r.trigger);
        if (src.mask == 0) continue;

        ds5::ButtonBinding b;
        b.source      = src;
        b.passthrough = false;
        if (r.trigger == "L2")
            b.clear_analog = ds5::OFFSET_L2_ANALOG;
        else if (r.trigger == "R2")
            b.clear_analog = ds5::OFFSET_R2_ANALOG;
        else
            b.clear_analog = 0;

        for (const auto& an : r.actions) {
            const ds5::ButtonBit t = BindStorage::nameToBtn(an);
            if (t.mask != 0) b.targets.push_back(t);
        }

        if (!b.targets.empty())
            result.push_back(std::move(b));
    }
    return result;
}

void BindEditorWidget::save_binds()
{
    BindStorage::save(binds_);
    emit bindingsChanged();
}

void BindEditorWidget::retranslate(bool ru)
{
    lang_ru_ = ru;
    use_bindings_cb_->setText(ru ? "Использовать бинды" : "Use bindings");
    set_bind_btn_->setText(ru ? "Задать бинд" : "Add binding");
    apply_btn_->setText(ru ? "Применить" : "Apply");
    update_status_text();
    rebuild_list();
}

void BindEditorWidget::update_status_text()
{
    switch (editor_state_) {
    case EditorState::IDLE:
        status_label_->setText("");
        break;
    case EditorState::SELECT_TRIGGER:
        status_label_->setText(lang_ru_
            ? "Выберите кнопку-триггер"
            : "Select trigger button");
        break;
    case EditorState::SELECT_ACTIONS: {
        const QString base = QStringLiteral("Trigger: ") + pending_trigger_name_ +
            (lang_ru_ ? " — выберите цели" : " — select targets");
        status_label_->setText(pending_action_names_.isEmpty()
            ? base
            : base + " (" + QString::number(pending_action_names_.size()) +
              (lang_ru_ ? " выбрано)" : " selected)"));
        break;
    }
    }
}
