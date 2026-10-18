// Developer diagnostics for the Sonic and the Black Knight port. Inert unless enabled with
// environment variables, so release behaviour is unchanged:
//   SATBK_SNAPSHOT_AT=<seconds>[,<seconds>...]  write MEM1/MEM2 snapshots (snapshot_<t>.bin[.mem2])
//                                               into the working directory at those times.
#include "system_bridge.h"
#include "memory.h"

#include <atomic>
#include <unistd.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

struct SatbkDevtools {
    SatbkDevtools() {
        const char* spec = std::getenv("SATBK_SNAPSHOT_AT");
        if (spec == nullptr || *spec == 0) {
            return;
        }
        std::vector<int> times;
        std::stringstream ss(spec);
        std::string item;
        while (std::getline(ss, item, ',')) {
            times.push_back(std::atoi(item.c_str()));
        }
        std::thread([times]() {
            const auto start = std::chrono::steady_clock::now();
            for (int t : times) {
                std::this_thread::sleep_until(start + std::chrono::seconds(t));
                const std::filesystem::path path = "snapshot_" + std::to_string(t) + ".bin";
                SystemBridge::WriteGuestMemorySnapshot(std::cerr, path);
                std::cerr << "[satbk] wrote " << path.string() << std::endl;
            }
        }).detach();
    }
};

SatbkDevtools g_devtools;

} // namespace

// SATBK_TESTQUAD=1: draw a solid red quad over the left half of the EFB right before every
// GXCopyDisp, using Aurora's GX API directly. Distinguishes "Aurora renders nothing" from
// "the game's draws are wrong".
#include <dolphin/gx.h>
#include <aurora/gfx.h>

// SATBK_GXTRACE=<first>,<count>: trace GX calls during display-copy frames [first, first+count).
static std::atomic<long> g_copyFrames{0};
extern "C" bool SatbkGxTraceOn() {
    static long first = -1, count = 0;
    static const bool parsed = [] {
        if (const char* spec = std::getenv("SATBK_GXTRACE")) {
            first = std::atol(spec);
            const char* comma = std::strchr(spec, ',');
            count = comma ? std::atol(comma + 1) : 1;
        }
        return true;
    }();
    (void)parsed;
    const long f = g_copyFrames.load(std::memory_order_relaxed);
    // SATBK_GXTRACE_T=<seconds>,<count>: start at the first display copy after that many seconds.
    static long timedFirst = -1, timedCount = 0;
    static double timedStart = -1;
    static const bool timedParsed = [] {
        if (const char* spec = std::getenv("SATBK_GXTRACE_T")) {
            timedStart = std::atof(spec);
            const char* comma = std::strchr(spec, ',');
            timedCount = comma ? std::atol(comma + 1) : 1;
        }
        return true;
    }();
    (void)timedParsed;
    if (timedStart >= 0) {
        static const auto t0 = std::chrono::steady_clock::now();
        if (timedFirst < 0 && std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() >= timedStart) {
            timedFirst = f;
        }
        if (timedFirst >= 0 && f >= timedFirst && f < timedFirst + timedCount) {
            return true;
        }
    }
    return first >= 0 && f >= first && f < first + count;
}

extern "C" void aurora_texture_stats(uint64_t* count, uint64_t* bytes);
extern "C" void aurora_texture_cache_stats(uint64_t* out);
extern "C" void aurora_satbk_counts(uint64_t* out, int n);

extern "C" void SatbkDevTestQuad() {
    if (const char* spec = std::getenv("SATBK_MEMDUMP")) { // <seconds>,<hex addr>,<hex len>
        static const auto t0 = std::chrono::steady_clock::now();
        static bool dumped = false;
        const double at = std::atof(spec);
        if (!dumped && std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() >= at) {
            dumped = true;
            const char* a = std::strchr(spec, ','); const char* l = a ? std::strchr(a + 1, ',') : nullptr;
            if (a && l) {
                const uint32_t addr = std::strtoul(a + 1, nullptr, 16), len = std::strtoul(l + 1, nullptr, 16);
                if (FILE* f = std::fopen("/tmp/claude-0/memdump.bin", "wb")) {
                    for (uint32_t i = 0; i < len; ++i) std::fputc(Memory::Read8(addr + i), f);
                    std::fclose(f);
                }
                std::fprintf(stderr, "[satbk] memdump %08X+%X\n", addr, len);
            }
        }
    }
    if (SatbkGxTraceOn()) {
        std::fprintf(stderr, "[gxtrace] ==== CopyDisp frame %ld ====\n", g_copyFrames.load());
    }
    const long frame = g_copyFrames.fetch_add(1, std::memory_order_relaxed);
    static const bool texStats = std::getenv("SATBK_TEXSTATS") != nullptr;
    if (texStats && frame % 10 == 0) {
        uint64_t count = 0, bytes = 0;
        aurora_texture_stats(&count, &bytes);
        const AuroraStats* st = aurora_get_stats();
        std::fprintf(stderr,
                     "[satbk] frame %ld: %llu live textures, ~%llu MB; pipelines queued %u created %u; draws %u; "
                     "vert %u uni %u idx %u stor %u texup %u\n",
                     frame, static_cast<unsigned long long>(count), static_cast<unsigned long long>(bytes >> 20),
                     st->queuedPipelines, st->createdPipelines, st->drawCallCount, st->lastVertSize,
                     st->lastUniformSize, st->lastIndexSize, st->lastStorageSize, st->lastTextureUploadSize);
        uint64_t sites[13] = {};
        aurora_satbk_counts(sites, 13);
        std::fprintf(stderr, "[satbk]   gx creates tlut %llu static %llu palette %llu\n", (unsigned long long)sites[10], (unsigned long long)sites[11], (unsigned long long)sites[12]);
        std::fprintf(stderr, "[satbk]   creates efb %llu/%llu snap %llu rt %llu buf %llu tex %llu/%llu/%llu depth %llu cap %llu\n",
                     (unsigned long long)sites[0], (unsigned long long)sites[1], (unsigned long long)sites[2],
                     (unsigned long long)sites[3], (unsigned long long)sites[4], (unsigned long long)sites[5],
                     (unsigned long long)sites[6], (unsigned long long)sites[7], (unsigned long long)sites[8],
                     (unsigned long long)sites[9]);
        uint64_t c[5] = {};
        aurora_texture_cache_stats(c);
        std::fprintf(stderr, "[satbk]   texobj %llu source %llu keys %llu rev %llu copies %llu\n", (unsigned long long)c[0],
                     (unsigned long long)c[1], (unsigned long long)c[2], (unsigned long long)c[3], (unsigned long long)c[4]);
    }
    static const bool enabled = std::getenv("SATBK_TESTQUAD") != nullptr;
    if (!enabled) {
        return;
    }
    // Ortho t=0 b=480 l=0 r=640 n=0 f=1 in GX convention.
    f32 proj[4][4] = {{2.f / 640.f, 0.f, 0.f, -1.f}, {0.f, -2.f / 480.f, 0.f, 1.f}, {0.f, 0.f, -1.f, -1.f}, {0.f, 0.f, 0.f, 1.f}};
    GXSetProjection(proj, GX_ORTHOGRAPHIC);
    f32 ident[3][4] = {{1.f, 0.f, 0.f, 0.f}, {0.f, 1.f, 0.f, 0.f}, {0.f, 0.f, 1.f, 0.f}};
    GXLoadPosMtxImm(ident, GX_PNMTX0);
    GXSetCurrentMtx(GX_PNMTX0);
    GXSetViewport(0.f, 0.f, 640.f, 480.f, 0.f, 1.f);
    GXSetScissor(0, 0, 640, 480);
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT7, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT7, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GXSetNumChans(1);
    GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
    GXSetNumTexGens(0);
    GXSetNumTevStages(1);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    static const bool alpha0 = std::getenv("SATBK_TESTALPHA0") != nullptr;
    static const bool alpha0tex = std::getenv("SATBK_TESTALPHA0TEX") != nullptr;
    if (alpha0tex) {
        // A textured quad set up exactly like the game's loading-screen quad.
        static std::vector<uint8_t> texels(64 * 64 * 4, 0xFF);
        static GXTexObj obj;
        GXInitTexObj(&obj, texels.data(), 64, 64, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
        GXLoadTexObj(&obj, GX_TEXMAP0);
        GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
        GXSetVtxAttrFmt(GX_VTXFMT7, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
        GXSetNumTexGens(1);
        GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY, GX_FALSE, GX_PTIDENTITY);
        GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
        GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_TEXC, GX_CC_RASC, GX_CC_ZERO);
        GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
        GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_NOOP);
        GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
        GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
        GXSetColorUpdate(GX_TRUE);
        GXSetCullMode(GX_CULL_NONE);
        GXBegin(GX_QUADS, GX_VTXFMT7, 4);
        GXPosition3f32(0.f, 0.f, 0.f);   GXColor4u8(255, 0, 0, 255); GXTexCoord2f32(0, 0);
        GXPosition3f32(320.f, 0.f, 0.f); GXColor4u8(255, 0, 0, 255); GXTexCoord2f32(1, 0);
        GXPosition3f32(320.f, 480.f, 0.f); GXColor4u8(255, 0, 0, 255); GXTexCoord2f32(1, 1);
        GXPosition3f32(0.f, 480.f, 0.f); GXColor4u8(255, 0, 0, 255); GXTexCoord2f32(0, 1);
        GXEnd();
        return;
    }
    if (alpha0) {
        // Same TEV/blend setup as the game's loading-screen background quad: alpha must come out 0,
        // so with SRCALPHA blending this quad has to be invisible.
        GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_RASC);
        GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
        GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    } else
    GXSetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
    GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GXSetColorUpdate(GX_TRUE);
    GXSetCullMode(GX_CULL_NONE);
    GXBegin(GX_QUADS, GX_VTXFMT7, 4);
    GXPosition3f32(0.f, 0.f, 0.f);   GXColor4u8(255, 0, 0, 255);
    GXPosition3f32(320.f, 0.f, 0.f); GXColor4u8(255, 0, 0, 255);
    GXPosition3f32(320.f, 480.f, 0.f); GXColor4u8(255, 0, 0, 255);
    GXPosition3f32(0.f, 480.f, 0.f); GXColor4u8(255, 0, 0, 255);
    GXEnd();
}


// Dev: SATBK_STAGE=stgNNN makes the first story mission (stg001) load stage NNN instead, for
// checking that every stage loads. The selected stage is kept in the game's globals by name
// ("stg001", plus its world "stg00" for enemy and effect sets); those copies are rewritten while
// the stage tables are read, so the game then loads the target stage's own data.

extern "C" void SatbkRamScan();
extern "C" void SatbkEditDvdRead(const char* name, uint32_t offset, void* data, uint32_t length) {
    if (offset == 0 && std::strcmp(name, "stg001.txd") == 0) SatbkRamScan();
    static const char* target = std::getenv("SATBK_STAGE");
    if (std::getenv("SATBK_STGDUMP") && offset == 0 && std::strcmp(name, "actstgmission.txt") == 0) {
        for (uint32_t a = 0x8072CC00u; a < 0x8072D000u; a += 16)
            std::fprintf(stderr, "[stgdump] %08X %08X %08X %08X %08X\n", a, Memory::Read32(a), Memory::Read32(a + 4), Memory::Read32(a + 8), Memory::Read32(a + 12));
    }
    if (target && offset == 0 && std::strlen(target) == 6 &&
        (std::strcmp(name, "actstgdesign.txt") == 0 || std::strcmp(name, "actstgdata.txt") == 0 ||
         std::strcmp(name, "actstgmission.txt") == 0 || std::strcmp(name, "hint_stg_eng.one") == 0)) {
        // "stg001" (stage) and "stg00" (its world, used for enemy and effect sets) in the stage globals.
        for (uint32_t a = 0x8072C000u; a < 0x80730000u; ++a) {
            const uint8_t c0 = Memory::Read8(a);
            if ((c0 != 's' && c0 != 'S') || Memory::Read8(a + 1) != 't' || Memory::Read8(a + 2) != 'g' ||
                Memory::Read8(a + 3) != '0' || Memory::Read8(a + 4) != '0') continue;
            int len = 0;
            if (Memory::Read8(a + 5) == 0) len = 5;
            else if (Memory::Read8(a + 5) == '1' && Memory::Read8(a + 6) == 0) len = 6;
            if (len == 0 || (a > 0x8072C000u && Memory::Read8(a - 1) != 0)) continue;
            for (int i = 1; i < len; ++i) Memory::Write8(a + i, static_cast<uint8_t>(target[i]));
            std::fprintf(stderr, "[satbk] SATBK_STAGE: %s: %08X -> %.*s\n", name, a, len, target);
        }
    }
}

// Dev: SATBK_RAMSCAN=<text> lists guest addresses holding <text> when stg001.txd starts loading.
extern "C" void SatbkRamScan() {
    static const char* needle = std::getenv("SATBK_RAMSCAN");
    if (needle == nullptr) return;
    const size_t n = std::strlen(needle);
    for (uint32_t base : {0x80000000u, 0x90000000u}) {
        const uint32_t size = base == 0x80000000u ? 0x01800000u : 0x04000000u;
        for (uint32_t a = base; a + n < base + size; ++a) {
            if (Memory::Read8(a) != static_cast<uint8_t>(needle[0])) continue;
            size_t k = 1;
            while (k < n && Memory::Read8(a + k) == static_cast<uint8_t>(needle[k])) ++k;
            if (k == n) {
                char ctx[48] = {};
                for (int i = 0; i < 40; ++i) { const uint8_t c = Memory::Read8(a + i); ctx[i] = c >= 32 && c < 127 ? c : '.'; }
                std::fprintf(stderr, "[ramscan] %08X %s\n", a, ctx);
            }
        }
    }
}
