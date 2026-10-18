// Sonic and the Black Knight PC port: controller button prompts.
//
// The game's menus show Wii Remote buttons (A, B, the remote itself) and the title screen says
// "PRESS (A) + (B)". The renderer lets the port edit specific game textures as they are loaded
// (aurora_register_texture_patch): the game's own pixels are decoded from guest memory, the Wii
// icons are cleared and Xbox or PlayStation buttons drawn in their place. Nothing from the game is
// stored; the button images (satbk_button_glyphs.inc, made by satbk/tools/gen_button_glyphs.py)
// are original artwork for this port.
//
// The style follows the launcher's "Button icons" setting ([launcher] button_icons =
// auto|xbox|playstation; auto looks at the first connected controller, like the launcher does).
// The icon drawn for the Wii A and B buttons is whichever controller button they are bound to
// ([controller] a / b). A Nintendo controller in auto mode keeps the original Wii prompts.
#include "runtime_config.h"

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_stdinc.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

#include "satbk_button_glyphs.inc"

extern "C" void aurora_register_texture_patch(uint64_t textureHash, uint32_t width, uint32_t height, uint32_t format,
                                              void (*apply)(uint8_t* rgba, uint32_t width, uint32_t height));

namespace {
using namespace satbk_glyphs;

enum class Style { Wii, Xbox, PlayStation };

// Reads `key = value` from a [section] of Config.toml (quotes stripped). Empty if absent.
std::string ConfigValue(const char* section, const char* key) {
    std::ifstream in(RuntimeConfigFile::ResolveConfigPath());
    const std::string header = std::string("[") + section + "]";
    std::string line;
    bool inSection = false;
    while (std::getline(in, line)) {
        const size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line[first] == '#') {
            continue;
        }
        if (line[first] == '[') {
            inSection = line.compare(first, header.size(), header) == 0;
            continue;
        }
        if (!inSection) {
            continue;
        }
        const size_t eq = line.find('=', first);
        if (eq == std::string::npos) {
            continue;
        }
        std::string name = line.substr(first, eq - first);
        name.erase(name.find_last_not_of(" \t") + 1);
        if (name != key) {
            continue;
        }
        std::string value = line.substr(eq + 1);
        const size_t hash = value.find('#');
        if (hash != std::string::npos) {
            value.resize(hash);
        }
        value.erase(std::remove_if(value.begin(), value.end(), [](char c) { return c == '"' || c == ' ' || c == '\t'; }),
                    value.end());
        return value;
    }
    return {};
}

Style DetectStyle() {
    const std::string setting = ConfigValue("launcher", "button_icons");
    if (setting == "xbox") {
        return Style::Xbox;
    }
    if (setting == "playstation") {
        return Style::PlayStation;
    }
    Style style = Style::Xbox; // no controller: same default as the launcher
    int count = 0;
    if (SDL_JoystickID* ids = SDL_GetGamepads(&count)) {
        if (count > 0) {
            switch (SDL_GetGamepadTypeForID(ids[0])) {
            case SDL_GAMEPAD_TYPE_PS3:
            case SDL_GAMEPAD_TYPE_PS4:
            case SDL_GAMEPAD_TYPE_PS5:
                style = Style::PlayStation;
                break;
            case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
            case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
            case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
            case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
            case SDL_GAMEPAD_TYPE_GAMECUBE:
                style = Style::Wii;
                break;
            default:
                break;
            }
        }
        SDL_free(ids);
    }
    return style;
}

// Decided once, the first time a prompt texture loads (the edited textures are cached).
Style CurrentStyle() {
    static const Style style = [] {
        const Style s = DetectStyle();
        std::fprintf(stderr, "[satbk] button prompts: %s\n",
                     s == Style::Xbox ? "xbox" : s == Style::PlayStation ? "playstation" : "wii (unchanged)");
        return s;
    }();
    return style;
}

// The glyph for a [controller] button name ("south", "left_trigger", ...), or nullptr.
const uint8_t* GlyphForSlot(const std::string& slotName, Style style) {
    const std::string slot = slotName.substr(0, slotName.find_first_of("@,"));
    const bool ps = style == Style::PlayStation;
    if (slot == "south") return ps ? kPsSouth : kXboxSouth;
    if (slot == "east") return ps ? kPsEast : kXboxEast;
    if (slot == "west") return ps ? kPsWest : kXboxWest;
    if (slot == "north") return ps ? kPsNorth : kXboxNorth;
    if (slot == "left_shoulder") return ps ? kPsL1 : kXboxLB;
    if (slot == "right_shoulder") return ps ? kPsR1 : kXboxRB;
    if (slot == "left_trigger") return ps ? kPsL2 : kXboxLT;
    if (slot == "right_trigger") return ps ? kPsR2 : kXboxRT;
    if (slot == "start") return ps ? kPsOptions : kXboxMenu;
    if (slot == "back") return ps ? kPsCreate : kXboxView;
    if (slot == "left_stick") return ps ? kPsL3 : kXboxLS;
    if (slot == "right_stick") return ps ? kPsR3 : kXboxRS;
    if (slot.rfind("dpad_", 0) == 0) return kDpad;
    return nullptr;
}

// Glyphs for the Wii A / B buttons as currently bound.
const uint8_t* WiiButtonGlyph(char wiiButton) {
    const char* key = wiiButton == 'a' ? "a" : "b";
    std::string slot = ConfigValue("controller", key);
    if (slot.empty()) {
        slot = wiiButton == 'a' ? "south" : "east";
    }
    const uint8_t* glyph = GlyphForSlot(slot, CurrentStyle());
    if (glyph == nullptr) {
        glyph = GlyphForSlot(wiiButton == 'a' ? "south" : "east", CurrentStyle());
    }
    return glyph;
}

struct Rect {
    int x0, y0, x1, y1; // inclusive
};

void Clear(uint8_t* rgba, uint32_t width, const Rect& r) {
    for (int y = r.y0; y <= r.y1; ++y) {
        std::memset(rgba + (static_cast<size_t>(y) * width + r.x0) * 4, 0, static_cast<size_t>(r.x1 - r.x0 + 1) * 4);
    }
}

// Bilinear sample of a straight-alpha RGBA image at (u, v) in pixels, premultiplied result.
void Sample(const uint8_t* img, uint32_t w, uint32_t h, float u, float v, float out[4]) {
    u = std::clamp(u - 0.5f, 0.0f, static_cast<float>(w - 1));
    v = std::clamp(v - 0.5f, 0.0f, static_cast<float>(h - 1));
    const uint32_t x0 = static_cast<uint32_t>(u), y0 = static_cast<uint32_t>(v);
    const uint32_t x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
    const float fx = u - x0, fy = v - y0;
    out[0] = out[1] = out[2] = out[3] = 0;
    const uint32_t xs[2] = {x0, x1}, ys[2] = {y0, y1};
    const float wx[2] = {1 - fx, fx}, wy[2] = {1 - fy, fy};
    for (int j = 0; j < 2; ++j) {
        for (int i = 0; i < 2; ++i) {
            const uint8_t* p = img + (static_cast<size_t>(ys[j]) * w + xs[i]) * 4;
            const float wt = wx[i] * wy[j];
            const float a = p[3] / 255.0f;
            out[0] += p[0] * a * wt;
            out[1] += p[1] * a * wt;
            out[2] += p[2] * a * wt;
            out[3] += a * wt;
        }
    }
}

// Draws an image into the rect (aspect kept, centred), over what is there. rotate: quarter turns
// clockwise. shade < 1 darkens. Images are kGlyphSize square unless w/h are given.
void Blit(uint8_t* rgba, uint32_t width, const Rect& r, const uint8_t* img, int rotate = 0, float shade = 1.0f,
          int offsetY = 0, uint32_t imgW = kGlyphSize, uint32_t imgH = kGlyphSize) {
    const int rw = r.x1 - r.x0 + 1, rh = r.y1 - r.y0 + 1;
    const bool sideways = rotate % 2 != 0;
    const float iw = static_cast<float>(sideways ? imgH : imgW), ih = static_cast<float>(sideways ? imgW : imgH);
    const float scale = std::min(rw / iw, rh / ih);
    const float dw = iw * scale, dh = ih * scale;
    const float ox = r.x0 + (rw - dw) * 0.5f, oy = r.y0 + (rh - dh) * 0.5f + offsetY;
    for (int y = r.y0; y <= r.y1; ++y) {
        for (int x = r.x0; x <= r.x1; ++x) {
            // position in the rotated image's frame, then back into the source image
            const float ru = (x + 0.5f - ox) / scale, rv = (y + 0.5f - oy) / scale;
            if (ru < 0 || rv < 0 || ru >= iw || rv >= ih) {
                continue;
            }
            float u = ru, v = rv;
            switch (rotate & 3) {
            case 1: u = rv; v = imgH - ru; break;           // 90 clockwise
            case 2: u = imgW - ru; v = imgH - rv; break;
            case 3: u = imgW - rv; v = ru; break;
            default: break;
            }
            float s[4];
            Sample(img, imgW, imgH, u, v, s);
            if (s[3] <= 0.0f) {
                continue;
            }
            uint8_t* d = rgba + (static_cast<size_t>(y) * width + x) * 4;
            const float da = d[3] / 255.0f;
            const float outA = s[3] + da * (1 - s[3]);
            for (int c = 0; c < 3; ++c) {
                const float premul = s[c] * shade + d[c] * da * (1 - s[3]);
                d[c] = static_cast<uint8_t>(std::clamp(premul / outA + 0.5f, 0.0f, 255.0f));
            }
            d[3] = static_cast<uint8_t>(std::clamp(outA * 255.0f + 0.5f, 0.0f, 255.0f));
        }
    }
}

bool Active() { return CurrentStyle() != Style::Wii; }

// --- Footer atlases (SELECT / CONFIRM / CANCEL bar on the menus) -----------------------------
// Two copies of the same 512x256 CI8 atlas (different palettes/data per menu set).
constexpr Rect kFooterSelect{167, 221, 200, 254}; // Wii Remote "point" icon -> D-pad
constexpr Rect kFooterB{204, 226, 224, 254};
constexpr Rect kFooterCap{226, 198, 254, 223};    // the A button seen from the side (pressed frame)
constexpr Rect kFooterA{228, 228, 254, 254};

void PatchFooter(uint8_t* rgba, uint32_t width, uint32_t height) {
    if (!Active() || width != 512 || height != 256) {
        return;
    }
    for (const Rect& r : {kFooterSelect, kFooterB, kFooterCap, kFooterA}) {
        Clear(rgba, width, r);
    }
    Blit(rgba, width, kFooterSelect, kDpad);
    Blit(rgba, width, kFooterB, WiiButtonGlyph('b'));
    Blit(rgba, width, kFooterCap, WiiButtonGlyph('a'), 0, 0.8f, 1);
    Blit(rgba, width, kFooterA, WiiButtonGlyph('a'));
}

// --- Pause menu atlas (256x256 CI8) ------------------------------------------------------------
constexpr Rect kPauseCap{227, 2, 255, 27};
constexpr Rect kPauseA{229, 30, 255, 56};

void PatchPause(uint8_t* rgba, uint32_t width, uint32_t height) {
    if (!Active() || width != 256 || height != 256) {
        return;
    }
    Clear(rgba, width, kPauseCap);
    Clear(rgba, width, kPauseA);
    Blit(rgba, width, kPauseCap, WiiButtonGlyph('a'), 0, 0.8f, 1);
    Blit(rgba, width, kPauseA, WiiButtonGlyph('a'));
}

// --- Parchment atlas (512x512 CI8): vertical "PUSH (A) + (B) BUTTON", reading top to bottom ----
constexpr Rect kParchA{467, 92, 503, 129};
constexpr Rect kParchB{467, 153, 503, 179};

void PatchParchment(uint8_t* rgba, uint32_t width, uint32_t height) {
    if (!Active() || width != 512 || height != 512) {
        return;
    }
    Clear(rgba, width, kParchA);
    Clear(rgba, width, kParchB);
    Blit(rgba, width, kParchA, WiiButtonGlyph('a'), 1);
    Blit(rgba, width, kParchB, WiiButtonGlyph('b'), 1);
}

// --- Title atlas (512x512 RGBA8): "PRESS (A) + (B)" -> "PRESS [START]" / "PRESS [OPTIONS]" ------
// The port accepts Start on the title screen (satbk_input.cpp), so this one is always edited.
constexpr Rect kTitleClear{163, 393, 299, 438}; // the "(A) + (B)" part, right of "PRESS"
constexpr Rect kTitlePill{170, 399, 170 + kPillStartW - 1, 399 + kPillStartH - 1};

void PatchTitle(uint8_t* rgba, uint32_t width, uint32_t height) {
    if (width != 512 || height != 512) {
        return;
    }
    Clear(rgba, width, kTitleClear);
    const bool ps = CurrentStyle() == Style::PlayStation;
    Blit(rgba, width, kTitlePill, ps ? kPillOptions : kPillStart, 0, 1.0f, 0, kPillStartW, kPillStartH);
}

// Base-level XXH64 of the guest texture data (USA RENE8P), as the renderer names its dumps.
struct Register {
    Register() {
        aurora_register_texture_patch(0x2ba9c7381206e16cull, 512, 256, 9 /* GX_TF_C8 */, PatchFooter);
        aurora_register_texture_patch(0xb12f4f4cf620fe5cull, 512, 256, 9, PatchFooter);
        aurora_register_texture_patch(0x29e5c1619b512833ull, 256, 256, 9, PatchPause);
        aurora_register_texture_patch(0xf4e490e6f5b76eecull, 512, 512, 9, PatchParchment);
        aurora_register_texture_patch(0xbdc2fd7fd63c05eaull, 512, 512, 6 /* GX_TF_RGBA8 */, PatchTitle);
    }
} s_register;

} // namespace
