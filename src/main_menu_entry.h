// thebigs2 - the game's main menu with RECOMP SETTINGS (opens the settings
// menu) and EXIT GAME at the bottom; ONLINE (the game's XBOX LIVE) first asks Host or Join
// (online_menu.h).
// The game data stays as it is: the menu's screen file is changed in memory
// as the game loads it.
#pragma once

#include <functional>

namespace tb2 {

// quit: the clean quit (the same as F10).
void InitMainMenuEntries(std::function<void()> quit);

}  // namespace tb2
