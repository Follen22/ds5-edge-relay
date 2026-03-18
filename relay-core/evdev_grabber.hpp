#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Захватывает evdev-ноды физического геймпада через EVIOCGRAB и
// блокирует доступ к hidraw-ноде (chmod 0000), скрывая устройство
// от игр (SDL, Steam, Proton). При остановке права восстанавливаются.
class EvdevGrabber {
public:
    EvdevGrabber() = default;
    ~EvdevGrabber();

    EvdevGrabber(const EvdevGrabber&)            = delete;
    EvdevGrabber& operator=(const EvdevGrabber&) = delete;
    EvdevGrabber(EvdevGrabber&&) noexcept;
    EvdevGrabber& operator=(EvdevGrabber&&) noexcept;

    /// Найти и захватить все evdev-ноды для заданного VID:PID.
    /// Возвращает количество успешно захваченных устройств.
    int grab(uint16_t vendor, uint16_t product);

    /// Освободить все захваты и закрыть файловые дескрипторы.
    /// Также восстанавливает права на hidraw если был вызван lock_hidraw().
    void release();

    /// Заблокировать hidraw-устройство от новых открытий (chmod 0000).
    /// Вызывать ПОСЛЕ того как relay уже открыл свой raw_fd — он продолжит работать.
    /// SDL/Steam не смогут открыть устройство, пока блокировка активна.
    /// Возвращает 0 при успехе, -1 при ошибке.
    int lock_hidraw(const std::string& path);

    /// Восстановить оригинальные права на hidraw. Автоматически вызывается из release().
    void unlock_hidraw();

    [[nodiscard]] int grabbed_count() const noexcept {
        return static_cast<int>(fds_.size());
    }

private:
    struct Entry {
        int         fd   = -1;
        std::string path;
    };
    std::vector<Entry> fds_;

    // hidraw lock state
    std::string  hidraw_path_;
    mode_t       hidraw_orig_mode_ = 0;
    bool         hidraw_locked_    = false;

    void release_fds_();  // только evdev fds, без unlock_hidraw

    static std::vector<std::string> find_evdev_nodes(uint16_t vendor,
                                                     uint16_t product);
};
