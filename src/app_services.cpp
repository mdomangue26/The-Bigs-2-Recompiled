// thebigs2 - what the game's modules reach of the app (see app_services.h).

#include "app_services.h"

#include <atomic>
#include <memory>
#include <string>
#include <utility>

#include <rex/app/app.h>
#include <rex/kernel/heap.h>
#include <rex/kernel/title_runtime.h>
#include <rex/logging/log_category.h>
#include <rex/ui/ui_system.h>

namespace tb2 {

namespace {

std::atomic<rex::kernel::TitleRuntime*> g_title{nullptr};
std::atomic<rex::ui::IUISystem*> g_ui{nullptr};
std::atomic<rex::hid::IInputSystem*> g_input{nullptr};
std::atomic<bool> g_quitting{false};

}  // namespace

rex::kernel::TitleRuntime* Title() {
  return g_title.load(std::memory_order_acquire);
}

void SetTitle(rex::kernel::TitleRuntime* title) {
  g_title.store(title, std::memory_order_release);
}

uint32_t GuestHeapAlloc(uint32_t size) {
  rex::kernel::TitleRuntime* title = Title();
  return title ? title->GetGuestHeap().SystemHeapAlloc(size) : 0;
}

void GuestHeapFree(uint32_t address) {
  if (rex::kernel::TitleRuntime* title = Title(); title && address) {
    title->GetGuestHeap().SystemHeapFree(address);
  }
}

rex::ui::IUISystem* UI() {
  return g_ui.load(std::memory_order_acquire);
}

rex::hid::IInputSystem* Input() {
  return g_input.load(std::memory_order_acquire);
}

void SetSystems(rex::ui::IUISystem* ui, rex::hid::IInputSystem* input) {
  g_ui.store(ui, std::memory_order_release);
  g_input.store(input, std::memory_order_release);
}

void PostToUI(std::function<void()> fn) {
  if (rex::ui::IUISystem* ui = UI()) {
    ui->PostTask([fn = std::move(fn)]() { fn(); });
  }
}

void RequestQuit(std::string_view source) {
  if (g_quitting.exchange(true)) {
    return;
  }
  REXLOG_INFO("Quit requested via {}", source);
  rex::app::RequestShutdown(0);
}

void ShowDialog(Dialog* dialog) {
  if (rex::ui::IUISystem* ui = UI()) {
    ui->AddDialog(std::unique_ptr<rex::ui::IDialog>(dialog));
  } else {
    delete dialog;
  }
}

}  // namespace tb2
