# DS5 Edge Relay

**DS5 Edge Relay** is a Qt6 GUI application for Linux that creates a virtual DualSense (054C:0CE6) controller via `/dev/uhid` and proxies input from a physical DualSense Edge (054C:0DF2) through `/dev/hidraw*`.

---

## Why it exists

The DualSense Edge has a non-standard USB PID (0x0DF2) and extra back buttons (L4, LB, R4, RB, LFN, RFN) that Proton and many Linux games do not recognise. The relay daemon makes the Edge appear as a standard DualSense — restoring full game compatibility while adding button remapping on top.

---

## Features

| Feature | Details |
|---|---|
| HID relay | Reads raw reports from the physical Edge, forwards them to a virtual DS5 |
| Button remapping | Visual click-on-gamepad editor — bind any button to one or more others |
| Multi-direction D-pad | Bindings to multiple D-pad directions combine correctly into hat-switch diagonals |
| Live binding updates | Toggle/untoggle bindings while the relay is running — no restart needed |
| Edge-exclusive buttons | LFN, RFN, LB, RB usable as trigger sources for remapping |
| Output forwarding | Haptics, adaptive triggers and LED commands are forwarded back to the physical device |
| Auto-reconnect | Automatically reconnects when the controller is unplugged and plugged back in |
| System tray | Minimises to tray; "Run in background" keeps the relay alive after window close |
| Autostart | One-click toggle adds/removes a `~/.config/autostart/` entry |
| Localisation | Russian / English UI, switchable at runtime |
| Custom title bar | Frameless window with native-feeling drag and buttons |

---

## Requirements

- **OS:** Linux (kernel ≥ 5.15 recommended)
- **Runtime:** Qt 6.2+
- **Build:** CMake ≥ 3.20, Ninja, C++23 compiler
- **Devices:** `/dev/uhid`, `/dev/hidraw*`

---

## Installation

### From AUR (Arch Linux)

```bash
yay -S ds5-edge-relay
# or
paru -S ds5-edge-relay
```

### Manual build

```bash
git clone https://github.com/Follen22/ds5-edge-relay
cd ds5-edge-relay
cmake -B build -GNinja -DCMAKE_BUILD_TYPE=Release
ninja -C build
sudo ninja -C build install   # optional
```

### Device permissions (without sudo)

The package installs udev rules automatically. To apply them immediately:

```bash
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=hidraw
```

Add yourself to the `input` group for reliable access:

```bash
sudo usermod -aG input $USER
# then log out and back in, or:
newgrp input
```

---

## Usage

### Starting the relay

1. Launch **DS5 Edge Relay** from your application menu or run `ds5-edge-relay`.
2. Connect your DualSense Edge via USB or Bluetooth.
3. Click **▶ Start** — the status indicator turns green and the virtual controller appears.
4. Launch your game. It will see a standard DualSense (054C:0CE6).
5. Click **⏹ Stop** to disconnect the virtual controller.

### Button remapping

1. Check **Use bindings** to expand the remapping editor.
2. Click **Add binding**.
3. Click the **trigger button** on the gamepad diagram (the button you want to remap *from*).
4. Click one or more **target buttons** (what it should produce). Click again to deselect.
5. Click **Apply** — the binding appears in the list.
6. Each binding has a checkbox to enable/disable it **live** (takes effect on the next HID report).
7. Click **✕** next to a binding to delete it.

Bindings are saved to `~/.config/ds5-edge-relay/binds.json` and loaded automatically on next launch.

**D-pad notes:**
- Binding a button to multiple D-pad directions creates diagonals (e.g. Up+Right → NE).
- Opposing directions cancel each other (Left+Right → neutral, Left+Up+Right → Up).

### Running in background

Enable **Run in background when window is closed** — the relay keeps running in the system tray after you close the window. Click the tray icon to reopen it.

### Autostart with system

Enable **Autostart with system** to add an XDG autostart entry. The application starts automatically on login.

Alternatively, enable the systemd user service:

```bash
systemctl --user enable --now ds5-edge-relay
```

---

## Binding file format

`~/.config/ds5-edge-relay/binds.json`

```json
{
  "binds": [
    { "enabled": true, "trigger": "LB", "actions": ["Cross"] },
    { "enabled": true, "trigger": "RB", "actions": ["DPadUp", "DPadRight"] }
  ]
}
```

Valid button names: `Cross`, `Circle`, `Square`, `Triangle`, `L1`, `R1`, `L2`, `R2`, `L3`, `R3`, `Options`, `Create`, `PS`, `Touchpad`, `DPadUp`, `DPadDown`, `DPadLeft`, `DPadRight`, `LFN`, `RFN`, `LB`, `RB`.

---

## Architecture

```
MainWindow (Qt GUI thread)
├── BindEditorWidget   — visual binding editor, emits bindingsChanged()
│   ├── GamepadWidget  — clickable gamepad diagram (1.png, Screen blend)
│   └── BindStorage    — JSON persistence (~/.config/ds5-edge-relay/binds.json)
└── RelayWorker (QThread)
    ├── find_hidraw_device()  — scans sysfs for VID:PID 054C:0DF2
    ├── UhidDevice            — creates /dev/uhid virtual DualSense 054C:0CE6
    ├── poll() loop           — multiplexes physical hidraw ↔ virtual uhid
    ├── apply_bindings()      — remaps buttons in raw HID report buffer
    └── update_bindings()     — thread-safe live update from UI thread
```

---

## License

MIT — see [LICENSE](LICENSE).
