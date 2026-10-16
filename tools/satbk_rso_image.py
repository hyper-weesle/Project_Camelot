#!/usr/bin/env python3
"""Build the linked image of an RSO module (e.g. hbm_part.rso) exactly as the game's own
RSOLinkList produces it in memory, so the translator can statically recompile the module's
code at the address the game links it to.

  satbk_rso_image.py <module.rso> <static.sel> <module_base> <bss_base> <stub_base> <out.bin> <stubs.bin> [<starts.txt>]

The static module's exports are reached through 24-byte far-jump stubs that
RSOStaticLocateObject writes (in export order) at <stub_base>, because a bl from MEM2
cannot reach MEM1 directly.

The output is written from the user's own extracted game data at build time; nothing from
the game is stored in this repository.
"""
import struct
import sys

R_PPC_ADDR32, R_PPC_ADDR24, R_PPC_ADDR16, R_PPC_ADDR16_LO, R_PPC_ADDR16_HI, R_PPC_ADDR16_HA = 1, 2, 3, 4, 5, 6
R_PPC_ADDR14, R_PPC_REL24, R_PPC_REL14 = 7, 10, 11

# Section bases of the static module (the DOL), by ELF section index as the .sel uses them.
STATIC_SECTION_BASE = {1: 0x80004000, 2: 0x80026C00, 0xFFF1: 0}

HEADER_FIELDS = ['next', 'prev', 'numSec', 'secInfoOff', 'nameOff', 'nameSize', 'version', 'bssSize',
                 'prologSec', 'epilogSec', 'unresSec', 'bssSec', 'prolog', 'epilog', 'unresolved',
                 'intRelOff', 'intRelSize', 'extRelOff', 'extRelSize', 'expOff', 'expSize', 'expNameOff',
                 'impOff', 'impSize', 'impNameOff']


def header(d):
    return dict(zip(HEADER_FIELDS, struct.unpack('>8I4B13I', d[:0x58])))


def cstr(d, off):
    return d[off:d.index(b'\0', off)].decode('latin1')


def static_exports(sel):
    """[(name, address)] in export-table order."""
    h = header(sel)
    out = []
    for i in range(h['expSize'] // 16):
        name_off, value, sec, _ = struct.unpack('>IIII', sel[h['expOff'] + i * 16:h['expOff'] + i * 16 + 16])
        out.append((cstr(sel, h['expNameOff'] + name_off), STATIC_SECTION_BASE[sec] + value))
    return out


def far_stubs(exports, stub_base):
    out = bytearray()
    for _, value in exports:
        lo = value & 0xFFFF
        hi = ((value >> 16) + (1 if lo & 0x8000 else 0)) & 0xFFFF
        out += struct.pack('>6I', 0x91810004, 0x3D800000 | hi, 0x398C0000 | lo, 0x7D8903A6, 0x81810004, 0x4E800420)
    return bytes(out)


def link(rso, sel, base, bss, stub_base):
    h = header(rso)
    img = bytearray(rso)
    sections = []
    for i in range(h['numSec']):
        off, size = struct.unpack('>II', rso[h['secInfoOff'] + i * 8:h['secInfoOff'] + i * 8 + 8])
        if i == h['bssSec'] or (off == 0 and size != 0):
            addr = bss
        elif off == 0:
            addr = 0
        else:
            addr = base + (off & ~1)
        sections.append((addr, size, off & 1))
    static = static_exports(sel)
    exports = {n: v for n, v in static}
    stub_of = {n: stub_base + k * 0x18 for k, (n, _) in enumerate(static)}
    imports = []
    for i in range(h['impSize'] // 12):
        name_off, _, _ = struct.unpack('>III', rso[h['impOff'] + i * 12:h['impOff'] + i * 12 + 12])
        name = cstr(rso, h['impNameOff'] + name_off)
        if name not in exports:
            raise SystemExit(f'unresolved import {name}')
        imports.append((exports[name], stub_of[name]))

    def apply(where_off, rtype, value):
        where = base + where_off
        if rtype == R_PPC_ADDR32:
            struct.pack_into('>I', img, where_off, value & 0xFFFFFFFF)
        elif rtype == R_PPC_ADDR16_LO:
            struct.pack_into('>H', img, where_off, value & 0xFFFF)
        elif rtype == R_PPC_ADDR16_HI:
            struct.pack_into('>H', img, where_off, (value >> 16) & 0xFFFF)
        elif rtype == R_PPC_ADDR16_HA:
            struct.pack_into('>H', img, where_off, ((value + 0x8000) >> 16) & 0xFFFF)
        elif rtype == R_PPC_ADDR16:
            struct.pack_into('>H', img, where_off, value & 0xFFFF)
        elif rtype == R_PPC_REL24:
            ins = struct.unpack_from('>I', img, where_off)[0]
            disp = (value - where) & 0x03FFFFFC
            struct.pack_into('>I', img, where_off, (ins & 0xFC000003) | disp)
        elif rtype == R_PPC_REL14:
            ins = struct.unpack_from('>I', img, where_off)[0]
            disp = (value - where) & 0xFFFC
            struct.pack_into('>I', img, where_off, (ins & 0xFFFF0003) | disp)
        elif rtype == 0:
            pass
        else:
            raise SystemExit(f'unsupported RSO relocation type {rtype}')

    for i in range(h['intRelSize'] // 12):
        off, info, addend = struct.unpack('>III', rso[h['intRelOff'] + i * 12:h['intRelOff'] + i * 12 + 12])
        apply(off, info & 0xFF, sections[info >> 8][0] + addend)
    for i in range(h['extRelSize'] // 12):
        off, info, addend = struct.unpack('>III', rso[h['extRelOff'] + i * 12:h['extRelOff'] + i * 12 + 12])
        value, stub = imports[info >> 8]
        apply(off, info & 0xFF, (stub if (info & 0xFF) in (R_PPC_REL24, R_PPC_REL14) else value) + addend)

    # The loader rewrites the header's section table and pointers in place too; the translator
    # only needs code and data, so leave the header as-is (it is never executed).
    module_exports = {}
    for i in range(h['expSize'] // 16):
        name_off, value, sec, _ = struct.unpack('>IIII', rso[h['expOff'] + i * 16:h['expOff'] + i * 16 + 16])
        module_exports[cstr(rso, h['expNameOff'] + name_off)] = sections[sec][0] + value
    # RSO section tables carry no exec flag; code lives in the prolog's section (.text).
    text = [(sections[h['prologSec']][0], sections[h['prologSec']][1])]
    prolog = sections[h['prologSec']][0] + h['prolog']
    epilog = sections[h['epilogSec']][0] + h['epilog']
    unresolved = sections[h['unresSec']][0] + h['unresolved']
    # Code addresses materialised through relocations (lis/addi pairs, pointer tables).
    code_refs = []
    for i in range(h['intRelSize'] // 12):
        off, info, addend = struct.unpack('>III', rso[h['intRelOff'] + i * 12:h['intRelOff'] + i * 12 + 12])
        if (info >> 8) == h['prologSec'] and (info & 0xFF) in (R_PPC_ADDR16_LO, R_PPC_ADDR16_HA, R_PPC_ADDR32):
            code_refs.append((base + off, info & 0xFF, sections[info >> 8][0] + addend))
    link.code_refs = code_refs
    return bytes(img), sections, module_exports, text, (prolog, epilog, unresolved), far_stubs(static, stub_base)


def function_starts(img, base, text, exports, specials):
    starts = set(specials)
    in_text = lambda a: any(t <= a < t + s for t, s in text)
    for name, addr in exports.items():
        if in_text(addr):
            starts.add(addr)
    for t, s in text:
        prev = 0
        for a in range(t, t + s, 4):
            ins = struct.unpack_from('>I', img, a - base)[0]
            if (ins >> 26) == 18 and (ins & 3) == 1:
                disp = ins & 0x03FFFFFC
                if disp & 0x02000000:
                    disp -= 0x04000000
                tgt = (a + disp) & 0xFFFFFFFF
                if in_text(tgt):
                    starts.add(tgt)
            if (ins >> 16) == 0x9421 and prev in (0x4E800020, 0) or ((prev >> 26) == 18 and (prev & 3) == 0 and (ins >> 16) == 0x9421):
                starts.add(a)
            prev = ins
    # Jump tables (lis/addi base + lwzx + mtctr + bctr) are code addresses in data that are
    # *not* functions; find them so they can be excluded below.
    def w(a):
        return struct.unpack_from('>I', img, a - base)[0]

    def simm(i):
        v = i & 0xFFFF
        return v - 0x10000 if v & 0x8000 else v

    jump_table_words = set()
    for t, s in text:
        for x in range(t + 40, t + s, 4):
            if w(x) != 0x4E800420:
                continue
            table = None
            for y in range(x - 4, max(t, x - 80), -4):
                i = w(y)
                if (i >> 26) == 31 and ((i >> 1) & 0x3FF) == 23:  # lwzx
                    ra, rb = (i >> 16) & 31, (i >> 11) & 31
                    for z in range(y - 4, max(t, y - 120), -4):
                        j = w(z)
                        if (j >> 26) == 14 and ((j >> 21) & 31) in (ra, rb):
                            for q in range(z - 4, max(t, z - 120), -4):
                                k = w(q)
                                if (k >> 26) == 15 and ((k >> 21) & 31) == ((j >> 16) & 31):
                                    table = (((k & 0xFFFF) << 16) + simm(j)) & 0xFFFFFFFF
                                    break
                            break
                    break
            if table is None:
                continue
            p = table
            while base <= p < base + len(img) - 3 and in_text(w(p)) and abs(w(p) - x) < 0x10000:
                jump_table_words.add(p)
                p += 4
    for where, rtype, target in getattr(link, 'code_refs', []):
        if rtype == R_PPC_ADDR32 and where in jump_table_words:
            continue
        if in_text(target) and target & 3 == 0:
            starts.add(target)
    # Function pointers in the module's data (vtables, callbacks, function tables).
    for off in range(0, len(img) - 3, 4):
        v = struct.unpack_from('>I', img, off)[0]
        a = base + off
        if v & 3 or not in_text(v) or in_text(a) or a in jump_table_words:
            continue
        starts.add(v)
    return sorted(starts)


def main():
    rso = open(sys.argv[1], 'rb').read()
    sel = open(sys.argv[2], 'rb').read()
    base, bss, stub_base = int(sys.argv[3], 16), int(sys.argv[4], 16), int(sys.argv[5], 16)
    img, sections, exports, text, specials, stubs = link(rso, sel, base, bss, stub_base)
    open(sys.argv[6], 'wb').write(img)
    open(sys.argv[7], 'wb').write(stubs)
    if len(sys.argv) > 8:
        starts = function_starts(img, base, text, exports, specials)
        starts += [stub_base + k for k in range(0, len(stubs), 0x18)]
        names = {v: k for k, v in exports.items()}
        with open(sys.argv[8], 'w') as f:
            for a in starts:
                n = names.get(a)
                f.write('%08x %s\n' % (a, ('rso_%s_%08x' % (''.join(c if c.isalnum() else '_' for c in n), a)) if n else '0x%08x' % a))
    print(f'linked {len(img)} bytes @ {base:#010x}, bss @ {bss:#010x}; prolog {specials[0]:#010x}')


if __name__ == '__main__':
    main()
