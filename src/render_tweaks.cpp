// thebigs2 - main scene MSAA / predicated tiling; the depth of field NaN fix.
//
// At startup the game (sub_82219148) sets up its main scene render pass
// (engine render-pass table entry 5) with 4x MSAA. A 1280x720 frame at 4x MSAA
// doesn't fit the Xbox 360's 10 MB EDRAM, so it also enables predicated tiling
// with 3 tiles (240-row bands): every frame the GPU replays the scene's
// command buffer once per tile. Emulated, that triples the command processor's
// work for the scene (about 3,800 draws a frame) and renders 4x MSAA. Without
// MSAA, 1280x720 colour + depth (7.4 MB) fits in EDRAM in one piece, so the
// game needs no tiling at all.
//
// Engine setters (renderlib/xbox360), entry = this->table(+3616) + index * 348:
//   sub_82491BF8(this, index, count): +264 tile count, +20 tiling on (count != 0)
//   sub_82491CB8(this, index, type):  +268 multisample type (0 none, 1 2x, 2 4x)
//   sub_82491C20(this, index, tile, ...): tile rectangles (unused without tiling)

#include <atomic>
#include <cstring>

#include <rex/logging/log_category.h>
#include <rex/ppc/context.h>
#include <rex/ppc/recompiled_code.h>

#include "game_settings.h"
#include "guest_memory.h"

// tb2.scene_msaa: main scene 4x MSAA as on the Xbox 360, or none (no 3-tile rendering either).
// Which EDRAM emulation path the GPU emulation uses is logged by the SDK ("EDRAM path: ...").

extern "C" REX_FUNC(__imp__sub_82491BF8);
extern "C" REX_FUNC(sub_82491BF8) {
  if (!tb2::Settings().scene_msaa.Get() && ctx.r5.u32 > 1) {
    REXLOG_INFO("[render] entry {}: predicated tiling ({} tiles) off", ctx.r4.u32, ctx.r5.u32);
    ctx.r5.u64 = 0;
  }
  [[clang::musttail]] return __imp__sub_82491BF8(ctx, base);
}

extern "C" REX_FUNC(__imp__sub_82491CB8);
extern "C" REX_FUNC(sub_82491CB8) {
  if (!tb2::Settings().scene_msaa.Get() && ctx.r5.u32 != 0) {
    REXLOG_INFO("[render] entry {}: multisample type {} -> none", ctx.r4.u32, ctx.r5.u32);
    ctx.r5.u64 = 0;
  }
  [[clang::musttail]] return __imp__sub_82491CB8(ctx, base);
}

// Depth of field blur at a camera cut (the strike-out cameras): on the
// effect's first frame the game's blur radius is 0 and its 16 Gaussian
// weights come out 0/0 = NaN (pixel shader c65-c68, sample offsets c69-c76
// all 0). Both blur passes then write 0 and the frame's composite comes out
// black. A zero-radius blur is a copy: equal weights over the 16 taps (at
// offset 0) make it one. SetPixelShaderConstantF = sub_823BC8E8 (r3 device,
// r4 first register, r5 data, r6 register count); the data is the engine's
// per-frame material constant block (sub_82492B08).
extern "C" REX_FUNC(__imp__sub_823BC8E8);
extern "C" REX_FUNC(sub_823BC8E8) {
  if (ctx.r4.u32 == 65 && ctx.r6.u32 == 4) {
    auto at = [&](uint32_t i) {
      return GuestPtr(base, ctx.r5.u32 + i * 4);
    };
    bool all_nan = true;
    for (uint32_t i = 0; i < 16 && all_nan; ++i) {
      uint32_t v;
      std::memcpy(&v, at(i), 4);
      v = __builtin_bswap32(v);
      all_nan = (v & 0x7F800000u) == 0x7F800000u && (v & 0x7FFFFFu);
    }
    if (all_nan) {
      static std::atomic<int> logged{0};
      if (logged.fetch_add(1) < 5)
        REXLOG_INFO("[render] depth of field blur with NaN weights (radius 0): made a copy");
      const uint32_t sixteenth = __builtin_bswap32(0x3D800000u);  // 1/16
      for (uint32_t i = 0; i < 16; ++i) std::memcpy(at(i), &sixteenth, 4);
    }
  }
  [[clang::musttail]] return __imp__sub_823BC8E8(ctx, base);
}
