"""Dump C++ vtables from retail Fable.exe and gauge whether their methods are
real or stubbed.

For each vtable: list every virtual function slot, flag trivial stubs
(return-0 / scalar-deleting-destructor shape), count instructions, and note any
Ws2_32/winsock or other import calls in each method body. Also finds the ctor
sites that reference each vtable. This is how the dormant-netcode / co-op-TC
investigation was done (docs/engine-internals.md).

Usage:
    python vtable_probe.py                       # the default set below
    python vtable_probe.py CFoo=0x01234567 ...   # arbitrary vtables

Look up a class's vtable address in docs/data/rtti-classes.tsv.
"""
import pefile, capstone, struct, sys

EXE = r"C:\Program Files (x86)\Steam\steamapps\common\Fable The Lost Chapters\Fable.exe"
VTABLES = {
    "CNetworkClient": 0x0122EDAC,
    "CNetworkServer": 0x0122ED30,
}
# Override/extend from argv: NAME=0xADDR tokens.
_cli = {}
for _a in sys.argv[1:]:
    if "=" in _a:
        _k, _v = _a.split("=", 1)
        try:
            _cli[_k] = int(_v, 16)
        except ValueError:
            pass
if _cli:
    VTABLES = _cli

pe = pefile.PE(EXE, fast_load=True)
pe.parse_data_directories()
base = pe.OPTIONAL_HEADER.ImageBase
image = pe.get_memory_mapped_image()

def rd32(va):
    off = va - base
    if off < 0 or off + 4 > len(image):
        return None
    return struct.unpack("<I", image[off:off+4])[0]

def in_text(va):
    for s in pe.sections:
        if s.Name.rstrip(b"\0") == b".text":
            lo = base + s.VirtualAddress
            hi = lo + s.Misc_VirtualSize
            return lo <= va < hi
    return False

# Build import address -> "dll!func" map (both IAT slot targets and thunks).
imp_by_va = {}
for entry in (pe.DIRECTORY_ENTRY_IMPORT or []):
    dll = entry.dll.decode(errors="replace")
    for imp in entry.imports:
        if imp.name:
            imp_by_va[imp.address] = "%s!%s" % (dll, imp.name.decode(errors="replace"))

# Winsock/network-ish imports we care about.
NET_HINT = ("Ws2_32", "WSOCK", "wsock", "send", "recv", "socket", "connect",
            "bind", "listen", "accept", "WSA")

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
md.detail = True

def func_len_guess(va, cap=0x600):
    """Read forward until a ret followed by alignment/int3, capped."""
    off = va - base
    return image[off:off+cap]

def scan_func(va):
    """Return set of import strings called, and whether it looks like a stub."""
    code = func_len_guess(va)
    calls = set()
    n_ins = 0
    stub_ret = None
    for ins in md.disasm(code, va):
        n_ins += 1
        if ins.mnemonic == "call" and ins.operands:
            op = ins.operands[0]
            tgt = None
            if op.type == capstone.x86.X86_OP_IMM:
                tgt = op.imm
            elif op.type == capstone.x86.X86_OP_MEM and op.mem.base == 0 and op.mem.index == 0:
                # call [imm]  -> IAT slot
                slot = op.mem.disp & 0xFFFFFFFF
                if slot in imp_by_va:
                    calls.add(imp_by_va[slot])
                    continue
                tgt = rd32(slot)
            if tgt is not None:
                if tgt in imp_by_va:
                    calls.add(imp_by_va[tgt])
                elif rd32(tgt) in imp_by_va:  # jmp-thunk
                    calls.add(imp_by_va[rd32(tgt)])
        if ins.mnemonic == "ret":
            if n_ins <= 2 and stub_ret is None:
                stub_ret = ins.op_str or "0"
            break
    return calls, n_ins, stub_ret

for cls, vt in VTABLES.items():
    col = rd32(vt - 4)  # CompleteObjectLocator pointer sits just above the vtable
    print("\n=== %s  vtable @ 0x%08X  (COL 0x%08X) ===" % (cls, vt, col or 0))
    slot = 0
    net_total = set()
    while slot < 64:
        fva = rd32(vt + slot*4)
        if fva is None or not in_text(fva):
            break
        calls, n_ins, stub = scan_func(fva)
        net = sorted(c for c in calls if any(h in c for h in NET_HINT))
        net_total |= set(net)
        tag = ""
        if stub is not None:
            tag = "  <stub ret %s>" % stub
        extra = ""
        if net:
            extra = "  NET:{%s}" % ", ".join(x.split("!")[1] for x in net)
        elif calls:
            some = sorted(calls)[:3]
            extra = "  calls:{%s}" % ", ".join(x.split("!")[1] for x in some)
        print("  [%2d] 0x%08X  ins~%-3d%s%s" % (slot, fva, n_ins, tag, extra))
        slot += 1
    print("  -> vtable size ~%d; net imports seen: %s"
          % (slot, ", ".join(sorted(x.split("!")[1] for x in net_total)) or "none"))

# Also: who references these vtables (i.e. the ctors) — find imm32 == vt.
print("\n=== references to the vtables (mov [reg], imm32 vt = ctor sites) ===")
text = None
for s in pe.sections:
    if s.Name.rstrip(b"\0") == b".text":
        text = (base + s.VirtualAddress, image[s.VirtualAddress:s.VirtualAddress+s.Misc_VirtualSize])
for cls, vt in VTABLES.items():
    needle = struct.pack("<I", vt)
    tbase, tdata = text
    hits = []
    start = 0
    while True:
        i = tdata.find(needle, start)
        if i < 0:
            break
        hits.append(tbase + i)
        start = i + 1
    print("  %s vt 0x%08X referenced at: %s"
          % (cls, vt, ", ".join("0x%08X" % h for h in hits[:8]) or "none"))
