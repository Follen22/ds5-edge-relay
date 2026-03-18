#include "macro_engine.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cmath>
#include <cstdio>

namespace macro {

MacroEngine::MacroEngine() = default;

// ─── UI thread → relay thread ───────────────────────────────────────────────

void MacroEngine::start_recording_from_ui() {
    std::lock_guard lk(mu_);
    recording_  = true;
    rec_events_.clear();
    rec_prev_   = {};          // обнулённый снэпшот — первый diff покажет все нажатые
    rec_last_time_ = Clock::now();
    rec_lstick_x_ = rec_lstick_y_ = 0;
    rec_rstick_x_ = rec_rstick_y_ = 0;
    std::fprintf(stderr, "[Macro] Recording started\n");
}

std::optional<Macro> MacroEngine::stop_recording_from_ui() {
    std::lock_guard lk(mu_);
    if (!recording_)
        return std::nullopt;

    recording_ = false;
    std::fprintf(stderr, "[Macro] Recording stopped, %zu events\n",
                 rec_events_.size());

    if (rec_events_.empty())
        return std::nullopt;

    Macro m;
    m.name    = "New Macro";
    m.trigger = Button::COUNT_; // пользователь назначит позже
    m.enabled = true;
    m.events  = std::move(rec_events_);

    // Первое событие всегда с delay=0
    if (!m.events.empty())
        m.events.front().delay_ms = 0;

    return m;
}

void MacroEngine::update_macros_from_ui(std::vector<Macro> macros) {
    std::lock_guard lk(mu_);
    pending_macros_ = std::move(macros);
    macros_dirty_   = true;
}

// ─── Relay thread ───────────────────────────────────────────────────────────

void MacroEngine::process_report(uint8_t* buf, size_t len) {
    // Подхватываем обновления из UI
    {
        std::lock_guard lk(mu_);
        apply_pending_macros_();
    }

    const auto cur = ButtonSnapshot::from_report(buf, len);

    // 1. Запись
    if (recording_) {
        std::lock_guard lk(mu_);
        record_diff_(cur, buf, len);
    }

    // 2. Проверяем триггеры (только если не записываем)
    if (!recording_ && !macros_.empty()) {
        check_triggers_(cur);
    }

    // 3. Воспроизведение — накладываем overlay на буфер
    if (playing_) {
        advance_playback_();
        play_overlay_.apply_to_report(buf, len, play_forced_);
        // Axis overlay: перезаписываем позиции стиков в репорте
        if (len >= 3 && buf[0] == 0x01) {
            if (play_force_lstick_) {
                buf[1] = static_cast<uint8_t>(play_lstick_x_ + 128);
                buf[2] = static_cast<uint8_t>(play_lstick_y_ + 128);
            }
            if (play_force_rstick_ && len >= 5) {
                buf[3] = static_cast<uint8_t>(play_rstick_x_ + 128);
                buf[4] = static_cast<uint8_t>(play_rstick_y_ + 128);
            }
        }
    }

    prev_snap_ = cur;
}

int MacroEngine::poll_timeout_ms() const {
    // Во время воспроизведения уменьшаем таймаут для лучшей точности
    return playing_ ? 1 : 500;
}

bool MacroEngine::is_recording() const {
    std::lock_guard lk(mu_);
    return recording_;
}

bool MacroEngine::is_playing() const {
    return playing_;
}

// ─── Persistence ────────────────────────────────────────────────────────────

bool MacroEngine::load(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    const auto doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
        return false;

    std::vector<Macro> loaded;
    for (const auto& val : doc.object()["macros"].toArray())
        loaded.push_back(Macro::from_json(val.toObject()));

    std::lock_guard lk(mu_);
    macros_ = std::move(loaded);
    std::fprintf(stderr, "[Macro] Loaded %zu macros from %s\n",
                 macros_.size(), path.toUtf8().constData());
    return true;
}

bool MacroEngine::save(const QString& path) const {
    // Убедимся что директория существует
    QDir().mkpath(QFileInfo(path).absolutePath());

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;

    QJsonArray arr;
    {
        std::lock_guard lk(mu_);
        for (const auto& m : macros_)
            arr.append(m.to_json());
    }

    QJsonObject root;
    root["macros"] = arr;

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

std::vector<Macro> MacroEngine::macros_snapshot() const {
    std::lock_guard lk(mu_);
    return macros_;
}

// ─── Internal ───────────────────────────────────────────────────────────────

void MacroEngine::apply_pending_macros_() {
    // Вызывается под mu_
    if (!macros_dirty_) return;
    macros_       = std::move(pending_macros_);
    macros_dirty_ = false;
    // Если шло воспроизведение — останавливаем, т.к. указатели могут стать невалидными
    if (playing_)
        stop_playback_();
}

void MacroEngine::record_diff_(const ButtonSnapshot& cur,
                               const uint8_t* buf, size_t len) {
    // Вызывается под mu_
    const auto now = Clock::now();
    const int dt = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now - rec_last_time_).count());

    bool any_change = false;

    // ── Кнопки ──────────────────────────────────────────────────────────────
    for (size_t i = 0; i < BUTTON_COUNT; ++i) {
        if (cur.state[i] != rec_prev_.state[i]) {
            MacroEvent ev;
            ev.kind    = MacroEvent::Kind::Button;
            ev.button  = static_cast<Button>(i);
            ev.pressed = cur.state[i];
            // Задержка ставится только на первое изменение в данном «кадре»,
            // все остальные синхронные изменения получают delay=0
            ev.delay_ms = any_change ? 0 : dt;
            rec_events_.push_back(ev);
            any_change = true;
        }
    }

    // ── Стики ───────────────────────────────────────────────────────────────
    // DualSense USB report: buf[1]=LX, buf[2]=LY, buf[3]=RX, buf[4]=RY (center=128)
    // Каждое изменение позиции записывается как отдельное событие — это сохраняет
    // полную траекторию (движение → возврат в центр). В UI несколько подряд идущих
    // событий одного стика отображаются как ОДНА строка "Left/Right Stick".
    if (len >= 5 && buf[0] == 0x01) {
        // Snap near-zero values to exactly 0 to filter stick drift at rest
        auto snap = [](int8_t v) -> int8_t {
            return (std::abs(v) <= 3) ? int8_t(0) : v;
        };
        const int8_t lx = snap(static_cast<int8_t>(buf[1] - 128));
        const int8_t ly = snap(static_cast<int8_t>(buf[2] - 128));
        const int8_t rx = snap(static_cast<int8_t>(buf[3] - 128));
        const int8_t ry = snap(static_cast<int8_t>(buf[4] - 128));

        auto record_stick = [&](MacroEvent::Kind sk,
                                int8_t ax, int8_t ay,
                                int8_t& prev_x, int8_t& prev_y) {
            if (ax == prev_x && ay == prev_y) return; // значение не изменилось
            prev_x = ax; prev_y = ay;

            // Всегда создаём новое событие — merge-in-place уничтожал бы
            // траекторию (напр., движение+возврат в центр давало бы (0,0)).
            MacroEvent ev;
            ev.kind     = sk;
            ev.axis_x   = ax;
            ev.axis_y   = ay;
            ev.delay_ms = any_change ? 0 : dt;
            rec_events_.push_back(ev);
            any_change  = true;
        };

        record_stick(MacroEvent::Kind::LeftStick,  lx, ly,
                     rec_lstick_x_, rec_lstick_y_);
        record_stick(MacroEvent::Kind::RightStick, rx, ry,
                     rec_rstick_x_, rec_rstick_y_);
    }

    if (any_change) {
        rec_last_time_ = now;
        rec_prev_      = cur;
    }
}

void MacroEngine::check_triggers_(const ButtonSnapshot& cur) {
    for (const auto& m : macros_) {
        if (!m.enabled || m.events.empty())
            continue;
        if (m.trigger >= Button::COUNT_)
            continue;

        const size_t ti = static_cast<size_t>(m.trigger);

        // Детектируем rising edge: кнопка нажата сейчас, не была нажата ранее
        if (cur.state[ti] && !prev_snap_.state[ti]) {
            // Если уже играем этот же макрос — перезапускаем
            if (playing_)
                stop_playback_();

            play_macro_ = &m;
            play_index_ = 0;
            playing_    = true;
            play_overlay_ = {};
            play_forced_.fill(false);

            // Если первое событие имеет delay=0, применяем сразу
            play_event_time_ = Clock::now() +
                std::chrono::milliseconds(m.events[0].delay_ms);

            std::fprintf(stderr, "[Macro] Playing '%s' (%zu events)\n",
                         m.name.toUtf8().constData(), m.events.size());
            break; // один макрос за раз
        }
    }
}

void MacroEngine::advance_playback_() {
    if (!play_macro_ || !playing_) return;

    const auto now = Clock::now();

    // Применяем все события, чьё время наступило
    while (play_index_ < play_macro_->events.size()) {
        if (now < play_event_time_)
            break; // ещё рано

        const auto& ev = play_macro_->events[play_index_];

        if (ev.kind == MacroEvent::Kind::Button) {
            const size_t bi = static_cast<size_t>(ev.button);
            play_overlay_[ev.button] = ev.pressed;
            play_forced_[bi]         = true;
        } else if (ev.kind == MacroEvent::Kind::LeftStick) {
            play_lstick_x_     = ev.axis_x;
            play_lstick_y_     = ev.axis_y;
            play_force_lstick_ = true;
        } else if (ev.kind == MacroEvent::Kind::RightStick) {
            play_rstick_x_     = ev.axis_x;
            play_rstick_y_     = ev.axis_y;
            play_force_rstick_ = true;
        }

        ++play_index_;

        // Устанавливаем время следующего события
        if (play_index_ < play_macro_->events.size()) {
            play_event_time_ = now +
                std::chrono::milliseconds(play_macro_->events[play_index_].delay_ms);
        }
    }

    // Макрос закончился
    if (play_index_ >= play_macro_->events.size()) {
        stop_playback_();
    }
}

void MacroEngine::stop_playback_() {
    playing_    = false;
    play_macro_ = nullptr;
    play_index_ = 0;
    play_overlay_ = {};
    play_forced_.fill(false);
    play_force_lstick_ = false;
    play_force_rstick_ = false;
}

} // namespace macro
