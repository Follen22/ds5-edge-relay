#pragma once

#include "ds5_report.hpp"

#include <QThread>
#include <QString>
#include <atomic>
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

signals:
    void started_ok(const QString& hidraw_path);
    void error(const QString& message);
    void stats(quint64 input_count, quint64 output_count);
    void device_disconnected();
    void log_message(const QString& message);

protected:
    void run() override;

private:
    std::atomic<bool>               running_{false};
    std::vector<ds5::ButtonBinding> bindings_;
    // Pending update from the UI thread — swapped into bindings_ on next HID report
    std::vector<ds5::ButtonBinding> pending_bindings_;
    std::atomic<bool>               bindings_dirty_{false};
    std::mutex                      pending_mutex_;
};
