// thebigs2 - the prompt ONLINE on the main menu opens: host a game or join
// one (the host's IP address), and the player name. Picking one turns on the
// Xbox Live stand-in (online.h) and goes on to the game's own online menu.
// Drawn like the settings menu; the game sees a neutral controller meanwhile.

#pragma once

#include <filesystem>
#include <functional>

namespace rex::input {
class InputSystem;
}
namespace rex::ui {
class ImGuiDrawer;
}

namespace tb2 {

// post_to_ui runs a callback on the UI thread.
void InitOnlineMenu(rex::ui::ImGuiDrawer* drawer, rex::input::InputSystem* input,
                    std::filesystem::path config_path,
                    std::function<void(std::function<void()>)> post_to_ui);

// Shows the prompt (main menu, ONLINE) and waits until it closes: true
// once a role is picked (online::Start done). Call from a game thread, never
// the UI thread.
bool AskOnlineRole();

}  // namespace tb2
