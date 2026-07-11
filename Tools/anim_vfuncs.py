"""Dump all vfuncs of PlayAnimation-family vtables; disasm each, collect call
targets, find common callees (the anim-start funnel)."""
import struct
import sys
from collections import Counter
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

VTABLES = {
    "PlayAnimation": 0x1273A0C,
    "PlayAnimationFromIndex": 0x127507C,
}


def vfuncs(vt_va):
    out = []
    rva = vt_va - base
    while True:
        f = struct.unpack_from("<I", data, rva)[0]
        if not (text_lo <= f < text_hi):
            break
        out.append(f)
        rva += 4
    return out


def calls_in(fva, max_bytes=0x400):
    code = bytes(data[fva - base : fva - base + max_bytes])
    targets = []
    for insn in md.disasm(code, fva):
        if insn.mnemonic == "call" and insn.op_str.startswith("0x"):
            targets.append(int(insn.op_str, 16))
        if insn.mnemonic == "ret":
            break
        if insn.mnemonic == "int3":
            break
    return targets


for name, vt in VTABLES.items():
    fs = vfuncs(vt)
    print(f"=== {name} vtable 0x{vt:X}: {len(fs)} vfuncs ===")
    hist = Counter()
    for i, f in enumerate(fs):
        tgts = calls_in(f)
        hist.update(set(tgts))
        interesting = [t for t in tgts if 0x680000 <= t <= 0x800000]
        print(f"  [{i:2}] 0x{f:X}: calls " + ", ".join(f"0x{t:X}" for t in tgts[:14]))
    print("  -- most common callees --")
    for t, n in hist.most_common(15):
        print(f"    0x{t:X} x{n}")
    print()
