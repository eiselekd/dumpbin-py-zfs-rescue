import struct
import zlib

def gzip_decompress(src,dsize):
    """
    Decompresses src, a bytearray of compressed data.
    """
    d = zlib.decompress(src);
    return d;
