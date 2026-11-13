# XP-Pen Deco 640 Android Root Userspace Driver (`deco640d`)

A lightweight, zero-dependency C userspace daemon for **rooted Android** devices (via Termux + `su` / KernelSU / Magisk) that unlocks full functionality for the **XP-Pen Deco 640 (IT640, USB ID `28bd:2904`)**.

On Android, standard `hid-generic` drivers leave the 8 soft shortcut keys inactive, lack customizable pen button actions, and cause pen drags in apps like **WPS Office** to select text instead of scrolling. `deco640d` switches the tablet into vendor report mode (`02 B0 04` on USB interface `2`) and creates clean virtual `/dev/uinput` devices (`Deco640-Stylus`, `Deco640-Touch`, `Deco640-Keyboard`).

---

## Features / Özellikler

- **Full Hardware Support (`28bd:2904`)**:
  - 8 soft shortcut keys (`AuxButtons[0..7]`)
  - 2 stylus barrel buttons
  - 16,384 pressure levels (`0..16383`) & X/Y tilt (`-127..127`)
  - Auto-detected screen resolution via `wm size` (or manual `--w` / `--h`)
  - Orientation rotation (`--rot 0|90|180|270`, default `270` for vertical phone sync)
- **Visible Hover Cursor + Natural Touch Scrolling**:
  - Hovering displays the pointer cursor (`INPUT_PROP_POINTER`).
  - Pressing and dragging the pen tip (default **Touch/Scroll Mode**) injects real touchscreen events (`BUS_VIRTUAL` + `INPUT_PROP_DIRECT`), so apps like **WPS Office**, PDF viewers, and browsers scroll smoothly instead of selecting text.
  - Press **Shortcut 5** anytime to toggle **Drawing Mode** (full 16k pressure + tilt on the pen tip) for apps like Ibis Paint, Krita, or Infinite Painter.
- **Hot-Plug Safe**:
  - Automatically grabs raw kernel `evdev` nodes (`EVIOCGRAB`) while active and cleanly destroys all virtual `uinput` devices on USB disconnect so the cursor disappears immediately and touchscreen input never gets stuck.

---

## Default Button Mapping / Varsayılan Tuş Atamaları

### Pen / Kalem
| Input | Default Mode (Touch/Scroll) | Drawing Mode (Key 5) |
|---|---|---|
| **Hover** | Moves cursor | Moves cursor |
| **Pen Tip Tap / Drag** | Tap / **Smooth Scroll** (finger touch) | Pressure-sensitive draw / select |
| **Pen Button 1** | **Left Click / Text Selection** (stylus press) | Left Click / Stylus press |
| **Pen Button 2 (Tap)** | **Right Click** (`BTN_RIGHT`) | Right Click (`BTN_RIGHT`) |
| **Pen Button 2 (Hold + Move)** | Smooth damped scroll (45% speed) | Smooth damped scroll (45% speed) |

### Tablet Soft Shortcut Keys (1–8)
| Key | Action (EN) | İşlev (TR) |
|---|---|---|
| **1** | Undo (`Ctrl+Z`) | Geri Al (`Ctrl+Z`) |
| **2** | Redo (`Ctrl+Y`) | İleri Al (`Ctrl+Y`) |
| **3** | Scroll Down (hold to repeat) | Aşağı Kaydır (basılı tutunca devam eder) |
| **4** | Scroll Up (hold to repeat) | Yukarı Kaydır (basılı tutunca devam eder) |
| **5** | Toggle Touch/Scroll ↔ Drawing Mode | Kaydırma ↔ Çizim Modu Geçişi |
| **6** | Zoom In (`Ctrl+=`) | Yakınlaştır (`Ctrl+=`) |
| **7** | Zoom Out (`Ctrl+-`) | Uzaklaştır (`Ctrl+-`) |
| **8** | Android Back (`KEY_BACK`) | Android Geri (`KEY_BACK`) |

---

## Requirements / Gereksinimler

- Rooted Android device with `/dev/uinput` and `/dev/bus/usb` access (tested on Android with KernelSU)
- [Termux](https://termux.dev/) with `clang` or `gcc` and `make`:
  ```bash
  pkg install clang make
  ```

---

## Build & Run / Kurulum ve Çalıştırma

```bash
git clone https://github.com/xe-ark/deco640-android-driver.git
cd deco640-android-driver
make
./start.sh 270
```

- **Stop daemon / Durdurmak için:**
  ```bash
  ./stop.sh
  ```
- **Change rotation / Yönü değiştirmek için:**
  If you hold the tablet vertically the opposite way (shortcut keys on the right instead of the left), start with `90`:
  ```bash
  ./start.sh 90
  ```
  Supported rotations: `0`, `90`, `180`, `270`.
- **Direct CLI options:**
  ```bash
  su -c "./deco640d --rot 270"
  # Optional flags: --w <width> --h <height> --drawing --help
  ```

---

## Credits

- USB vendor initialization (`0x02 0xB0 0x04`) and 12-byte report structure based on [OpenTabletDriver](https://github.com/OpenTabletDriver/OpenTabletDriver) (`Deco 640 (IT640).json` & `XP_PenReportParser`).

## License

MIT — see [LICENSE](LICENSE).
