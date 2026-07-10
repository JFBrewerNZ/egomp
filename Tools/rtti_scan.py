"""RTTI + string-xref scanner for Fable.exe (MSVC x86).

For each target class: locate the RTTI TypeDescriptor, walk back through the
CompleteObjectLocator to the vtable, and dump the virtual function addresses.
For each target string: find code that references its address (imm32 operand).
"""
import sys
import struct
import pefile

EXE = r"C:\Program Files (x86)\Steam\steamapps\common\Fable The Lost Chapters\Fable.exe"

CLASSES = [
    "CTCInventoryClothing",
    "CTCInventoryWeapons",
    "CTCInventoryItem",
    "CCreatureAction_SheatheWeapons",
    "CCreatureAction_SheatheItemToInventory",
]

STRINGS = [
    b"MeleeWeaponCarried",
    b"RangedWeaponCarried",
    b"LastWeaponEquippedID",
    b"PreviouslyWieldedMeleeWeaponDefName",
    b"PreviouslyWieldedRangedWeaponDefName",
]

pe = pefile.PE(EXE, fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase
data = pe.get_memory_mapped_image()  # indexed by RVA

text = next(s for s in pe.sections if s.Name.rstrip(b"\0") == b".text")
text_lo = base + text.VirtualAddress
text_hi = text_lo + text.Misc_VirtualSize

def find_all(needle: bytes):
    out, i = [], 0
    while True:
        i = data.find(needle, i)
        if i < 0:
            return out
        out.append(i)  # RVA
        i += 1

def dword_refs(va: int):
    return find_all(struct.pack("<I", va))

print(f"image base 0x{base:X}, .text 0x{text_lo:X}-0x{text_hi:X}\n")

for cls in CLASSES:
    mangled = b".?AV" + cls.encode() + b"@@"
    hits = find_all(mangled)
    if not hits:
        print(f"[{cls}] type descriptor name NOT FOUND")
        continue
    for name_rva in hits:
        td_va = base + name_rva - 8  # TypeDescriptor starts 8 bytes before name
        # CompleteObjectLocator holds pTypeDescriptor at offset 0x0C
        cols = [rva - 0x0C for rva in dword_refs(td_va)]
        vtables = []
        for col_rva in cols:
            sig = struct.unpack_from("<I", data, col_rva)[0]
            if sig != 0:
                continue
            offset = struct.unpack_from("<I", data, col_rva + 4)[0]
            col_va = base + col_rva
            for ref_rva in dword_refs(col_va):
                vt_va = base + ref_rva + 4  # vtable begins after the COL pointer
                funcs = []
                r = ref_rva + 4
                while True:
                    f = struct.unpack_from("<I", data, r)[0]
                    if not (text_lo <= f < text_hi):
                        break
                    funcs.append(f)
                    r += 4
                vtables.append((offset, vt_va, funcs))
        print(f"[{cls}] TD at 0x{td_va:X}")
        for offset, vt_va, funcs in vtables:
            head = ", ".join(f"0x{f:X}" for f in funcs[:12])
            print(f"    vtable@0x{vt_va:X} (this-offset {offset}) {len(funcs)} vfuncs: {head}{' ...' if len(funcs) > 12 else ''}")
    print()

print("=== string xrefs (imm32 references from anywhere in the image) ===")
for s in STRINGS:
    hits = find_all(s)
    if not hits:
        print(f'"{s.decode()}" NOT FOUND')
        continue
    for str_rva in hits:
        str_va = base + str_rva
        refs = [base + r for r in dword_refs(str_va)]
        code_refs = [r for r in refs if text_lo <= r < text_hi]
        print(f'"{s.decode()}" at 0x{str_va:X}: {len(refs)} refs, {len(code_refs)} in .text: '
              + ", ".join(f"0x{r:X}" for r in code_refs[:8]))
