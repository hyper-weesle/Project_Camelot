// Sonic and the Black Knight PC port: Wii Remote + Nunchuk input.
//
// The game only understands a Wii Remote with a Nunchuk (KPAD). Two sources feed it:
//  * a real Bluetooth Wii Remote, through the upstream WiiRemoteInput path, or
//  * any PC gamepad / keyboard, read through aurora's GameCube pad layer (so the
//    existing F10 binding editor and Dolphin-syntax expressions apply) and turned into
//    a virtual Wii Remote + Nunchuk. Motion gestures are synthesized from buttons:
//
//      GC layout          ->  Wii Remote + Nunchuk
//      main stick         ->  Nunchuk stick
//      A                  ->  A        (jump)
//      B                  ->  B
//      X                  ->  remote swing   (sword slash; hold to keep slashing)
//      Y                  ->  remote thrust  (forward stab)
//      Z                  ->  Nunchuk shake
//      L                  ->  Nunchuk Z
//      R                  ->  Nunchuk C
//      D-pad              ->  remote D-pad
//      Start              ->  +
//      C-stick up/down    ->  1 / 2
//      C-stick left/right ->  - / HOME
//
// Default keyboard layout (port 1, rebindable in the F10 menu like any GameCube binding):
//      WASD move, Space jump (A), J slash (X), K thrust (Y), L = B, Left Shift guard (Nunchuk Z),
//      E = Nunchuk C, Q = Nunchuk shake, Enter = +, arrow keys = D-pad, I/O = 1/2 (C-stick), M = -.
#include <map>
#include <fstream>
#include <cstdio>
#include "runtime_config.h"
#include "hle_stubs.h"
#include "input_bindings.h"
#include "memory.h"
#include "wii_remote_input.h"

#include <SDL3/SDL_timer.h>
#include <dolphin/pad.h>

#include <SDL3/SDL_scancode.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

// KPADStatus in this SDK revision is 0xB0 bytes (the game memsets that much).
constexpr uint32_t kKpadStatusSize = 0xB0;
constexpr uint32_t kHold = 0x00, kTrig = 0x04, kRelease = 0x08, kAcc = 0x0C, kAccValue = 0x18,
                   kAccSpeed = 0x1C, kDevType = 0x5C, kWpadErr = 0x5D, kDpdValidFg = 0x5E,
                   kDataFormat = 0x5F, kFsStick = 0x60, kFsAcc = 0x68, kFsAccValue = 0x74, kFsAccSpeed = 0x78;

constexpr uint8_t kDevCore = 0, kDevFreestyle = 1, kDevClassic = 2;
constexpr uint8_t kFmtCoreAccDpd = 2, kFmtFreestyleAccDpd = 5, kFmtClassicAccDpd = 8;
constexpr int8_t kWpadErrNone = 0, kWpadErrNoController = -1;

// WPAD / KPAD button bits.
constexpr uint32_t kBtnLeft = 0x0001, kBtnRight = 0x0002, kBtnDown = 0x0004, kBtnUp = 0x0008,
                   kBtnPlus = 0x0010, kBtn2 = 0x0100, kBtn1 = 0x0200, kBtnB = 0x0400, kBtnA = 0x0800,
                   kBtnMinus = 0x1000, kBtnZ = 0x2000, kBtnC = 0x4000, kBtnHome = 0x8000;

struct Gesture {
    int frame = -1; // -1 idle, otherwise frames since the gesture started
};

struct ChannelState {
    uint32_t prevHold = 0;
    float prevAcc[3] = {0.0f, -1.0f, 0.0f};
    float prevFsAcc[3] = {0.0f, -1.0f, 0.0f};
    Gesture swing, thrust, shake;
    int swingSide = 1;
};

std::array<ChannelState, 4> g_channels{};
std::array<PADStatus, PAD_CHANMAX> g_pad{};
uint64_t g_padSampleTick = 0;

// Keyboard layout. Config.toml [keyboard] (written by the launcher) overrides any entry; values
// are SDL scancode names ("Space", "Left Shift", "J", ...), "" or "none" for unbound.
struct KeyDefault {
    const char* key;
    SDL_Scancode scancode;
};
constexpr KeyDefault kDefaultButtonKeys[PAD_BUTTON_COUNT] = {
    {"a", SDL_SCANCODE_SPACE},      {"b", SDL_SCANCODE_L},        {"x", SDL_SCANCODE_J},
    {"y", SDL_SCANCODE_K},          {"start", SDL_SCANCODE_RETURN}, {"z", SDL_SCANCODE_Q},
    {"l", SDL_SCANCODE_LSHIFT},     {"r", SDL_SCANCODE_E},        {"up", SDL_SCANCODE_UP},
    {"down", SDL_SCANCODE_DOWN},    {"left", SDL_SCANCODE_LEFT},  {"right", SDL_SCANCODE_RIGHT},
};
constexpr PADButton kButtonOrder[PAD_BUTTON_COUNT] = {
    PAD_BUTTON_A, PAD_BUTTON_B, PAD_BUTTON_X, PAD_BUTTON_Y, PAD_BUTTON_START, PAD_TRIGGER_Z,
    PAD_TRIGGER_L, PAD_TRIGGER_R, PAD_BUTTON_UP, PAD_BUTTON_DOWN, PAD_BUTTON_LEFT, PAD_BUTTON_RIGHT,
};
struct AxisDefault {
    const char* key;
    SDL_Scancode scancode;
    PADAxis axis;
};
constexpr AxisDefault kDefaultAxisKeys[8] = {
    {"stick_right", SDL_SCANCODE_D, PAD_AXIS_LEFT_X_POS},   {"stick_left", SDL_SCANCODE_A, PAD_AXIS_LEFT_X_NEG},
    {"stick_up", SDL_SCANCODE_W, PAD_AXIS_LEFT_Y_POS},      {"stick_down", SDL_SCANCODE_S, PAD_AXIS_LEFT_Y_NEG},
    {"cstick_right", SDL_SCANCODE_N, PAD_AXIS_RIGHT_X_POS}, {"cstick_left", SDL_SCANCODE_M, PAD_AXIS_RIGHT_X_NEG},
    {"cstick_up", SDL_SCANCODE_I, PAD_AXIS_RIGHT_Y_POS},    {"cstick_down", SDL_SCANCODE_O, PAD_AXIS_RIGHT_Y_NEG},
};

// Minimal reader for the [keyboard] table: `name = "value"` lines.
std::map<std::string, std::string> ReadKeyboardSection() {
    std::map<std::string, std::string> out;
    std::ifstream in(RuntimeConfigFile::ResolveConfigPath());
    std::string line;
    bool inSection = false;
    while (std::getline(in, line)) {
        const size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line[first] == '#') {
            continue;
        }
        if (line[first] == '[') {
            inSection = line.compare(first, 10, "[keyboard]") == 0;
            continue;
        }
        if (!inSection) {
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        std::string key = line.substr(first, eq - first);
        key.erase(key.find_last_not_of(" \t") + 1);
        const size_t q1 = line.find('"', eq);
        const size_t q2 = q1 == std::string::npos ? std::string::npos : line.find('"', q1 + 1);
        if (q2 != std::string::npos) {
            out[key] = line.substr(q1 + 1, q2 - q1 - 1);
        }
    }
    return out;
}

SDL_Scancode ResolveKey(const std::map<std::string, std::string>& cfg, const char* key, SDL_Scancode fallback) {
    const auto it = cfg.find(key);
    if (it == cfg.end()) {
        return fallback;
    }
    if (it->second.empty() || it->second == "none") {
        return SDL_SCANCODE_UNKNOWN;
    }
    const SDL_Scancode sc = SDL_GetScancodeFromName(it->second.c_str());
    if (sc == SDL_SCANCODE_UNKNOWN) {
        std::fprintf(stderr, "[satbk] unknown key name '%s' for keyboard.%s\n", it->second.c_str(), key);
        return fallback;
    }
    return sc;
}

void EnsurePadInitialized() {
    static bool done = false;
    if (done) {
        return;
    }
    done = true;
    PADInit(); // this game never calls the GameCube PAD library itself
    const auto cfg = ReadKeyboardSection();
    u32 count = 0;
    if (!cfg.empty() || PADGetKeyButtonBindings(0, &count) == nullptr) {
        PADKeyButtonBinding buttons[PAD_BUTTON_COUNT];
        for (size_t i = 0; i < PAD_BUTTON_COUNT; ++i) {
            const SDL_Scancode sc = ResolveKey(cfg, kDefaultButtonKeys[i].key, kDefaultButtonKeys[i].scancode);
            buttons[i] = {sc == SDL_SCANCODE_UNKNOWN ? PAD_KEY_INVALID : static_cast<s32>(sc), kButtonOrder[i]};
        }
        PADKeyAxisBinding axes[PAD_AXIS_COUNT];
        for (size_t i = 0; i < 8; ++i) {
            const SDL_Scancode sc = ResolveKey(cfg, kDefaultAxisKeys[i].key, kDefaultAxisKeys[i].scancode);
            axes[i] = {sc == SDL_SCANCODE_UNKNOWN ? PAD_KEY_INVALID : static_cast<s32>(sc), kDefaultAxisKeys[i].axis, 100};
        }
        axes[8] = {PAD_KEY_INVALID, PAD_AXIS_TRIGGER_L, 0};
        axes[9] = {PAD_KEY_INVALID, PAD_AXIS_TRIGGER_R, 0};
        PADSetKeyButtonBindings(0, buttons);
        PADSetKeyAxisBindings(0, axes);
        PADSetKeyboardActive(0, TRUE);
    }
}

// Developer aid: SATBK_AUTOPRESS="A:30-31,PLUS:45-46" holds WPAD buttons on channel 0 during
// those seconds since start (A, B, PLUS, MINUS, 1, 2, Z, C, UP, DOWN, LEFT, RIGHT, SWING).
uint32_t AutoPressButtons(bool& swing) {
    static const char* spec = std::getenv("SATBK_AUTOPRESS");
    swing = false;
    if (spec == nullptr) {
        return 0;
    }
    static const uint64_t start = SDL_GetTicksNS();
    const double t = static_cast<double>(SDL_GetTicksNS() - start) / 1e9;
    uint32_t held = 0;
    std::string all(spec);
    size_t pos = 0;
    while (pos < all.size()) {
        size_t end = all.find(',', pos);
        if (end == std::string::npos) end = all.size();
        const std::string item = all.substr(pos, end - pos);
        pos = end + 1;
        const size_t colon = item.find(':'), dash = item.find('-');
        if (colon == std::string::npos || dash == std::string::npos) continue;
        const std::string name = item.substr(0, colon);
        const double from = std::atof(item.substr(colon + 1, dash - colon - 1).c_str());
        const double to = std::atof(item.substr(dash + 1).c_str());
        if (t < from || t > to) continue;
        static const std::pair<const char*, uint32_t> kNames[] = {
            {"A", kBtnA}, {"B", kBtnB}, {"PLUS", kBtnPlus}, {"MINUS", kBtnMinus}, {"1", kBtn1},
            {"2", kBtn2}, {"Z", kBtnZ}, {"C", kBtnC}, {"UP", kBtnUp}, {"DOWN", kBtnDown},
            {"LEFT", kBtnLeft}, {"RIGHT", kBtnRight}};
        if (name == "SWING") swing = true;
        for (const auto& [n, bit] : kNames) {
            if (name == n) held |= bit;
        }
    }
    return held;
}

// One PADRead per ~frame for all four channels (KPADRead is called once per channel).
void RefreshPad() {
    EnsurePadInitialized();
    const uint64_t now = SDL_GetTicksNS();
    if (g_padSampleTick != 0 && now - g_padSampleTick < 4'000'000ull) {
        return;
    }
    g_padSampleTick = now;
    WiiRemoteInput::Poll();
    PADStatus statuses[PAD_CHANMAX]{};
    PADRead(statuses);
    WiiRemoteInput::HideRemotesFromPad(statuses, PAD_CHANMAX);
    InputBindings::Apply(statuses);
    // One player: every PC pad (whatever port SDL/aurora put it on) and the keyboard all drive
    // Wii Remote 1. Each control takes whichever source is pushed furthest.
    PADStatus merged{};
    merged.err = PAD_ERR_NO_CONTROLLER;
    const auto pick = [](int8_t a, int8_t b) { return std::abs(b) > std::abs(a) ? b : a; };
    for (uint32_t i = 0; i < PAD_CHANMAX; ++i) {
        const PADStatus& p = statuses[i];
        if (p.err != PAD_ERR_NONE) {
            continue;
        }
        merged.err = PAD_ERR_NONE;
        merged.button |= p.button;
        merged.stickX = pick(merged.stickX, p.stickX);
        merged.stickY = pick(merged.stickY, p.stickY);
        merged.substickX = pick(merged.substickX, p.substickX);
        merged.substickY = pick(merged.substickY, p.substickY);
        merged.triggerLeft = std::max(merged.triggerLeft, p.triggerLeft);
        merged.triggerRight = std::max(merged.triggerRight, p.triggerRight);
    }
    g_pad[0] = merged;
    for (uint32_t i = 1; i < PAD_CHANMAX; ++i) {
        g_pad[i] = PADStatus{};
        g_pad[i].err = PAD_ERR_NO_CONTROLLER;
    }
    // Report controller assignment changes, so a log shows which pad the game is reading.
    static int32_t lastIndex[PAD_CHANMAX] = {-2, -2, -2, -2};
    for (uint32_t i = 0; i < PAD_CHANMAX; ++i) {
        const int32_t index = PADGetIndexForPort(i);
        if (index != lastIndex[i]) {
            lastIndex[i] = index;
            const char* name = index >= 0 ? PADGetName(i) : nullptr;
            std::fprintf(stderr, "[satbk] pad port %u: %s\n", i, index >= 0 ? (name ? name : "controller") : "none");
        }
    }
}

float StickAxis(int8_t v) {
    return std::clamp(static_cast<float>(v) / 72.0f, -1.0f, 1.0f);
}

// Advances a button-driven gesture: starts on press, runs `length` frames, and
// restarts while the button stays held (so holding X keeps slashing).
bool StepGesture(Gesture& g, bool held, int length) {
    if (g.frame >= 0) {
        ++g.frame;
        if (g.frame >= length) {
            g.frame = held ? 0 : -1;
            return g.frame == 0;
        }
        return false;
    }
    if (held) {
        g.frame = 0;
        return true;
    }
    return false;
}

// Remote accelerometer for the current gesture frame, KPAD frame (+x left, +y down... the
// SDK's convention: resting flat reads (0, -1, 0)).
void GestureAcc(ChannelState& st, float acc[3]) {
    acc[0] = 0.0f;
    acc[1] = -1.0f;
    acc[2] = 0.0f;
    constexpr float kPi = 3.14159265f;
    if (st.swing.frame >= 0) {
        // A sideways slash: a strong lateral spike then the opposite deceleration.
        const float t = static_cast<float>(st.swing.frame) / 8.0f;
        acc[0] += st.swingSide * 3.4f * std::sin(t * 2.0f * kPi);
        acc[1] += -1.2f * std::sin(t * kPi);
    }
    if (st.thrust.frame >= 0) {
        const float t = static_cast<float>(st.thrust.frame) / 8.0f;
        acc[2] += -3.4f * std::sin(t * 2.0f * kPi);
    }
}

void NunchukAcc(ChannelState& st, float acc[3]) {
    acc[0] = 0.0f;
    acc[1] = -1.0f;
    acc[2] = 0.0f;
    if (st.shake.frame >= 0) {
        const float t = static_cast<float>(st.shake.frame) / 6.0f;
        acc[1] += 3.0f * std::sin(t * 2.0f * 3.14159265f);
    }
}

bool VirtualSample(uint32_t chan, WiiRemoteInput::KpadSample& s) {
    RefreshPad();
    if (chan >= PAD_CHANMAX || g_pad[chan].err != PAD_ERR_NONE) {
        return false;
    }
    const PADStatus& p = g_pad[chan];
    ChannelState& st = g_channels[chan];
    const bool blocked = InputBindings::InputBlocked();
    const uint16_t b = blocked ? 0 : p.button;

    uint32_t hold = 0;
    if (b & PAD_BUTTON_A) hold |= kBtnA;
    if (b & PAD_BUTTON_B) hold |= kBtnB;
    if (b & PAD_BUTTON_START) hold |= kBtnPlus;
    if (b & PAD_TRIGGER_L || p.triggerL > 0x80) hold |= kBtnZ;
    if (b & PAD_TRIGGER_R || p.triggerR > 0x80) hold |= kBtnC;
    if (b & PAD_BUTTON_LEFT) hold |= kBtnLeft;
    if (b & PAD_BUTTON_RIGHT) hold |= kBtnRight;
    if (b & PAD_BUTTON_UP) hold |= kBtnUp;
    if (b & PAD_BUTTON_DOWN) hold |= kBtnDown;
    if (!blocked) {
        if (p.substickY > 50) hold |= kBtn1;
        if (p.substickY < -50) hold |= kBtn2;
        if (p.substickX < -50) hold |= kBtnMinus;
        if (p.substickX > 50) hold |= kBtnHome;
    }

    bool autoSwing = false;
    if (chan == 0) {
        hold |= AutoPressButtons(autoSwing);
    }
    if (StepGesture(st.swing, !blocked && ((b & PAD_BUTTON_X) || autoSwing), 8)) {
        st.swingSide = -st.swingSide; // alternate forehand / backhand like a real combo
    }
    StepGesture(st.thrust, !blocked && (b & PAD_BUTTON_Y), 8);
    StepGesture(st.shake, !blocked && (b & PAD_TRIGGER_Z), 6);

    s = {};
    s.hold = hold;
    GestureAcc(st, s.acc);
    s.hasNunchuk = true;
    s.stick[0] = blocked ? 0.0f : StickAxis(p.stickX);
    s.stick[1] = blocked ? 0.0f : StickAxis(p.stickY);
    NunchukAcc(st, s.nunchukAcc);
    return true;
}

bool Sample(uint32_t chan, WiiRemoteInput::KpadSample& s, bool& isClassic) {
    isClassic = false;
    if (WiiRemoteInput::ReadKpadSample(chan, s)) {
        isClassic = s.hasClassic;
        if (InputBindings::InputBlocked()) {
            s.hold = 0;
            s.stick[0] = s.stick[1] = 0.0f;
        }
        return true;
    }
    return VirtualSample(chan, s);
}

float Length(const float* v) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }
float Distance(const float* a, const float* b) {
    const float d[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
    return Length(d);
}
void WriteVec3(uint32_t addr, const float* v) {
    Memory::WriteFloat32(addr, v[0]);
    Memory::WriteFloat32(addr + 4, v[1]);
    Memory::WriteFloat32(addr + 8, v[2]);
}
void Zero(uint32_t addr, uint32_t bytes) {
    for (uint32_t o = 0; o < bytes; o += 4) Memory::Write32(addr + o, 0);
}

int32_t WriteStatus(uint32_t chan, uint32_t addr, const WiiRemoteInput::KpadSample* s) {
    ChannelState& st = g_channels[chan];
    Zero(addr, kKpadStatusSize);
    if (s == nullptr) {
        st.prevHold = 0;
        Memory::Write8(addr + kDevType, kDevCore);
        Memory::Write8(addr + kWpadErr, static_cast<uint8_t>(kWpadErrNoController));
        return 0;
    }
    Memory::Write32(addr + kHold, s->hold);
    Memory::Write32(addr + kTrig, s->hold & ~st.prevHold);
    Memory::Write32(addr + kRelease, st.prevHold & ~s->hold);
    st.prevHold = s->hold;
    WriteVec3(addr + kAcc, s->acc);
    Memory::WriteFloat32(addr + kAccValue, Length(s->acc));
    Memory::WriteFloat32(addr + kAccSpeed, Distance(s->acc, st.prevAcc));
    std::copy(s->acc, s->acc + 3, st.prevAcc);
    const uint8_t dev = s->hasClassic ? kDevClassic : s->hasNunchuk ? kDevFreestyle : kDevCore;
    const uint8_t fmt = s->hasClassic ? kFmtClassicAccDpd : s->hasNunchuk ? kFmtFreestyleAccDpd : kFmtCoreAccDpd;
    Memory::Write8(addr + kDevType, dev);
    Memory::Write8(addr + kWpadErr, static_cast<uint8_t>(kWpadErrNone));
    Memory::Write8(addr + kDpdValidFg, 0);
    Memory::Write8(addr + kDataFormat, fmt);
    if (s->hasNunchuk) {
        Memory::WriteFloat32(addr + kFsStick, s->stick[0]);
        Memory::WriteFloat32(addr + kFsStick + 4, s->stick[1]);
        WriteVec3(addr + kFsAcc, s->nunchukAcc);
        Memory::WriteFloat32(addr + kFsAccValue, Length(s->nunchukAcc));
        Memory::WriteFloat32(addr + kFsAccSpeed, Distance(s->nunchukAcc, st.prevFsAcc));
        std::copy(s->nunchukAcc, s->nunchukAcc + 3, st.prevFsAcc);
    }
    return 1;
}

} // namespace

// True when `chan` has either a real remote or a PC pad standing in for one.
extern "C" bool SatbkRemoteConnected(uint32_t chan) {
    if (WiiRemoteInput::IsRemoteChannel(chan)) {
        return true;
    }
    RefreshPad();
    return chan < PAD_CHANMAX && g_pad[chan].err == PAD_ERR_NONE;
}

// KPADRead(s32 chan, KPADStatus* samplingBufs, u32 length, s32* err) - SatBK SDK revision.
extern "C" int32_t SatbkKPADRead_HLE(uint32_t chan, uint32_t statusPtr, uint32_t length, uint32_t errPtr) {
    if (chan >= g_channels.size() || statusPtr == 0 || length == 0) {
        if (errPtr) Memory::Write32(errPtr, static_cast<uint32_t>(-1));
        return 0;
    }
    WiiRemoteInput::KpadSample sample;
    bool classic = false;
    const bool have = Sample(chan, sample, classic);
    try {
        const int32_t n = WriteStatus(chan, statusPtr, have ? &sample : nullptr);
        if (errPtr) Memory::Write32(errPtr, have ? 0u : static_cast<uint32_t>(-1));
        return n;
    } catch (const Memory::AccessViolation&) {
        return 0;
    }
}
PPC_NATIVE_OVERRIDE(80574558, SatbkKPADRead_HLE, int32_t,
                    (uint32_t chan, uint32_t statusPtr, uint32_t length, uint32_t errPtr),
                    (chan, statusPtr, length, errPtr));
