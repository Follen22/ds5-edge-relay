#include "hidraw_utils.hpp"
#include "relay.hpp"
#include "uhid_device.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>

#include <fcntl.h>
#include <unistd.h>

// ─── VID:PID ──────────────────────────────────────────────────────────────────
static constexpr uint16_t EDGE_VID = 0x054C;
static constexpr uint16_t EDGE_PID = 0x0DF2;
static constexpr uint16_t DS5_VID  = 0x054C;
static constexpr uint16_t DS5_PID  = 0x0CE6;

// ─── Feature report constants (from kernel hid-playstation.c) ─────────────────
// 0x20: firmware info, 64 bytes. hw_version @ [24..27], fw_version @ [28..31]
// 0x09: pairing info,  20 bytes. MAC address @ [1..6] (6 bytes, little-endian)
static constexpr uint8_t DS_REPORT_FIRMWARE = 0x20;
static constexpr uint8_t DS_REPORT_PAIRING  = 0x09;
static constexpr size_t  DS_FIRMWARE_SIZE   = 64;
static constexpr size_t  DS_FW_HW_OFFSET    = 24;
static constexpr size_t  DS_FW_FW_OFFSET    = 28;
static constexpr size_t  DS_MAC_OFFSET      = 1; // байты [1..6] в report 0x09

static std::atomic<bool> g_running{true};

static void signal_handler(int sig) {
    printf("\n[INFO] Signal %d received, shutting down...\n", sig);
    g_running = false;
}

static std::pair<uint32_t, uint32_t>
read_versions_from_sysfs(const std::string& hidraw_path) {
    namespace fs = std::filesystem;
    const std::string hidraw_name = fs::path(hidraw_path).filename().string();
    const std::string base = "/sys/class/hidraw/" + hidraw_name + "/device/";

    auto read_hex = [](const std::string& path) -> uint32_t {
        FILE* f = fopen(path.c_str(), "r");
        if (!f) return 0;
        uint32_t val = 0;
        fscanf(f, "0x%x", &val);
        fclose(f);
        return val;
    };

    const uint32_t hw = read_hex(base + "hardware_version");
    const uint32_t fw = read_hex(base + "firmware_version");
    if (hw || fw)
        printf("[INFO] sysfs versions: hw=0x%08X fw=0x%08X\n", hw, fw);
    return {hw, fw};
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

/// Модифицирует MAC в pairing report чтобы избежать EEXIST (-17).
/// hid-playstation проверяет уникальность MAC при регистрации устройства.
/// Меняем последний байт MAC: виртуальное устройство будет иметь
/// адрес XX:XX:XX:XX:XX:(original+1) — уникально, но отличается минимально.
static std::vector<uint8_t>
patch_pairing_report_mac(std::vector<uint8_t> report) {
    // report[0] = 0x09 (report ID)
    // report[1..6] = MAC address (6 bytes)
    if (report.size() < DS_MAC_OFFSET + 6) {
        printf("[WARN] Pairing report too short (%zu bytes), cannot patch MAC\n",
               report.size());
        return report;
    }

    // Логируем оригинальный MAC
    printf("[INFO] Original MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
           report[1], report[2], report[3],
           report[4], report[5], report[6]);

    // Инвертируем бит 1 первого байта MAC (locally administered bit).
    // Это стандартная практика для виртуальных MAC — не конфликтует с реальными.
    report[DS_MAC_OFFSET] ^= 0x02;

    printf("[INFO] Virtual  MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
           report[1], report[2], report[3],
           report[4], report[5], report[6]);

    return report;
}

static void print_usage(const char* argv0) {
    fprintf(stderr,
            "Usage: %s [--wait]\n"
            "  --wait   Wait for DualSense Edge to be connected\n\n"
            "Requires: /dev/uhid access, input group or root\n",
            argv0);
}

int main(int argc, char* argv[]) {
    bool wait_for_device = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);
        if      (arg == "--wait") { wait_for_device = true; }
        else if (arg == "--help") { print_usage(argv[0]); return 0; }
    }

    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);

    printf("╔══════════════════════════════════════╗\n");
    printf("║   DualSense Edge Relay Daemon v1.0   ║\n");
    printf("╚══════════════════════════════════════╝\n\n");

    // ── 1. Найти физический Edge ─────────────────────────────────────────────
    std::optional<relay::HidDeviceInfo> edge_info;
    while (g_running) {
        edge_info = relay::find_hidraw_device(EDGE_VID, EDGE_PID);
        if (edge_info) break;
        if (!wait_for_device) {
            fprintf(stderr, "[ERROR] DualSense Edge (054C:0DF2) not found.\n"
                            "        Use --wait to wait for connection.\n");
            return EXIT_FAILURE;
        }
        printf("[WAIT] Waiting for DualSense Edge...\n");
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    if (!edge_info) return EXIT_SUCCESS;

    printf("[INFO] Found: %s (bus=%s)\n",
           edge_info->path.c_str(),
           edge_info->bus_type == BUS_USB ? "USB" : "Bluetooth");

    // ── 2. Версии прошивки из sysfs ──────────────────────────────────────────
    auto [hw_ver, fw_ver] = read_versions_from_sysfs(edge_info->path);
    if (hw_ver == 0) hw_ver = 0x01000216; // fallback из dmesg
    if (fw_ver == 0) fw_ver = 0x0100008b;

    // ── 3. Открыть физическое устройство ────────────────────────────────────
    const int raw_fd = ::open(edge_info->path.c_str(),
                               O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (raw_fd < 0) {
        fprintf(stderr, "[ERROR] Cannot open %s: %s\n",
                edge_info->path.c_str(), strerror(errno));
        return EXIT_FAILURE;
    }
    relay::Fd phys_fd(raw_fd);
    printf("[INFO] Opened physical device fd=%d\n", raw_fd);

    // ── 4. Report Descriptor ─────────────────────────────────────────────────
    std::vector<uint8_t> rd;
    try {
        rd = relay::read_report_descriptor(raw_fd);
        printf("[INFO] Report descriptor: %zu bytes\n", rd.size());
    } catch (const std::exception& e) {
        fprintf(stderr, "[ERROR] %s\n", e.what());
        return EXIT_FAILURE;
    }

    // ── 5. Захват evdev ──────────────────────────────────────────────────────
    auto grabbed = relay::grab_evdev_nodes(edge_info->path);
    printf("[INFO] Grabbed %zu evdev node(s)\n", grabbed.size());

    // ── 6. Виртуальный DualSense ─────────────────────────────────────────────
    relay::UhidDevice::Config virt_cfg;
    virt_cfg.name              = "DualSense Wireless Controller";
    virt_cfg.vendor            = DS5_VID;
    virt_cfg.product           = DS5_PID;
    virt_cfg.bus               = edge_info->bus_type;
    virt_cfg.report_descriptor = rd;

    virt_cfg.on_get_report = [raw_fd, hw_ver, fw_ver]
                              (uint8_t report_id) -> std::vector<uint8_t> {
        switch (report_id) {

        case DS_REPORT_FIRMWARE:
            // Синтетический ответ: hid-playstation не может получить его
            // через HIDIOCGFEATURE (драйвер перехватывает на kernel level)
            printf("[INFO] GET_REPORT 0x20 → synthetic firmware report\n");
            return make_firmware_report(hw_ver, fw_ver);

        case DS_REPORT_PAIRING: {
            // Проксируем, но патчим MAC чтобы избежать EEXIST (-17)
            auto data = relay::get_feature_report(raw_fd, report_id);
            if (!data.empty()) {
                return patch_pairing_report_mac(std::move(data));
            }
            // Если физическое устройство не ответило — возвращаем пустой
            printf("[WARN] GET_REPORT 0x09 → no data from physical device\n");
            return {};
        }

        default:
            // Все остальные reports проксируем напрямую
            auto data = relay::get_feature_report(raw_fd, report_id);
            if (!data.empty()) {
                printf("[INFO] GET_REPORT 0x%02X → proxied (%zu bytes)\n",
                       report_id, data.size());
            } else {
                printf("[WARN] GET_REPORT 0x%02X → empty\n", report_id);
            }
            return data;
        }
    };

    std::unique_ptr<relay::UhidDevice> virt_dev;
    try {
        virt_dev = std::make_unique<relay::UhidDevice>(std::move(virt_cfg));
        printf("[INFO] Virtual DualSense created (054C:0CE6)\n");
    } catch (const std::exception& e) {
        fprintf(stderr, "[ERROR] %s\n", e.what());
        return EXIT_FAILURE;
    }

    // ── 7. Реле ──────────────────────────────────────────────────────────────
    relay::Relay rel(std::move(phys_fd), std::move(virt_dev), std::move(grabbed));
    rel.run(g_running);

    return EXIT_SUCCESS;
}
