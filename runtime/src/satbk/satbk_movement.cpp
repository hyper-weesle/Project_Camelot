// Sonic and the Black Knight PC port: player movement options.
//
// Story stages keep Sonic on the level's path through PLAYER::GOCRunOnPath (vtable 0x806CE720).
// Its per-frame update (0x8021A250) and timed update (0x8021B8E4) are only reached through that
// vtable, so they are wrapped here; the wrappers run the original translated code.
#include "ppc_runtime.h"
#include "memory.h"
#include "recomp_mod_loader.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <cmath>
#include <cstring>
#include <fstream>
#include <string>

#include "runtime_config.h"

extern "C" void func_8021A250(CpuContext* ctx);
extern "C" void func_8021B8E4(CpuContext* ctx);

namespace {

// --- Modern movement: pull back to turn around -------------------------------------------------
// The path follower (the component's +0x1C object) keeps a direction word at +0x748: 1 runs the
// path forward, 0 runs it backward, and the camera follows whichever way it points. Holding the
// stick back flips it, so Sonic turns and runs back along the path; while the stick stays held
// back after the turn it is read as "forward" so he keeps running. Config.toml [game]
// modern_movement = true (launcher: Controls page) turns it on.
constexpr uint32_t kPathDirectionOffset = 0x748;
constexpr float kTurnThreshold = -0.75f;  // stick Y (down is negative) that asks for a turn
constexpr float kReleaseThreshold = -0.3f; // stick Y above this ends the held-back turn
constexpr int kTurnHoldSamples = 6;        // ~0.2 s of input samples held back before turning

std::atomic<bool> s_turnRequested{false};
std::atomic<uint64_t> s_pathUpdates{0}; // advances while a story path is being followed

bool ModernMovementEnabled() {
    static const bool enabled = [] {
        std::ifstream in(RuntimeConfigFile::ResolveConfigPath());
        std::string line;
        bool inGame = false;
        while (std::getline(in, line)) {
            const size_t first = line.find_first_not_of(" \t");
            if (first == std::string::npos || line[first] == '#') continue;
            if (line[first] == '[') {
                inGame = line.compare(first, 6, "[game]") == 0;
                continue;
            }
            if (inGame && line.compare(first, 15, "modern_movement") == 0) {
                return line.find("true") != std::string::npos;
            }
        }
        return false;
    }();
    return enabled;
}

// Dev: SATBK_PATHDUMP=N dumps the objects the component points at (+0x18, +0x1C) every N frames,
// printing only the words that changed since the previous dump.
void DumpObject(int slot, uint32_t base, uint32_t words) {
    static uint32_t prev[2][0x400] = {};
    static uint32_t prevBase[2] = {};
    const bool first = prevBase[slot] != base;
    prevBase[slot] = base;
    for (uint32_t i = 0; i < words; ++i) {
        const uint32_t w = Memory::Read32(base + i * 4);
        if (!first && w != prev[slot][i]) {
            float f, pf;
            std::memcpy(&f, &w, 4);
            std::memcpy(&pf, &prev[slot][i], 4);
            std::fprintf(stderr, "[pathdump]  obj%d +%03X %08X %12.4f (was %12.4f)\n", slot, i * 4, w, f, pf);
        }
        prev[slot][i] = w;
    }
}

void DumpComponent(const char* tag, uint32_t self) {
    static const long every = std::getenv("SATBK_PATHDUMP") ? std::atol(std::getenv("SATBK_PATHDUMP")) : 0;
    if (every <= 0 || self < 0x80000000u) return;
    static long calls = 0;
    if (++calls % every != 0) return;
    std::fprintf(stderr, "[pathdump] %s this=%08X frame=%ld\n", tag, self, calls);
    const uint32_t a = Memory::Read32(self + 0x18), b = Memory::Read32(self + 0x1C);
    if (a >= 0x80000000u) DumpObject(0, a, 0x400);
    if (b >= 0x80000000u) DumpObject(1, b, 0x400);
    if (b >= 0x80000000u) {
        std::fprintf(stderr, "[pathdump]  path740:");
        for (uint32_t o = 0x740; o < 0x780; o += 4) std::fprintf(stderr, " %08X", Memory::Read32(b + o));
        std::fprintf(stderr, "\n");
    }
}

} // namespace

extern "C" void SatbkRunOnPathUpdate(CpuContext* ctx) {
    DumpComponent("update", ctx->gpr[3]);
    s_pathUpdates.fetch_add(1, std::memory_order_relaxed);
    if (s_turnRequested.exchange(false, std::memory_order_acq_rel)) {
        const uint32_t follower = Memory::Read32(ctx->gpr[3] + 0x1C);
        if (follower >= 0x80000000u && follower < 0x94000000u) {
            const uint32_t dir = Memory::Read32(follower + kPathDirectionOffset);
            if (dir <= 1u) {
                Memory::Write32(follower + kPathDirectionOffset, dir ^ 1u);
            }
        }
    }
    // Dev: SATBK_PATHDIR=<value> forces the path direction word (+0x748 of the path follower).
    static const char* dir = std::getenv("SATBK_PATHDIR");
    if (dir != nullptr) {
        const uint32_t follower = Memory::Read32(ctx->gpr[3] + 0x1C);
        if (follower >= 0x80000000u) Memory::Write32(follower + 0x748, static_cast<uint32_t>(std::atoi(dir)));
    }
    func_8021A250(ctx);
}
REGISTER_NATIVE_FUNCTION_AS(0x8021A250, SatbkRunOnPathUpdate, "SatbkRunOnPathUpdate");

extern "C" void SatbkRunOnPathStep(CpuContext* ctx) {
    func_8021B8E4(ctx);
}
REGISTER_NATIVE_FUNCTION_AS(0x8021B8E4, SatbkRunOnPathStep, "SatbkRunOnPathStep");

// Called by the input layer for the player's stick, once per sample (satbk_input.cpp).
extern "C" void SatbkModernMovementFilter(float* x, float* y) {
    (void)x;
    if (!ModernMovementEnabled()) return;
    static int heldBack = 0;
    static bool reversedHold = false;
    static uint64_t lastPathUpdates = 0;
    static int idleSamples = 0;
    // Only on a story path: the follower must have run since recent samples.
    const uint64_t updates = s_pathUpdates.load(std::memory_order_relaxed);
    if (updates == lastPathUpdates) {
        if (++idleSamples > 30) { heldBack = 0; reversedHold = false; s_turnRequested.store(false); }
    } else {
        idleSamples = 0;
    }
    lastPathUpdates = updates;
    if (idleSamples > 30) return;

    if (reversedHold) {
        if (*y > kReleaseThreshold) {
            reversedHold = false;
        } else {
            *y = -*y; // still holding back after the turn: keep running the new way
            return;
        }
    }
    if (*y < kTurnThreshold) {
        if (++heldBack >= kTurnHoldSamples) {
            heldBack = 0;
            reversedHold = true;
            s_turnRequested.store(true, std::memory_order_release);
            *y = -*y;
        }
    } else {
        heldBack = 0;
    }
}
