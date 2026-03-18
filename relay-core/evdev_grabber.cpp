#include "evdev_grabber.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>

namespace fs = std::filesystem;

// ─── Crash-safe emergency hidraw restore ────────────────────────────────────
// If the process is killed or crashes while hidraw is chmod 0000,
// the physical controller would stay inaccessible until reboot / udev re-trigger.
// We keep a signal-safe copy of the path+mode and restore it from
// signal handlers (SIGSEGV, SIGABRT, SIGTERM, SIGINT, SIGHUP) and atexit.
// chmod(2) is async-signal-safe per POSIX, so calling it from a handler is legal.

static std::atomic<bool> g_hidraw_locked{false};
static char              g_hidraw_path[4096]{};
static mode_t            g_hidraw_orig_mode = 0;

static void emergency_restore_hidraw(int sig) {
    if (g_hidraw_locked.exchange(false, std::memory_order_relaxed)) {
        ::chmod(g_hidraw_path, g_hidraw_orig_mode);
    }
    if (sig > 0) {
        ::signal(sig, SIG_DFL);
        ::raise(sig);
    }
}

static void install_emergency_handlers() {
    static std::once_flag flag;
    std::call_once(flag, []() {
        std::atexit([]() { emergency_restore_hidraw(0); });
        ::signal(SIGTERM, emergency_restore_hidraw);
        ::signal(SIGINT,  emergency_restore_hidraw);
        ::signal(SIGHUP,  emergency_restore_hidraw);
        ::signal(SIGSEGV, emergency_restore_hidraw);
        ::signal(SIGABRT, emergency_restore_hidraw);
    });
}

// ─── helpers ────────────────────────────────────────────────────────────────

static std::optional<uint16_t> read_sysfs_hex(const fs::path& path) {
    std::ifstream f(path);
    if (!f) return std::nullopt;
    unsigned val = 0;
    f >> std::hex >> val;
    return f ? std::optional<uint16_t>(static_cast<uint16_t>(val))
             : std::nullopt;
}

// ─── find_evdev_nodes ───────────────────────────────────────────────────────
// Сканируем /sys/class/input/event* и сопоставляем vendor/product
// с device/id/{vendor,product} в sysfs.

std::vector<std::string>
EvdevGrabber::find_evdev_nodes(uint16_t vendor, uint16_t product) {
    std::vector<std::string> result;
    constexpr auto input_class = "/sys/class/input";

    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(input_class, ec)) {
        const auto name = entry.path().filename().string();
        if (name.compare(0, 5, "event") != 0)
            continue;

        const auto id_dir = entry.path() / "device" / "id";
        auto vid = read_sysfs_hex(id_dir / "vendor");
        auto pid = read_sysfs_hex(id_dir / "product");

        if (vid && pid && *vid == vendor && *pid == product)
            result.push_back("/dev/input/" + name);
    }
    return result;
}

// ─── grab ───────────────────────────────────────────────────────────────────

int EvdevGrabber::grab(uint16_t vendor, uint16_t product) {
    release_fds_(); // только evdev fds — hidraw-блокировка остаётся активной

    const auto nodes = find_evdev_nodes(vendor, product);
    if (nodes.empty()) {
        std::fprintf(stderr,
                     "[EvdevGrabber] No evdev nodes found for %04X:%04X\n",
                     vendor, product);
        return 0;
    }

    for (const auto& path : nodes) {
        // O_RDONLY достаточно для EVIOCGRAB, O_NONBLOCK чтобы не блокироваться
        int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            std::fprintf(stderr,
                         "[EvdevGrabber] Cannot open %s: %s\n",
                         path.c_str(), std::strerror(errno));
            continue;
        }

        if (::ioctl(fd, EVIOCGRAB, 1) < 0) {
            std::fprintf(stderr,
                         "[EvdevGrabber] EVIOCGRAB failed for %s: %s\n",
                         path.c_str(), std::strerror(errno));
            ::close(fd);
            continue;
        }

        char dev_name[256] = {};
        ::ioctl(fd, EVIOCGNAME(sizeof(dev_name)), dev_name);
        std::fprintf(stderr,
                     "[EvdevGrabber] Grabbed %s (%s)\n",
                     path.c_str(), dev_name);

        fds_.push_back({fd, path});
    }

    return static_cast<int>(fds_.size());
}

// ─── release_fds_ ───────────────────────────────────────────────────────────
// Освобождает только evdev-захваты, не трогает hidraw-блокировку.
// Используется внутри grab() чтобы не снимать hidraw-блокировку преждевременно.

void EvdevGrabber::release_fds_() {
    for (auto& e : fds_) {
        if (e.fd >= 0) {
            ::ioctl(e.fd, EVIOCGRAB, 0);
            ::close(e.fd);
            std::fprintf(stderr,
                         "[EvdevGrabber] Released %s\n", e.path.c_str());
        }
    }
    fds_.clear();
}

// ─── release ────────────────────────────────────────────────────────────────

void EvdevGrabber::release() {
    unlock_hidraw();
    release_fds_();
}

// ─── lock_hidraw ─────────────────────────────────────────────────────────────
// Идея: relay уже держит открытый raw_fd до вызова этой функции.
// Меняем права на 0000 — новые открытия будут получать EACCES.
// Существующий fd relay'а продолжает работать в обычном режиме.

int EvdevGrabber::lock_hidraw(const std::string& path) {
    struct stat st{};
    if (::stat(path.c_str(), &st) < 0) {
        std::fprintf(stderr,
                     "[EvdevGrabber] stat(%s) failed: %s\n",
                     path.c_str(), std::strerror(errno));
        return -1;
    }

    hidraw_orig_mode_ = st.st_mode;
    hidraw_path_      = path;

    if (::chmod(path.c_str(), 0000) < 0) {
        std::fprintf(stderr,
                     "[EvdevGrabber] chmod(0000) failed for %s: %s\n",
                     path.c_str(), std::strerror(errno));
        hidraw_path_.clear();
        return -1;
    }

    hidraw_locked_ = true;

    // Update global emergency-restore state and install signal handlers once
    std::strncpy(g_hidraw_path, path.c_str(), sizeof(g_hidraw_path) - 1);
    g_hidraw_orig_mode = hidraw_orig_mode_;
    g_hidraw_locked.store(true, std::memory_order_relaxed);
    install_emergency_handlers();

    std::fprintf(stderr,
                 "[EvdevGrabber] hidraw locked: %s (orig mode 0%o)\n",
                 path.c_str(), hidraw_orig_mode_ & 0777);
    return 0;
}

// ─── unlock_hidraw ───────────────────────────────────────────────────────────

void EvdevGrabber::unlock_hidraw() {
    if (!hidraw_locked_) return;

    // Clear global emergency state first so signal handlers don't double-restore
    g_hidraw_locked.store(false, std::memory_order_relaxed);

    if (::chmod(hidraw_path_.c_str(), hidraw_orig_mode_) < 0) {
        std::fprintf(stderr,
                     "[EvdevGrabber] chmod restore failed for %s: %s\n",
                     hidraw_path_.c_str(), std::strerror(errno));
    } else {
        std::fprintf(stderr,
                     "[EvdevGrabber] hidraw unlocked: %s (mode restored 0%o)\n",
                     hidraw_path_.c_str(), hidraw_orig_mode_ & 0777);
    }

    hidraw_locked_ = false;
    hidraw_path_.clear();
}

// ─── special members ────────────────────────────────────────────────────────

EvdevGrabber::~EvdevGrabber() { release(); }

EvdevGrabber::EvdevGrabber(EvdevGrabber&& o) noexcept
    : fds_(std::move(o.fds_))
    , hidraw_path_(std::move(o.hidraw_path_))
    , hidraw_orig_mode_(o.hidraw_orig_mode_)
    , hidraw_locked_(o.hidraw_locked_)
{
    o.hidraw_locked_ = false;
}

EvdevGrabber& EvdevGrabber::operator=(EvdevGrabber&& o) noexcept {
    if (this != &o) {
        release();
        fds_              = std::move(o.fds_);
        hidraw_path_      = std::move(o.hidraw_path_);
        hidraw_orig_mode_ = o.hidraw_orig_mode_;
        hidraw_locked_    = o.hidraw_locked_;
        o.hidraw_locked_  = false;
    }
    return *this;
}
