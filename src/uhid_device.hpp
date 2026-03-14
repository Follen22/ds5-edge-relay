#pragma once

#include <atomic>
#include <cstring>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <cerrno>
#include <fcntl.h>
#include <linux/uhid.h>
#include <poll.h>
#include <unistd.h>

namespace relay {

    using FeatureReportCallback =
    std::function<std::vector<uint8_t>(uint8_t report_id)>;

    class UhidDevice {
    public:
        struct Config {
            std::string          name    = "DualSense Wireless Controller";
            uint16_t             vendor  = 0x054C;
            uint16_t             product = 0x0CE6;
            uint16_t             version = 0x0100;
            uint16_t             country = 0x0000;
            uint32_t             bus     = BUS_USB;
            std::vector<uint8_t> report_descriptor;
            FeatureReportCallback on_get_report;
        };

        explicit UhidDevice(Config cfg) : cfg_(std::move(cfg)) {
            const int fd = ::open("/dev/uhid", O_RDWR | O_CLOEXEC);
            if (fd < 0) {
                throw std::runtime_error(
                    std::string("Cannot open /dev/uhid: ") + strerror(errno) +
                    "\n  → Run as root or add user to 'input' group + udev rule");
            }
            fd_ = fd;

            send_create();
            // Ждём готовности И обрабатываем GET_REPORT от hid-playstation
            // во время его probe — иначе драйвер не получит firmware info и убьёт устройство
            wait_for_ready_and_serve(5000);
        }

        ~UhidDevice() {
            if (fd_ >= 0) {
                uhid_event ev{};
                ev.type = UHID_DESTROY;
                (void)::write(fd_, &ev, sizeof(ev));
                ::close(fd_);
            }
        }

        UhidDevice(UhidDevice&&)                 = delete;
        UhidDevice& operator=(UhidDevice&&)      = delete;
        UhidDevice(const UhidDevice&)            = delete;
        UhidDevice& operator=(const UhidDevice&) = delete;

        void send_input_report(const uint8_t* data, size_t len) {
            if (len > sizeof(uhid_input2_req::data)) {
                throw std::invalid_argument("Input report too large");
            }

            uhid_event ev{};
            ev.type          = UHID_INPUT2;
            ev.u.input2.size = static_cast<uint16_t>(len);
            std::memcpy(ev.u.input2.data, data, len);

            if (::write(fd_, &ev, sizeof(ev)) < 0) {
                fprintf(stderr, "[WARN] UHID_INPUT2 write failed: %s\n", strerror(errno));
            }
        }

        /// Обрабатывает одно событие UHID.
        /// Возвращает output report если пришёл, иначе nullopt.
        std::optional<std::vector<uint8_t>> process_event() {
            uhid_event ev{};
            const ssize_t n = ::read(fd_, &ev, sizeof(ev));
            if (n < 0) {
                if (errno != EAGAIN) {
                    fprintf(stderr, "[WARN] UHID read error: %s\n", strerror(errno));
                }
                return std::nullopt;
            }

            switch (ev.type) {
                case UHID_START:
                    printf("[INFO] UHID device started (flags=0x%04llx)\n",
                           (unsigned long long)ev.u.start.dev_flags);
                    break;
                case UHID_STOP:
                    printf("[INFO] UHID device stopped\n");
                    break;
                case UHID_OPEN:
                    printf("[INFO] App opened virtual device\n");
                    break;
                case UHID_CLOSE:
                    printf("[INFO] App closed virtual device\n");
                    break;
                case UHID_OUTPUT:
                    return std::vector<uint8_t>(
                        ev.u.output.data,
                        ev.u.output.data + ev.u.output.size);
                case UHID_GET_REPORT:
                    handle_get_report(ev);
                    break;
                case UHID_SET_REPORT:
                    handle_set_report(ev);
                    break;
                default:
                    break;
            }
            return std::nullopt;
        }

        [[nodiscard]] int fd() const noexcept { return fd_; }

    private:
        Config cfg_;
        int    fd_ = -1;

        void send_create() {
            if (cfg_.report_descriptor.size() > HID_MAX_DESCRIPTOR_SIZE) {
                throw std::runtime_error("Report descriptor too large");
            }

            uhid_event ev{};
            ev.type = UHID_CREATE2;

            auto& c = ev.u.create2;
            std::strncpy(reinterpret_cast<char*>(c.name), cfg_.name.c_str(),
                         sizeof(c.name) - 1);

            c.rd_size = static_cast<uint16_t>(cfg_.report_descriptor.size());
            std::memcpy(c.rd_data, cfg_.report_descriptor.data(),
                        cfg_.report_descriptor.size());

            c.bus     = static_cast<uint16_t>(cfg_.bus);
            c.vendor  = cfg_.vendor;
            c.product = cfg_.product;
            c.version = cfg_.version;
            c.country = cfg_.country;

            if (::write(fd_, &ev, sizeof(ev)) < 0) {
                throw std::runtime_error(
                    std::string("UHID_CREATE2 failed: ") + strerror(errno));
            }
        }

        /// Ждёт UHID_START и одновременно обслуживает GET_REPORT запросы.
        /// Критично: hid-playstation посылает GET_REPORT 0x20 (firmware info)
        /// во время probe, ещё до старта relay loop. Если не ответить — probe fail.
        void wait_for_ready_and_serve(int timeout_ms = 5000) {
            int elapsed = 0;
            bool started = false;

            printf("[INFO] Waiting for UHID_START (serving GET_REPORT during probe)...\n");

            while (elapsed < timeout_ms) {
                pollfd pfd{fd_, POLLIN, 0};
                const int ret = ::poll(&pfd, 1, 50); // 50ms тики — быстро реагируем
                elapsed += 50;

                if (ret <= 0) continue;
                if (!(pfd.revents & POLLIN)) continue;

                uhid_event ev{};
                const ssize_t n = ::read(fd_, &ev, sizeof(ev));
                if (n < 0) continue;

                switch (ev.type) {
                    case UHID_START:
                        printf("[INFO] UHID_START received\n");
                        started = true;
                        break;

                    case UHID_OPEN:
                        printf("[INFO] Driver opened virtual device during probe\n");
                        break;

                    case UHID_CLOSE:
                        printf("[INFO] Driver closed virtual device after probe\n");
                        break;

                    case UHID_GET_REPORT:
                        // Отвечаем на запрос firmware info ДО старта основного цикла
                        printf("[INFO] [probe] GET_REPORT 0x%02X — responding...\n",
                               ev.u.get_report.rnum);
                        handle_get_report(ev);
                        break;

                    case UHID_SET_REPORT:
                        handle_set_report(ev);
                        break;

                    default:
                        break;
                }

                if (started) {
                    // Дочитываем оставшиеся события (могут быть ещё GET_REPORT)
                    int drain_ms = 300;
                    while (drain_ms > 0) {
                        pollfd dpfd{fd_, POLLIN, 0};
                        if (::poll(&dpfd, 1, 20) <= 0) {
                            drain_ms -= 20;
                            continue;
                        }
                        uhid_event dev{};
                        if (::read(fd_, &dev, sizeof(dev)) < 0) break;

                        if (dev.type == UHID_GET_REPORT) {
                            printf("[INFO] [post-start] GET_REPORT 0x%02X\n",
                                   dev.u.get_report.rnum);
                            handle_get_report(dev);
                        } else if (dev.type == UHID_SET_REPORT) {
                            handle_set_report(dev);
                        } else if (dev.type == UHID_OPEN) {
                            printf("[INFO] App opened virtual device\n");
                            drain_ms = 0; // хватит дренировать — игра уже подключилась
                        }
                    }
                    printf("[INFO] Virtual device ready\n");
                    return;
                }
            }

            fprintf(stderr,
                    "[WARN] UHID_START not received in %d ms — continuing anyway\n",
                    timeout_ms);
        }

        void handle_get_report(const uhid_event& req) {
            std::vector<uint8_t> data;
            if (cfg_.on_get_report) {
                data = cfg_.on_get_report(req.u.get_report.rnum);
            }

            uhid_event reply{};
            reply.type                   = UHID_GET_REPORT_REPLY;
            reply.u.get_report_reply.id  = req.u.get_report.id;
            reply.u.get_report_reply.err = data.empty() ? EIO : 0;

            if (!data.empty()) {
                const size_t copy_size =
                std::min(data.size(), sizeof(reply.u.get_report_reply.data));
                reply.u.get_report_reply.size = static_cast<uint16_t>(copy_size);
                std::memcpy(reply.u.get_report_reply.data, data.data(), copy_size);
            }

            if (::write(fd_, &reply, sizeof(reply)) < 0) {
                fprintf(stderr, "[WARN] GET_REPORT reply failed: %s\n", strerror(errno));
            }
        }

        void handle_set_report(const uhid_event& req) {
            uhid_event reply{};
            reply.type                   = UHID_SET_REPORT_REPLY;
            reply.u.set_report_reply.id  = req.u.set_report.id;
            reply.u.set_report_reply.err = 0;
            (void)::write(fd_, &reply, sizeof(reply));
        }
    };

} // namespace relay
