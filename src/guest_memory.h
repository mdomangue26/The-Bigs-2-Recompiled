// thebigs2 - guest memory and guest call helpers for hand-written hooks.
#pragma once

#include <cstdint>

#include <rex/memory/mapped_ptr.h>
#include <rex/ppc/context.h>

// Guest address -> host pointer with the arithmetic the generated code uses. On hosts whose MMU
// page is above 4 KB (Apple Silicon: 16 KB) the 0xE0000000 physical view is mapped without its
// 4 KB offset and code adds it on every access; hooks must do the same, or anything in
// 0xE0000000+ (file buffers, e.g. the unpacked main menu) is read 4 KB off.
inline uint8_t* GuestPtr(uint8_t* base, uint32_t addr) {
  return rex::memory::GuestPtr(base, addr);
}

namespace tb2 {

// An isolated register file for a side call into guest code from a hook: a copy of the caller's
// (so the thread state and detour chain come along) whose registers the callee may clobber. The
// FPSCR, which the callee may change for the thread, is copied back.
struct CallFrame {
  PPCContext ctx;
  PPCContext& parent_;

  explicit CallFrame(PPCContext& parent) : ctx(parent), parent_(parent) {}
  ~CallFrame() { parent_.fpscr = ctx.fpscr; }

  CallFrame(const CallFrame&) = delete;
  CallFrame& operator=(const CallFrame&) = delete;
};

}  // namespace tb2
