#pragma once

#include <QThread>
#include <QString>
#include <atomic>

class RelayWorker : public QThread {
    Q_OBJECT

public:
    explicit RelayWorker(QObject* parent = nullptr);
    ~RelayWorker() override;

    void stop();

signals:
    void started_ok(const QString& hidraw_path);
    void error(const QString& message);
    void stats(quint64 input_count, quint64 output_count);
    void device_disconnected();
    void log_message(const QString& message);

protected:
    void run() override;

private:
    std::atomic<bool> running_{false};
};
