# dumpbin of py-zfs-rescue fork with patches

Fork of https://github.com/hiliev/py-zfs-rescue with the following improvements:

 * lz4 decompression
 * fletcher4 cksum
 * first level child datasets
 * blkptr with embedded data
 * improved block server protocol
 * bigger than 2TB disk support
 * SystemAttributes, bonus type 0x2c
 * variable sectorsize
 