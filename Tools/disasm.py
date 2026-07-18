"""Annotated disassembler for retail Fable.exe.

Disassembles a function and annotates call/jmp targets with names from
docs/data/symbols.tsv (and imports), so you can read control flow without a
full RE tool. Stops at the first ret past the start (or the length cap).

Usage:
    python disasm.py 0x5C8101 [max_bytes]
    python disasm.py 0x5C8101 0x400
"""
import sys, os, struct
import pefile, capstone

EXE = r"C:\Program Files (x86)\Steam\steamapps\common\Fable The Lost Chapters\Fable.exe"
HERE = os.path.dirname(os.path.abspath(__file__))
SYMS_PATH = os.path.join(HERE, "..", "docs", "data", "symbols.tsv")

def load_syms():
    d = {}
    if os.path.exists(SYMS_PATH):
        for line in open(SYMS_PATH, encoding="utf-8"):
            if line.startswith(("#", "addr")):
                continue
            c = line.rstrip("\n").split("\t")
            if len(c) >= 2:
                d[int(c[0], 16)] = c[1]
    return d

SYMS = load_syms()

pe = pefile.PE(EXE, fast_load=True)
pe.parse_data_directories()
base = pe.OPTIONAL_HEADER.ImageBase
image = pe.get_memory_mapped_image()

imp = {}
for e in (pe.DIRECTORY_ENTRY_IMPORT or []):
    dll = e.dll.decode(errors="replace")
    for i in e.imports:
        if i.name:
            imp[i.address] = "%s!%s" % (dll, i.name.decode(errors="replace"))

def rd32(va):
    o = va - base
    if 0 <= o <= len(image) - 4:
        return struct.unpack("<I", image[o:o+4])[0]
    return None

def name_for(va):
    if va in SYMS:
        return SYMS[va]
    if va in imp:
        return imp[va]
    t = rd32(va)
    if t in imp:
        return "thunk->" + imp[t]
    # nearest symbol below (for internal jumps within a function)
    return None

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
md.detail = True

def annotate(ins):
    for op in ins.operands:
        if op.type == capstone.x86.X86_OP_IMM and ins.mnemonic in ("call", "jmp",
                "je","jne","jz","jnz","jg","jl","jge","jle","ja","jb","jae","jbe"):
            n = name_for(op.imm)
            if n:
                return "; -> " + n
            return ""
        if op.type == capstone.x86.X86_OP_MEM and op.mem.base == 0 and op.mem.index == 0:
            slot = op.mem.disp & 0xFFFFFFFF
            if slot in imp:
                return "; -> " + imp[slot]
            if slot in SYMS:
                return "; [%s]" % SYMS[slot]
    return ""

def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    va = int(sys.argv[1], 16)
    cap = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x300
    label = SYMS.get(va, "sub_%08X" % va)
    print("; %s @ 0x%08X  (%d syms loaded)\n" % (label, va, len(SYMS)))
    o = va - base
    count = 0
    for ins in md.disasm(image[o:o+cap], va):
        note = annotate(ins)
        print("  0x%08X  %-8s %-34s %s" % (ins.address, ins.mnemonic, ins.op_str, note))
        count += 1
        if ins.mnemonic == "ret":
            break
        if count > 400:
            break

if __name__ == "__main__":
    main()
