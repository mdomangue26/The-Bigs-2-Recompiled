// thebigs2 - the game's own settings (scope "tb2") and the old flat setting names.
//
// thebigs2.toml, the settings menu and command lines use flat names such as frame_rate or
// resolution_scale. The SDK's settings are scoped (gpu.resolution_scale_x, hid.mnk_mode); the
// game's live here as tb2.*. RegisterGameSettings maps every flat name the game uses to its
// setting, so old files and command lines keep working.
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include <rex/setting.h>
#include <rex/settings_registry.h>
#include <rex/types.h>

namespace tb2 {

class GameSettings final : public rex::SettingOwner {
 public:
  GameSettings() : SettingOwner("tb2") {}

  // Game disc.
  REX_SETTING(std::string, iso_path, "",
              "Disc image (.iso) to extract the game from on first launch; preselected on the "
              "setup screen");

  // Display.
  REX_SETTING(std::string, display_resolution, "desktop",
              "Output resolution: 'desktop' or WIDTHxHEIGHT (e.g. 1920x1080). Window size when "
              "windowed, display mode in exclusive fullscreen");
  REX_SETTING(rex::f64, display_refresh_rate, 0.0,
              "Monitor refresh rate in Hz for exclusive fullscreen; 0 = desktop rate",
              rex::Range{0.0, 1000.0});

  // Graphics. resolution_scale sets gpu.resolution_scale_x and _y; 0 keeps the profile's.
  REX_SETTING(rex::i32, resolution_scale, 0,
              "Internal render scale 1-8 (gpu.resolution_scale_x/_y); 0 = the profile's default",
              rex::Range{0, 8});
  REX_SETTING(bool, scene_msaa, true,
              "Main scene 4x MSAA as on the Xbox 360. false = no MSAA, which also removes the "
              "3-tile rendering the console needed for it (much less CPU and GPU work; edges are "
              "less smooth at 1x)");
  REX_SETTING(rex::i32, frame_rate, 30,
              "Frame rate in matches: 30 (the game's own cap) or 60 (experimental)",
              rex::Range{1, 240});
  REX_SETTING(bool, fps_60, false, "Old name for frame_rate = 60");

  // Controls.
  REX_SETTING(std::string, bind_quit, "F10", "Key that quits the game");
  REX_SETTING(std::string, bind_settings_menu, "F4", "Key that opens the settings menu");

  // Online.
  REX_SETTING(std::string, online_name, "", "Gamertag shown to the other player online");
  REX_SETTING(std::string, online_host, "", "The host's address last joined online");

  // Debug.
  REX_SETTING(rex::f64, fps_log_interval, 0.0, "Log the game's frame rate every N seconds; 0 = off",
              rex::Range{0.0, 3600.0});
  REX_SETTING(rex::i32, gpu_wait_sleep_us, 100,
              "Sleep between polls while the game waits for the GPU (us); 0 = spin",
              rex::Range{0, 100000});
  REX_SETTING(rex::i32, trace_frames, 1, "Render trace build: frames per trace", rex::Range{1, 1000});
  REX_SETTING(rex::f64, trace_max_seconds, 5.0, "Render trace build: longest trace in seconds",
              rex::Range{0.0, 3600.0});
  REX_SETTING(bool, trace_at_start, false, "Render trace build: trace from the first frame");
  REX_SETTING(std::string, trace_frame_marker, "823BA340",
              "Render trace build: the function that starts a frame");
};

// The game's settings. Valid for the life of the program.
GameSettings& Settings();

// Registers the game's settings and the old flat names. Call from App::OnConfigure, before the
// config file and the command line are read.
[[nodiscard]] rex::Result<void> RegisterGameSettings(rex::SettingsRegistry& settings);

// Applies thebigs2.toml at the config file layer, each value under its full name (so the SDK
// never sees the flat names, which it would warn about one by one). The command line still wins.
// Call from App::OnConfigure, with app.config_file emptied so the SDK doesn't read it again.
void ApplyConfigFile(const std::filesystem::path& config_path);

// Sets a setting by its flat or full name, as the settings menu does, at the runtime layer.
// Logs and returns false when the setting refuses the value.
bool ApplySettingText(std::string_view name, std::string_view text);

// Reads a setting by its flat or full name as text, empty when it does not exist.
std::string SettingText(std::string_view name);

}  // namespace tb2
