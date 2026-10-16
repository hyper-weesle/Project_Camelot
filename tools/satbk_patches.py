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
