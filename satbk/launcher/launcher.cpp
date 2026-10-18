// Sonic and the Black Knight PC - launcher.
//
// Lets the player start the game, extract their own disc into GameData, and set up the
// controls without opening Config.toml. Everything it changes is written to the same
// Config.toml the game reads at startup:
//   [keyboard]   one SDL key name per game input (read by the port's input layer)
//   [controller] the GameCube-style pad slots the port maps onto the Wii Remote/Nunchuk
//   [video] / [audio]  a few display and volume settings
#include "config_file.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#else
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

#include "launcher_font.h" // kLauncherFont / kLauncherFontSize (Roboto Medium, Apache-2.0)

namespace fs = std::filesystem;

namespace {

std::string U8(const fs::path& path) {
    const auto text = path.u8string();
    return std::string(text.begin(), text.end());
}

// ---------------------------------------------------------------------------------------------
// Paths
// ---------------------------------------------------------------------------------------------

fs::path ExecutableDirectory() {
    const char* base = SDL_GetBasePath();
    return base ? fs::u8path(base) : fs::current_path();
}

#ifdef _WIN32
constexpr const char* kGameExecutable = "SonicBlackKnight.exe";
constexpr const char* kExtractorExecutable = "tools\\satbk_extract.exe";
#else
constexpr const char* kGameExecutable = "WiiCompiled";
constexpr const char* kExtractorExecutable = "tools/satbk_extract";
#endif

// Mirrors the runtime: portable.txt next to the game keeps settings in UserData beside it.
fs::path ConfigPath() {
    const fs::path exeDir = ExecutableDirectory();
    std::error_code ec;
    if (fs::is_regular_file(exeDir / "portable.txt", ec)) {
        return exeDir / "UserData" / "Config.toml";
    }
#ifdef _WIN32
    PWSTR raw = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &raw)) && raw) {
        fs::path dir = fs::path(raw) / "SonicBlackKnightPC";
        CoTaskMemFree(raw);
        return dir / "Config.toml";
    }
#else
    if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg) {
        return fs::path(xdg) / "SonicBlackKnightPC" / "Config.toml";
    }
    if (const char* home = std::getenv("HOME"); home && *home) {
        return fs::path(home) / ".local" / "share" / "SonicBlackKnightPC" / "Config.toml";
    }
#endif
    return exeDir / "UserData" / "Config.toml";
}

// ---------------------------------------------------------------------------------------------
// Child processes (the game and the disc extractor)
// ---------------------------------------------------------------------------------------------

struct ChildProcess {
#ifdef _WIN32
    HANDLE handle = nullptr;
#else
    pid_t pid = -1;
#endif
    bool Running() const {
#ifdef _WIN32
        return handle != nullptr;
#else
        return pid > 0;
#endif
    }
};

#ifdef _WIN32
std::wstring Widen(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int n = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), n);
    return out;
}

std::wstring QuoteArg(const std::wstring& arg) {
    std::wstring out = L"\"";
    size_t slashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        if (c == L'"') {
            out.append(slashes * 2 + 1, L'\\');
        } else {
            out.append(slashes, L'\\');
        }
        slashes = 0;
        out += c;
    }
    out.append(slashes * 2, L'\\');
    return out + L"\"";
}
#endif

bool StartProcess(const fs::path& exe, const std::vector<std::string>& args, bool newConsole, ChildProcess* out,
                  std::string& error) {
    std::error_code ec;
    if (!fs::is_regular_file(exe, ec)) {
        error = "Not found: " + U8(exe);
        return false;
    }
#ifdef _WIN32
    std::wstring cmd = QuoteArg(exe.wstring());
    for (const std::string& a : args) {
        cmd += L" " + QuoteArg(Widen(a));
    }
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    const std::wstring dir = exe.parent_path().wstring();
    if (!CreateProcessW(exe.wstring().c_str(), cmd.data(), nullptr, nullptr, FALSE,
                        newConsole ? CREATE_NEW_CONSOLE : 0, nullptr, dir.c_str(), &si, &pi)) {
        error = "Could not start " + U8(exe.filename()) + " (error " + std::to_string(GetLastError()) + ")";
        return false;
    }
    CloseHandle(pi.hThread);
    if (out) {
        out->handle = pi.hProcess;
    } else {
        CloseHandle(pi.hProcess);
    }
    return true;
#else
    (void)newConsole;
    std::vector<std::string> storage{exe.string()};
    storage.insert(storage.end(), args.begin(), args.end());
    std::vector<char*> argv;
    for (auto& s : storage) {
        argv.push_back(s.data());
    }
    argv.push_back(nullptr);
    pid_t pid = -1;
    const std::string cwd = exe.parent_path().string();
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addchdir_np(&actions, cwd.c_str());
    const int rc = posix_spawn(&pid, exe.c_str(), &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    if (rc != 0) {
        error = "Could not start " + exe.filename().string();
        return false;
    }
    if (out) {
        out->pid = pid;
    }
    return true;
#endif
}

// Returns the exit code once the process has finished, nullopt while it runs.
std::optional<int> PollProcess(ChildProcess& process) {
#ifdef _WIN32
    if (!process.handle) {
        return std::nullopt;
    }
    if (WaitForSingleObject(process.handle, 0) != WAIT_OBJECT_0) {
        return std::nullopt;
    }
    DWORD code = 1;
    GetExitCodeProcess(process.handle, &code);
    CloseHandle(process.handle);
    process.handle = nullptr;
    return static_cast<int>(code);
#else
    if (process.pid <= 0) {
        return std::nullopt;
    }
    int status = 0;
    if (waitpid(process.pid, &status, WNOHANG) != process.pid) {
        return std::nullopt;
    }
    process.pid = -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
#endif
}

// ---------------------------------------------------------------------------------------------
// Controls
// ---------------------------------------------------------------------------------------------

// One physical controller button, by the name Config.toml [controller] uses.
struct PadButton {
    const char* configName;
    SDL_GamepadButton button; // SDL_GAMEPAD_BUTTON_INVALID for the triggers
    SDL_GamepadAxis axis;     // the trigger axis, else SDL_GAMEPAD_AXIS_INVALID
};

constexpr PadButton kPadButtons[] = {
    {"south", SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_AXIS_INVALID},
    {"east", SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_AXIS_INVALID},
    {"west", SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_AXIS_INVALID},
    {"north", SDL_GAMEPAD_BUTTON_NORTH, SDL_GAMEPAD_AXIS_INVALID},
    {"back", SDL_GAMEPAD_BUTTON_BACK, SDL_GAMEPAD_AXIS_INVALID},
    {"guide", SDL_GAMEPAD_BUTTON_GUIDE, SDL_GAMEPAD_AXIS_INVALID},
    {"start", SDL_GAMEPAD_BUTTON_START, SDL_GAMEPAD_AXIS_INVALID},
    {"left_stick", SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_AXIS_INVALID},
    {"right_stick", SDL_GAMEPAD_BUTTON_RIGHT_STICK, SDL_GAMEPAD_AXIS_INVALID},
    {"left_shoulder", SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_AXIS_INVALID},
    {"right_shoulder", SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, SDL_GAMEPAD_AXIS_INVALID},
    {"dpad_up", SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_AXIS_INVALID},
    {"dpad_down", SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_AXIS_INVALID},
    {"dpad_left", SDL_GAMEPAD_BUTTON_DPAD_LEFT, SDL_GAMEPAD_AXIS_INVALID},
    {"dpad_right", SDL_GAMEPAD_BUTTON_DPAD_RIGHT, SDL_GAMEPAD_AXIS_INVALID},
    {"misc1", SDL_GAMEPAD_BUTTON_MISC1, SDL_GAMEPAD_AXIS_INVALID},
    {"right_paddle1", SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1, SDL_GAMEPAD_AXIS_INVALID},
    {"left_paddle1", SDL_GAMEPAD_BUTTON_LEFT_PADDLE1, SDL_GAMEPAD_AXIS_INVALID},
    {"right_paddle2", SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2, SDL_GAMEPAD_AXIS_INVALID},
    {"left_paddle2", SDL_GAMEPAD_BUTTON_LEFT_PADDLE2, SDL_GAMEPAD_AXIS_INVALID},
    {"touchpad", SDL_GAMEPAD_BUTTON_TOUCHPAD, SDL_GAMEPAD_AXIS_INVALID},
    {"left_trigger", SDL_GAMEPAD_BUTTON_INVALID, SDL_GAMEPAD_AXIS_LEFT_TRIGGER},
    {"right_trigger", SDL_GAMEPAD_BUTTON_INVALID, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER},
};

const PadButton* FindPadButton(const std::string& name) {
    const std::string base = name.substr(0, name.find_first_of("@,"));
    for (const PadButton& b : kPadButtons) {
        if (base == b.configName) {
            return &b;
        }
    }
    return nullptr;
}

enum class PadStyle { Xbox, PlayStation, Nintendo };

PadStyle StyleFor(SDL_Gamepad* pad) {
    if (!pad) {
        return PadStyle::Xbox;
    }
    switch (SDL_GetGamepadType(pad)) {
    case SDL_GAMEPAD_TYPE_PS3:
    case SDL_GAMEPAD_TYPE_PS4:
    case SDL_GAMEPAD_TYPE_PS5:
        return PadStyle::PlayStation;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
    case SDL_GAMEPAD_TYPE_GAMECUBE:
        return PadStyle::Nintendo;
    default:
        return PadStyle::Xbox;
    }
}

std::string PadButtonLabel(const std::string& name, PadStyle style) {
    struct Label {
        const char* name;
        const char* xbox;
        const char* ps;
        const char* nintendo;
    };
    static constexpr Label labels[] = {
        {"south", "A", "Cross", "B"},
        {"east", "B", "Circle", "A"},
        {"west", "X", "Square", "Y"},
        {"north", "Y", "Triangle", "X"},
        {"back", "View / Back", "Create / Share", "Minus"},
        {"guide", "Guide", "PS button", "Home"},
        {"start", "Menu / Start", "Options", "Plus"},
        {"left_stick", "Left stick click", "L3", "Left stick click"},
        {"right_stick", "Right stick click", "R3", "Right stick click"},
        {"left_shoulder", "LB", "L1", "L"},
        {"right_shoulder", "RB", "R1", "R"},
        {"left_trigger", "LT", "L2", "ZL"},
        {"right_trigger", "RT", "R2", "ZR"},
        {"dpad_up", "D-pad Up", "D-pad Up", "D-pad Up"},
        {"dpad_down", "D-pad Down", "D-pad Down", "D-pad Down"},
        {"dpad_left", "D-pad Left", "D-pad Left", "D-pad Left"},
        {"dpad_right", "D-pad Right", "D-pad Right", "D-pad Right"},
        {"misc1", "Share", "Mic", "Capture"},
        {"right_paddle1", "Paddle P1", "Right paddle 1", "Right paddle 1"},
        {"left_paddle1", "Paddle P3", "Left paddle 1", "Left paddle 1"},
        {"right_paddle2", "Paddle P2", "Right paddle 2", "Right paddle 2"},
        {"left_paddle2", "Paddle P4", "Left paddle 2", "Left paddle 2"},
        {"touchpad", "Touchpad", "Touchpad", "Touchpad"},
    };
    if (name.empty() || name == "disabled") {
        return "Not bound";
    }
    const std::string base = name.substr(0, name.find_first_of("@,"));
    for (const Label& l : labels) {
        if (base == l.name) {
            return style == PadStyle::PlayStation ? l.ps : style == PadStyle::Nintendo ? l.nintendo : l.xbox;
        }
    }
    return name;
}

// ---------------------------------------------------------------------------------------------
// Controller button icons (drawn, so they scale with the UI)
// ---------------------------------------------------------------------------------------------

ImU32 Rgb(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }

void CenteredText(ImDrawList* dl, ImFont* font, float size, ImVec2 c, ImU32 col, const char* text) {
    const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.f, text);
    dl->AddText(font, size, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), col, text);
}

// Draws the icon for a [controller] button name centred at c; returns its width.
float DrawPadGlyph(ImDrawList* dl, ImFont* font, ImVec2 c, float r, const std::string& name, PadStyle style,
                   bool held) {
    const std::string base = name.substr(0, name.find_first_of("@,"));
    const ImU32 body = held ? Rgb(70, 90, 130) : Rgb(40, 46, 60);
    const ImU32 dpadBody = held ? Rgb(110, 130, 170) : Rgb(150, 158, 175);
    const ImU32 rim = Rgb(120, 130, 150);
    const float fs = r * 1.15f;
    const bool face = base == "south" || base == "east" || base == "west" || base == "north";
    if (face) {
        dl->AddCircleFilled(c, r, body, 32);
        dl->AddCircle(c, r, rim, 32, 1.2f);
        if (style == PadStyle::PlayStation) {
            const float s = r * 0.48f;
            if (base == "south") { // cross
                const ImU32 col = Rgb(124, 178, 232);
                dl->AddLine(ImVec2(c.x - s, c.y - s), ImVec2(c.x + s, c.y + s), col, r * 0.2f);
                dl->AddLine(ImVec2(c.x - s, c.y + s), ImVec2(c.x + s, c.y - s), col, r * 0.2f);
            } else if (base == "east") { // circle
                dl->AddCircle(c, s * 1.05f, Rgb(255, 102, 102), 24, r * 0.18f);
            } else if (base == "west") { // square
                dl->AddRect(ImVec2(c.x - s * 0.9f, c.y - s * 0.9f), ImVec2(c.x + s * 0.9f, c.y + s * 0.9f),
                            Rgb(255, 105, 248), 0.f, 0, r * 0.18f);
            } else { // triangle
                const ImU32 col = Rgb(64, 226, 160);
                dl->AddTriangle(ImVec2(c.x, c.y - s * 1.05f), ImVec2(c.x - s * 1.05f, c.y + s * 0.7f),
                                ImVec2(c.x + s * 1.05f, c.y + s * 0.7f), col, r * 0.18f);
            }
        } else {
            const char* letter = nullptr;
            ImU32 col = Rgb(230, 230, 230);
            if (style == PadStyle::Nintendo) {
                letter = base == "south" ? "B" : base == "east" ? "A" : base == "west" ? "Y" : "X";
            } else {
                if (base == "south") { letter = "A"; col = Rgb(110, 200, 90); }
                if (base == "east") { letter = "B"; col = Rgb(240, 80, 80); }
                if (base == "west") { letter = "X"; col = Rgb(80, 150, 240); }
                if (base == "north") { letter = "Y"; col = Rgb(245, 200, 60); }
            }
            CenteredText(dl, font, fs, c, col, letter);
        }
        return r * 2.f;
    }
    if (base == "left_shoulder" || base == "right_shoulder" || base == "left_trigger" || base == "right_trigger") {
        const bool left = base[0] == 'l';
        const bool trigger = base.find("trigger") != std::string::npos;
        const char* label = style == PadStyle::PlayStation ? (trigger ? (left ? "L2" : "R2") : (left ? "L1" : "R1"))
                            : style == PadStyle::Nintendo ? (trigger ? (left ? "ZL" : "ZR") : (left ? "L" : "R"))
                                                          : (trigger ? (left ? "LT" : "RT") : (left ? "LB" : "RB"));
        const float w = r * 1.9f, h = trigger ? r * 1.7f : r * 1.25f;
        const ImVec2 a(c.x - w * 0.5f, c.y - h * 0.5f), b(c.x + w * 0.5f, c.y + h * 0.5f);
        dl->AddRectFilled(a, b, body, trigger ? r * 0.7f : r * 0.35f,
                          trigger ? ImDrawFlags_RoundCornersTop : ImDrawFlags_RoundCornersAll);
        dl->AddRect(a, b, rim, trigger ? r * 0.7f : r * 0.35f,
                    trigger ? ImDrawFlags_RoundCornersTop : ImDrawFlags_RoundCornersAll, 1.2f);
        CenteredText(dl, font, fs * 0.8f, c, Rgb(235, 235, 240), label);
        return w;
    }
    if (base.rfind("dpad_", 0) == 0) {
        const float t = r * 0.42f, l = r * 0.95f;
        dl->AddRectFilled(ImVec2(c.x - t, c.y - l), ImVec2(c.x + t, c.y + l), dpadBody, 2.f);
        dl->AddRectFilled(ImVec2(c.x - l, c.y - t), ImVec2(c.x + l, c.y + t), dpadBody, 2.f);
        const ImU32 hi = Rgb(232, 184, 58);
        if (base == "dpad_up") dl->AddRectFilled(ImVec2(c.x - t, c.y - l), ImVec2(c.x + t, c.y - t), hi, 2.f);
        if (base == "dpad_down") dl->AddRectFilled(ImVec2(c.x - t, c.y + t), ImVec2(c.x + t, c.y + l), hi, 2.f);
        if (base == "dpad_left") dl->AddRectFilled(ImVec2(c.x - l, c.y - t), ImVec2(c.x - t, c.y + t), hi, 2.f);
        if (base == "dpad_right") dl->AddRectFilled(ImVec2(c.x + t, c.y - t), ImVec2(c.x + l, c.y + t), hi, 2.f);
        return l * 2.f;
    }
    if (base == "left_stick" || base == "right_stick") {
        dl->AddCircleFilled(c, r, body, 32);
        dl->AddCircle(c, r * 0.62f, rim, 32, 1.5f);
        CenteredText(dl, font, fs * 0.75f, c, Rgb(235, 235, 240),
                     style == PadStyle::PlayStation ? (base[0] == 'l' ? "L3" : "R3") : (base[0] == 'l' ? "LS" : "RS"));
        return r * 2.f;
    }
    // Menu-style buttons and anything else: a pill with a short label.
    std::string label = PadButtonLabel(name, style);
    if (base == "start") label = style == PadStyle::PlayStation ? "OPTIONS" : style == PadStyle::Nintendo ? "+" : "MENU";
    if (base == "back") label = style == PadStyle::PlayStation ? "CREATE" : style == PadStyle::Nintendo ? "-" : "VIEW";
    if (base == "guide") label = style == PadStyle::PlayStation ? "PS" : "HOME";
    const ImVec2 ts = font->CalcTextSizeA(fs * 0.7f, FLT_MAX, 0.f, label.c_str());
    const float w = std::max(r * 2.f, ts.x + r * 0.9f), h = r * 1.3f;
    const ImVec2 a(c.x - w * 0.5f, c.y - h * 0.5f), b(c.x + w * 0.5f, c.y + h * 0.5f);
    dl->AddRectFilled(a, b, body, h * 0.5f);
    dl->AddRect(a, b, rim, h * 0.5f, 0, 1.2f);
    CenteredText(dl, font, fs * 0.7f, c, Rgb(235, 235, 240), label.c_str());
    return w;
}

// The analog stick a fixed row is tied to, with the direction marked.
void DrawStickGlyph(ImDrawList* dl, ImFont* font, ImVec2 c, float r, bool right, const char* dir, bool held,
                    PadStyle style) {
    dl->AddCircleFilled(c, r, held ? Rgb(70, 90, 130) : Rgb(40, 46, 60), 32);
    dl->AddCircle(c, r, Rgb(120, 130, 150), 32, 1.2f);
    ImVec2 o(0, 0);
    if (!std::strcmp(dir, "up")) o.y = -r * 0.42f;
    if (!std::strcmp(dir, "down")) o.y = r * 0.42f;
    if (!std::strcmp(dir, "left")) o.x = -r * 0.42f;
    if (!std::strcmp(dir, "right")) o.x = r * 0.42f;
    dl->AddCircleFilled(ImVec2(c.x + o.x, c.y + o.y), r * 0.45f, Rgb(232, 184, 58), 24);
    (void)font;
    (void)style;
    (void)right;
}

// A game input. `keyboardKey` names the [keyboard] entry; `padKey` the [controller] slot,
// or nullptr when the controller side is fixed (`padFixed` describes it).
struct Action {
    const char* group;
    const char* label;
    const char* wiiInput;
    const char* keyboardKey;
    SDL_Scancode keyboardDefault;
    const char* padKey;
    const char* padDefault;
    const char* padFixed;
};

const std::array<Action, 20> kActions = {{
    {"Movement", "Move forward", "Nunchuk stick", "stick_up", SDL_SCANCODE_W, nullptr, nullptr, "Left stick"},
    {"Movement", "Move back", "Nunchuk stick", "stick_down", SDL_SCANCODE_S, nullptr, nullptr, "Left stick"},
    {"Movement", "Move left", "Nunchuk stick", "stick_left", SDL_SCANCODE_A, nullptr, nullptr, "Left stick"},
    {"Movement", "Move right", "Nunchuk stick", "stick_right", SDL_SCANCODE_D, nullptr, nullptr, "Left stick"},
    {"Actions", "Jump", "A button", "a", SDL_SCANCODE_SPACE, "a", "south", nullptr},
    {"Actions", "Sword slash", "Swing the Wii Remote", "x", SDL_SCANCODE_J, "x", "west", nullptr},
    {"Actions", "Thrust", "Thrust the Wii Remote", "y", SDL_SCANCODE_K, "y", "north", nullptr},
    {"Actions", "B button", "B trigger", "b", SDL_SCANCODE_L, "b", "east", nullptr},
    {"Actions", "Guard", "Nunchuk Z", "l", SDL_SCANCODE_LSHIFT, "l", "left_trigger", nullptr},
    {"Actions", "Nunchuk C", "Nunchuk C", "r", SDL_SCANCODE_E, "r", "right_trigger", nullptr},
    {"Actions", "Shake Nunchuk", "Shake the Nunchuk", "z", SDL_SCANCODE_Q, "z", "right_shoulder", nullptr},
    {"Menus", "Pause", "+ button", "start", SDL_SCANCODE_RETURN, "start", "start", nullptr},
    {"Menus", "D-pad up", "Remote D-pad", "up", SDL_SCANCODE_UP, "up", "dpad_up", nullptr},
    {"Menus", "D-pad down", "Remote D-pad", "down", SDL_SCANCODE_DOWN, "down", "dpad_down", nullptr},
    {"Menus", "D-pad left", "Remote D-pad", "left", SDL_SCANCODE_LEFT, "left", "dpad_left", nullptr},
    {"Menus", "D-pad right", "Remote D-pad", "right", SDL_SCANCODE_RIGHT, "right", "dpad_right", nullptr},
    {"Menus", "1 button", "1 button", "cstick_up", SDL_SCANCODE_I, nullptr, nullptr, "Right stick up"},
    {"Menus", "2 button", "2 button", "cstick_down", SDL_SCANCODE_O, nullptr, nullptr, "Right stick down"},
    {"Menus", "- button", "- button", "cstick_left", SDL_SCANCODE_M, nullptr, nullptr, "Right stick left"},
    {"Menus", "HOME menu", "HOME button", "cstick_right", SDL_SCANCODE_N, nullptr, nullptr, "Right stick right"},
}};
constexpr size_t kActionCount = kActions.size();

enum class Page { Play, Controls, Display };

struct Capture {
    bool active = false;
    bool openPopup = false;
    bool keyboard = true;
    size_t action = 0;
    uint64_t startTicks = 0;
};

struct App {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    ImFont* bodyFont = nullptr;
    ImFont* titleFont = nullptr;
    ImFont* smallFont = nullptr;

    ConfigFile config;
    Page page = Page::Play;
    Capture capture;
    std::vector<SDL_Gamepad*> pads;

    std::string status;
    uint64_t statusUntil = 0;
    bool statusIsError = false;

    ChildProcess extractor;
    std::mutex dialogMutex;
    std::optional<std::string> pickedImage;
    bool dialogOpen = false;

    bool quit = false;
};

void SetStatus(App& app, std::string text, bool error = false) {
    app.status = std::move(text);
    app.statusIsError = error;
    app.statusUntil = SDL_GetTicks() + 4000;
}

std::string KeyboardValue(const App& app, const Action& a) {
    if (auto v = app.config.GetString("keyboard", a.keyboardKey)) {
        return *v;
    }
    return SDL_GetScancodeName(a.keyboardDefault);
}

std::string PadValue(const App& app, const Action& a) {
    if (!a.padKey) {
        return {};
    }
    if (auto v = app.config.GetString("controller", a.padKey)) {
        return *v;
    }
    return a.padDefault;
}

void SaveConfig(App& app) {
    if (!app.config.Save()) {
        SetStatus(app, "Could not save " + U8(app.config.Path()), true);
    } else {
        SetStatus(app, "Saved");
    }
}

void RestoreDefaults(App& app) {
    for (size_t i = 0; i < kActionCount; ++i) {
        const Action& a = kActions[i];
        app.config.SetString("keyboard", a.keyboardKey, SDL_GetScancodeName(a.keyboardDefault));
        if (a.padKey) {
            app.config.SetString("controller", a.padKey, a.padDefault);
        }
    }
    SaveConfig(app);
    SetStatus(app, "Controls restored to the defaults");
}

SDL_Gamepad* FirstPad(const App& app) {
    return app.pads.empty() ? nullptr : app.pads.front();
}

bool PadNameHeld(const App& app, const std::string& name) {
    const PadButton* b = FindPadButton(name);
    if (!b) {
        return false;
    }
    for (SDL_Gamepad* pad : app.pads) {
        if (b->button != SDL_GAMEPAD_BUTTON_INVALID && SDL_GetGamepadButton(pad, b->button)) {
            return true;
        }
        if (b->axis != SDL_GAMEPAD_AXIS_INVALID && SDL_GetGamepadAxis(pad, b->axis) > 16000) {
            return true;
        }
    }
    return false;
}

bool PadFixedHeld(const App& app, size_t action) {
    // The fixed rows are stick directions: Movement uses the left stick, 1/2/-/HOME the right.
    const Action& a = kActions[action];
    const bool right = std::strncmp(a.keyboardKey, "cstick", 6) == 0;
    const SDL_GamepadAxis x = right ? SDL_GAMEPAD_AXIS_RIGHTX : SDL_GAMEPAD_AXIS_LEFTX;
    const SDL_GamepadAxis y = right ? SDL_GAMEPAD_AXIS_RIGHTY : SDL_GAMEPAD_AXIS_LEFTY;
    const char* dir = std::strchr(a.keyboardKey, '_') + 1;
    for (SDL_Gamepad* pad : app.pads) {
        const int vx = SDL_GetGamepadAxis(pad, x), vy = SDL_GetGamepadAxis(pad, y);
        if ((!std::strcmp(dir, "up") && vy < -16000) || (!std::strcmp(dir, "down") && vy > 16000) ||
            (!std::strcmp(dir, "left") && vx < -16000) || (!std::strcmp(dir, "right") && vx > 16000)) {
            return true;
        }
    }
    return false;
}

bool KeyHeld(const std::string& name) {
    if (name.empty() || name == "none") {
        return false;
    }
    const SDL_Scancode sc = SDL_GetScancodeFromName(name.c_str());
    if (sc == SDL_SCANCODE_UNKNOWN) {
        return false;
    }
    int count = 0;
    const bool* state = SDL_GetKeyboardState(&count);
    return sc < count && state[sc];
}

// ---------------------------------------------------------------------------------------------
// Look
// ---------------------------------------------------------------------------------------------

constexpr ImU32 kGold = IM_COL32(232, 184, 58, 255);
constexpr ImU32 kBlue = IM_COL32(47, 111, 219, 255);
constexpr ImVec4 kGoldV{232 / 255.f, 184 / 255.f, 58 / 255.f, 1.f};
constexpr ImVec4 kMutedV{0.62f, 0.66f, 0.74f, 1.f};
constexpr ImVec4 kGoodV{0.42f, 0.83f, 0.52f, 1.f};
constexpr ImVec4 kBadV{0.95f, 0.45f, 0.42f, 1.f};

void ApplyStyle() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 0.f;
    s.ChildRounding = 8.f;
    s.FrameRounding = 6.f;
    s.PopupRounding = 8.f;
    s.GrabRounding = 6.f;
    s.TabRounding = 6.f;
    s.FramePadding = ImVec2(12, 7);
    s.ItemSpacing = ImVec2(10, 8);
    s.CellPadding = ImVec2(10, 6);
    s.WindowPadding = ImVec2(0, 0);
    s.ScrollbarSize = 12.f;
    s.WindowBorderSize = 0.f;
    s.ChildBorderSize = 0.f;
    s.PopupBorderSize = 1.f;
    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(0.067f, 0.082f, 0.122f, 1.f);
    c[ImGuiCol_ChildBg] = ImVec4(0.098f, 0.118f, 0.173f, 1.f);
    c[ImGuiCol_PopupBg] = ImVec4(0.11f, 0.13f, 0.19f, 0.98f);
    c[ImGuiCol_Border] = ImVec4(0.25f, 0.29f, 0.38f, 1.f);
    c[ImGuiCol_Text] = ImVec4(0.93f, 0.94f, 0.97f, 1.f);
    c[ImGuiCol_TextDisabled] = kMutedV;
    c[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.19f, 0.27f, 1.f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.21f, 0.25f, 0.35f, 1.f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.24f, 0.29f, 0.41f, 1.f);
    c[ImGuiCol_Button] = ImVec4(0.18f, 0.22f, 0.31f, 1.f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.25f, 0.31f, 0.44f, 1.f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.18f, 0.43f, 0.86f, 1.f);
    c[ImGuiCol_Header] = ImVec4(0.18f, 0.22f, 0.31f, 1.f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.25f, 0.31f, 0.44f, 1.f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.18f, 0.43f, 0.86f, 1.f);
    c[ImGuiCol_CheckMark] = kGoldV;
    c[ImGuiCol_SliderGrab] = kGoldV;
    c[ImGuiCol_SliderGrabActive] = ImVec4(1.f, 0.8f, 0.3f, 1.f);
    c[ImGuiCol_TableHeaderBg] = ImVec4(0.13f, 0.16f, 0.23f, 1.f);
    c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.025f);
    c[ImGuiCol_TableBorderLight] = ImVec4(0.2f, 0.23f, 0.31f, 1.f);
    c[ImGuiCol_TableBorderStrong] = ImVec4(0.2f, 0.23f, 0.31f, 1.f);
    c[ImGuiCol_Separator] = ImVec4(0.2f, 0.23f, 0.31f, 1.f);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.02f, 0.03f, 0.06f, 0.7f);
}

bool AccentButton(const char* label, ImVec2 size) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.43f, 0.86f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.52f, 0.95f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.14f, 0.36f, 0.75f, 1.f));
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    return pressed;
}

bool NavButton(const char* label, bool selected, float width) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 size(width, 44);
    const bool pressed = ImGui::InvisibleButton(label, size);
    const bool hovered = ImGui::IsItemHovered();
    if (selected || hovered) {
        dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                          selected ? IM_COL32(40, 52, 78, 255) : IM_COL32(30, 38, 58, 255), 8.f);
    }
    if (selected) {
        dl->AddRectFilled(pos, ImVec2(pos.x + 4, pos.y + size.y), kGold, 2.f);
    }
    const ImVec2 textSize = ImGui::CalcTextSize(label);
    dl->AddText(ImVec2(pos.x + 20, pos.y + (size.y - textSize.y) * 0.5f),
                selected ? IM_COL32(255, 255, 255, 255) : IM_COL32(176, 184, 200, 255), label);
    return pressed;
}

void SectionTitle(App& app, const char* text) {
    ImGui::PushFont(app.titleFont);
    ImGui::TextUnformatted(text);
    ImGui::PopFont();
}

// ---------------------------------------------------------------------------------------------
// Pages
// ---------------------------------------------------------------------------------------------

fs::path GameDataDirectory(const App& app) {
    if (auto root = app.config.GetString("paths", "dvd_root"); root && !root->empty()) {
        return fs::u8path(*root);
    }
    return ExecutableDirectory() / "GameData";
}

bool GameDataReady(const App& app) {
    std::error_code ec;
    return fs::is_regular_file(GameDataDirectory(app) / "sys" / "main.dol", ec) &&
           fs::is_directory(GameDataDirectory(app) / "files", ec);
}

void SDLCALL OnImagePicked(void* user, const char* const* files, int) {
    App& app = *static_cast<App*>(user);
    std::lock_guard<std::mutex> lock(app.dialogMutex);
    app.dialogOpen = false;
    if (files && files[0]) {
        app.pickedImage = files[0];
    }
}

void PickDiscImage(App& app) {
    static const SDL_DialogFileFilter filters[] = {
        {"Wii disc images", "iso;wbfs;rvz;wia;ciso;gcz"},
        {"All files", "*"},
    };
    {
        std::lock_guard<std::mutex> lock(app.dialogMutex);
        if (app.dialogOpen) {
            return;
        }
        app.dialogOpen = true;
    }
    SDL_ShowOpenFileDialog(OnImagePicked, &app, app.window, filters, 2, nullptr, false);
}

void DrawPlay(App& app) {
    const bool ready = GameDataReady(app);
    SectionTitle(app, "Play");
    ImGui::TextColored(kMutedV, "The game runs from files extracted from your own disc. No game data is included.");
    ImGui::Dummy(ImVec2(0, 8));

    ImGui::BeginChild("gamedata", ImVec2(0, 150), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::Dummy(ImVec2(0, 2));
    ImGui::TextUnformatted("Game data");
    if (app.extractor.Running()) {
        ImGui::TextColored(kGoldV, "Extracting your disc... follow the progress in the console window.");
        const float t = static_cast<float>(SDL_GetTicks() % 1600) / 1600.f;
        ImGui::ProgressBar(-1.f * t, ImVec2(-1, 6), "");
    } else if (ready) {
        ImGui::TextColored(kGoodV, "Ready");
        ImGui::TextColored(kMutedV, "%s", U8(GameDataDirectory(app)).c_str());
    } else {
        ImGui::TextColored(kBadV, "Not set up yet");
        ImGui::TextColored(kMutedV, "Choose your Sonic and the Black Knight (USA) disc image to extract it (about 3.5 GB).");
    }
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::BeginDisabled(app.extractor.Running());
    if (ImGui::Button(ready ? "Extract again from a disc image..." : "Choose disc image...")) {
        PickDiscImage(app);
    }
    ImGui::EndDisabled();
    ImGui::EndChild();

    ImGui::Dummy(ImVec2(0, 12));
    ImGui::BeginDisabled(!ready || app.extractor.Running());
    ImGui::PushFont(app.titleFont);
    const bool play = AccentButton("PLAY", ImVec2(260, 64));
    ImGui::PopFont();
    ImGui::EndDisabled();
    ImGui::SameLine();
    bool closeOnLaunch = app.config.GetBool("launcher", "close_on_launch").value_or(true);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 18);
    if (ImGui::Checkbox("Close the launcher when the game starts", &closeOnLaunch)) {
        app.config.SetBool("launcher", "close_on_launch", closeOnLaunch);
        SaveConfig(app);
    }
    if (play) {
        std::string error;
        if (StartProcess(ExecutableDirectory() / kGameExecutable, {}, false, nullptr, error)) {
            SetStatus(app, "Starting the game...");
            if (closeOnLaunch) {
                app.quit = true;
            }
        } else {
            SetStatus(app, error, true);
        }
    }

    ImGui::Dummy(ImVec2(0, 16));
    ImGui::TextColored(kMutedV, "Tip: set up your keyboard and controller on the Controls page first.");
    ImGui::TextColored(kMutedV, "In game, F10 opens the port's settings bar.");
}

void BeginCapture(App& app, size_t action, bool keyboard) {
    app.capture.active = true;
    app.capture.keyboard = keyboard;
    app.capture.action = action;
    app.capture.startTicks = SDL_GetTicks();
    app.capture.openPopup = true;
}

// Counts how many actions share each binding so duplicates can be flagged.
std::map<std::string, int> CountBindings(const App& app, bool keyboard) {
    std::map<std::string, int> counts;
    for (size_t i = 0; i < kActionCount; ++i) {
        const Action& a = kActions[i];
        const std::string v = keyboard ? KeyboardValue(app, a) : PadValue(app, a);
        if (!v.empty() && v != "none" && v != "disabled") {
            ++counts[v];
        }
    }
    return counts;
}

void DrawControls(App& app) {
    SectionTitle(app, "Controls");
    ImGui::TextColored(kMutedV, "Click a binding, then press the key or controller button to use. "
                                "Esc cancels, Backspace clears.");
    SDL_Gamepad* pad = FirstPad(app);
    const std::string iconSetting = app.config.GetString("launcher", "button_icons").value_or("auto");
    PadStyle style = StyleFor(pad);
    if (iconSetting == "xbox") style = PadStyle::Xbox;
    if (iconSetting == "playstation") style = PadStyle::PlayStation;
    if (pad) {
        ImGui::TextColored(kGoodV, "Controller connected:");
        ImGui::SameLine();
        ImGui::TextUnformatted(SDL_GetGamepadName(pad) ? SDL_GetGamepadName(pad) : "Gamepad");
    } else {
        ImGui::TextColored(kGoldV, "No controller detected - connect one to bind and test it (keyboard works too).");
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(kMutedV, "Button icons:");
    ImGui::SameLine();
    static const char* iconKeys[] = {"auto", "xbox", "playstation"};
    static const char* iconLabels[] = {"Auto", "Xbox", "PlayStation"};
    for (int k = 0; k < 3; ++k) {
        if (k) ImGui::SameLine();
        if (ImGui::RadioButton(iconLabels[k], iconSetting == iconKeys[k])) {
            app.config.SetString("launcher", "button_icons", iconKeys[k]);
            SaveConfig(app);
        }
    }
    bool homeMenu = app.config.GetBool("game", "home_menu").value_or(false);
    if (ImGui::Checkbox("Allow the Wii HOME menu", &homeMenu)) {
        app.config.SetBool("game", "home_menu", homeMenu);
        SaveConfig(app);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Off: the HOME button does nothing, so the Wii HOME menu can't pop up by accident.");
    }
    ImGui::Dummy(ImVec2(0, 4));

    const auto keyCounts = CountBindings(app, true);
    const auto padCounts = CountBindings(app, false);

    const float footer = ImGui::GetFrameHeightWithSpacing() + 30.f;
    ImGui::BeginChild("controls", ImVec2(0, -footer), ImGuiChildFlags_None);
    const float rowH = ImGui::GetFrameHeight() * 1.25f;
    if (ImGui::BeginTable("bindings", 3,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX |
                              ImGuiTableFlags_ScrollY,
                          ImVec2(0, 0))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch, 1.3f);
        ImGui::TableSetupColumn("Keyboard", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Controller", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableHeadersRow();

        const char* lastGroup = "";
        for (size_t i = 0; i < kActionCount; ++i) {
            const Action& a = kActions[i];
            if (!homeMenu && std::strcmp(a.keyboardKey, "cstick_right") == 0) {
                continue; // the HOME row only matters while the HOME menu is allowed
            }
            if (std::strcmp(a.group, lastGroup) != 0) {
                lastGroup = a.group;
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Dummy(ImVec2(0, 2));
                ImGui::PushFont(app.smallFont);
                ImGui::TextColored(kGoldV, "%s", a.group);
                ImGui::PopFont();
            }
            ImGui::TableNextRow(ImGuiTableRowFlags_None, rowH);
            ImGui::PushID(static_cast<int>(i));

            const std::string keyValue = KeyboardValue(app, a);
            const std::string padValue = PadValue(app, a);
            const bool keyHeld = KeyHeld(keyValue);
            const bool padHeld = a.padKey ? PadNameHeld(app, padValue) : PadFixedHeld(app, i);

            ImGui::TableSetColumnIndex(0);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (rowH - ImGui::GetFrameHeight()) * 0.5f);
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(keyHeld || padHeld ? kGoldV : ImVec4(0.93f, 0.94f, 0.97f, 1.f), "%s", a.label);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Wii: %s", a.wiiInput);
            }

            ImGui::TableSetColumnIndex(1);
            {
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (rowH - ImGui::GetFrameHeight()) * 0.5f);
                const bool unbound = keyValue.empty() || keyValue == "none";
                const bool dup = !unbound && keyCounts.count(keyValue) && keyCounts.at(keyValue) > 1;
                if (keyHeld) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.43f, 0.86f, 1.f));
                }
                if (dup) {
                    ImGui::PushStyleColor(ImGuiCol_Text, kBadV);
                }
                const std::string text = (unbound ? std::string("Not bound") : keyValue) + "##key";
                if (ImGui::Button(text.c_str(), ImVec2(-1, 0))) {
                    BeginCapture(app, i, true);
                }
                if (dup) {
                    ImGui::PopStyleColor();
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("This key is used by more than one action.");
                    }
                }
                if (keyHeld) {
                    ImGui::PopStyleColor();
                }
            }

            ImGui::TableSetColumnIndex(2);
            {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                const float r = ImGui::GetFrameHeight() * 0.48f;
                const ImVec2 pos = ImGui::GetCursorScreenPos();
                const float cellW = ImGui::GetContentRegionAvail().x;
                const ImVec2 size(cellW, rowH - 4.f);
                if (a.padKey) {
                    const bool unbound = padValue.empty() || padValue == "disabled";
                    const bool dup = !unbound && padCounts.count(padValue) && padCounts.at(padValue) > 1;
                    if (padHeld) {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.43f, 0.86f, 1.f));
                    }
                    if (ImGui::Button("##pad", size)) {
                        BeginCapture(app, i, false);
                    }
                    if (padHeld) {
                        ImGui::PopStyleColor();
                    }
                    const ImVec2 c(pos.x + 14.f + r * 1.0f, pos.y + size.y * 0.5f);
                    float w = 0.f;
                    if (!unbound) {
                        // Centre the icon in the cell; wide icons (pills) grow both ways.
                        const ImVec2 mid(pos.x + size.x * 0.5f, c.y);
                        w = DrawPadGlyph(dl, app.bodyFont, mid, r, padValue, style, padHeld);
                        w = 0.f;
                    }
                    const std::string label = unbound ? std::string("Not bound") : std::string();
                    const ImVec2 ts = ImGui::CalcTextSize(label.c_str());
                    dl->AddText(ImVec2(pos.x + 14.f + std::max(w, r * 2.f) + 10.f, pos.y + (size.y - ts.y) * 0.5f),
                                dup ? ImGui::GetColorU32(kBadV) : Rgb(225, 228, 236), label.c_str());
                    if (dup && ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("This button is used by more than one action.");
                    }
                } else {
                    ImGui::Dummy(size);
                    const bool right = std::strncmp(a.keyboardKey, "cstick", 6) == 0;
                    const char* dir = std::strchr(a.keyboardKey, '_') + 1;
                    const ImVec2 c(pos.x + 14.f + r, pos.y + size.y * 0.5f);
                    DrawStickGlyph(dl, app.bodyFont, c, r, right, dir, padHeld, style);
                    const std::string label = std::string(right ? "Right stick " : "Left stick ") + dir;
                    const ImVec2 ts = ImGui::CalcTextSize(label.c_str());
                    dl->AddText(ImVec2(pos.x + 14.f + r * 2.f + 10.f, pos.y + (size.y - ts.y) * 0.5f),
                                ImGui::GetColorU32(padHeld ? kGoldV : kMutedV), label.c_str());
                }
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    // Capture popup (opened here, at the same ID level BeginPopupModal uses).
    if (app.capture.openPopup) {
        app.capture.openPopup = false;
        ImGui::OpenPopup("Bind input");
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28, 22));
    if (ImGui::BeginPopupModal("Bind input", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar |
                                   ImGuiWindowFlags_NoMove)) {
        if (!app.capture.active) {
            ImGui::CloseCurrentPopup();
        } else {
            const Action& a = kActions[app.capture.action];
            ImGui::PushFont(app.titleFont);
            ImGui::TextUnformatted(a.label);
            ImGui::PopFont();
            ImGui::Dummy(ImVec2(0, 6));
            ImGui::TextUnformatted(app.capture.keyboard ? "Press a key..." : "Press a controller button or trigger...");
            const uint64_t elapsed = SDL_GetTicks() - app.capture.startTicks;
            ImGui::ProgressBar(1.f - std::min(1.f, elapsed / 8000.f), ImVec2(320, 6), "");
            ImGui::TextColored(kMutedV, "Esc to cancel, Backspace to clear");
            if (elapsed > 8000) {
                app.capture.active = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
    ImGui::EndChild();

    ImGui::Dummy(ImVec2(0, 6));
    if (ImGui::Button("Restore defaults")) {
        RestoreDefaults(app);
    }
    ImGui::SameLine();
    ImGui::TextColored(kMutedV, "Changes are saved right away and used the next time the game starts.");
}

void DrawDisplay(App& app) {
    SectionTitle(app, "Display & sound");
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::BeginChild("display", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PushItemWidth(260);

    static const char* modes[] = {"windowed", "borderless", "exclusive"};
    static const char* modeLabels[] = {"Windowed", "Borderless fullscreen", "Exclusive fullscreen"};
    const std::string mode = app.config.GetString("video", "display_mode").value_or("windowed");
    int modeIndex = 0;
    for (int i = 0; i < 3; ++i) {
        if (mode == modes[i]) {
            modeIndex = i;
        }
    }
    if (ImGui::Combo("Display mode", &modeIndex, modeLabels, 3)) {
        app.config.SetString("video", "display_mode", modes[modeIndex]);
        SaveConfig(app);
    }

    static const float scales[] = {0.f, 1.f, 1.5f, 2.f, 3.f, 4.f, 6.f, 8.f};
    static const char* scaleLabels[] = {"Auto (match the window)", "1x (480p)", "1.5x (720p)", "2x (960p)",
                                        "3x (1440p)", "4x (4K)", "6x", "8x"};
    const double scale = app.config.GetNumber("video", "resolution_multiplier").value_or(1.0);
    int scaleIndex = 1;
    for (int i = 0; i < 8; ++i) {
        if (std::fabs(scale - scales[i]) < 0.01) {
            scaleIndex = i;
        }
    }
    if (ImGui::Combo("Internal resolution", &scaleIndex, scaleLabels, 8)) {
        app.config.SetNumber("video", "resolution_multiplier", scales[scaleIndex]);
        SaveConfig(app);
    }

    // The game runs at 30 FPS; 60 / 120 add interpolated in-between frames on the GPU (the game's
    // own logic, physics and timing are unchanged).
    static const int fpsValues[] = {0, 60, 120};
    static const char* fpsLabels[] = {"30 FPS (original)", "60 FPS (interpolated)", "120 FPS (interpolated)"};
    const double fpsSetting = app.config.GetNumber("video", "frame_interpolation_fps").value_or(0.0);
    int fpsIndex = 0;
    for (int i = 0; i < 3; ++i) {
        if (static_cast<int>(fpsSetting + 0.5) == fpsValues[i]) {
            fpsIndex = i;
        }
    }
    if (ImGui::Combo("Frame rate", &fpsIndex, fpsLabels, 3)) {
        app.config.SetRaw("video", "frame_interpolation_fps", std::to_string(fpsValues[fpsIndex]));
        SaveConfig(app);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("The game itself runs at 30 FPS. 60 and 120 draw extra in-between frames\n"
                          "for smoother motion; gameplay speed is unchanged. 120 needs a 120 Hz+ display.");
    }

    bool widescreen = app.config.GetBool("video", "widescreen").value_or(true);
    if (ImGui::Checkbox("Widescreen (16:9)", &widescreen)) {
        app.config.SetBool("video", "widescreen", widescreen);
        SaveConfig(app);
    }
    bool fps = app.config.GetBool("video", "show_fps").value_or(true);
    if (ImGui::Checkbox("Show FPS counter", &fps)) {
        app.config.SetBool("video", "show_fps", fps);
        SaveConfig(app);
    }

    ImGui::Dummy(ImVec2(0, 10));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 6));
    struct Volume {
        const char* key;
        const char* label;
    };
    static const Volume volumes[] = {
        {"volume", "Master volume"},
        {"music_volume", "Music"},
        {"sound_effects_volume", "Sound effects"},
        {"voices_volume", "Voices"},
    };
    for (const Volume& v : volumes) {
        float value = static_cast<float>(app.config.GetNumber("audio", v.key).value_or(1.0)) * 100.f;
        if (ImGui::SliderFloat(v.label, &value, 0.f, 100.f, "%.0f%%")) {
            app.config.SetNumber("audio", v.key, std::round(value) / 100.0);
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            SaveConfig(app);
        }
    }
    bool muted = app.config.GetBool("audio", "muted").value_or(false);
    if (ImGui::Checkbox("Mute", &muted)) {
        app.config.SetBool("audio", "muted", muted);
        SaveConfig(app);
    }
    ImGui::PopItemWidth();
    ImGui::Dummy(ImVec2(0, 10));
    ImGui::TextColored(kMutedV, "Settings file: %s", U8(app.config.Path()).c_str());
    ImGui::EndChild();
}

void DrawFrame(App& app) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::Begin("launcher", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetWindowPos();
    const float width = ImGui::GetWindowWidth();
    const float height = ImGui::GetWindowHeight();
    const float sidebar = 220.f;
    const float header = 92.f;

    // Header band with the title.
    dl->AddRectFilledMultiColor(origin, ImVec2(origin.x + width, origin.y + header), IM_COL32(22, 44, 98, 255),
                                IM_COL32(16, 26, 56, 255), IM_COL32(16, 26, 56, 255), IM_COL32(22, 44, 98, 255));
    dl->AddRectFilled(ImVec2(origin.x, origin.y + header - 3), ImVec2(origin.x + width, origin.y + header), kGold);
    ImGui::SetCursorPos(ImVec2(28, 16));
    ImGui::PushFont(app.titleFont);
    ImGui::TextUnformatted("Sonic and the Black Knight");
    ImGui::PopFont();
    ImGui::SetCursorPos(ImVec2(30, 54));
    ImGui::TextColored(ImVec4(0.75f, 0.8f, 0.92f, 1.f), "PC port  |  Launcher");

    // Sidebar.
    dl->AddRectFilled(ImVec2(origin.x, origin.y + header), ImVec2(origin.x + sidebar, origin.y + height),
                      IM_COL32(13, 17, 27, 255));
    ImGui::SetCursorPos(ImVec2(14, header + 20));
    ImGui::BeginGroup();
    if (NavButton("Play", app.page == Page::Play, sidebar - 28)) {
        app.page = Page::Play;
    }
    if (NavButton("Controls", app.page == Page::Controls, sidebar - 28)) {
        app.page = Page::Controls;
    }
    if (NavButton("Display & sound", app.page == Page::Display, sidebar - 28)) {
        app.page = Page::Display;
    }
    ImGui::EndGroup();

    // Status line at the bottom of the sidebar.
    if (!app.status.empty() && SDL_GetTicks() < app.statusUntil) {
        ImGui::SetCursorPos(ImVec2(18, height - 40));
        ImGui::PushTextWrapPos(sidebar - 12);
        ImGui::TextColored(app.statusIsError ? kBadV : kGoodV, "%s", app.status.c_str());
        ImGui::PopTextWrapPos();
    }

    // Page body.
    ImGui::SetCursorPos(ImVec2(sidebar + 28, header + 18));
    ImGui::BeginChild("page", ImVec2(width - sidebar - 56, height - header - 36), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    switch (app.page) {
    case Page::Play:
        DrawPlay(app);
        break;
    case Page::Controls:
        DrawControls(app);
        break;
    case Page::Display:
        DrawDisplay(app);
        break;
    }
    ImGui::EndChild();
    ImGui::End();
}

// ---------------------------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------------------------

void FinishCapture(App& app) {
    app.capture.active = false;
}

// Returns true when the event was consumed by an active binding capture.
bool HandleCaptureEvent(App& app, const SDL_Event& e) {
    if (!app.capture.active) {
        return false;
    }
    const Action& a = kActions[app.capture.action];
    if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
        if (e.key.scancode == SDL_SCANCODE_ESCAPE) {
            FinishCapture(app);
            return true;
        }
        if (e.key.scancode == SDL_SCANCODE_BACKSPACE || e.key.scancode == SDL_SCANCODE_DELETE) {
            if (app.capture.keyboard) {
                app.config.SetString("keyboard", a.keyboardKey, "none");
            } else if (a.padKey) {
                app.config.SetString("controller", a.padKey, "disabled");
            }
            SaveConfig(app);
            FinishCapture(app);
            return true;
        }
        if (app.capture.keyboard) {
            app.config.SetString("keyboard", a.keyboardKey, SDL_GetScancodeName(e.key.scancode));
            SaveConfig(app);
            FinishCapture(app);
        }
        return true;
    }
    if (!app.capture.keyboard && a.padKey) {
        const char* name = nullptr;
        if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
            for (const PadButton& b : kPadButtons) {
                if (b.button == static_cast<SDL_GamepadButton>(e.gbutton.button)) {
                    name = b.configName;
                }
            }
        } else if (e.type == SDL_EVENT_GAMEPAD_AXIS_MOTION && e.gaxis.value > 20000) {
            if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER) {
                name = "left_trigger";
            } else if (e.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
                name = "right_trigger";
            }
        }
        if (name) {
            app.config.SetString("controller", a.padKey, name);
            SaveConfig(app);
            FinishCapture(app);
            return true;
        }
    }
    // Keep controller input away from ImGui navigation while capturing.
    return e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || e.type == SDL_EVENT_GAMEPAD_BUTTON_UP ||
           e.type == SDL_EVENT_GAMEPAD_AXIS_MOTION;
}

void RefreshPads(App& app) {
    for (SDL_Gamepad* pad : app.pads) {
        SDL_CloseGamepad(pad);
    }
    app.pads.clear();
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    for (int i = 0; ids && i < count; ++i) {
        if (SDL_Gamepad* pad = SDL_OpenGamepad(ids[i])) {
            app.pads.push_back(pad);
        }
    }
    SDL_free(ids);
}

void PollBackground(App& app) {
    std::optional<std::string> picked;
    {
        std::lock_guard<std::mutex> lock(app.dialogMutex);
        picked.swap(app.pickedImage);
    }
    if (picked) {
        const fs::path exeDir = ExecutableDirectory();
        std::string error;
        if (StartProcess(exeDir / kExtractorExecutable, {*picked, U8(GameDataDirectory(app))}, true,
                         &app.extractor, error)) {
            SetStatus(app, "Extracting...");
        } else {
            SetStatus(app, error, true);
        }
    }
    if (auto code = PollProcess(app.extractor)) {
        if (*code == 0 && GameDataReady(app)) {
            SetStatus(app, "Game data ready");
        } else {
            SetStatus(app, "Extraction failed - is this a clean USA disc image?", true);
        }
    }
}

} // namespace

int main(int, char**) {
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    App app;
    app.config.Load(ConfigPath()); // a missing file is fine: it is created on the first save

    const float dpi = std::max(1.f, SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay()));
    app.window = SDL_CreateWindow("Sonic and the Black Knight - Launcher", static_cast<int>(1000 * dpi),
                                  static_cast<int>(660 * dpi), SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!app.window) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetWindowMinimumSize(app.window, 860, 560);
    app.renderer = SDL_CreateRenderer(app.window, nullptr);
    if (!app.renderer) {
        std::fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetRenderVSync(app.renderer, 1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImFontConfig fontConfig;
    fontConfig.FontDataOwnedByAtlas = false;
    app.bodyFont = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(kLauncherFont), kLauncherFontSize,
                                                  17.f * dpi, &fontConfig);
    app.titleFont = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(kLauncherFont), kLauncherFontSize,
                                                   30.f * dpi, &fontConfig);
    app.smallFont = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(kLauncherFont), kLauncherFontSize,
                                                   14.f * dpi, &fontConfig);
    ApplyStyle();
    ImGui::GetStyle().ScaleAllSizes(dpi);
    ImGui_ImplSDL3_InitForSDLRenderer(app.window, app.renderer);
    ImGui_ImplSDLRenderer3_Init(app.renderer);
    RefreshPads(app);

    while (!app.quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT ||
                (e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && e.window.windowID == SDL_GetWindowID(app.window))) {
                app.quit = true;
            }
            if (e.type == SDL_EVENT_GAMEPAD_ADDED || e.type == SDL_EVENT_GAMEPAD_REMOVED) {
                RefreshPads(app);
            }
            if (HandleCaptureEvent(app, e)) {
                continue;
            }
            ImGui_ImplSDL3_ProcessEvent(&e);
        }
        PollBackground(app);
        if (SDL_GetWindowFlags(app.window) & SDL_WINDOW_MINIMIZED) {
            SDL_Delay(50);
            continue;
        }
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        DrawFrame(app);
        ImGui::Render();
        SDL_SetRenderScale(app.renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
        SDL_SetRenderDrawColor(app.renderer, 17, 21, 31, 255);
        SDL_RenderClear(app.renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), app.renderer);
        SDL_RenderPresent(app.renderer);
    }

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    for (SDL_Gamepad* pad : app.pads) {
        SDL_CloseGamepad(pad);
    }
    SDL_DestroyRenderer(app.renderer);
    SDL_DestroyWindow(app.window);
    SDL_Quit();
    return 0;
}
