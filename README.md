# logi-rgb-kb-driver

`gpro-rgb`: control the RGB lighting of a **Logitech G Pro** keyboard (`046d:c339`, wired,
tenkeyless) from Linux, in plain C with no dependencies.

It talks to the keyboard through `/dev/hidraw`, from user space, so no kernel module
is needed. The PC Dash (`~/Documents/Workspace/3-WEB/pcdash`) Keyboard page uses it for its per-key color picker.

## Build and install

Requires a C compiler, `make` and Linux headers.

```fish
make
make install PREFIX=~/.local   # or `sudo make install` for /usr/local/bin
sudo make install-udev         # access without root + restore colors on plug-in
make install-service           # user service that re-applies your colors
```

Unplug and replug the keyboard after installing the udev rule.

| Target | What it does |
|---|---|
| `make` | builds `./gpro-rgb` |
| `make install` | installs to `$(PREFIX)/bin` (default `/usr/local`) |
| `make install-udev` | installs `udev/70-logitech-gpro.rules` to `/etc/udev/rules.d` (needs root) |
| `make install-service` | installs and enables `gpro-rgb-restore.service` for your user |
| `make clean` | removes build output |

## Usage

```fish
gpro-rgb list                      # the keyboard's hidraw nodes
gpro-rgb all ff0000                # every key and the logo
gpro-rgb logo 00ff00
gpro-rgb code 04 0000ff            # one key by code (04 = A on QWERTY, Q on AZERTY)
gpro-rgb keys all=000000 29=ff0000 e0=00ff00 logo=ffffff
gpro-rgb load ~/.config/gpro-rgb/profile
```

Colors are `rrggbb` (a leading `#` is accepted). `keys` and `load` send everything and
apply it in a single commit, so the keyboard changes all at once. `all=` is applied
first, so later entries override it.

### Profile files

One entry per line, `#` starts a comment, blank lines are ignored:

```
# My layout
all=00ff40
29=ff0000     # Échap
e0=00e5ff     # left Ctrl
logo=8000ff
```

Entries have no spaces around `=`.

### Key codes

Codes are the key's [USB HID usage ID](https://usb.org/sites/default/files/hut1_5.pdf)
(Keyboard page 0x07), in hex. They name the key's *position*: the code is the same
whatever layout your OS uses. Esc, Q, Space, both ISO keys and the modifiers were
checked on the keyboard; the rest follow the same standard:

| Keys | Codes |
|---|---|
| Letters (QWERTY positions A–Z) | `04`–`1d` |
| Number row 1–0 | `1e`–`27` |
| Enter, Esc, Backspace, Tab, Space | `28`, `29`, `2a`, `2b`, `2c` |
| `-` `=` `[` `]` `\` | `2d`–`31` |
| ISO key next to Enter, `;` `'` `` ` `` `,` `.` `/` | `32`–`38` |
| Caps Lock, F1–F12 | `39`, `3a`–`45` |
| Print Screen, Scroll Lock, Pause | `46`–`48` |
| Insert, Home, Page Up, Delete, End, Page Down | `49`–`4e` |
| Right, Left, Down, Up | `4f`–`52` |
| ISO key next to left Shift, Menu | `64`, `65` |
| Left Ctrl, Shift, Alt, Win | `e0`–`e3` |
| Right Ctrl, Shift, Alt (Alt Gr) | `e4`–`e6` |
| Logo | `logo` |

Fn has no controllable LED.

## Restoring colors after a replug

The keyboard keeps software colors only while it has power. Unplugging it, rebooting,
or a suspend that cuts USB power sends it back to its onboard rainbow. To bring your
colors back automatically:

- Keep your colors in `~/.config/gpro-rgb/profile`. PC Dash writes this file on every
  change, or you can write it by hand.
- The udev rule starts `gpro-rgb-restore.service` in your user's systemd instance
  whenever the keyboard is plugged in. The unit waits 1 s for the keyboard to start,
  runs `gpro-rgb load` on the profile, and retries if the lighting interface isn't
  there yet. It also runs at login.
- Without a profile the unit does nothing. Delete the file to stop restoring.

If it doesn't work: `journalctl --user -u gpro-rgb-restore -n 20`.

## How it works

The G Pro has two USB interfaces. Interface 0 is the boot keyboard. Interface 1 has
the media keys and a vendor page (`0xFF43`) with two output reports for lighting:
`0x11` (20 bytes) and `0x12` (64 bytes). `gpro-rgb` opens every `/dev/hidraw*`, keeps
the one with the right vendor/product ID whose report descriptor has that vendor
page, and writes reports to it:

| Command | Bytes |
|---|---|
| Set keys | `12 ff 0c 3a 00 01 00 0e` + up to 14 × (`code r g b`), 64 bytes |
| Set logo | `11 ff 0c 3a 00 10 00 01` + `01 r g b`, 20 bytes |
| Commit | `11 ff 0c 5a`, padded to 20 bytes |

Nothing changes until the commit. The command layout comes from the
[g810-led](https://github.com/MatMoul/g810-led) project.

## Layout

```
src/gpro.c                       device lookup and the HID++ lighting reports
src/gpro.h                       public API (open, set keys, set all, commit)
src/main.c                       the gpro-rgb CLI and profile parser
udev/70-logitech-gpro.rules      user access + start the restore unit on plug-in
systemd/gpro-rgb-restore.service user unit that applies the profile
```

## Not supported yet

- Hardware effects (breathing, color cycle, waves) and saving to the keyboard's onboard memory
- The game mode and brightness indicator LEDs
- Other Logitech keyboards (the G610/G810/G512 family uses the same commands, but
  each needs its product ID and key list)
