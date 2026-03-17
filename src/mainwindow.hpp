#pragma once

#include <QMainWindow>
#include <QSystemTrayIcon>
#include <QSettings>
#include <QString>
#include <memory>

class RelayWorker;
class BindEditorWidget;
class QLabel;
class QPushButton;
class QCheckBox;
class QGroupBox;
class QSystemTrayIcon;
class QMenu;
class QAction;
class QTimer;

struct LangStrings {
    QString title;
    QString group_status;
    QString status_stopped;
    QString status_starting;
    QString status_running;
    QString status_disconnected;
    QString status_reconnecting;
    QString device_none;
    QString device_prefix;   // "Устройство: " / "Device: "
    QString device_suffix;   // " → 054C:0CE6 (виртуальный)" / " → 054C:0CE6 (virtual)"
    QString btn_start;
    QString btn_stop;
    QString group_settings;
    QString cb_background;
    QString cb_background_tip;
    QString cb_autostart;
    QString cb_autostart_tip;
    QString tray_stopped;
    QString tray_running;
    QString tray_action_show;
    QString tray_action_quit;
    QString tray_notify_active;
    QString tray_notify_disconnected;
    QString tray_notify_reconnecting;
    QString dlg_error_title;
    QString lang_btn_label;
    QString lang_btn_tip;
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
    void on_start_clicked();
    void on_stop_clicked();
    void on_relay_started(const QString& hidraw_path);
    void on_relay_error(const QString& message);
    void on_relay_stats(quint64 input, quint64 output);
    void on_relay_disconnected();
    void on_tray_activated(QSystemTrayIcon::ActivationReason reason);
    void on_autostart_toggled(bool checked);
    void on_lang_clicked();
    void on_reconnect_attempt();

private:
    void setup_ui();
    void setup_tray();
    void update_state(bool running);
    void retranslate_ui();
    void save_settings();
    void load_settings();
    bool is_autostart_enabled() const;
    void set_autostart(bool enable);

    static LangStrings make_ru();
    static LangStrings make_en();

    // UI элементы
    QWidget*     custom_title_bar_ = nullptr;
    QLabel*      title_label_    = nullptr;
    QGroupBox*   status_group_   = nullptr;
    QLabel*      status_label_   = nullptr;
    QLabel*      device_label_   = nullptr;
    QLabel*      stats_label_    = nullptr;
    QPushButton* start_btn_      = nullptr;
    QPushButton* stop_btn_       = nullptr;
    QGroupBox*   settings_group_ = nullptr;
    QCheckBox*   background_cb_  = nullptr;
    QCheckBox*   autostart_cb_   = nullptr;
    QPushButton*      lang_btn_     = nullptr;
    BindEditorWidget* bind_editor_ = nullptr;

    // Системный трей
    QSystemTrayIcon* tray_icon_        = nullptr;
    QMenu*           tray_menu_        = nullptr;
    QAction*         tray_toggle_      = nullptr;
    QAction*         tray_show_action_ = nullptr;
    QAction*         tray_quit_        = nullptr;

    // Воркер и авто-реконнект
    RelayWorker* worker_          = nullptr;
    QTimer*      reconnect_timer_ = nullptr;
    bool         user_stopped_    = false;

    // Состояние
    bool    lang_ru_      = true;
    bool    is_running_   = false;
    QString current_hidraw_;

    QSettings settings_;
};
