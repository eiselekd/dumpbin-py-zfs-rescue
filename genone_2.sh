zpool create datapoolone -f -o ashift=12 \
      -O atime=off -O canmount=off -O compression=gzip-9 -O normalization=formD /dev/loop3

zfs create datapoolone/datadir
zfs mount datapoolone/datadir
echo "Hello" > /datapoolone/datadir/test.txt
