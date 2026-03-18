#include "mainwindow.hpp"
#include "relayworker.hpp"
#include "ds5_report.hpp"
#include "bindeditorwidget.hpp"
#include "macro_widget.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QEvent>
#include <QMouseEvent>
#include <QWindow>
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
#include <QPropertyAnimation>
#include <QEasingCurve>
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
    setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
    setAttribute(Qt::WA_TranslucentBackground);
    setWindowTitle("DS5 Edge Relay");
    setMinimumWidth(520);

    // Создаём worker один раз — указатель macro_engine() остаётся стабильным
    worker_ = new RelayWorker(this);

    setup_ui();
    setup_tray();
    load_settings();
    retranslate_ui();
    update_state(false);

    // Подключаем сигналы worker_ после setup_ui() — все виджеты уже созданы
    connect(worker_, &RelayWorker::started_ok,         this, &MainWindow::on_relay_started);
    connect(worker_, &RelayWorker::error,               this, &MainWindow::on_relay_error);
    connect(worker_, &RelayWorker::stats,               this, &MainWindow::on_relay_stats);
    connect(worker_, &RelayWorker::device_disconnected, this, &MainWindow::on_relay_disconnected);
    connect(worker_, &RelayWorker::log_message, this, [this](const QString& msg) {
        status_label_->setText(msg);
    });
    connect(worker_, &RelayWorker::macro_playback_state,
            macro_widget_, &macro::MacroWidget::on_playback_state);
    connect(worker_, &RelayWorker::quick_macro_recorded,
            macro_widget_, &macro::MacroWidget::reload_from_engine);
    connect(macro_widget_, &macro::MacroWidget::quick_macros_toggled,
            this, [this](bool enabled) { worker_->set_quick_macros_enabled(enabled); });

    // Авто-запуск listen mode для записи макросов без активного relay
    connect(macro_widget_, &macro::MacroWidget::recording_state_changed,
            this, [this](bool recording) {
        if (recording && !worker_->isRunning())
            worker_->start_listen_mode();
        else if (!recording && worker_->is_listen_mode())
            worker_->stop();
    });

    reconnect_timer_ = new QTimer(this);
    reconnect_timer_->setSingleShot(true);
    reconnect_timer_->setInterval(3000);
    connect(reconnect_timer_, &QTimer::timeout,
            this, &MainWindow::on_reconnect_attempt);
}

MainWindow::~MainWindow() {
    if (worker_) {
        worker_->stop();
        if (!worker_->wait(3000))
            worker_->terminate();
    }
}

// ── Построение UI ─────────────────────────────────────────────────────────────

void MainWindow::setup_ui() {
    auto* central = new QWidget(this);
    setCentralWidget(central);
    central->setObjectName("CW");
    central->setAttribute(Qt::WA_StyledBackground);
    central->setStyleSheet(
        "QWidget#CW {"
        "  background: #12121f;"
        "  border: 1px solid #2a2a45;"
        "  border-radius: 10px;"
        "}");

    auto* outer = new QVBoxLayout(central);
    outer->setSpacing(0);
    outer->setContentsMargins(0, 0, 0, 0);

    // ── Кастомный title bar ───────────────────────────────────────────────────
    custom_title_bar_ = new QWidget(central);
    custom_title_bar_->setFixedHeight(42);
    custom_title_bar_->setObjectName("TB");
    custom_title_bar_->setAttribute(Qt::WA_StyledBackground);
    custom_title_bar_->setStyleSheet(
        "QWidget#TB {"
        "  background: #0d0d1e;"
        "  border-top-left-radius: 10px;"
        "  border-top-right-radius: 10px;"
        "  border-bottom: 1px solid #2a2a45;"
        "}");
    custom_title_bar_->installEventFilter(this);

    auto* tb = new QHBoxLayout(custom_title_bar_);
    tb->setContentsMargins(14, 0, 6, 0);
    tb->setSpacing(0);

    title_label_ = new QLabel(custom_title_bar_);
    title_label_->setStyleSheet(
        "font-size: 13px; font-weight: bold; color: #e0e0f0; background: transparent;");
    tb->addWidget(title_label_);
    tb->addStretch();

    lang_btn_ = new QPushButton(custom_title_bar_);
    lang_btn_->setFixedSize(34, 28);
    lang_btn_->setStyleSheet(
        "QPushButton { font-size: 16px; border: 1px solid #2a2a45;"
        "border-radius: 6px; background: transparent; color: #e0e0f0; }"
        "QPushButton:hover { background: #2a2a45; border-color: #00c9a7; }");
    tb->addWidget(lang_btn_);
    tb->addSpacing(8);

    auto* tb_sep = new QFrame(custom_title_bar_);
    tb_sep->setFrameShape(QFrame::VLine);
    tb_sep->setFixedHeight(18);
    tb_sep->setStyleSheet("color: #2a2a45;");
    tb->addWidget(tb_sep);
    tb->addSpacing(4);

    auto* min_btn = new QPushButton("—", custom_title_bar_);
    min_btn->setFixedSize(34, 28);
    min_btn->setStyleSheet(
        "QPushButton { background: transparent; color: #7878aa; border: none;"
        "font-size: 14px; border-radius: 4px; }"
        "QPushButton:hover { background: #2a2a45; color: #e0e0f0; }");
    tb->addWidget(min_btn);

    auto* close_btn = new QPushButton("✕", custom_title_bar_);
    close_btn->setFixedSize(34, 28);
    close_btn->setStyleSheet(
        "QPushButton { background: transparent; color: #7878aa; border: none;"
        "font-size: 11px; border-radius: 4px; }"
        "QPushButton:hover { background: #c0392b; color: #ffffff; }");
    tb->addWidget(close_btn);
    tb->addSpacing(2);

    outer->addWidget(custom_title_bar_);

    // ── Горизонтальная обёртка: левый контент + правая макро-панель ───────────
    auto* content_row = new QWidget(central);
    auto* h_layout    = new QHBoxLayout(content_row);
    h_layout->setSpacing(0);
    h_layout->setContentsMargins(0, 0, 0, 0);

    // ── Левая панель: основной контент ────────────────────────────────────────
    auto* content = new QWidget(content_row);
    auto* layout  = new QVBoxLayout(content);
    layout->setSpacing(12);
    layout->setContentsMargins(16, 12, 16, 16);

    // ── Статус ────────────────────────────────────────────────────────────────
    status_group_       = new QGroupBox(content);
    auto* status_layout = new QVBoxLayout(status_group_);

    status_label_ = new QLabel(content);
    status_label_->setStyleSheet("font-size: 13px; color: #e0e0f0; background: transparent;");
    status_layout->addWidget(status_label_);

    device_label_ = new QLabel(content);
    device_label_->setStyleSheet("color: #7878aa; font-size: 11px; background: transparent;");
    status_layout->addWidget(device_label_);

    stats_label_ = new QLabel("Input: 0  |  Output: 0", content);
    stats_label_->setStyleSheet("color: #7878aa; font-size: 11px; background: transparent;");
    status_layout->addWidget(stats_label_);

    layout->addWidget(status_group_);

    // ── Кнопки управления ─────────────────────────────────────────────────────
    auto* btn_layout = new QHBoxLayout();
    start_btn_ = new QPushButton(content);
    stop_btn_  = new QPushButton(content);

    start_btn_->setMinimumHeight(36);
    stop_btn_->setMinimumHeight(36);
    stop_btn_->setEnabled(false);

    start_btn_->setStyleSheet(
        "QPushButton { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "stop:0 #00c9a7, stop:1 #00a896); color: #0a0a1a; border: none;"
        "border-radius: 8px; font-weight: bold; font-size: 13px; }"
        "QPushButton:hover { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "stop:0 #00e0bb, stop:1 #00c9a7); }"
        "QPushButton:disabled { background: #1e1e35; color: #4a4a6a; border: none; }");
    stop_btn_->setStyleSheet(
        "QPushButton { background: #1e1220; color: #e05252; border: 1px solid #5a2a2a;"
        "border-radius: 8px; font-weight: bold; font-size: 13px; }"
        "QPushButton:hover { background: #e05252; color: #fff; border-color: #e05252; }"
        "QPushButton:disabled { background: #1e1e35; color: #4a4a6a; border-color: #2a2a45; }");

    btn_layout->addWidget(start_btn_);
    btn_layout->addWidget(stop_btn_);
    layout->addLayout(btn_layout);

    // ── Настройки ─────────────────────────────────────────────────────────────
    settings_group_       = new QGroupBox(content);
    auto* settings_layout = new QVBoxLayout(settings_group_);

    background_cb_ = new QCheckBox(content);
    settings_layout->addWidget(background_cb_);

    autostart_cb_ = new QCheckBox(content);
    settings_layout->addWidget(autostart_cb_);

    layout->addWidget(settings_group_);

    // ── Bind editor ───────────────────────────────────────────────────────────
    bind_editor_ = new BindEditorWidget(content);
    layout->addWidget(bind_editor_);

    // Левая панель заканчивается здесь
    h_layout->addWidget(content);

    // ── Правая панель: macro_container_ (выдвигается вправо) ─────────────────
    macro_widget_ = new macro::MacroWidget(worker_->macro_engine(), nullptr);
    macro_widget_->set_save_path(
        QDir::homePath() + "/.config/ds5-edge-relay/macros.json");
    macro_widget_->load_macros();

    macro_container_ = new QWidget(content_row);
    macro_container_->setMinimumWidth(0);
    macro_container_->setMaximumWidth(0);
    macro_container_->hide();
    macro_container_->setStyleSheet(
        "border-left: 1px solid #2a2a45;");
    auto* mc_layout = new QVBoxLayout(macro_container_);
    mc_layout->setContentsMargins(8, 4, 8, 8);
    mc_layout->setSpacing(0);
    mc_layout->addWidget(macro_widget_);
    h_layout->addWidget(macro_container_);

    macro_anim_ = new QPropertyAnimation(macro_container_, "maximumWidth", this);
    macro_anim_->setDuration(220);
    macro_anim_->setEasingCurve(QEasingCurve::InOutCubic);
    connect(macro_anim_, &QPropertyAnimation::valueChanged, this, [this](const QVariant&) {
        if (auto* w = window()) w->adjustSize();
    });
    connect(macro_anim_, &QPropertyAnimation::finished, this, [this]() {
        if (macro_anim_->endValue().toInt() == 0)
            macro_container_->hide();
        macro_container_->setMaximumWidth(QWIDGETSIZE_MAX);
        if (auto* w = window()) w->adjustSize();
    });

    outer->addWidget(content_row);

    // ── Сигналы ───────────────────────────────────────────────────────────────
    connect(start_btn_,     &QPushButton::clicked, this, &MainWindow::on_start_clicked);
    connect(stop_btn_,      &QPushButton::clicked, this, &MainWindow::on_stop_clicked);
    connect(lang_btn_,      &QPushButton::clicked, this, &MainWindow::on_lang_clicked);
    connect(autostart_cb_,  &QCheckBox::toggled,   this, &MainWindow::on_autostart_toggled);
    connect(background_cb_, &QCheckBox::toggled,   this, [this](bool) { save_settings(); });
    connect(close_btn,      &QPushButton::clicked, this, &MainWindow::close);
    connect(min_btn,        &QPushButton::clicked, this, &MainWindow::showMinimized);

    // Live-apply bindings to a running worker when the user toggles them
    connect(bind_editor_, &BindEditorWidget::bindingsChanged, this, [this]() {
        if (worker_ && worker_->isRunning())
            worker_->update_bindings(bind_editor_->activeBindings());
    });

    // Анимация выдвижения макро-панели вправо
    connect(bind_editor_, &BindEditorWidget::macrosToggled, this, [this](bool checked) {
        if (checked) {
            macro_container_->setMinimumWidth(0);
            macro_container_->setMaximumWidth(0);
            macro_container_->show();
            macro_anim_->stop();
            macro_anim_->setStartValue(0);
            // Измеряем естественную ширину: снимаем ограничение на миг
            macro_container_->setMaximumWidth(QWIDGETSIZE_MAX);
            const int target = macro_container_->sizeHint().width();
            macro_container_->setMaximumWidth(0);
            macro_anim_->setEndValue(target);
            macro_anim_->start();
        } else {
            macro_anim_->stop();
            macro_anim_->setStartValue(macro_container_->width());
            macro_anim_->setEndValue(0);
            macro_anim_->start();
        }
    });
}

void MainWindow::setup_tray() {
    const QIcon icon = QIcon(":/input-gaming.svg");

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
    bind_editor_->retranslate(lang_ru_);
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

    worker_->set_bindings(bind_editor_->activeBindings());
    worker_->start();
    const LangStrings s = lang_ru_ ? make_ru() : make_en();
    status_label_->setText(s.status_starting);
}

void MainWindow::on_stop_clicked() {
    if (!worker_) return;
    user_stopped_ = true;
    reconnect_timer_->stop();
    worker_->stop();
    if (!worker_->wait(3000))
        worker_->terminate();
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
    bind_editor_->setRunning(running);
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
        tray_icon_->hide();
        event->accept();
        QApplication::quit();
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

bool MainWindow::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == custom_title_bar_
            && event->type() == QEvent::MouseButtonPress) {
        auto* e = static_cast<QMouseEvent*>(event);
        if (e->button() == Qt::LeftButton && windowHandle())
            windowHandle()->startSystemMove();
    }
    return QMainWindow::eventFilter(obj, event);
}
