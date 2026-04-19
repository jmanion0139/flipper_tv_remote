# flipper_tv_remote

TV remote replacement app for the Flipper Zero - record IR buttons from any TV remote and replay them directly from your Flipper.

## Features

- **Multiple named remotes** — save as many remotes as you like, each stored as a separate file
- **Learn mode** — record up to 12 buttons from any IR remote (Power, Vol+, Vol−, Ch+, Ch−, Up, Down, Left, Right, OK, Back, Home)
- **Concentric-circle remote UI** — press or hold d-pad buttons to transmit signals; short-press fires the primary action, hold fires the alternate action
- **Double-tap Back for Power** — tap Back twice quickly to send the Power signal
- **Settings** — choose between Vertical (portrait) and Horizontal (landscape) screen orientation, saved across restarts
- **Button Map** — quick in-app reference showing every button's press and hold action
- **About** — author and license info

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

On first run uFBT automatically downloads the Flipper SDK matching your device's firmware.

### 2 — Clone this repository

```bash
git clone https://github.com/jmanion0139/flipper_tv_remote.git
cd flipper_tv_remote
```

### 3 — Build the app

```bash
ufbt
```

Produces `dist/flipper_tv_remote.fap`.

### 4 — Deploy to your Flipper Zero

**Option A — Deploy and launch automatically (recommended)**

Connect your Flipper via USB, unlock it, and run:

```bash
ufbt launch
```

**Option B — Copy the file manually**

Copy `dist/flipper_tv_remote.fap` to `apps/Infrared/` on your Flipper's microSD card, then open it from **Applications → Infrared → TV Remote**.

## Usage

### Main menu

| Item | Action |
|---|---|
| **Learn Remote** | Record a new remote or update an existing one |
| **Use Remote** | Pick a saved remote and use your Flipper as a TV remote |
| **Delete Remote** | Remove a saved remote from the SD card |
| **Button Map** | View the full press/hold button reference |
| **Settings** | Change screen orientation |
| **About** | Author and license info |

### Learning buttons

1. From the main menu select **Learn Remote**.
2. Choose **New Remote** and type a name, or choose **Update Remote** to overwrite an existing one.
3. For each of the 12 buttons, point your original remote at the Flipper's IR window (top of the device) and press the button when prompted.
4. When a signal is received:
   - **OK** — save and advance to the next button
   - **Right (→)** — skip this button
   - **Left (←)** — discard and retry
5. Press **Back** at any time to stop learning and return to the menu.
6. Once all buttons are done the remote is saved automatically.

### Using as a remote

1. From the main menu select **Use Remote** and pick a saved remote.
2. Use the d-pad and OK button to send IR signals.

### Button mapping

| Physical key | Press | Hold |
|---|---|---|
| Up | Up | Vol Up |
| Down | Down | Vol Dn |
| Left | Left | Ch Dn |
| Right | Right | Ch Up |
| OK | OK | Home |
| Back | Back IR | — |
| Back × 2 (double-tap) | Power | — |
| Back (long hold) | Exit app | — |

### Settings — Orientation

| Option | Description |
|---|---|
| **Vertical** | Flipper rotated 90° so it resembles a tall remote — d-pad on the right side |
| **Horizontal** | Flipper held normally in landscape — concentric circle on the right, Back/Power on the left |

The setting is saved to SD card and restored on next launch.

## File storage

| Path | Contents |
|---|---|
| `SD:/infrared/tv_remote_<name>.ir` | Saved IR signals for a named remote |
| `SD:/infrared/tv_remote_settings.dat` | App settings (orientation) |

Saved `.ir` files use the standard Flipper IR format and are compatible with the built-in Infrared app.

## Project structure

```
flipper_tv_remote/
├── application.fam          # App manifest
├── flipper_tv_remote.c      # Entry point, lifecycle, file I/O, menus
├── flipper_tv_remote.h      # Shared types, constants, app struct
├── icons/
│   └── tv_remote_10px.png   # App icon (10×10 monochrome)
└── views/
    ├── tv_remote_learn.c    # IR receive/decode logic (learn mode)
    ├── tv_remote_learn.h
    ├── tv_remote_remote.c   # Concentric-circle remote UI and IR transmit
    └── tv_remote_remote.h
```

## Troubleshooting

| Symptom | Fix |
|---|---|
| `ufbt launch` cannot find the device | Ensure the Flipper is connected via USB, unlocked, and not in an app that blocks the USB serial port |
| App shows API version mismatch | Run `ufbt update` to download the SDK matching your firmware, then rebuild |
| Buttons not recognised during learning | Hold the original remote within ~30 cm of the Flipper's IR window with a clear line of sight |
| Saved remotes lost after restart | Check that a microSD card is inserted |

## License

This project is licensed under the [GNU General Public License v3.0](LICENSE).
