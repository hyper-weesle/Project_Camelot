// Sonic and the Black Knight PC port: HLE for SDK functions that exist in this game's
// (newer) Revolution SDK build but not in Mario Kart Wii's, so upstream has no hook.
#include "hle_stubs.h"
#include "memory.h"
#include "system_bridge.h"
#include "ppc_runtime.h"

#include <cstdio>
#include <cstdlib>

#include <cstdint>

// VIGetScanMode (Sep 2008 VI): reads VI_DTV_STATUS / VI_CLK to report interlaced,
// non-interlaced or progressive output. The PC presenter is always progressive-capable,
// but the game only uses this to pick its render mode; report interlace (0) so it keeps
// the default 480i/EFB path that the GX HLE is built for.
extern "C" uint32_t SatbkVIGetScanMode_HLE() {
    return 0u; // VI_INTERLACE
}
PPC_NATIVE_OVERRIDE(80554DEC, SatbkVIGetScanMode_HLE, uint32_t, (), ());

// RSOLinkList for hbm_part.rso. The module's code was translated ahead of time at the address
// the game links it to (see tools/satbk_rso_image.py); if the link ever lands somewhere else,
// stop with a clear message instead of running mismatched code.
extern "C" void func_80577614(CpuContext* ctx);
extern "C" void SatbkRSOLinkList_Checked(CpuContext* ctx) {
    const uint32_t module = ctx->gpr[3];
    func_80577614(ctx);
    if (module == 0) {
        return;
    }
    const uint32_t textAddr = Memory::Read32(Memory::Read32(module + 0x0C) + 1 * 8);
    const uint32_t bssAddr = Memory::Read32(Memory::Read32(module + 0x0C) + 6 * 8);
    if (module != 0x900022E0u || textAddr != 0x900023D0u || bssAddr != 0x91754BE0u) {
        char details[256];
        std::snprintf(details, sizeof details,
                      "hbm_part.rso was linked at module=0x%08X text=0x%08X bss=0x%08X, but the port "
                      "was built for 0x900022E0/0x900023D0/0x91754BE0.",
                      module, textAddr, bssAddr);
        RuntimeCrash::WriteCrashArtifacts("rso_layout", details);
        SetRuntimeExitCode(EXIT_FAILURE);
        ShowRuntimeFatalPopup("Unexpected module layout", details);
        MarkFatalErrorReported();
        std::exit(EXIT_FAILURE);
    }
}
REGISTER_NATIVE_FUNCTION_AS(0x80577614, SatbkRSOLinkList_Checked, "SatbkRSOLinkList_Checked");

// ---------------------------------------------------------------------------------------------
// Interrupt delivery at interrupt-enable points.
//
// Guest threads run cooperatively on host fibers, so upstream only delivers VI retrace, alarm
// and audio DMA "interrupts" when the running thread blocks. Sonic and the Black Knight's boot
// (and its CRI middleware) busy-wait on work done by higher-priority threads that are woken by
// the VI retrace interrupt, which on hardware preempts the spinning thread. Model that: when
// the guest re-enables interrupts, deliver whatever interrupts are due and, if a higher
// priority thread became ready, reschedule exactly as the interrupt return path
// (__OSReschedule -> SelectThread(FALSE)) would.
// ---------------------------------------------------------------------------------------------
#include "fiber_manager.h"

#include <chrono>

void VI_HLE_ProcessRetracesDeferred(int maxToProcess);
extern "C" void SelectThread_801a9c08(CpuContext* ctx);
extern "C" void SatbkRebuildRunQueueBits();
extern "C" int32_t OS__DisableInterrupts_801a65ac();
extern "C" int32_t OS__RestoreInterrupts_801a65d4(int32_t level);

extern "C" void SatbkPreemptionPoint() {
    static thread_local bool busy = false;
    static thread_local std::chrono::steady_clock::time_point last{};
    if (busy || !Fiber::GuestFiberManager::IsInitialized() || VI_HLE_IsAdvancingRetrace()) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (now - last < std::chrono::milliseconds(1)) {
        return;
    }
    last = now;
    CpuContext* cpu = TryGetCpuContext();
    if (cpu == nullptr) {
        return;
    }
    busy = true;
    VI_HLE_ProcessRetracesDeferred(2);
    OS_HLE_ProcessAlarmsDeferred(8);
    Audio_HLE_PollDeferred();
    constexpr uint32_t kRunQueueBits = 0x808050E8u;
    SatbkRebuildRunQueueBits();
    if (Memory::Read32(kRunQueueBits) != 0) {
        const CpuContext saved = *cpu;
        const int32_t level = OS__DisableInterrupts_801a65ac();
        cpu->gpr[3] = 0; // SelectThread(FALSE): switch only to a strictly higher priority thread
        SelectThread_801a9c08(cpu);
        *cpu = saved;
        OS__RestoreInterrupts_801a65d4(level);
    }
    busy = false;
}

// ---------------------------------------------------------------------------------------------
// Skip the Wii Remote strap safety screen. 0x8034A0A0 advances the strap screen's state machine
// (load -> show for ~11 s -> fade -> done = 5); the boot sequence moves on once it reports done.
// Run it many times per frame so the screen finishes almost immediately. Set
// [game] show_strap_screen = true in Config.toml to keep it.
// ---------------------------------------------------------------------------------------------
#include "runtime_config.h"

#include <fstream>
#include <string>

extern "C" void func_8034A0A0(CpuContext* ctx);

namespace {
bool ShowStrapScreen() {
    static const bool show = [] {
        std::ifstream in(RuntimeConfigFile::ResolveConfigPath());
        std::string line;
        bool inGame = false;
        while (std::getline(in, line)) {
            const size_t first = line.find_first_not_of(" \t");
            if (first == std::string::npos || line[first] == '#') {
                continue;
            }
            if (line[first] == '[') {
                inGame = line.compare(first, 6, "[game]") == 0;
                continue;
            }
            if (inGame && line.compare(first, 17, "show_strap_screen") == 0) {
                return line.find("true") != std::string::npos;
            }
        }
        return false;
    }();
    return show;
}
bool HomeMenuAllowed() {
    static const bool allowed = [] {
        std::ifstream in(RuntimeConfigFile::ResolveConfigPath());
        std::string line;
        bool inGame = false;
        while (std::getline(in, line)) {
            const size_t first = line.find_first_not_of(" \t");
            if (first == std::string::npos || line[first] == '#') {
                continue;
            }
            if (line[first] == '[') {
                inGame = line.compare(first, 6, "[game]") == 0;
                continue;
            }
            if (inGame && line.compare(first, 9, "home_menu") == 0) {
                return line.find("true") != std::string::npos;
            }
        }
        return false;
    }();
    return allowed;
}
} // namespace

// The Wii HOME menu is off unless Config.toml [game] home_menu = true (launcher: Controls page).
extern "C" bool SatbkHomeMenuAllowed() { return HomeMenuAllowed(); }

extern "C" void SatbkStrapUpdate(CpuContext* ctx) {
    if (ShowStrapScreen()) {
        func_8034A0A0(ctx);
        return;
    }
    const CpuContext entry = *ctx;
    const uint32_t strap = entry.gpr[3];
    for (int i = 0; i < 400; ++i) {
        *ctx = entry;
        func_8034A0A0(ctx);
        const uint32_t state = Memory::Read32(strap + 8);
        if (state == 0 || state == 5) {
            break; // still loading its files (next frame), or finished
        }
    }
}
REGISTER_NATIVE_FUNCTION_AS(0x8034A0A0, SatbkStrapUpdate, "SatbkStrapUpdate");

// --- Start on the title screen --------------------------------------------------------------
// The title screen asks for A+B held together. Loading its layout arms "Start = A+B"; loading the
// main menu's layout (the prompt was accepted) disarms it. See the DVD patch in satbk_patches.py.
#include <atomic>
namespace { std::atomic<bool> s_titlePromptArmed{false}; }
extern "C" void SatbkTitlePromptArm() { s_titlePromptArmed.store(true); std::fprintf(stderr, "[satbk] title prompt: Start = A+B\n"); }
extern "C" bool SatbkTitlePromptArmed() { return s_titlePromptArmed.load(); }
extern "C" void SatbkTitlePromptDisarm() { if (s_titlePromptArmed.exchange(false)) std::fprintf(stderr, "[satbk] title prompt done\n"); }
