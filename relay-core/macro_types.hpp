#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

namespace macro {

// ─── Button identifiers ─────────────────────────────────────────────────────
// Порядок совпадает с именами в binds.json проекта.

enum class Button : uint8_t {
    Cross, Circle, Square, Triangle,
    L1, R1, L2, R2, L3, R3,
    Options, Create, PS, Touchpad, Mute,
    DPadUp, DPadDown, DPadLeft, DPadRight,
    // Edge-exclusive
    LFN, RFN, LB, RB,
    COUNT_
};

inline constexpr size_t BUTTON_COUNT = static_cast<size_t>(Button::COUNT_);

// Имена для UI и JSON — индекс = enum value
inline constexpr std::string_view BUTTON_NAMES[] = {
    "Cross", "Circle", "Square", "Triangle",
    "L1", "R1", "L2", "R2", "L3", "R3",
    "Options", "Create", "PS", "Touchpad", "Mute",
    "DPadUp", "DPadDown", "DPadLeft", "DPadRight",
    "LFN", "RFN", "LB", "RB"
};
static_assert(std::size(BUTTON_NAMES) == BUTTON_COUNT);

inline QString button_to_string(Button b) {
    return QString::fromUtf8(BUTTON_NAMES[static_cast<size_t>(b)].data(),
                             static_cast<int>(BUTTON_NAMES[static_cast<size_t>(b)].size()));
}

inline Button button_from_string(const QString& s) {
    for (size_t i = 0; i < BUTTON_COUNT; ++i)
        if (s == QLatin1StringView(BUTTON_NAMES[i].data(),
                                   static_cast<int>(BUTTON_NAMES[i].size())))
            return static_cast<Button>(i);
    return Button::COUNT_; // invalid
}

// ─── Button snapshot — моментальное состояние всех кнопок ────────────────────

struct ButtonSnapshot {
    std::array<bool, BUTTON_COUNT> state{};

    bool operator[](Button b) const { return state[static_cast<size_t>(b)]; }
    bool& operator[](Button b)      { return state[static_cast<size_t>(b)]; }

    bool operator==(const ButtonSnapshot& o) const { return state == o.state; }
    bool operator!=(const ButtonSnapshot& o) const { return !(*this == o); }

    /// Парсит состояние кнопок из USB HID-репорта DualSense (report ID 0x01).
    /// buf[0] == 0x01, кнопки в байтах 8–10.
    static ButtonSnapshot from_report(const uint8_t* buf, size_t len) {
        ButtonSnapshot snap{};
        if (len < 11 || buf[0] != 0x01)
            return snap;

        const uint8_t b0 = buf[8];  // dpad hat (bits 0–3) + face buttons
        const uint8_t b1 = buf[9];  // shoulders, triggers, sticks, create/options
        const uint8_t b2 = buf[10]; // PS, touchpad, mute, Edge buttons

        // Face buttons (byte 8, верхний ниббл)
        snap[Button::Square]   = b0 & 0x10;
        snap[Button::Cross]    = b0 & 0x20;
        snap[Button::Circle]   = b0 & 0x40;
        snap[Button::Triangle] = b0 & 0x80;

        // D-pad — декомпозиция hat-switch в отдельные направления
        const uint8_t hat = b0 & 0x0F;
        snap[Button::DPadUp]    = (hat == 0 || hat == 1 || hat == 7);
        snap[Button::DPadRight] = (hat == 1 || hat == 2 || hat == 3);
        snap[Button::DPadDown]  = (hat == 3 || hat == 4 || hat == 5);
        snap[Button::DPadLeft]  = (hat == 5 || hat == 6 || hat == 7);

        // Byte 9
        snap[Button::L1]      = b1 & 0x01;
        snap[Button::R1]      = b1 & 0x02;
        snap[Button::L2]      = b1 & 0x04;
        snap[Button::R2]      = b1 & 0x08;
        snap[Button::Create]  = b1 & 0x10;
        snap[Button::Options] = b1 & 0x20;
        snap[Button::L3]      = b1 & 0x40;
        snap[Button::R3]      = b1 & 0x80;

        // Byte 10
        snap[Button::PS]       = b2 & 0x01;
        snap[Button::Touchpad] = b2 & 0x02;
        snap[Button::Mute]     = b2 & 0x04;
        // Edge-exclusive (верхний ниббл)
        snap[Button::LFN]     = b2 & 0x10;
        snap[Button::RFN]     = b2 & 0x20;
        snap[Button::LB]      = b2 & 0x40;
        snap[Button::RB]      = b2 & 0x80;

        return snap;
    }

    /// Применить состояние кнопок обратно в HID-репорт (для playback overlay).
    /// forced — какие кнопки макрос форсирует (true = форсировать).
    /// Только форсированные биты перезаписываются в буфере.
    void apply_to_report(uint8_t* buf, size_t len,
                         const std::array<bool, BUTTON_COUNT>& forced) const {
        if (len < 11 || buf[0] != 0x01)
            return;

        // ── Byte 8: face buttons (биты 4–7) ─────────────────────────────────
        auto set_bit = [&](Button btn, uint8_t& byte, uint8_t mask) {
            if (!forced[static_cast<size_t>(btn)]) return;
            if (state[static_cast<size_t>(btn)])
                byte |= mask;
            else
                byte &= ~mask;
        };

        uint8_t& b0 = buf[8];
        set_bit(Button::Square,   b0, 0x10);
        set_bit(Button::Cross,    b0, 0x20);
        set_bit(Button::Circle,   b0, 0x40);
        set_bit(Button::Triangle, b0, 0x80);

        // ── D-pad: перекомпозиция hat-switch ─────────────────────────────────
        bool dpad_forced = forced[static_cast<size_t>(Button::DPadUp)]   ||
                           forced[static_cast<size_t>(Button::DPadDown)] ||
                           forced[static_cast<size_t>(Button::DPadLeft)] ||
                           forced[static_cast<size_t>(Button::DPadRight)];
        if (dpad_forced) {
            // Берём текущее физическое состояние d-pad из репорта
            bool up    = state[static_cast<size_t>(Button::DPadUp)];
            bool down  = state[static_cast<size_t>(Button::DPadDown)];
            bool left  = state[static_cast<size_t>(Button::DPadLeft)];
            bool right = state[static_cast<size_t>(Button::DPadRight)];

            // Противоположные направления отменяют друг друга
            if (up && down)    { up = false; down = false; }
            if (left && right) { left = false; right = false; }

            uint8_t hat = 0x08; // neutral
            if (up && !right && !left) hat = 0;
            else if (up && right)      hat = 1;
            else if (right && !down)   hat = 2;
            else if (down && right)    hat = 3;
            else if (down && !left)    hat = 4;
            else if (down && left)     hat = 5;
            else if (left && !up)      hat = 6;
            else if (up && left)       hat = 7;

            b0 = (b0 & 0xF0) | hat;
        }

        // ── Byte 9 ──────────────────────────────────────────────────────────
        uint8_t& b1 = buf[9];
        set_bit(Button::L1,      b1, 0x01);
        set_bit(Button::R1,      b1, 0x02);
        set_bit(Button::L2,      b1, 0x04);
        set_bit(Button::R2,      b1, 0x08);
        set_bit(Button::Create,  b1, 0x10);
        set_bit(Button::Options, b1, 0x20);
        set_bit(Button::L3,      b1, 0x40);
        set_bit(Button::R3,      b1, 0x80);

        // ── Byte 10 ─────────────────────────────────────────────────────────
        uint8_t& b2 = buf[10];
        set_bit(Button::PS,       b2, 0x01);
        set_bit(Button::Touchpad, b2, 0x02);
        set_bit(Button::Mute,     b2, 0x04);
        set_bit(Button::LFN,      b2, 0x10);
        set_bit(Button::RFN,      b2, 0x20);
        set_bit(Button::LB,       b2, 0x40);
        set_bit(Button::RB,       b2, 0x80);
    }
};

// ─── Macro event — одно действие в макросе ──────────────────────────────────

struct MacroEvent {
    enum class Kind : uint8_t { Button, LeftStick, RightStick };

    Kind   kind     = Kind::Button;
    Button button   = Button::Cross; // Kind::Button only
    bool   pressed  = true;          // Kind::Button only: нажатие / отпускание
    int8_t axis_x   = 0;             // Kind::LeftStick / RightStick: signed (-128..127)
    int8_t axis_y   = 0;
    int    delay_ms = 0;             // задержка ПЕРЕД этим событием (мс)

    QJsonObject to_json() const {
        if (kind == Kind::Button) {
            return {
                {"kind",     "button"},
                {"button",   button_to_string(button)},
                {"pressed",  pressed},
                {"delay_ms", delay_ms}
            };
        }
        return {
            {"kind",     kind == Kind::LeftStick ? "lstick" : "rstick"},
            {"axis_x",   axis_x},
            {"axis_y",   axis_y},
            {"delay_ms", delay_ms}
        };
    }

    static MacroEvent from_json(const QJsonObject& obj) {
        MacroEvent ev;
        const auto k = obj["kind"].toString("button");
        if (k == "lstick" || k == "rstick") {
            ev.kind    = (k == "lstick") ? Kind::LeftStick : Kind::RightStick;
            ev.axis_x  = static_cast<int8_t>(obj["axis_x"].toInt(0));
            ev.axis_y  = static_cast<int8_t>(obj["axis_y"].toInt(0));
            ev.delay_ms = obj["delay_ms"].toInt(0);
        } else {
            ev.kind    = Kind::Button;
            ev.button  = button_from_string(obj["button"].toString());
            ev.pressed = obj["pressed"].toBool(true);
            ev.delay_ms = obj["delay_ms"].toInt(0);
        }
        return ev;
    }
};

// ─── Macro — именованная последовательность событий с триггером ──────────────

struct Macro {
    QString                name;
    Button                 trigger  = Button::COUNT_;  // кнопка-триггер
    bool                   enabled  = true;
    std::vector<MacroEvent> events;

    QJsonObject to_json() const {
        QJsonArray arr;
        for (const auto& ev : events)
            arr.append(ev.to_json());

        return {
            {"name",    name},
            {"trigger", button_to_string(trigger)},
            {"enabled", enabled},
            {"events",  arr}
        };
    }

    static Macro from_json(const QJsonObject& obj) {
        Macro m;
        m.name    = obj["name"].toString("Unnamed");
        m.trigger = button_from_string(obj["trigger"].toString());
        m.enabled = obj["enabled"].toBool(true);

        for (const auto& val : obj["events"].toArray())
            m.events.push_back(MacroEvent::from_json(val.toObject()));

        return m;
    }
};

} // namespace macro
