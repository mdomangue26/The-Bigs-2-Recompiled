# Building The Bigs 2

The Linux and Steam Deck AppImages and the Windows zip (cross-compiled) are
built on **Linux** (tested on Ubuntu with Clang 20). The Mac app is built on
an **Apple Silicon Mac** (see [macOS](#macos-apple-silicon)).

Overview:

1. Build and install the ReXGlue SDK.
2. Put your extracted game files in `game/`.
3. Build the game (the recompiler runs as part of the build).
4. Package it: AppImage, Steam Deck AppImage, Windows zip, Mac app.

## 1. The ReXGlue SDK

Requires CMake 3.25+, Ninja, **Clang 20+**, and Vulkan headers **1.4.313** or
newer with the Vulkan Memory Allocator header: the
[LunarG Vulkan SDK](https://vulkan.lunarg.com/sdk/home) (source its
`setup-env.sh` before configuring), or a distribution's `libvulkan-dev` that
new plus `libvulkan-memory-allocator-dev` (or point
`-DREXGLUE_VMA_INCLUDE_DIR` at a folder with `vk_mem_alloc.h`).

```bash
sudo apt install clang-20 lld-20 ninja-build cmake pkg-config nasm \
  libgtk-3-dev libx11-xcb-dev libxcb1-dev libxcb-xkb-dev libxcb-randr0-dev \
  libxcb-keysyms1-dev libxcb-icccm4-dev libxcb-xfixes0-dev libxcb-shape0-dev \
  libxss-dev libxrandr-dev libxcursor-dev libxi-dev libxinerama-dev libxtst-dev \
  libxkbcommon-dev libwayland-dev wayland-protocols libegl-dev libdrm-dev \
  libgbm-dev libudev-dev libdbus-1-dev libpipewire-0.3-dev libasound2-dev \
  libpulse-dev libvulkan-dev curl cabextract
git clone --recursive -b fix/stability-next https://github.com/mdomangue26/rexglue-sdk.git
cd rexglue-sdk
cmake --preset linux-amd64 -DCMAKE_C_COMPILER=clang-20 -DCMAKE_CXX_COMPILER=clang++-20
cmake --build out/build/linux-amd64 --config Release
cmake --install out/build/linux-amd64 --config Release --prefix ~/rexsdk-install
```

Configure the game with `-DCMAKE_PREFIX_PATH=~/rexsdk-install` (or wherever
the SDK is installed), or with `-DREXSDK_DIR=/path/to/rexglue-sdk` to build the
SDK from source together with the game.

### SDK fixes

The SDK above is the port's own fork: the upstream SDK's `development` branch
(at 5f06b92f) plus the fixes below, on the branch **`fix/stability-next`**
([mdomangue26/rexglue-sdk](https://github.com/mdomangue26/rexglue-sdk/tree/fix/stability-next)).
Use it. (The older `fix/stability` branch, on an older upstream, builds the
current releases but not this version of the game: the SDK's API changed.)

| Fix | Without it |
|---|---|
| Sort `.eh_frame_hdr` after linking with lld | With lld 21, every C++ exception aborts: the game closes within seconds |
| Request vertex buffer ranges on every draw | Stale geometry |
| Sample cube maps with an LOD from the gradients | NVIDIA GPU hang at 2x |
| Bulk register writes, cheaper per-draw constants | `frame_rate = 60` can't hold 60 fps |
| Lock-free shared memory validity check per draw | GPU thread stalls on game writes |
| Frame pacing: each frame presented 5 ms after its guest vblank | Uneven frame times |
| Libraries pinned to older glibc symbol versions | Built on a new distro, it doesn't run on SteamOS |
| 4xMSAA depth resolve fix | Dashed streaks on players at 1x |
| Thread names cut to 15 characters on Linux | Threads keep the process name |
| No build stamp under the F3 overlay | Debug text over the bottom of the screen |
| `EDRAM path:` log line | No way to tell whether the GPU runs the `fsi` path |
| Controller button remapping (`hid.sdl.padbind_*`) | The settings menu's Controller tab does nothing |
| Online play (from [MatRanc's SDK fork](https://github.com/MatRanc/rexglue-sdk/tree/tb2-online)), on top of the upstream socket fixes: socket ioctls and one-byte options, `WSARecv` / `WSASend`, `XNetXnAddrToInAddr`, Live startup notifications, an empty friends list, the offline stats lookup, and the `kernel.fake_xbox_live` / `kernel.user_name` settings the game switches on at ONLINE | ONLINE can't sign in or connect |

## 2. Game files

Extract your disc image so that `game/default.xex` exists next to the game's
data folders:

```bash
extract-xiso -x TheBigs2.iso -d game
# or: xdvdfs unpack TheBigs2.iso game
```

Full disc dumps (Redump-style) and XISOs both work. The port is made for
Title ID 54540830, XEX version 0.0.0.3. The build records the MD5 of this
`default.xex`, and the setup screen only accepts disc images with the same
one.

The `game/` folder is git-ignored. Never commit game files or
`generated/default` (code generated from the game).

## 3. Build the game

```bash
cmake --preset linux-amd64-release -DCMAKE_CXX_COMPILER=clang++-20 -DCMAKE_PREFIX_PATH=~/rexsdk-install
cmake --build --preset linux-amd64-release
./out/build/linux-amd64-release/thebigs2
```

- **On the first build, run both commands twice.** The first pass generates
  the recompiled sources, and the list of them is read at configure time.
  Otherwise the link fails with `undefined reference to kPPCImageConfig`.
- If the build stops with `thebigs2_pch.h has been modified since the
  precompiled header was built`, run the build again.
- Recompilation reruns automatically when `game/default.xex` or a file in
  `config/` changes.

Logs are in `out/build/linux-amd64-release/logs/`.

## 4. Packaging

### Linux AppImage

```bash
packaging/build-appimage.sh                     # out/appimage/TheBigs2-x86_64.AppImage
PROFILE=steamdeck packaging/build-appimage.sh   # out/appimage/TheBigs2-SteamDeck-x86_64.AppImage
```

The script downloads `appimagetool` the first time. It refuses binaries that
need a glibc newer than 2.38, so the AppImage runs on older distros and
SteamOS. Building on a newer distro works: `src/glibc_compat.h` and the SDK's
glibc fix pin the symbol versions down.

The Steam Deck AppImage is the same binary; its launcher sets
`TB2_PROFILE=steamdeck`, which picks Deck defaults (1x, host render targets,
the game's own texture filtering, no keyboard as player 1, the game's MSAA
off).

### Windows zip (cross-compiled on Linux)

> With `fix/stability-next`, the SDK's Windows build also needs the Windows
> Vulkan headers and import library (`-DVulkan_INCLUDE_DIR=<dir>/include
> -DVulkan_LIBRARY=<dir>/lib/vulkan-1.lib`) and, as on Linux,
> `-DREXGLUE_VMA_INCLUDE_DIR`. It builds Vulkan and D3D12 by default (the
> `REXGLUE_USE_*` options below are from the older SDK).

```bash
packaging/build-windows.sh      # out/windows/TheBigs2-windows-x64.zip
```

Before the first run:

1. **The MSVC runtime and Windows SDK** via
   [xwin](https://github.com/Jake-Shadle/xwin) (this accepts Microsoft's
   license):
   ```bash
   xwin --accept-license --arch x86_64 splat --output ~/msvc-sdk
   cd ~/msvc-sdk
   ln -s objbase.h sdk/include/um/ObjBase.h
   ln -s sdkddkver.h sdk/include/shared/SDKDDKVer.h
   ```
   The build looks in `~/msvc-sdk`; set `XWIN_DIR` to use another folder.
2. **The SDK built for Windows**, installed to `~/rexsdk-install-win`:
   ```bash
   cd rexglue-sdk
   cmake -S . -B out/build/win-amd64-cross -G Ninja -DCMAKE_BUILD_TYPE=Release \
     -DCMAKE_TOOLCHAIN_FILE=/path/to/thebigs2/packaging/windows/clang-msvc-cross.cmake \
     -DREXGLUE_USE_VULKAN=ON -DREXGLUE_USE_D3D12=ON \
     -DCMAKE_INSTALL_PREFIX=$HOME/rexsdk-install-win
   cmake --build out/build/win-amd64-cross && cmake --install out/build/win-amd64-cross
   rm ~/.cmake/packages/rexglue/$(echo -n ~/rexsdk-install-win | md5sum | cut -c1-32)
   ```
   The last line removes the Windows SDK from CMake's package registry, so a
   Linux build doesn't pick it up. Don't pass `CMAKE_C(XX)_FLAGS`: the
   toolchain file keeps its include paths there.

The script recompiles with the Linux SDK's `rexglue` and bundles the Visual
C++ runtime DLLs (downloaded from Microsoft once, unpacked with `cabextract`).
Those DLLs are needed: with an older `msvcp140.dll`, matches run at 1-2 fps.
Under Wine, use them with
`WINEDLLOVERRIDES="msvcp140,msvcp140_atomic_wait,vcruntime140,vcruntime140_1=n"`.

The `win-amd64-release` preset is for building natively on Windows with
Visual Studio 2022 and its Clang tools. It hasn't been tested.

## macOS (Apple Silicon)

Built on the Mac itself, natively for arm64 (the build runs the recompiler,
which needs the game's `default.xex`, so it can't run in CI).

1. macOS 13.3 or newer, **Xcode 26** or newer (AppleClang 17), and
   `brew install cmake ninja`.
2. The [LunarG Vulkan SDK](https://vulkan.lunarg.com/sdk/home) for macOS
   (Vulkan loader and MoltenVK). In every shell you build from:
   `source ~/VulkanSDK/<version>/setup-env.sh`.
3. The SDK:
   ```bash
   git clone --recursive -b fix/stability-next https://github.com/mdomangue26/rexglue-sdk.git
   cd rexglue-sdk
   cmake --preset mac-arm64
   cmake --build out/build/mac-arm64 --config Release
   cmake --install out/build/mac-arm64 --config Release --prefix ~/rexsdk-install
   ```
4. The game files in `game/` (step 2 above), then:
   ```bash
   packaging/build-macos.sh        # out/macos/TheBigs2-macos-arm64.zip
   ```
   It configures and builds the `mac-arm64-release` preset (twice on a fresh
   checkout), puts the executable, the SDK libraries, SDL3 and `vulkan/`
   (loader, MoltenVK) into `The Bigs 2.app`, signs it ad hoc and zips it.
   `REXSDK=<prefix>` picks another SDK install.

To run without packaging: `./out/build/mac-arm64-release/thebigs2`, with the
`game/` folder in the working directory. Logs are then in the build folder's
`logs/` (the app writes them to `~/Library/Logs/thebigs2`).

## Project layout

| Path | What it is |
|---|---|
| `thebigs2_manifest.toml` | Recompiler entry point |
| `config/codegen.toml`, `config/overrides.toml` | Recompiler settings and hand-written fixes (function bounds, jump tables) |
| `src/` | The port's own code: the app (`main.cpp`), the game's settings and their old flat names (`game_settings.cpp`), game fixes (`hooks.cpp`), rendering tweaks, settings and the settings menu, main menu entries, setup screen, online play (`online.cpp`, the ONLINE prompt `online_menu.cpp`; see [ONLINE.md](ONLINE.md)) |
| `assets/fonts/` | Roboto (Apache 2.0), Barlow Condensed (SIL OFL 1.1) |
| `packaging/` | AppImage, Windows and Mac packaging |
| `tools/` | Analysis scripts and test scripts (`atbat.sh` plays to the first at-bat for screenshots, `perf/` benchmarks) |
| `game/` | Your extracted game files (git-ignored) |
| `generated/default` | Recompiler output (regenerated, git-ignored) |

`config/codegen.toml` has two settings that must stay `false`: `skip_msr` and
`non_argument_as_local`. Turning either on makes the game hang.
