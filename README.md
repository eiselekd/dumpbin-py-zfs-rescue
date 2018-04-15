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
 * fuse (llfuse) interface for recovery

This make it possible to read datapools created via i.e.

     zpool create datapool -f -o ashift=12 -O atime=off -O canmount=off -O compression=lz4 -O normalization=formD raidz /dev/loop0 /dev/loop1 /dev/loop2
     zfs create datapool/datadir



