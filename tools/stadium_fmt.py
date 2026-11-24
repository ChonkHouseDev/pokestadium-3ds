"""Formatos de Pokemon Stadium: Yay0, PERS-SZP, BinArchive."""
import struct

def yay0(src, off=0):
    assert src[off:off+4] == b'Yay0'
    size, link, chunk = struct.unpack('>III', src[off+4:off+16])
    out = bytearray(); mask_p = off + 16; link_p = off + link; chunk_p = off + chunk
    bits = 0; mask = 0
    while len(out) < size:
        if bits == 0:
            mask = struct.unpack('>I', src[mask_p:mask_p+4])[0]; mask_p += 4; bits = 32
        if mask & 0x80000000:
            out.append(src[chunk_p]); chunk_p += 1
        else:
            v = struct.unpack('>H', src[link_p:link_p+2])[0]; link_p += 2
            dist = (v & 0xFFF) + 1; n = v >> 12
            if n == 0: n = src[chunk_p] + 18; chunk_p += 1
            else: n += 2
            for _ in range(n): out.append(out[-dist])
        mask = (mask << 1) & 0xFFFFFFFF; bits -= 1
    return bytes(out)

def pers_szp(src, off=0):
    """Descomprime un PERS-SZP: Yay0 en header_size, aplica parches (valor, offset), rellena con ceros."""
    assert src[off:off+8] == b'PERS-SZP'
    hsize, d1, d2, npatch = struct.unpack('>IIII', src[off+8:off+24])
    mem = bytearray(yay0(src, off + hsize)); mem += bytes(max(0, d2 - len(mem)))
    p = off + 24
    for _ in range(npatch):
        val, at = struct.unpack('>II', src[p:p+8]); p += 8
        mem[at:at+4] = struct.pack('>I', val)
    return bytes(mem[:d2])

def archive_files(src, off=0):
    n = struct.unpack('>I', src[off+12:off+16])[0]
    return [struct.unpack('>II', src[off+0x10+i*0x10:off+0x18+i*0x10]) for i in range(n)]
