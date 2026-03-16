#include "mainwindow.hpp"
#include "relayworker.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QVBoxLayout>
#include <QAction>
#include <QFrame>
#include <QStandardPaths>
#include <QDir>
#include <QFile>

// ── Строки локализации ────────────────────────────────────────────────────────

LangStrings MainWindow::make_ru() {
    LangStrings s;
    s.title                    = "🎮  DualSense Edge Relay";
    s.group_status             = "Статус";
    s.status_stopped           = "⚪  Остановлен";
    s.status_starting          = "🟡  Запуск...";
    s.status_running           = "🟢  Работает";
    s.status_disconnected      = "🔴  Контроллер отключён";
    s.status_reconnecting      = "🟡  Переподключение...";
    s.device_none              = "Устройство: —";
    s.device_prefix            = "Устройство: ";
    s.device_suffix            = " → 054C:0CE6 (виртуальный)";
    s.btn_start                = "▶  Запустить";
    s.btn_stop                 = "⏹  Остановить";
    s.group_settings           = "Настройки";
    s.cb_background            = "Работать в фоне при закрытии окна";
    s.cb_background_tip        = "При закрытии окна демон продолжит работу в системном трее.\n"
                                 "Кликни на иконку в трее чтобы открыть окно снова.";
    s.cb_autostart             = "Автозапуск с системой";
    s.cb_autostart_tip         = "Добавляет приложение в автозапуск через ~/.config/autostart/";
    s.tray_stopped             = "DS5 Edge Relay — остановлен";
    s.tray_running             = "DS5 Edge Relay — работает";
    s.tray_action_show         = "Показать окно";
    s.tray_action_quit         = "Выйти";
    s.tray_notify_active       = "Виртуальный DualSense активен";
    s.tray_notify_disconnected = "Контроллер отключён";
    s.tray_notify_reconnecting = "Переподключение через 3 секунды...";
    s.dlg_error_title          = "Ошибка";
    s.lang_btn_label           = "🇬🇧";
    s.lang_btn_tip             = "Switch to English";
    return s;
}

LangStrings MainWindow::make_en() {
    LangStrings s;
    s.title                    = "🎮  DualSense Edge Relay";
    s.group_status             = "Status";
    s.status_stopped           = "⚪  Stopped";
    s.status_starting          = "🟡  Starting...";
    s.status_running           = "🟢  Running";
    s.status_disconnected      = "🔴  Controller disconnected";
    s.status_reconnecting      = "🟡  Reconnecting...";
    s.device_none              = "Device: —";
    s.device_prefix            = "Device: ";
    s.device_suffix            = " → 054C:0CE6 (virtual)";
    s.btn_start                = "▶  Start";
    s.btn_stop                 = "⏹  Stop";
    s.group_settings           = "Settings";
    s.cb_background            = "Run in background when window is closed";
    s.cb_background_tip        = "The relay keeps running in the system tray after closing.\n"
                                 "Click the tray icon to reopen the window.";
    s.cb_autostart             = "Autostart with system";
    s.cb_autostart_tip         = "Adds the app to autostart via ~/.config/autostart/";
    s.tray_stopped             = "DS5 Edge Relay — stopped";
    s.tray_running             = "DS5 Edge Relay — running";
    s.tray_action_show         = "Show window";
    s.tray_action_quit         = "Quit";
    s.tray_notify_active       = "Virtual DualSense is active";
    s.tray_notify_disconnected = "Controller disconnected";
    s.tray_notify_reconnecting = "Reconnecting in 3 seconds...";
    s.dlg_error_title          = "Error";
    s.lang_btn_label           = "🇷🇺";
    s.lang_btn_tip             = "Переключить на русский";
    return s;
}

// ── Конструктор / деструктор ──────────────────────────────────────────────────

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , settings_("ds5-edge-relay", "ds5-edge-relay")
{
    setWindowTitle("DS5 Edge Relay");
    setMinimumWidth(380);
    setMaximumWidth(380);

    setup_ui();
    setup_tray();
    load_settings();
    retranslate_ui();
    update_state(false);

    reconnect_timer_ = new QTimer(this);
    reconnect_timer_->setSingleShot(true);
    reconnect_timer_->setInterval(3000);
    connect(reconnect_timer_, &QTimer::timeout,
            this, &MainWindow::on_reconnect_attempt);
}

MainWindow::~MainWindow() {
    if (worker_) {
        worker_->stop();
        worker_->wait(3000);
    }
}

// ── Построение UI ─────────────────────────────────────────────────────────────

void MainWindow::setup_ui() {
    auto* central = new QWidget(this);
    setCentralWidget(central);

    auto* layout = new QVBoxLayout(central);
    layout->setSpacing(12);
    layout->setContentsMargins(16, 16, 16, 16);

    // ── Заголовок + кнопка языка ──────────────────────────────────────────────
    auto* title_row = new QHBoxLayout();

    title_label_ = new QLabel(this);
    title_label_->setStyleSheet("font-size: 16px; font-weight: bold;");
    title_row->addWidget(title_label_);
    title_row->addStretch();

    lang_btn_ = new QPushButton(this);
    lang_btn_->setFixedSize(36, 28);
    lang_btn_->setStyleSheet(
        "QPushButton { font-size: 16px; border: 1px solid #ccc; "
        "border-radius: 5px; background: transparent; }"
        "QPushButton:hover { background: rgba(0,0,0,0.06); }");
    title_row->addWidget(lang_btn_);

    layout->addLayout(title_row);

    auto* line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    layout->addWidget(line);

    // ── Статус ────────────────────────────────────────────────────────────────
    status_group_       = new QGroupBox(this);
    auto* status_layout = new QVBoxLayout(status_group_);

    status_label_ = new QLabel(this);
    status_label_->setStyleSheet("font-size: 13px;");
    status_layout->addWidget(status_label_);

    device_label_ = new QLabel(this);
    device_label_->setStyleSheet("color: gray; font-size: 11px;");
    status_layout->addWidget(device_label_);

    stats_label_ = new QLabel("Input: 0  |  Output: 0", this);
    stats_label_->setStyleSheet("color: gray; font-size: 11px;");
    status_layout->addWidget(stats_label_);

    layout->addWidget(status_group_);

    // ── Кнопки управления ─────────────────────────────────────────────────────
    auto* btn_layout = new QHBoxLayout();
    start_btn_ = new QPushButton(this);
    stop_btn_  = new QPushButton(this);

    start_btn_->setMinimumHeight(36);
    stop_btn_->setMinimumHeight(36);
    stop_btn_->setEnabled(false);

    start_btn_->setStyleSheet(
        "QPushButton { background: #2ecc71; color: white; border-radius: 6px; font-weight: bold; }"
        "QPushButton:hover { background: #27ae60; }"
        "QPushButton:disabled { background: #ccc; color: #888; }");
    stop_btn_->setStyleSheet(
        "QPushButton { background: #e74c3c; color: white; border-radius: 6px; font-weight: bold; }"
        "QPushButton:hover { background: #c0392b; }"
        "QPushButton:disabled { background: #ccc; color: #888; }");

    btn_layout->addWidget(start_btn_);
    btn_layout->addWidget(stop_btn_);
    layout->addLayout(btn_layout);

    // ── Настройки ─────────────────────────────────────────────────────────────
    settings_group_       = new QGroupBox(this);
    auto* settings_layout = new QVBoxLayout(settings_group_);

    background_cb_ = new QCheckBox(this);
    settings_layout->addWidget(background_cb_);

    autostart_cb_ = new QCheckBox(this);
    settings_layout->addWidget(autostart_cb_);

    layout->addWidget(settings_group_);

    // ── Сигналы ───────────────────────────────────────────────────────────────
    connect(start_btn_,    &QPushButton::clicked, this, &MainWindow::on_start_clicked);
    connect(stop_btn_,     &QPushButton::clicked, this, &MainWindow::on_stop_clicked);
    connect(lang_btn_,     &QPushButton::clicked, this, &MainWindow::on_lang_clicked);
    connect(autostart_cb_, &QCheckBox::toggled,   this, &MainWindow::on_autostart_toggled);
    connect(background_cb_,&QCheckBox::toggled,   this, [this](bool) { save_settings(); });
}

void MainWindow::setup_tray() {
    const QIcon icon = QIcon::fromTheme("input-gaming",
                       QIcon::fromTheme("media-playback-start"));

    tray_icon_ = new QSystemTrayIcon(icon, this);
    tray_menu_ = new QMenu(this);

    tray_toggle_ = tray_menu_->addAction("");
    connect(tray_toggle_, &QAction::triggered, this, [this]() {
        if (worker_ && worker_->isRunning()) on_stop_clicked();
        else                                 on_start_clicked();
    });

    tray_menu_->addSeparator();

    tray_show_action_ = tray_menu_->addAction("");
    connect(tray_show_action_, &QAction::triggered, this, [this]() {
        showNormal();
        activateWindow();
    });

    tray_menu_->addSeparator();

    tray_quit_ = tray_menu_->addAction("");
    connect(tray_quit_, &QAction::triggered, this, []() {
        QApplication::quit();
    });

    tray_icon_->setContextMenu(tray_menu_);
    tray_icon_->show();

    connect(tray_icon_, &QSystemTrayIcon::activated,
            this, &MainWindow::on_tray_activated);
}

// ── Локализация ───────────────────────────────────────────────────────────────

void MainWindow::retranslate_ui() {
    const LangStrings s = lang_ru_ ? make_ru() : make_en();

    title_label_->setText(s.title);

    status_group_->setTitle(s.group_status);
    device_label_->setText(is_running_
        ? s.device_prefix + current_hidraw_ + s.device_suffix
        : s.device_none);

    start_btn_->setText(s.btn_start);
    stop_btn_->setText(s.btn_stop);

    settings_group_->setTitle(s.group_settings);
    background_cb_->setText(s.cb_background);
    background_cb_->setToolTip(s.cb_background_tip);
    autostart_cb_->setText(s.cb_autostart);
    autostart_cb_->setToolTip(s.cb_autostart_tip);

    lang_btn_->setText(s.lang_btn_label);
    lang_btn_->setToolTip(s.lang_btn_tip);

    tray_icon_->setToolTip(is_running_ ? s.tray_running : s.tray_stopped);
    tray_toggle_->setText(is_running_ ? s.btn_stop : s.btn_start);
    tray_show_action_->setText(s.tray_action_show);
    tray_quit_->setText(s.tray_action_quit);
}

// ── Слоты ─────────────────────────────────────────────────────────────────────

void MainWindow::on_lang_clicked() {
    lang_ru_ = !lang_ru_;
    retranslate_ui();
    save_settings();
}

void MainWindow::on_start_clicked() {
    if (worker_ && worker_->isRunning()) return;
    user_stopped_ = false;
    delete worker_;
    worker_ = new RelayWorker(this);

    connect(worker_, &RelayWorker::started_ok,         this, &MainWindow::on_relay_started);
    connect(worker_, &RelayWorker::error,               this, &MainWindow::on_relay_error);
    connect(worker_, &RelayWorker::stats,               this, &MainWindow::on_relay_stats);
    connect(worker_, &RelayWorker::device_disconnected, this, &MainWindow::on_relay_disconnected);
    connect(worker_, &RelayWorker::log_message, this, [this](const QString& msg) {
        status_label_->setText(msg);
    });

    worker_->start();
    const LangStrings s = lang_ru_ ? make_ru() : make_en();
    status_label_->setText(s.status_starting);
}

void MainWindow::on_stop_clicked() {
    if (!worker_) return;
    user_stopped_ = true;
    reconnect_timer_->stop();
    worker_->stop();
    worker_->wait(3000);
    update_state(false);
}

void MainWindow::on_relay_started(const QString& hidraw_path) {
    current_hidraw_ = hidraw_path;
    update_state(true);

    const LangStrings s = lang_ru_ ? make_ru() : make_en();
    tray_icon_->showMessage("DS5 Edge Relay",
                            s.tray_notify_active,
                            QSystemTrayIcon::Information, 2000);
}

void MainWindow::on_relay_error(const QString& message) {
    update_state(false);
    // При авто-реконнекте не показываем диалог — просто ждём следующей попытки
    if (!user_stopped_) {
        reconnect_timer_->start();
        const LangStrings s = lang_ru_ ? make_ru() : make_en();
        status_label_->setText(s.status_reconnecting);
        return;
    }
    const LangStrings s = lang_ru_ ? make_ru() : make_en();
    QMessageBox::critical(this, s.dlg_error_title, message);
}

void MainWindow::on_relay_stats(quint64 input, quint64 output) {
    stats_label_->setText(
        QString("Input: %1  |  Output: %2").arg(input).arg(output));
}

void MainWindow::on_relay_disconnected() {
    update_state(false);
    const LangStrings s = lang_ru_ ? make_ru() : make_en();

    if (!user_stopped_) {
        // Авто-реконнект — тихо пробуем снова через 3 секунды
        status_label_->setText(s.status_reconnecting);
        tray_icon_->showMessage("DS5 Edge Relay",
                                s.tray_notify_reconnecting,
                                QSystemTrayIcon::Warning, 2000);
        reconnect_timer_->start();
    } else {
        status_label_->setText(s.status_disconnected);
        tray_icon_->showMessage("DS5 Edge Relay",
                                s.tray_notify_disconnected,
                                QSystemTrayIcon::Warning, 2000);
    }
}

void MainWindow::on_reconnect_attempt() {
    on_start_clicked();
}

void MainWindow::on_tray_activated(QSystemTrayIcon::ActivationReason reason) {
    if (reason == QSystemTrayIcon::DoubleClick) {
        showNormal();
        activateWindow();
    }
}

void MainWindow::on_autostart_toggled(bool checked) {
    set_autostart(checked);
    save_settings();
}

// ── Вспомогательные ───────────────────────────────────────────────────────────

void MainWindow::update_state(bool running) {
    is_running_ = running;
    start_btn_->setEnabled(!running);
    stop_btn_->setEnabled(running);
    if (!running) stats_label_->setText("Input: 0  |  Output: 0");
    retranslate_ui();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (background_cb_->isChecked() && QSystemTrayIcon::isSystemTrayAvailable()) {
        hide();
        event->ignore();
    } else {
        if (worker_) {
            worker_->stop();
            worker_->wait(3000);
        }
        event->accept();
    }
}

void MainWindow::save_settings() {
    settings_.setValue("background", background_cb_->isChecked());
    settings_.setValue("lang_ru",    lang_ru_);
}

void MainWindow::load_settings() {
    background_cb_->setChecked(settings_.value("background", false).toBool());
    autostart_cb_->setChecked(is_autostart_enabled());
    lang_ru_ = settings_.value("lang_ru", true).toBool();
}

bool MainWindow::is_autostart_enabled() const {
    const QString path = QStandardPaths::writableLocation(
                             QStandardPaths::ConfigLocation) +
                         "/autostart/ds5-edge-relay.desktop";
    return QFile::exists(path);
}

void MainWindow::set_autostart(bool enable) {
    const QString dir  = QStandardPaths::writableLocation(
                             QStandardPaths::ConfigLocation) + "/autostart";
    const QString path = dir + "/ds5-edge-relay.desktop";

    if (enable) {
        QDir().mkpath(dir);
        QFile f(path);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream s(&f);
            s << "[Desktop Entry]\n"
              << "Type=Application\n"
              << "Name=DS5 Edge Relay\n"
              << "Exec=ds5-edge-relay\n"
              << "Icon=input-gaming\n"
              << "Comment=DualSense Edge HID relay daemon\n"
              << "X-GNOME-Autostart-enabled=true\n";
        }
    } else {
        QFile::remove(path);
    }
}
