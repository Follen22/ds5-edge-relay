#include "macro_widget.hpp"

#include <QCheckBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QVBoxLayout>

namespace macro {

MacroWidget::MacroWidget(MacroEngine* engine, QWidget* parent)
    : QWidget(parent)
    , engine_(engine)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    // ── Верхняя панель: кнопка записи + статус ──────────────────────────────
    auto* top_bar = new QHBoxLayout;

    btn_record_ = new QPushButton(tr("⏺ Record macro"));
    btn_record_->setCheckable(true);
    btn_record_->setToolTip(tr("Start recording gamepad inputs as a macro"));
    connect(btn_record_, &QPushButton::clicked, this, &MacroWidget::on_record_clicked);

    btn_quick_ = new QPushButton(tr("⚡ Quick macros"));
    btn_quick_->setCheckable(true);
    btn_quick_->setToolTip(tr(
        "Press LFN once to start recording.\n"
        "Press LFN again to stop — the macro is bound to RFN.\n"
        "While active, LFN and RFN bindings are disabled to avoid conflicts."));
    connect(btn_quick_, &QPushButton::toggled, this, [this](bool checked) {
        if (checked) {
            btn_quick_->setStyleSheet(
                "background-color: #1a4a1a; color: #00dd77; border: 1px solid #00dd77;");
            btn_quick_->setText(tr("⚡ Quick macros  ●"));
        } else {
            btn_quick_->setStyleSheet("");
            btn_quick_->setText(tr("⚡ Quick macros"));
        }
        emit quick_macros_toggled(checked);
    });

    lbl_status_ = new QLabel;
    lbl_status_->setStyleSheet("color: gray;");

    top_bar->addWidget(btn_record_);
    top_bar->addSpacing(6);
    top_bar->addWidget(btn_quick_);
    top_bar->addStretch();
    top_bar->addWidget(lbl_status_);
    root->addLayout(top_bar);

    // ── Основная область: список макросов + детали ───────────────────────────
    auto* body = new QHBoxLayout;
    body->setSpacing(8);

    // Список макросов (левая панель, фиксированная ширина)
    macro_list_ = new QListWidget;
    macro_list_->setFixedWidth(160);
    macro_list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(macro_list_, &QListWidget::currentRowChanged,
            this, &MacroWidget::on_macro_selected);
    body->addWidget(macro_list_);

    // Панель деталей (правая панель, растягивается)
    detail_panel_ = new QWidget;
    auto* detail_layout = new QVBoxLayout(detail_panel_);
    detail_layout->setContentsMargins(0, 0, 0, 0);

    // Имя
    auto* name_row = new QHBoxLayout;
    name_row->addWidget(new QLabel(tr("Name:")));
    edit_name_ = new QLineEdit;
    connect(edit_name_, &QLineEdit::editingFinished, this, &MacroWidget::on_name_edited);
    name_row->addWidget(edit_name_);
    detail_layout->addLayout(name_row);

    // Триггер + enabled
    auto* trigger_row = new QHBoxLayout;
    trigger_row->addWidget(new QLabel(tr("Trigger button:")));
    combo_trigger_ = new QComboBox;
    combo_trigger_->addItem(tr("(not set)"));
    for (size_t i = 0; i < BUTTON_COUNT; ++i)
        combo_trigger_->addItem(QString::fromUtf8(
            BUTTON_NAMES[i].data(), static_cast<int>(BUTTON_NAMES[i].size())));
    connect(combo_trigger_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &MacroWidget::on_trigger_changed);

    chk_enabled_ = new QCheckBox(tr("Enabled"));
    chk_enabled_->setChecked(true);
    connect(chk_enabled_, &QCheckBox::toggled, this, &MacroWidget::on_enabled_toggled);

    trigger_row->addWidget(combo_trigger_);
    trigger_row->addStretch();
    trigger_row->addWidget(chk_enabled_);
    detail_layout->addLayout(trigger_row);

    // Таблица событий
    event_table_ = new QTableWidget;
    event_table_->setColumnCount(3);
    event_table_->setHorizontalHeaderLabels({tr("Button"), tr("Action"), tr("Delay (ms)")});
    event_table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    event_table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    event_table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    event_table_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    event_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    event_table_->verticalHeader()->setDefaultSectionSize(28);
    connect(event_table_, &QTableWidget::cellChanged,
            this, &MacroWidget::on_event_delay_changed);
    detail_layout->addWidget(event_table_);

    // Суммарное время
    lbl_total_time_ = new QLabel;
    lbl_total_time_->setStyleSheet("color: gray; font-size: 11px;");
    detail_layout->addWidget(lbl_total_time_);

    detail_panel_->setEnabled(false);
    body->addWidget(detail_panel_, 1);

    root->addLayout(body);
}

void MacroWidget::set_save_path(const QString& path) {
    save_path_ = path;
}

void MacroWidget::load_macros() {
    if (save_path_.isEmpty()) return;
    engine_->load(save_path_);
    macros_ = engine_->macros_snapshot();
    rebuild_macro_list();
}

// ─── Record ─────────────────────────────────────────────────────────────────

void MacroWidget::on_record_clicked() {
    if (btn_record_->isChecked()) {
        engine_->start_recording_from_ui();
        btn_record_->setText(tr("⏹ Stop recording"));
        btn_record_->setStyleSheet("background-color: #cc3333; color: white;");
        lbl_status_->setText(tr("Recording... Press buttons on your gamepad."));
        lbl_status_->setStyleSheet("color: #cc3333; font-weight: bold;");
        emit recording_state_changed(true);
    } else {
        auto result = engine_->stop_recording_from_ui();
        btn_record_->setText(tr("⏺ Record macro"));
        btn_record_->setStyleSheet("");
        lbl_status_->setStyleSheet("color: gray;");
        emit recording_state_changed(false);

        if (result && !result->events.empty()) {
            result->name = QString("Macro %1").arg(macros_.size() + 1);
            macros_.push_back(std::move(*result));
            sync_to_engine();
            rebuild_macro_list();
            macro_list_->setCurrentRow(static_cast<int>(macros_.size()) - 1);
            lbl_status_->setText(tr("Recorded %1 events. Set a trigger button.")
                                 .arg(macros_.back().events.size()));
        } else {
            lbl_status_->setText(tr("No button presses recorded."));
        }
    }
}

// ─── Selection ──────────────────────────────────────────────────────────────

void MacroWidget::on_macro_selected(int row) {
    selected_ = row;
    detail_panel_->setEnabled(row >= 0);

    if (row >= 0 && row < static_cast<int>(macros_.size()))
        show_macro_details(row);
}

// ─── Detail editing ─────────────────────────────────────────────────────────

void MacroWidget::on_trigger_changed(int index) {
    if (selected_ < 0) return;
    auto& m = macros_[selected_];

    if (index == 0)
        m.trigger = Button::COUNT_;
    else
        m.trigger = static_cast<Button>(index - 1);

    sync_to_engine();
    rebuild_macro_list();
}

void MacroWidget::on_enabled_toggled(bool checked) {
    if (selected_ < 0) return;
    macros_[selected_].enabled = checked;
    sync_to_engine();
    rebuild_macro_list();
}

void MacroWidget::on_event_delay_changed(int row, int col) {
    if (selected_ < 0) return;
    if (col != 2) return;
    if (row < 0 || row >= static_cast<int>(display_groups_.size())) return;

    const auto& g = display_groups_[row];
    auto& events  = macros_[selected_].events;

    // Stick groups have non-editable delay cells — ignore
    if (events[g.start].kind != MacroEvent::Kind::Button) return;

    auto* item = event_table_->item(row, col);
    if (!item) return;

    bool ok  = false;
    int  val = item->text().toInt(&ok);
    if (ok && val >= 0) {
        events[g.start].delay_ms = val;
        sync_to_engine();
        update_total_time_label_();
    }
}

void MacroWidget::on_name_edited() {
    if (selected_ < 0) return;
    macros_[selected_].name = edit_name_->text();
    sync_to_engine();
    rebuild_macro_list();
}

void MacroWidget::on_playback_state(bool playing) {
    if (playing)
        lbl_status_->setText(tr("▶ Playing macro..."));
    else
        lbl_status_->setText(tr(""));
}

void MacroWidget::reload_from_engine() {
    macros_ = engine_->macros_snapshot();
    selected_ = -1;
    detail_panel_->setEnabled(false);
    rebuild_macro_list();
    save_macros(); // persist to macros.json
}

// ─── Internal helpers ───────────────────────────────────────────────────────

void MacroWidget::rebuild_macro_list() {
    const int prev = macro_list_->currentRow();
    macro_list_->blockSignals(true);
    macro_list_->clear();

    for (size_t i = 0; i < macros_.size(); ++i) {
        const auto& m = macros_[i];
        QString label = m.name;
        if (m.trigger < Button::COUNT_)
            label += QString(" [%1]").arg(button_to_string(m.trigger));
        if (!m.enabled)
            label += tr(" (off)");

        auto* item = new QListWidgetItem;
        item->setSizeHint(QSize(0, 28));
        if (!m.enabled)
            item->setForeground(Qt::gray);
        macro_list_->addItem(item);

        // Custom widget: label + stretch + × button
        auto* row_w  = new QWidget;
        auto* row_l  = new QHBoxLayout(row_w);
        row_l->setContentsMargins(4, 0, 2, 0);
        row_l->setSpacing(4);

        auto* lbl = new QLabel(label, row_w);
        lbl->setStyleSheet(m.enabled ? "" : "color: gray;");
        row_l->addWidget(lbl, 1);

        auto* del_btn = new QPushButton("✕", row_w);
        del_btn->setFixedSize(16, 16);
        del_btn->setStyleSheet(
            "QPushButton { color: #4a2a2a; border: none; background: transparent; "
            "font-size: 10px; font-weight: bold; padding: 0; }"
            "QPushButton:hover { color: #e05252; }");
        const int idx = static_cast<int>(i);
        connect(del_btn, &QPushButton::clicked, this, [this, idx]() {
            if (idx < static_cast<int>(macros_.size())) {
                macros_.erase(macros_.begin() + idx);
                selected_ = -1;
                detail_panel_->setEnabled(false);
                sync_to_engine();
                rebuild_macro_list();
            }
        });
        row_l->addWidget(del_btn);

        macro_list_->setItemWidget(item, row_w);
    }

    macro_list_->blockSignals(false);

    if (prev >= 0 && prev < macro_list_->count())
        macro_list_->setCurrentRow(prev);
}

void MacroWidget::show_macro_details(int index) {
    if (index < 0 || index >= static_cast<int>(macros_.size()))
        return;

    const auto& m = macros_[index];

    edit_name_->blockSignals(true);
    combo_trigger_->blockSignals(true);
    chk_enabled_->blockSignals(true);
    event_table_->blockSignals(true);

    edit_name_->setText(m.name);

    if (m.trigger < Button::COUNT_)
        combo_trigger_->setCurrentIndex(static_cast<int>(m.trigger) + 1);
    else
        combo_trigger_->setCurrentIndex(0);

    chk_enabled_->setChecked(m.enabled);

    // ── Build display groups ────────────────────────────────────────────────
    // Consecutive events of the same stick type collapse into ONE display row
    // so the table stays clean even with high-resolution trajectory recording.
    display_groups_.clear();
    {
        int i = 0;
        const int n = static_cast<int>(m.events.size());
        while (i < n) {
            const auto kind = m.events[i].kind;
            if (kind != MacroEvent::Kind::Button) {
                // Greedily merge all consecutive same-stick events
                int j = i + 1;
                while (j < n && m.events[j].kind == kind) ++j;
                display_groups_.push_back({i, j - i});
                i = j;
            } else {
                display_groups_.push_back({i, 1});
                ++i;
            }
        }
    }

    event_table_->setRowCount(static_cast<int>(display_groups_.size()));

    int total_ms = 0;
    for (int row = 0; row < static_cast<int>(display_groups_.size()); ++row) {
        const auto& g  = display_groups_[row];
        const auto& ev = m.events[g.start]; // first (or only) event of this group

        QString btn_text;
        QString act_text;
        QColor  act_color      = Qt::white;
        bool    delay_editable = true;

        if (ev.kind == MacroEvent::Kind::Button) {
            btn_text  = button_to_string(ev.button);
            act_text  = ev.pressed ? tr("Press") : tr("Release");
            act_color = ev.pressed ? QColor("#33aa33") : QColor("#aa3333");
            total_ms += ev.delay_ms;
        } else {
            // Stick group: find the peak deflection across all frames in the group
            int    max_dist2 = 0;
            int8_t peak_x = 0, peak_y = 0;
            int    group_delay = 0;
            for (int k = g.start; k < g.start + g.count; ++k) {
                const auto& e = m.events[k];
                const int d2 = e.axis_x * e.axis_x + e.axis_y * e.axis_y;
                if (d2 > max_dist2) { max_dist2 = d2; peak_x = e.axis_x; peak_y = e.axis_y; }
                group_delay += e.delay_ms;
            }
            btn_text  = (ev.kind == MacroEvent::Kind::LeftStick)
                        ? tr("Left Stick") : tr("Right Stick");
            act_text  = g.count == 1
                ? QString("X%1%2  Y%3%4")
                  .arg(ev.axis_x >= 0 ? "+" : "").arg(ev.axis_x)
                  .arg(ev.axis_y >= 0 ? "+" : "").arg(ev.axis_y)
                : QString("X%1%2  Y%3%4  (%5 frames)")
                  .arg(peak_x >= 0 ? "+" : "").arg(peak_x)
                  .arg(peak_y >= 0 ? "+" : "").arg(peak_y)
                  .arg(g.count);
            act_color      = QColor("#7878cc");
            delay_editable = false; // задержка стик-группы нередактируема
            total_ms      += group_delay;
        }

        auto* btn_item = new QTableWidgetItem(btn_text);
        btn_item->setFlags(btn_item->flags() & ~Qt::ItemIsEditable);
        event_table_->setItem(row, 0, btn_item);

        auto* act_item = new QTableWidgetItem(act_text);
        act_item->setFlags(act_item->flags() & ~Qt::ItemIsEditable);
        act_item->setForeground(act_color);
        event_table_->setItem(row, 1, act_item);

        const int disp_delay = (ev.kind == MacroEvent::Kind::Button)
                               ? ev.delay_ms
                               : [&]{ int s=0; for(int k=g.start;k<g.start+g.count;++k) s+=m.events[k].delay_ms; return s; }();
        auto* delay_item = new QTableWidgetItem(QString::number(disp_delay));
        if (!delay_editable)
            delay_item->setFlags(delay_item->flags() & ~Qt::ItemIsEditable);
        event_table_->setItem(row, 2, delay_item);
    }

    lbl_total_time_->setText(tr("Total duration: %1 ms (%2 events)")
                             .arg(total_ms)
                             .arg(m.events.size()));

    edit_name_->blockSignals(false);
    combo_trigger_->blockSignals(false);
    chk_enabled_->blockSignals(false);
    event_table_->blockSignals(false);
}

void MacroWidget::sync_to_engine() {
    engine_->update_macros_from_ui(macros_);
    emit macros_changed(macros_);
    save_macros();
}

void MacroWidget::save_macros() {
    if (!save_path_.isEmpty())
        engine_->save(save_path_);
}

void MacroWidget::update_total_time_label_() {
    if (selected_ < 0 || selected_ >= static_cast<int>(macros_.size()))
        return;
    const auto& events = macros_[selected_].events;
    int total = 0;
    for (const auto& ev : events)
        total += ev.delay_ms;
    lbl_total_time_->setText(tr("Total duration: %1 ms (%2 events)")
                             .arg(total)
                             .arg(events.size()));
}

} // namespace macro
