// thebigs2 - the prompt ONLINE on the main menu opens: host a game or join
// one (the host's IP address), and the player name. Picking one turns on the
// Xbox Live stand-in (online.h) and goes on to the game's own online menu.
// Drawn like the settings menu; the game sees a neutral controller meanwhile.

#pragma once

#include <filesystem>

namespace tb2 {

// The prompt saves the name and the host's address to config_path.
void InitOnlineMenu(std::filesystem::path config_path);

// Shows the prompt (main menu, ONLINE) and waits until it closes: true
// once a role is picked (online::Start done), false when it was closed or the
// game is quitting. Call from a game thread, never the UI thread.
bool AskOnlineRole();

}  // namespace tb2
