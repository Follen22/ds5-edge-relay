#pragma once

#include <cstdint>
#include <cstring>       // strerror
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <fcntl.h>
#include <linux/hidraw.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace relay {

    /// RAII-обёртка над файловым дескриптором
    struct Fd {
        int value = -1;

        explicit Fd(int fd) noexcept : value(fd) {}
        Fd() noexcept = default;

        ~Fd() {
            if (value >= 0) ::close(value);
        }

        Fd(Fd&& o) noexcept : value(o.value) { o.value = -1; }
        Fd& operator=(Fd&& o) noexcept {
            if (this != &o) {
                if (value >= 0) ::close(value);
                value = o.value;
                o.value = -1;
            }
            return *this;
        }
        Fd(const Fd&)            = delete;
        Fd& operator=(const Fd&) = delete;

        [[nodiscard]] bool valid() const noexcept { return value >= 0; }
        explicit operator int() const noexcept { return value; }
    };

    /// Информация о HID-устройстве
    struct HidDeviceInfo {
        std::string path;
        uint16_t    vendor_id;
        uint16_t    product_id;
        uint32_t    bus_type; // BUS_USB, BUS_BLUETOOTH, etc. — храним как u32
    };

    /// Сканирует /dev/hidraw* и возвращает устройство с заданным VID:PID.
    [[nodiscard]] std::optional<HidDeviceInfo>
    find_hidraw_device(uint16_t vendor_id, uint16_t product_id) {
        namespace fs = std::filesystem;

        std::error_code ec;
        for (const auto& entry : fs::directory_iterator("/dev", ec)) {
            const auto name = entry.path().filename().string();
            if (name.rfind("hidraw", 0) != 0) continue;

            const int fd = ::open(entry.path().c_str(), O_RDONLY | O_NONBLOCK);
            if (fd < 0) continue;
            Fd guard(fd);

            hidraw_devinfo info{};
            if (::ioctl(fd, HIDIOCGRAWINFO, &info) < 0) continue;

            if (static_cast<uint16_t>(info.vendor)  == vendor_id &&
                static_cast<uint16_t>(info.product) == product_id) {
                return HidDeviceInfo{
                    .path       = entry.path().string(),
                    .vendor_id  = vendor_id,
                    .product_id = product_id,
                    .bus_type   = static_cast<uint32_t>(info.bustype), // явное приведение u32→u32
                };
                }
        }
        return std::nullopt;
    }

    /// Читает HID Report Descriptor с открытого fd
    [[nodiscard]] std::vector<uint8_t> read_report_descriptor(int fd) {
        int desc_size = 0;
        if (::ioctl(fd, HIDIOCGRDESCSIZE, &desc_size) < 0) {
            throw std::runtime_error("HIDIOCGRDESCSIZE failed: " +
            std::string(strerror(errno)));
        }

        hidraw_report_descriptor desc{};
        desc.size = static_cast<uint32_t>(desc_size);
        if (::ioctl(fd, HIDIOCGRDESC, &desc) < 0) {
            throw std::runtime_error("HIDIOCGRDESC failed: " +
            std::string(strerror(errno)));
        }

        return {desc.value, desc.value + static_cast<size_t>(desc_size)};
    }

    /// Читает feature report по ID с физического устройства
    [[nodiscard]] std::vector<uint8_t> get_feature_report(int fd, uint8_t report_id) {
        std::vector<uint8_t> buf(4096);
        buf[0] = report_id;

        const ssize_t n = ::ioctl(fd, HIDIOCGFEATURE(static_cast<int>(buf.size())),
                                  buf.data());
        if (n < 0) {
            return {}; // не все отчёты поддерживаются — не падаем
        }

        buf.resize(static_cast<size_t>(n));
        return buf;
    }

} // namespace relay
