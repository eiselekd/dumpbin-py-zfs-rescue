import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "python-lz4-0.7.0+dfsg/build/lib.linux-x86_64-3.5"))

# size indicator is big endian and depice source size, use own routine
import lz4zfs

def lz4_decompress(src, dlen, dst=None):
    """
    Decompresses src, a bytearray of compressed data.
    The dst argument can be an optional bytearray which will have the output appended.
    If it's None, a new bytearray is created.
    The output bytearray is returned.
    """
    if dst is None:
        dst = bytearray()
    print(str(src))
    b = bytes(src)
    d=lz4zfs.decompress(b,dlen)
    l=len(d)
    if (dlen != l):
        print("[-] decompress size differ from %d, got %d" %(dlen,l))
        raise RuntimeError("decompress size differ from %d, got %d" %(dlen,l))
    else:
        if (dlen < l):
            dst[0:dlen] = d;
        else:
            dst[0:l] = d;
    print(str(dst))
    return dst
