// thebigs2 - ReXGlue Recompiled Project
//
// The app: paths and defaults (OnConfigure), the profile's graphics defaults (GetRequirements),
// the game's menus, hotkeys and theme (OnInit), and the first launch setup screen, which runs
// before the title launches when there are no game files yet (GameMain).

#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <string>
#include <thread>

#include <rex/app/title_app.h>
#include <rex/hid/input_system.h>
#include <rex/hid/pad.h>
#include <rex/input_keys.h>
#include <rex/kernel/title_paths.h>
#include <rex/kernel/title_runtime.h>
#include <rex/logging/log_category.h>
#include <rex/path_settings.h>
#include <rex/platform/file.h>
#include <rex/settings_registry.h>
#include <rex/ui/ui_event.h>
#include <rex/ui/ui_system.h>

#include "generated/default/thebigs2_init.h"

#include "app_services.h"
#include "first_launch.h"
#include "game_settings.h"
#include "main_menu_entry.h"
#include "online_menu.h"
#include "settings.h"
#include "settings_menu.h"
#include "ui_theme.h"
#if defined(TB2_RENDER_TRACE)
#include "render_trace.h"
#endif

void TB2_RenderDocCaptureKey();   // renderdoc_capture.cpp
void TB2_InitRenderDocCapture();  // renderdoc_capture.cpp

namespace {

namespace fs = std::filesystem;

// Sets a setting at the title layer: the config file, the environment and the command line
// override it. Works before the setting's module has registered it.
void SetDefault(std::string_view name, std::string_view text) {
  if (auto set = REX_SETTINGS().SetText(rex::SettingLayer::kTitle, name, text); !set) {
    REXLOG_WARN("Default {} = {} refused: {}", name, text, set.error().message);
  }
}

class Thebigs2App final : public rex::app::TitleApp, public rex::ui::IUIEventListener {
 public:
  Thebigs2App() : TitleApp("thebigs2", kPPCImageConfig) {}

  ~Thebigs2App() override {
    shutting_down_.store(true);
    if (extract_thread_.joinable()) extract_thread_.join();
  }

  // Before the launch: makes sure there are game files, showing the setup screen (which extracts
  // a disc image) when there are none. False when the player quit instead.
  bool PrepareGame() {
    rex::kernel::TitlePaths& paths = GetTitleRuntime().GetTitlePaths();
    if (!paths.game_root.Get().empty()) return true;
    if (tb2::IsExtractedGame(ExtractedDir())) {
      return SetGameRoot(ExtractedDir());
    }

    tb2::FirstLaunchSetup setup;
    setup.dest = ExtractedDir();
    setup.start_dir = tb2::LauncherFolder();
    if (const fs::path iso = tb2::IsoPathSetting(); !iso.empty()) {
      std::string error;
      if (tb2::CheckIso(iso, error)) {
        setup.suggested_iso = iso;
      } else {
        REXLOG_ERROR("{}", error);
        setup.message = "iso_path in the settings file: " + error;
      }
    }
    if (setup.suggested_iso.empty()) setup.suggested_iso = tb2::FindIsoNearLauncher();
    setup.extract = [this](const fs::path& iso) { StartExtract(iso); };
    setup.cancel = [this] { user_cancel_.store(true); };
    setup.quit = [] {
      REXLOG_INFO("Quit from the setup screen");
      rex::app::RequestShutdown(0);
    };

    if (!tb2::UI() || tb2::UI()->IsHeadless()) {  // no window to show the setup screen in
      if (setup.suggested_iso.empty()) {
        REXLOG_ERROR("No game files. Set iso_path in {} or start with --iso_path=/path/to/TheBigs2.iso",
                     config_path_.string());
        return false;
      }
      StartExtract(setup.suggested_iso);
    } else {
      tb2::ShowFirstLaunch(std::move(setup), &extract_status_);
    }
    while (!game_ready_.load() && RunFrame()) {
    }
    return game_ready_.load() && SetGameRoot(ExtractedDir());
  }

 protected:
  rex::Result<void> OnConfigure(rex::SettingsRegistry& settings) override {
#ifndef _WIN32
    // Online: a send() to a player who has left raises SIGPIPE, which would end the game;
    // ignored, send() returns EPIPE instead (as on Windows).
    std::signal(SIGPIPE, SIG_IGN);
#endif
    if (auto registered = tb2::RegisterGameSettings(settings); !registered) {
      return registered;
    }

    // Folders. Portable (an AppImage in a writable folder, or the Windows zip): everything next to
    // it. Otherwise the settings in ~/.config/thebigs2 (%APPDATA%\thebigs2, ~/Library/Application
    // Support/thebigs2 on macOS) unless a thebigs2.toml sits next to the executable, saves where
    // the SDK always kept them, logs next to the executable when it is writable.
    // The cache and the fallback log folder follow app.user_root ({app.user_root}), so moving
    // the saves with --user_data_root moves them too.
    fs::path user_root = tb2::DefaultUserRoot();
    fs::path cache_root = "{app.user_root}/cache";
    fs::path log_root = tb2::ExeDirWritable() ? rex::platform::GetExecutableFolder() / "logs"
                                              : tb2::DefaultLogRoot("{app.user_root}");
    if (tb2::PortableMode()) {
      const fs::path root = tb2::LauncherFolder();
      tb2::MigrateToPortable(user_root);
      user_root = root / "saves";
      cache_root = root / "cache";
      log_root = root / "logs";
    }
    config_path_ = tb2::ResolveConfigPath(rex::platform::GetExecutableFolder() / "thebigs2.toml");
    tb2::EnsureConfigFile(config_path_);
    // The game reads thebigs2.toml itself (old flat names, comments kept by the settings menu).
    SetDefault("app.config_file", "");
    tb2::ApplyConfigFile(config_path_);
    SetDefault("app.user_root", user_root.string());
    SetDefault("app.cache_root", cache_root.string());
    SetDefault("app.log_root", log_root.string());

    // Game files: a game/ folder (with default.xex) next to the executable or the AppImage, or in
    // the working directory. Otherwise the extracted disc image (PrepareGame).
    std::error_code ec;
    for (const fs::path& candidate :
         {rex::platform::GetExecutableFolder() / "game", tb2::LauncherFolder() / "game",
          fs::current_path(ec) / "game"}) {
      if (fs::is_regular_file(candidate / "default.xex", ec)) {
        SetDefault("title.game_root", candidate.string());
        break;
      }
    }

    // Present with plain FIFO (vsync). Immediate or mailbox redraw the latest guest frame far
    // more often than the 60 Hz guest vblank; frames then land unevenly and the game hitches.
    SetDefault("gpu.vulkan.allow_present_mode_immediate", "false");
    SetDefault("gpu.vulkan.allow_present_mode_mailbox", "false");
    SetDefault("gpu.vulkan.allow_present_mode_fifo_relaxed", "false");
    // The game's own settings menu takes F4 (tb2.bind_settings_menu), not the SDK's overlay,
    // whose "Save to config" would rewrite thebigs2.toml from scratch.
    SetDefault("ui.bind.settings", "");
    // Vulkan on Windows too, where the SDK picks D3D12: the settings and several SDK fixes
    // (the resolve shader) are Vulkan only.
    SetDefault("gpu.driver", "vulkan");

    if (tb2::SteamDeckProfile()) {
      // Steam Deck AppImage: host render targets at 1x (the game's 1280x720 fills the 1280x800
      // screen's width 1:1), the game's own texture filtering, and no keyboard: Steam Input can
      // send keys from the trackpads and back buttons, which would also drive player 1.
      SetDefault("gpu.vulkan.render_target_path", "host");
      SetDefault("gpu.anisotropic_override", "-1");
      SetDefault("hid.mnk_mode", "false");
    } else if (tb2::MacProfile()) {
      // Apple GPUs through MoltenVK: host render targets at 1x until FSI is known to work there.
      SetDefault("gpu.vulkan.render_target_path", "host");
      SetDefault("hid.mnk_mode", "true");
    } else {
      // Render at 2x (clean edges at 1440p) through the fragment shader interlock EDRAM path; the
      // host render target path at 2x breaks the game's MSAA targets (green striped bat, cap and
      // shoes). The keyboard plays as player 1 (hid.keybind_*), merged with the first pad.
      SetDefault("gpu.vulkan.render_target_path", "fsi");
      SetDefault("hid.mnk_mode", "true");
    }
    return TitleApp::OnConfigure(settings);
  }

  // Runs after the config file and the command line are read and before the drivers start: the
  // last point where the profile's graphics defaults can follow the player's settings.
  rex::AppSysRequirements GetRequirements() const override {
    tb2::GameSettings& game = tb2::Settings();
    int scale = game.resolution_scale.Get();
    if (scale <= 0) scale = tb2::SteamDeckProfile() || tb2::MacProfile() ? 1 : 2;
    SetDefault("gpu.resolution_scale_x", std::to_string(scale));
    SetDefault("gpu.resolution_scale_y", std::to_string(scale));
    // Main scene 4x MSAA (render_tweaks.cpp). Without it the game also drops its 3-tile
    // rendering: about half the GPU command thread's work and ~20% of the GPU's. At 2x and up the
    // resolution scaling already smooths the edges, and the Deck needs the headroom.
    if (game.scene_msaa.GetLayer() == rex::SettingLayer::kDefault &&
        (tb2::SteamDeckProfile() || scale >= 2)) {
      SetDefault("tb2.scene_msaa", "false");
    }
    if (tb2::SteamDeckProfile()) REXLOG_INFO("Steam Deck profile (TB2_PROFILE=steamdeck)");
    if (tb2::MacProfile()) REXLOG_INFO("Mac profile");
    return TitleApp::GetRequirements();
  }

  rex::Result<void> OnInit() override {
    if (auto base = TitleApp::OnInit(); !base) return base;
    tb2::SetSystems(&GetUI(), &GetInput());

    // The game's look for every overlay, and the setup screen's fonts.
    tb2::AddFirstLaunchFonts(&GetUI().GetFonts());
    tb2::theme::AddFonts(&GetUI().GetFonts(), GetUI().GetImGuiIO());
    tb2::theme::ConfigureStyle(GetUI().GetImGuiStyle(), GetUI().GetStyle());

    // Hotkeys (tb2.bind_quit, tb2.bind_settings_menu, F9 RenderDoc capture) and the menus.
    event_listener_ = GetUI().AddEventListener(*this);
    tb2::InitMainMenuEntries([] { tb2::RequestQuit("main menu"); });
    tb2::InitSettingsMenu(config_path_);
    tb2::InitOnlineMenu(config_path_);
    TB2_InitRenderDocCapture();
#if defined(TB2_RENDER_TRACE)
    TB2TraceInit(GetTitleRuntime().GetTitlePaths().game_root.Get().parent_path() / "traces");
#endif

    // Display mode from the settings file (resolution / refresh rate).
    tb2::PostToUI([] { tb2::ApplyDisplaySettings(true); });
    tb2::InstallDisplayWatch([](std::function<void()> fn) { tb2::PostToUI(std::move(fn)); });

    StartPadPoller();
    return rex::Ok();
  }

  rex::Result<void> OnTitleLaunch(rex::kernel::TitleRuntime& title) override {
    tb2::SetTitle(&title);
    return TitleApp::OnTitleLaunch(title);
  }

  void OnShutdown() override {
    stop_poller_.store(true, std::memory_order_relaxed);
    if (pad_poller_.joinable()) pad_poller_.join();
    event_listener_ = rex::ScopeGuard();
    tb2::SetTitle(nullptr);
    tb2::SetSystems(nullptr, nullptr);
    TitleApp::OnShutdown();
  }

  // IUIEventListener: the game's hotkeys.
  void OnUIEvent(const rex::ui::UIEvent&) override {}

  void OnInputEvent(rex::InputEvent& event) override {
    if (event.type != rex::InputEvent::Type::kKeyDown || event.repeat) return;
    // A bind with modifiers ("Shift+F10") needs exactly those; a plain key ("F10") works with any
    // modifier held, as the old binds did.
    auto matches = [&event](const std::string& bind) {
      auto parsed = rex::ParseKeyChord(bind);
      return parsed && parsed->key != rex::VirtualKey::kNone && parsed->key == event.key &&
             (parsed->modifiers == rex::KeyModifiers::kNone || parsed->modifiers == event.modifiers);
    };
    if (matches(tb2::Settings().bind_quit.Get())) {
      event.handled = true;
      tb2::RequestQuit("keyboard");
    } else if (matches(tb2::Settings().bind_settings_menu.Get())) {
      event.handled = true;
      tb2::ToggleSettingsMenu();
    } else if (event.key == rex::VirtualKey::kF9) {
      TB2_RenderDocCaptureKey();
    }
  }

 private:
  bool SetGameRoot(const fs::path& root) {
    if (auto set = GetTitleRuntime().GetTitlePaths().game_root.Set(root); !set) {
      REXLOG_ERROR("Could not use the game files in {}: {}", root.string(), set.error().message);
      return false;
    }
    return true;
  }

  // The extracted disc: game/ next to a portable AppImage or zip, else in the save folder
  // (app.user_root, after the command line and the config file applied).
  fs::path ExtractedDir() const {
    return tb2::PortableMode() ? tb2::LauncherFolder() / "game" : REX_PATHS().user_root.Get() / "game";
  }

  // Extraction runs on a worker thread; the setup screen shows its progress.
  void StartExtract(const fs::path& iso) {
    if (extract_status_.running.load()) return;
    if (extract_thread_.joinable()) extract_thread_.join();
    tb2::SetIsoPathSetting(iso.string());
    tb2::SaveConfigString(config_path_, "iso_path", iso.string());
    user_cancel_.store(false);
    extract_status_.percent.store(0);
    extract_status_.running.store(true);

    extract_thread_ = std::thread([this, iso] {
      std::string error;
      const bool ok = tb2::ExtractIso(iso, ExtractedDir(), [this](int pct) {
        extract_status_.percent.store(pct);
        return !user_cancel_.load() && !shutting_down_.load();
      }, error);
      if (shutting_down_.load()) return;
      if (user_cancel_.load()) error = "Extraction cancelled.";
      tb2::PostToUI([this, ok, error] { OnExtractDone(ok, error); });
    });
  }

  void OnExtractDone(bool ok, const std::string& error) {
    if (extract_thread_.joinable()) extract_thread_.join();
    {
      std::lock_guard lock(extract_status_.mutex);
      extract_status_.error = ok || user_cancel_.load()
                                  ? (ok ? "" : error)
                                  : "Extracting the disc image failed: " + error;
    }
    if (!ok) {
      REXLOG_ERROR("Extraction failed: {}", error);
      extract_status_.running.store(false);
      if (!tb2::UI() || tb2::UI()->IsHeadless()) rex::app::RequestShutdown(1);
      return;
    }
    tb2::CloseFirstLaunch();
    extract_status_.running.store(false);
    game_ready_.store(true);
  }

  // The controller's hotkeys: hold Back + Start for 2 seconds to quit, Back + Y for a second for
  // the settings menu.
  void StartPadPoller() {
    pad_poller_ = std::thread([this] {
      using namespace std::chrono;
      constexpr rex::u16 kQuit = rex::hid::kPadBack | rex::hid::kPadStart;
      constexpr rex::u16 kMenu = rex::hid::kPadBack | rex::hid::kPadY;
      steady_clock::time_point held_since{}, menu_since{};
      bool holding = false, menu_holding = false, menu_toggled = false;
      while (!stop_poller_.load(std::memory_order_relaxed)) {
        bool pressed = false, menu_pressed = false;
        if (rex::hid::IInputSystem* input = tb2::Input()) {
          for (rex::u32 user = 0; user < 4; ++user) {
            auto reading = input->GetState(user);
            if (!reading) continue;
            const rex::u16 buttons = reading->state.buttons;
            pressed |= (buttons & kQuit) == kQuit;
            menu_pressed |= (buttons & kMenu) == kMenu;
          }
        }
        const auto now = steady_clock::now();
        if (!menu_pressed) {
          menu_holding = menu_toggled = false;
        } else if (!menu_holding) {
          menu_holding = true;
          menu_since = now;
        } else if (!menu_toggled && now - menu_since >= seconds(1)) {
          menu_toggled = true;  // once per hold
          tb2::ToggleSettingsMenu();
        }
        if (pressed && !holding) {
          holding = true;
          held_since = now;
        } else if (!pressed) {
          holding = false;
        } else if (now - held_since >= seconds(2)) {
          tb2::RequestQuit("controller (Back+Start)");
          return;
        }
        std::this_thread::sleep_for(milliseconds(50));
      }
    });
  }

  fs::path config_path_;
  std::thread extract_thread_;
  tb2::ExtractStatus extract_status_;
  std::atomic<bool> user_cancel_{false};
  std::atomic<bool> shutting_down_{false};
  std::atomic<bool> game_ready_{false};
  rex::ScopeGuard event_listener_;
  std::thread pad_poller_;
  std::atomic<bool> stop_poller_{false};
};

int GameMain(Thebigs2App& app, const rex::CLIArgList& args) {
  if (app.Init(args) && app.PrepareGame() && app.LaunchTitle()) {
    app.Run();
  }
  return app.Shutdown();
}

}  // namespace

REX_TITLE_APP_MAIN(Thebigs2App, GameMain)
