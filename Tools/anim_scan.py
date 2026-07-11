"""Animation-system RE sweep for Fable.exe.

1. Enumerate ALL RTTI classes whose name contains an animation keyword.
2. For key classes (CCreatureAction_PlayAnimation etc.) dump vtable + find
   .text code refs to the vtable address (= ctor candidates stamping vptr).
3. Scan for animation-related strings and their code xrefs.
"""
import struct
import pefile

EXE = r"C:\Program Files (x86)\Steam\steamapps\common\Fable The Lost Chapters\Fable.exe"

pe = pefile.PE(EXE, fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase
data = pe.get_memory_mapped_image()

text = next(s for s in pe.sections if s.Name.rstrip(b"\0") == b".text")
text_lo = base + text.VirtualAddress
text_hi = text_lo + text.Misc_VirtualSize


def find_all(needle: bytes, start=0):
    out, i = [], start
    while True:
        i = data.find(needle, i)
        if i < 0:
            return out
        out.append(i)
        i += 1


def dword_refs(va: int):
    return find_all(struct.pack("<I", va))


print(f"image base 0x{base:X}, .text 0x{text_lo:X}-0x{text_hi:X}\n")

# ---- 1. all RTTI class names containing keywords ----
KEYWORDS = [b"Anim", b"ShapeManager", b"Pose", b"Motion"]
print("=== RTTI classes matching keywords ===")
names = set()
for kw in KEYWORDS:
    for rva in find_all(b".?AV"):
        end = data.find(b"@@", rva)
        if end < 0 or end - rva > 120:
            continue
        nm = bytes(data[rva + 4 : end])
        if kw in nm:
            names.add(nm)
for nm in sorted(names):
    print("  " + nm.decode(errors="replace"))
print()

# ---- 2. vtable + ctor candidates for key classes ----
KEY = [
    "CCreatureAction_PlayAnimation",
    "CTCShapeManager",
    "CAnimComponentFlags",
]
print("=== key classes: vtables + code refs (ctor candidates) ===")
for cls in KEY:
    mangled = b".?AV" + cls.encode() + b"@@"
    hits = find_all(mangled)
    if not hits:
        print(f"[{cls}] NOT FOUND")
        continue
    for name_rva in hits:
        td_va = base + name_rva - 8
        cols = [rva - 0x0C for rva in dword_refs(td_va)]
        print(f"[{cls}] TD 0x{td_va:X}")
        for col_rva in cols:
            sig = struct.unpack_from("<I", data, col_rva)[0]
            if sig != 0:
                continue
            this_off = struct.unpack_from("<I", data, col_rva + 4)[0]
            col_va = base + col_rva
            for ref_rva in dword_refs(col_va):
                vt_va = base + ref_rva + 4
                # count vfuncs
                funcs = []
                r = ref_rva + 4
                while True:
                    f = struct.unpack_from("<I", data, r)[0]
                    if not (text_lo <= f < text_hi):
                        break
                    funcs.append(f)
                    r += 4
                # code refs to vtable VA = ctor/dtor candidates
                coderefs = [base + r2 for r2 in dword_refs(vt_va)
                            if text_lo <= base + r2 < text_hi]
                print(f"  vtable 0x{vt_va:X} (this-off {this_off}) {len(funcs)} vfuncs")
                print(f"    first vfuncs: " + ", ".join(f"0x{f:X}" for f in funcs[:10]))
                print(f"    .text refs (ctor/dtor sites): "
                      + ", ".join(f"0x{r2:X}" for r2 in coderefs[:20]))
    print()

# ---- 3. animation strings ----
print("=== animation strings + .text xrefs ===")
STRINGS = [
    b"PlayAnimation",
    b"CurrentAnimation",
    b"AnimationName",
    b"AnimName",
    b"ANIM_",
    b"PlayedAnimation",
    b"AnimationPlaying",
    b"LoopAnimation",
]
for s in STRINGS:
    hits = find_all(s)
    shown = 0
    for str_rva in hits:
        # only strings that START here (prev byte is NUL or non-print)
        prev = data[str_rva - 1]
        if 0x20 <= prev < 0x7F:
            continue
        # read the full string
        end = str_rva
        while data[end] >= 0x20 and data[end] < 0x7F:
            end += 1
        full = bytes(data[str_rva:end]).decode(errors="replace")
        str_va = base + str_rva
        refs = [base + r for r in dword_refs(str_va)]
        code_refs = [r for r in refs if text_lo <= r < text_hi]
        if not refs:
            continue
        print(f'  "{full}" @0x{str_va:X}: {len(code_refs)} .text refs: '
              + ", ".join(f"0x{r:X}" for r in code_refs[:6]))
        shown += 1
        if shown >= 25:
            print(f"  ... (more {s.decode()} strings)")
            break
    if not hits:
        print(f'  "{s.decode()}" NOT FOUND')
