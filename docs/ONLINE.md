# Online play: how it works

Players only need ONLINE on the main menu (see the README). This is the
developer side. **Online play is MatRanc's work**
([github.com/MatRanc](https://github.com/MatRanc)): the reverse engineering,
the hooks and the SDK changes below are theirs, from
[The-Bigs-2-Recompiled, branch `online`](https://github.com/MatRanc/The-Bigs-2-Recompiled/tree/online)
(its `docs/ONLINE.md` has the full history, and its `src/online_debug.cpp`
the tracing probes used to find all this) and
[rexglue-sdk, branch `tb2-online`](https://github.com/MatRanc/rexglue-sdk/tree/tb2-online).
This port keeps its hooks and SDK changes but replaces the launcher and its
environment variables with an in-game prompt. Addresses are guest addresses
in the retail `default.xex` (`sub_XXXXXXXX` in `generated/`).

## The short version

*The Bigs 2* has no dedicated server. Two consoles found each other through
Xbox Live matchmaking and then played **deterministic lockstep**: each side
sends only its controller input every frame, both run the same simulation
from the same input and random seed. Team picks, menus and the match all ride
on that input stream.

Xbox Live is gone, so the port stands in for the parts the game waits for:

| What the game needs from Live | Stand-in |
|---|---|
| A profile signed in to Live | SDK cvar `fake_xbox_live` (sign-in state 2, privileges, LAN address as the title address), name from `user_name` |
| A Live account in the game's own Live layer | `online.cpp`: fills the pending sign-in object and raises event 0x3C, so the game builds the account itself |
| Quick Match search results | The joiner gets one result: the host's address. The host gets "No Game Rooms found" and creates a room (X) |
| Per-player stats (names in the Game Room) | SDK `XamUserCreateStatsEnumerator` answers offline |
| The roster download server | Skipped: the built-in roster is used |

Everything else is the game's own peer-to-peer code.

## From the main menu to a match

1. **ONLINE** (`main_menu_entry.cpp`, the game's XBOX LIVE entry): the button's `ACT:OnlineMenu` is
   patched to `ACT:RecompOnline`. The title screen's command handler
   (`sub_82326E48`) opens the prompt (`online_menu.cpp`) and **waits** on the
   game thread until it closes (ImGui keeps drawing without new game frames).
   Closed: nothing happens. Host or Join: `online::Start` sets the role,
   name and peer, turns on `fake_xbox_live` / `user_name` and broadcasts the
   Live connection and sign-in notifications.
2. **Sign-in before the menu** (`online::SignIn`): still inside the handler,
   the game's sign-in processor (`sub_823F5F88`) is run until our account
   exists. The game's online menu must find the player signed in: otherwise
   it starts a Live sign-in of its own ("Signing In...") that never ends.
   The offline boot leaves the Live user index (`825D4BF8`) at 0; with no
   session that makes `sub_823F12B0` read guest address 0, so it is set back
   to 4 ("no user") until a session exists.
3. The same command then goes on to the game's handler as `OnlineMenu`.
   (Replaying the command later from another call site crashed: it points
   into the original call's stack.)
4. **Search / host** (`sub_823F82D8`), **Game Room** over TCP, **Ready**,
   then the lockstep start (`sub_822DC1D8` -> GameConn over UDP), Team Setup
   and the match are the game's own code, with the fixes below.

From then on the run stays "online": the stand-in stays signed in and online
matches keep the game's 30 fps (`frame_stats.cpp`), even after leaving the
ONLINE menu. Every hook in `online.cpp` passes straight through until a
role has been picked.

## "Online" instead of "Xbox LIVE"

The game's strings (`globstr.bcs`, `platform.bcs`) all go through the string
lookup `sub_824A35D0`; `main_menu_entry.cpp` returns a renamed copy of any
that names Xbox Live (whole phrases first, e.g. "connected to Xbox LIVE" ->
"connected online", then "XBOX LIVE" -> "ONLINE"), made once per string. The
main menu entry's glow (`m_xboxlive.bct`) is a picture of the words, so it is
hidden.

## Network

| Port | Protocol | Used for |
|---|---|---|
| 51000 | TCP | Game Room (join, participant table, settings, Ready); the host listens. The game's own port is 1000, which needs root on Linux |
| 1795 | UDP | GameConn, the lockstep input link, both ways |
| 1796 | UDP | GameConn of the second instance when both players have the same IP (one PC) |

The host connects GameConn to the address the joiner's TCP connection came
from, so NAT-free tunnels like Tailscale work. On one PC each side would
connect to its own port; `sub_823CDD10` (the connect wrapper) swaps 1795 and
1796 when the target is this machine.

## The other fixes in online.cpp

- The roster check (`sub_822DD5A0`) says "up to date"; a roster download
  start (`sub_823F08F8`) queues an empty response (event 0x35).
- Our account survives the "Live disconnected" sign-out (`sub_823F5DA0`); the
  "You must be connected to Xbox LIVE" handler (`sub_822A5060`) is skipped
  while no session exists.
- Session health check (`sub_823F9960`) and the per-player stats request
  (`sub_823EE498`) run with Live user 0; the stats manager is pumped from the
  online manager's update (`sub_82289560`), which also sets the Live user
  index to 0 while a session exists and back to 4 after.
- Joiner: session `+0x58` is armed directly once its participant table is
  sent (`sub_82420CB8`); the real opcode-5 path joins a Live XSession and
  crashes on the SDK's stand-in. An empty type-10 packet skips the
  participant-table parse (`sub_823F7AA0` / `sub_823F3878`).
- A session constructed while another is still registered releases the
  orphan's lock (`sub_823F43B0`), else the host sat on "Exiting match".
- Leaving: the end of the leave handling queues the game's return-to-menu
  (`sub_824B0128` -> `sub_82289278`).
- Game Room buttons that need Live player data (view player, gamer card,
  player list, gamer pictures) do nothing instead of crashing.
- `SIGPIPE` is ignored (a send to a player who left would end the game).
- Lockstep needs bit-identical floats: the game is built with
  `-ffp-contract=off` (matters where the CPU has FMA, e.g. arm64).

## Names

The Game Room draws the account XUID's 8 bytes as the player's name (a real
Live XUID starts with a zero byte and would draw nothing), so the account's
XUID *is* the name: up to 8 characters, and two players need names that
differ there. The SDK profile's own XUID stays fixed, since saves are stored
per XUID.

## Testing on one PC

Two instances in two headless gamescopes work (each with its own
`--user_data_root` and `XDG_CONFIG_HOME`, and different `--online_name`s).
The joiner enters this machine's LAN address. xdotool on each gamescope's
X display drives them: on the main menu `shift+Up` x5 reaches ONLINE;
Space selects in the prompt; on the Join page digits and `.` type the
address and Enter connects. Then Space x3 (Quick Match, Search), X (`l`) on
the host to create the room, Space on both for Ready.

The SDK's `net_log` (network, sign-in and notification calls) and
`xam_message_log` (XAM app messages) settings help, as `--net_log=true`.

## Open items

- Linux against Windows (MSVC CRT math vs glibc) not tested for desync yet;
  Linux against Linux and (in the fork) Linux against an Apple Silicon Mac
  worked.
- The desync path (`8237A950` ends the session) hasn't been seen.
