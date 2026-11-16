// thebigs2 - what the game's modules reach of the app: the title, the UI and input systems, the UI
// thread and quitting. Set up by the app (main.cpp).
#pragma once

#include <cstdint>
#include <functional>
#include <string_view>

#include <imgui.h>

#include <rex/ui/ui_system.h>

namespace rex::hid {
class IInputSystem;
}
namespace rex::kernel {
class TitleRuntime;
}
namespace rex::ui {
class IUISystem;
}

namespace tb2 {

// The running title, null before the launch and after the unload.
rex::kernel::TitleRuntime* Title();
void SetTitle(rex::kernel::TitleRuntime* title);

// Guest heap allocations (the system heap the kernel uses), 0 / no-op without a title.
uint32_t GuestHeapAlloc(uint32_t size);
void GuestHeapFree(uint32_t address);

// The app's systems, null before the app has set them.
rex::ui::IUISystem* UI();
rex::hid::IInputSystem* Input();
void SetSystems(rex::ui::IUISystem* ui, rex::hid::IInputSystem* input);

// Runs fn on the UI thread later. Any thread.
void PostToUI(std::function<void()> fn);

// Quits cleanly (logs flushed), from any thread; `source` goes to the log.
void RequestQuit(std::string_view source);

// An ImGui window drawn every frame until it closes itself, the way the game's menus are
// written: draw in OnDraw, call Close() to finish. Show() hands it to the UI system, which
// deletes it once it is closed.
class Dialog : public rex::ui::DialogBase {
 public:
  ~Dialog() override = default;

  // ImGui's context is current only while dialogs draw (not in constructors or posted tasks), so
  // keyboard and gamepad navigation is turned on at the first Draw and the flags are restored in
  // the Draw that closes the dialog.
  bool Draw(ImGuiIO& io) final {
    if (!opened_) {
      opened_ = true;
      saved_config_flags_ = io.ConfigFlags;
      saved_backend_flags_ = io.BackendFlags;
      io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
      io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    }
    if (!closed_) {
      OnDraw(io);
    }
    if (closed_) {
      io.ConfigFlags = saved_config_flags_;
      io.BackendFlags = saved_backend_flags_;
    }
    return !closed_;
  }

  // The game's own windows hold the keyboard and mouse while open.
  bool WantsExclusiveInput() const override { return true; }

 protected:
  virtual void OnDraw(ImGuiIO& io) = 0;
  void Close() { closed_ = true; }
  bool IsClosed() const { return closed_; }

 private:
  bool opened_ = false;
  bool closed_ = false;
  ImGuiConfigFlags saved_config_flags_ = 0;
  ImGuiBackendFlags saved_backend_flags_ = 0;
};

// Shows a dialog; it is deleted after it closes. Call on the UI thread.
void ShowDialog(Dialog* dialog);

}  // namespace tb2
