// thebigs2 - online play (system link over IP) through the game's own XBOX LIVE
// menus.
//
// The game has no dedicated server: two consoles met through Xbox Live
// matchmaking and then played deterministic lockstep (each side sends its pad
// input every frame). Xbox Live is gone, so online.cpp stands in for the parts
// the game waits for: a signed-in Live account, the Quick Match search (the
// joiner gets one result: the host's address), hosting a Game Room, the
// per-player stats lookup and the roster download. The game's own
// peer-to-peer code does the rest. Ported from MatRanc's online fork
// (github.com/MatRanc/The-Bigs-2-Recompiled, branch online, docs/ONLINE.md).
//
// Nothing here is on until the player picks Host or Join in the prompt that
// ONLINE (the game's XBOX LIVE entry) on the main menu opens (online_menu.cpp); from then on, for the
// rest of the run, the profile counts as signed in to Xbox Live.
//
// Network: TCP 51000 (Game Room, the host listens) and UDP 1795 (the lockstep
// link, both ways; 1796 for the second instance on one PC).

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <rex/ppc/context.h>

namespace tb2::online {

enum class Role { kNone, kHost, kJoin };

inline constexpr uint16_t kSessionPort = 51000;  // TCP, Game Room
inline constexpr uint16_t kGamePort = 1795;      // UDP, lockstep input
inline constexpr size_t kMaxNameLength = 8;      // the Game Room shows 8 characters

// The role picked last (kNone until the first pick).
Role CurrentRole();
// True once a role has been picked in this run.
bool Active();

// Picks a role: turns on the Xbox Live stand-in (SDK fake_xbox_live,
// user_name) with this player name; peer_ip (host byte order) is the host's
// address when joining. Any thread.
void Start(Role role, const std::string& name, uint32_t peer_ip);

// On the game thread after Start, before the game's online menu opens: builds
// the stand-in account now, so the menu finds the player signed in (it would
// start a Live sign-in of its own that never ends). False if it isn't there
// yet.
bool SignIn(PPCContext& ctx, uint8_t* base);

// "" if the name can be used, else why not.
std::string CheckName(const std::string& name);
// A default player name (the OS user name, cut to 8 characters).
std::string DefaultName();
// This machine's IPv4 addresses, for the host to give to the other player
// (LAN, VPNs such as Tailscale), most likely first.
std::vector<std::string> LocalAddresses();
// An IPv4 address or a host name (looked up) -> host byte order. Blocking.
bool ResolvePeer(const std::string& text, uint32_t& ip, std::string& error);
std::string FormatIp(uint32_t ip);

}  // namespace tb2::online
