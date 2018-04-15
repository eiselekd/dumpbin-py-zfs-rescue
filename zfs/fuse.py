#!/usr/bin/env python3

from __future__ import division, print_function, absolute_import

import cmd, sys
import os
import sys
import errno
import stat
import logging
import errno
import llfuse

from zfs.dataset import zfsnode

class wrap():
    def __init__(self, f):
        self.fh = f

class zfsfuse(llfuse.Operations):
    def __init__(self, dataset):
        super(zfsfuse, self).__init__()
        self.dataset = dataset
        self.m = dict()
        self.hello_name = b"message"
        self.hello_inode = llfuse.ROOT_INODE+1
        self.hello_data = b"hello world\n"

    def register(self, p, e):
        a = self._path_join(p, e.name())
        self.m[a] = e;
        return a

    def lookup(selfp):
        return self.m[p]
    
    def _path_join(self, prefix, partial):
        if partial.startswith("/"):
            partial = partial[1:]
        path = os.path.join(prefix, partial)
        return path

    def getattr(self, inode, ctx=None):
        entry = llfuse.EntryAttributes()
        if inode == llfuse.ROOT_INODE:
            entry.st_mode = (stat.S_IFDIR | 0o755)
            entry.st_size = 0
        elif inode == self.hello_inode:
            entry.st_mode = (stat.S_IFREG | 0o644)
            entry.st_size = len(self.hello_data)
        else:
            raise llfuse.FUSEError(errno.ENOENT)

        stamp = int(1438467123.985654 * 1e9)
        entry.st_atime_ns = stamp
        entry.st_ctime_ns = stamp
        entry.st_mtime_ns = stamp
        entry.st_gid = os.getgid()
        entry.st_uid = os.getuid()
        entry.st_ino = inode

        return entry

    def lookup(self, parent_inode, name, ctx=None):
        if parent_inode != llfuse.ROOT_INODE or name != self.hello_name:
            raise llfuse.FUSEError(errno.ENOENT)
        return self.getattr(self.hello_inode)

    def opendir(self, inode, ctx):
        if inode != llfuse.ROOT_INODE:
            raise llfuse.FUSEError(errno.ENOENT)
        return inode

    def readdir(self, fh, off):
        assert fh == llfuse.ROOT_INODE

        # only one entry
        if off == 0:
            yield (self.hello_name, self.getattr(self.hello_inode), 1)

    def open(self, inode, flags, ctx):
        if inode != self.hello_inode:
            raise llfuse.FUSEError(errno.ENOENT)
        if flags & os.O_RDWR or flags & os.O_WRONLY:
            raise llfuse.FUSEError(errno.EPERM)
        return inode

    def read(self, fh, off, size):
        assert fh == self.hello_inode
        return self.hello_data[off:off+size]

class mountpoint():
    def __init__(self, mountpoint, dataset):
        self.mountpoint = mountpoint
        self.dataset = dataset
        print("[+] mount fuse at " + mountpoint)
    
    def mount(self):
        fs = zfsfuse(self.dataset)
        fuse_options = set(llfuse.default_options)
        fuse_options.add('fsname=zfsrescue')
        llfuse.init(fs, self.mountpoint, fuse_options)
        try:
            llfuse.main(workers=1)
        except:
            llfuse.close(unmount=False)
            raise
        llfuse.close()

