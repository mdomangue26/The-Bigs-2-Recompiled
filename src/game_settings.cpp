// thebigs2 - the game's own settings and the old flat setting names (see game_settings.h).

#include "game_settings.h"

#include <utility>

#include <rex/logging/log_category.h>

#include "settings.h"

namespace tb2 {

namespace {

// Flat names of thebigs2.toml, the settings menu and older command lines, and their settings.
// The SDK already maps some flat names itself (keybind_*, mnk_mode, fullscreen, monitor,
// padbind_*, fake_xbox_live, ...); those are listed too, for SettingText and ApplySettingText.
// An empty target marks a setting this version no longer has.
constexpr std::pair<std::string_view, std::string_view> kFlatNames[] = {
    // The game's own.
    {"iso_path", "tb2.iso_path"},
    {"display_resolution", "tb2.display_resolution"},
    {"display_refresh_rate", "tb2.display_refresh_rate"},
    {"resolution_scale", "tb2.resolution_scale"},
    {"scene_msaa", "tb2.scene_msaa"},
    {"frame_rate", "tb2.frame_rate"},
    {"fps_60", "tb2.fps_60"},
    {"bind_quit", "tb2.bind_quit"},
    {"bind_settings_menu", "tb2.bind_settings_menu"},
    {"online_name", "tb2.online_name"},
    {"online_host", "tb2.online_host"},
    {"fps_log_interval", "tb2.fps_log_interval"},
    {"gpu_wait_sleep_us", "tb2.gpu_wait_sleep_us"},
    {"trace_frames", "tb2.trace_frames"},
    {"trace_max_seconds", "tb2.trace_max_seconds"},
    {"trace_at_start", "tb2.trace_at_start"},
    {"trace_frame_marker", "tb2.trace_frame_marker"},
    // The SDK's, under their old names.
    {"gpu_backend", "gpu.driver"},
    {"anisotropic_override", "gpu.anisotropic_override"},
    {"render_target_path_vulkan", "gpu.vulkan.render_target_path"},
    {"render_target_path_d3d12", "gpu.d3d12.render_target_path"},
    {"vulkan_allow_present_mode_immediate", "gpu.vulkan.allow_present_mode_immediate"},
    {"vulkan_allow_present_mode_mailbox", "gpu.vulkan.allow_present_mode_mailbox"},
    {"vulkan_allow_present_mode_fifo_relaxed", "gpu.vulkan.allow_present_mode_fifo_relaxed"},
    {"present_letterbox", "gpu.xenos.present_letterbox"},
    {"vsync", "gpu.vsync"},
    {"user_data_root", "app.user_root"},
    {"cache_root", "app.cache_root"},
    {"fullscreen", "ui.fullscreen"},
    {"fullscreen_exclusive", "ui.fullscreen_exclusive"},
    {"monitor", "ui.monitor"},
    {"mnk_mode", "hid.mnk_mode"},
    {"fake_xbox_live", "kernel.fake_xbox_live"},
    {"user_name", "kernel.user_name"},
    // Gone: the CAS and FSR present effects (the SDK presents bilinear only).
    {"present_effect", ""},
};

// The SDK's own aliases cover these prefixes (hid.keybind_*, hid.sdl.padbind_*).
std::string ResolveName(std::string_view name) {
  if (name.find('.') != std::string_view::npos) {
    return std::string(name);
  }
  for (const auto& [flat, full] : kFlatNames) {
    if (flat == name) {
      return std::string(full);
    }
  }
  if (name.starts_with("keybind_")) {
    return "hid." + std::string(name);
  }
  if (name.starts_with("padbind_")) {
    return "hid.sdl." + std::string(name);
  }
  return std::string(name);
}

}  // namespace

GameSettings& Settings() {
  static GameSettings settings;
  return settings;
}

rex::Result<void> RegisterGameSettings(rex::SettingsRegistry& settings) {
  if (rex::Result<void> registered = settings.Register(Settings()); !registered) {
    return registered;
  }
  for (const auto& [flat, full] : kFlatNames) {
    if (!settings.IsAlias(flat)) {
      settings.AddAlias(flat, full);
    }
  }
  return {};
}

void ApplyConfigFile(const std::filesystem::path& config_path) {
  for (const auto& [key, literal] : ReadConfigValues(config_path)) {
    const std::string full = ResolveName(key);
    if (full.empty()) {
      continue;  // a setting this version no longer has
    }
    // TOML basic string -> its text; numbers and booleans as they are.
    std::string text = literal;
    if (text.size() >= 2 && text.front() == '"' && text.back() == '"') {
      text.clear();
      for (size_t i = 1; i + 1 < literal.size(); ++i) {
        if (literal[i] == '\\' && i + 2 < literal.size()) ++i;
        text += literal[i];
      }
    }
    if (auto set = REX_SETTINGS().SetText(rex::SettingLayer::kFile, full, text); !set) {
      REXLOG_WARN("{}: {} = {} refused: {}", config_path.string(), key, literal,
                  set.error().message);
    }
  }
}

bool ApplySettingText(std::string_view name, std::string_view text) {
  const std::string full = ResolveName(name);
  if (full.empty()) {
    return true;  // a setting this version no longer has
  }
  auto found = REX_SETTINGS().Find(full);
  if (!found) {
    REXLOG_WARN("Setting {} ({}) does not exist", name, full);
    return false;
  }
  if (auto set = (*found)->SetText(rex::SettingLayer::kRuntime, text); !set) {
    REXLOG_WARN("Setting {} = {} refused: {}", full, text, set.error().message);
    return false;
  }
  return true;
}

std::string SettingText(std::string_view name) {
  const std::string full = ResolveName(name);
  if (full.empty()) {
    return {};
  }
  auto found = REX_SETTINGS().Find(full);
  return found ? (*found)->GetText() : std::string();
}

}  // namespace tb2
