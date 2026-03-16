#pragma once

#include "hidraw_utils.hpp"
#include "uhid_device.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>
#include <vector>

#include <errno.h>
#include <poll.h>
#include <string.h>
#include <unistd.h>

namespace relay {

    /// Основной цикл реле:
    ///   физический Edge → input → виртуальный DS5 → игра
    ///   игра → output (haptic/LED/triggers) → виртуальный DS5 → физический Edge
    class Relay {
    public:
        Relay(Fd physical_fd, std::unique_ptr<UhidDevice> virtual_dev,
              std::vector<Fd> grabbed_evdevs)
        : phys_fd_(std::move(physical_fd))
        , virt_dev_(std::move(virtual_dev))
        , grabbed_evdevs_(std::move(grabbed_evdevs))
        {}

        void run(std::atomic<bool>& running) {
            constexpr size_t BUF = 1024; // DualSense max report ~ 78 bytes USB / 547 BT
            uint8_t buf[BUF];

            uint64_t input_count  = 0;
            uint64_t output_count = 0;

            printf("[INFO] Relay loop started. Ctrl+C to stop.\n");

            while (running) {
                // Ждём события от физического устройства (input)
                // или от виртуального UHID (output от игры)
                pollfd pfds[2];
                pfds[0] = {phys_fd_.value,  POLLIN, 0};
                pfds[1] = {virt_dev_->fd(), POLLIN, 0};

                const int ret = ::poll(pfds, 2, 500 /* ms */);
                if (ret < 0) {
                    if (errno == EINTR) continue;
                    perror("[ERROR] poll");
                    break;
                }

                // ─── Физический Edge → Виртуальный DS5 ───────────────────────
                if (pfds[0].revents & POLLIN) {
                    const ssize_t n = ::read(phys_fd_.value, buf, BUF);
                    if (n > 0) {
                        virt_dev_->send_input_report(buf, static_cast<size_t>(n));
                        ++input_count;

                        // Периодическая статистика
                        if (input_count % 1000 == 0)
                            printf("[STAT] input=%llu output=%llu\n",
                                   (unsigned long long)input_count,
                                   (unsigned long long)output_count);
                    } else if (n == 0 || (n < 0 && errno != EAGAIN)) {
                        fprintf(stderr,
                                "[ERROR] Physical device read error "
                                "(disconnected?): %s\n",
                                strerror(errno));
                        break;
                    }
                }

                if (pfds[0].revents & (POLLHUP | POLLERR)) {
                    fprintf(stderr, "[ERROR] Physical device disconnected\n");
                    break;
                }

                // ─── Виртуальный DS5 (output от игры) → Физический Edge ──────
                if (pfds[1].revents & POLLIN) {
                    auto output = virt_dev_->process_event();
                    if (output && !output->empty()) {
                        // Игра отправила output report (haptic, LED, trigger resistance).
                        // Форвардим напрямую в физический Edge — форматы идентичны DS5/Edge.
                        if (::write(phys_fd_.value, output->data(),
                            output->size()) < 0) {
                            fprintf(stderr,
                                    "[WARN] Write to physical Edge failed: %s\n",
                                    strerror(errno));
                            }
                            ++output_count;
                    }
                }
            }

            printf("[INFO] Relay stopped. Total: input=%llu output=%llu\n",
                   (unsigned long long)input_count,
                   (unsigned long long)output_count);
        }

    private:
        Fd                    phys_fd_;
        std::unique_ptr<UhidDevice> virt_dev_;
        std::vector<Fd>       grabbed_evdevs_; // держим захват пока живёт Relay
    };

} // namespace relay
