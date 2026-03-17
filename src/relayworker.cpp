#include "relayworker.hpp"

#include "hidraw_utils.hpp"
#include "uhid_device.hpp"

#include <cstdio>
#include <filesystem>

// ─── VID:PID ──────────────────────────────────────────────────────────────────
static constexpr uint16_t EDGE_VID = 0x054C;
static constexpr uint16_t EDGE_PID = 0x0DF2;
static constexpr uint16_t DS5_VID  = 0x054C;
static constexpr uint16_t DS5_PID  = 0x0CE6;

// ─── Feature report constants ─────────────────────────────────────────────────
static constexpr uint8_t DS_REPORT_FIRMWARE = 0x20;
static constexpr uint8_t DS_REPORT_PAIRING  = 0x09;
static constexpr size_t  DS_FIRMWARE_SIZE   = 64;
static constexpr size_t  DS_FW_HW_OFFSET    = 24;
static constexpr size_t  DS_FW_FW_OFFSET    = 28;
static constexpr size_t  DS_MAC_OFFSET      = 1;

// ─── Вспомогательные функции ──────────────────────────────────────────────────

static std::pair<uint32_t, uint32_t>
read_versions_from_sysfs(const std::string& hidraw_path) {
    namespace fs = std::filesystem;
    const std::string name = fs::path(hidraw_path).filename().string();
    const std::string base = "/sys/class/hidraw/" + name + "/device/";

    auto read_hex = [](const std::string& path) -> uint32_t {
        FILE* f = fopen(path.c_str(), "r");
        if (!f) return 0;
        uint32_t val = 0;
        (void)fscanf(f, "0x%x", &val);
        fclose(f);
        return val;
    };

    return {read_hex(base + "hardware_version"),
            read_hex(base + "firmware_version")};
}

static std::vector<uint8_t>
make_firmware_report(uint32_t hw, uint32_t fw) {
    std::vector<uint8_t> buf(DS_FIRMWARE_SIZE, 0x00);
    buf[0] = DS_REPORT_FIRMWARE;
    buf[DS_FW_HW_OFFSET + 0] = (hw >>  0) & 0xFF;
    buf[DS_FW_HW_OFFSET + 1] = (hw >>  8) & 0xFF;
    buf[DS_FW_HW_OFFSET + 2] = (hw >> 16) & 0xFF;
    buf[DS_FW_HW_OFFSET + 3] = (hw >> 24) & 0xFF;
    buf[DS_FW_FW_OFFSET + 0] = (fw >>  0) & 0xFF;
    buf[DS_FW_FW_OFFSET + 1] = (fw >>  8) & 0xFF;
    buf[DS_FW_FW_OFFSET + 2] = (fw >> 16) & 0xFF;
    buf[DS_FW_FW_OFFSET + 3] = (fw >> 24) & 0xFF;
    return buf;
}

static std::vector<uint8_t>
patch_pairing_mac(std::vector<uint8_t> report) {
    if (report.size() < DS_MAC_OFFSET + 6) return report;
    report[DS_MAC_OFFSET] ^= 0x02;
    return report;
}

// ─── RelayWorker ──────────────────────────────────────────────────────────────

RelayWorker::RelayWorker(QObject* parent)
    : QThread(parent)
{}

RelayWorker::~RelayWorker() {
    stop();
    wait();
}

void RelayWorker::stop() {
    running_ = false;
}

void RelayWorker::set_bindings(std::vector<ds5::ButtonBinding> bindings) {
    bindings_ = std::move(bindings);
}

void RelayWorker::update_bindings(std::vector<ds5::ButtonBinding> bindings) {
    std::lock_guard<std::mutex> lk(pending_mutex_);
    pending_bindings_ = std::move(bindings);
    bindings_dirty_.store(true, std::memory_order_release);
}

void RelayWorker::run() {
    running_ = true;

    fprintf(stderr, "[DEBUG] run() started\n");

    // ── 1. Найти физический Edge ──────────────────────────────────────────────
    auto edge_info = relay::find_hidraw_device(EDGE_VID, EDGE_PID);
    if (!edge_info) {
        emit error("DualSense Edge (054C:0DF2) not found.\n"
                   "Connect the controller and try again.");
        return;
    }
    fprintf(stderr, "[DEBUG] Found: %s\n", edge_info->path.c_str());

    // ── 2. Читаем версии прошивки из sysfs ───────────────────────────────────
    auto [hw_ver, fw_ver] = read_versions_from_sysfs(edge_info->path);
    if (hw_ver == 0) hw_ver = 0x01000216;
    if (fw_ver == 0) fw_ver = 0x0100008b;

    // ── 3. Открываем физическое устройство ───────────────────────────────────
    const int raw_fd = ::open(edge_info->path.c_str(),
                               O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (raw_fd < 0) {
        emit error(QString("Cannot open %1: %2")
                   .arg(QString::fromStdString(edge_info->path))
                   .arg(strerror(errno)));
        return;
    }
    relay::Fd phys_fd(raw_fd);
    fprintf(stderr, "[DEBUG] Opened fd=%d\n", raw_fd);

    // ── 4. Читаем Report Descriptor ДО revoke ────────────────────────────────
    std::vector<uint8_t> rd;
    try {
        rd = relay::read_report_descriptor(raw_fd);
    } catch (const std::exception& e) {
        emit error(QString("Failed to read report descriptor: %1").arg(e.what()));
        return;
    }
    fprintf(stderr, "[DEBUG] Report descriptor: %zu bytes\n", rd.size());

    // ── 5. Создаём виртуальный DualSense ─────────────────────────────────────
    relay::UhidDevice::Config virt_cfg;
    virt_cfg.name              = "DualSense Wireless Controller";
    virt_cfg.vendor            = DS5_VID;
    virt_cfg.product           = DS5_PID;
    virt_cfg.bus               = edge_info->bus_type;
    virt_cfg.report_descriptor = rd;

    virt_cfg.on_get_report = [raw_fd, hw_ver, fw_ver]
                              (uint8_t report_id) -> std::vector<uint8_t> {
        if (report_id == DS_REPORT_FIRMWARE) {
            return make_firmware_report(hw_ver, fw_ver);
        }
        auto data = relay::get_feature_report(raw_fd, report_id);
        if (report_id == DS_REPORT_PAIRING && !data.empty()) {
            return patch_pairing_mac(std::move(data));
        }
        return data;
    };

    std::unique_ptr<relay::UhidDevice> virt_dev;
    try {
        virt_dev = std::make_unique<relay::UhidDevice>(std::move(virt_cfg));
    } catch (const std::exception& e) {
        emit error(QString("Failed to create virtual device: %1").arg(e.what()));
        return;
    }
    fprintf(stderr, "[DEBUG] Virtual device created, entering relay loop\n");

    emit started_ok(QString::fromStdString(edge_info->path));

    // ── 8. Relay loop ─────────────────────────────────────────────────────────
    constexpr size_t BUF = 1024;
    uint8_t buf[BUF];
    uint64_t input_count = 0, output_count = 0;

    while (running_) {
        // Поллим оба fd одновременно — реагируем мгновенно на любой из них
        pollfd pfds[2];
        pfds[0] = {raw_fd,         POLLIN, 0};
        pfds[1] = {virt_dev->fd(), POLLIN, 0};

        const int ret = ::poll(pfds, 2, 500);
        if (ret < 0) {
            if (errno == EINTR) continue;
            fprintf(stderr, "[DEBUG] poll error: %s\n", strerror(errno));
            break;
        }

        // ── Физический Edge → Виртуальный DS5 ────────────────────────────────
        if (pfds[0].revents & POLLIN) {
            const ssize_t n = ::read(raw_fd, buf, BUF);
            if (n > 0) {
                // Swap in updated bindings from the UI thread if available
                if (bindings_dirty_.load(std::memory_order_acquire)) {
                    std::lock_guard<std::mutex> lk(pending_mutex_);
                    bindings_ = std::move(pending_bindings_);
                    bindings_dirty_.store(false, std::memory_order_relaxed);
                }
#ifdef EDGE_BTN_DEBUG
                // Дамп изменившихся байт (bytes 11+) для нахождения Edge-кнопок.
                // Запусти с EDGE_BTN_DEBUG=1, нажимай L4/LB/R4/RB по одной.
                static uint8_t prev_dbg[1024] = {};
                for (ssize_t i = 11; i < n; ++i) {
                    if (buf[i] != prev_dbg[i]) {
                        fprintf(stderr,
                            "[EDGE] byte[%3zd] 0x%02X→0x%02X  diff=0x%02X  bits=",
                            i, prev_dbg[i], (unsigned)buf[i],
                            prev_dbg[i] ^ (unsigned)buf[i]);
                        for (int b = 7; b >= 0; --b)
                            fputc('0' + ((buf[i] >> b) & 1), stderr);
                        fputc('\n', stderr);
                    }
                }
                memcpy(prev_dbg, buf, static_cast<size_t>(n));
#endif
                ds5::apply_bindings(buf, static_cast<size_t>(n), bindings_);
                // Clear Edge-exclusive bits (byte 10, upper nibble) before forwarding
                // to virtual DualSense — it has no LFN/RFN/LB/RB in its HID profile.
                if (static_cast<size_t>(n) > 10 && buf[0] == 0x01)
                    buf[10] &= 0x0F;
                virt_dev->send_input_report(buf, static_cast<size_t>(n));
                ++input_count;
                if (input_count % 1000 == 0)
                    emit stats(input_count, output_count);
            } else if (n == 0 || (n < 0 && errno != EAGAIN)) {
                fprintf(stderr, "[DEBUG] raw_fd read error: n=%zd errno=%d (%s)\n",
                        n, errno, strerror(errno));
                break;
            }
        }

        if (pfds[0].revents & (POLLHUP | POLLERR)) {
            fprintf(stderr, "[DEBUG] Physical device disconnected\n");
            break;
        }

        // ── Виртуальный DS5 output → Физический Edge (haptics, LED, triggers) ─
        if (pfds[1].revents & POLLIN) {
            auto output = virt_dev->process_event();
            if (output && !output->empty()) {
                (void)::write(raw_fd, output->data(), output->size());
                ++output_count;
            }
        }
    }

    fprintf(stderr, "[DEBUG] Relay loop exited. input=%llu output=%llu\n",
            (unsigned long long)input_count, (unsigned long long)output_count);

    emit device_disconnected();
}
