# flipper_tv_remote

TV remote replacement Flipper Zero App — record buttons from any IR remote and replay them with your Flipper Zero.

## Features

- **Learn mode** — point your existing remote at the Flipper and record up to 12 buttons (Power, Mute, Vol+, Vol−, Ch+, Ch−, Up, Down, Left, Right, OK, Back)
- **Remote mode** — navigate a 4×3 button grid with the d-pad and transmit the stored signal by pressing OK
- Signals are saved automatically to `SD:/infrared/TV_Remote.ir` in the standard Flipper IR format, so they are compatible with the built-in Infrared app and other community apps

## Building and Installing

### Prerequisites

| Requirement | Notes |
|---|---|
| [Flipper Zero](https://flipperzero.one/) | With a microSD card inserted |
| Python 3.8 or newer | Required by uFBT |
| [uFBT](https://github.com/flipperdevices/flipperzero-ufbt) | Flipper build tool for external apps |
| USB-C cable | For deploying the app directly from your computer |

### 1 — Install uFBT

```bash
# Linux / macOS
python3 -m pip install --upgrade ufbt

# Windows
py -m pip install --upgrade ufbt
```

On first run uFBT automatically downloads the Flipper SDK matching your device's firmware, so no separate firmware checkout is needed.

### 2 — Clone this repository

```bash
git clone https://github.com/jmanion0139/flipper_tv_remote.git
cd flipper_tv_remote
```

### 3 — Build the app

```bash
ufbt
```

This compiles the source and produces a `.fap` (Flipper App Package) file inside the `dist/` directory.

### 4 — Deploy to your Flipper Zero

**Option A — Deploy and launch automatically (recommended)**

Connect your Flipper via USB, unlock it, and run:

```bash
ufbt launch
```

uFBT uploads the `.fap` to the device and starts the app immediately.

**Option B — Copy the file manually**

1. Copy `dist/flipper_tv_remote.fap` to the `apps/Infrared/` folder on your Flipper's microSD card.
2. On the Flipper, go to **Applications → Infrared** and open **TV Remote**.

## Usage

### Learning buttons

1. From the main menu select **Learn Remote**.
2. For each of the 12 buttons, point your original remote at the Flipper's IR receiver (top of the device) and press the button.
3. When the Flipper shows the decoded signal:
   - **OK** — save and move to the next button
   - **Right (→)** — skip this button
   - **Left (←)** — discard the signal and try again
4. Press **Back** at any time to stop learning and return to the menu. All buttons recorded so far are saved.

### Using as a remote

1. From the main menu select **Use Remote**.
2. Use the **d-pad** to highlight a button in the 4×3 grid.
3. **Hold OK** to transmit the signal repeatedly. Buttons that have not been learned are shown as `--`.
4. Press **Back** to return to the menu.

### Button layout

```
[ PWR ] [ MUT ] [ V+  ] [ V-  ]
[ C+  ] [ C-  ] [  ↑  ] [  ↓  ]
[  ←  ] [  →  ] [ OK  ] [ BCK ]
```

## Project structure

```
flipper_tv_remote/
├── application.fam          # App manifest (appid, name, category, …)
├── flipper_tv_remote.c      # Entry point, lifecycle, file I/O
├── flipper_tv_remote.h      # Shared data structures and constants
└── views/
    ├── tv_remote_learn.c    # IR receive/decode logic (learn mode)
    ├── tv_remote_learn.h
    ├── tv_remote_remote.c   # Button grid display and IR transmit (remote mode)
    └── tv_remote_remote.h
```

Recorded signals are stored at `SD:/infrared/TV_Remote.ir` in the standard Flipper IR file format.

## Troubleshooting

| Symptom | Fix |
|---|---|
| `ufbt launch` cannot find the device | Make sure the Flipper is connected via USB, unlocked, and not in any app that blocks the USB serial port |
| App shows API version mismatch | Run `ufbt update` to download the SDK matching your current firmware, then rebuild with `ufbt` |
| Buttons not recognized during learning | Keep the original remote close to the Flipper (< 30 cm) and ensure nothing is blocking the IR window on top of the device |
| Learned signals are lost after restarting | Check that a microSD card is inserted; the app saves to `SD:/infrared/TV_Remote.ir` |

## License

This project is licensed under the [GNU General Public License v3.0](LICENSE).
