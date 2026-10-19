"""Disassemble unpacked police2.exe at a VA: dis.py <va> [count]"""
import sys, pefile, capstone
pe = pefile.PE(__import__('os').path.join(__import__('os').path.dirname(__file__), '..', 'original', 'police2_unpacked.exe'), fast_load=True)
img = pe.get_memory_mapped_image(); base = pe.OPTIONAL_HEADER.ImageBase
va = int(sys.argv[1], 16); n = int(sys.argv[2]) if len(sys.argv) > 2 else 40
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
for i, ins in enumerate(md.disasm(img[va-base:va-base+n*16], va)):
    if i >= n: break
    print(f'{ins.address:08x}  {ins.bytes.hex():20s} {ins.mnemonic} {ins.op_str}')
