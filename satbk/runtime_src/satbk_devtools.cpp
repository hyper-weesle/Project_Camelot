// Developer diagnostics for the Sonic and the Black Knight port. Inert unless enabled with
// environment variables, so release behaviour is unchanged:
//   SATBK_SNAPSHOT_AT=<seconds>[,<seconds>...]  write MEM1/MEM2 snapshots (snapshot_<t>.bin[.mem2])
//                                               into the working directory at those times.
#include "system_bridge.h"

#include <atomic>
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
    return first >= 0 && f >= first && f < first + count;
}

extern "C" void SatbkDevTestQuad() {
    if (SatbkGxTraceOn()) {
        std::fprintf(stderr, "[gxtrace] ==== CopyDisp frame %ld ====\n", g_copyFrames.load());
    }
    g_copyFrames.fetch_add(1, std::memory_order_relaxed);
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
