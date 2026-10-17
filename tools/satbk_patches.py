#!/usr/bin/env python3
"""SatBK-specific source edits applied to the runtime after the automatic MKW->SatBK
address remap (tools/satbk_remap.py). Every edit asserts its anchor so a drifted
upstream fails loudly instead of silently skipping."""
import os
import sys

ROOT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), '..', 'runtime')
os.chdir(ROOT)


def patch(path, pairs):
    s = open(path).read()
    for old, new in pairs:
        if old not in s:
            raise SystemExit(f'patch anchor missing in {path}:\n{old}')
        s = s.replace(old, new, 1)
    open(path, 'w').write(s)


def remove(path):
    if os.path.exists(path):
        os.remove(path)


# --- MKW-only modules -----------------------------------------------------------------
for f in ['src/hle/strap_scene.cpp', 'src/hle/egg_decomp.cpp', 'src/hle/task_thread.cpp',
          'src/hle/gx/gx_egg.cpp', 'src/hle/trk.cpp']:
    remove(f)

# --- boot / low memory ------------------------------------------------------------------
s = open('src/system_bridge.cpp').read()
a = s.index("    // StaticR.rel ctors must be run manually since we aren't using OSLink")
end_marker = '    g_suppressSehReporting = false;\n    RT_LOG(RT_TAG_RUNTIME) << "Executed " << count << " static constructors." << std::endl;'
b = s.index(end_marker)
s = s[:a] + '    // Sonic and the Black Knight has no StaticR.rel; every constructor lives in the DOL.\n    g_suppressSehReporting = false;\n' + s[b + len(end_marker):]
open('src/system_bridge.cpp', 'w').write(s)
patch('src/system_bridge.cpp', [
    ('entries.push_back({0x80000000u, 0x524D4350u, "Disc game code"}); // RMCP',
     'entries.push_back({0x80000000u, 0x52454E45u, "Disc game code"}); // RENE'),
    ('entries.push_back({0x80000004u, 0x30310100u, "Disc maker/id"});  // 01 + disc 1',
     'entries.push_back({0x80000004u, 0x38500000u, "Disc maker/id"});  // 8P, disc 0, rev 0'),
    ('entries.push_back({0x80003180u, 0x524D4350u, "OS app game code"}); // RMCP',
     'entries.push_back({0x80003180u, 0x52454E45u, "OS app game code"}); // RENE'),
    ('entries.push_back({0x80003194u, 0x524D4350u, "OS app gamename"});  // RMCP',
     'entries.push_back({0x80003194u, 0x52454E45u, "OS app gamename"});  // RENE'),
])

# --- EGG/MKW presentation helpers -------------------------------------------------------
patch('src/dynamic_aspect.cpp', [
    ('void AssertOffscreenScreenBypass() {',
     'void AssertOffscreenScreenBypass() {\n    return; // SatBK: the EGG/MKW screen graph does not exist in this game.'),
    ('void ApplyEggScreenRecords(uint32_t surfaceWidth, uint32_t surfaceHeight) {',
     'void ApplyEggScreenRecords(uint32_t surfaceWidth, uint32_t surfaceHeight) {\n    (void)surfaceWidth; (void)surfaceHeight;\n    return; // SatBK: no EGG screen records.'),
])
patch('src/music_attenuation.cpp', [
    ('    uint32_t soundManager = 0;\n    uint32_t archivePlayer = 0;',
     '    return 0; // SatBK: CRI audio, no NW4R sound players to attenuate.\n    uint32_t soundManager = 0;\n    uint32_t archivePlayer = 0;'),
    # The WinRT media-session monitor needs MSVC's C++/WinRT headers; llvm-mingw builds skip it.
    ('#if defined(_WIN32)\n#include <winrt/base.h>', '#if defined(_WIN32) && defined(_MSC_VER)\n#include <winrt/base.h>'),
    ('#if defined(_WIN32)\nvoid MonitorWindowsMediaSessions', '#if defined(_WIN32) && defined(_MSC_VER)\nvoid MonitorWindowsMediaSessions'),
    ('#if defined(_WIN32)\n    // The process owns this monitor', '#if defined(_WIN32) && defined(_MSC_VER)\n    // The process owns this monitor'),
])
patch('src/hle/vi.cpp', [
    ('''            uint32_t sSystemPtr = Memory::Read32(kEggSSystemAddr);
            if (sSystemPtr != 0) {
                InvokeIndirectCpu(postCb, ctx);
            }''', '''            // SatBK has no EGG::BaseSystem; the post-retrace callback is always safe to run.
            InvokeIndirectCpu(postCb, ctx);'''),
    ('constexpr uint32_t kEggSSystemAddr = 0x80386F60;', ''),
])
patch('include/abi_bridge.h', [
    ('''    if (target == 0x8023BD38u) {
        // ScnRenderer::createPath receives the post-processing path mask in r4.
        ctx->gpr[4] = RuntimeGameGraphicsOptions::FilterScnRendererPathMask(ctx->gpr[4]);
    }''', '''    // MKW-only hook (ScnRenderer::createPath path mask) removed for SatBK.
    (void)target; (void)ctx;'''),
])

# --- SDK globals the automatic remap could only approximate --------------------------------
patch('src/hle/storage/dvd.cpp', [
    ('Memory::Write32(0x8038667C, 1); // CancelAllSync complete',
     'Memory::Write32(0x8080519C, 1); // CancelAllSync complete'),
])
patch('src/hle/os/os_interrupt.cpp', [
    ('constexpr uint32_t kDefaultCallbackAddr = 0x801b0000u - 0x43f4u; // 0x805469F4',
     'constexpr uint32_t kDefaultCallbackAddr = 0x805469F4u; // SatBK equivalent of MKW 0x801ABC0C'),
])
patch('src/hle/gx/gx_init.cpp', [
    ('constexpr uint32_t kGXDataAddr = 0x803437C0u;',
     'constexpr uint32_t kGXDataAddr = 0x807805E0u; // __GXData (FifoObj + 0x80)'),
])

# --- MKW-only native hooks ------------------------------------------------------------------
patch('include/native_cpu_calls.inc', [
    ('MKW_KNOWN_TYPED_NATIVE(0x8016EAB0u, GX__DrawDone_8016eab0);\n', ''),
    ('MKW_KNOWN_NATIVE_CPU_CALL(0x800847C0u, nw4r__lyt__detail__DrawQuad_800847c0);\n'
     'MKW_KNOWN_NATIVE_CPU_CALL(0x80084D20u, nw4r__lyt__detail__DrawQuad_80084d20);\n', ''),
])
patch('src/hle/gx/gx_dl.cpp', [
    ('REGISTER_NATIVE_FUNCTION(0x800847C0, nw4r__lyt__detail__DrawQuad_800847c0);', '// SatBK: MKW nw4r::lyt DrawQuad hook removed'),
    ('REGISTER_NATIVE_FUNCTION(0x80084D20, nw4r__lyt__detail__DrawQuad_80084d20);', '// SatBK: MKW nw4r::lyt DrawQuad hook removed'),
])
patch('src/hle/storage/nand_isfs.cpp', [
    ('REGISTER_NATIVE_FUNCTION_AS(0x801938FC, NAND_IOS_OpenBody_HLE_801938FC,',
     '// SatBK-REMOVED REGISTER_NATIVE_FUNCTION_AS(0x801938FC, NAND_IOS_OpenBody_HLE_801938FC,'),
])
patch('src/hle/os/os_init.cpp', [
    ('extern "C" void func_8055531C(CpuContext* ctx);\n', ''),
    ('''extern "C" void StaticRProlog_RecompModInit_8055531c(CpuContext* ctx) {
    RecompMod::RunMemoryInitializers();
    func_8055531C(ctx);
    RecompMod::RunPostRelInitializers();
}''', '// SatBK: no StaticR.rel prolog.'),
    ('REGISTER_NATIVE_FUNCTION_AS(0x8055531C, StaticRProlog_RecompModInit_8055531c,',
     '// SatBK-REMOVED REGISTER_NATIVE_FUNCTION_AS(0x8055531C, StaticRProlog_RecompModInit_8055531c,'),
])
patch('src/hle/os/os_thread.cpp', [
    ('''    switch (entryFunc) {
    case 0x805529A8u:
    case 0x80552A74u:
        return true;
    default:
        return false;
    }''', '''    (void)entryFunc; // MKW THP decoder threads; SatBK uses CRI Sofdec instead.
    return false;'''),
])
print('satbk patches applied')

# --- branding / user data separation -------------------------------------------------------
s = open('src/product/base_product.cpp').read()
import re as _re
s2 = _re.sub(r'"[^"]*Mario Kart[^"]*"|"WiiCompiled"', '"Sonic and the Black Knight"', s)
open('src/product/base_product.cpp', 'w').write(s2)
patch('include/runtime_config.h', [
    ('inline constexpr const char* kApplicationDirectoryName = "WiiCompiled";',
     'inline constexpr const char* kApplicationDirectoryName = "SonicBlackKnightPC";'),
    ('    output << "# WiiCompiled user configuration\\n"\n              "# Set paths.dvd_root to an extracted Mario Kart Wii DATA directory.\\n\\n"',
     '    output << "# Sonic and the Black Knight PC port - user configuration\\n"\n              "# Game data is read from GameData next to the executable unless paths.dvd_root says otherwise.\\n\\n"'),
    ('inline bool DiscordPresenceEnabled(bool fallback = true) {',
     'inline bool DiscordPresenceEnabled(bool fallback = false) {'),
])
patch('src/main.cpp', [
    ('DiscordPresence::Initialize(RuntimeConfigFile::DiscordClientId(), "Mario Kart Wii");',
     'DiscordPresence::Initialize(RuntimeConfigFile::DiscordClientId(), "Sonic and the Black Knight");'),
])
# Default DVD root: GameData beside the executable (the extractor's output folder).
patch('include/runtime_config.h', [
    ('''inline std::filesystem::path ResolvedDvdRoot() {
    const std::string configured = DvdRoot();
    return configured.empty() ? std::filesystem::path{} : ResolveRelativeToConfig(configured);
}''', '''inline std::filesystem::path ResolvedDvdRoot() {
    const std::string configured = DvdRoot();
    if (configured.empty()) {
        const auto exeDir = ExecutableDirectory();
        return exeDir ? (*exeDir / "GameData") : std::filesystem::path{};
    }
    return ResolveRelativeToConfig(configured);
}'''),
])
patch('src/hle/storage/dvd.cpp', [
    ('"to your extracted Mario Kart Wii DATA directory.\\n");',
     '"to the GameData folder made by satbk_extract from your own disc.\\n");'),
    ('details += "\\n\\nSet [paths] dvd_root in Config.toml to the extracted "\n               "Mario Kart Wii DATA directory.";',
     'details += "\\n\\nRun Setup to extract your own Sonic and the Black Knight (USA) disc into "\n               "GameData next to the executable, or set [paths] dvd_root in Config.toml.";'),
])
print('satbk branding patches applied')

# --- virtual Wii Remote + Nunchuk (src/satbk_input.cpp) ---------------------------------------
patch('src/hle/input/wpad.cpp', [
    ('''    if (typePtr != 0) {
        Memory::Write32(typePtr, WpadContract::kExtensionCore);
    }
    return WpadContract::kErrorNoController;
}''', '''    // SatBK: a PC gamepad/keyboard on this port stands in for a Wii Remote + Nunchuk.
    if (SatbkRemoteConnected(chan)) {
        if (typePtr != 0) {
            Memory::Write32(typePtr, 1u); // WPAD_DEV_FREESTYLE
        }
        return kStatusOk;
    }
    if (typePtr != 0) {
        Memory::Write32(typePtr, WpadContract::kExtensionCore);
    }
    return WpadContract::kErrorNoController;
}'''),
    ('''    (void)infoPtr;
    return CompleteWpadRequest(chan, callback, WpadContract::kErrorNoController);''',
     '''    if (SatbkRemoteConnected(chan)) {
        if (infoPtr != 0) {
            for (uint32_t o = 0; o < 0x18; o += 4) Memory::Write32(infoPtr + o, 0);
            Memory::Write32(infoPtr + 0x08, 1); // attach: Nunchuk present
            Memory::Write8(infoPtr + 0x14, 4);  // battery: full
        }
        return CompleteWpadRequest(chan, callback, kStatusOk);
    }
    (void)infoPtr;
    return CompleteWpadRequest(chan, callback, WpadContract::kErrorNoController);'''),
    ('''    (void)ledMask;
    return CompleteWpadRequest(chan, callback, WpadContract::kErrorNoController);''',
     '''    (void)ledMask;
    return CompleteWpadRequest(chan, callback,
                               SatbkRemoteConnected(chan) ? kStatusOk : WpadContract::kErrorNoController);'''),
])
s = open('src/hle/input/wpad.cpp').read()
s = s.replace('#include "hle_stubs.h"', '#include "hle_stubs.h"\nextern "C" bool SatbkRemoteConnected(uint32_t chan);', 1)
open('src/hle/input/wpad.cpp', 'w').write(s)
print('satbk input patches applied')

# The game's own __start -> __init_user -> __init_cpp runs the static constructors; running
# them here as well (as upstream does) would construct every global twice.
patch('src/system_bridge.cpp', [
    ('    const uint32_t dolCtorEnd = 0x80663300;',
     '    const uint32_t dolCtorEnd = dolCtorStart; // SatBK: ctors run by the game\'s __init_user'),
])

# Sonic and the Black Knight's TMD requires IOS55 (title 00000001-00000037), not MKW's IOS36.
patch('src/system_bridge.cpp', [
    ('    constexpr uint32_t kIos36Version = 0x00240E18u;',
     '    constexpr uint32_t kIos36Version = 0x0037161Eu; // SatBK: IOS55 v5662'),
])
patch('src/system_bridge.cpp', [
    ('    entries.push_back({0x80003144u, kIos36Date, "IOS36 date"});',
     '    entries.push_back({0x80003144u, kIos36Date, "IOS36 date"});\n'
     '    // The apploader stores the TMD\'s required IOS here; OSInit refuses to run if it differs.\n'
     '    entries.push_back({0x80003188u, kIos36Version, "Required IOS version"});'),
])

# Deliver due interrupts (and reschedule) when the guest re-enables interrupts; see
# satbk/runtime_src/satbk_hle.cpp SatbkPreemptionPoint.
patch('src/hle/os/os_interrupt.cpp', [
    ('''extern "C" int32_t OS__RestoreInterrupts_801a65d4(int32_t level)
{
    const bool prev = g_interrupts_enabled.exchange(level != 0);
    UpdateCurrentContextInterruptFlag(level != 0);
    return prev ? 1 : 0;
}''', '''extern "C" void SatbkPreemptionPoint();
extern "C" int32_t OS__RestoreInterrupts_801a65d4(int32_t level)
{
    const bool prev = g_interrupts_enabled.exchange(level != 0);
    UpdateCurrentContextInterruptFlag(level != 0);
    if (level != 0 && !prev) {
        SatbkPreemptionPoint();
    }
    return prev ? 1 : 0;
}'''),
    ('''extern "C" int32_t OS__EnableInterrupts_801a65c0()
{
    const bool prev = g_interrupts_enabled.exchange(true);
    UpdateCurrentContextInterruptFlag(true);
    return prev ? 1 : 0;
}''', '''extern "C" int32_t OS__EnableInterrupts_801a65c0()
{
    const bool prev = g_interrupts_enabled.exchange(true);
    UpdateCurrentContextInterruptFlag(true);
    if (!prev) {
        SatbkPreemptionPoint();
    }
    return prev ? 1 : 0;
}'''),
])

# The startup cover is lifted by MKW's strap-screen hook (removed); for SatBK lift it as soon as
# boot shaders are ready.
patch('src/settings_overlay.cpp', [
    ('''    return !g_strapInputAccepted.load(std::memory_order_acquire) ||
           !g_bootShadersReady.load(std::memory_order_acquire) ||
           g_presentedFrame < g_startupDismissFrame.load(std::memory_order_relaxed);''',
     '''    return !g_bootShadersReady.load(std::memory_order_acquire);'''),
    ('        constexpr const char* kTitle = "WiiCompiled";', '        constexpr const char* kTitle = "Sonic and the Black Knight";'),
    ('    ImGui::TextUnformatted("WiiCompiled");', '    ImGui::TextUnformatted("Sonic and the Black Knight");'),
])

# Developer trace of GX primitives (SATBK_GXTRACE=<count>), inert otherwise.
patch('src/hle/gx/gx_vertex.cpp', [
    ('''    ServiceDeferredTimingDuringGxWork();
    ApplyAuroraVtxStateForBegin(static_cast<GXVtxFmt>(vf));''', '''    ServiceDeferredTimingDuringGxWork();
    {
        static const char* traceSpec = std::getenv("SATBK_GXTRACE");
        static long traceLeft = traceSpec ? std::atol(traceSpec) : 0;
        if (traceLeft > 0) {
            --traceLeft;
            char line[512];
            int n = std::snprintf(line, sizeof line, "[gxtrace] Begin prim=%02X fmt=%u nv=%u desc:", t, vf, nv);
            for (int a = 0; a < 26 && n < 480; ++a) {
                if (g_hleGxState.vtxDesc[a] != GX_NONE) {
                    const auto& f = g_hleGxState.vtxAttrFmt[vf & 7][a];
                    n += std::snprintf(line + n, sizeof line - n, " %d:%d(c%d t%d f%d)", a,
                                       (int)g_hleGxState.vtxDesc[a], (int)f.cnt, (int)f.type, (int)f.frac);
                }
            }
            std::fprintf(stderr, "%s\\n", line);
        }
    }
    ApplyAuroraVtxStateForBegin(static_cast<GXVtxFmt>(vf));'''),
])
s = open('src/hle/gx/gx_vertex.cpp').read()
if '#include <cstdlib>' not in s:
    s = '#include <cstdlib>\n#include <cstdio>\n' + s
open('src/hle/gx/gx_vertex.cpp', 'w').write(s)
patch('src/hle/gx/gx_fifo.cpp', [
    ('''void SubmitAttribute(GXAttr attr, float* comps, const VtxAttrFmt& fmt, const u32* rawComps) {
''', '''void SubmitAttribute(GXAttr attr, float* comps, const VtxAttrFmt& fmt, const u32* rawComps) {
    {
        static const char* traceSpec = std::getenv("SATBK_GXTRACE");
        static long traceLeft = traceSpec ? std::atol(traceSpec) * 4 : 0;
        if (traceLeft > 0) {
            --traceLeft;
            std::fprintf(stderr, "[gxtrace]   attr %d comps %g %g %g %g raw=%d [%d %d %d]\\n", (int)attr, comps[0], comps[1],
                         comps[2], comps[3], rawComps != nullptr, rawComps ? (int)rawComps[0] : 0,
                         rawComps ? (int)rawComps[1] : 0, rawComps ? (int)rawComps[2] : 0);
        }
    }
'''),
])
s = open('src/hle/gx/gx_fifo.cpp').read()
if '#include <cstdlib>' not in s:
    s = '#include <cstdlib>\n#include <cstdio>\n' + s
open('src/hle/gx/gx_fifo.cpp', 'w').write(s)
patch('src/hle/gx/gx_fifo.cpp', [
    ('''                GXColor color{};
                DecodeColorBytes(colorBytes, fmt.type, fmt.cnt, color);

                if (!recordOnly) {''', '''                GXColor color{};
                DecodeColorBytes(colorBytes, fmt.type, fmt.cnt, color);
                {
                    static const char* traceSpec = std::getenv("SATBK_GXTRACE");
                    static long traceLeft = traceSpec ? std::atol(traceSpec) * 4 : 0;
                    if (traceLeft > 0) {
                        --traceLeft;
                        std::fprintf(stderr, "[gxtrace]   color %02X%02X%02X%02X (type %d)\\n", color.r, color.g,
                                     color.b, color.a, (int)fmt.type);
                    }
                }

                if (!recordOnly) {'''),
])
# GX state-call trace for SATBK_GXTRACE.
def _trace_fn(path, sig, fmt, args):
    s = open(path).read()
    i = s.index(sig)
    j = s.index('{', i) + 1
    inject = ('\n    { static const char* ts = std::getenv("SATBK_GXTRACE"); static long tl = ts ? std::atol(ts) * 3 : 0;'
              ' if (tl > 0) { --tl; std::fprintf(stderr, "[gxtrace] ' + fmt + '\\n", ' + args + '); } }')
    s = s[:j] + inject + s[j:]
    if '#include <cstdlib>' not in s:
        s = '#include <cstdlib>\n#include <cstdio>\n' + s
    open(path, 'w').write(s)
_trace_fn('src/hle/gx/gx_pixel.cpp', 'extern "C" void GX__SetBlendMode_8017277c(', 'BlendMode t=%u s=%u d=%u op=%u', 't, s, d, op')
_trace_fn('src/hle/gx/gx_tev.cpp', 'extern "C" void GX__SetTevOp_80171c4c(', 'TevOp s=%u m=%u', 's, m')
_trace_fn('src/hle/gx/gx_tev.cpp', 'extern "C" void GX__SetTevOrder_8017214c(', 'TevOrder s=%u coord=%u map=%u col=%u', 's, c, m, col')
_trace_fn('src/hle/gx/gx_tev.cpp', 'extern "C" void GX__SetNumTevStages_801722a8(', 'NumTevStages %u', 'n')
_trace_fn('src/hle/gx/gx_tev.cpp', 'extern "C" void GX__SetTevColorIn_80171ce0(', 'TevColorIn s=%u %u %u %u %u', 's, a, b, c, d')
_trace_fn('src/hle/gx/gx_tev.cpp', 'extern "C" void GX__SetTevAlphaIn_80171d20(', 'TevAlphaIn s=%u %u %u %u %u', 's, a, b, c, d')
_trace_fn('src/hle/gx/gx_lighting.cpp', 'extern "C" void GX__SetNumChans_8017054c(', 'NumChans %u', 'n')
_trace_fn('src/hle/gx/gx_lighting.cpp', 'extern "C" void GX__SetChanCtrl_80170570(', 'ChanCtrl ch=%u en=%u amb=%u mat=%u lm=%u df=%u af=%u', 'ch, en, as, ms, lm, df, af')
_trace_fn('src/hle/gx/gx_lighting.cpp', 'extern "C" void GX__SetChanMatColor_80170474(', 'ChanMatColor c=%u cp=%08X', 'c, cp')
_trace_fn('src/hle/gx/gx_vertex.cpp', 'extern "C" void GX__SetNumTexGens_8016e5a4(', 'NumTexGens %u', 'n')
_trace_fn('src/hle/gx/gx_vertex.cpp', 'extern "C" void GX__SetTexCoordGen2_8016e37c(', 'TexCoordGen2 dc=%u f=%u sp=%u m=%u', 'dc, f, sp, m')
_trace_fn('src/hle/gx/gx_texture.cpp', 'extern "C" void GX__LoadTexObj_80170f2c(', 'LoadTexObj obj=%08X map=%u', 'oa, tid')
_trace_fn('src/hle/gx/gx_pixel.cpp', 'extern "C" void GX__SetZMode_80172824(', 'ZMode cmp=%u func=%u upd=%u', 'ce, f, ue')
_trace_fn('src/hle/gx/gx_pixel.cpp', 'extern "C" void GX__SetColorUpdate_801727cc(', 'ColorUpdate %u', 'en')
_trace_fn('src/hle/gx/gx_pixel.cpp', 'extern "C" void GX__SetAlphaUpdate_801727f8(', 'AlphaUpdate %u', 'en')
_trace_fn('src/hle/gx/gx_pixel.cpp', 'extern "C" void GX__SetAlphaCompare_80172088(', 'AlphaCompare %u %u %u %u %u', 'c0, r0, op, c1, r1')
_trace_fn('src/hle/gx/gx_pixel.cpp', 'extern "C" void GX__SetCullMode_8016f3b8(', 'CullMode %u', 'm')
_trace_fn('src/hle/gx/gx_transform.cpp', 'extern "C" void GX__SetViewport_801733b4(', 'Viewport %g %g %g %g %g %g', 'l, t, w, h, nz, fz')
# Dev-only render experiments (inert unless the env var is set).
patch('src/hle/gx/gx_pixel.cpp', [
    ('    GXSetZMode(static_cast<GXBool>(ce), static_cast<GXCompare>(f), static_cast<GXBool>(ue));',
     '    { static const bool nodepth = std::getenv("SATBK_NODEPTH") != nullptr; if (nodepth) { ce = 0; ue = 0; } }\n'
     '    GXSetZMode(static_cast<GXBool>(ce), static_cast<GXCompare>(f), static_cast<GXBool>(ue));'),
])
patch('src/hle/gx/gx_copy.cpp', [
    ('#include "gx_internal.h"\n', '#include "gx_internal.h"\nextern "C" void SatbkDevTestQuad();\n'),
    ('''    EnsureAuroraFrameActive();
    // GX copies are FIFO-ordered on hardware.''', '''    EnsureAuroraFrameActive();
    SatbkDevTestQuad();
    // GX copies are FIFO-ordered on hardware.'''),
])
patch('src/hle/gx/gx_transform.cpp', [
    ("""    SwapBeF32ArrayToHost(raw, m, 16);
    GXSetProjection(m, (GXProjectionType)pt);""", """    SwapBeF32ArrayToHost(raw, m, 16);
    { static const char* ts = std::getenv("SATBK_GXTRACE"); static long tl = ts ? std::atol(ts) : 0; if (tl > 0) { --tl;
      std::fprintf(stderr, "[gxtrace] Projection t=%u [%g %g %g %g | %g %g %g %g | %g %g %g %g | %g %g %g %g]\\n", pt,
        m[0],m[1],m[2],m[3],m[4],m[5],m[6],m[7],m[8],m[9],m[10],m[11],m[12],m[13],m[14],m[15]); } }
    GXSetProjection(m, (GXProjectionType)pt);"""),
    ("""    SwapBeF32ArrayToHost(raw, m, 12);
    GXLoadPosMtxImm((float(*)[4])m, id);""", """    SwapBeF32ArrayToHost(raw, m, 12);
    { static const char* ts = std::getenv("SATBK_GXTRACE"); static long tl = ts ? std::atol(ts) : 0; if (tl > 0) { --tl;
      std::fprintf(stderr, "[gxtrace] PosMtx id=%u [%g %g %g %g | %g %g %g %g | %g %g %g %g]\\n", id,
        m[0],m[1],m[2],m[3],m[4],m[5],m[6],m[7],m[8],m[9],m[10],m[11]); } }
    GXLoadPosMtxImm((float(*)[4])m, id);"""),
])
_trace_fn('src/hle/gx/gx_transform.cpp', 'extern "C" void GX__SetProjectionv_80173080(', 'Projectionv %08X', 'pa')
_trace_fn('src/hle/gx/gx_transform.cpp', 'extern "C" void GX__SetCurrentMtx_80173214(', 'CurrentMtx %u', 'id')
_trace_fn('src/hle/gx/gx_transform.cpp', 'extern "C" void GX__SetScissor_80173430(', 'Scissor %u %u %u %u', 'l, t, w, h')
# Scheduler self-heal: RunQueueBits must equal the set of non-empty run queues. Some path in this
# game's SDK leaves a thread queued without its bit (seen: the asset loader threads after the
# title movie, which then never run). Rebuild the mask from the queues before every decision.
patch('src/hle/os/os_scheduler.cpp', [
    ('''extern "C" void SelectThread_801a9c08(CpuContext* ctx)
{''', '''extern "C" void SatbkRebuildRunQueueBits() {
    uint32_t mask = 0;
    for (uint32_t prio = 0; prio < 32; ++prio) {
        if (::Memory::Read32(kThreadQueueArrayAddr + prio * 8u) != 0) {
            mask |= 1u << (31u - prio);
        }
    }
    const uint32_t old = ::Memory::Read32(kSchedulerPendingFlagAddr);
    if (old != mask) {
        static int logged = 0;
        if (logged < 20) {
            ++logged;
            RT_LOG(RT_TAG_OS) << "[satbk] RunQueueBits 0x" << std::hex << old << " -> 0x" << mask << std::dec
                              << std::endl;
        }
        ::Memory::Write32(kSchedulerPendingFlagAddr, mask);
    }
}

extern "C" void SelectThread_801a9c08(CpuContext* ctx)
{
    SatbkRebuildRunQueueBits();'''),
])
# VIGetRetraceCount polling. The game's frame pacing busy-waits on VIGetRetraceCount. On the Wii
# the main thread also sleeps inside GX sync while the GPU draws, which is when lower-priority
# threads (the CRI file/decode server) run; with HLE GX nothing ever sleeps, so those threads
# starve and loading never finishes. When the same thread polls an unchanged count with
# interrupts enabled, wait for the next retrace (VIWaitForRetrace) so other threads can run.
patch('src/hle/vi.cpp', [
    ('''extern "C" void VIGetRetraceCount_HLE_801baba4(CpuContext* ctx)
{
    uint32_t count = 0;
    {
        std::lock_guard<std::mutex> lock(g_viMutex);
        EnsureInitializedLocked();
        count = g_vi.retraceCount;
    }''', '''namespace OsHleInternal { extern std::atomic<bool> g_interrupts_enabled; }
extern "C" void VIWaitForRetrace_HLE_801b99ec(CpuContext* ctx);
extern "C" void VIGetRetraceCount_HLE_801baba4(CpuContext* ctx)
{
    uint32_t count = 0;
    {
        std::lock_guard<std::mutex> lock(g_viMutex);
        EnsureInitializedLocked();
        count = g_vi.retraceCount;
    }
    {
        static thread_local uint32_t lastThread = 0, lastCount = 0, repeats = 0;
        const uint32_t thread = Memory::Read32(0x800000E4u);
        repeats = (thread == lastThread && count == lastCount) ? repeats + 1 : 0;
        if (ctx != nullptr && thread != 0 && repeats >= 3 &&
            Fiber::GuestFiberManager::IsInitialized() && !VI_HLE_IsAdvancingRetrace() &&
            OsHleInternal::g_interrupts_enabled.load(std::memory_order_acquire)) {
            VIWaitForRetrace_HLE_801b99ec(ctx);
            std::lock_guard<std::mutex> lock(g_viMutex);
            count = g_vi.retraceCount;
            repeats = 0;
        }
        lastThread = thread;
        lastCount = count;
    }'''),
])
# OSSuspendThread: upstream hooks it at an MKW address the remap could not place; without the HLE the
# translated SDK version never parks the guest fiber, so OSResumeThread (HLE) cannot wake the thread
# (the CRI ADX/Sofdec server thread, which suspends itself every loop, then never runs again).
patch('src/hle/os/os_thread.cpp', [
    ('// SATBK-UNMAPPED(MKW 801aa6a8): PPC_NATIVE_OVERRIDE_VOID(801AA6A8, OSSuspendThread_HLE_801aa6a8, (CpuContext* ctx), (ctx));',
     'PPC_NATIVE_OVERRIDE_VOID(80545504, OSSuspendThread_HLE_801aa6a8, (CpuContext* ctx), (ctx));'),
])
# Disc titles live under title/00010000 (MKW's channel-style 00010004 came from upstream).
patch('src/hle/esp.cpp', [('static constexpr uint32_t MKW_TITLE_ID_HI = 0x00010004;', 'static constexpr uint32_t MKW_TITLE_ID_HI = 0x00010000;')])
patch('src/hle/storage/nand_internal.h', [('constexpr uint32_t kNandTitleIdHi = 0x00010004;', 'constexpr uint32_t kNandTitleIdHi = 0x00010000;')])
# --- Small-data (r13-relative) offsets -------------------------------------------------------
# Upstream addresses some SDK globals as r13 - offset with MKW's offsets; the address remap does
# not see those. These are this game's offsets, read from the same instruction in each matched
# SDK function (tools: sdamap). Writing MKW's offsets would scribble over unrelated variables.
patch('src/hle/audio/ax_mix.cpp', [
    ('Memory::Read16(r13 - 0x73fcu)', 'Memory::Read16(r13 - 0x3a34u)'),
    ('Memory::Read16(r13 - 0x7400u)', 'Memory::Read16(r13 - 0x3a38u)'),
    ('Memory::Read16(r13 - 0x73feu)', 'Memory::Read16(r13 - 0x3a36u)'),
    ('Memory::Write32(r13 - 0x66d8u, 1);', 'Memory::Write32(r13 - 0x1278u, 1);'),
    ('Memory::Write32(r13 - 0x66dcu, 0);', 'Memory::Write32(r13 - 0x127cu, 0);'),
])
patch('src/hle/os/os_interrupt.cpp', [
    ('const uint32_t kCallbackPtrAddr = r13 - 0x62b8u;', 'const uint32_t kCallbackPtrAddr = r13 - 0x15b0u;'),
    ('const uint32_t kHandlerActiveAddr = r13 - 0x62c0u;', 'const uint32_t kHandlerActiveAddr = r13 - 0x15b8u;'),
    ('Memory::Read32(ctx->gpr[13] + -25620)', 'Memory::Read32(ctx->gpr[13] + -4396)'),
    ('Memory::Write32(ctx->gpr[13] + -25620, newBufLo)', 'Memory::Write32(ctx->gpr[13] + -4396, newBufLo)'),
])
patch('src/hle/os/os_internal.h', [
    ('constexpr uint32_t kAlarmQueueOffsetFromR13 = 0x6360u;', 'constexpr uint32_t kAlarmQueueOffsetFromR13 = 0x1660u;'),
])
patch('src/hle/os/os_init.cpp', [
    ('::Memory::Write32(r13 - 0x62ccu, 1);', '::Memory::Write32(r13 - 0x15c4u, 1);'),
    ('::Memory::Write32(r13 - 0x62c8u, 0x00535401);', '::Memory::Write32(r13 - 0x15c0u, 0x00535401);'),
    ('::Memory::Write32(r13 - 0x62c4u, 0x00535402);', '::Memory::Write32(r13 - 0x15bcu, 0x00535402);'),
])
patch('src/hle/storage/nand_isfs.cpp', [
    ('kIsfsFdSda1Offset = 29408u;', 'kIsfsFdSda1Offset = 17536u;'),
    ('kIsfsPathSda1Offset = 29400u;', 'kIsfsPathSda1Offset = 17528u;'),
    ('kIpcBufferLoSda1Offset = 25620u;', 'kIpcBufferLoSda1Offset = 4396u;'),
    ('kIpcBufferHiSda1Offset = 25616u;', 'kIpcBufferHiSda1Offset = 4392u;'),
    ('kIpcArenaLoSda1Offset = 25732u;', 'kIpcArenaLoSda1Offset = 6588u;'),
    ('kIpcArenaHiSda1Offset = 25728u;', 'kIpcArenaHiSda1Offset = 6584u;'),
    ('kIsfsHeapHandleSda1Offset = 25724u;', 'kIsfsHeapHandleSda1Offset = 6580u;'),
    ('kIsfsHeapBaseSda1Offset = 25740u;', 'kIsfsHeapBaseSda1Offset = 6596u;'),
    ('kIsfsHeapInitializedSda1Offset = 25744u;', 'kIsfsHeapInitializedSda1Offset = 6600u;'),
])
patch('src/hle/ios.cpp', [
    ('''    const int32_t result = NAND_IOS_Ioctl_HLE(fd, cmd, inBuf, inLen, outBuf, outLen);
    CompleteAsync(ctx, callback, result, callbackArg); // Success''', '''    const int32_t result = NAND_IOS_Ioctl_HLE(fd, cmd, inBuf, inLen, outBuf, outLen);
    {
        static int logged = 0;
        static uint32_t lastFd = ~0u, lastCmd = ~0u;
        if ((fd != lastFd || cmd != lastCmd) && logged < 40) {
            ++logged;
            lastFd = fd;
            lastCmd = cmd;
            std::fprintf(stderr, "[ios] IoctlAsync fd=%u cmd=%u in=%08X/%u out=%08X/%u -> %d\\n", fd, cmd, inBuf, inLen,
                         outBuf, outLen, result);
        }
    }
    CompleteAsync(ctx, callback, result, callbackArg); // Success'''),
])
patch('src/hle/storage/nand_api.cpp', [
    ('''        LogNandError("NANDOpen", "invalid params: path=%p fileInfo=0x%08X", path, fileInfoPtr);
        return NAND_RESULT_INVALID;
    }
''', '''        LogNandError("NANDOpen", "invalid params: path=%p fileInfo=0x%08X", path, fileInfoPtr);
        return NAND_RESULT_INVALID;
    }
    std::fprintf(stderr, "[nand] NANDOpen %s mode=%u\\n", path, mode);
'''),
])
patch('src/hle/ios.cpp', [
    ('''    const int32_t result = NAND_IOS_Ioctlv_HLE(fd, cmd, numIn, numOut, vectorPtr);
    CompleteAsync(ctx, callback, result, callbackArg);''', '''    const int32_t result = NAND_IOS_Ioctlv_HLE(fd, cmd, numIn, numOut, vectorPtr);
    {
        static int logged = 0;
        if (logged < 40) {
            ++logged;
            const uint32_t v0 = numIn + numOut ? Memory::Read32(vectorPtr) : 0;
            std::fprintf(stderr, "[ios] IoctlvAsync fd=%u cmd=%u in=%u out=%u v0=%08X '%s' cb=%08X -> %d\\n", fd, cmd,
                         numIn, numOut, v0, v0 ? (const char*)Memory::GetPointer(v0) : "", callback, result);
        }
    }
    CompleteAsync(ctx, callback, result, callbackArg);'''),
])
# 0x803D0828 is not NANDGetCurrentDir in this game: it is the ISFS_ReadDir completion callback of
# NANDChangeDirAsync (copies the new dir, then calls the user callback). Hooking it with the
# NANDGetCurrentDir HLE swallowed the callback, so the boot-time save check never completed.
patch('src/hle/storage/nand_api.cpp', [
    ('PPC_NATIVE_OVERRIDE(803D0828, NANDGetCurrentDir_HLE, int32_t, (uint32_t outPathPtr), (outPathPtr));',
     '// SATBK: 0x803D0828 is nandChangeDirCallback here, not NANDGetCurrentDir; leave it translated.'),
])
# Aurora retires idle cached textures only when the texture-cache revision advances, which happens
# on GXInvalidateTexAll. This game re-inits texture objects every frame (movie frames, menus)
# without calling it every frame, so the caches -- and GPU memory -- grew without bound (OOM after
# a minute or two). Invalidating once per displayed frame bounds them; on hardware it is only a
# texture-cache flush.
patch('src/hle/gx/gx_copy.cpp', [
    ('''    EnsureAuroraFrameActive();
    SatbkDevTestQuad();''', '''    EnsureAuroraFrameActive();
    SatbkDevTestQuad();
    GXInvalidateTexAll();'''),
])
# Dev: SATBK_TEXLOG lists each distinct large texture load and every copy destination.
patch('src/hle/gx/gx_texture.cpp', [
    ('''    if (!IsReasonableTextureDimensions(meta.width, meta.height)) {
        if (s_invalidDimLogCount++ < 128) {''', '''    {
        static const bool texlog = std::getenv("SATBK_TEXLOG") != nullptr;
        static std::set<uint64_t> seen;
        if (texlog && meta.width >= 256 &&
            seen.insert((uint64_t(meta.dataAddr) << 24) ^ (uint64_t(meta.format) << 20) ^ meta.width).second) {
            std::fprintf(stderr, "[texlog] load data=%08X %ux%u fmt=%u obj=%08X\\n", meta.dataAddr, meta.width,
                         meta.height, meta.format, oa);
        }
        static const uint32_t skipTex = std::getenv("SATBK_SKIPTEX") ? std::strtoul(std::getenv("SATBK_SKIPTEX"), nullptr, 16) : 0;
        if (skipTex != 0 && meta.dataAddr == skipTex) {
            BindUnloadableTexturePlaceholder(tid);
            return;
        }
        if (SatbkGxTraceOn()) {
            std::fprintf(stderr, "[gxtrace]   tex data=%08X %ux%u fmt=%u tlut=%08X\\n", meta.dataAddr, meta.width,
                         meta.height, meta.format, meta.tlut);
        }
    }
    if (!IsReasonableTextureDimensions(meta.width, meta.height)) {
        if (s_invalidDimLogCount++ < 128) {'''),
])
s = open('src/hle/gx/gx_texture.cpp').read().lstrip('\ufeff')
if '#include <set>' not in s:
    s = '#include <set>\n#include <cstdlib>\n#include <cstdio>\n' + s
open('src/hle/gx/gx_texture.cpp', 'w').write(s)
patch('src/hle/gx/gx_copy.cpp', [
    ('''extern "C" void GX__CopyTex_8016fd74(uint32_t da, uint32_t c) {
    EnsureAuroraFrameActive();''', '''extern "C" void GX__CopyTex_8016fd74(uint32_t da, uint32_t c) {
    EnsureAuroraFrameActive();
    {
        static const bool texlog = std::getenv("SATBK_TEXLOG") != nullptr;
        static std::set<uint32_t> seen;
        if (texlog && seen.insert(da).second) {
            std::fprintf(stderr, "[texlog] CopyTex dest=%08X src=%u,%u %ux%u\\n", da, g_texCopyState.srcLeft,
                         g_texCopyState.srcTop, g_texCopyState.srcWidth, g_texCopyState.srcHeight);
        }
    }'''),
    ('''    EnsureAuroraFrameActive();
    SatbkDevTestQuad();''', '''    EnsureAuroraFrameActive();
    {
        static const bool texlog = std::getenv("SATBK_TEXLOG") != nullptr;
        static std::set<uint32_t> seen;
        if (texlog && seen.insert(da).second) {
            std::fprintf(stderr, "[texlog] CopyDisp dest=%08X\\n", da);
        }
        if (SatbkGxTraceOn()) {
            std::fprintf(stderr, "[gxtrace] CopyDisp dest=%08X clear=%u\\n", da, c);
        }
    }
    SatbkDevTestQuad();'''),
])
s = open('src/hle/gx/gx_copy.cpp').read().lstrip('\ufeff')
if '#include <set>' not in s:
    s = '#include <set>\n#include <cstdlib>\n#include <cstdio>\nextern "C" bool SatbkGxTraceOn();\n' + s
open('src/hle/gx/gx_copy.cpp', 'w').write(s)
# GX resources are identified by host pointer. Texture loads use the canonical physical alias,
# but GXCopyTex destinations arrive as cached virtual addresses (0x8.../0x9...), which on Linux
# and Windows map to a different host alias -- so Aurora never matched a copy to the texture
# that samples it and sampled stale RAM instead (garbage on loading screens and transitions).
patch('src/hle/gx/gx_utils.cpp', [
    ('''#if defined(__APPLE__)
    // The macOS flat guest map exposes separate host aliases for cached,''', '''#if 1 // SatBK: canonicalize on every host (see tools/satbk_patches.py)
    // The macOS flat guest map exposes separate host aliases for cached,'''),
])
_trace_fn('src/hle/gx/gx_copy.cpp', 'extern "C" void GX__SetDispCopySrc_8016f438(', 'DispCopySrc %u %u %u %u', 'l, t, w, h')
_trace_fn('src/hle/gx/gx_copy.cpp', 'extern "C" void GX__SetDispCopyDst_8016f4b8(', 'DispCopyDst %u %u', 'w, h')
_trace_fn('src/hle/gx/gx_pixel.cpp', 'extern "C" void GX__SetPixelFmt_80172888(', 'PixelFmt %u %u', 'pf, zf')
_trace_fn('src/hle/gx/gx_dl.cpp', 'extern "C" void GX__CallDisplayList_80172f64(', 'CallDL %08X %u', 'listAddr, nbytes')
_trace_fn('src/hle/gx/gx_pixel.cpp', 'extern "C" void GX__SetFog_801722cc(', 'SetFog t=%u sz=%g ez=%g nz=%g fz=%g', 't, sz, ez, nz, fz')
_trace_fn('src/hle/gx/gx_transform.cpp', 'extern "C" void GX__SetViewportJitter_80173378(', 'ViewportJitter %g %g %g %g %g %g f=%u', 'l, t, w, h, nz, fz, f')
_trace_fn('src/hle/gx/gx_texture.cpp', 'extern "C" void GX__InitTlutObj_80170f80(', 'InitTlutObj oa=%08X da=%08X f=%u n=%u', 'oa, da, f, e')
_trace_fn('src/hle/gx/gx_texture.cpp', 'extern "C" void GX__LoadTlut_80170fa8(', 'LoadTlut oa=%08X slot=%u', 'oa, tl')
_trace_fn('src/hle/gx/gx_copy.cpp', 'extern "C" void GX__CopyTex_8016fd74(', 'CopyTex %08X clear=%u', 'da, c')

# Gate every SATBK_GXTRACE trace on a frame window (SATBK_GXTRACE=<first frame>,<frame count>).
import re as _re
for _root, _dirs, _files in os.walk('src/hle/gx'):
    for _f in _files:
        _p = os.path.join(_root, _f)
        _s = open(_p).read()
        _n = _re.sub(r'static const char\* (\w+) = std::getenv\("SATBK_GXTRACE"\);\s*static long (\w+) = \1 \? std::atol\(\1\)(?: \* \d)? : 0;\s*if \(\2 > 0\) \{\s*--\2;',
                     'if (SatbkGxTraceOn()) {', _s)
        if _n != _s:
            _n = _n.replace('#include <cstdlib>\n', '#include <cstdlib>\nextern "C" bool SatbkGxTraceOn();\n', 1)
            open(_p, 'w').write(_n)

# Dev: SATBK_DMAWATCH=<hex addr>,<hex len> logs every DVD DMA that lands in that guest range.
patch('src/hle/storage/dvd.cpp', [
    ('''static void CopyToGuestAsDma(uint32_t dest, const uint8_t* data, size_t size) {
''', '''extern "C" void SatbkTitlePromptArm();
static void CopyToGuestAsDma(uint32_t dest, const uint8_t* data, size_t size) {
'''),
    ('''static void CopyToGuestAsDma(uint32_t dest, const uint8_t* data, size_t size) {
''', '''static void CopyToGuestAsDma(uint32_t dest, const uint8_t* data, size_t size) {
    {
        static uint32_t wa = 0, wl = 0;
        static const bool parsed = [] { if (const char* e = std::getenv("SATBK_DMAWATCH")) { wa = std::strtoul(e, nullptr, 16); if (const char* c = std::strchr(e, ',')) wl = std::strtoul(c + 1, nullptr, 16); } return true; }();
        (void)parsed;
        const uint32_t d = dest | 0x80000000u;
        if (wl != 0 && d < wa + wl && d + size > wa) std::fprintf(stderr, "[dmawatch] dvd dest=%08X size=%zX\\n", dest, size);
    }
'''),
])

patch('src/hle/storage/dvd.cpp', [
    ('''    CopyToGuestAsDma(bufferPtr, tempBuf.data(), uLength);

    Memory::Write32(fileInfoPtr + DVD_CB_OFFSET_TRANSFERRED, uLength);''', '''    { static const bool dvdlog = std::getenv("SATBK_DVDLOG") != nullptr;
      if (dvdlog) std::fprintf(stderr, "[dvdlog] %s off=%X len=%X dest=%08X thread=%08X\\n", HostPathText(entry.hostPath).c_str(), uOffset, uLength, bufferPtr, Memory::Read32(0x800000E4u));
      // A fresh start of the title movie (not its loop restart) means the title prompt is up.
      static bool lastMovieWasTitle = false;
      if (entry.hostPath.extension() == ".sfd") {
          const bool isTitle = entry.hostPath.filename() == "titleloop.sfd";
          if (isTitle && uOffset == 0 && !lastMovieWasTitle) SatbkTitlePromptArm();
          lastMovieWasTitle = isTitle;
      } }
    CopyToGuestAsDma(bufferPtr, tempBuf.data(), uLength);

    Memory::Write32(fileInfoPtr + DVD_CB_OFFSET_TRANSFERRED, uLength);'''),
])



# Full-screen EFB copies are screen snapshots the game keeps drawing through scene transitions.
# The level loader reuses that (already released) RAM long before the transition ends, because
# disc reads are instant here; retiring the GPU copy on that CPU write made the loading screen
# and the menus after quitting a level sample the loader's data. Keep full-screen copies alive
# until the game copies into them again.
patch('src/hle/gx/gx_copy.cpp', [
    ('''            if (dirtyEnd <= copyStart || dirtyStart >= copyEnd) {
                ++it;
                continue;
            }
            retired.push_back(it->first);''', '''            if (dirtyEnd <= copyStart || dirtyStart >= copyEnd || it->second >= 512u * 448u * 2u) {
                ++it;
                continue;
            }
            retired.push_back(it->first);'''),
])

patch('src/hle/gx/gx_guest_write_hooks.cpp', [
    ('''void InstallAuroraHooks() {
''', '''void InstallAuroraHooks() {
    if (std::getenv("SATBK_NOWRITETRACK")) return; // dev: always re-digest texture sources
'''),
    ('#include <cstdio>\n', '#include <cstdio>\n#include <cstdlib>\n'),
])

# Dev: SATBK_DLLOG logs every display list the game records.
patch('src/hle/gx/gx_init.cpp', [
    ('#include "gx_internal.h"\n', '#include "gx_internal.h"\n#include <cstdlib>\n#include <cstdio>\n'),
    ('''extern "C" void GX__BeginDisplayList_80172e00(uint32_t la, uint32_t s) {
    try {''', '''extern "C" void GX__BeginDisplayList_80172e00(uint32_t la, uint32_t s) {
    { static const bool dllog = std::getenv("SATBK_DLLOG") != nullptr; static int n = 0;
      if (dllog && n++ < 200) std::fprintf(stderr, "[dllog] BeginDisplayList %08X size=%u lr=%08X\\n", la, s, TryGetCpuContext() ? TryGetCpuContext()->lr : 0u); }
    try {'''),
])

