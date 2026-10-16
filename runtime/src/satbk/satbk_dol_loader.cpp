// Sonic and the Black Knight PC port: load the DOL image from the user's own extracted
// game data at startup.
//
// Upstream WiiCompiled embeds the DOL sections into the executable. This port deliberately
// does not: the executable carries no game data at all, and refuses to start unless the
// user has extracted their own disc (GameData/sys/main.dol) with satbk_extract.
#include <cryptopp/sha.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "memory.h"
#include "runtime_config.h"
#include "system_bridge.h"
#include "runtime_log.h"

namespace {

// SHA-256 of main.dol from Sonic and the Black Knight (USA, RENE8P, revision 0).
constexpr const char* kExpectedDolSha256 =
    "b49266a531c64305848f4feb67f927873d209f7e5bb81f593794ca5b96e65dee";

bool g_dataInitialized = false;

uint32_t Be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

std::string Sha256Hex(const std::vector<uint8_t>& data) {
    CryptoPP::SHA256 hash;
    std::array<uint8_t, CryptoPP::SHA256::DIGESTSIZE> digest{};
    hash.CalculateDigest(digest.data(), data.data(), data.size());
    std::ostringstream oss;
    for (uint8_t b : digest) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
    }
    return oss.str();
}

[[noreturn]] void FailGameData(const std::string& details) {
    std::fprintf(stderr, "[satbk] %s\n", details.c_str());
    RuntimeCrash::WriteCrashArtifacts("game_data", details);
    SetRuntimeExitCode(EXIT_FAILURE);
    ShowRuntimeFatalPopup("Game data not found", details);
    MarkFatalErrorReported();
    std::exit(EXIT_FAILURE);
}

} // namespace

namespace SatbkGameData {

// GameData/ next to the executable unless [paths] dvd_root says otherwise.
std::filesystem::path Root() {
    std::filesystem::path root = RuntimeConfigFile::ResolvedDvdRoot();
    if (root.empty()) {
        if (const auto exeDir = RuntimeConfigFile::ExecutableDirectory()) {
            root = *exeDir / "GameData";
        }
    }
    return root;
}

} // namespace SatbkGameData

extern "C" void InitializeDataSections() {
    if (g_dataInitialized) {
        return;
    }
    g_dataInitialized = true;

    const std::filesystem::path dolPath = SatbkGameData::Root() / "sys" / "main.dol";
    std::ifstream in(dolPath, std::ios::binary);
    if (!in) {
        FailGameData(
            "Could not open " + dolPath.string() +
            "\n\nThis port does not include any game data. Extract your own Sonic and the Black "
            "Knight (USA) disc image with satbk_extract (or Setup.bat) so that GameData\\sys\\main.dol "
            "and GameData\\files\\ exist next to the executable, or set [paths] dvd_root in Config.toml.");
    }
    std::vector<uint8_t> dol((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (dol.size() < 0x100) {
        FailGameData("main.dol is truncated: " + dolPath.string());
    }
    const std::string sha = Sha256Hex(dol);
    if (sha != kExpectedDolSha256) {
        FailGameData("main.dol does not match Sonic and the Black Knight (USA, RENE8P).\n\nFound SHA-256 " +
                     sha + "\nExpected " + kExpectedDolSha256 +
                     "\n\nOnly a clean, unmodified USA disc is supported.");
    }

    RT_LOG(RT_TAG_RUNTIME) << "Loading DOL sections from " << dolPath.string() << std::endl;
    for (int i = 0; i < 18; ++i) {
        const uint32_t offset = Be32(&dol[i * 4]);
        const uint32_t address = Be32(&dol[0x48 + i * 4]);
        const uint32_t size = Be32(&dol[0x90 + i * 4]);
        if (size == 0) {
            continue;
        }
        if (uint64_t(offset) + size > dol.size() || !Memory::Contains(address, size)) {
            FailGameData("main.dol section " + std::to_string(i) + " is out of range");
        }
        std::memcpy(Memory::GetPointer(address, size), dol.data() + offset, size);
    }
    const uint32_t bssAddress = Be32(&dol[0xD8]);
    const uint32_t bssSize = Be32(&dol[0xDC]);
    if (bssSize != 0 && Memory::Contains(bssAddress, bssSize)) {
        // .bss overlaps the small data sections on Wii DOLs; only zero the bytes no section owns.
        std::vector<std::pair<uint32_t, uint32_t>> owned;
        for (int i = 0; i < 18; ++i) {
            const uint32_t address = Be32(&dol[0x48 + i * 4]);
            const uint32_t size = Be32(&dol[0x90 + i * 4]);
            if (size != 0) owned.emplace_back(address, address + size);
        }
        for (uint32_t a = bssAddress; a < bssAddress + bssSize; ++a) {
            bool inSection = false;
            for (const auto& [lo, hi] : owned) {
                if (a >= lo && a < hi) { inSection = true; break; }
            }
            if (!inSection) *Memory::GetPointer(a, 1) = 0;
        }
    }
}
