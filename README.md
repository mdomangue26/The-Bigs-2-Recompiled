# The Bigs 2 — PC port (static recompilation)

A native Linux and Windows port of *The Bigs 2* (Xbox 360, 2K Sports, 2009),
made by recompiling the original Xbox 360 executable to C++ with the
[ReXGlue SDK](https://github.com/rexglue/rexglue-sdk).

**No game files are included.** You need your own copy of the disc (an `.iso`
dump). Ready-made builds are on the
**[Releases](https://github.com/mdomangue26/thebigs2/releases)** page; to build
it yourself, see **[Building](docs/BUILDING.md)**.

## Features

- **Runs natively** on Linux (desktop and Steam Deck AppImages) and Windows.
- **Sharper picture**: renders at 2x (1440p) by default, with 1x and 3x (4K)
  available.
- **Experimental 60 fps** in matches (the original runs matches at 30).
- **Steam Deck build** with Deck-tuned defaults; works in Game Mode.
- **Setup screen** on first launch: pick your disc image and it's extracted
  once. Works with mouse, keyboard, controller and touchscreen.
- **Settings menu in the game's own style**: open it from **RECOMP SETTINGS**
  on the main menu, with **F4**, or by holding **Back + Y** on a controller.
- **Online play** against a friend, over the internet or a home network:
  **ONLINE** on the main menu lets you host a game or join one by IP
  address (see [Online play](#online-play)).
- **Main menu additions**: RECOMP SETTINGS and EXIT GAME at the bottom.
- **Controller button remapping** and **keyboard controls**, both editable in
  the settings menu.
- **Portable**: settings, saves, logs and the extracted game live next to the
  AppImage or the Windows `.exe`.
- Saving and loading work.

## Getting started

1. Download the latest release from the
   **[Releases](https://github.com/mdomangue26/thebigs2/releases/latest)** page:
   - Linux: `TheBigs2-x86_64.AppImage` (make it executable:
     `chmod +x TheBigs2-x86_64.AppImage`, or tick *Allow executing* in its
     properties)
   - Steam Deck: `TheBigs2-SteamDeck-x86_64.AppImage`
   - Windows: `TheBigs2-windows-x64.zip` (unzip it)

   Or build it yourself (see **[Building](docs/BUILDING.md)**).
2. Put it in a folder of its own, ideally with your `.iso` next to it.
3. Run it. The setup screen asks for the disc image (an `.iso` in the same
   folder is preselected), extracts it once, and the game starts.

Everything the game writes stays in that folder:

```
TheBigs2-x86_64.AppImage   (or thebigs2.exe)
game/      the extracted game files
config/    thebigs2.toml (settings)
saves/     profile, save games, achievements
cache/     shader cache
logs/
```

Move or back up the folder as a whole. If the folder can't be written to, the
game uses `~/.config/thebigs2` and `~/.local/share/thebigs2` (Linux) or
`%APPDATA%\thebigs2` (Windows) instead.

**Requirements:** a GPU with Vulkan support and a current driver. Linux needs
glibc 2.38 or newer (Ubuntu 24.04, Fedora 39, current Arch or SteamOS).
Windows needs 64-bit Windows 10 or 11.

## Key commands

| Keyboard | Controller | Does |
|---|---|---|
| F4 | hold Back + Y (1 second) | Settings menu |
| F10 | hold Back + Start (2 seconds) | Quit the game |
| F3 | | Overlay with the frame rate |
| F7 / Backtick | | Achievements / console |

You can also quit with **EXIT GAME** on the main menu.

**In the settings menu:** LB / RB (Page Up / Page Down) switch tabs, up and
down pick a setting, left and right change it. Start (Ctrl+S) saves, Y saves
and quits, B (Escape) closes. Some settings apply at once, the others after a
restart; the menu marks which.

### Playing with the keyboard

The keyboard works as player 1's controller, alongside a gamepad:

| Controller | Keys |
|---|---|
| A / B / X / Y | `;` or Space / `'` or Backspace / L / P |
| LB / RB | 1 / 3 |
| LT / RT | Q or I / E or O |
| Back / Start | Z or Tab / X or Return |
| Left stick (click) | W A S D (F) |
| Right stick (click) | arrow keys (K) |
| D-pad | Shift + arrow keys |

Change the keys on the settings menu's **Keyboard** tab, and remap controller
buttons on its **Controller** tab (e.g. swap A and B). Button remapping takes
effect after a restart.

## Online play

Two players, each with their own copy of the game, play a full match against
each other. Xbox Live is long gone; the port stands in for it, and the game's
own peer-to-peer code does the rest. The game's Xbox Live menus and messages
say "Online" instead.

1. Both players: **ONLINE** on the main menu. Set your **player name** (up
   to 8 letters or digits; the two names must differ).
2. One player picks **HOST A GAME**. The page shows this computer's
   addresses: give one to your friend. Then **START HOSTING**, and in the
   game's menus **QUICK MATCH**, **SEARCH**, then **CREATE GAME** (X).
3. The other picks **JOIN A GAME**, enters the host's address (keypad or
   keyboard) and **CONNECT**s, then **QUICK MATCH** and **SEARCH**: the
   host's Game Room comes up.
4. Both press **A (Ready)**, then pick sides, teams and lineups as usual.

Which address to use:

| Setup | Address | Router |
|---|---|---|
| Same home network | The host's home address (e.g. `192.168.1.20`) | Nothing to do |
| [Tailscale](https://tailscale.com) (or another VPN) on both computers, **recommended** over the internet | The host's Tailscale address (`100.x.y.z`) or machine name | Nothing to do |
| Over the internet without a VPN | The host's public IP | The host forwards **TCP 51000** and **UDP 1795** to their computer |

Good to know:

- Online matches always run at 30 fps (the game's own rate), whatever
  `frame_rate` says.
- Once you've opened ONLINE, the game counts as signed in to Xbox Live
  until you quit; offline modes still work as before.
- Both players should run the same release. Linux against Linux has been
  tested; Linux against Windows not yet.
- Leaving works like on the console: the other player gets "Opponent has
  exited match" and can host or join again without restarting.
- Leaderboards, Lobby, roster downloads and gamer cards were Xbox Live
  services and aren't supported.

Online play was worked out by [MatRanc](https://github.com/MatRanc) in the
[online fork](https://github.com/MatRanc/The-Bigs-2-Recompiled) (with its
SDK changes); this port brings it in. Details for developers:
[docs/ONLINE.md](docs/ONLINE.md).

## Settings

The settings menu edits `config/thebigs2.toml`. You can also edit the file by
hand: every setting is listed in it with an explanation. The main ones:

| Setting | Default | |
|---|---|---|
| `fullscreen` | `true` | `false` for a window |
| `display_resolution` | `"desktop"` | Window size or fullscreen mode, e.g. `"1920x1080"` |
| `resolution_scale` | `2` | Render scale: `1` = 720p, `2` = 1440p, `3` = 4K |
| `frame_rate` | `30` | `60` for 60 fps in matches (**experimental**; use with `resolution_scale = 1`) |
| `present_effect` | `"bilinear"` | Upscaling: `bilinear`, `cas` or `fsr` |
| `present_letterbox` | `true` | Keep 16:9 with black bars |
| `mnk_mode` | `true` | Keyboard plays as player 1 |
| `online_name` | your user name | Online player name (the ONLINE screen saves it) |
| `online_host` | | The host's address you last joined |

Delete the file to get a fresh one with the defaults. For one run, any setting
can also be given on the command line, as `--name=value`.

## Steam Deck

Use the Steam Deck AppImage. It's the same game with defaults suited to the
Deck: 1x resolution (fills the screen), lighter settings, and the keyboard
turned off as a controller (Steam Input sends key presses).

1. In Desktop Mode, make a folder (e.g. `~/Games/TheBigs2`) and put the
   AppImage in it, ideally with your `.iso`.
2. *Add a Non-Steam Game to My Library*, pick the AppImage, switch to Game
   Mode and play. The setup screen works with the Deck's controls and
   touchscreen and can browse to an SD card.
3. Quit with **View + Menu held 2 seconds**, or EXIT GAME on the main menu.
   **View + Y held a second** opens the settings menu.
4. On the Deck OLED, set the refresh rate to **60 Hz** in the Quick Access
   performance menu, or motion judders.

It runs at 30 fps. With `frame_rate = 60` it holds 60 in gameplay and slows
down in transitions. The first minutes can stutter while shaders compile.
If it closes right away, run it from Konsole in Desktop Mode to see the error.

## Good to know

- **Windows** builds are cross-compiled on Linux and have only been tested
  under Wine so far.
- For smooth motion, use a 60 Hz or 120 Hz display mode, or VRR / G-Sync.
- After a crash or a black screen, check `logs/`. An old settings file from
  an earlier build can cause trouble; deleting it resets everything.

## License

The port's own code is public domain ([Unlicense](LICENSE)). The game itself,
the fonts and the SDK keep their own terms; see [LICENSE](LICENSE).
