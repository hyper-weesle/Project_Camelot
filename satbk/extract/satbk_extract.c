/*
 * satbk_extract - extracts the game data from the user's own Sonic and the Black Knight
 * disc image (ISO/WBFS/RVZ/WIA/CISO/GCZ...) into a GameData folder that the PC port reads.
 *
 * Nothing from the game is shipped with the port: this tool is the only way the port gets
 * its assets, and it only reads a disc image that you supply.
 *
 * Usage: satbk_extract <disc image> <output dir> [--time-limit seconds]
 *
 * Resumable: files already present with the correct size are skipped (an interrupted file has
 * the wrong size and is written again). Exit code 0 = complete, 2 = stopped by time limit
 * (run again to continue), 1 = error.
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "nod.h"

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#define MKDIR(p) mkdir(p, 0755)
#endif

static const char kExpectedGameId[6] = {'R', 'E', 'N', 'E', '8', 'P'};

static void mkdirs(const char *path) {
    char tmp[4096];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; ++p) {
        if (*p == '/' || *p == '\\') {
            char c = *p;
            *p = 0;
            MKDIR(tmp);
            *p = c;
        }
    }
    MKDIR(tmp);
}

static long long file_size(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    return (long long)st.st_size;
}

static int write_blob(const char *dir, const char *name, const uint8_t *data, size_t size) {
    char path[4096];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    if (file_size(path) == (long long)size) return 0;
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "cannot write %s: %s\n", path, strerror(errno)); return -1; }
    if (size && fwrite(data, 1, size, f) != size) { fclose(f); return -1; }
    fclose(f);
    return 0;
}

static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <disc image> <output dir> [--time-limit seconds]\n", argv[0]);
        return 1;
    }
    const char *image = argv[1];
    const char *out = argv[2];
    double time_limit = 0;
    for (int i = 3; i + 1 < argc; ++i)
        if (!strcmp(argv[i], "--time-limit")) time_limit = atof(argv[i + 1]);
    time_t start = time(NULL);

    NodHandle *disc = NULL, *part = NULL;
    if (nod_disc_open(image, NULL, &disc) != NOD_RESULT_OK) {
        fprintf(stderr, "failed to open disc image: %s\n", nod_error_message());
        return 1;
    }
    NodDiscHeader hdr;
    nod_disc_header(disc, &hdr);
    if (memcmp(hdr.game_id, kExpectedGameId, 6) != 0) {
        fprintf(stderr, "this is not Sonic and the Black Knight (USA, RENE8P); game id %.6s\n", hdr.game_id);
        return 1;
    }
    if (nod_disc_open_partition_kind(disc, 0, NULL, &part) != NOD_RESULT_OK) {
        fprintf(stderr, "failed to open data partition: %s\n", nod_error_message());
        return 1;
    }
    NodPartitionMeta meta;
    if (nod_partition_meta(part, &meta) != NOD_RESULT_OK) {
        fprintf(stderr, "failed to read partition metadata: %s\n", nod_error_message());
        return 1;
    }

    char sysdir[4096], filesdir[4096];
    snprintf(sysdir, sizeof sysdir, "%s/sys", out);
    snprintf(filesdir, sizeof filesdir, "%s/files", out);
    mkdirs(sysdir);
    mkdirs(filesdir);
    if (write_blob(sysdir, "boot.bin", meta.raw_boot.data, meta.raw_boot.size) ||
        write_blob(sysdir, "bi2.bin", meta.raw_bi2.data, meta.raw_bi2.size) ||
        write_blob(sysdir, "apploader.img", meta.raw_apploader.data, meta.raw_apploader.size) ||
        write_blob(sysdir, "main.dol", meta.raw_dol.data, meta.raw_dol.size) ||
        write_blob(sysdir, "fst.bin", meta.raw_fst.data, meta.raw_fst.size))
        return 1;

    const uint8_t *fst = meta.raw_fst.data;
    uint32_t count = be32(fst + 8);
    const char *names = (const char *)fst + count * 12;

    /* directory stack: path + end index */
    char paths[64][4096];
    uint32_t ends[64];
    int depth = 0;
    snprintf(paths[0], sizeof paths[0], "%s", filesdir);
    ends[0] = count;

    static uint8_t buf[1 << 20];
    uint32_t done = 0, total = 0;
    for (uint32_t i = 1; i < count; ++i) total += !(fst[i * 12]);

    for (uint32_t i = 1; i < count; ++i) {
        const uint8_t *e = fst + i * 12;
        while (depth > 0 && i >= ends[depth]) --depth;
        const char *name = names + (be32(e) & 0x00FFFFFF);
        if (e[0]) {
            ++depth;
            snprintf(paths[depth], sizeof paths[depth], "%s/%s", paths[depth - 1], name);
            ends[depth] = be32(e + 8);
            mkdirs(paths[depth]);
            continue;
        }
        uint32_t size = be32(e + 8);
        char path[4096];
        snprintf(path, sizeof path, "%s/%s", paths[depth], name);
        ++done;
        if (file_size(path) == (long long)size) continue;
        if (time_limit > 0 && difftime(time(NULL), start) > time_limit) {
            printf("time limit reached at %u/%u files; run again to continue\n", done - 1, total);
            return 2;
        }
        NodHandle *fh = NULL;
        if (nod_partition_open_file(part, i, &fh) != NOD_RESULT_OK) {
            fprintf(stderr, "failed to open %s: %s\n", path, nod_error_message());
            return 1;
        }
        FILE *f = fopen(path, "wb");
        if (!f) { fprintf(stderr, "cannot write %s\n", path); return 1; }
        uint64_t left = size;
        while (left) {
            int64_t n = nod_read(fh, buf, left < sizeof buf ? (size_t)left : sizeof buf);
            if (n <= 0) { fprintf(stderr, "read error in %s\n", path); fclose(f); return 1; }
            fwrite(buf, 1, (size_t)n, f);
            left -= (uint64_t)n;
        }
        fclose(f);
        nod_free(fh);
        printf("[%u/%u] %s\n", done, total, path + strlen(out) + 1);
        fflush(stdout);
    }
    nod_free(part);
    nod_free(disc);
    printf("extraction complete: %u files\n", total);
    return 0;
}
