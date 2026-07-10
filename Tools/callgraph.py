"""Disassemble the functions containing given xref addresses; list their calls.

Function start heuristic: scan back for CC padding or push ebp/mov ebp,esp.
"""
import struct
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

EXE = r"C:\Program Files (x86)\Steam\steamapps\common\Fable The Lost Chapters\Fable.exe"

# xref sites from rtti_scan: (label, va)
SITES = [
    ("MeleeWeaponCarried ref#1", 0x5C3BD6),
    ("MeleeWeaponCarried ref#2 (load?)", 0x5C3D55),
    ("LastWeaponEquippedID ref#1", 0x6CA1AA),
    ("LastWeaponEquippedID ref#2", 0x6CA29F),
]

pe = pefile.PE(EXE, fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase
data = pe.get_memory_mapped_image()

md = Cs(CS_ARCH_X86, CS_MODE_32)
md.detail = False

def func_start(va: int) -> int:
    rva = va - base
    # walk back up to 0x2000 bytes looking for padding followed by prologue
    for back in range(1, 0x2000):
        p = rva - back
        if data[p] == 0xCC and data[p + 1] != 0xCC:
            return base + p + 1
        if data[p] == 0xC3 and data[p + 1 : p + 3] == b"\x55\x8b":
            return base + p + 1
    return va - 0x200

for label, site in SITES:
    start = func_start(site)
    rva = start - base
    code = bytes(data[rva : rva + 0x1800])
    calls = []
    end = None
    depth_bytes = 0
    for insn in md.disasm(code, start):
        depth_bytes = insn.address + insn.size - start
        if insn.mnemonic == "call" and insn.op_str.startswith("0x"):
            calls.append((insn.address, int(insn.op_str, 16)))
        if insn.mnemonic == "ret" and insn.address > site:
            end = insn.address
            break
    print(f"[{label}] site 0x{site:X} -> function 0x{start:X}..0x{end or 0:X}")
    seen = set()
    for at, tgt in calls:
        mark = " *" if tgt not in seen else ""
        seen.add(tgt)
        near = "  <-- after string ref" if abs(at - site) < 0x60 and at > site else ""
        print(f"    call 0x{tgt:X} at 0x{at:X}{mark}{near}")
    print()
