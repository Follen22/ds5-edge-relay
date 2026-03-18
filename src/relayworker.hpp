#pragma once

#include "ds5_report.hpp"
#include "evdev_grabber.hpp"
#include "macro_engine.hpp"
#include "uhid_device.hpp"

#include <QThread>
#include <QString>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

class RelayWorker : public QThread {
    Q_OBJECT

public:
    explicit RelayWorker(QObject* parent = nullptr);
    ~RelayWorker() override;

    void stop();
    void set_bindings(std::vector<ds5::ButtonBinding> bindings);
    // Thread-safe: can be called from the UI thread while the worker is running
    void update_bindings(std::vector<ds5::ButtonBinding> bindings);

    /// Запустить в режиме прослушивания (только чтение HID для записи макросов,
    /// без создания виртуального устройства). Останавливается автоматически.
    void start_listen_mode();
    [[nodiscard]] bool is_listen_mode() const { return listen_only_.load(); }

    /// Включить/выключить функцию быстрых макросов (LFN=запись, RFN=воспроизведение).
    /// Когда включено: LFN/RFN-бинды игнорируются, LFN обрабатывается как триггер записи.
    void set_quick_macros_enabled(bool enabled) { quick_macros_enabled_.store(enabled); }

    /// Макродвижок — создаётся и принадлежит RelayWorker,
    /// но доступен из UI через указатель (потокобезопасные методы).
    [[nodiscard]] macro::MacroEngine* macro_engine() { return &macro_engine_; }

signals:
    void started_ok(const QString& hidraw_path);
    void error(const QString& message);
    void stats(quint64 input_count, quint64 output_count);
    void device_disconnected();
    void log_message(const QString& message);
    void macro_playback_state(bool playing);
    /// Fired after a quick-record macro (LFN toggle) is injected into the engine.
    /// MacroWidget should reload its list from the engine upon receiving this.
    void quick_macro_recorded();

protected:
    void run() override;

private:
    std::atomic<bool>               running_{false};
    std::atomic<bool>               listen_only_{false};
    EvdevGrabber                    evdev_grabber_;
    macro::MacroEngine              macro_engine_;
    std::vector<ds5::ButtonBinding> bindings_;
    // Pending update from the UI thread — swapped into bindings_ on next HID report
    std::vector<ds5::ButtonBinding> pending_bindings_;
    std::atomic<bool>               bindings_dirty_{false};
    std::mutex                      pending_mutex_;

    // ── Virtual device — persisted across physical reconnects ─────────────────
    // Keeping virt_dev_ alive means the game's evdev fd stays valid even when
    // the physical controller is unplugged and replugged. Only destroyed on
    // explicit user stop (running_ == false at end of run()).
    std::unique_ptr<relay::UhidDevice> virt_dev_;
    int      phys_raw_fd_ = -1;  // current physical hidraw fd (for GET_REPORT callback)
    uint32_t phys_hw_ver_ = 0;   // physical firmware hw version
    uint32_t phys_fw_ver_ = 0;   // physical firmware fw version

    // ── LFN quick-record state ────────────────────────────────────────────────
    bool                            lfn_prev_              = false;
    std::atomic<bool>               quick_macros_enabled_  {false};
};
