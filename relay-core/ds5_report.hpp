#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace ds5 {

// ── Button descriptor ─────────────────────────────────────────────────────────

struct ButtonBit {
    size_t  byte_offset;
    uint8_t mask;
    // 0xFF = normal bitmask: pressed when (buf[off] & mask) != 0
    // 0..8 = D-pad hat switch: pressed when (buf[off] & mask) == dpad_value
    uint8_t dpad_value = 0xFF;
};

// ── Button constants (USB input report with Report ID) ────────────────────────
//
// buf[0]  = Report ID (0x01)
// buf[5]  = L2 analog (0–255)
// buf[6]  = R2 analog (0–255)
// buf[8]  = [7:△][6:○][5:✕][4:□] | [3:0] D-pad hat (0=N,2=E,4=S,6=W,8=none)
// buf[9]  = [7:R3][6:L3][5:Options][4:Create][3:R2][2:L2][1:R1][0:L1]
// buf[10] = [7:RB][6:LB][5:RFN][4:LFN][2:Mute][1:Touchpad][0:PS]
// buf[44] = [7:RB_alt][6:R4][5:LB_alt][4:L4]  (alternate Edge offsets — NOT USED)

// Face buttons
inline constexpr ButtonBit BTN_SQUARE    = {8, 0x10};
inline constexpr ButtonBit BTN_CROSS     = {8, 0x20};
inline constexpr ButtonBit BTN_CIRCLE    = {8, 0x40};
inline constexpr ButtonBit BTN_TRIANGLE  = {8, 0x80};

// D-pad hat switch (lower nibble of buf[8])
inline constexpr ButtonBit BTN_DPAD_UP    = {8, 0x0F, 0};
inline constexpr ButtonBit BTN_DPAD_RIGHT = {8, 0x0F, 2};
inline constexpr ButtonBit BTN_DPAD_DOWN  = {8, 0x0F, 4};
inline constexpr ButtonBit BTN_DPAD_LEFT  = {8, 0x0F, 6};

// Shoulder / trigger / stick buttons
inline constexpr ButtonBit BTN_L1        = {9, 0x01};
inline constexpr ButtonBit BTN_R1        = {9, 0x02};
inline constexpr ButtonBit BTN_L2        = {9, 0x04};
inline constexpr ButtonBit BTN_R2        = {9, 0x08};
inline constexpr ButtonBit BTN_CREATE    = {9, 0x10};
inline constexpr ButtonBit BTN_OPTIONS   = {9, 0x20};
inline constexpr ButtonBit BTN_L3        = {9, 0x40};
inline constexpr ButtonBit BTN_R3        = {9, 0x80};

// Special buttons
inline constexpr ButtonBit BTN_PS        = {10, 0x01};
inline constexpr ButtonBit BTN_TOUCHPAD  = {10, 0x02};
inline constexpr ButtonBit BTN_MUTE      = {10, 0x04};

// DualSense Edge extra buttons (byte 10, upper nibble)
inline constexpr ButtonBit BTN_LFN = {10, 0x10};  // left  Fn button  (bit4)
inline constexpr ButtonBit BTN_RFN = {10, 0x20};  // right Fn button  (bit5)
inline constexpr ButtonBit BTN_LB  = {10, 0x40};  // left  paddle     (bit6)
inline constexpr ButtonBit BTN_RB  = {10, 0x80};  // right paddle     (bit7)

// Analog axis offsets
inline constexpr size_t OFFSET_L2_ANALOG = 5;
inline constexpr size_t OFFSET_R2_ANALOG = 6;

// ── Binding descriptor ────────────────────────────────────────────────────────

struct ButtonBinding {
    ButtonBit              source;
    std::vector<ButtonBit> targets;
    bool                   passthrough  = false;  // keep source bit if true
    size_t                 clear_analog = 0;       // zero this analog offset (0 = skip)
};

// ── Apply all bindings to a raw input report buffer ──────────────────────────

inline void apply_bindings(uint8_t* buf, size_t len,
                           const std::vector<ButtonBinding>& bindings)
{
    if (len < 12 || buf[0] != 0x01) return;

    // Full snapshot so that targets written by earlier bindings do not
    // accidentally trigger later bindings in the same pass.
    uint8_t orig[64] = {};
    std::memcpy(orig, buf, (len < 64 ? len : 64));

    // D-pad hat-switch targets from ALL active bindings must be combined into a
    // single compass value.  Accumulate direction flags here across every binding
    // and apply once after the loop — this handles simultaneous button presses
    // that each contribute a different D-pad direction.
    //   bit0=Up(0) bit1=Right(2) bit2=Down(4) bit3=Left(6)
    static constexpr uint8_t DPAD_COMBINE[16] = {
        8, 0, 2, 1, 4, 8, 3, 2, 6, 7, 8, 0, 5, 6, 4, 8
    };
    uint8_t dpad_flags   = 0;
    bool    has_dpad_tgt = false;

    for (const auto& b : bindings) {
        const size_t off = b.source.byte_offset;
        if (off >= len || off >= 64) continue;

        // Check source against snapshot
        const bool pressed = (b.source.dpad_value != 0xFF)
            ? (orig[off] & b.source.mask) == b.source.dpad_value   // D-pad hat
            : (orig[off] & b.source.mask) != 0;                     // normal bit
        if (!pressed) continue;

        // Clear source unless passthrough
        if (!b.passthrough) {
            if (b.source.dpad_value != 0xFF)
                buf[off] = static_cast<uint8_t>((buf[off] & ~b.source.mask) | 0x08); // D-pad released
            else
                buf[off] &= static_cast<uint8_t>(~b.source.mask);
        }

        // Zero analog axis
        if (b.clear_analog != 0 && b.clear_analog < len)
            buf[b.clear_analog] = 0;

        for (const auto& t : b.targets) {
            if (t.byte_offset >= len) continue;
            if (t.dpad_value != 0xFF && t.byte_offset == 8 && t.mask == 0x0F) {
                // Accumulate D-pad direction — applied after all bindings
                has_dpad_tgt = true;
                if      (t.dpad_value == 0) dpad_flags |= 0x01; // Up
                else if (t.dpad_value == 2) dpad_flags |= 0x02; // Right
                else if (t.dpad_value == 4) dpad_flags |= 0x04; // Down
                else if (t.dpad_value == 6) dpad_flags |= 0x08; // Left
            } else {
                buf[t.byte_offset] |= t.mask;
            }
        }
    }

    // Apply accumulated D-pad result once — combines directions from all bindings
    if (has_dpad_tgt)
        buf[8] = static_cast<uint8_t>((buf[8] & ~0x0Fu) | DPAD_COMBINE[dpad_flags]);
}

} // namespace ds5
