"""Recover Delphi (XE2, 32-bit) class/method names from VMT RTTI of the unpacked police2.exe."""
import struct, sys, pefile, json

path = sys.argv[1]
pe = pefile.PE(path)
base = pe.OPTIONAL_HEADER.ImageBase
img = pe.get_memory_mapped_image()
def u32(va): return struct.unpack_from('<I', img, va - base)[0]
def u16(va): return struct.unpack_from('<H', img, va - base)[0]
def sstr(va):
    n = img[va - base]
    return img[va - base + 1: va - base + 1 + n].decode('latin1')
lo, hi = base + 0x1000, base + 0x21a000 + 0x16b0
def valid(va): return lo <= va < hi

syms = {}
classes = {}
for off in range(0x1000, 0x21a000, 4):
    va = base + off
    try:
        if u32(va) != va + 88: continue
        cn = u32(va + 88 - 56)
        if not valid(cn): continue
        name = sstr(cn)
        if not name or not all(c.isalnum() or c in '_<>.,' for c in name): continue
    except Exception:
        continue
    vmt = va + 88
    classes[vmt] = name
    syms.setdefault(u32(vmt - 4), f'{name}.Destroy')
    mt = u32(vmt - 64)
    if mt and valid(mt):
        p = mt
        cnt = u16(p); p += 2
        for _ in range(cnt):
            ln = u16(p); code = u32(p + 2); mn = sstr(p + 6)
            syms[code] = f'{name}.{mn}'
            p += ln
        try:
            excnt = u16(p); p += 2
            for _ in range(excnt):
                ent = u32(p); p += 8
                if valid(ent):
                    code = u32(ent + 2); mn = sstr(ent + 6)
                    if valid(code): syms.setdefault(code, f'{name}.{mn}')
        except Exception:
            pass
json.dump({hex(k): v for k, v in sorted(syms.items())}, open(sys.argv[2], 'w'), indent=0)
json.dump({hex(k): v for k, v in sorted(classes.items())}, open(sys.argv[2].replace('.json', '_classes.json'), 'w'), indent=0)
print(len(classes), 'classes', len(syms), 'methods')
