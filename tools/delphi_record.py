"""Dump field offsets of a Delphi XE2 record type from its extended RTTI: delphi_record.py <TypeName>"""
import sys, struct, pefile
pe = pefile.PE(__import__('os').path.join(__import__('os').path.dirname(__file__), '..', 'original', 'police2_unpacked.exe'), fast_load=True)
img = pe.get_memory_mapped_image(); base = 0x400000
name = sys.argv[1].encode()
pat = bytes([0x0e, len(name)]) + name     # tkRecord = 14
i = img.find(pat)
while i != -1:
    p = i + 2 + len(name)
    recsize, mcount = struct.unpack_from('<II', img, p); p += 8
    p += mcount * 8                       # managed fields: TypeRef, Offset
    nops = img[p]; p += 1 + nops * 4      # record ops
    cnt = struct.unpack_from('<I', img, p)[0]; p += 4
    print(f'{sys.argv[1]} @ {hex(i+base)} size={hex(recsize)} fields={cnt}')
    for _ in range(min(cnt, 200)):
        tref, off, flags = struct.unpack_from('<IIB', img, p); p += 9
        ln = img[p]; fname = img[p+1:p+1+ln].decode('latin1'); p += 1 + ln
        alen = struct.unpack_from('<H', img, p)[0]; p += alen
        tname = '?'
        try:
            ti = struct.unpack_from('<I', img, tref - base)[0] - base
            tname = img[ti+2:ti+2+img[ti+1]].decode('latin1')
        except Exception: pass
        print(f'  +{off:#06x} {fname}: {tname}')
    break
