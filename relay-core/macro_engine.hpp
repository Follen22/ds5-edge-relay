#pragma once

#include "macro_types.hpp"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>

namespace macro {

/// Движок макросов — работает в relay-потоке.
///
/// Два режима:
///   Recording — ловит изменения кнопок между HID-репортами и формирует MacroEvent'ы.
///   Playback  — неблокирующе воспроизводит макрос, накладывая кнопки на исходящий буфер.
///
/// Все публичные методы *_from_ui() потокобезопасны (вызываются из GUI-потока).
/// process_report() вызывается ТОЛЬКО из relay-потока.

class MacroEngine {
public:
    using Clock     = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    MacroEngine();

    // ── UI thread → relay thread (потокобезопасные) ─────────────────────────

    /// Начать запись нового макроса.
    void start_recording_from_ui();

    /// Остановить запись. Возвращает записанный макрос (без имени и триггера).
    std::optional<Macro> stop_recording_from_ui();

    /// Обновить полный список макросов (из UI после редактирования).
    void update_macros_from_ui(std::vector<Macro> macros);

    // ── Relay thread ────────────────────────────────────────────────────────

    /// Обрабатывает каждый входящий HID-репорт.
    ///
    /// 1. Если идёт запись — фиксирует изменения кнопок.
    /// 2. Проверяет триггеры — если триггер-кнопка нажата, запускает playback.
    /// 3. Если идёт воспроизведение — накладывает кнопки на буфер.
    ///
    /// Вызывать ПОСЛЕ apply_bindings(), но ДО send_input_report().
    void process_report(uint8_t* buf, size_t len);

    /// Сколько миллисекунд ставить таймаут poll() — уменьшаем во время playback.
    [[nodiscard]] int poll_timeout_ms() const;

    /// Идёт ли запись прямо сейчас.
    [[nodiscard]] bool is_recording() const;

    /// Идёт ли воспроизведение прямо сейчас.
    [[nodiscard]] bool is_playing() const;

    // ── Persistence ─────────────────────────────────────────────────────────

    /// Загрузить макросы из JSON-файла.
    bool load(const QString& path);

    /// Сохранить макросы в JSON-файл.
    bool save(const QString& path) const;

    /// Текущий список макросов (для UI). Потокобезопасно.
    std::vector<Macro> macros_snapshot() const;

private:
    mutable std::mutex mu_;

    // ── Список макросов ─────────────────────────────────────────────────────
    std::vector<Macro> macros_;
    // Pending update from UI thread
    std::vector<Macro> pending_macros_;
    bool               macros_dirty_ = false;

    // ── Recording state ─────────────────────────────────────────────────────
    bool              recording_     = false;
    ButtonSnapshot    rec_prev_;            // предыдущий снэпшот при записи
    TimePoint         rec_last_time_;       // время предыдущего события
    std::vector<MacroEvent> rec_events_;    // накапливаемые события
    // Последние записанные позиции стиков (для группировки consecutive кадров)
    int8_t            rec_lstick_x_ = 0, rec_lstick_y_ = 0;
    int8_t            rec_rstick_x_ = 0, rec_rstick_y_ = 0;

    // ── Playback state ──────────────────────────────────────────────────────
    bool              playing_       = false;
    size_t            play_index_    = 0;       // индекс текущего события
    const Macro*      play_macro_    = nullptr;  // указатель в macros_
    TimePoint         play_event_time_;          // когда должно сработать текущее событие
    ButtonSnapshot    play_overlay_;             // что форсировать (кнопки)
    std::array<bool, BUTTON_COUNT> play_forced_{}; // какие кнопки форсируются
    // Axis overlay для стиков
    int8_t            play_lstick_x_ = 0, play_lstick_y_ = 0;
    int8_t            play_rstick_x_ = 0, play_rstick_y_ = 0;
    bool              play_force_lstick_ = false;
    bool              play_force_rstick_ = false;

    // ── Предыдущий снэпшот для отслеживания trigger-кнопок ──────────────────
    ButtonSnapshot    prev_snap_;

    // ── Внутренние методы (вызываются под mu_ или из relay thread) ──────────
    void apply_pending_macros_();
    void record_diff_(const ButtonSnapshot& cur, const uint8_t* buf, size_t len);
    void check_triggers_(const ButtonSnapshot& cur);
    void advance_playback_();
    void stop_playback_();
};

} // namespace macro
