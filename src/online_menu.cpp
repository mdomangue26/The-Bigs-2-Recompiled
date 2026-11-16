// thebigs2 - the ONLINE prompt: host or join (see online_menu.h).

#include "online_menu.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cfloat>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include <SDL3/SDL.h>
#include <imgui.h>

#include <rex/app/app.h>
#include <rex/hid/input_system.h>
#include <rex/logging/log_category.h>

#include "app_services.h"
#include "game_settings.h"
#include "imgui_gamepad.h"
#include "online.h"
#include "settings.h"
#include "ui_theme.h"

namespace tb2 {
namespace {

namespace fs = std::filesystem;
namespace th = theme;
using online::Role;

struct State {
  fs::path config_path;
  bool initialized = false;
};
State g_state;

class OnlineMenu;
OnlineMenu* g_menu = nullptr;

// The waiting AskOnlineRole call: 0 open, 1 closed, 2 a role was picked.
std::mutex g_result_mutex;
std::condition_variable g_result_cv;
int g_result = 0;

void Finish(int result) {
  {
    std::lock_guard lock(g_result_mutex);
    if (g_result == 0) g_result = result;
  }
  g_result_cv.notify_all();
}

enum class Page { kMain, kHost, kJoin };

class OnlineMenu final : public Dialog {
 public:
  OnlineMenu() {
    if (rex::hid::IInputSystem* input = Input()) input->AcquireUIBlock();
    std::string name = Settings().online_name.Get();
    if (!online::CheckName(name).empty()) name = online::DefaultName();
    Copy(name_, name);
    Copy(address_, Settings().online_host.Get());
    addresses_ = online::LocalAddresses();
  }

  ~OnlineMenu() override {
    Finish(1);  // closed without a pick (no-op after Go)
    if (rex::hid::IInputSystem* input = Input()) input->ReleaseUIBlock();
    if (g_menu == this) g_menu = nullptr;
  }

 protected:
  void OnDraw(ImGuiIO& io) override {
    gamepads_.Feed(io);
    // Enter, from SDL: the SDK's ImGui doesn't get it here.
    {
      const bool* keys = SDL_GetKeyboardState(nullptr);
      const bool down = keys && (keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_KP_ENTER]);
      enter_pressed_ = down && !enter_down_ && !enter_wait_release_;
      if (!down) enter_wait_release_ = false;
      enter_down_ = down;
    }
    const float s = std::clamp(io.DisplaySize.y / 720.0f, 0.75f, 4.0f);
    const bool typing = typing_was_active_;

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##tb2_online", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(2);
    ImGui::PushStyleColor(ImGuiCol_NavCursor, ImVec4(0, 0, 0, 0));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(4, 8, 22, 205));

    const float width = std::min(760.0f * s, io.DisplaySize.x * 0.94f);
    const float height = std::min(600.0f * s, io.DisplaySize.y * 0.96f);
    x0_ = (io.DisplaySize.x - width) * 0.5f;
    x1_ = x0_ + width;
    const float top = (io.DisplaySize.y - height) * 0.5f, bottom = top + height;
    const float title_h = 56 * s, footer_h = 40 * s, help_h = 150 * s;
    s_ = s;

    const char* title = page_ == Page::kMain   ? "ONLINE"
                        : page_ == Page::kHost ? "HOST A GAME"
                                               : "JOIN A GAME";
    th::DrawTitleBand(dl, ImVec2(x0_ - 14 * s, top), ImVec2(x0_ + 420 * s, top + title_h), s);
    th::DrawLabel(dl, th::Face::kDisplay, 46 * s, ImVec2(x0_ + 28 * s, top + 2 * s), th::kGold,
                  title);

    const float panel_top = top + title_h + 10 * s, panel_bottom = bottom - footer_h - 8 * s;
    th::DrawSilverPanel(dl, ImVec2(x0_, panel_top), ImVec2(x1_, panel_bottom), s);
    ImGui::SetCursorScreenPos(ImVec2(x0_ + 10 * s, panel_top + 10 * s));
    help_ = nullptr;
    help_text_.clear();
    action_ = Action::kNone;
    rows_w_ = width - 20 * s;
    focused_row_now_ = 0;
    switch (page_) {
      case Page::kMain: DrawMain(); break;
      case Page::kHost: DrawHost(); break;
      case Page::kJoin: DrawJoin(); break;
    }
    if (focused_row_now_) focused_row_ = focused_row_now_;

    // Help: what the selected row does, and the last message.
    {
      const ImVec2 h0(x0_ + 10 * s, panel_bottom - help_h - 4 * s), h1(x1_ - 10 * s, panel_bottom - 8 * s);
      dl->AddRectFilled(h0, h1, IM_COL32(12, 18, 38, 238), 3 * s);
      ImFont* font = th::Font(th::Face::kText, 21 * s);
      const float wrap = h1.x - h0.x - 24 * s;
      const std::string text = !help_text_.empty() ? help_text_ : help_ ? help_ : "";
      dl->AddText(font, 21 * s, ImVec2(h0.x + 12 * s, h0.y + 8 * s), th::kWhite, text.c_str(), nullptr,
                  wrap);
      if (!message_.empty()) {
        const float y = h0.y + 8 * s + font->CalcTextSizeA(21 * s, FLT_MAX, wrap, text.c_str()).y;
        dl->AddText(font, 19 * s, ImVec2(h0.x + 12 * s, std::max(y + 4 * s, h1.y - 27 * s)), th::kGold,
                    message_.c_str(), nullptr, wrap);
      }
    }

    // Buttons.
    bool back = !typing && (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
                            ImGui::IsKeyPressed(ImGuiKey_Escape, false));
    if (page_ == Page::kJoin && !typing) {
      // Digits and . type into the address without selecting the field (a
      // machine name needs the field: click it or select it with Enter).
      auto type = [this](char c) {
        const size_t n = std::strlen(address_);
        if (n + 1 < sizeof(address_)) address_[n] = c, address_[n + 1] = 0;
      };
      for (int d = 0; d < 10; ++d)
        if (ImGui::IsKeyPressed(ImGuiKey(ImGuiKey_0 + d)) || ImGui::IsKeyPressed(ImGuiKey(ImGuiKey_Keypad0 + d)))
          type(char('0' + d));
      if (ImGui::IsKeyPressed(ImGuiKey_Period) || ImGui::IsKeyPressed(ImGuiKey_KeypadDecimal)) type('.');
      if (ImGui::IsKeyPressed(ImGuiKey_Backspace)) Backspace();
      if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceLeft, false)) Backspace();
      if (ImGui::IsKeyPressed(ImGuiKey_GamepadStart, false) || enter_pressed_) action_ = Action::kConnect;
    }
    if (page_ == Page::kHost && !typing && ImGui::IsKeyPressed(ImGuiKey_GamepadStart, false))
      action_ = Action::kStartHost;
    {
      struct Prompt {
        const char* button;
        const char* label;
        int action;  // 0 none, 1 back, 2 delete, 3 connect / host
      };
      std::vector<Prompt> prompts = {{"A", "SELECT", 0}};
      if (page_ == Page::kJoin) {
        prompts.push_back({"X", "DELETE", 2});
        prompts.push_back({"START", "CONNECT", 3});
      }
      if (page_ == Page::kHost) prompts.push_back({"START", "HOST", 3});
      prompts.push_back({"B", page_ == Page::kMain ? "CLOSE" : "BACK", 1});
      const float fy = bottom - footer_h, px = 21 * s, r = 13 * s;
      dl->AddRectFilledMultiColor(ImVec2(x0_, fy), ImVec2(x1_, bottom), IM_COL32(0, 0, 0, 0),
                                  IM_COL32(0, 0, 0, 200), IM_COL32(0, 0, 0, 200), IM_COL32(0, 0, 0, 0));
      float x = x1_ - 16 * s;
      ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
      for (int k = int(prompts.size()) - 1; k >= 0; --k) {
        const Prompt& p = prompts[k];
        const float tw = th::TextSize(th::Face::kDisplay, px, p.label).x;
        const float gw = th::PadButtonWidth(p.button, r);
        const float x_start = x - tw - 8 * s - gw;
        th::DrawPadButton(dl, ImVec2(x_start + gw * 0.5f, fy + footer_h * 0.5f), r, p.button);
        th::DrawLabel(dl, th::Face::kDisplay, px,
                      ImVec2(x - tw, fy + (footer_h - th::TextSize(th::Face::kDisplay, px, "A").y) * 0.5f),
                      th::kWhite, p.label);
        ImGui::SetCursorScreenPos(ImVec2(x_start, fy));
        ImGui::PushID(k);
        if (ImGui::InvisibleButton("##prompt", ImVec2(x - x_start, footer_h))) {
          if (p.action == 1) back = true;
          if (p.action == 2) Backspace();
          if (p.action == 3) action_ = page_ == Page::kJoin ? Action::kConnect : Action::kStartHost;
        }
        ImGui::PopID();
        x = x_start - 26 * s;
      }
      ImGui::PopItemFlag();
    }
    typing_was_active_ = ImGui::GetIO().WantTextInput;
    ImGui::PopStyleColor();
    ImGui::End();

    if (back) {
      message_.clear();
      if (page_ == Page::kMain) {
        Close();
        return;
      }
      SetPage(Page::kMain);
    }
    switch (action_) {
      case Action::kNone: break;
      case Action::kHostPage:
        if (CheckName()) SetPage(Page::kHost);
        break;
      case Action::kJoinPage:
        if (CheckName()) SetPage(Page::kJoin);
        break;
      case Action::kStartHost:
        if (CheckName()) Go(Role::kHost, 0);
        break;
      case Action::kConnect: {
        if (!CheckName()) break;
        uint32_t ip = 0;
        std::string error;
        if (!online::ResolvePeer(address_, ip, error)) {
          message_ = error;
          break;
        }
        Go(Role::kJoin, ip);
        break;
      }
    }
  }

 private:
  enum class Action { kNone, kHostPage, kJoinPage, kStartHost, kConnect };

  template <size_t N>
  static void Copy(char (&out)[N], const std::string& text) {
    std::memset(out, 0, N);
    text.copy(out, N - 1);
  }

  void SetPage(Page page) {
    page_ = page;
    wait_release_ = true;
    enter_wait_release_ = enter_down_;
    first_frame_ = true;
    focused_row_ = 0;
  }

  bool CheckName() {
    const std::string error = online::CheckName(name_);
    if (!error.empty()) {
      message_ = error;
      SetPage(Page::kMain);
      return false;
    }
    return true;
  }

  void Backspace() {
    const size_t n = std::strlen(address_);
    if (n) address_[n - 1] = 0;
  }

  void Go(Role role, uint32_t ip) {
    const std::string name = name_;
    if (name != Settings().online_name.Get()) {
      ApplySettingText("tb2.online_name", name);
      SaveConfigString(g_state.config_path, "online_name", name);
    }
    if (role == Role::kJoin && std::string(address_) != Settings().online_host.Get()) {
      ApplySettingText("tb2.online_host", address_);
      SaveConfigString(g_state.config_path, "online_host", address_);
    }
    online::Start(role, name, ip);
    Finish(2);
    Close();
  }

  // A / Enter / Space on the focused item, once per press (the buttons take only mouse clicks
  // from ImGui).
  // After opening or a page change, only a new press counts.
  bool Activated(bool enter = true) {
    bool down = ImGui::IsKeyDown(ImGuiKey_GamepadFaceDown) || ImGui::IsKeyDown(ImGuiKey_Space);
    bool now = ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false) || ImGui::IsKeyPressed(ImGuiKey_Space, false);
    if (enter) {
      down = down || enter_down_;
      now = now || enter_pressed_;
    }
    if (wait_release_) {
      wait_release_ = down;
      return false;
    }
    if (!now || activated_frame_ == ImGui::GetFrameCount()) return false;
    activated_frame_ = ImGui::GetFrameCount();
    return true;
  }

  // A selectable row: the gold band and baseball cursor when focused.
  bool Row(const char* id, float height, bool* focused_out = nullptr) {
    const float s = s_;
    ImDrawList* rl = ImGui::GetWindowDrawList();
    const ImVec2 r0 = ImGui::GetCursorScreenPos(), r1(r0.x + rows_w_, r0.y + height);
    const ImGuiID row_id = ImGui::GetID(id);
    if (row_id == focused_row_) {
      th::DrawGoldBand(rl, r0, r1);
      const ImVec2 c(r0.x + height * 0.32f, (r0.y + r1.y) * 0.5f);
      rl->AddCircleFilled(c, height * 0.22f, IM_COL32(236, 236, 230, 255));
      rl->AddCircle(c, height * 0.22f, IM_COL32(120, 120, 120, 255), 0, 1.0f);
      for (float side : {-1.0f, 1.0f})
        rl->AddBezierQuadratic(ImVec2(c.x + side * height * 0.12f, c.y - height * 0.17f),
                               ImVec2(c.x + side * height * 0.03f, c.y),
                               ImVec2(c.x + side * height * 0.12f, c.y + height * 0.17f),
                               IM_COL32(200, 40, 40, 255), std::max(1.0f, 1.5f * s));
    } else if (ImGui::IsMouseHoveringRect(r0, r1)) {
      rl->AddRectFilled(r0, r1, IM_COL32(0, 0, 0, 28));
    }
    if (first_frame_) {
      ImGui::SetKeyboardFocusHere();
      first_frame_ = false;
    }
    // Mouse clicks only: A / Enter / Space go through Activated() (ImGui's nav activation would
    // press the item a second time).
    bool pressed = ImGui::InvisibleButton(id, ImVec2(rows_w_, height), ImGuiButtonFlags_EnableNav) &&
                   ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    const bool focused = ImGui::IsItemFocused();
    pressed = pressed || (focused && Activated());
    if (focused) focused_row_now_ = row_id;
    if (focused_out) *focused_out = focused || ImGui::IsItemHovered();
    ImGui::SetCursorScreenPos(ImVec2(r0.x, r1.y + 3 * s));
    return pressed;
  }

  void RowLabel(const ImVec2& r0, float height, const char* label) {
    const float px = 25 * s_;
    th::DrawLabel(ImGui::GetWindowDrawList(), th::Face::kDisplay, px,
                  ImVec2(r0.x + height * 1.25f,
                         r0.y + (height - th::TextSize(th::Face::kDisplay, px, "A").y) * 0.5f),
                  th::kInk, label);
  }

  // A row with a text field on its right half (A / Enter / a click to type).
  void TextRow(const char* id, const char* label, char* buffer, size_t size, const char* help,
               ImGuiInputTextFlags flags = 0) {
    const float s = s_, height = 38 * s;
    const ImVec2 r0 = ImGui::GetCursorScreenPos();
    const ImGuiID row_id = ImGui::GetID(id);
    if (row_id == focused_row_) th::DrawGoldBand(ImGui::GetWindowDrawList(), r0,
                                                 ImVec2(r0.x + rows_w_, r0.y + height));
    RowLabel(r0, height, label);
    ImGui::PushID(id);
    ImGui::SetCursorScreenPos(r0);
    ImGui::InvisibleButton(id, ImVec2(1, height));  // the row's id
    ImGui::SetCursorScreenPos(ImVec2(r0.x + rows_w_ * 0.48f, r0.y + 4 * s));
    ImFont* font = th::Font(th::Face::kText, 25 * s);
    ImGui::PushFont(font, font->LegacySize);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.09f, 0.10f, 0.13f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(1, 1, 1, 0.35f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(1, 1, 1, 0.55f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(1, 1, 1, 0.95f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8 * s, (height - 8 * s - font->LegacySize) * 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3 * s);
    ImGui::SetNextItemWidth(rows_w_ * 0.52f - 16 * s);
    ImGui::InputText("##text", buffer, size, flags);
    const bool focused = ImGui::IsItemFocused() || ImGui::IsItemActive();
    if (focused || ImGui::IsItemHovered()) help_ = help;
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);
    ImGui::PopFont();
    ImGui::PopID();
    if (focused) focused_row_now_ = row_id;
    ImGui::SetCursorScreenPos(ImVec2(r0.x, r0.y + height + 3 * s));
  }

  void ButtonRow(const char* id, const char* label, const char* help, Action action) {
    const float height = 38 * s_;
    const ImVec2 r0 = ImGui::GetCursorScreenPos();
    bool hot = false;
    if (Row(id, height, &hot)) action_ = action;
    RowLabel(r0, height, label);
    if (hot) help_ = help;
  }

  void DrawMain() {
    static constexpr const char* kNameHelp =
        "Your name in the Game Room: up to 8 letters or digits. The two players need different "
        "names.";
    TextRow("##name", "PLAYER NAME", name_, online::kMaxNameLength + 1, kNameHelp,
            ImGuiInputTextFlags_CharsNoBlank);
    ButtonRow("##host", "HOST A GAME", nullptr, Action::kHostPage);
    if (focused_row_ == ImGui::GetID("##host")) help_text_ = HostHelp(false);
    ButtonRow("##join", "JOIN A GAME",
              "Join a friend's game: you need the address their HOST A GAME page shows.",
              Action::kJoinPage);
    if (!help_ && help_text_.empty())
      help_ = "Play a friend over the internet or your home network. One of you hosts, the other "
              "joins with the host's address. Online matches run at 30 fps.";
  }

  std::string HostHelp(bool detailed) const {
    std::string text = "Give your friend ";
    if (addresses_.empty()) {
      text += "this computer's IP address.";
    } else {
      text += addresses_.size() == 1 ? "this address: " : "one of these addresses: ";
      for (size_t i = 0; i < addresses_.size() && i < 4; ++i) text += (i ? ", " : "") + addresses_[i];
      text += ".";
    }
    if (detailed)
      text +=
          " Over the internet your router must forward TCP 51000 and UDP 1795 to this computer "
          "(not needed on one home network or with a VPN like Tailscale). Next, in the game: "
          "QUICK MATCH, SEARCH, then CREATE GAME with X.";
    return text;
  }

  void DrawHost() {
    help_text_ = HostHelp(true);
    ButtonRow("##start", "START HOSTING", nullptr, Action::kStartHost);
  }

  void DrawJoin() {
    TextRow("##address", "HOST'S ADDRESS", address_, sizeof(address_),
            "The host's IP address (it is on their HOST A GAME page), or its machine name.");
    // Keypad for controllers.
    const float s = s_, key_h = 38 * s;
    static constexpr const char* kKeys[4][3] = {
        {"1", "2", "3"}, {"4", "5", "6"}, {"7", "8", "9"}, {".", "0", "DEL"}};
    const ImVec2 k0 = ImGui::GetCursorScreenPos();
    const float key_w = 92 * s, gap = 6 * s;
    const float kx = k0.x + rows_w_ * 0.48f;
    ImDrawList* rl = ImGui::GetWindowDrawList();
    for (int r = 0; r < 4; ++r) {
      for (int c = 0; c < 3; ++c) {
        const ImVec2 p0(kx + c * (key_w + gap), k0.y + r * (key_h + gap));
        const ImVec2 p1(p0.x + key_w, p0.y + key_h);
        ImGui::SetCursorScreenPos(p0);
        ImGui::PushID(r * 3 + c);
        if (first_frame_) {
          ImGui::SetKeyboardFocusHere();
          first_frame_ = false;
        }
        bool pressed =
            ImGui::InvisibleButton("##key", ImVec2(key_w, key_h), ImGuiButtonFlags_EnableNav) &&
            ImGui::IsMouseReleased(ImGuiMouseButton_Left);  // keys: Activated() below
        const bool focused = ImGui::IsItemFocused();
        pressed = pressed || (focused && Activated(false));  // Enter: CONNECT
        ImGui::PopID();
        if (focused) th::DrawGoldBand(rl, p0, p1);
        else rl->AddRectFilled(p0, p1, ImGui::IsItemHovered() ? IM_COL32(0, 0, 0, 50) : IM_COL32(0, 0, 0, 22),
                               3 * s);
        th::DrawLabel(rl, th::Face::kDisplay, 25 * s,
                      ImVec2((p0.x + p1.x) * 0.5f, p0.y + (key_h - th::TextSize(th::Face::kDisplay, 25 * s, "0").y) * 0.5f),
                      th::kInk, kKeys[r][c], 0.5f);
        if (focused || ImGui::IsItemHovered())
          help_ = "Type the host's address with the keypad or the keyboard (X or Backspace deletes), "
                  "then CONNECT.";
        if (pressed) {
          if (std::strcmp(kKeys[r][c], "DEL") == 0) {
            Backspace();
          } else if (std::strlen(address_) + 1 < sizeof(address_)) {
            std::strcat(address_, kKeys[r][c]);
          }
        }
      }
    }
    ImGui::SetCursorScreenPos(ImVec2(k0.x, k0.y + 4 * (key_h + gap) + 4 * s));
    ButtonRow("##connect", "CONNECT", "Connect to the host. Next, in the game: QUICK MATCH, then SEARCH.",
              Action::kConnect);
  }

  Page page_ = Page::kMain;
  Action action_ = Action::kNone;
  char name_[online::kMaxNameLength + 1] = {};
  char address_[64] = {};
  std::vector<std::string> addresses_;
  std::string message_, help_text_;
  const char* help_ = nullptr;
  float s_ = 1, x0_ = 0, x1_ = 0, rows_w_ = 0;
  bool first_frame_ = true;
  bool typing_was_active_ = false;
  int activated_frame_ = -1;
  bool wait_release_ = true;
  bool enter_down_ = true, enter_pressed_ = false, enter_wait_release_ = true;
  ImGuiID focused_row_ = 0, focused_row_now_ = 0;
  ImGuiGamepads gamepads_;
};

}  // namespace

void InitOnlineMenu(std::filesystem::path config_path) {
  g_state = {std::move(config_path), true};
}

bool AskOnlineRole() {
  if (!g_state.initialized || !UI()) return false;
  {
    std::lock_guard lock(g_result_mutex);
    g_result = 0;
  }
  PostToUI([] {
    if (g_menu) {
      Finish(1);  // one at a time
      return;
    }
    g_menu = new OnlineMenu();  // the UI system deletes it after it closes
    ShowDialog(g_menu);
  });
  // A guest thread waits here: give up when the game quits, or shutdown would wait for it.
  std::unique_lock lock(g_result_mutex);
  while (!g_result_cv.wait_for(lock, std::chrono::milliseconds(100), [] { return g_result != 0; })) {
    if (rex::app::IsShutdownRequested()) return false;
  }
  return g_result == 2;
}

}  // namespace tb2
