# dumpbin of py-zfs-rescue fork with patches

Fork of https://github.com/hiliev/py-zfs-rescue with the following improvements:

 * lz4 decompression
 * fletcher4 cksum
 * first level child datasets
 * blkptr with embedded data
 * improved block server protocol
 * bigger than 2TB disk support
 * support SystemAttributes, bonus type 0x2c
 * variable sectorsize
 * fuse (llfuse) interface for recovery

This make it possible to read datapools created via i.e.

     zpool create datapool -f -o ashift=12 -O atime=off -O canmount=off -O compression=lz4 -O normalization=formD raidz /dev/loop0 /dev/loop1 /dev/loop2
     zfs create datapool/datadir


Recuse is done via the following steps:
 * dump partitions of pool via dd 
 * use `zdb -l -u <disk>` and determine the uberblock to use and edit variable TXG in zfs_rescue.py 
 * For partitins greater than 2TB split partition into 1TB chunks and suffix with index `<diskname>.0, <diskname>.1, ...`. use block_proxy json init syntax ` <vdev> : [ "<diskname>.0", "<diskname>.1",... ]`
 * edit BLK_PROXY_ADDR, INITIALDISKS, BLK_INITIAL_DISK
 * run zfs_rescue.py to determine the dataset to use and edit list variable DS_TO_ARCHIVE
 * edit MOUNTPOINT for fuse filesystem directory via which to access dataset and `rsync -avPX <MOUNTPOINT>/. <MOUNTPOINT>.bck/.``to recover, hopefully... :-)
 
 
