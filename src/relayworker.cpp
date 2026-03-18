#include "relayworker.hpp"

#include "hidraw_utils.hpp"
#include "uhid_device.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>

#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

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

// ─── Helpers ──────────────────────────────────────────────────────────────────

// После создания виртуального DS5 hid-playstation создаёт evdev-ноды через
// HID-слой по дескриптору — flat=0 (дескриптор не кодирует мёртвую зону).
// Реальный DS5 получает flat=4 от hid-playstation напрямую.
// Фиксируем вручную через ioctl: находим gamepad-ноду и ставим flat=4.
// Запускается в отдельном треде чтобы не блокировать relay loop.
static void fix_virtual_ds5_deadzone(uint16_t vendor, uint16_t product) {
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Scan sysfs instead of probing event0..N with a hardcoded limit.
    // hid-playstation registers evdev nodes after our UHID device is started;
    // sysfs is the authoritative source for device enumeration.
    namespace fs = std::filesystem;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator("/sys/class/input", ec)) {
        const auto name = entry.path().filename().string();
        if (name.compare(0, 5, "event") != 0)
            continue;

        // Read VID:PID from sysfs — skip entries without id/ (e.g. mice, keyboards).
        auto read_hex = [](const fs::path& p) -> uint16_t {
            std::ifstream f(p);
            unsigned v = 0;
            f >> std::hex >> v;
            return f ? static_cast<uint16_t>(v) : 0;
        };
        const auto id_dir = entry.path() / "device" / "id";
        if (read_hex(id_dir / "vendor") != vendor ||
            read_hex(id_dir / "product") != product)
            continue;

        const std::string dev = "/dev/input/" + name;
        const int fd = ::open(dev.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;

        // Only the gamepad evdev node has ABS_RX — skip touchpad / motion nodes.
        unsigned long abs_bits[(ABS_MAX / (sizeof(unsigned long) * 8)) + 1] = {};
        if (::ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs_bits)), abs_bits) < 0 ||
            !(abs_bits[ABS_RX / (sizeof(unsigned long) * 8)] &
              (1UL << (ABS_RX % (sizeof(unsigned long) * 8))))) {
            ::close(fd);
            continue;
        }

        for (int code : {ABS_X, ABS_Y, ABS_RX, ABS_RY}) {
            struct input_absinfo ai{};
            if (::ioctl(fd, EVIOCGABS(code), &ai) == 0) {
                ai.flat = 4;
                if (::ioctl(fd, EVIOCSABS(code), &ai) == 0)
                    fprintf(stderr, "[ABS] %s ABS_%s flat→4\n", dev.c_str(),
                            code == ABS_X  ? "X"  : code == ABS_Y  ? "Y" :
                            code == ABS_RX ? "RX" : "RY");
            }
        }
        ::close(fd);
    }
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

void RelayWorker::start_listen_mode() {
    if (isRunning()) return;
    listen_only_.store(true);
    start();
}

void RelayWorker::run() {
    running_ = true;

    // ── Listen-only mode: читаем HID только для записи макросов ──────────────
    if (listen_only_.load()) {
        auto edge_info = relay::find_hidraw_device(EDGE_VID, EDGE_PID);
        if (!edge_info) {
            listen_only_.store(false);
            return;
        }
        const int raw_fd = ::open(edge_info->path.c_str(),
                                   O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (raw_fd < 0) {
            listen_only_.store(false);
            return;
        }
        relay::Fd phys_fd(raw_fd);

        constexpr size_t BUF = 1024;
        uint8_t buf[BUF];
        while (running_ && macro_engine_.is_recording()) {
            pollfd pfd = {raw_fd, POLLIN, 0};
            const int ret = ::poll(&pfd, 1, 100);
            if (ret < 0) { if (errno == EINTR) continue; break; }
            if (pfd.revents & POLLIN) {
                const ssize_t n = ::read(raw_fd, buf, BUF);
                if (n > 0)
                    macro_engine_.process_report(buf, static_cast<size_t>(n));
                else if (n == 0 || (n < 0 && errno != EAGAIN))
                    break;
            }
            if (pfd.revents & (POLLHUP | POLLERR)) break;
        }
        listen_only_.store(false);
        running_ = false;
        return;
    }

    fprintf(stderr, "[DEBUG] run() started\n");
    lfn_prev_ = false; // reset quick-record toggle state on each run

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

    // ── 3b. Блокируем hidraw от новых открытий (SDL/Steam используют hidraw) ──
    // raw_fd уже открыт — он продолжит работать. Новые открытия получат EACCES.
    if (evdev_grabber_.lock_hidraw(edge_info->path) < 0)
        fprintf(stderr, "[DEBUG] hidraw lock failed (не root?)\n");

    // ── Сохраняем fd и версии прошивки как члены — используются callback'ом ──
    phys_raw_fd_ = raw_fd;
    phys_hw_ver_ = hw_ver;
    phys_fw_ver_ = fw_ver;

    // ── 4+5. Создаём виртуальный DualSense (только на первом запуске) ────────
    // На реконнекте virt_dev_ уже существует — пропускаем, сохраняя fd игры.
    if (!virt_dev_) {
        std::vector<uint8_t> rd;
        try {
            rd = relay::read_report_descriptor(raw_fd);
        } catch (const std::exception& e) {
            emit error(QString("Failed to read report descriptor: %1").arg(e.what()));
            return;
        }
        fprintf(stderr, "[DEBUG] Report descriptor: %zu bytes\n", rd.size());

        relay::UhidDevice::Config virt_cfg;
        virt_cfg.name              = "DualSense Wireless Controller";
        virt_cfg.vendor            = DS5_VID;
        virt_cfg.product           = DS5_PID;
        virt_cfg.bus               = edge_info->bus_type;
        virt_cfg.report_descriptor = rd;

        // Callback захватывает this — phys_raw_fd_/hw/fw обновляются при реконнекте.
        virt_cfg.on_get_report = [this](uint8_t report_id) -> std::vector<uint8_t> {
            if (report_id == DS_REPORT_FIRMWARE)
                return make_firmware_report(phys_hw_ver_, phys_fw_ver_);
            auto data = relay::get_feature_report(phys_raw_fd_, report_id);
            if (report_id == DS_REPORT_PAIRING && !data.empty())
                return patch_pairing_mac(std::move(data));
            return data;
        };

        try {
            virt_dev_ = std::make_unique<relay::UhidDevice>(std::move(virt_cfg));
        } catch (const std::exception& e) {
            emit error(QString("Failed to create virtual device: %1").arg(e.what()));
            return;
        }
        fprintf(stderr, "[DEBUG] Virtual device created\n");

        // Выставляем flat=4 на стики виртуального DS5 в фоне.
        std::thread([](){ fix_virtual_ds5_deadzone(DS5_VID, DS5_PID); }).detach();
    } else {
        fprintf(stderr, "[DEBUG] Reusing existing virtual device (reconnect)\n");
    }

    // ── Захватываем evdev-ноды Edge ДО emit started_ok ───────────────────────
    const int grabbed = evdev_grabber_.grab(EDGE_VID, EDGE_PID);
    fprintf(stderr, "[DEBUG] Evdev nodes grabbed: %d\n", grabbed);

    emit started_ok(QString::fromStdString(edge_info->path));

    if (grabbed > 0)
        emit log_message(QString("Physical gamepad hidden from games (%1 evdev node(s) grabbed)")
                         .arg(grabbed));
    else
        emit log_message("Warning: could not grab evdev nodes — "
                         "games may still see the physical gamepad");

    // ── 8. Relay loop ─────────────────────────────────────────────────────────
    constexpr size_t BUF = 1024;
    uint8_t buf[BUF];
    uint64_t input_count = 0, output_count = 0;

    while (running_) {
        // Поллим оба fd одновременно — реагируем мгновенно на любой из них
        pollfd pfds[2];
        pfds[0] = {raw_fd,         POLLIN, 0};
        pfds[1] = {virt_dev_->fd(), POLLIN, 0};

        const int ret = ::poll(pfds, 2, macro_engine_.poll_timeout_ms());
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
                // ── LFN quick-record toggle (только когда функция активна) ──
                if (quick_macros_enabled_.load(std::memory_order_relaxed) &&
                    static_cast<size_t>(n) > 10 && buf[0] == 0x01)
                {
                    const bool lfn_now = (buf[10] & ds5::BTN_LFN.mask) != 0;
                    if (lfn_now && !lfn_prev_) {
                        if (!macro_engine_.is_recording()) {
                            macro_engine_.start_recording_from_ui();
                            emit log_message("⏺ Recording macro (press LFN again to stop)...");
                        } else {
                            auto result = macro_engine_.stop_recording_from_ui();
                            if (result && !result->events.empty()) {
                                result->trigger = macro::Button::RFN;
                                result->name    = "LFN Quick Macro";
                                auto macros = macro_engine_.macros_snapshot();
                                macros.erase(
                                    std::remove_if(macros.begin(), macros.end(),
                                        [](const macro::Macro& m){ return m.name == "LFN Quick Macro"; }),
                                    macros.end());
                                macros.push_back(std::move(*result));
                                macro_engine_.update_macros_from_ui(std::move(macros));
                                emit log_message("✓ Quick macro saved, trigger: RFN");
                                emit quick_macro_recorded();
                            } else {
                                emit log_message("No events recorded.");
                            }
                        }
                    }
                    lfn_prev_ = lfn_now;
                    buf[10] &= static_cast<uint8_t>(~ds5::BTN_LFN.mask); // clear LFN
                }

                // ── Apply bindings, skipping LFN/RFN when quick-macros active ─
                // LFN/RFN are hijacked by quick-record; bindings that use them
                // as source or target would interfere, so we filter them out.
                if (quick_macros_enabled_.load(std::memory_order_relaxed)) {
                    auto is_lfn_rfn = [](const ds5::ButtonBit& b) {
                        return b.byte_offset == 10 && (b.mask & 0x30) != 0;
                    };
                    std::vector<ds5::ButtonBinding> filtered;
                    filtered.reserve(bindings_.size());
                    for (const auto& bd : bindings_) {
                        if (is_lfn_rfn(bd.source)) continue;
                        bool target_uses = false;
                        for (const auto& t : bd.targets)
                            if (is_lfn_rfn(t)) { target_uses = true; break; }
                        if (!target_uses)
                            filtered.push_back(bd);
                    }
                    ds5::apply_bindings(buf, static_cast<size_t>(n), filtered);
                } else {
                    ds5::apply_bindings(buf, static_cast<size_t>(n), bindings_);
                }

                // Макросы: запись / проверка триггеров / overlay playback
                {
                    const bool was_playing = macro_engine_.is_playing();
                    macro_engine_.process_report(buf, static_cast<size_t>(n));
                    const bool now_playing = macro_engine_.is_playing();
                    if (was_playing != now_playing)
                        emit macro_playback_state(now_playing);
                }

                // Clear Edge-exclusive bits (byte 10, upper nibble) before forwarding
                // to virtual DualSense — it has no LFN/RFN/LB/RB in its HID profile.
                if (static_cast<size_t>(n) > 10 && buf[0] == 0x01)
                    buf[10] &= 0x0F;
                virt_dev_->send_input_report(buf, static_cast<size_t>(n));
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
            auto output = virt_dev_->process_event();
            if (output && !output->empty()) {
                (void)::write(raw_fd, output->data(), output->size());
                ++output_count;
            }
        }
    }

    fprintf(stderr, "[DEBUG] Relay loop exited. input=%llu output=%llu\n",
            (unsigned long long)input_count, (unsigned long long)output_count);

    // Закрываем физический fd (relay::Fd RAII сделает это при выходе из run(),
    // но обнуляем член заранее чтобы callback не использовал устаревший fd).
    phys_raw_fd_ = -1;

    // ── Освобождаем evdev-ноды ────────────────────────────────────────────────
    evdev_grabber_.release();

    // ── Управляем жизнью виртуального девайса ────────────────────────────────
    if (!running_) {
        // Пользователь явно нажал Stop — уничтожаем виртуальный геймпад.
        // Следующий старт создаст его заново с нуля.
        virt_dev_.reset();
        fprintf(stderr, "[DEBUG] Virtual device destroyed (user stop)\n");
    } else {
        // Физический геймпад отключился сам — оставляем virt_dev_ живым.
        // Игра продолжает видеть виртуальный контроллер; при реконнекте
        // relay возобновится без пересоздания устройства.
        fprintf(stderr, "[DEBUG] Virtual device kept alive (physical disconnect, reconnect pending)\n");
    }

    emit device_disconnected();
}
