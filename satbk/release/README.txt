Sonic and the Black Knight - PC port (static recompilation)
============================================================

This folder contains NO game data. The game runs from files extracted from
your own disc image of Sonic and the Black Knight (USA, RENE8P).

Getting started
  Run SonicBlackKnightLauncher.exe.
    Play              - shows whether your game data is set up, extracts it from
                        your disc image (ISO / WBFS / RVZ / WIA / CISO / GCZ) if
                        not, and starts the game.
    Controls          - rebind every keyboard key and controller button: click a
                        binding, then press the key or button. Esc cancels,
                        Backspace clears. Held keys/buttons light up so you can
                        test them. Changes are saved immediately. Controller
                        buttons are shown as Xbox or PlayStation icons (auto-
                        detected, or pick one at the top of the page).
    Display & sound   - window mode, internal resolution, widescreen, FPS
                        counter and volume.
  You can still start SonicBlackKnight.exe directly, and Setup.bat still works
  for extracting the disc without the launcher.

The game checks GameData\sys\main.dol against the clean USA build and refuses
to start if it is missing or different.

Requirements
  - Windows 10/11 x64, a GPU with Vulkan or Direct3D 12.
  - Microsoft Visual C++ 2015-2022 Redistributable (x64) - most PCs already have it.

Default controls
  Keyboard: WASD move (Nunchuk stick), Space jump (A), J sword slash,
  K thrust, L = B, Left Shift guard (Nunchuk Z), E = Nunchuk C,
  Q shake Nunchuk, Enter = +, arrow keys = D-pad, I / O = 1 / 2,
  M = -, N = HOME.  On the title screen, Start (Enter, or Start on a
  controller) works in place of "A + B".
  Controller: left stick moves, A jump, X slash, Y thrust, B = B,
  LT guard, RT = Nunchuk C, RB shake, Start = +, D-pad = D-pad,
  right stick up/down = 1/2, left/right = - / HOME.

The Wii HOME menu is turned off, so the HOME button (N / right stick right)
does nothing. To turn it back on, tick "Allow the Wii HOME menu" on the
launcher's Controls page, or add  home_menu = true  under [game] in
UserData\Config.toml.

The Wii Remote strap safety screen at startup is skipped. To see it again, add
  [game]
  show_strap_screen = true
to UserData\Config.toml.

Settings, saves and logs are stored in the UserData folder (portable.txt keeps
them next to the executable). Controls live in UserData\Config.toml under
[keyboard] and [controller].

This is an early build. If something breaks, the newest folder in
UserData\Logs has a console.log that helps track it down.
