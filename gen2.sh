zpool create datapool -f -o ashift=12 \
      -O atime=off -O canmount=off -O compression=gzip-9 -O normalization=formD raidz /dev/loop0 /dev/loop1 /dev/loop2

zfs create datapool/datadir
zfs mount datapool/datadir
echo "Hello" > /datapool/datadir/test.txt
