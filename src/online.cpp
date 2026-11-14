// thebigs2 - online play: the Xbox Live stand-in (see online.h).
//
// Ported from MatRanc's online fork (src/online_debug.cpp there, which also
// has the tracing probes used to find all this). Addresses are guest
// addresses in the retail default.xex (sub_XXXXXXXX in generated/). Every
// hook passes straight through until a role has been picked (Active()).

#include "online.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string_view>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>

#include "guest_memory.h"

namespace tb2::online {
namespace {

std::atomic<Role> g_role{Role::kNone};
std::atomic<uint32_t> g_peer_ip{0};
std::mutex g_name_mutex;
char g_name[16] = "Player";

}  // namespace

Role CurrentRole() { return g_role.load(std::memory_order_acquire); }
bool Active() { return CurrentRole() != Role::kNone; }

void Start(Role role, const std::string& name, uint32_t peer_ip) {
  {
    std::lock_guard lock(g_name_mutex);
    std::memset(g_name, 0, sizeof(g_name));
    name.copy(g_name, kMaxNameLength);
  }
  g_peer_ip.store(peer_ip, std::memory_order_release);
  const bool first = !Active();
  // The SDK's profile: signed in to Xbox Live from now on, under this name.
  rex::cvar::SetFlagByName("user_name", name);
  rex::cvar::SetFlagByName("fake_xbox_live", "true");
  g_role.store(role, std::memory_order_release);
  if (first) {
    // Titles learn about a Live sign-in through these notifications.
    if (auto* ks = rex::system::kernel_state()) {
      ks->BroadcastNotification(0x02000001, 0x001510F0u);  // XN_LIVE_CONNECTIONCHANGED: connected
      ks->BroadcastNotification(0x0000000A, 1);            // XN_SYS_SIGNINCHANGED
    }
  }
  REXLOG_INFO("Online: {} as '{}'{}", role == Role::kHost ? "hosting" : "joining", name,
              role == Role::kJoin ? " -> " + FormatIp(peer_ip) : std::string());
}

std::string CheckName(const std::string& name) {
  if (name.empty()) return "Enter a player name.";
  if (name.size() > kMaxNameLength) return "Player names can have at most 8 characters.";
  for (char c : name)
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != ' ' && c != '_' && c != '-')
      return "Use only letters, digits, space, _ and - in the player name.";
  if (name.front() == ' ') return "The player name can't start with a space.";
  return {};
}

std::string DefaultName() {
  const char* user = std::getenv("USER");
  if (!user || !*user) user = std::getenv("USERNAME");
  std::string name;
  for (const char* p = user ? user : ""; *p && name.size() < kMaxNameLength; ++p)
    if (std::isalnum(static_cast<unsigned char>(*p)) || *p == '_' || *p == '-') name += *p;
  return name.empty() ? "Player" : name;
}

std::string FormatIp(uint32_t ip) {
  char text[16];
  std::snprintf(text, sizeof(text), "%u.%u.%u.%u", ip >> 24, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF,
                ip & 0xFF);
  return text;
}

std::vector<std::string> LocalAddresses() {
  std::vector<uint32_t> ips;
#ifdef _WIN32
  // The address the default route uses (no packet is sent).
  const SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (s != INVALID_SOCKET) {
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(9);
    to.sin_addr.s_addr = htonl(0xC0000201);  // 192.0.2.1 (TEST-NET-1)
    sockaddr_in me{};
    int len = sizeof(me);
    if (connect(s, reinterpret_cast<sockaddr*>(&to), sizeof(to)) == 0 &&
        getsockname(s, reinterpret_cast<sockaddr*>(&me), &len) == 0)
      ips.push_back(ntohl(me.sin_addr.s_addr));
    closesocket(s);
  }
#else
  ifaddrs* list = nullptr;
  if (getifaddrs(&list) == 0) {
    for (ifaddrs* i = list; i; i = i->ifa_next) {
      if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET) continue;
      // Container and VM bridges aren't reachable from another machine.
      const std::string_view name = i->ifa_name ? i->ifa_name : "";
      if (name.starts_with("docker") || name.starts_with("br-") || name.starts_with("veth") ||
          name.starts_with("virbr") || name.starts_with("lxc") || name.starts_with("podman"))
        continue;
      const uint32_t ip = ntohl(reinterpret_cast<sockaddr_in*>(i->ifa_addr)->sin_addr.s_addr);
      if (ip && (ip >> 24) != 127) ips.push_back(ip);
    }
    freeifaddrs(list);
  }
#endif
  // Home networks (192.168/16, 10/8, 172.16/12) first, then VPNs like
  // Tailscale (100.64/10), then the rest.
  auto rank = [](uint32_t ip) {
    if ((ip >> 16) == 0xC0A8 || (ip >> 24) == 10 || (ip >> 20) == 0xAC1) return 0;
    if ((ip >> 22) == (0x64400000u >> 22)) return 1;
    return 2;
  };
  std::stable_sort(ips.begin(), ips.end(), [&](uint32_t a, uint32_t b) { return rank(a) < rank(b); });
  std::vector<std::string> out;
  for (uint32_t ip : ips) out.push_back(FormatIp(ip));
  return out;
}

bool ResolvePeer(const std::string& input, uint32_t& ip, std::string& error) {
  std::string text = input;
  text.erase(0, text.find_first_not_of(" \t"));
  text.erase(text.find_last_not_of(" \t") + 1);
  if (text.empty()) {
    error = "Enter the host's IP address.";
    return false;
  }
  unsigned a, b, c, d;
  char extra;
  if (std::sscanf(text.c_str(), "%u.%u.%u.%u%c", &a, &b, &c, &d, &extra) == 4) {
    if (a > 255 || b > 255 || c > 255 || d > 255) {
      error = "That isn't an IP address (each number goes up to 255).";
      return false;
    }
    ip = a << 24 | b << 16 | c << 8 | d;
    return true;
  }
  // A machine name (e.g. a Tailscale name): it has a letter (getaddrinfo
  // would take "1" or "10.1" as an address).
  if (std::none_of(text.begin(), text.end(), [](char c) { return std::isalpha(static_cast<unsigned char>(c)); })) {
    error = "That isn't an IP address: four numbers with dots, like 192.168.1.20.";
    return false;
  }
  addrinfo hints{};
  hints.ai_family = AF_INET;
  addrinfo* result = nullptr;
  if (getaddrinfo(text.c_str(), nullptr, &hints, &result) != 0 || !result) {
    error = "Couldn't find \"" + text + "\". Enter the host's IP address.";
    return false;
  }
  ip = ntohl(reinterpret_cast<sockaddr_in*>(result->ai_addr)->sin_addr.s_addr);
  freeaddrinfo(result);
  return true;
}

}  // namespace tb2::online

using tb2::online::Active;
using tb2::online::CurrentRole;
using tb2::online::Role;

namespace {

uint32_t LoadGuestU32(uint8_t* base, uint32_t addr) {
  uint32_t v = 0;
  std::memcpy(&v, GuestPtr(base, addr), sizeof(v));
  return __builtin_bswap32(v);
}
void StoreGuestU8(uint8_t* base, uint32_t addr, uint8_t v) { *GuestPtr(base, addr) = v; }
void StoreGuestU16(uint8_t* base, uint32_t addr, uint16_t v) {
  v = __builtin_bswap16(v);
  std::memcpy(GuestPtr(base, addr), &v, sizeof(v));
}
void StoreGuestU32(uint8_t* base, uint32_t addr, uint32_t v) {
  v = __builtin_bswap32(v);
  std::memcpy(GuestPtr(base, addr), &v, sizeof(v));
}
void StoreGuestU64(uint8_t* base, uint32_t addr, uint64_t v) {
  v = __builtin_bswap64(v);
  std::memcpy(GuestPtr(base, addr), &v, sizeof(v));
}
void StoreGuestBytes(uint8_t* base, uint32_t addr, const void* src, size_t len) {
  std::memcpy(GuestPtr(base, addr), src, len);
}

// Guest globals of the game's Live layer.
constexpr uint32_t kLiveMode = 0x825f50e0;       // 2 = online
constexpr uint32_t kAccountList = 0x825f50e8;    // signed-in accounts
constexpr uint32_t kPendingSignin = 0x825f50ec;  // sign-in the Live stack fills in
constexpr uint32_t kEventQueue = 0x825f50fc;     // Live event queue
constexpr uint32_t kSession = 0x825f5144;        // the OP session (0 = none)
constexpr uint32_t kSessionLock = 0x825f514c;
constexpr uint32_t kLiveUser = 0x825d4bf8;       // Live user index, 4 = no user
constexpr uint32_t kUIManager = 0x82633548;
constexpr uint32_t kLiveModeOnline = 2;
constexpr uint16_t kGameSessionPort = 1000;      // the game's own port (needs root on Linux)

// The player name as the game's account XUID: the Game Room draws the XUID's
// 8 bytes as the name (a real XUID starts with a zero byte), so the XUID is
// the name, NUL-padded. Two players need names that differ in those 8.
uint64_t XuidFromName(const char* n) {
  uint64_t x = 0;
  for (int i = 0; i < 8 && n[i]; i++) x |= uint64_t(uint8_t(n[i])) << (8 * (7 - i));
  return x;
}

}  // namespace

extern "C" REX_FUNC(__imp__sub_8240A5C0);
extern "C" REX_FUNC(__imp__sub_823EA090);
extern "C" REX_FUNC(__imp__sub_823F81B0);
extern "C" REX_FUNC(__imp__sub_8240F610);
extern "C" REX_FUNC(__imp__sub_823E3348);
extern "C" REX_FUNC(__imp__sub_82289278);
extern "C" REX_FUNC(__imp__sub_823FB6A0);

namespace {

uint32_t GuestAlloc(PPCContext& parent, uint8_t* base, uint32_t size, uint32_t flag) {
  rex::CallFrame frame(parent);
  frame.ctx.r3.u64 = size;
  frame.ctx.r4.u64 = 25;
  frame.ctx.r5.u64 = 0;
  frame.ctx.r6.u64 = flag;
  __imp__sub_8240A5C0(frame.ctx, base);
  return frame.ctx.r3.u32;
}

void EnqueueLiveEvent(PPCContext& parent, uint8_t* base, uint32_t id) {
  rex::CallFrame frame(parent);
  frame.ctx.r3.u64 = id;
  __imp__sub_823EA090(frame.ctx, base);
}

// One XSESSION_SEARCHRESULT (header + result + eight properties) pointing at
// the host. Returns the header address, or 0.
uint32_t BuildSearchResult(PPCContext& parent, uint8_t* base, uint32_t ip_host_order) {
  constexpr uint32_t kHeaderSize = 0x08, kResultSize = 0x5C, kPropCount = 8, kPropSize = 0x18;
  constexpr uint32_t kTotal = kHeaderSize + kResultSize + kPropCount * kPropSize;
  const uint32_t block = GuestAlloc(parent, base, kTotal, 1);
  if (!block) return 0;
  std::memset(GuestPtr(base, block), 0, kTotal);
  const uint32_t result = block + kHeaderSize;
  const uint32_t props = result + kResultSize;
  StoreGuestU32(base, block + 0x00, 1);  // one result
  StoreGuestU32(base, block + 0x04, result);

  const uint8_t ip[4] = {uint8_t(ip_host_order >> 24), uint8_t(ip_host_order >> 16),
                         uint8_t(ip_host_order >> 8), uint8_t(ip_host_order)};
  // XSESSION_INFO { XNKID, XNADDR, XNKEY }
  StoreGuestU64(base, result + 0x00, 0x4249473200000001ull);  // XNKID
  StoreGuestBytes(base, result + 0x08, ip, 4);                // XNADDR.ina
  StoreGuestBytes(base, result + 0x0C, ip, 4);                // XNADDR.inaOnline
  StoreGuestU16(base, result + 0x10, tb2::online::kSessionPort);
  const uint8_t mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
  StoreGuestBytes(base, result + 0x12, mac, sizeof(mac));
  StoreGuestBytes(base, result + 0x18, ip, 4);                // SGADDR.ina
  StoreGuestU32(base, result + 0x1C, 0x12345678);             // SGADDR.security_parameter_index
  StoreGuestU64(base, result + 0x20, 0xB13EBABEBABEBABEull);  // SGADDR.xbox_id
  StoreGuestU8(base, result + 0x28, 1);                       // SGADDR.platform_type = Xbox 360
  const uint8_t key[16] = {0x42, 0x49, 0x47, 0x53, 0x32, 0x42, 0x49, 0x47,
                           0x53, 0x32, 0x42, 0x49, 0x47, 0x53, 0x32, 0x01};
  StoreGuestBytes(base, result + 0x2C, key, sizeof(key));     // XNKEY
  StoreGuestU32(base, result + 0x3C, 1);  // open public slots
  StoreGuestU32(base, result + 0x40, 0);  // open private slots
  StoreGuestU32(base, result + 0x44, 1);  // filled public slots
  StoreGuestU32(base, result + 0x48, 0);  // filled private slots
  StoreGuestU32(base, result + 0x4C, kPropCount);
  StoreGuestU32(base, result + 0x50, 0);
  StoreGuestU32(base, result + 0x54, props);
  StoreGuestU32(base, result + 0x58, 0);
  // The game wants at least eight properties: the ids its search filter asks
  // for (0x1000003e..0x10000044).
  for (uint32_t i = 0; i < kPropCount; ++i) {
    StoreGuestU32(base, props + i * kPropSize + 0x00, 0x1000003E + (i % 7));
    StoreGuestU32(base, props + i * kPropSize + 0x10, 0);
  }
  return block;
}

// The account our sign-in made (sub_823E9F18), kept through sign-out resets.
uint32_t g_fake_account = 0;

// The statically linked Live stack never finishes a sign-in (no service). The
// game itself creates the pending sign-in object (*kPendingSignin, 0x38
// bytes) that Live would fill in and then raise event 0x3C, which makes
// sub_823F5F88 build the account with the game's own constructor
// (sub_823E9F18). We fill a new pending object and raise the event.
void MaybeInjectAccount(PPCContext& parent, uint8_t* base) {
  static bool injected = false;
  if (LoadGuestU32(base, kLiveMode) != kLiveModeOnline) {
    injected = false;  // left Live mode: sign in again next time
    return;
  }
  if (injected || !LoadGuestU32(base, kAccountList) || !LoadGuestU32(base, kEventQueue)) return;
  const uint32_t pending = GuestAlloc(parent, base, 0x38, 1);
  if (!pending) {
    REXLOG_WARN("Online: sign-in allocation failed");
    return;
  }
  std::memset(GuestPtr(base, pending), 0, 0x38);
  char name[16] = {};
  {
    std::lock_guard lock(tb2::online::g_name_mutex);
    std::memcpy(name, tb2::online::g_name, sizeof(name));
  }
  StoreGuestU32(base, pending + 0x00, 0);  // user index
  // +0x08: the account's 16-byte player id, sent in the participant table and
  // shown as the gamertag; its first 8 bytes also serve as an id.
  std::memcpy(GuestPtr(base, pending + 0x08), name, 16);
  StoreGuestU32(base, pending + 0x18, 0);  // account type (Live)
  StoreGuestU64(base, pending + 0x1C, XuidFromName(name));
  StoreGuestU32(base, pending + 0x24, 0);  // user flags
  StoreGuestU32(base, pending + 0x28, 2);  // signed in to Live
  StoreGuestU32(base, pending + 0x2C, 0);  // guest number
  StoreGuestU32(base, pending + 0x30, 4);
  StoreGuestU8(base, pending + 0x34, 1);
  // The old pending object is left alone (the Live stack may still free it).
  StoreGuestU32(base, kPendingSignin, pending);
  EnqueueLiveEvent(parent, base, 0x3C);
  injected = true;
  REXLOG_INFO("Online: signed in to the stand-in Live as '{}'", name);
}

uint32_t g_stats_mgr = 0;  // stats manager with a request pending
bool g_return_to_menu_pending = false;
bool g_skip_empty_table = false;

// True if ip (host order) is one of this machine's addresses (only those can
// be bound).
bool IsOwnIp(uint32_t ip) {
  if ((ip >> 24) == 127) return true;
  const auto fd = socket(AF_INET, SOCK_DGRAM, 0);
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(ip);
  const bool own = bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0;
#ifdef _WIN32
  closesocket(fd);
#else
  close(fd);
#endif
  return own;
}

}  // namespace

// --- Roster: the download server is gone, use the built-in roster ----------

// "Is the local roster up to date?" (Quick Match): yes, skip the download.
extern "C" REX_FUNC(__imp__sub_822DD5A0);
extern "C" REX_FUNC(sub_822DD5A0) {
  if (Active()) {
    ctx.r3.u64 = 1;
    return;
  }
  [[clang::musttail]] return __imp__sub_822DD5A0(ctx, base);
}

// Roster download start (roster object vtable+0x14): mark it in progress and
// queue an empty download response (event 0x35); the roster object then
// reports "download complete" (0x37) and keeps the built-in roster.
extern "C" REX_FUNC(__imp__sub_823F08F8);
extern "C" REX_FUNC(sub_823F08F8) {
  if (Active()) {
    if (const uint32_t obj = ctx.r3.u32) StoreGuestU8(base, obj + 0xC, 1);
    EnqueueLiveEvent(ctx, base, 0x35);
    return;
  }
  [[clang::musttail]] return __imp__sub_823F08F8(ctx, base);
}

// --- Sign-in -----------------------------------------------------------------

// The sign-in processor (only advances queue entries of type 0x3C).
extern "C" REX_FUNC(__imp__sub_823F5F88);
extern "C" REX_FUNC(sub_823F5F88) {
  if (Active()) {
    // The offline boot left the Live user index at 0 (the local profile);
    // with no session that makes sub_823F12B0 here read guest address 0.
    // Same rule as the sub_823E9EB0 hook: 4 until a session exists.
    if (!LoadGuestU32(base, kSession) && LoadGuestU32(base, kLiveUser) < 4)
      StoreGuestU32(base, kLiveUser, 4);
    MaybeInjectAccount(ctx, base);
  }
  [[clang::musttail]] return __imp__sub_823F5F88(ctx, base);
}

// The account constructor: remember our account.
extern "C" REX_FUNC(__imp__sub_823E9F18);
extern "C" REX_FUNC(sub_823E9F18) {
  __imp__sub_823E9F18(ctx, base);
  if (Active() && ctx.r3.u32) g_fake_account = ctx.r3.u32;
}

// sub_823F5DA0(user, reason) signs a user out and deletes the account; the
// online manager calls it as a "Live disconnected" reset. Keep ours.
extern "C" REX_FUNC(__imp__sub_823F5DA0);
extern "C" REX_FUNC(sub_823F5DA0) {
  if (Active() && g_fake_account) {
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = ctx.r3.u32;
    __imp__sub_823E3348(frame.ctx, base);  // the user's account
    if (frame.ctx.r3.u32 == g_fake_account) return;
  }
  [[clang::musttail]] return __imp__sub_823F5DA0(ctx, base);
}

// Boot race: a sign-in message can arrive before the UI manager exists; the
// "Signing in" dialog code (sub_822A5328) then reads a null vtable.
extern "C" REX_FUNC(__imp__sub_822A5328);
extern "C" REX_FUNC(sub_822A5328) {
  const uint32_t mgr = LoadGuestU32(base, kUIManager);
  if (!mgr || !LoadGuestU32(base, mgr + 4)) {
    REXLOG_WARN("Online: sign-in dialog skipped, UI manager not ready yet");
    return;
  }
  [[clang::musttail]] return __imp__sub_822A5328(ctx, base);
}

// sub_822A5060(mgr, reason): a Live connection-lost event (sign out + "You
// must be connected to Xbox LIVE"). Before a session exists there is nothing
// to lose.
extern "C" REX_FUNC(__imp__sub_822A5060);
extern "C" REX_FUNC(sub_822A5060) {
  if (Active() && !LoadGuestU32(base, kSession)) {
    REXLOG_INFO("Online: Live-lost dialog skipped (reason {}, no session)", ctx.r4.s32);
    return;
  }
  [[clang::musttail]] return __imp__sub_822A5060(ctx, base);
}

// sub_823E9EB0(user) sets the Live user index. With user 0 set and no session
// the Live code looks the user up in a list our account isn't in and reads
// guest address 0: keep it at 4 ("no user") until a session exists
// (sub_82289560 below sets it then).
extern "C" REX_FUNC(__imp__sub_823E9EB0);
extern "C" REX_FUNC(sub_823E9EB0) {
  if (Active() && ctx.r3.s32 < 4 && !LoadGuestU32(base, kSession)) ctx.r3.u64 = 4;
  [[clang::musttail]] return __imp__sub_823E9EB0(ctx, base);
}

// --- Matchmaking ---------------------------------------------------------------

// sub_823F82D8 ends the search start: it issues the (dead) Live search. The
// host gets "No Game Rooms found" (it then creates one with X); the joiner
// gets one result, the host's address, processed by sub_823F81B0.
extern "C" REX_FUNC(__imp__sub_823F82D8);
extern "C" REX_FUNC(sub_823F82D8) {
  const uint32_t manager = ctx.r3.u32;
  __imp__sub_823F82D8(ctx, base);
  const Role role = CurrentRole();
  if (!manager || role == Role::kNone) return;
  if (role == Role::kHost) {
    // No session here: Create Game makes its own (a second one here used to
    // crash on a later Search).
    StoreGuestU32(base, manager + 0xA8, 0);  // search status
    return;
  }
  const uint32_t ip = tb2::online::g_peer_ip.load(std::memory_order_acquire);
  const uint32_t block = BuildSearchResult(ctx, base, ip);
  if (!block) {
    REXLOG_WARN("Online: search result allocation failed");
    return;
  }
  StoreGuestU32(base, manager + 0xE0, block);  // result list
  StoreGuestU32(base, manager + 0xDC, 0x08 + 0x5C + 8 * 0x18);
  StoreGuestU32(base, manager + 0xA8, 0);
  rex::CallFrame frame(ctx);
  frame.ctx.r3.u64 = manager;
  __imp__sub_823F81B0(frame.ctx, base);
  REXLOG_INFO("Online: search result -> {}:{}", tb2::online::FormatIp(ip), tb2::online::kSessionPort);
}

// --- Sessions -----------------------------------------------------------------

// The OP session's constructor takes the session lock, its teardown releases
// it. A session still registered at construction is an orphan whose lock
// would stay held (host stuck on "Exiting match"): release it once.
extern "C" REX_FUNC(__imp__sub_823F43B0);
extern "C" REX_FUNC(sub_823F43B0) {
  if (Active()) {
    if (const uint32_t old = LoadGuestU32(base, kSession)) {
      REXLOG_INFO("Online: session 0x{:08X} replaced without teardown, releasing its lock", old);
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = kSessionLock;
      __imp__sub_8240F610(frame.ctx, base);
    }
  }
  [[clang::musttail]] return __imp__sub_823F43B0(ctx, base);
}

// Session health check: it wants a Live account for the Live user index;
// ours is user 0.
extern "C" REX_FUNC(__imp__sub_823F9960);
extern "C" REX_FUNC(sub_823F9960) {
  if (!Active()) {
    [[clang::musttail]] return __imp__sub_823F9960(ctx, base);
  }
  const uint32_t saved_user = LoadGuestU32(base, kLiveUser);
  if (saved_user != 0) StoreGuestU32(base, kLiveUser, 0);
  __imp__sub_823F9960(ctx, base);
  if (saved_user != 0) StoreGuestU32(base, kLiveUser, saved_user);
}

// Per-player stats request (Game Room names): it bails out while the Live
// user index is 4. Use user 0 for the call, and pump the manager afterwards
// (sub_82289560: the Game Room never does).
extern "C" REX_FUNC(__imp__sub_823EE498);
extern "C" REX_FUNC(sub_823EE498) {
  if (!Active()) {
    [[clang::musttail]] return __imp__sub_823EE498(ctx, base);
  }
  const uint32_t mgr = ctx.r3.u32;
  const uint32_t saved_user = LoadGuestU32(base, kLiveUser);
  const bool fix = saved_user == 4;
  if (fix) StoreGuestU32(base, kLiveUser, 0);
  __imp__sub_823EE498(ctx, base);
  if ((ctx.r3.u32 & 0xFF) == 1) g_stats_mgr = mgr;
  if (fix) StoreGuestU32(base, kLiveUser, saved_user);
}

// The online manager's per-frame update.
extern "C" REX_FUNC(__imp__sub_82289560);
extern "C" REX_FUNC(sub_82289560) {
  if (!Active()) {
    [[clang::musttail]] return __imp__sub_82289560(ctx, base);
  }
  // The Game Room's lookups use the Live user's account; the real sign-in
  // sets the index, ours doesn't. User 0 while a session exists, 4 after
  // (with 0 and no session a second search crashed).
  static bool set_user = false;
  const bool session = LoadGuestU32(base, kSession) != 0;
  const uint32_t user = LoadGuestU32(base, kLiveUser);
  if (session && user == 4) {
    StoreGuestU32(base, kLiveUser, 0);
    set_user = true;
  } else if (!session && set_user && user == 0) {
    StoreGuestU32(base, kLiveUser, 4);
    set_user = false;
  }
  __imp__sub_82289560(ctx, base);
  // The stats manager's update (vtable+0x28) while a request is pending.
  if (const uint32_t m = g_stats_mgr) {
    if (*GuestPtr(base, m + 0x104) || *GuestPtr(base, m + 0x105) || *GuestPtr(base, m + 0x106)) {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = m;
      __imp__sub_823FB6A0(frame.ctx, base);
    } else {
      g_stats_mgr = 0;
    }
  }
  if (g_return_to_menu_pending) {
    g_return_to_menu_pending = false;
    REXLOG_INFO("Online: left the Game Room, back to the menu");
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = ctx.r3.u64;  // as in the fork (tested): r3 after the update
    __imp__sub_82289278(frame.ctx, base);
  }
}

// Dialog dismissed: the end of the leave handling (event 0x26). The game
// never pops the Game Room by itself with our account: queue its own
// return-to-menu call (sub_82289278).
extern "C" REX_FUNC(__imp__sub_824B0128);
extern "C" REX_FUNC(sub_824B0128) {
  if (Active() && ctx.r3.u32 == 0xDFC52393 && uint32_t(ctx.lr) == 0x822A4924)
    g_return_to_menu_pending = true;
  [[clang::musttail]] return __imp__sub_824B0128(ctx, base);
}

// --- Game Room buttons that need Live data (all crashed: null player record) -

// X: "view player".
extern "C" REX_FUNC(__imp__sub_8230E458);
extern "C" REX_FUNC(sub_8230E458) {
  if (Active()) return;
  [[clang::musttail]] return __imp__sub_8230E458(ctx, base);
}
// Right stick: gamer card.
extern "C" REX_FUNC(__imp__sub_8230E0F8);
extern "C" REX_FUNC(sub_8230E0F8) {
  if (Active()) {
    ctx.r3.u64 = 1;
    return;
  }
  [[clang::musttail]] return __imp__sub_8230E0F8(ctx, base);
}
// LB / RB: the Live player list (waits forever for an enumeration).
extern "C" REX_FUNC(__imp__sub_8230E0A0);
extern "C" REX_FUNC(sub_8230E0A0) {
  if (Active()) {
    ctx.r3.u64 = 1;
    return;
  }
  [[clang::musttail]] return __imp__sub_8230E0A0(ctx, base);
}
// Slot picture: no gamer pictures; the fetch left a white square. Keep "?".
extern "C" REX_FUNC(__imp__sub_8230E510);
extern "C" REX_FUNC(sub_8230E510) {
  if (Active()) {
    ctx.r3.u64 = 0;
    return;
  }
  [[clang::musttail]] return __imp__sub_8230E510(ctx, base);
}

// --- Transport -------------------------------------------------------------------

// Connect (sockaddr builder; r4 = address, r5 = port): the joiner's Game
// Room connection goes to the host on our port.
extern "C" REX_FUNC(__imp__sub_82420E38);
extern "C" REX_FUNC(sub_82420E38) {
  if (CurrentRole() == Role::kJoin) {
    ctx.r4.u64 = tb2::online::g_peer_ip.load(std::memory_order_acquire);
    ctx.r5.u64 = tb2::online::kSessionPort;
  }
  [[clang::musttail]] return __imp__sub_82420E38(ctx, base);
}

// Bind + listen: the game session's port 1000 needs root on Linux; listen on
// ours. (The Live session layer's own listeners are left alone.)
extern "C" REX_FUNC(__imp__sub_82420F20);
extern "C" REX_FUNC(sub_82420F20) {
  if (Active() && ctx.r4.u16 == kGameSessionPort) ctx.r4.u64 = tb2::online::kSessionPort;
  [[clang::musttail]] return __imp__sub_82420F20(ctx, base);
}

// Per-tick socket pump. Joiner: receiving opcode 5 would arm session+0x58 = 2,
// but its handler also joins a real Live XSession (crashes on the SDK's
// stand-in). Arm it directly once our participant table is sent (+0x58 ==
// 1), so the host's opcode 10 starts the match.
extern "C" REX_FUNC(__imp__sub_82420CB8);
extern "C" REX_FUNC(sub_82420CB8) {
  __imp__sub_82420CB8(ctx, base);
  if (!Active()) return;
  const uint32_t session = LoadGuestU32(base, kSession);
  const uint32_t transport = session ? LoadGuestU32(base, session + 0x60) : 0;
  if (session && transport && LoadGuestU32(base, session + 0x5C) == 2 &&
      LoadGuestU32(base, session + 0x58) == 1)
    StoreGuestU32(base, session + 0x58, 2);
}

// Wire packet dispatcher. An empty type 10 (host accept, payload 0) made the
// joiner parse a participant table from uninitialised stack: skip that parse.
extern "C" REX_FUNC(__imp__sub_823F3878);
extern "C" REX_FUNC(sub_823F3878) {
  if (g_skip_empty_table) {
    g_skip_empty_table = false;
    return;
  }
  [[clang::musttail]] return __imp__sub_823F3878(ctx, base);
}
extern "C" REX_FUNC(__imp__sub_823F7AA0);
extern "C" REX_FUNC(sub_823F7AA0) {
  if (Active()) {
    const uint32_t obj = ctx.r4.u32;
    const uint32_t data = obj ? LoadGuestU32(base, obj) : 0;
    g_skip_empty_table = data && LoadGuestU32(base, data) == 10 && *GuestPtr(base, data + 4) == 0;
  }
  __imp__sub_823F7AA0(ctx, base);
  g_skip_empty_table = false;
}

// The game's connect() wrapper (r4 = sockaddr_in, network byte order). With
// both players on one IP the lockstep link binds UDP 1795 (host) / 1796
// (joiner) but each side connects to its own port, i.e. to itself: when the
// target is this machine, connect to the other one. Two machines both use
// 1795 and are left alone.
extern "C" REX_FUNC(__imp__sub_823CDD10);
extern "C" REX_FUNC(sub_823CDD10) {
  if (Active() && ctx.r4.u32) {
    uint8_t* p = GuestPtr(base, ctx.r4.u32 + 2);  // sin_port
    const uint16_t port = uint16_t(p[0] << 8 | p[1]);
    const uint32_t ip = LoadGuestU32(base, ctx.r4.u32 + 4);
    if ((port == 1795 || port == 1796) && (port == 1796 || IsOwnIp(ip))) {
      const uint16_t to = port == 1795 ? 1796 : 1795;
      p[0] = uint8_t(to >> 8);
      p[1] = uint8_t(to);
      REXLOG_INFO("Online: lockstep link on one machine, port {} -> {}", port, to);
    }
  }
  [[clang::musttail]] return __imp__sub_823CDD10(ctx, base);
}


namespace tb2::online {

bool SignIn(PPCContext& ctx, uint8_t* base) {
  auto accounts = [&] {
    const uint32_t list = LoadGuestU32(base, kAccountList);
    return list ? LoadGuestU32(base, list + 8) : 0;
  };
  // The sign-in processor, as the main loop runs it (our hook injects the
  // account on the first call, builds it on a later one).
  for (int i = 0; i < 8 && !accounts(); ++i) {
    rex::CallFrame frame(ctx);
    sub_823F5F88(frame.ctx, base);
  }
  const bool ok = accounts() != 0;
  REXLOG_INFO("Online: {} before the online menu", ok ? "signed in" : "not signed in yet");
  return ok;
}

}  // namespace tb2::online
