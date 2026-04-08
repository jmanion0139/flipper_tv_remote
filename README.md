# Flipper TV Remote

A Flipper Zero external app (`.fap`) that uses the built-in IR transceiver to
**record buttons from any TV remote** and then lets you use the Flipper as that
TV remote.

## Features

- **Learn mode** – sequentially records IR signals for 12 standard TV buttons:
  Power, Mute, Volume Up/Down, Channel Up/Down, directional navigation (Up,
  Down, Left, Right), OK and Back.
- **Remote mode** – displays a 4 × 3 button grid on screen; navigate with the
  d-pad, press **OK** to transmit the highlighted button's IR signal. Hold OK
  to repeat (simulates holding a button on the original remote).
- Learned signals are automatically saved to  
  `SD Card/infrared/TV_Remote.ir` on exit and reloaded on the next launch.
- Compatible with the standard Flipper `.ir` file format, so the saved file can
  be used with other Flipper IR applications.

## Usage

### 1. Learn your TV remote

1. Open the app and select **Learn Remote**.
2. For each button the screen shows which button to teach (e.g. "Button 1/12: Power").
3. Point your TV remote at the Flipper's IR receiver and press the corresponding
   button.
4. When the Flipper receives the signal it shows the decoded protocol / command
   information.
   - Press **OK** to save the signal and move to the next button.
   - Press **Right** (→) to skip the current button without saving.
   - Press **Left** (←) to discard the received signal and try again.
5. Repeat for all 12 buttons (or skip any you don't need).
6. Press **Left** at any time to stop learning and return to the menu.

### 2. Use the remote

1. From the main menu select **Use Remote**.
2. A 4 × 3 grid of buttons is displayed.  Buttons that have not been learned
   are shown as `--`.
3. Navigate the cursor with the d-pad.
4. Press and **hold** the **OK** button to transmit the highlighted signal.
   Release to stop transmitting.
5. Press **Back** to return to the main menu.

## Button grid layout

```
[ PWR ] [ MUT ] [ V+  ] [ V-  ]
[ C+  ] [ C-  ] [  ↑  ] [  ↓  ]
[  ←  ] [  →  ] [ OK  ] [ BCK ]
```

| Cell | IR button | Description |
|------|-----------|-------------|
| PWR  | Power     | Power on/off |
| MUT  | Mute      | Mute / unmute |
| V+   | Vol_up    | Volume up |
| V-   | Vol_dn    | Volume down |
| C+   | Ch_up     | Channel up |
| C-   | Ch_dn     | Channel down |
| ↑    | Up        | Navigation up |
| ↓    | Down      | Navigation down |
| ←    | Left      | Navigation left |
| →    | Right     | Navigation right |
| OK   | Ok        | Select / OK |
| BCK  | Back      | Back / return |

## Building

This app is built as a Flipper Zero external application (FAP).

```bash
# From inside the Flipper Zero firmware tree, with this repo as an external app:
./fbt fap_flipper_tv_remote

# Or build and launch on a connected Flipper:
./fbt launch APPSRC=applications_user/flipper_tv_remote
```

## File format

Learned signals are stored in the standard Flipper IR file format at  
`SD Card/infrared/TV_Remote.ir`.  You can edit or share this file with any
other Flipper IR application.

## License

See [LICENSE](LICENSE).
