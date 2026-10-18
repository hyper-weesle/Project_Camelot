# Project Camelot

**A native PC port of _Sonic and the Black Knight_ (Wii, 2009), made with static recompilation.**

Project Camelot turns the game's PowerPC code into native x64 code ahead of time, then runs it
on a PC reimplementation of the Wii's system software and graphics hardware. There is no
emulator, no interpreter and no JIT: the game runs as an ordinary Windows program, with its own
launcher for controls and display settings.

> [!IMPORTANT]
> **This repository contains no game code, assets or data from Sonic and the Black Knight.**
> To play, you need your own copy of the **USA release (game ID `RENE8P`)**, dumped from a disc
> you own. The port extracts what it needs from your disc image on your machine, and it refuses
> to start unless the game's executable matches the clean USA build.

---

## Status

Early, but playable. On the current build you can:

- boot past the SEGA logos and intro movies, reach the title screen, create and load saves;
- watch story scenes and play the first stages with Sonic, through the loading screens and back
  to the world map after quitting a stage;
- play on keyboard or any controller SDL3 supports (Xbox, PlayStation, Steam Controller, ...);
  every menu, name entry included, works with the stick or D-pad, so no pointer is needed;
- load every story stage and battle arena, and fight the bosses (checked with automated runs).

Known gaps:

- the planned "modern" 360° movement mode isn't written yet;
- no one has played it start to finish yet. Every stage loads and every boss fight starts, but
  expect bugs deeper in, and please report them (the newest `UserData/Logs` folder helps).

## What's different from the Wii version

- **Runs natively** at the internal resolution you pick, in a window or full screen, with an
  optional 16:9 mode.
- **Launcher** (`SonicBlackKnightLauncher.exe`) that checks and extracts your game data and
  starts the game. You can rebind every keyboard key and controller button there, with Xbox or
  PlayStation button icons, and change display and sound settings.
- **Sword motions are on buttons.** Slash, thrust, Nunchuk shake, guard and the rest each have
  their own button, so no motion controls are needed.
- **Xbox or PlayStation button prompts** in the menus, pause screen and title screen, matching
  the launcher's button icon setting and your bindings.
- **Start works on the title screen** in place of "Press A + B".
- **The Wii Remote strap warning is skipped**, and the **Wii HOME menu is off** so it can't pop up
  by accident. Both can be turned back on.

## Playing

1. Run `SonicBlackKnightLauncher.exe`.
2. On **Play**, pick your disc image (ISO, WBFS, RVZ, WIA, CISO or GCZ). The launcher extracts
   the game files into a `GameData` folder next to the game (about 3.5 GB). Setup.bat does the
   same thing without the launcher.
3. Set up your controls on **Controls**, then press **Play**.

Settings, saves and logs live in the `UserData` folder. The full player guide, including the
default controls, is in [`satbk/release/README.txt`](satbk/release/README.txt).

**Requirements:** Windows 10 or 11 (x64), a GPU with Vulkan or Direct3D 12, and the Microsoft
Visual C++ 2015-2022 Redistributable (x64).

## Building from source

Builds are made on Linux. A native Linux build is used for development and testing, and the
Windows x64 build is cross-compiled with [llvm-mingw](https://github.com/mstorsjo/llvm-mingw).

You need CMake, Ninja, Python 3, the .NET 8 SDK (for the translator) and a C++20 compiler. The
game's files must already be extracted into `GameData/` at the repository root, because the
translator reads the game's `main.dol` and the home-button module from your own copy.

```sh
# 1. Rebuild the runtime sources from upstream WiiCompiled + the SatBK patches
./tools/satbk_prepare_runtime.sh

# 2. Translate your main.dol into C++ (checks it is the clean USA build first)
./tools/satbk_translate.sh

# 3a. Linux build
cmake -S runtime -B native-build -G Ninja
ninja -C native-build WiiCompiled

# 3b. Windows x64 cross-build
cmake -S runtime -B win-build -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=tools/mingw-toolchain.cmake -DLLVM_MINGW_ROOT=/path/to/llvm-mingw
ninja -C win-build WiiCompiled.exe
```

`tools/satbk_devloop.sh` runs steps 1-3a in one go (set `NO_TRANSLATE=1` to skip translation
when no native override was added or removed). The launcher builds from `satbk/launcher/` with
CMake and SDL3. The disc extractor in `satbk/extract/` is built against
[nod](https://github.com/encounter/nod).

### Where things live

| Path | What it is |
| --- | --- |
| `projects/satbk/` | Translation project for the game: function map and recompiler settings |
| `satbk/runtime_src/` | Port-specific runtime code: SDK functions this game uses, input, data loading, dev tools |
| `satbk/launcher/` | The launcher (SDL3 + Dear ImGui) |
| `satbk/extract/` | Disc image extractor |
| `satbk/release/` | Files that ship next to the game: `README.txt`, `Setup.bat` |
| `tools/satbk_*.py`, `tools/satbk_*.sh` | Address remapping from Mario Kart Wii to this game, runtime patches and build scripts |
| `runtime/`, `aurora-main/`, `translator/` | The WiiCompiled runtime, graphics layer and recompiler (from upstream, with the patches applied) |

## Credits

Project Camelot is a fork of **[WiiCompiled](https://github.com/patchzyy/wiicompiled) by patchzyy
and contributors**, a static recompilation of Mario Kart Wii. WiiCompiled provides everything
this port is built on: the PowerPC-to-C++ translator, the native reimplementation of the Wii
SDK and hardware, and the overall approach. Project Camelot retargets that work at a different
game. The first commit in this repository is an import of WiiCompiled at upstream commit
`bd219e3`, and WiiCompiled's original README is kept at
[`docs/WiiCompiled-README.md`](docs/WiiCompiled-README.md). Please support the original project.

Also built on:

- **[aurora](https://github.com/encounter/aurora)** by encounter: the GameCube/Wii graphics and
  system compatibility layer, running on Dawn/WebGPU.
- **[nod](https://github.com/encounter/nod)** by encounter: reading Wii disc images.
- **[SDL3](https://github.com/libsdl-org/SDL)** and **[Dear ImGui](https://github.com/ocornut/imgui)**
  for the launcher, input and windowing.
- The other third-party libraries listed in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

## License and disclaimer

Licensed under the GNU GPL v3, the same as WiiCompiled. See [LICENSE](LICENSE).

Project Camelot is a non-commercial fan project. It is not affiliated with, endorsed by or
sponsored by SEGA or Nintendo. _Sonic and the Black Knight_ and Sonic the Hedgehog are trademarks
of SEGA, and Wii is a trademark of Nintendo. No copyrighted game content is distributed here.
