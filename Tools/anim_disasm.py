"""Disassemble PlayAnimation ctor cluster + anim-name resolver sites."""
import struct
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

EXE = r"C:\Program Files (x86)\Steam\steamapps\common\Fable The Lost Chapters\Fable.exe"

pe = pefile.PE(EXE, fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase
data = pe.get_memory_mapped_image()
md = Cs(CS_ARCH_X86, CS_MODE_32)

text = next(s for s in pe.sections if s.Name.rstrip(b"\0") == b".text")
text_lo = base + text.VirtualAddress
text_hi = text_lo + text.Misc_VirtualSize


def find_all(needle: bytes):
    out, i = [], 0
    while True:
        i = data.find(needle, i)
        if i < 0:
            return out
        out.append(i)
        i += 1


def dword_refs(va: int):
    return find_all(struct.pack("<I", va))


def disasm_range(lo: int, hi: int, title: str):
    print(f"--- {title}: 0x{lo:X}..0x{hi:X} ---")
    code = bytes(data[lo - base : hi - base])
    for insn in md.disasm(code, lo):
        print(f"  0x{insn.address:X}: {insn.mnemonic} {insn.op_str}")
    print()


def func_start(va: int) -> int:
    rva = va - base
    for back in range(1, 0x3000):
        p = rva - back
        if data[p] == 0xCC and data[p + 1] != 0xCC:
            return base + p + 1
    return va - 0x200


import sys
mode = sys.argv[1] if len(sys.argv) > 1 else "ctors"

if mode == "ctors":
    # PlayAnimation vtable refs: 0x84254E, 0x8425C5, 0x84263F, 0x8426C8 (ctors?)
    # 0x843B05 (dtor?), 0x84C350
    disasm_range(0x842470, 0x842780, "PlayAnimation ctor cluster")
elif mode == "vt_from_index":
    # find PlayAnimationFromIndex + PlayIntoLoopOutOfAnimation vtables & ctor refs
    for cls in ["CCreatureAction_PlayAnimationFromIndex",
                "CCreatureAction_PlayIntoLoopOutOfAnimation",
                "CCreatureAction_PlayCombatAnimation"]:
        mangled = b".?AV" + cls.encode() + b"@@"
        for name_rva in find_all(mangled):
            td_va = base + name_rva - 8
            for col_rva in [r - 0x0C for r in dword_refs(td_va)]:
                if struct.unpack_from("<I", data, col_rva)[0] != 0:
                    continue
                col_va = base + col_rva
                for ref_rva in dword_refs(col_va):
                    vt_va = base + ref_rva + 4
                    funcs = []
                    r = ref_rva + 4
                    while True:
                        f = struct.unpack_from("<I", data, r)[0]
                        if not (text_lo <= f < text_hi):
                            break
                        funcs.append(f)
                        r += 4
                    coderefs = [base + r2 for r2 in dword_refs(vt_va)
                                if text_lo <= base + r2 < text_hi]
                    print(f"[{cls}] vtable 0x{vt_va:X} {len(funcs)} vfuncs; "
                          f".text refs: " + ", ".join(f"0x{x:X}" for x in coderefs[:12]))
                    print("   vfuncs[0..6]: " + ", ".join(f"0x{f:X}" for f in funcs[:7]))
elif mode == "resolver":
    # spell code refs anim strings @0x79F638/48/5D — disasm around to find
    # name->index/handle resolver call
    disasm_range(0x79F600, 0x79F780, "lightning aim anim usage")
elif mode == "range":
    lo = int(sys.argv[2], 16); hi = int(sys.argv[3], 16)
    disasm_range(lo, hi, "range")
elif mode == "fstart":
    va = int(sys.argv[2], 16)
    s = func_start(va)
    print(f"func containing 0x{va:X} starts at 0x{s:X}")
elif mode == "callers":
    # find all direct 'call rel32' to a target VA in .text
    tgt = int(sys.argv[2], 16)
    hits = []
    tdata = bytes(data[text_lo - base : text_hi - base])
    i = 0
    while True:
        i = tdata.find(b"\xe8", i)
        if i < 0:
            break
        src = text_lo + i
        rel = struct.unpack_from("<i", tdata, i + 1)[0]
        if src + 5 + rel == tgt:
            hits.append(src)
        i += 1
    print(f"callers of 0x{tgt:X}: {len(hits)}")
    for h in hits[:40]:
        print(f"  0x{h:X}")
