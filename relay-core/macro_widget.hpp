#pragma once

#include "macro_engine.hpp"
#include "macro_types.hpp"

#include <QComboBox>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QWidget>

#include <memory>

class QCheckBox;
class QLineEdit;

namespace macro {

/// Виджет управления макросами.
/// Встраивается в MainWindow аналогично BindEditorWidget.
class MacroWidget : public QWidget {
    Q_OBJECT

public:
    explicit MacroWidget(MacroEngine* engine, QWidget* parent = nullptr);

    /// Путь к файлу сохранения макросов.
    void set_save_path(const QString& path);

    /// Загрузить макросы из файла (вызвать после конструктора).
    void load_macros();

signals:
    /// Уведомление для RelayWorker — макросы обновились.
    void macros_changed(std::vector<macro::Macro> macros);

    /// Запись начата/остановлена — можно обновлять индикатор в MainWindow.
    void recording_state_changed(bool recording);

    /// Функция быстрых макросов включена/выключена.
    void quick_macros_toggled(bool enabled);

public slots:
    /// Вызывать из relay thread когда playback стартует/завершается.
    void on_playback_state(bool playing);

    /// Перезагрузить список макросов из движка (напр. после quick-record через LFN).
    void reload_from_engine();

private slots:
    void on_record_clicked();
    void on_macro_selected(int row);
    void on_trigger_changed(int index);
    void on_enabled_toggled(bool checked);
    void on_event_delay_changed(int row, int col);
    void on_name_edited();

private:
    void rebuild_macro_list();
    void show_macro_details(int index);
    void sync_to_engine();
    void save_macros();
    void update_total_time_label_();

    // Mapping from event-table row → underlying event indices.
    // Consecutive stick events of the same type are collapsed into one display row.
    struct DisplayGroup {
        int start = 0; // first event index in macros_[selected_].events
        int count = 1; // how many events this row represents
    };

    MacroEngine*   engine_;
    QString        save_path_;
    std::vector<Macro> macros_;
    int            selected_ = -1;
    std::vector<DisplayGroup> display_groups_; // rebuilt in show_macro_details

    // ── UI elements ─────────────────────────────────────────────────────────
    QListWidget*   macro_list_     = nullptr;
    QPushButton*   btn_record_     = nullptr;
    QPushButton*   btn_quick_      = nullptr;
    QLabel*        lbl_status_     = nullptr;

    // Detail panel
    QWidget*       detail_panel_   = nullptr;
    QLineEdit*     edit_name_      = nullptr;
    QComboBox*     combo_trigger_  = nullptr;
    QCheckBox*     chk_enabled_    = nullptr;
    QTableWidget*  event_table_    = nullptr;
    QLabel*        lbl_total_time_ = nullptr;
};

} // namespace macro
